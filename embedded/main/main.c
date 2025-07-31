#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mfrc522.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "lcd_i2c.h"
#include "esp_timer.h"

#define WIFI_SSID           "MOB-ALTOS"
#define WIFI_PASSWORD       "mob3876150"
#define MQTT_BROKER_URL     "mqtt://192.168.18.73"
#define MQTT_USERNAME       "calebe"
#define MQTT_PASSWORD       "8811"
#define MQTT_TOPIC          "rfid/scanner/uid"
#define MQTT_TOPIC_RESPONSE "rfid/scanner/response"
#define LCD_MESSAGE_TIMEOUT_MS 5000
#define RFID_DEBOUNCE_MS    3000

static const char* TAG = "RFID_MQTT_PROJECT";
static esp_mqtt_client_handle_t client = NULL;
static SemaphoreHandle_t lcd_mutex;
static esp_timer_handle_t lcd_timeout_timer;

static void lcd_timeout_callback(void* arg) {
    xSemaphoreTake(lcd_mutex, portMAX_DELAY);
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print_str(" Storege Track  ");
    xSemaphoreGive(lcd_mutex);
}

void show_await_message() {
    xSemaphoreTake(lcd_mutex, portMAX_DELAY);
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print_str(" Storege Track  ");
    xSemaphoreGive(lcd_mutex);
}

void show_temp_message(const char* line1, const char* line2) {
    xSemaphoreTake(lcd_mutex, portMAX_DELAY);
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_print_str(line1);
    lcd_set_cursor(1, 0);
    lcd_print_str(line2);
    xSemaphoreGive(lcd_mutex);
    esp_timer_start_once(lcd_timeout_timer, LCD_MESSAGE_TIMEOUT_MS * 1000);
}


static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Tentando reconectar ao Wi-Fi...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASSWORD, }, };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED: Conectado ao broker!");
            esp_mqtt_client_subscribe(client, MQTT_TOPIC_RESPONSE, 1);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA: MENSAGEM RECEBIDA!");
            ESP_LOGI(TAG, "TOPICO: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "DADOS: %.*s", event->data_len, event->data);
            if (strncmp(event->topic, MQTT_TOPIC_RESPONSE, event->topic_len) == 0) {
                char line1[17] = "";
                char line2[17] = "";

                char* data_copy = malloc(event->data_len + 1);
                if (data_copy == NULL) {
                    ESP_LOGE(TAG, "Falha ao alocar memoria para dados MQTT");
                    break;
                }
                memcpy(data_copy, event->data, event->data_len);
                data_copy[event->data_len] = '\0';

                cJSON *json = cJSON_Parse(data_copy);
                free(data_copy);

                if (json) {
                    cJSON *nome = cJSON_GetObjectItemCaseSensitive(json, "nome");
                    cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "status");
                    cJSON *erro = cJSON_GetObjectItemCaseSensitive(json, "erro");

                    if (nome && status) {
                        snprintf(line1, sizeof(line1), "%.16s", nome->valuestring);
                        snprintf(line2, sizeof(line2), "Sts: %.11s", status->valuestring);
                    } else if (erro) {
                        snprintf(line1, sizeof(line1), "ERRO");
                        snprintf(line2, sizeof(line2), "%.16s", erro->valuestring);
                    }
                    cJSON_Delete(json);
                } else {
                    snprintf(line1, sizeof(line1), "Erro JSON");
                    snprintf(line2, sizeof(line2), "Formato invalido");
                }
                show_temp_message(line1, line2);
            }
            break;
        default:
            break;
    }
}
static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,
        .credentials = { .username = MQTT_USERNAME, .authentication.password = MQTT_PASSWORD, },
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static uint64_t last_scanned_uid = 0;
static int64_t last_scan_time = 0;
static void rc522_handler(void* arg, esp_event_base_t base, int32_t id, void* event_data) {
    if (id == RC522_EVENT_TAG_SCANNED) {
        rc522_event_data_t* data = (rc522_event_data_t*) event_data;
        rc522_tag_t* tag = (rc522_tag_t*) data->ptr;
        int64_t now = esp_timer_get_time();

        if (tag->serial_number == last_scanned_uid &&
            (now - last_scan_time) < (RFID_DEBOUNCE_MS * 1000)) {
            return;
        }

        last_scanned_uid = tag->serial_number;
        last_scan_time = now;

        esp_timer_stop(lcd_timeout_timer);
        show_temp_message("Lendo...", "");

        if (client) {
            char uid_hex_string[21];
            snprintf(uid_hex_string, sizeof(uid_hex_string), "%llX", tag->serial_number);

            char json_payload[100];
            snprintf(json_payload, sizeof(json_payload),
                     "{\"uid\":\"%s\",\"leitorId\":\"ESP32_LEITOR_01\"}",
                     uid_hex_string);

            esp_mqtt_client_publish(client, MQTT_TOPIC, json_payload, 0, 1, 0);
        }
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(lcd_module_init());
    lcd_mutex = xSemaphoreCreateMutex();

    const esp_timer_create_args_t lcd_timer_args = {
            .callback = &lcd_timeout_callback,
            .name = "lcd_timeout"
    };
    ESP_ERROR_CHECK(esp_timer_create(&lcd_timer_args, &lcd_timeout_timer));

    show_await_message();
    vTaskDelay(pdMS_TO_TICKS(500));

    wifi_init_sta();
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi conectado. Iniciando MQTT...");
        mqtt_app_start();
    } else {
        ESP_LOGE(TAG, "Falha ao conectar ao Wi-Fi.");
        show_temp_message("Erro no WiFi", "Reinicie");
        return; 
    }

    rc522_config_t config = {
        .transport = RC522_TRANSPORT_SPI,
        .spi = {
            .host = VSPI_HOST,
            .miso_gpio = 19,
            .mosi_gpio = 23,
            .sck_gpio = 18,
            .sda_gpio = 15
        },
    };

    rc522_handle_t scanner;
    ESP_ERROR_CHECK(rc522_create(&config, &scanner));
    ESP_ERROR_CHECK(rc522_register_events(scanner, RC522_EVENT_TAG_SCANNED, rc522_handler, NULL));
    ESP_ERROR_CHECK(rc522_start(scanner));

    ESP_LOGI(TAG, "Sistema iniciado e pronto.");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
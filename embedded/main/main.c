#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mfrc522.h" 
#include "mqtt_client.h"

#define WIFI_SSID      "MOB-ALTOS"
#define WIFI_PASSWORD  "mob3876150"
#define MQTT_BROKER_URL "mqtt://192.168.18.73" 
#define MQTT_USERNAME  "calebe"
#define MQTT_PASSWORD  "8811"
#define MQTT_TOPIC     "rfid/scanner/uid"

static const char* TAG = "RFID_MQTT_PROJECT";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;

static esp_mqtt_client_handle_t client = NULL;

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
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi_init_sta finalizado.");
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        default:
            ESP_LOGI(TAG, "Outro evento MQTT, id=%d", event->event_id);
            break;
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,
        .credentials = {
            .username = MQTT_USERNAME,
            .authentication.password = MQTT_PASSWORD,
        },
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static void rc522_handler(void* arg, esp_event_base_t base, int32_t id, void* event_data) {
    rc522_event_data_t* data = (rc522_event_data_t*) event_data;
    if (id == RC522_EVENT_TAG_SCANNED) {
        rc522_tag_t* tag = (rc522_tag_t*) data->ptr;
        ESP_LOGI(TAG, "Cartão lido! UID decimal: %llu", tag->serial_number);
        if (client) {
            char uid_hex_string[21];
            char json_payload[100]; 

            snprintf(uid_hex_string, sizeof(uid_hex_string), "%llX", tag->serial_number);
            
            snprintf(json_payload, sizeof(json_payload), "{\"uid\": \"%s\", \"leitorId\": \"ESP32_LEITOR_01\"}", uid_hex_string);
            
            ESP_LOGI(TAG, "Enviando JSON via MQTT: %s", json_payload);
            
            esp_mqtt_client_publish(client, MQTT_TOPIC, json_payload, 0, 1, 0);
        }
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_sta();
    mqtt_app_start();

    rc522_config_t config = {
        .transport = RC522_TRANSPORT_SPI,
        .spi = { .host = VSPI_HOST, .miso_gpio = 19, .mosi_gpio = 23, .sck_gpio = 18, .sda_gpio = 15, .bus_is_initialized = false, },
    };
    rc522_handle_t scanner;
    ESP_ERROR_CHECK(rc522_create(&config, &scanner));
    ESP_ERROR_CHECK(rc522_register_events(scanner, RC522_EVENT_TAG_SCANNED, rc522_handler, NULL));
    ESP_ERROR_CHECK(rc522_start(scanner));
    
    ESP_LOGI(TAG, "Sistema iniciado. Aguardando cartões...");
}
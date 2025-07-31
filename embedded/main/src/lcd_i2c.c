#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lcd_i2c.h" 

#define ACK_CHECK_EN 0x1  
static const char *TAG_LCD = "lcd_module"; 
static bool i2c_initialized_flag = false; 

static esp_err_t i2c_bus_init(void) {
    if (i2c_initialized_flag) {
        return ESP_OK;
    }
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = LCD_I2C_MASTER_SDA_IO,       
        .scl_io_num = LCD_I2C_MASTER_SCL_IO,       
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = LCD_I2C_MASTER_FREQ_HZ, 
    };

    esp_err_t err = i2c_param_config(LCD_I2C_MASTER_NUM, &conf); 
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LCD, "Falha ao configurar parâmetros I2C: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(LCD_I2C_MASTER_NUM, conf.mode, 
                             LCD_I2C_MASTER_RX_BUF_DISABLE, 
                             LCD_I2C_MASTER_TX_BUF_DISABLE, 
                             0); 
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LCD, "Falha ao instalar driver I2C: %s", esp_err_to_name(err));
        return err;
    }
    i2c_initialized_flag = true;
    ESP_LOGI(TAG_LCD, "Driver I2C inicializado com sucesso (API Antiga) para a porta %d", LCD_I2C_MASTER_NUM);
    return ESP_OK;
}

static esp_err_t lcd_i2c_send_buffer(uint8_t *data_wr, size_t size) {
    if (!i2c_initialized_flag) {
        ESP_LOGE(TAG_LCD, "Driver I2C não inicializado antes de enviar buffer!");
        return ESP_FAIL;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(LCD_I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000)); 
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_LCD, "Falha ao enviar dados I2C para o LCD (end: 0x%X): %s", LCD_I2C_ADDRESS, esp_err_to_name(ret));
    }
    return ret;
}

static void lcd_pulse_enable(uint8_t data_with_backlight) {
    uint8_t buffer_en[1];
    uint8_t buffer_dis[1];

    buffer_en[0] = data_with_backlight | LCD_ENABLE_BIT;
    lcd_i2c_send_buffer(buffer_en, 1);
    vTaskDelay(pdMS_TO_TICKS(1)); 

    buffer_dis[0] = data_with_backlight & ~LCD_ENABLE_BIT;
    lcd_i2c_send_buffer(buffer_dis, 1);
    vTaskDelay(pdMS_TO_TICKS(1)); 
}

static void lcd_write_nibble(uint8_t nibble_val, uint8_t mode) {
    uint8_t data_to_send = (nibble_val & 0xF0) | mode | LCD_BACKLIGHT;
    lcd_pulse_enable(data_to_send);
}

static void lcd_send_byte(uint8_t data, uint8_t mode) {
    lcd_write_nibble(data, mode);         
    lcd_write_nibble(data << 4, mode);    
}

static void lcd_send_command(uint8_t cmd) {
    lcd_send_byte(cmd, 0); 
}

static void lcd_send_data(uint8_t data) {
    lcd_send_byte(data, LCD_REGISTER_SELECT_BIT); 
}

esp_err_t lcd_module_init(void) {
    esp_err_t err = i2c_bus_init(); 
    if (err != ESP_OK) {
        return err; 
    }

    ESP_LOGI(TAG_LCD, "Inicializando display LCD...");
    vTaskDelay(pdMS_TO_TICKS(50)); 

    lcd_write_nibble(0x30, 0); 
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x30, 0); 
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write_nibble(0x30, 0); 
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write_nibble(0x20, 0); 
    vTaskDelay(pdMS_TO_TICKS(1));

    lcd_send_command(LCD_FUNCTION_SET | LCD_4BIT_MODE | LCD_2LINE | LCD_5x8DOTS);
    lcd_send_command(LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON | LCD_CURSOR_OFF | LCD_BLINK_OFF);
    lcd_clear(); 
    lcd_send_command(LCD_ENTRY_MODE_SET | LCD_ENTRY_LEFT | LCD_ENTRY_SHIFT_DECREMENT);
    
    ESP_LOGI(TAG_LCD, "Display LCD inicializado com sucesso.");
    return ESP_OK;
}

void lcd_clear(void) {
    lcd_send_command(LCD_CLEAR_DISPLAY);
    vTaskDelay(pdMS_TO_TICKS(2)); 
}

void lcd_set_cursor(uint8_t row, uint8_t col) {
    const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54}; 
    uint8_t target_row = row;
    if (target_row >= 2) { 
        target_row = (target_row % 2); 
    }
    lcd_send_command(LCD_SET_DDRAM_ADDR | (col + row_offsets[target_row]));
}

void lcd_print_char(char c) {
    lcd_send_data((uint8_t)c);
}

void lcd_print_str(const char *str) {
    while (*str) {
        lcd_print_char(*str++);
    }
}
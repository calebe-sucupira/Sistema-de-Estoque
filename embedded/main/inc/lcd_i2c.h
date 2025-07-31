#ifndef LCD_I2C_H
#define LCD_I2C_H

#include <stdint.h>
#include "driver/i2c.h" 
#include "esp_err.h"    

// --- Configuração da porta I2C para o LCD (USADA PELO lcd_i2c.c) ---
#define LCD_I2C_MASTER_SCL_IO       14
#define LCD_I2C_MASTER_SDA_IO       13
#define LCD_I2C_MASTER_NUM          I2C_NUM_0 
#define LCD_I2C_MASTER_FREQ_HZ      100000
#define LCD_I2C_MASTER_TX_BUF_DISABLE 0
#define LCD_I2C_MASTER_RX_BUF_DISABLE 0

// --- Configurações Específicas do LCD ---
#define LCD_I2C_ADDRESS 0x27 

// Comandos do LCD
#define LCD_CLEAR_DISPLAY           0x01
#define LCD_RETURN_HOME             0x02
#define LCD_ENTRY_MODE_SET          0x04
#define LCD_DISPLAY_CONTROL         0x08
#define LCD_CURSOR_SHIFT            0x10 
#define LCD_FUNCTION_SET            0x20
#define LCD_SET_CGRAM_ADDR          0x40 
#define LCD_SET_DDRAM_ADDR          0x80

// Flags para entry mode set
#define LCD_ENTRY_LEFT              0x02
#define LCD_ENTRY_SHIFT_DECREMENT   0x00

// Flags para display on/off control
#define LCD_DISPLAY_ON              0x04
#define LCD_CURSOR_OFF              0x00
#define LCD_BLINK_OFF               0x00

// Flags para function set
#define LCD_4BIT_MODE               0x00
#define LCD_2LINE                   0x08
#define LCD_5x8DOTS                 0x00

// Bits de controle para o expansor I2C (PCF8574 comum)
#define LCD_BACKLIGHT               0x08 // Bit P3 (Backlight)
#define LCD_ENABLE_BIT              0x04 // Bit P2 (Enable)
#define LCD_REGISTER_SELECT_BIT     0x01 // Bit P0 (RS)
// O bit P1 (RW) é geralmente aterrado para escrita.

esp_err_t lcd_module_init(void);
void lcd_clear(void);
void lcd_set_cursor(uint8_t row, uint8_t col);
void lcd_print_char(char c);
void lcd_print_str(const char *str);

#endif 
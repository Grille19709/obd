/*
 * DisplayConfig.h - JC4827W543 (ESP32_4827A043_QSPI)
 * NV3041A via ESP32 QSPI (confirmed from official demo)
 */

#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Display: NV3041A via ESP32 QSPI (4-bit)
// Pins from Dev_Device_Pins/PINS_JC4827W543.h (official)
#define GFX_BL 1
Arduino_DataBus *dispBus = new Arduino_ESP32QSPI(
    45 /* CS */, 47 /* SCK */,
    21 /* D0 */, 48 /* D1 */, 40 /* D2 */, 39 /* D3 */);
Arduino_NV3041A *gfx = new Arduino_NV3041A(dispBus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);

// Touch: GT911 I2C (official)
#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST 38

#define SCREEN_WIDTH   480
#define SCREEN_HEIGHT  272

// Colors RGB565
#define COLOR_BG          0x18C5
#define COLOR_BG_DARKER   0x1083
#define COLOR_ACCENT      0xEB2C
#define COLOR_ACCENT2     0x09AC
#define COLOR_GREEN       0x4E74
#define COLOR_YELLOW      0xF608
#define COLOR_ORANGE      0xF244
#define COLOR_RED         0xE267
#define COLOR_WHITE       0xFFFF
#define COLOR_GRAY        0x8C51
#define COLOR_LIGHT_GRAY  0xCE79
#define COLOR_TEXT        0xEF7D
#define COLOR_BATTERY_OK  0x4E74
#define COLOR_BATTERY_MED 0xF608
#define COLOR_BATTERY_LOW 0xEB2C

#endif
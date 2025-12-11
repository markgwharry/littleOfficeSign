// User_Setup.h for ESP32 Cheap Yellow Display (CYD) with ILI9341
// This file configures TFT_eSPI library for the CYD board
//
// IMPORTANT: Copy this file to your Arduino/libraries/TFT_eSPI/ folder
// and rename it to User_Setup.h (replacing the existing one)
// OR use User_Setup_Select.h to include this file

#define USER_SETUP_INFO "CYD_ILI9341_Office_Sign"

// =====================
// Display driver
// =====================
#define ILI9341_DRIVER

// =====================
// Display dimensions
// =====================
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// =====================
// ESP32 pins for TFT (HSPI)
// =====================
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1  // Connected to EN/Reset, use -1

// Backlight control
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// =====================
// Touch pins (XPT2046 on VSPI)
// =====================
#define TOUCH_CS 33  // Note: CYD uses pin 33 for touch CS

// =====================
// SPI frequency
// =====================
#define SPI_FREQUENCY       40000000  // 40 MHz (stable for CYD)
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// =====================
// Fonts to include
// =====================
#define LOAD_GLCD   // Standard Adafruit 5x7 font
#define LOAD_FONT2  // Small 16 pixel font
#define LOAD_FONT4  // Medium 26 pixel font
#define LOAD_FONT6  // Large 48 pixel numeric font
#define LOAD_FONT7  // 7 segment 48 pixel font
#define LOAD_FONT8  // Large 75 pixel font
#define LOAD_GFXFF  // FreeFonts

// Enable smooth fonts (anti-aliased)
#define SMOOTH_FONT

// =====================
// Optional features
// =====================
// Use HSPI port for display
#define USE_HSPI_PORT

// Enable DMA for faster screen updates (ESP32)
// #define TFT_SPI_DMA  // Uncomment if needed

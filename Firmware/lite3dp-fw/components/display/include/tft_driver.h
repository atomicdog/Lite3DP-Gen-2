#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#define TFT_WIDTH   320
#define TFT_HEIGHT  426

/* Common ILI9481 commands */
#define ILI9481_SLPOUT  0x11
#define ILI9481_DISPON  0x29
#define ILI9481_CASET   0x2A
#define ILI9481_PASET   0x2B
#define ILI9481_RAMWR   0x2C
#define ILI9481_MADCTL  0x36
#define ILI9481_PIXFMT  0x3A
#define ILI9481_INVON   0x21

/** Initialize the ILI9481 TFT display over SPI. */
esp_err_t tft_init(void);

/** Set display rotation (0-3). Rotation 2 = print orientation, 3 = menu. */
esp_err_t tft_set_rotation(uint8_t rotation);

/** Fill entire screen with a single RGB565 color. */
esp_err_t tft_fill_screen(uint16_t color);

/** Set the active drawing window for subsequent pixel writes. */
esp_err_t tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/** Push a block of RGB565 pixel data to the current window. */
esp_err_t tft_push_pixels(const uint16_t *data, uint32_t count);

/** Push a single scanline of RGB565 data at the given y coordinate (full width). */
esp_err_t tft_push_line(uint16_t y, const uint16_t *data, uint16_t width);

/** Send a command byte to the display. */
esp_err_t tft_write_cmd(uint8_t cmd);

/** Send data bytes to the display. */
esp_err_t tft_write_data(const uint8_t *data, size_t len);

/**
 * Get the SPI device handle for LVGL flush callback.
 * LVGL needs direct access for efficient DMA transfers.
 */
void *tft_get_spi_handle(void);

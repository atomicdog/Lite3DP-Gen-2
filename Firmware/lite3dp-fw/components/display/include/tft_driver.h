#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/* Native panel resolution (portrait, MADCTL rotation 0).
 * Panel is 320x480 — ST7796S per the original Arduino User_Setup.h
 * (ILI9481 selectable via CONFIG_LITE3DP_TFT_ILI9481 as fallback). */
#define TFT_NATIVE_WIDTH      320
#define TFT_NATIVE_HEIGHT     480
#define TFT_NATIVE_LONG_SIDE  480

/* Legacy aliases — logical size depends on rotation; prefer tft_width()/tft_height() */
#define TFT_WIDTH   TFT_NATIVE_WIDTH
#define TFT_HEIGHT  TFT_NATIVE_HEIGHT

/* MIPI DCS commands (shared by ST7796 / ILI9481) */
#define TFT_CMD_SWRESET 0x01
#define TFT_CMD_RDID4   0xD3
#define TFT_CMD_SLPOUT  0x11
#define TFT_CMD_INVON   0x21
#define TFT_CMD_DISPON  0x29
#define TFT_CMD_CASET   0x2A
#define TFT_CMD_PASET   0x2B
#define TFT_CMD_RAMWR   0x2C
#define TFT_CMD_MADCTL  0x36
#define TFT_CMD_PIXFMT  0x3A

/** Initialize the TFT display over SPI (probes controller ID first). */
esp_err_t tft_init(void);

/** Set display rotation (0-3). Rotation 2 = print orientation, 3 = menu. */
esp_err_t tft_set_rotation(uint8_t rotation);

/** Logical width/height for the current rotation (480x320 in landscape). */
uint16_t tft_width(void);
uint16_t tft_height(void);

/**
 * Read the 4-byte RDID4 (0xD3) response, captured during tft_init() with a
 * temporary low-speed SPI device. ST7796S reads xx 00 77 96. All-00/all-FF
 * means MISO is not routed from the panel — fall back to visual A/B via
 * CONFIG_LITE3DP_TFT_CONTROLLER.
 */
void tft_get_id(uint8_t out[4]);

/**
 * Visual bring-up pattern, cycles all 4 rotations: R/G/B fills (byte order +
 * RGB/BGR), 1px white border on black (extents), corner colors + "F" glyph
 * (mirroring), tick marks showing the rotation number. Blocks ~30 s.
 */
void tft_test_pattern(void);

/** Fill the logical screen with a single native RGB565 color. */
esp_err_t tft_fill_screen(uint16_t color);

/**
 * Atomic rectangle blit: acquires the SPI bus, sets the window, pushes
 * w*h pixels (big-endian RGB565), releases the bus. Safe against SD/touch
 * transactions interleaving on the shared bus.
 */
esp_err_t tft_blit(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                   const uint16_t *data);

/** Push one scanline (big-endian RGB565) at row y. Atomic like tft_blit. */
esp_err_t tft_push_line(uint16_t y, const uint16_t *data, uint16_t width);

/* ── Raw access (caller is responsible for bus atomicity) ─────────── */

/** Set the active drawing window for subsequent pixel writes. */
esp_err_t tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/** Push a block of big-endian RGB565 pixel data to the current window. */
esp_err_t tft_push_pixels(const uint16_t *data, uint32_t count);

/** Send a command byte to the display. */
esp_err_t tft_write_cmd(uint8_t cmd);

/** Send data bytes to the display. */
esp_err_t tft_write_data(const uint8_t *data, size_t len);

/**
 * Get the SPI device handle for LVGL flush callback.
 * LVGL needs direct access for efficient DMA transfers.
 */
void *tft_get_spi_handle(void);

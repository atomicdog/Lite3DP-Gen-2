#pragma once

#include "esp_err.h"

/**
 * Decode a PNG file from the SD card and render it line-by-line to the TFT.
 * Uses a single 426-pixel RGB565 line buffer for minimal memory usage.
 *
 * @param filepath  Full path to the PNG file (e.g., "/sdcard/model/0.png")
 */
esp_err_t png_decode_to_tft(const char *filepath);

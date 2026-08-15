#include "png_decoder.h"
#include "tft_driver.h"
#include "PNGdec.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "png_dec";

/* PNGIMAGE is ~42KB — allocate statically to avoid heap fragmentation */
static PNGIMAGE s_png;

/* RGB565 line buffer for TFT output (one scanline) */
static uint16_t s_line_buf[TFT_WIDTH];

/* ── VFS file I/O callbacks for PNGdec ─────────────────────────── */

static void *png_open_cb(const char *filename, int32_t *pFileSize)
{
    struct stat st;
    if (stat(filename, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", filename);
        return NULL;
    }
    *pFileSize = st.st_size;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open: %s", filename);
    }
    return (void *)f;
}

static void png_close_cb(void *pHandle)
{
    if (pHandle) {
        fclose((FILE *)pHandle);
    }
}

static int32_t png_read_cb(PNGFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    FILE *f = (FILE *)pFile->fHandle;
    return (int32_t)fread(pBuf, 1, iLen, f);
}

static int32_t png_seek_cb(PNGFILE *pFile, int32_t iPosition)
{
    FILE *f = (FILE *)pFile->fHandle;
    return fseek(f, iPosition, SEEK_SET);
}

/* ── Draw callback: called once per decoded scanline ───────────── */

static void png_draw_cb(PNGDRAW *pDraw)
{
    /* Convert the decoded line to RGB565 in big-endian (SPI byte order) */
    PNG_getLineAsRGB565(pDraw, s_line_buf, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF,
                        PNG_hasAlpha(&s_png));

    /* Push this scanline directly to the TFT */
    int width = pDraw->iWidth;
    if (width > TFT_WIDTH) width = TFT_WIDTH;

    tft_push_line((uint16_t)pDraw->y, s_line_buf, (uint16_t)width);
}

/* ── Public API ────────────────────────────────────────────────── */

esp_err_t png_decode_to_tft(const char *filepath)
{
    ESP_LOGD(TAG, "Decoding: %s", filepath);

    /* Set up the TFT window for full-screen write */
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    /* Open the PNG file with VFS callbacks */
    int rc = PNG_openFileCallbacks(&s_png, filepath, png_open_cb, png_close_cb,
                          png_read_cb, png_seek_cb, png_draw_cb);
    if (rc != 1) {
        ESP_LOGE(TAG, "PNG open failed: %s (error %d)", filepath, PNG_getLastError(&s_png));
        return ESP_ERR_INVALID_ARG;
    }

    int w = PNG_getWidth(&s_png);
    int h = PNG_getHeight(&s_png);
    ESP_LOGD(TAG, "PNG: %dx%d, %d bpp, type %d", w, h, PNG_getBpp(&s_png), PNG_getPixelType(&s_png));

    if (w > TFT_WIDTH || h > TFT_HEIGHT) {
        ESP_LOGW(TAG, "PNG (%dx%d) exceeds TFT (%dx%d), will be clipped", w, h, TFT_WIDTH, TFT_HEIGHT);
    }

    /* If the PNG is smaller than the TFT, clear the screen first */
    if (w < TFT_WIDTH || h < TFT_HEIGHT) {
        tft_fill_screen(0x0000);
        tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    }

    /* Decode — this calls png_draw_cb for each scanline */
    rc = PNG_decode(&s_png, NULL, 0);
    PNG_close(&s_png);

    if (rc != PNG_SUCCESS) {
        ESP_LOGE(TAG, "PNG decode failed: error %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "PNG decoded successfully: %dx%d", w, h);
    return ESP_OK;
}

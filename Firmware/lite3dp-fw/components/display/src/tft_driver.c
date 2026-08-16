#include "tft_driver.h"
#include "hal_gpio.h"
#include "hal_spi_bus.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "tft";

/* TFT chip select — TFT_eSPI default for ESP32 is GPIO15 */
#define PIN_TFT_CS    GPIO_NUM_15
#define PIN_TFT_DC    GPIO_NUM_2
#define PIN_TFT_RST   GPIO_NUM_4

#define SPI_FREQ_HZ        (26 * 1000 * 1000)  /* 26 MHz (max full-duplex on ESP32) */
#define SPI_PROBE_FREQ_HZ  (2 * 1000 * 1000)   /* ID register reads need a slow clock */

static spi_device_handle_t s_spi_dev;
static uint8_t  s_rotation = 0;
static uint16_t s_width    = TFT_NATIVE_WIDTH;
static uint16_t s_height   = TFT_NATIVE_HEIGHT;
static uint8_t  s_id[4];

/* Scanline scratch for fills — sized for the panel's long side */
static uint16_t s_fill_buf[TFT_NATIVE_LONG_SIDE];

/* ── Low-level SPI helpers ─────────────────────────────────────── */

static esp_err_t spi_send_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    gpio_set_level(PIN_TFT_DC, 0);  /* Command mode */
    esp_err_t ret = spi_device_polling_transmit(s_spi_dev, &t);
    gpio_set_level(PIN_TFT_DC, 1);  /* Back to data mode */
    return ret;
}

static esp_err_t spi_send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return ESP_OK;
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_spi_dev, &t);
}

static esp_err_t spi_send_data8(uint8_t val)
{
    return spi_send_data(&val, 1);
}

/* Acquire/release exclusive bus access so a multi-transaction sequence
 * (window + DC toggles + pixel data) can't interleave with SD/touch. */
static void bus_acquire(void)
{
    spi_device_acquire_bus(s_spi_dev, portMAX_DELAY);
}

static void bus_release(void)
{
    spi_device_release_bus(s_spi_dev);
}

/* ── Controller ID probe ───────────────────────────────────────── */
/* Runs before the main device is added: a temporary half-duplex 2 MHz
 * device on the same CS reads RDID4 (0xD3). ST7796S: xx 00 77 96. */

static void probe_controller_id(void)
{
    spi_device_handle_t probe_dev;
    spi_device_interface_config_t cfg = {
        .clock_speed_hz = SPI_PROBE_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = PIN_TFT_CS,
        .queue_size     = 3,
        .flags          = SPI_DEVICE_HALFDUPLEX,
    };
    if (spi_bus_add_device(SPI_HOST_ID, &cfg, &probe_dev) != ESP_OK) {
        ESP_LOGW(TAG, "ID probe: device add failed");
        return;
    }

    spi_device_acquire_bus(probe_dev, portMAX_DELAY);

    uint8_t cmd = TFT_CMD_RDID4;
    spi_transaction_t t_cmd = {
        .length    = 8,
        .tx_buffer = &cmd,
        .flags     = SPI_TRANS_CS_KEEP_ACTIVE,
    };
    gpio_set_level(PIN_TFT_DC, 0);
    esp_err_t ret = spi_device_polling_transmit(probe_dev, &t_cmd);
    gpio_set_level(PIN_TFT_DC, 1);

    spi_transaction_t t_rd = {
        .rxlength  = 8 * sizeof(s_id),
        .rx_buffer = s_id,
    };
    if (ret == ESP_OK) {
        ret = spi_device_polling_transmit(probe_dev, &t_rd);
    }

    spi_device_release_bus(probe_dev);
    spi_bus_remove_device(probe_dev);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ID probe failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "RDID4 (0xD3): %02X %02X %02X %02X%s",
             s_id[0], s_id[1], s_id[2], s_id[3],
             (s_id[2] == 0x77 && s_id[3] == 0x96) ? "  -> ST7796" : "");
    if ((s_id[1] | s_id[2] | s_id[3]) == 0x00 ||
        (s_id[1] & s_id[2] & s_id[3]) == 0xFF) {
        ESP_LOGW(TAG, "ID reads as all-0/all-1 — panel MISO likely not routed; "
                      "trust CONFIG_LITE3DP_TFT_CONTROLLER + visual test");
    }
}

/* ── Panel reset ───────────────────────────────────────────────── */

static void panel_hw_reset(void)
{
    gpio_set_level(PIN_TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* ── Init sequences ────────────────────────────────────────────── */

#ifdef CONFIG_LITE3DP_TFT_ILI9481

static void controller_init_seq(void)
{
    spi_send_cmd(TFT_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Power setting */
    spi_send_cmd(0xD0);
    spi_send_data8(0x07);
    spi_send_data8(0x42);
    spi_send_data8(0x18);

    /* VCOM control */
    spi_send_cmd(0xD1);
    spi_send_data8(0x00);
    spi_send_data8(0x07);
    spi_send_data8(0x10);

    /* Power setting for normal mode */
    spi_send_cmd(0xD2);
    spi_send_data8(0x01);
    spi_send_data8(0x02);

    /* Panel driving setting */
    spi_send_cmd(0xC0);
    spi_send_data8(0x10);
    spi_send_data8(0x3B);
    spi_send_data8(0x00);
    spi_send_data8(0x02);
    spi_send_data8(0x11);

    /* Frame rate */
    spi_send_cmd(0xC5);
    spi_send_data8(0x03);

    /* Gamma setting */
    spi_send_cmd(0xC8);
    uint8_t gamma[] = {0x00,0x32,0x36,0x45,0x06,0x16,0x37,0x75,0x77,0x54,0x0C,0x00};
    spi_send_data(gamma, sizeof(gamma));

    spi_send_cmd(TFT_CMD_MADCTL);
    spi_send_data8(0x0A);

    spi_send_cmd(TFT_CMD_PIXFMT);
    spi_send_data8(0x55);  /* 16-bit/pixel */

    spi_send_cmd(TFT_CMD_INVON);

    vTaskDelay(pdMS_TO_TICKS(120));
    spi_send_cmd(TFT_CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(25));
}

#else /* ST7796 (default) */

static void controller_init_seq(void)
{
    /* Canonical TFT_eSPI ST7796 sequence */
    spi_send_cmd(TFT_CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_send_cmd(TFT_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Command set control: unlock part 1 + 2 */
    spi_send_cmd(0xF0);
    spi_send_data8(0xC3);
    spi_send_cmd(0xF0);
    spi_send_data8(0x96);

    spi_send_cmd(TFT_CMD_MADCTL);
    spi_send_data8(0x48);

    spi_send_cmd(TFT_CMD_PIXFMT);
    spi_send_data8(0x55);  /* 16-bit/pixel */

    /* Display inversion control: 1-dot */
    spi_send_cmd(0xB4);
    spi_send_data8(0x01);

    /* Display function control */
    spi_send_cmd(0xB6);
    spi_send_data8(0x80);
    spi_send_data8(0x02);
    spi_send_data8(0x3B);

    /* Display output ctrl adjust */
    spi_send_cmd(0xE8);
    uint8_t doca[] = {0x40,0x8A,0x00,0x00,0x29,0x19,0xA5,0x33};
    spi_send_data(doca, sizeof(doca));

    /* Power controls + VCOM */
    spi_send_cmd(0xC1);
    spi_send_data8(0x06);
    spi_send_cmd(0xC2);
    spi_send_data8(0xA7);
    spi_send_cmd(0xC5);
    spi_send_data8(0x18);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Gamma */
    spi_send_cmd(0xE0);
    uint8_t gamma_p[] = {0xF0,0x09,0x0B,0x06,0x04,0x15,0x2F,0x54,0x42,0x3C,0x17,0x14,0x18,0x1B};
    spi_send_data(gamma_p, sizeof(gamma_p));
    spi_send_cmd(0xE1);
    uint8_t gamma_n[] = {0xE0,0x09,0x0B,0x06,0x04,0x03,0x2B,0x43,0x42,0x3B,0x16,0x14,0x17,0x1B};
    spi_send_data(gamma_n, sizeof(gamma_n));
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Command set control: re-lock */
    spi_send_cmd(0xF0);
    spi_send_data8(0x3C);
    spi_send_cmd(0xF0);
    spi_send_data8(0x69);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Original hardware ran with TFT_INVERSION_ON */
    spi_send_cmd(TFT_CMD_INVON);

    spi_send_cmd(TFT_CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(25));
}

#endif

/* ── Public API ────────────────────────────────────────────────── */

esp_err_t tft_init(void)
{
    /* Configure DC and RST pins */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PIN_TFT_DC) | (1ULL << PIN_TFT_RST),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_cfg);
    gpio_set_level(PIN_TFT_DC, 1);

    /* SPI bus is already initialized by spi_bus_shared_init() in main.c. */

    panel_hw_reset();

    /* Probe the controller ID before the 26 MHz device claims the CS */
    probe_controller_id();

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = PIN_TFT_CS,
        .queue_size     = 7,
    };
    esp_err_t ret = spi_bus_add_device(SPI_HOST_ID, &dev_cfg, &s_spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    bus_acquire();
    controller_init_seq();
    bus_release();

#ifdef CONFIG_LITE3DP_TFT_ILI9481
    ESP_LOGI(TAG, "ILI9481 TFT initialized (%dx%d native)", TFT_NATIVE_WIDTH, TFT_NATIVE_HEIGHT);
#else
    ESP_LOGI(TAG, "ST7796 TFT initialized (%dx%d native)", TFT_NATIVE_WIDTH, TFT_NATIVE_HEIGHT);
#endif
    return ESP_OK;
}

void tft_get_id(uint8_t out[4])
{
    memcpy(out, s_id, sizeof(s_id));
}

esp_err_t tft_set_rotation(uint8_t rotation)
{
    uint8_t madctl;
    rotation &= 0x03;
#ifdef CONFIG_LITE3DP_TFT_ILI9481
    switch (rotation) {
    case 0:  madctl = 0x0A; break;  /* Portrait */
    case 1:  madctl = 0x28; break;  /* Landscape */
    case 2:  madctl = 0xCA; break;  /* Portrait inverted (print mode) */
    default: madctl = 0xE8; break;  /* Landscape inverted (menu mode) */
    }
#else /* ST7796: MY=0x80 MX=0x40 MV=0x20 BGR=0x08 */
    switch (rotation) {
    case 0:  madctl = 0x48; break;  /* Portrait */
    case 1:  madctl = 0x28; break;  /* Landscape */
    case 2:  madctl = 0x88; break;  /* Portrait inverted (print mode) */
    default: madctl = 0xE8; break;  /* Landscape inverted (menu mode) */
    }
#endif

    s_rotation = rotation;
    if (rotation & 0x01) {
        s_width  = TFT_NATIVE_HEIGHT;
        s_height = TFT_NATIVE_WIDTH;
    } else {
        s_width  = TFT_NATIVE_WIDTH;
        s_height = TFT_NATIVE_HEIGHT;
    }

    bus_acquire();
    spi_send_cmd(TFT_CMD_MADCTL);
    esp_err_t ret = spi_send_data8(madctl);
    bus_release();
    return ret;
}

uint16_t tft_width(void)  { return s_width; }
uint16_t tft_height(void) { return s_height; }

esp_err_t tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t col_data[] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t row_data[] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };

    spi_send_cmd(TFT_CMD_CASET);
    spi_send_data(col_data, 4);
    spi_send_cmd(TFT_CMD_PASET);
    spi_send_data(row_data, 4);
    spi_send_cmd(TFT_CMD_RAMWR);
    return ESP_OK;
}

esp_err_t tft_push_pixels(const uint16_t *data, uint32_t count)
{
    /* Send in chunks to stay within DMA transfer limits */
    const size_t chunk = 1024;  /* pixels per transfer */
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t remaining = count;

    while (remaining > 0) {
        uint32_t n = remaining > chunk ? chunk : remaining;
        spi_transaction_t t = {
            .length    = n * 16,  /* bits */
            .tx_buffer = ptr,
        };
        esp_err_t ret = spi_device_polling_transmit(s_spi_dev, &t);
        if (ret != ESP_OK) return ret;
        ptr += n * 2;
        remaining -= n;
    }
    return ESP_OK;
}

esp_err_t tft_blit(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                   const uint16_t *data)
{
    bus_acquire();
    tft_set_window(x0, y0, x1, y1);
    esp_err_t ret = tft_push_pixels(data,
        (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1));
    bus_release();
    return ret;
}

esp_err_t tft_push_line(uint16_t y, const uint16_t *data, uint16_t width)
{
    return tft_blit(0, y, width - 1, y, data);
}

/* Fill a rectangle in logical coordinates with a native RGB565 color. */
static esp_err_t fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           uint16_t color)
{
    if (w == 0 || h == 0) return ESP_OK;
    if (w > TFT_NATIVE_LONG_SIDE) w = TFT_NATIVE_LONG_SIDE;

    for (int i = 0; i < w; i++) {
        s_fill_buf[i] = __builtin_bswap16(color);  /* SPI is big-endian */
    }

    bus_acquire();
    tft_set_window(x, y, x + w - 1, y + h - 1);
    esp_err_t ret = ESP_OK;
    for (int row = 0; row < h && ret == ESP_OK; row++) {
        ret = tft_push_pixels(s_fill_buf, w);
    }
    bus_release();
    return ret;
}

esp_err_t tft_fill_screen(uint16_t color)
{
    return fill_rect(0, 0, s_width, s_height, color);
}

/* ── Bring-up test pattern ─────────────────────────────────────── */

#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_BLUE   0x001F
#define C_WHITE  0xFFFF
#define C_BLACK  0x0000

void tft_test_pattern(void)
{
    for (uint8_t rot = 0; rot < 4; rot++) {
        tft_set_rotation(rot);
        uint16_t w = s_width, h = s_height;
        ESP_LOGI(TAG, "Test pattern: rotation %u (%ux%u logical)", rot, w, h);

        /* 1. Solid fills: red must look red (cyan => byte order wrong,
         *    blue => RGB/BGR bit wrong) */
        ESP_LOGI(TAG, "  fill RED");
        tft_fill_screen(C_RED);
        vTaskDelay(pdMS_TO_TICKS(1200));
        ESP_LOGI(TAG, "  fill GREEN");
        tft_fill_screen(C_GREEN);
        vTaskDelay(pdMS_TO_TICKS(1200));
        ESP_LOGI(TAG, "  fill BLUE");
        tft_fill_screen(C_BLUE);
        vTaskDelay(pdMS_TO_TICKS(1200));

        /* 2. Geometry: black + white border (all 4 edges must be visible;
         *    3px so bezel overscan of a pixel or two doesn't hide it),
         *    corner squares, "F" glyph for mirroring, rot+1 tick marks. */
        ESP_LOGI(TAG, "  geometry frame");
        tft_fill_screen(C_BLACK);
        const uint16_t bw = 3;
        fill_rect(0, 0, w, bw, C_WHITE);
        fill_rect(0, h - bw, w, bw, C_WHITE);
        fill_rect(0, 0, bw, h, C_WHITE);
        fill_rect(w - bw, 0, bw, h, C_WHITE);

        fill_rect(2, 2, 40, 40, C_RED);              /* top-left */
        fill_rect(w - 42, 2, 40, 40, C_GREEN);       /* top-right */
        fill_rect(2, h - 42, 40, 40, C_BLUE);        /* bottom-left */
        fill_rect(w - 42, h - 42, 40, 40, C_WHITE);  /* bottom-right */

        /* "F": readable => not mirrored */
        uint16_t cx = w / 2, cy = h / 2;
        fill_rect(cx - 30, cy - 50, 12, 100, C_WHITE);  /* vertical bar */
        fill_rect(cx - 30, cy - 50, 60, 12, C_WHITE);   /* top arm */
        fill_rect(cx - 30, cy - 8, 45, 12, C_WHITE);    /* middle arm */

        /* rotation number as tick marks along the top edge */
        for (uint8_t i = 0; i <= rot; i++) {
            fill_rect(50 + i * 20, 8, 12, 12, C_WHITE);
        }

        vTaskDelay(pdMS_TO_TICKS(2500));
    }
    ESP_LOGI(TAG, "Test pattern done");
}

esp_err_t tft_write_cmd(uint8_t cmd)
{
    return spi_send_cmd(cmd);
}

esp_err_t tft_write_data(const uint8_t *data, size_t len)
{
    return spi_send_data(data, len);
}

void *tft_get_spi_handle(void)
{
    return (void *)s_spi_dev;
}

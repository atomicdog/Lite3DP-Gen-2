#include "tft_driver.h"
#include "hal_gpio.h"
#include "hal_spi_bus.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "tft";

/* TFT chip select — TFT_eSPI default for ESP32 is GPIO15 */
#define PIN_TFT_CS    GPIO_NUM_15
#define PIN_TFT_DC    GPIO_NUM_2
#define PIN_TFT_RST   GPIO_NUM_4    /* -1 if tied to EN */

#define SPI_FREQ_HZ   (26 * 1000 * 1000)  /* 26 MHz (max for full-duplex on ESP32) */

static spi_device_handle_t s_spi_dev;

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

/* ── ILI9481 init sequence (INIT_1 — original default) ─────────── */

static void ili9481_init_seq(void)
{
    /* Hardware reset */
    if (PIN_TFT_RST >= 0) {
        gpio_set_level(PIN_TFT_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(PIN_TFT_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    /* Sleep out */
    spi_send_cmd(ILI9481_SLPOUT);
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

    /* Memory access control */
    spi_send_cmd(ILI9481_MADCTL);
    spi_send_data8(0x0A);

    /* Pixel format: 16-bit RGB565 for SPI */
    spi_send_cmd(ILI9481_PIXFMT);
    spi_send_data8(0x55);  /* 16-bit/pixel */

    /* Display inversion on */
    spi_send_cmd(ILI9481_INVON);

    /* Column address set: 0 to 319 */
    spi_send_cmd(ILI9481_CASET);
    spi_send_data8(0x00);
    spi_send_data8(0x00);
    spi_send_data8(0x01);
    spi_send_data8(0x3F);

    /* Page address set: 0 to 479 (ILI9481 native) */
    spi_send_cmd(ILI9481_PASET);
    spi_send_data8(0x00);
    spi_send_data8(0x00);
    spi_send_data8(0x01);
    spi_send_data8(0xDF);

    vTaskDelay(pdMS_TO_TICKS(120));

    /* Display on */
    spi_send_cmd(ILI9481_DISPON);
    vTaskDelay(pdMS_TO_TICKS(25));
}

/* ── Public API ────────────────────────────────────────────────── */

esp_err_t tft_init(void)
{
    /* Configure DC and RST pins */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << PIN_TFT_DC),
        .mode         = GPIO_MODE_OUTPUT,
    };
    if (PIN_TFT_RST >= 0) {
        io_cfg.pin_bit_mask |= (1ULL << PIN_TFT_RST);
    }
    gpio_config(&io_cfg);
    gpio_set_level(PIN_TFT_DC, 1);

    /* SPI bus is already initialized by spi_bus_shared_init() in main.c.
     * Add the TFT as a device on the shared bus. */

    /* Add TFT device to SPI bus */
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

    /* Run ILI9481 initialization */
    ili9481_init_seq();

    ESP_LOGI(TAG, "ILI9481 TFT initialized (%dx%d)", TFT_WIDTH, TFT_HEIGHT);
    return ESP_OK;
}

esp_err_t tft_set_rotation(uint8_t rotation)
{
    uint8_t madctl;
    switch (rotation & 0x03) {
    case 0: madctl = 0x0A; break;  /* Portrait */
    case 1: madctl = 0x28; break;  /* Landscape */
    case 2: madctl = 0xCA; break;  /* Portrait inverted (print mode) */
    case 3: madctl = 0xE8; break;  /* Landscape inverted (menu mode) */
    default: madctl = 0x0A; break;
    }
    spi_send_cmd(ILI9481_MADCTL);
    return spi_send_data8(madctl);
}

esp_err_t tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t col_data[] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t row_data[] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };

    spi_send_cmd(ILI9481_CASET);
    spi_send_data(col_data, 4);
    spi_send_cmd(ILI9481_PASET);
    spi_send_data(row_data, 4);
    spi_send_cmd(ILI9481_RAMWR);
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

esp_err_t tft_fill_screen(uint16_t color)
{
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    /* Fill line buffer */
    uint16_t line_buf[TFT_WIDTH];
    for (int i = 0; i < TFT_WIDTH; i++) {
        line_buf[i] = __builtin_bswap16(color);  /* SPI is big-endian */
    }

    for (int y = 0; y < TFT_HEIGHT; y++) {
        tft_push_pixels(line_buf, TFT_WIDTH);
    }
    return ESP_OK;
}

esp_err_t tft_push_line(uint16_t y, const uint16_t *data, uint16_t width)
{
    tft_set_window(0, y, width - 1, y);
    return tft_push_pixels(data, width);
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

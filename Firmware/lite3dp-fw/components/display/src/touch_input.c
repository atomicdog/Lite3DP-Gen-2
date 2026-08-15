#include "touch_input.h"
#include "hal_gpio.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "touch";

/* XPT2046 commands */
#define XPT2046_CMD_X   0xD0  /* Channel: X position */
#define XPT2046_CMD_Y   0x90  /* Channel: Y position */

/* Calibration: our 0xD0 cmd reads YP+ (= library p.y), 0x90 reads XP+ (= library p.x).
 * Original firmware: p.y range 240-3700, p.x range 360-3800.  */
#define RAW_X_MIN   240   /* 0xD0 channel (YP+) = library p.y */
#define RAW_X_MAX   3700
#define RAW_Y_MIN   360   /* 0x90 channel (XP+) = library p.x */
#define RAW_Y_MAX   3800
#define SCREEN_W  319   /* LVGL x range (TFT width - 1)  */
#define SCREEN_H  425   /* LVGL y range (TFT height - 1) */

#define XPT2046_SPI_FREQ  (2 * 1000 * 1000)  /* 2 MHz */

static spi_device_handle_t s_touch_dev;

esp_err_t touch_input_init(void)
{
    /* Configure IRQ pin as input */
    gpio_config_t irq_cfg = {
        .pin_bit_mask = (1ULL << PIN_TOUCH_IRQ),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&irq_cfg);

    /* Add XPT2046 to the shared SPI bus (already initialized by TFT driver) */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = XPT2046_SPI_FREQ,
        .mode           = 0,
        .spics_io_num   = PIN_TOUCH_CS,
        .queue_size     = 1,
    };
    esp_err_t ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_touch_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add XPT2046 SPI device: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "XPT2046 touch initialized");
    return ESP_OK;
}

static uint16_t xpt2046_read_channel(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };

    spi_transaction_t t = {
        .length    = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_touch_dev, &t);

    /* Result is in bits 1-12 of the response (rx[1] << 5 | rx[2] >> 3) */
    return ((uint16_t)rx[1] << 5) | (rx[2] >> 3);
}

static int32_t map_range(int32_t val, int32_t in_min, int32_t in_max,
                         int32_t out_min, int32_t out_max)
{
    if (val < in_min) val = in_min;
    if (val > in_max) val = in_max;
    return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static uint16_t median5(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e)
{
    uint16_t v[5] = {a, b, c, d, e};
    /* Simple insertion sort for 5 elements */
    for (int i = 1; i < 5; i++) {
        uint16_t key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
    return v[2];
}

esp_err_t touch_input_read(touch_point_t *point)
{
    static int32_t filt_x = -1, filt_y = -1;  /* IIR filter state */

    if (!touch_input_pressed()) {
        point->pressed = false;
        point->x = 0;
        point->y = 0;
        filt_x = filt_y = -1;  /* Reset filter on lift */
        return ESP_OK;
    }

    /* Hold the SPI bus for the entire read sequence — prevents TFT
     * transactions from interleaving and corrupting samples. */
    spi_device_acquire_bus(s_touch_dev, portMAX_DELAY);

    /* Discard first conversion — XPT2046 S/H capacitor needs time to settle */
    xpt2046_read_channel(XPT2046_CMD_X);
    xpt2046_read_channel(XPT2046_CMD_Y);

    /* Read 5 samples, take median (rejects outliers) */
    uint16_t sx[5], sy[5];
    for (int i = 0; i < 5; i++) {
        sx[i] = xpt2046_read_channel(XPT2046_CMD_X);
        sy[i] = xpt2046_read_channel(XPT2046_CMD_Y);
    }

    spi_device_release_bus(s_touch_dev);

    uint16_t raw_x = median5(sx[0], sx[1], sx[2], sx[3], sx[4]);
    uint16_t raw_y = median5(sy[0], sy[1], sy[2], sy[3], sy[4]);

    int32_t mx = map_range(raw_x, RAW_X_MIN, RAW_X_MAX, 0, SCREEN_H);
    int32_t my = map_range(raw_y, RAW_Y_MIN, RAW_Y_MAX, SCREEN_W, 0);

    /* IIR low-pass: smooths jitter while keeping response quick.
     * First sample after press seeds the filter directly. */
    if (filt_x < 0) {
        filt_x = mx;
        filt_y = my;
    } else {
        filt_x = (filt_x + mx) / 2;
        filt_y = (filt_y + my) / 2;
    }

    point->raw_x = raw_x;
    point->raw_y = raw_y;
    point->x = (uint16_t)filt_y;
    point->y = (uint16_t)filt_x;
    point->pressed = true;

    return ESP_OK;
}

bool touch_input_pressed(void)
{
    return gpio_get_level(PIN_TOUCH_IRQ) == 0;  /* Active low */
}


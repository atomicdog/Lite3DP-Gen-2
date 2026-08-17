#include "touch_input.h"
#include "hal_gpio.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "touch";

/* XPT2046 differential-mode conversions.  Low nibble is 0 in every command,
 * which leaves PD1:PD0 = 00 — power-down between conversions with PENIRQ
 * left enabled.  Changing that would silently kill the IRQ line. */
#define XPT2046_CMD_X   0xD0  /* A2:A0 = 101 */
#define XPT2046_CMD_Y   0x90  /* A2:A0 = 001 */
#define XPT2046_CMD_Z1  0xB0  /* A2:A0 = 011 */
#define XPT2046_CMD_Z2  0xC0  /* A2:A0 = 100 */

#define XPT2046_SPI_FREQ  (2 * 1000 * 1000)  /* 2 MHz */

#define RAW_SAMPLES_MAX  32

/* Default calibration — measured 2026-08-16 by 5-point stylus capture on the
 * physical printer (tools/touch_test.py capture + fit), then validated against
 * a held-out capture: worst error 5px in X, 2px in Y.
 *
 * The predecessor values were inherited from the v1.1 Arduino firmware
 * (LITE3DP-G2-TOUCH-v1-1.ino:315), which mapped into a 426x320 space.  The UI
 * actually runs rotation 3 = 480x320 (UI_MENU_ROTATION, ui_manager.c), so
 * screen X was pinned to 0..319 — the right third of the panel unreachable —
 * and screen Y was stretched to 0..425 on a 320px-tall screen.
 *
 * The 0x90 channel is the panel's long axis (screen X, inverted); 0xD0 is the
 * short axis (screen Y).  Both are linear to within ~2px across the panel, so
 * a per-axis fit is sufficient — no affine/skew term is needed.
 *
 * pressure_min sits between the measured idle ceiling (41) and the lightest
 * real contact seen (253). */
static touch_cal_t s_cal = {
    .x_min = 0,    .x_max = 4006,   /* 0x90 channel */
    .y_min = 195,  .y_max = 3803,   /* 0xD0 channel */
    .screen_w = 480, .screen_h = 320,
    .swap_xy  = true,
    .invert_x = true,
    .invert_y = false,
    .pressure_min = 150,
};

static spi_device_handle_t s_touch_dev;

/* Two tasks reach the XPT2046: the LVGL input callback and the HTTP debug
 * handlers.  spi_device_acquire_bus() on one handle is not reentrant across
 * tasks — the second caller gets "Cannot acquire bus when a polling
 * transaction is in progress", and the unbalanced release_bus that follows
 * asserts and reboots the chip.  This mutex is what makes the device
 * single-entry; do not touch the SPI device outside of it. */
static SemaphoreHandle_t s_touch_lock;

#define TOUCH_LOCK_TIMEOUT_MS  200

/* Take the device and the SPI bus together. On failure nothing is held. */
static bool touch_bus_take(void)
{
    if (!s_touch_lock) return false;
    if (xSemaphoreTake(s_touch_lock, pdMS_TO_TICKS(TOUCH_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    /* Holding the bus for the whole burst keeps TFT traffic from interleaving
     * with (and corrupting) the samples.  portMAX_DELAY is not a choice —
     * spi_device_acquire_bus rejects any finite wait — but the mutex above has
     * already made us the only claimant for this device. */
    if (spi_device_acquire_bus(s_touch_dev, portMAX_DELAY) != ESP_OK) {
        xSemaphoreGive(s_touch_lock);
        return false;
    }
    return true;
}

static void touch_bus_give(void)
{
    spi_device_release_bus(s_touch_dev);
    xSemaphoreGive(s_touch_lock);
}

esp_err_t touch_input_init(void)
{
    /* IO35 is input-only and has no internal pull resistor, so no pull-up is
     * requested here — it would be silently ignored.  The board does not supply
     * one either (schematic sheet 2/2: FPC2 pin 11 runs straight to the net), so
     * the pin may float when untouched.  Presses are confirmed by pressure. */
    gpio_config_t irq_cfg = {
        .pin_bit_mask = (1ULL << PIN_TOUCH_IRQ),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
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

    s_touch_lock = xSemaphoreCreateMutex();
    if (!s_touch_lock) {
        ESP_LOGE(TAG, "Failed to create touch mutex");
        return ESP_ERR_NO_MEM;
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

/* Touch pressure from the Z channels.  Untouched, z1 sits near 0 and z2 near
 * full scale, so this lands near 0; contact drives z1 up and z2 down.  The
 * ratiometric form is more accurate but needs a known plate resistance we do
 * not have, and this only has to separate "finger" from "noise". */
static uint16_t pressure_from_z(uint16_t z1, uint16_t z2)
{
    int32_t z = (int32_t)z1 + 4095 - (int32_t)z2;
    if (z < 0) z = 0;
    if (z > 4095) z = 4095;
    return (uint16_t)z;
}

static int32_t map_range(int32_t val, int32_t in_min, int32_t in_max,
                         int32_t out_min, int32_t out_max)
{
    if (in_max == in_min) return out_min;
    if (in_min < in_max) {
        if (val < in_min) val = in_min;
        if (val > in_max) val = in_max;
    } else {
        if (val > in_min) val = in_min;
        if (val < in_max) val = in_max;
    }
    return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static uint16_t median_of(uint16_t *v, int n)
{
    /* Insertion sort — n is at most RAW_SAMPLES_MAX and usually 5 */
    for (int i = 1; i < n; i++) {
        uint16_t key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
    return v[n / 2];
}

/* Map a raw pair through the active calibration into LVGL pixel space.
 * raw_x is the 0xD0 channel, raw_y the 0x90 channel. */
static void apply_cal(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y)
{
    uint16_t src_x = s_cal.swap_xy ? raw_y : raw_x;
    uint16_t src_y = s_cal.swap_xy ? raw_x : raw_y;

    int32_t max_x = (int32_t)s_cal.screen_w - 1;
    int32_t max_y = (int32_t)s_cal.screen_h - 1;

    int32_t mx = map_range(src_x, s_cal.x_min, s_cal.x_max, 0, max_x);
    int32_t my = map_range(src_y, s_cal.y_min, s_cal.y_max, 0, max_y);

    if (s_cal.invert_x) mx = max_x - mx;
    if (s_cal.invert_y) my = max_y - my;

    *out_x = (uint16_t)mx;
    *out_y = (uint16_t)my;
}

esp_err_t touch_input_read(touch_point_t *point)
{
    static int32_t filt_x = -1, filt_y = -1;  /* IIR filter state */

    memset(point, 0, sizeof(*point));

    if (!touch_input_pressed()) {
        filt_x = filt_y = -1;  /* Reset filter on lift */
        return ESP_OK;
    }

    if (!touch_bus_take()) {
        /* Another task is mid-burst. Report no change rather than guessing —
         * the next poll is 30ms away. */
        return ESP_ERR_TIMEOUT;
    }

    /* Discard first conversion — XPT2046 S/H capacitor needs time to settle */
    xpt2046_read_channel(XPT2046_CMD_X);
    xpt2046_read_channel(XPT2046_CMD_Y);

    /* Read 5 samples, take median (rejects outliers) */
    uint16_t sx[5], sy[5];
    for (int i = 0; i < 5; i++) {
        sx[i] = xpt2046_read_channel(XPT2046_CMD_X);
        sy[i] = xpt2046_read_channel(XPT2046_CMD_Y);
    }

    uint16_t z1 = xpt2046_read_channel(XPT2046_CMD_Z1);
    uint16_t z2 = xpt2046_read_channel(XPT2046_CMD_Z2);

    touch_bus_give();

    uint16_t pressure = pressure_from_z(z1, z2);

    /* IRQ low but no real contact — the pin floats on this board, so this is
     * the check that keeps phantom presses out of the UI. */
    if (pressure < s_cal.pressure_min) {
        filt_x = filt_y = -1;
        return ESP_OK;
    }

    uint16_t raw_x = median_of(sx, 5);
    uint16_t raw_y = median_of(sy, 5);

    uint16_t mx, my;
    apply_cal(raw_x, raw_y, &mx, &my);

    /* IIR low-pass: smooths jitter while keeping response quick.
     * First sample after press seeds the filter directly. */
    if (filt_x < 0) {
        filt_x = mx;
        filt_y = my;
    } else {
        filt_x = (filt_x + mx) / 2;
        filt_y = (filt_y + my) / 2;
    }

    point->raw_x   = raw_x;
    point->raw_y   = raw_y;
    point->x       = (uint16_t)filt_x;
    point->y       = (uint16_t)filt_y;
    point->pressure = pressure;
    point->pressed = true;

    return ESP_OK;
}

bool touch_input_pressed(void)
{
    return gpio_get_level(PIN_TOUCH_IRQ) == 0;  /* Active low */
}

esp_err_t touch_input_read_raw(touch_raw_t *out, uint16_t n)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (n < 1) n = 1;
    if (n > RAW_SAMPLES_MAX) n = RAW_SAMPLES_MAX;

    memset(out, 0, sizeof(*out));
    out->samples = n;
    out->irq = (uint8_t)gpio_get_level(PIN_TOUCH_IRQ);

    uint16_t sx[RAW_SAMPLES_MAX], sy[RAW_SAMPLES_MAX];
    uint32_t z1_sum = 0, z2_sum = 0;

    if (!touch_bus_take()) {
        return ESP_ERR_TIMEOUT;
    }

    xpt2046_read_channel(XPT2046_CMD_X);   /* settle */
    xpt2046_read_channel(XPT2046_CMD_Y);

    for (uint16_t i = 0; i < n; i++) {
        sx[i] = xpt2046_read_channel(XPT2046_CMD_X);
        sy[i] = xpt2046_read_channel(XPT2046_CMD_Y);
        z1_sum += xpt2046_read_channel(XPT2046_CMD_Z1);
        z2_sum += xpt2046_read_channel(XPT2046_CMD_Z2);
    }

    touch_bus_give();

    out->x_min = out->x_max = sx[0];
    out->y_min = out->y_max = sy[0];
    for (uint16_t i = 1; i < n; i++) {
        if (sx[i] < out->x_min) out->x_min = sx[i];
        if (sx[i] > out->x_max) out->x_max = sx[i];
        if (sy[i] < out->y_min) out->y_min = sy[i];
        if (sy[i] > out->y_max) out->y_max = sy[i];
    }

    /* median_of sorts in place, so take the extremes above first */
    out->x_med = median_of(sx, n);
    out->y_med = median_of(sy, n);

    out->z1 = (uint16_t)(z1_sum / n);
    out->z2 = (uint16_t)(z2_sum / n);
    out->pressure = pressure_from_z(out->z1, out->z2);

    apply_cal(out->x_med, out->y_med, &out->mapped_x, &out->mapped_y);

    return ESP_OK;
}

void touch_input_get_cal(touch_cal_t *cal)
{
    if (cal) *cal = s_cal;
}

void touch_input_set_cal(const touch_cal_t *cal)
{
    if (!cal) return;
    s_cal = *cal;
    ESP_LOGI(TAG, "cal: x[%u..%u] y[%u..%u] %ux%u swap=%d invx=%d invy=%d pmin=%u",
             s_cal.x_min, s_cal.x_max, s_cal.y_min, s_cal.y_max,
             s_cal.screen_w, s_cal.screen_h,
             s_cal.swap_xy, s_cal.invert_x, s_cal.invert_y, s_cal.pressure_min);
}

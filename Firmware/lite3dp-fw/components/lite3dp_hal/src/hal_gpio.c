#include "hal_gpio.h"
#include "esp_log.h"

static const char *TAG = "hal_gpio";

esp_err_t hal_gpio_init(void)
{
    /* Motor pins: output, default low */
    gpio_config_t motor_cfg = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_DIR) |
                        (1ULL << PIN_MOTOR_STEP) |
                        (1ULL << PIN_MOTOR_EN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&motor_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure motor pins: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Motor disabled by default (EN is active low, set high to disable) */
    gpio_set_level(PIN_MOTOR_EN, 1);

    /* Sensor pins: input only (GPIO36, 34, 39 are input-only on ESP32) */
    gpio_config_t sensor_cfg = {
        .pin_bit_mask = (1ULL << PIN_ENDSTOP) |
                        (1ULL << PIN_SD_DETECT) |
                        (1ULL << PIN_BTN_PLAY),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,   /* GPIO36/39 have no internal pull */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&sensor_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure sensor pins: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GPIO initialized");
    return ESP_OK;
}

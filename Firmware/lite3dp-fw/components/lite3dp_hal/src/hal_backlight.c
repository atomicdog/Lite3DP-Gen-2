#include "hal_backlight.h"
#include "hal_gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "hal_backlight";

#define BL_LEDC_TIMER       LEDC_TIMER_1
#define BL_LEDC_CHANNEL     LEDC_CHANNEL_1
#define BL_LEDC_FREQ_HZ     5000
#define BL_LEDC_RESOLUTION  LEDC_TIMER_8_BIT

esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RESOLUTION,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .gpio_num   = PIN_BACKLIGHT,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Backlight initialized (GPIO%d)", PIN_BACKLIGHT);
    return ESP_OK;
}

esp_err_t backlight_set(uint8_t brightness)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, brightness);
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

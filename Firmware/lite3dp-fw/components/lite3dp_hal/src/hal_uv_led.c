#include "hal_uv_led.h"
#include "hal_gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "hal_uv_led";

#define UV_LEDC_TIMER       LEDC_TIMER_0
#define UV_LEDC_CHANNEL     LEDC_CHANNEL_0
#define UV_LEDC_FREQ_HZ     5000
#define UV_LEDC_RESOLUTION  LEDC_TIMER_8_BIT

esp_err_t uv_led_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = UV_LEDC_TIMER,
        .duty_resolution = UV_LEDC_RESOLUTION,
        .freq_hz         = UV_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = UV_LEDC_CHANNEL,
        .timer_sel  = UV_LEDC_TIMER,
        .gpio_num   = PIN_UV_LED,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "UV LED initialized (GPIO%d, %dHz)", PIN_UV_LED, UV_LEDC_FREQ_HZ);
    return ESP_OK;
}

esp_err_t uv_led_set_power(uint8_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, UV_LEDC_CHANNEL, duty);
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, UV_LEDC_CHANNEL);
}

esp_err_t uv_led_off(void)
{
    return uv_led_set_power(0);
}

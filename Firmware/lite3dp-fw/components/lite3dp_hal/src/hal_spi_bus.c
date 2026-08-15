#include "hal_spi_bus.h"
#include "hal_gpio.h"
#include "esp_log.h"

static const char *TAG = "spi_bus";

static SemaphoreHandle_t s_spi_mutex = NULL;
static bool s_initialized = false;

esp_err_t spi_bus_shared_init(void)
{
    if (s_initialized) return ESP_OK;

    s_spi_mutex = xSemaphoreCreateMutex();
    if (!s_spi_mutex) {
        ESP_LOGE(TAG, "Failed to create SPI mutex");
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = PIN_SPI_MOSI,
        .miso_io_num     = PIN_SPI_MISO,
        .sclk_io_num     = PIN_SPI_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 320 * 2 * 20,  /* 20 lines of RGB565 */
    };

    esp_err_t ret = spi_bus_initialize(SPI_HOST_ID, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Shared SPI bus initialized (MOSI=%d, MISO=%d, CLK=%d)",
             PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_CLK);
    return ESP_OK;
}

esp_err_t spi_bus_acquire(void)
{
    if (!s_spi_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_spi_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "SPI mutex acquire timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t spi_bus_release(void)
{
    if (!s_spi_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreGive(s_spi_mutex);
    return ESP_OK;
}

SemaphoreHandle_t spi_bus_get_mutex(void)
{
    return s_spi_mutex;
}

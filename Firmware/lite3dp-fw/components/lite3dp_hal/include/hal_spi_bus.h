#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"

#define SPI_HOST_ID     SPI2_HOST   /* HSPI — shared by TFT, SD, XPT2046 */

/**
 * Initialize the shared SPI bus and its access mutex.
 * Must be called once before any SPI device (TFT, SD, touch) is initialized.
 */
esp_err_t spi_bus_shared_init(void);

/**
 * Acquire exclusive access to the SPI bus.
 * Blocks until the mutex is available (max 5 seconds).
 */
esp_err_t spi_bus_acquire(void);

/** Release the SPI bus mutex. */
esp_err_t spi_bus_release(void);

/** Get the mutex handle (for drivers that need timed waits). */
SemaphoreHandle_t spi_bus_get_mutex(void);

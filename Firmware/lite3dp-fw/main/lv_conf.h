/*
 * LVGL configuration for Lite3DP Gen 2
 *
 * When using the ESP component manager, LVGL reads this from the project root.
 * Only non-default values need to be specified here.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Color depth: 16-bit RGB565 to match ILI9481 */
#define LV_COLOR_DEPTH          16

/* Big-endian color for SPI displays */
#define LV_COLOR_16_SWAP        1

/* Memory: use malloc/free (ESP-IDF heap) */
#define LV_MEM_CUSTOM           1
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC     malloc
#define LV_MEM_CUSTOM_FREE      free
#define LV_MEM_CUSTOM_REALLOC   realloc

/* Display refresh period (ms) */
#define LV_DISP_DEF_REFR_PERIOD 33

/* Input device read period (ms) */
#define LV_INDEV_DEF_READ_PERIOD 50

/* Enable built-in fonts */
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_40   1

#define LV_FONT_DEFAULT         &lv_font_montserrat_14

/* Enable widgets used by our UI */
#define LV_USE_BTN              1
#define LV_USE_LABEL            1
#define LV_USE_LIST             1
#define LV_USE_SPINBOX          1
#define LV_USE_SLIDER           1
#define LV_USE_BAR              1

/* Enable theme */
#define LV_USE_THEME_DEFAULT    1

/* Enable flex layout */
#define LV_USE_FLEX             1

/* Enable animations */
#define LV_USE_ANIM             1

/* Logging via ESP_LOG */
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN

/* Tick: provided by esp_timer callback */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "esp_timer.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR ((esp_timer_get_time() / 1000))

#endif /* LV_CONF_H */

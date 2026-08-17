/* LVGL assert hook.
 *
 * LVGL's stock LV_ASSERT_HANDLER is `while(1);`, so a failed allocation in its
 * private pool (LV_MEM_SIZE_KILOBYTES — separate from the ESP heap) spins the
 * UI task forever with no output.  The display freezes while WiFi, HTTP and
 * the button task all keep running, and esp_get_free_heap_size() still looks
 * healthy, which makes it very hard to recognise.  abort() turns that into a
 * panic with a decodable backtrace.
 *
 * This lives in a header rather than a -D because the handler has to expand
 * with a trailing semicolon (lv_assert.h supplies none) and CMake strips
 * semicolons from COMPILE_DEFINITIONS as list separators.
 *
 * Wired up via CONFIG_LV_ASSERT_HANDLER_INCLUDE + LV_ASSERT_HANDLER in the
 * top-level CMakeLists.txt.
 */
#pragma once

#include <stdlib.h>

#define LITE3DP_LV_ABORT abort();

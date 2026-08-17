cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Replace LVGL's default `while(1);` assert handler with abort() — see
# lv_hooks/lite3dp_lv_assert.h for why.  The macro body lives in that header
# because CMake strips the required trailing semicolon from COMPILE_DEFINITIONS;
# LVGL pulls the header in via CONFIG_LV_ASSERT_HANDLER_INCLUDE.
# lv_conf_internal.h guards LV_ASSERT_HANDLER with #ifndef, so this -D wins.
idf_build_set_property(INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/lv_hooks" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LV_ASSERT_HANDLER=LITE3DP_LV_ABORT" APPEND)

project(lite3dp-fw)

## Complete Firmware Reimplementation — 80 files, 7 components

### Architecture
```
lite3dp-fw/
├── main/              main.c, Kconfig, LVGL config, component deps
├── components/
│   ├── hal/           GPIO, motor, UV LED, backlight, endstop, buttons (6 modules)
│   ├── display/       ILI9481 TFT, SH1107 OLED, XPT2046 touch (3 drivers)
│   ├── storage/       SD card, NVS profiles, slicer detection (3 modules)
│   ├── pngdec/        Ported PNGdec library (C-only, line-by-line decode)
│   ├── print_engine/  State machine, layer manager, PNG-to-TFT pipeline (3 modules)
│   ├── ui/            LVGL screens, input handler, UI manager (3 modules)
│   └── wifi/          WiFi AP/STA, REST API, web UI, OTA updates (4 modules)
├── partitions.csv     Dual OTA + SPIFFS layout
└── sdkconfig.*        Build variants (OLED/Touch)
```

### What works end-to-end:
- **Print flow**: File browser → slicer auto-detect → print preview (with time estimate) → start print → state machine (calibrate → bottom/transition/normal layers → finish)
- **WiFi**: AP fallback on first boot, STA with saved credentials, mDNS (`lite3dp.local`)
- **Web UI**: Embedded HTML/CSS/JS SPA with status polling, file browser, print control, WiFi config, OTA
- **REST API**: 11 endpoints (status, files, profiles, print control, upload, WiFi config, OTA)
- **Profile editor**: Interactive spinboxes for all 12 parameters, 6 save/load slots
- **Calibration**: Interactive Z-offset with motor jog, save to profile
- **Utilities**: Vat clean (full-screen UV cure), UV LED test, motor test
- **PNG decode**: PNGdec ported as pure C component, line-by-line RGB565 output to TFT
- **File upload**: Raw file upload to SD card via HTTP

### To build:
```bash
# Install ESP-IDF v5.x, then:
cd Firmware/lite3dp-fw
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.oled" build
idf.py flash monitor
```
#pragma once

#include "driver/gpio.h"

/* ── Stepper motor ─────────────────────────────────────────────── */
#define PIN_MOTOR_DIR       GPIO_NUM_26
#define PIN_MOTOR_STEP      GPIO_NUM_27
#define PIN_MOTOR_EN        GPIO_NUM_25

/* ── UV LED & backlight ────────────────────────────────────────── */
#define PIN_UV_LED          GPIO_NUM_16
#define PIN_BACKLIGHT       GPIO_NUM_33

/* ── Sensors ───────────────────────────────────────────────────── */
#define PIN_ENDSTOP         GPIO_NUM_36
#define PIN_SD_DETECT       GPIO_NUM_34
#define PIN_BTN_PLAY        GPIO_NUM_39

/* ── Capacitive touch buttons (main PCB) ──────────────────────── */
#define PIN_BTN_UP          GPIO_NUM_12
#define PIN_BTN_DOWN        GPIO_NUM_14
#define PIN_BTN_NEXT        GPIO_NUM_13
#define PIN_BTN_BACK        GPIO_NUM_32

/* ── SPI bus (shared: TFT + SD + XPT2046) ─────────────────────── */
#define PIN_SPI_CLK         GPIO_NUM_18
#define PIN_SPI_MISO        GPIO_NUM_19
#define PIN_SPI_MOSI        GPIO_NUM_23
#define PIN_SD_CS           GPIO_NUM_5

/* ── XPT2046 touch controller (front LCD FPC) ─────────────────── */
#define PIN_TOUCH_CS        GPIO_NUM_17
#define PIN_TOUCH_IRQ       GPIO_NUM_35

/* ── Motor constants ───────────────────────────────────────────── */
#define MOTOR_STEPS_PER_REV     200
#define MOTOR_MICROSTEPS        64
#define MOTOR_LEADSCREW_PITCH   2.44f   /* mm per revolution (T3.5) */
#define MOTOR_STEPS_PER_MM      ((MOTOR_STEPS_PER_REV * MOTOR_MICROSTEPS) / MOTOR_LEADSCREW_PITCH)

/* ── Touch threshold ───────────────────────────────────────────── */
#define TOUCH_THRESHOLD         20

/**
 * Initialize all GPIO pins to their default states.
 * Must be called before any other HAL functions.
 */
esp_err_t hal_gpio_init(void);

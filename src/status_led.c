/*
 * yx87 status LED controller
 *
 * Drives a single WS2812B LED (data on P0.08, via SPI1 MOSI) as a status
 * indicator:
 *   - Caps Lock on   -> blue
 *   - Charging       -> green  (CHRG/STAT signal on P0.13, active low)
 *   - Battery < 20%  -> red
 *   - otherwise      -> off
 *
 * Priority (highest first): low battery > charging > caps lock.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/rgb_underglow.h>

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

/* CHRG/STAT line on P0.13: open-drain, active low (low = charging) */
#define CHRG_DEV DEVICE_DT_GET(DT_NODELABEL(gpio0))
#define CHRG_PIN 13

/* USB HID LED report bitmask: bit 1 = Caps Lock */
#define HID_LED_CAPS_LOCK 0x02

#define LOW_BATTERY_PCT 20
#define CHRG_POLL_PERIOD_MS K_SECONDS(2)

static bool caps_lock_on;
static bool charging;
static uint8_t battery_level = 100;

static struct k_work_delayable chrg_poll_work;

static void status_led_apply(void) {
    if (battery_level < LOW_BATTERY_PCT) {
        /* Low battery: solid red */
        zmk_rgb_underglow_on();
        zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 0, .s = 100, .b = 40});
    } else if (charging) {
        /* Charging: solid green */
        zmk_rgb_underglow_on();
        zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 90, .s = 100, .b = 40});
    } else if (caps_lock_on) {
        /* Caps lock: solid blue */
        zmk_rgb_underglow_on();
        zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = 210, .s = 100, .b = 40});
    } else {
        zmk_rgb_underglow_off();
    }
}

static void chrg_poll_work_handler(struct k_work *work) {
    int val = gpio_pin_get(CHRG_DEV, CHRG_PIN);

    if (val >= 0) {
        /* Active-low CHRG line: 0 = charging */
        charging = (val == 0);
        status_led_apply();
    }

    k_work_schedule(&chrg_poll_work, CHRG_POLL_PERIOD_MS);
}

static int hid_indicators_cb(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);

    caps_lock_on = (ev->indicators & HID_LED_CAPS_LOCK) != 0;
    status_led_apply();

    return ZMK_EV_EVENT_HANDLED;
}

ZMK_LISTENER(status_led_hid, hid_indicators_cb);
ZMK_SUBSCRIPTION(status_led_hid, zmk_hid_indicators_changed);

static int battery_cb(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    battery_level = ev->state_of_charge;
    status_led_apply();

    return ZMK_EV_EVENT_HANDLED;
}

ZMK_LISTENER(status_led_battery, battery_cb);
ZMK_SUBSCRIPTION(status_led_battery, zmk_battery_state_changed);

static int status_led_init(void) {
    if (!device_is_ready(CHRG_DEV)) {
        LOG_WRN("gpio0 not ready");
    } else {
        gpio_pin_configure(CHRG_DEV, CHRG_PIN, GPIO_INPUT | GPIO_PULL_UP);
    }

    /* Use the solid effect so status colors are exact */
    zmk_rgb_underglow_select_effect(0);

    k_work_init_delayable(&chrg_poll_work, chrg_poll_work_handler);
    k_work_schedule(&chrg_poll_work, K_SECONDS(1));

    return 0;
}

SYS_INIT(status_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

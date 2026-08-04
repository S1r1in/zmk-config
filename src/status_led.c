/*
 * yx87 status LED controller
 *
 * Drives two WS2812-style chains:
 *   - led_strip  (P0.08, SPI1 MOSI): single T80 status LED
 *   - led_strip0 (P0.06, SPI0 MOSI): 81 per-key underglow LEDs
 *     (Kailh socket LEDs, daisy-chained)
 *
 * Status colors (both chains, highest priority first):
 *   - Caps Lock on   -> blue
 *   - Charging       -> green  (CHRG/STAT signal on P0.13, active low)
 *   - Battery < 20%  -> red
 *   - otherwise      -> off
 *
 * Note: BQ24075 #CHG is not wired to the MCU yet; P0.13 reads POWER_PIN
 * (power switch sense). Charging detection is kept for when #CHG is
 * fly-wired - flip CHRG_PIN/polarity then.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/rgb_underglow.h>

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

/* CHRG/STAT line on P0.13: open-drain, active low (low = charging) */
#define CHRG_DEV DEVICE_DT_GET(DT_NODELABEL(gpio0))
#define CHRG_PIN 13

/* USB HID LED report bitmask: bit 1 = Caps Lock */
#define HID_LED_CAPS_LOCK 0x02

#define LOW_BATTERY_PCT 20
#define CHRG_POLL_PERIOD_MS K_SECONDS(2)

/* Per-key underglow chain (81 LEDs on P0.06) */
#define STRIP0_DEV DEVICE_DT_GET(DT_NODELABEL(led_strip0))
#define STRIP0_LEN 81

static bool caps_lock_on;
static bool charging;
static bool usb_connected;
static uint8_t battery_level = 100;

static struct k_work_delayable chrg_poll_work;

static void underglow_set_all(struct led_rgb *px, uint32_t len, uint8_t h, uint8_t s, uint8_t b) {
    /* Convert HSB to RGB (simple, good enough for status colors) */
    uint8_t region = h / 60;
    uint8_t f = (h % 60) * 255 / 60;
    uint8_t p = b * (255 - s) / 255;
    uint8_t q = b * (255 - (s * f) / 255) / 255;
    uint8_t t = b * (255 - (s * (255 - f)) / 255) / 255;
    uint8_t r, g, bl;
    switch (region) {
    case 0: r = b; g = t; bl = p; break;
    case 1: r = q; g = b; bl = p; break;
    case 2: r = p; g = b; bl = t; break;
    case 3: r = p; g = q; bl = b; break;
    case 4: r = t; g = p; bl = b; break;
    default: r = b; g = p; bl = q; break;
    }
    for (uint32_t i = 0; i < len; i++) {
        px[i].r = r;
        px[i].g = g;
        px[i].b = bl;
    }
}

static void status_led_apply(void) {
    uint8_t h = 0, s = 100, b = 40;
    bool on = false;

    if (battery_level < LOW_BATTERY_PCT) {
        /* Low battery: solid red (highest priority) */
        h = 0;
        on = true;
    } else if (usb_connected) {
        /* Charging inferred from USB connection (#CHG not wired to MCU):
         * full -> cyan, charging -> green */
        if (battery_level >= 100) {
            h = 180; /* charged: cyan */
        } else {
            h = 90; /* charging: green */
        }
        on = true;
    } else if (caps_lock_on) {
        /* Caps lock: solid blue */
        h = 210;
        on = true;
    }

    /* T80 status LED via ZMK underglow API */
    if (on) {
        zmk_rgb_underglow_on();
        zmk_rgb_underglow_set_hsb((struct zmk_led_hsb){.h = h, .s = s, .b = b});
    } else {
        zmk_rgb_underglow_off();
    }

    /* 81 per-key LEDs via raw LED strip API */
    static struct led_rgb px[STRIP0_LEN];
    if (device_is_ready(STRIP0_DEV)) {
        if (on) {
            /* Dimmer for per-key chain to avoid glare */
            underglow_set_all(px, STRIP0_LEN, h, s, 20);
        } else {
            memset(px, 0, sizeof(px));
        }
        led_strip_update_rgb(STRIP0_DEV, px, STRIP0_LEN);
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

static int usb_conn_cb(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);

    usb_connected = (ev->conn_state != ZMK_USB_CONN_NONE);
    status_led_apply();

    return ZMK_EV_EVENT_HANDLED;
}

ZMK_LISTENER(status_led_usb, usb_conn_cb);
ZMK_SUBSCRIPTION(status_led_usb, zmk_usb_conn_state_changed);

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

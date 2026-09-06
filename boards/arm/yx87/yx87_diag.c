/*
 * yx87 bringup diagnostic v2:
 *  1. Solid BLUE on-board LED (P0.15, plain GPIO - no SPI dependency)
 *     => proves the code runs and GPIO works
 *  2. Delayed RED on T80 WS2812 (SPI1/P0.08), retried after 1s
 *     => proves SPI + WS2812 path (or exposes init-order issue)
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(yx87_diag, LOG_LEVEL_INF);

#define BLUE_LED_NODE DT_NODELABEL(gpio0)
#define BLUE_LED_PIN 15

static void diag_red_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(diag_red_work, diag_red_work_handler);

static void diag_red_work_handler(struct k_work *work)
{
	const struct device *strip = DEVICE_DT_GET(DT_NODELABEL(led_strip));

	if (!device_is_ready(strip)) {
		LOG_ERR("diag: led_strip NOT ready (SPI init order?)");
		return;
	}

	struct led_rgb px = {
		.r = 200,
		.g = 0,
		.b = 0,
	};
	int err = led_strip_update_rgb(strip, &px, 1);
	LOG_INF("diag: T80 red update ret=%d", err);
}

static int diag_led_init(void)
{
	/* 1. Solid blue on-board LED via plain GPIO */
	const struct device *gpio0 = DEVICE_DT_GET(BLUE_LED_NODE);

	if (device_is_ready(gpio0)) {
		gpio_pin_configure(gpio0, BLUE_LED_PIN, GPIO_OUTPUT_ACTIVE);
		gpio_pin_set(gpio0, BLUE_LED_PIN, 1);
		LOG_INF("diag: blue LED ON");
	} else {
		LOG_ERR("diag: gpio0 not ready");
	}

	/* 2. Try T80 red now, and again after 1s (drivers all up by then) */
	const struct device *strip = DEVICE_DT_GET(DT_NODELABEL(led_strip));
	if (device_is_ready(strip)) {
		struct led_rgb px = {
			.r = 200,
			.g = 0,
			.b = 0,
		};
		int err = led_strip_update_rgb(strip, &px, 1);
		LOG_INF("diag: T80 red immediate ret=%d", err);
	} else {
		LOG_WRN("diag: led_strip not ready yet, scheduling retry");
	}

	k_work_schedule(&diag_red_work, K_SECONDS(1));
	return 0;
}

SYS_INIT(diag_led_init, APPLICATION, 90);

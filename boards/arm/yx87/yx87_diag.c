/*
 * yx87 bringup diagnostic: light the T80 status LED (WS2812 on SPI1/P0.08,
 * chain-length 1) solid red once device/driver init completes.
 *
 * Purpose: tell whether the system boots far enough to run APPLICATION-level
 * init. If the LED lights red, drivers are initialized and the hang (if any)
 * is later (ZMK main / USB). If it stays dark, the system hangs during early
 * boot (PRE_KERNEL / POST_KERNEL driver init).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(yx87_diag, LOG_LEVEL_INF);

static int diag_led_init(void)
{
	const struct device *strip = DEVICE_DT_GET(DT_NODELABEL(led_strip));

	if (!device_is_ready(strip)) {
		LOG_ERR("led_strip not ready");
		return 0;
	}

	struct led_rgb px = {
		.r = 200,
		.g = 0,
		.b = 0,
	};
	int err = led_strip_update_rgb(strip, &px, 1);
	if (err) {
		LOG_ERR("led_strip update failed: %d", err);
	} else {
		LOG_INF("diag: T80 LED set to RED");
	}
	return 0;
}

SYS_INIT(diag_led_init, APPLICATION, 90);

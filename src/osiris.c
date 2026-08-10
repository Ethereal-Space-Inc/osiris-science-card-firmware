/**
 * osiris.c
 *
 * Controls the bootup of the payload based on CANopen timeouts
 * 
 */
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <canopennode.h>
#include <CO_OD.h>


LOG_MODULE_REGISTER(osiris, LOG_LEVEL_INF);

extern const k_tid_t osiris_id;

/* 1000 msec = 1 sec */
#define INTERLOOP_DELAY 1000

/* size of stack area used by each thread */
#define OSIRIS_THREAD_STACK_SIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY -11

/* === GPIO data === */
#define BP_NODE DT_NODELABEL(scigpios)
static const struct gpio_dt_spec btldin_0 = GPIO_DT_SPEC_GET(BP_NODE, btldin_0_gpios);
static const struct gpio_dt_spec btldin_1 = GPIO_DT_SPEC_GET(BP_NODE, btldin_1_gpios);
static const struct gpio_dt_spec ocxo_nen = GPIO_DT_SPEC_GET(BP_NODE, ocxo_no_en_gpios);
static const struct gpio_dt_spec sep_nen = GPIO_DT_SPEC_GET(BP_NODE, sep_no_en_gpios);
static const struct gpio_dt_spec pico_nen = GPIO_DT_SPEC_GET(BP_NODE, pico_no_en_gpios);

static uint8_t state;

/* helper function to check that all of the gpios are working */
int verify_gpios(void)
{
	int ret;
	/* A bit ugly, but would otherwise take up much more room. */

	if (!device_is_ready(btldin_0.port)) {LOG_ERR("btldin_0 not ready");return -1;}
	if (!device_is_ready(btldin_1.port)) {LOG_ERR("btldin_1 not ready");return -1;}
	if (!device_is_ready(ocxo_nen.port)) {LOG_ERR("ocxo_nen not ready");return -1;}
	if (!device_is_ready(sep_nen.port )) {LOG_ERR("sep_nen  not ready");return -1;}
	if (!device_is_ready(pico_nen.port)) {LOG_ERR("pico_nen not ready");return -1;}

	/* Set it up */
	ret = gpio_pin_configure_dt(&btldin_0, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {LOG_ERR("Could not configure the btldin_0 GPIO pin: %d", ret);return ret;}
	ret = gpio_pin_configure_dt(&btldin_1, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {LOG_ERR("Could not configure the btldin_1 GPIO pin: %d", ret);return ret;}
	ret = gpio_pin_configure_dt(&ocxo_nen, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {LOG_ERR("Could not configure the btldin_0 GPIO pin: %d", ret);return ret;}
	ret = gpio_pin_configure_dt(&sep_nen, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {LOG_ERR("Could not configure the sep_nen  GPIO pin: %d", ret);return ret;}
	ret = gpio_pin_configure_dt(&pico_nen, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {LOG_ERR("Could not configure the pico_nen GPIO pin: %d", ret);return ret;}
	return 0;
}

uint8_t read_ocxo(void)
{
	gpio_flags_t * flag = 0;
	int ret;

	ret = gpio_pin_get_config_dt(&ocxo_nen, flag);
	if (ret < 0) {return 255;}
	switch (*flag) {
		case GPIO_OUTPUT_INACTIVE:
			return 0;
		case GPIO_OUTPUT_ACTIVE:
			return 1;
		default:
			return 254;
	}
}

uint8_t read_sep(void)
{
	gpio_flags_t * flag = 0;
	int ret;

	ret = gpio_pin_get_config_dt(&sep_nen, flag);
	if (ret < 0) {return 255;}
	switch (*flag) {
		case GPIO_OUTPUT_INACTIVE:
			return 0;
		case GPIO_OUTPUT_ACTIVE:
			return 1;
		default:
			return 254;
	}
}

uint8_t read_pico(void)
{
	gpio_flags_t * flag = 0;
	int ret;

	ret = gpio_pin_get_config_dt(&pico_nen, flag);
	if (ret < 0) {return 255;}
	switch (*flag) {
		case GPIO_OUTPUT_INACTIVE:
			return 0;
		case GPIO_OUTPUT_ACTIVE:
			return 1;
		default:
			return 254;
	}
}

static void handle_can_open_data(void)
{
    CO_LOCK_OD();

	CO_OD_RAM.status = 1;

	CO_OD_RAM.powerup.ocxo_gpio_state = read_ocxo();
	CO_OD_RAM.powerup.sep_gpio_state = read_sep();
	CO_OD_RAM.powerup.pico_gpio_state = read_pico();

	state = CO_OD_RAM.powerup.powerup_state;
    CO_UNLOCK_OD();

	switch (state) {
		case 0:
			gpio_pin_set_dt(&ocxo_nen, false);
			gpio_pin_set_dt(&sep_nen, false);
			gpio_pin_set_dt(&pico_nen, false);
			return;
		case 1:
			gpio_pin_set_dt(&ocxo_nen, true);
			gpio_pin_set_dt(&sep_nen, false);
			gpio_pin_set_dt(&pico_nen, false);
			return;
		case 2:
			gpio_pin_set_dt(&ocxo_nen, true);
			gpio_pin_set_dt(&sep_nen, true);
			gpio_pin_set_dt(&pico_nen, false);
			return;
		case 3:
			gpio_pin_set_dt(&ocxo_nen, true);
			gpio_pin_set_dt(&sep_nen, true);
			gpio_pin_set_dt(&pico_nen, true);
			return;
		default:
			return;
	}
}

static int handle_bootup(void)
{
	int ret;
	ret = verify_gpios();
	if (ret < 0) {
		return ret;
	}
	state = 0;

	LOG_INF("Starting osiris powerup state machine");
	/* for the sake of simplicity I will be using ints to mark the state. Refer to oresat-configs-esi for the definitions */
	while (1) {
		handle_can_open_data();
		k_msleep(INTERLOOP_DELAY);
	}
	return 0;
}

K_THREAD_DEFINE(osiris_id, OSIRIS_THREAD_STACK_SIZE, handle_bootup, NULL, NULL, NULL, PRIORITY, 0, 0);

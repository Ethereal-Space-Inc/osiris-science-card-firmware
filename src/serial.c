/*
 *  Handles relaying information from the c3 to the picozed.
 *
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(esi_passthrough, CONFIG_LOG_DEFAULT_LEVEL);

struct patch_info {
	const uint8_t * const name;

	const struct device *rx_dev;
	struct ring_buf *rx_ring_buf;
	bool rx_error;
	bool rx_overflow;

	const struct device *tx_dev;
};

extern const k_tid_t serial_id;

/* size of stack area used by each thread */
#define SERIAL_THREAD_STACK_SIZE 4096

/* scheduling priority used by each thread */
#define SERIAL_THREAD_PRIORITY 0

#define DEV_CCC	DEVICE_DT_GET(DT_CHOSEN(uart_ccc))
#define DEV_INST DEVICE_DT_GET(DT_ALIAS(instr))
#define DEV_CONSOLE	DEVICE_DT_GET(DT_CHOSEN(zephyr_console))

#define RING_BUF_SIZE 64

RING_BUF_DECLARE(rb_ccc, RING_BUF_SIZE);
struct patch_info patch_ccc2inst = {
	.name = "ccc2inst",

	.rx_dev = DEV_CCC,
	.rx_ring_buf = &rb_ccc,
	.rx_error = false,
	.rx_overflow = false,

	.tx_dev = DEV_INST,
};

RING_BUF_DECLARE(rb_inst, RING_BUF_SIZE);
struct patch_info patch_inst2ccc = {
	.name = "inst2ccc",

	.rx_dev = DEV_INST,
	.rx_ring_buf = &rb_inst,
	.rx_error = false,
	.rx_overflow = false,

	.tx_dev = DEV_CCC,
};

static void uart_cb(const struct device *dev, void *ctx)
{
	struct patch_info *patch = (struct patch_info *)ctx;
	int ret;
	uint8_t *buf;
	uint32_t len;

	while (uart_irq_update(patch->rx_dev) > 0) {
		ret = uart_irq_rx_ready(patch->rx_dev);
		if (ret < 0) {
			patch->rx_error = true;
		}
		if (ret <= 0) {
			break;
		}

		len = ring_buf_put_claim(patch->rx_ring_buf, &buf, RING_BUF_SIZE);
		if (len == 0) {
			/* no space for Rx, disable the IRQ */
			uart_irq_rx_disable(patch->rx_dev);
			patch->rx_overflow = true;
			break;
		}

		ret = uart_fifo_read(patch->rx_dev, buf, len);
		if (ret < 0) {
			patch->rx_error = true;
		}
		if (ret <= 0) {
			break;
		}
		len = ret;

		ret = ring_buf_put_finish(patch->rx_ring_buf, len);
		if (ret != 0) {
			patch->rx_error = true;
			break;
		}
	}
}

struct uart_config uart_cfg = {
	.baudrate = 9600,
	.parity = UART_CFG_PARITY_NONE,
	.stop_bits = UART_CFG_STOP_BITS_1,
	.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	.data_bits = UART_CFG_DATA_BITS_8,
};

static void passthrough(struct patch_info *patch)
{
	int ret;
	uint8_t *buf;
	uint32_t len;

	if (patch->rx_error) {
		printk("<<%s: Rx Error!>>\n", patch->name);
		patch->rx_error = false;
	}

	if (patch->rx_overflow) {
		printk("<<%s: Rx Overflow!>>\n", patch->name);
		patch->rx_overflow = false;
	}

	len = ring_buf_get_claim(patch->rx_ring_buf, &buf, RING_BUF_SIZE);
	if (len == 0) {
		goto done;
	}
	struct uart_config *cfg;

	ret = uart_fifo_fill(patch->tx_dev, buf, len);
	// uart_poll_out(patch->tx_dev, buf[0]);
	if (ret < 0) {
		goto error;
	}
	len = ret;

	ret = ring_buf_get_finish(patch->rx_ring_buf, len);
	if (ret < 0) {
		goto error;
	}

done:
	uart_irq_rx_enable(patch->rx_dev);
	return;

error:
	printk("<<%s: Tx Error!>>\n", patch->name);
}

static int handle_serial (void *p1, void *p2, void *p3)
{
	/* Can we use the LED? */
	if (!device_is_ready(DEV_CCC)) {
		LOG_ERR("LED device is not ready");
		return -1;
	}
	if (!device_is_ready(DEV_INST)) {
		LOG_ERR("LED device is not ready");
		return -1;
	}

	printk("Console Device: %p\n", patch_ccc2inst.rx_dev);
	printk("Other Device:   %p\n", patch_inst2ccc.rx_dev);

	// int rc = uart_configure(DEV_INST, &uart_cfg);
	// if (rc) {
	// 	printk("Could not configure device %s", DEV_INST -> name);
	// 	LOG_ERR("AAAAAAAAAAAAAAA");
	// 	return -1;
	// }

	uart_irq_callback_user_data_set(patch_ccc2inst.rx_dev, uart_cb, (void *)&patch_ccc2inst);
	uart_irq_callback_user_data_set(patch_inst2ccc.rx_dev, uart_cb, (void *)&patch_inst2ccc);

	for (;;) {
		passthrough(&patch_ccc2inst);
		passthrough(&patch_inst2ccc);
	}

	return 0;
}

K_THREAD_DEFINE(serial_id, SERIAL_THREAD_STACK_SIZE, handle_serial, NULL, NULL, NULL, SERIAL_THREAD_PRIORITY, 0, 0);

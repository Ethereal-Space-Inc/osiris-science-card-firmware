/*
 *  Handles relaying information from the c3 to the picozed.
 *
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(oresat_passthrough, LOG_LEVEL_INF);

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
#define SERIAL_THREAD_STACK_SIZE 1024

/* scheduling priority used by each thread */
#define SERIAL_THREAD_PRIORITY -12

#define DEV_CCC	DEVICE_DT_GET(DT_CHOSEN(uart_ccc))
#define DEV_INST DEVICE_DT_GET(DT_CHOSEN(uart_inst))

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

	ret = uart_fifo_fill(patch->tx_dev, buf, len);
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

void handle_serial (void *p1, void *p2, void *p3)
{
	LOG_INF("Starting serial passthrough");
	uart_irq_callback_user_data_set(patch_ccc2inst.rx_dev, uart_cb, (void *)&patch_ccc2inst);
	uart_irq_callback_user_data_set(patch_inst2ccc.rx_dev, uart_cb, (void *)&patch_inst2ccc);

	for (;;) {
		passthrough(&patch_ccc2inst);
		passthrough(&patch_inst2ccc);
	}

	return;
}

K_THREAD_DEFINE(serial_id, SERIAL_THREAD_STACK_SIZE, handle_serial, NULL, NULL, NULL, SERIAL_THREAD_PRIORITY, 0, 0);

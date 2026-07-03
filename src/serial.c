/*
 *  Handles relaying information from the c3 to the picozed.
 *
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

#include <stdio.h>
#include <string.h>

struct patch_info {
	const uint8_t * const name;

	const struct device *rx_dev;
	struct ring_buf *rx_ring_buf;
	bool rx_error;
	bool rx_overflow;

	const struct device *tx_dev;
};

/* #define UART_PICOZED DEVICE_DT_GET(DT_CHOSEN(uart_picoze)); */
#define UART_PICOZED DEVICE_DT_GET(DT_CHOSEN(uart_picozed));

#define RING_BUF_SIZE 65536

RING_BUF_DECLARE(rb_console, RING_BUF_SIZE);
struct patch_info patch_c2o = {
	.name = "c2o",

	.rx_dev = DEV_CONSOLE,
	.rx_ring_buf = &rb_console,
	.rx_error = false,
	.rx_overflow = false,

	.tx_dev = DEV_OTHER,
};

RING_BUF_DECLARE(rb_other, RING_BUF_SIZE);
struct patch_info patch_o2c = {
	.name = "o2c",

	.rx_dev = DEV_OTHER,
	.rx_ring_buf = &rb_other,
	.rx_error = false,
	.rx_overflow = false,

	.tx_dev = DEV_CONSOLE,
};

#define SERIAL_THREAD_STACK_SIZE 2048
#define SERIAL_THREAD_PRIORITY -10
extern const k_tid_t serial_id;

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct net_buf *buf;
	int rc;

	LOG_DBG("EVENT: %d", evt->type);

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("TX complete %p", tx_pending_buffer);

		/* Free TX buffer */
		net_buf_unref(tx_pending_buffer);
		tx_pending_buffer = NULL;

		/* Handle any queued buffers */
		buf = k_fifo_get(&tx_queue, K_NO_WAIT);
		if (buf != NULL) {
			rc = uart_tx(dev, buf->data, buf->len, 0);
			if (rc != 0) {
				LOG_ERR("TX from ISR failed (%d)", rc);
				net_buf_unref(buf);
			} else {
				tx_pending_buffer = buf;
			}
		}
		break;
	case UART_RX_BUF_REQUEST:
		/* Return the next buffer index */
		LOG_DBG("Providing buffer index %d", async_rx_buffer_idx);
		rc = uart_rx_buf_rsp(dev, async_rx_buffer[async_rx_buffer_idx],
				     sizeof(async_rx_buffer[0]));
		__ASSERT_NO_MSG(rc == 0);
		async_rx_buffer_idx = async_rx_buffer_idx ? 0 : 1;
		break;
	case UART_RX_BUF_RELEASED:
	case UART_RX_DISABLED:
		break;
	case UART_RX_RDY:
		LOG_HEXDUMP_INF(evt->data.rx.buf + evt->data.rx.offset,
				evt->data.rx.len, "RX_RDY");
		break;
	default:
		LOG_WRN("Unhandled event %d", evt->type);
	}
}

void handle_serial (void *p1, void *p2, void *p3)
{

}

K_THREAD_DEFINE(serial_id, SERIAL_THREAD_STACK_SIZE, handle_serial, NULL, NULL, NULL, SERIAL_THREAD_PRIORITY, 0, 0);

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include "ble_scanner.h"
#include "uart_commands.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	/* Initialize BLE scanner */
	err = ble_scanner_init();
	if (err) {
		LOG_ERR("BLE scanner init failed (err %d)", err);
		return;
	}

	ble_scanner_start();
}

int main(void)
{
	int err;

	/* Initialize UART commands */
	err = uart_commands_init();
	if (err) {
		LOG_ERR("UART commands init failed (err %d)", err);
		return err;
	}

	/* Initialize Bluetooth */
	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	/* Start UART command interface */
	uart_commands_start();

	return 0;
}
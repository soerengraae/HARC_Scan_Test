#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

#include "vcp_controller.h"
#include "ble_scanner.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/* Bluetooth ready callback */
static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	/* Initialize VCP controller */
	err = vcp_controller_init();
	if (err) {
		LOG_ERR("VCP controller init failed (err %d)", err);
		return;
	}

	/* Initialize BLE scanner */
	err = ble_scanner_init();
	if (err) {
		LOG_ERR("BLE scanner init failed (err %d)", err);
		return;
	}

	/* Start scanning for VCP devices */
	ble_scanner_start();
}

int main(void)
{
	int err;

	LOG_INF("VCP Controller Application Starting");

	/* Initialize Bluetooth */
	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	/* Main application loop */
	while (true) {
		k_sleep(K_SECONDS(5));

		if (!default_conn) {
			LOG_INF("Restarting scan...");
			ble_scanner_start();
		} else if (vcp_discovered) {
			vcp_controller_demo();
		} else {
			LOG_INF("Discovering VCP services...");
			err = bt_vcp_vol_ctlr_discover(default_conn, &vol_ctlr);
			if (err) {
				LOG_ERR("VCP discovery failed (err %d)", err);
			}
		}
	}

	return 0;
}
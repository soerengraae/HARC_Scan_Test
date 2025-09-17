#include "ble_scanner.h"
#include "vcp_controller.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/audio/vcp.h>

LOG_MODULE_REGISTER(ble_scanner, LOG_LEVEL_DBG);

static bool should_connect = false;
static int scan_retry_count = 0;

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("Failed to connect to %s (%u)", addr, err);
		return;
	}

	LOG_INF("Connected: %s", addr);
	default_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

	vcp_controller_reset_state();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* Device discovery functions */
static bool device_found(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;
	char addr_str[BT_ADDR_LE_STR_LEN];
	int i;

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	switch (data->type) {
	case BT_DATA_UUID16_SOME:
	case BT_DATA_UUID16_ALL:
		if (data->data_len % sizeof(uint16_t) != 0U) {
			LOG_WRN("AD malformed");
			return true;
		}

		for (i = 0; i < data->data_len; i += sizeof(uint16_t)) {
			const struct bt_uuid *uuid;
			uint16_t u16;

			memcpy(&u16, &data->data[i], sizeof(u16));
			uuid = BT_UUID_DECLARE_16(sys_le16_to_cpu(u16));

			if (bt_uuid_cmp(uuid, BT_UUID_VCS) == 0) {
				LOG_INF("Found VCP device: %s", addr_str);
				should_connect = true;
				return false;
			}
		}
		break;
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED:
		if (memcmp(data->data, "Renderer", 8) == 0 ||
		    memcmp(data->data, "VCP", 3) == 0) {
			LOG_INF("Found potential VCP device by name: %s", addr_str);
			should_connect = true;
			return false;
		}
		break;
	}

	return true;
}

static void device_found_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			    struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	int err;

	if (default_conn) {
		return;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	should_connect = false;

	bt_data_parse(ad, device_found, (void *)addr);

	if (!should_connect) {
		return;
	}

	LOG_INF("Attempting to connect to %s", addr_str);

	err = bt_le_scan_stop();
	if (err) {
		LOG_ERR("Stop LE scan failed (err %d)", err);
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &default_conn);
	if (err) {
		LOG_ERR("Create connection failed (err %d)", err);
	}
}

/* Start BLE scanning */
void ble_scanner_start(void)
{
	int err;

	/* Stop any existing scan first */
	bt_le_scan_stop();

	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found_cb);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning successfully started");
}

/* Initialize BLE scanner */
int ble_scanner_init(void)
{
	LOG_INF("BLE scanner initialized");
	return 0;
}

/* Reset scan retry count */
void ble_scanner_reset_retry_count(void)
{
	scan_retry_count = 0;
}
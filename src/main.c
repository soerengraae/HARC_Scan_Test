#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(vcp_controller, LOG_LEVEL_DBG);

static struct bt_conn *default_conn;
static struct bt_vcp_vol_ctlr *vol_ctlr;
static bool vcp_discovered = false;
static bool should_connect = false;

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

	vcp_discovered = false;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

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
		if (memcmp(data->data, "VCP Renderer", 12) == 0 ||
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

static void vcp_discover_cb(struct bt_vcp_vol_ctlr *vcp_vol_ctlr, int err,
			    uint8_t vocs_count, uint8_t aics_count)
{
	if (err) {
		LOG_ERR("VCP discovery failed (err %d)", err);
		return;
	}

	LOG_INF("VCP discovery complete - VOCS: %u, AICS: %u", vocs_count, aics_count);
	vol_ctlr = vcp_vol_ctlr;
	vcp_discovered = true;
}

static void vcp_vol_down_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP volume down error (err %d)", err);
		return;
	}

	LOG_INF("Volume down success");
}

static void vcp_vol_up_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP volume up error (err %d)", err);
		return;
	}

	LOG_INF("Volume up success");
}

static struct bt_vcp_vol_ctlr_cb vcp_callbacks = {
	.discover = vcp_discover_cb,
	.vol_down = vcp_vol_down_cb,
	.vol_up = vcp_vol_up_cb,
};

static void start_scan(void)
{
	int err;

	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found_cb);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning successfully started");
}

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	err = bt_vcp_vol_ctlr_cb_register(&vcp_callbacks);
	if (err) {
		LOG_ERR("Failed to register VCP callbacks (err %d)", err);
		return;
	}

	start_scan();
}

static void volume_control_demo(void)
{
	int err;
	static bool volume_up = true;

	if (!vol_ctlr || !vcp_discovered) {
		return;
	}

	if (volume_up) {
		err = bt_vcp_vol_ctlr_vol_up(vol_ctlr);
		if (err) {
			LOG_ERR("Failed to volume up (err %d)", err);
		} else {
			LOG_INF("Volume up requested");
		}
		volume_up = false;
	} else {
		err = bt_vcp_vol_ctlr_vol_down(vol_ctlr);
		if (err) {
			LOG_ERR("Failed to volume down (err %d)", err);
		} else {
			LOG_INF("Volume down requested");
		}
		volume_up = true;
	}
}

int main(void)
{
	int err;

	LOG_INF("VCP Controller starting");

	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return err;
	}

	while (true) {
		k_sleep(K_SECONDS(5));

		if (!default_conn) {
			LOG_INF("Restarting scan...");
			start_scan();
		} else if (vcp_discovered) {
			volume_control_demo();
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

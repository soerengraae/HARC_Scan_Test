#include "ble_scanner.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/audio/vcp.h>

LOG_MODULE_REGISTER(ble_scanner, LOG_LEVEL_DBG);

#define MAX_DISCOVERED_DEVICES_MEMORY_SIZE 2048 // 2 KB
#define BT_NAME_MAX_LEN 30

struct k_work_delayable printDevicesWork;

static void printDevicesHandler(struct k_work *work)
{
	(void)(work);
	print_discovered_devices();
	k_work_schedule(&printDevicesWork, K_SECONDS(10));
}

static bool should_connect = false;
static int scan_retry_count = 0;

// Linked list of discovered device address to avoid printing duplicates
static struct bt_device_node {
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN + 1];
	struct bt_device_node *next;
} *discovered_devices = NULL;

static size_t get_discovered_devices_memory_used(void)
{
	struct bt_device_node *current = discovered_devices;
	size_t total_size = 0;

	while (current) {
		total_size += sizeof(*current);
		current = current->next;
	}

	return total_size;
}

/* Save discovered device address */
static bool save_discovered_device(const bt_addr_le_t *addr, char *name)
{
	struct bt_device_node *current = discovered_devices;

	/* Check if the address is already in the list */
	while (current) {
		if (!bt_addr_le_cmp(&current->addr, addr)) {
			return false; // Address already exists
		}
		current = current->next;
	}

	/* Add new address to the list */
	if (get_discovered_devices_memory_used() + sizeof(*current) > MAX_DISCOVERED_DEVICES_MEMORY_SIZE) {
		LOG_WRN("Memory limit reached - cannot save more addresses");
		return false;
	}

	struct bt_device_node *new_node = k_malloc(sizeof(*new_node));
	if (!new_node) {
		LOG_ERR("Memory allocation failed");
		return false;
	}

	bt_addr_le_copy(&new_node->addr, addr);
	strncpy(new_node->name, name, BT_NAME_MAX_LEN); // Copy name with max length
	new_node->name[BT_NAME_MAX_LEN] = '\0'; // Ensure null-termination
	new_node->next = discovered_devices;
	discovered_devices = new_node;

	return true;
}

void print_discovered_devices(void)
{
	LOG_INF("Discovered devices:");
	struct bt_device_node *current = discovered_devices;
	if (!current) {
		return;
	} else {
		while (current) {
			char addr_str[BT_ADDR_LE_STR_LEN];
			bt_addr_le_to_str(&current->addr, addr_str, sizeof(addr_str));
			LOG_INF(" - %s, %s", addr_str, current->name);
			current = current->next;
		}
	}
}

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
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

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	switch (data->type) {
		case BT_DATA_NAME_COMPLETE:
		case BT_DATA_NAME_SHORTENED:
			char name[BT_NAME_MAX_LEN + 1] = {0};
			int copy_len = MIN(data->data_len, BT_NAME_MAX_LEN);
			memcpy(name, data->data, copy_len);
			name[copy_len] = '\0';

			if (save_discovered_device(addr, name))
				LOG_DBG("Device found: %s, Name: %s", addr_str, name);
			
			break;
	}

	return true;
}

static void device_found_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			    struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	should_connect = false;

	bt_data_parse(ad, device_found, (void *)addr);
}

/* Start BLE scanning */
void ble_scanner_start(void)
{
	int err;

	/* Stop any existing scan first */
	bt_le_scan_stop();

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found_cb);
	if (err) {
		LOG_ERR("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning successfully started");

	k_work_init_delayable(&printDevicesWork, printDevicesHandler);
	k_work_schedule(&printDevicesWork, K_SECONDS(10));
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
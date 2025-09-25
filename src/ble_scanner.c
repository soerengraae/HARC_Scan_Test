#include "ble_scanner.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/audio/vcp.h>
#include <string.h>

LOG_MODULE_REGISTER(ble_scanner, LOG_LEVEL_DBG);

#define MAX_DISCOVERED_DEVICES_MEMORY_SIZE 1024 // 1 KB
#define BT_NAME_MAX_LEN 12

struct k_work_delayable printDevicesWork;

static void printDevicesHandler(struct k_work *work)
{
	(void)(work);
	print_discovered_devices();
	k_work_schedule(&printDevicesWork, K_SECONDS(10));
}

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
	if (!discovered_devices) {
		LOG_INF("No devices discovered yet.");
		return;
	}

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

/* Structure to pass data between parsing callbacks */
struct device_info {
	bt_addr_le_t *addr;
	char name[BT_NAME_MAX_LEN + 1];
	bool has_service;
	bool has_name;
};

/* Device discovery function
   Extracts HAS service and device name from advertisement data */
static bool device_found(struct bt_data *data, void *user_data)
{
	struct device_info *info = (struct device_info *)user_data;
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(info->addr, addr_str, sizeof(addr_str));
	// if (strcmp(addr_str, "60:41:42:63:63:53 (random)") != 0)
	// {
	// 	return false; // Skip processing for this specific address
	// }

	/* Check for Hearing Access Service UUID */
	if (data->type == BT_DATA_UUID16_ALL || data->type == BT_DATA_UUID16_SOME) {
		if (data->data_len % 2 != 0) {
			LOG_WRN("Invalid UUID16 data length from %s", addr_str);
			return true;
		}

		for (size_t i = 0; i < data->data_len; i += 2) {
			uint16_t uuid_val = sys_get_le16(&data->data[i]);
			struct bt_uuid_16 uuid = BT_UUID_INIT_16(uuid_val);

			LOG_DBG("Found UUID 0x%04X from %s", uuid_val, addr_str);

			if (bt_uuid_cmp(&uuid, BT_UUID_HAS) == 0) {
				info->has_service = true;
				break;
			}
		}
	}
	/* Extract device name */
	else if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
		size_t name_len = MIN(data->data_len, BT_NAME_MAX_LEN);
		memcpy(info->name, data->data, name_len);
		info->name[name_len] = '\0';
		info->has_name = true;
		LOG_DBG("Found name '%s' from %s", info->name, addr_str);
	}

	return true;
}

static void device_found_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	struct device_info info = {0};

	/* Initialize device info structure */
	info.addr = (bt_addr_le_t *)addr;
	info.has_service = false;
	info.has_name = false;
	memset(info.name, 0, sizeof(info.name));

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

	/* Parse advertisement data to extract HAS service and name */
	bt_data_parse(ad, device_found, &info);

	/* Only save and print devices that have both HAS and a valid name */
	if (info.has_service && info.has_name && strlen(info.name) > 0) {
		if (save_discovered_device(addr, info.name)) {
			LOG_INF("Found HAS device: %s (%s)", info.name, addr_str);
		}
	}
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

	LOG_INF("Scanning for HIs");

	k_work_init_delayable(&printDevicesWork, printDevicesHandler);
	k_work_schedule(&printDevicesWork, K_SECONDS(10));
}

/* Initialize BLE scanner */
int ble_scanner_init(void)
{
	LOG_INF("BLE scanner initialized");
	return 0;
}
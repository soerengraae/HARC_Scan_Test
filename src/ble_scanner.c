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
static struct bt_device_node
{
	bt_addr_le_t addr;
	char name[BT_NAME_MAX_LEN + 1];
	struct bt_device_node *next;
} *discovered_devices = NULL;

static size_t get_discovered_devices_memory_used(void)
{
	struct bt_device_node *current = discovered_devices;
	size_t total_size = 0;

	while (current)
	{
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
	while (current)
	{
		if (!bt_addr_le_cmp(&current->addr, addr))
		{
			return false; // Address already exists
		}
		current = current->next;
	}

	/* Add new address to the list */
	if (get_discovered_devices_memory_used() + sizeof(*current) > MAX_DISCOVERED_DEVICES_MEMORY_SIZE)
	{
		LOG_WRN("Memory limit reached - cannot save more addresses");
		return false;
	}

	struct bt_device_node *new_node = k_malloc(sizeof(*new_node));
	if (!new_node)
	{
		LOG_ERR("Memory allocation failed");
		return false;
	}

	bt_addr_le_copy(&new_node->addr, addr);
	strncpy(new_node->name, name, BT_NAME_MAX_LEN); // Copy name with max length
	new_node->name[BT_NAME_MAX_LEN] = '\0';			// Ensure null-termination
	new_node->next = discovered_devices;
	discovered_devices = new_node;

	return true;
}

struct bt_device_node *search_discovered_devices_by_addr(const bt_addr_le_t *addr)
{
	struct bt_device_node *current = discovered_devices;

	while (current)
	{
		if (!bt_addr_le_cmp(&current->addr, addr))
		{
			return current;
		}

		current = current->next;
	}

	return NULL;
}

struct bt_device_node *search_discovered_devices_by_name(const char *name)
{
	struct bt_device_node *current = discovered_devices;

	while (current)
	{
		if (strcmp(current->name, name) == 0)
		{
			return current;
		}

		current = current->next;
	}

	return NULL;
}

void print_discovered_devices(void)
{
	if (!discovered_devices)
	{
		LOG_INF("No devices discovered yet.");
		return;
	}

	LOG_INF("Discovered devices:");
	struct bt_device_node *current = discovered_devices;
	if (!current)
	{
		return;
	}
	else
	{
		while (current)
		{
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
struct device_info
{
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

	struct bt_device_node *existing_device = search_discovered_devices_by_addr(info->addr);
	if (existing_device && existing_device->name[0] != '\0')
	{
		LOG_DBG("Advertisement data type 0x%X len %u from %s, %s", data->type, data->data_len, addr_str, existing_device->name);

		switch (data->type)
		{
		case BT_DATA_SVC_DATA16:
			// Parse 16-bit service data
			if (data->data_len >= 2)
			{
				for (size_t i = 0; i <= data->data_len - 2; i += 2)
				{
					uint16_t uuid_val = sys_get_le16(&data->data[i]);
					LOG_DBG("Service Data UUID 0x%04X", uuid_val);

					if (uuid_val == 0x1854)
					{ // HAS UUID
						info->has_service = true;
						LOG_INF("Found HAS service data");
						break;
					}
				}
			}
			break;

		case BT_DATA_UUID16_ALL:
		case BT_DATA_UUID16_SOME:
			// Parse 16-bit service UUIDs
			if (data->data_len >= 2)
			{
				for (size_t i = 0; i <= data->data_len - 2; i += 2)
				{
					uint16_t uuid_val = sys_get_le16(&data->data[i]);
					LOG_DBG("16-bit Service UUID 0x%04X", uuid_val);

					if (uuid_val == 0x1854)
					{ // HAS UUID
						info->has_service = true;
						LOG_INF("Found HAS service UUID");
						break;
					}
				}
			}
			break;

		case BT_DATA_UUID128_ALL:
		case BT_DATA_UUID128_SOME:
			// Parse 128-bit UUIDs properly
			if (data->data_len >= 16)
			{
				for (size_t i = 0; i <= data->data_len - 16; i += 16)
				{
					// 128-bit UUIDs are 16 bytes each
					uint8_t uuid_128[16];
					memcpy(uuid_128, &data->data[i], 16);

					// Log the full 128-bit UUID
					LOG_DBG("128-bit UUID: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
							uuid_128[15], uuid_128[14], uuid_128[13], uuid_128[12],
							uuid_128[11], uuid_128[10], uuid_128[9], uuid_128[8],
							uuid_128[7], uuid_128[6], uuid_128[5], uuid_128[4],
							uuid_128[3], uuid_128[2], uuid_128[1], uuid_128[0]);

					// Check if this is a Bluetooth SIG 128-bit UUID based on 16-bit UUID
					// Format: 0000XXXX-0000-1000-8000-00805F9B34FB
					if (uuid_128[0] == 0xFB && uuid_128[1] == 0x34 &&
						uuid_128[2] == 0x9B && uuid_128[3] == 0x5F &&
						uuid_128[4] == 0x80 && uuid_128[5] == 0x00 &&
						uuid_128[6] == 0x00 && uuid_128[7] == 0x80 &&
						uuid_128[8] == 0x00 && uuid_128[9] == 0x10 &&
						uuid_128[10] == 0x00 && uuid_128[11] == 0x00)
					{

						uint16_t short_uuid = sys_get_le16(&uuid_128[12]);
						LOG_DBG("128-bit UUID contains 16-bit UUID: 0x%04X", short_uuid);

						if (short_uuid == 0x1854)
						{
							info->has_service = true;
							LOG_INF("Found HAS service in 128-bit UUID");
						}
					}
				}
			}
			break;

		case BT_DATA_GAP_APPEARANCE:
			if (data->data_len == 2)
			{
				uint16_t appearance = sys_get_le16(data->data);
				LOG_DBG("Appearance: 0x%04X", appearance);

				// Check for hearing aid appearance category (0x0840-0x087F)
				if ((appearance & 0xFFC0) == 0x0840)
				{
					info->has_service = true;
					LOG_INF("Found hearing aid appearance: 0x%04X", appearance);
				}
			}
			break;

		case BT_DATA_MANUFACTURER_DATA:
			if (data->data_len >= 2)
			{
				uint16_t company_id = sys_get_le16(data->data);
				LOG_DBG("Manufacturer ID: 0x%04X", company_id);

				// Known hearing aid manufacturer IDs
				switch (company_id)
				{
				case 0x00D7: // Oticon
				case 0x0136: // Phonak
				case 0x0141: // Widex
				case 0x01DE: // ReSound/GN
				case 0x0260: // Unitron
					info->has_service = true;
					LOG_INF("Found known hearing aid manufacturer: 0x%04X", company_id);
					break;
				default:
					break;
				}

				// Log manufacturer data for analysis
				if (data->data_len > 2)
				{
					LOG_DBG("Manufacturer data (%u bytes): ", data->data_len - 2);
					for (size_t i = 2; i < data->data_len && i < 10; i++)
					{
						LOG_DBG("0x%02X ", data->data[i]);
					}
				}
			}
			break;

		default:
			return true; // Ignore other data types
		}
	}

	switch (data->type)
	{
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED:
		size_t name_len = MIN(data->data_len, BT_NAME_MAX_LEN);
		memcpy(info->name, data->data, name_len);
		info->name[name_len] = '\0';
		info->has_name = true;
		if (existing_device == NULL)
		{
			LOG_DBG("Found name '%s'", info->name);
			save_discovered_device(info->addr, info->name);
		}

		break;
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
}

/* Start BLE scanning */
void ble_scanner_start(void)
{
	int err;

	/* Stop any existing scan first */
	bt_le_scan_stop();

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found_cb);
	if (err)
	{
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
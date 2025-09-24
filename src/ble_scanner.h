#ifndef BLE_SCANNER_H
#define BLE_SCANNER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

/* BLE scanner functions */
int ble_scanner_init(void);
void ble_scanner_start(void);
void ble_scanner_reset_retry_count(void);
void print_discovered_devices(void);

/* Connection management */
extern struct bt_conn_cb conn_callbacks;

#endif /* BLE_SCANNER_H */
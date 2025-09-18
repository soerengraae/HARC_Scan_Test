#ifndef VCP_CONTROLLER_H
#define VCP_CONTROLLER_H

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/vcp.h>

/* VCP Controller state */
extern struct bt_conn *default_conn;
extern struct bt_vcp_vol_ctlr *vol_ctlr;
extern bool vcp_discovered;

/* VCP Controller functions */
int vcp_controller_init(void);
void vcp_controller_demo(void);
void vcp_controller_reset_state(void);

/* Note: bt_vcp_vol_ctlr_vol_up_unmute and bt_vcp_vol_ctlr_vol_down_unmute
 * are provided by Zephyr VCP API - no custom declarations needed */

#endif /* VCP_CONTROLLER_H */
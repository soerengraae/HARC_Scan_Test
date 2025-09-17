#include "vcp_controller.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vcp_controller, LOG_LEVEL_DBG);

/* Global state variables */
struct bt_conn *default_conn;
struct bt_vcp_vol_ctlr *vol_ctlr;
bool vcp_discovered = false;

/* Custom VCP function implementations */
int bt_vcp_vol_ctlr_vol_up_unmute(struct bt_vcp_vol_ctlr *vol_ctlr)
{
	int err;

	/* First unmute */
	err = bt_vcp_vol_ctlr_unmute(vol_ctlr);
	if (err) {
		return err;
	}

	/* Small delay to ensure unmute completes */
	k_sleep(K_MSEC(50));

	/* Then volume up */
	return bt_vcp_vol_ctlr_vol_up(vol_ctlr);
}

int bt_vcp_vol_ctlr_vol_down_unmute(struct bt_vcp_vol_ctlr *vol_ctlr)
{
	int err;

	/* First unmute */
	err = bt_vcp_vol_ctlr_unmute(vol_ctlr);
	if (err) {
		return err;
	}

	/* Small delay to ensure unmute completes */
	k_sleep(K_MSEC(50));

	/* Then volume down */
	return bt_vcp_vol_ctlr_vol_down(vol_ctlr);
}


/* VCP callback implementations */
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

static void vcp_mute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP mute error (err %d)", err);
		return;
	}

	LOG_INF("Mute success");
}

static void vcp_unmute_cb(struct bt_vcp_vol_ctlr *vol_ctlr, int err)
{
	if (err) {
		LOG_ERR("VCP unmute error (err %d)", err);
		return;
	}

	LOG_INF("Unmute success");
}

static struct bt_vcp_vol_ctlr_cb vcp_callbacks = {
	.discover = vcp_discover_cb,
	.vol_down = vcp_vol_down_cb,
	.vol_up = vcp_vol_up_cb,
	.mute = vcp_mute_cb,
	.unmute = vcp_unmute_cb,
};

/* Initialize VCP controller */
int vcp_controller_init(void)
{
	int err;

	err = bt_vcp_vol_ctlr_cb_register(&vcp_callbacks);
	if (err) {
		LOG_ERR("Failed to register VCP callbacks (err %d)", err);
		return err;
	}

	LOG_INF("VCP controller initialized");
	return 0;
}

/* Reset VCP controller state */
void vcp_controller_reset_state(void)
{
	vcp_discovered = false;
	vol_ctlr = NULL;
}

/* VCP demonstration function */
void vcp_controller_demo(void)
{
	int err;
	static uint8_t demo_state = 0;
	static uint8_t abs_volume = 50;

	if (!vol_ctlr || !vcp_discovered) {
		return;
	}

	switch (demo_state) {
	case 0:
		LOG_INF("Requesting volume up...");
		err = bt_vcp_vol_ctlr_vol_up(vol_ctlr);
		if (err) {
			LOG_ERR("Failed to volume up (err %d)", err);
		}
		break;
	case 1:
		LOG_INF("Requesting volume down...");
		err = bt_vcp_vol_ctlr_vol_down(vol_ctlr);
		if (err) {
			LOG_ERR("Failed to volume down (err %d)", err);
		}
		break;
	case 2:
		LOG_INF("Requesting volume up and unmute...");
		err = bt_vcp_vol_ctlr_vol_up_unmute(vol_ctlr);
		if (err) {
			LOG_ERR("Failed to volume up and unmute (err %d)", err);
		}
		break;
	case 3:
		LOG_INF("Requesting volume down and unmute...");
		err = bt_vcp_vol_ctlr_vol_down_unmute(vol_ctlr);
		if (err) {
			LOG_ERR("Failed to volume down and unmute (err %d)", err);
		}
		break;
	case 4:
		abs_volume = (abs_volume == 50) ? 80 : 50;
		LOG_INF("Setting absolute volume to %u...", abs_volume);
		err = bt_vcp_vol_ctlr_set_vol(vol_ctlr, abs_volume);
		if (err) {
			LOG_ERR("Failed to set absolute volume (err %d)", err);
		}
		break;
	case 5:
		LOG_INF("Requesting mute...");
		err = bt_vcp_vol_ctlr_mute(vol_ctlr);
		if (err) {
			LOG_ERR("Failed to mute (err %d)", err);
		}
		break;
	case 6:
		LOG_INF("Requesting unmute...");
		err = bt_vcp_vol_ctlr_unmute(vol_ctlr);
		if (err) {
			LOG_ERR("Failed to unmute (err %d)", err);
		}
		break;
	}

	demo_state = (demo_state + 1) % 7; // Cycle through 7 operations
}
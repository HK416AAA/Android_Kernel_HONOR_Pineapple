/*
 * direct_charger_stage.h
 *
 * direct charger stage
 *
 * Copyright (c) 2021-2021 Honor Device Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DIRECT_CHARGER_STAGE_H_
#define _DIRECT_CHARGER_STAGE_H_

#define DC_STAGE_MAX_NUM 100

enum direct_charge_stage_type {
	DC_STAGE_BEGIN = 0,
	DC_STAGE_DEFAULT = DC_STAGE_BEGIN,
	DC_STAGE_READY,

	DC_STAGE_PRE_CHECK_BEGIN = 0x100,
	DC_STAGE_PRE_CHECK_BTB_CHECK,
	DC_STAGE_PRE_CHECK_SECURITY,
	DC_STAGE_PRE_CHECK_ADAPTER_PROTOCOL,
	DC_STAGE_PRE_CHECK_ADAPTER_POWER_CURVE,
	DC_STAGE_PRE_CHECK_CABLE_DETECT,
	DC_STAGE_PRE_CHECK_SEND_UI_EVENT,
	DC_STAGE_PRE_CHECK_BATTERY_VOLTAGE,
	DC_STAGE_PRE_CHECK_UPDATE_ADAPTER_INFO,
	DC_STAGE_PRE_CHECK_BATTERY_TEMP,
	DC_STAGE_PRE_CHECK_ADAPTER_ANTIFAKE,
	DC_STAGE_PRE_CHECK_END = 0x1FF,

	DC_STAGE_INIT_CHG_BEGIN = 0x200,
	DC_STAGE_INIT_CHG_ADAPTER_INIT,
	DC_STAGE_INIT_CHG_IC_DEIVCE_INIT,
	DC_STAGE_INIT_CHG_INIT_IC_MODE,
	DC_STAGE_INIT_CHG_PATH_MNG_SWITCH,
	DC_STAGE_INIT_CHG_END = 0x2FF,

	DC_STAGE_POST_CHECK_BEGIN = 0x300,
	DC_STAGE_POST_CHECK_ADAPTER_ACCUARCY,
	DC_STAGE_POST_CHECK_ADAPTER_LEAKAGE_CURRENT,
	DC_STAGE_POST_CHECK_OPEN_CHARGING,
	DC_STAGE_POST_CHECK_FULL_PATH_RESISTANCE,
	DC_STAGE_POST_CHECK_END = 0x3FF,

	DC_STAGE_CHECK_SUCCESS = 0x400,

	DC_STAGE_CHARGING = 0x600,

	DC_STAGE_STOP_CHG_BEGIN = 0x700,
	DC_STAGE_STOP_CHG_IC_DEIVCE_STOP,
	DC_STAGE_STOP_CHG_RESET_ADAPTER_STATE,
	DC_STAGE_STOP_CHG_IC_DEIVCE_EXIT,
	DC_STAGE_STOP_CHG_PATH_MNG_SWITCH,
	DC_STAGE_STOP_CHG_END = 0x7FF,

	DC_STAGE_EXIT_CHG_BEGIN = 0x800,
	DC_STAGE_EXIT_CHG_PATH_MNG_SWITCH,
	DC_STAGE_EXIT_CHG_END = 0x8FF,

	DC_STAGE_CHARGE_DONE = 0x900,

	DC_STAGE_END,
};

struct dc_stage_desc {
	enum direct_charge_stage_type type;
	const char *desc;
};

struct dc_stage_ops {
	int stage;
	int (*stage_ops)(void);
};

struct dc_stage_info {
	int index;
	struct dc_stage_ops *dc_stage_ops_tbl[DC_STAGE_MAX_NUM];
};

int direct_charger_stage_ops_register(struct dc_stage_ops *ops_tbl,
				      int tbl_size);
int direct_charger_stage_pre_check(void);
int direct_charger_stage_init_charging(void);
int direct_charger_stage_post_check(void);
int direct_charger_stage_stop_charging(void);
int direct_charger_stage_exit_charging(void);

unsigned int direct_charge_get_stage_status(void);
const char *direct_charge_get_stage_status_string(unsigned int stage);
void direct_charge_set_stage_status(unsigned int stage);

#endif /* _DIRECT_CHARGER_STAGE_H_ */

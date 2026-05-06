/*
 * direct_charger_data.h
 *
 * direct charger data
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

#ifndef _DIRECT_CHARGER_DATA_H_
#define _DIRECT_CHARGER_DATA_H_

#include <hwpower/direct_charger/direct_charger_adapter.h>
#include <hwpower/direct_charger/direct_charger_battery.h>
#include <hwpower/direct_charger/direct_charger_cable.h>
#include <hwpower/direct_charger/direct_charger_ic_device.h>
#include <hwpower/direct_charger/direct_charger_path_manager.h>
#include <hwpower/direct_charger/direct_charger_uevent.h>
#include <securec.h>

#define DC_DATA_NAME_LEN 64

#define dc_data_memzero_para(data, type) \
	(data), sizeof(*(data)), 0, offsetof(type, static_info)
#define dc_static_data_memzero_para(data)                     \
	&(data)->static_info, sizeof((data)->static_info), 0, \
		sizeof((data)->static_info)

enum dc_data_type {
	DC_DATA_TYPE_BEGIN = 0,
	DC_DATA_TYPE_ADAPTER = DC_DATA_TYPE_BEGIN,
	DC_DATA_TYPE_BATTERY,
	DC_DATA_TYPE_CABLE,
	DC_DATA_TYPE_CHARGE_IC,
	DC_DATA_TYPE_PATH_MANAGER,
	DC_DATA_TYPE_UEVENT,
	DC_DATA_TYPE_SECURITY,
	DC_DATA_TYPE_TIME,
	DC_DATA_TYPE_THERMAL,
	DC_DATA_TYPE_ALL,
	DC_DATA_TYPE_END,
};

struct dc_data_info {
	char *name;
	enum dc_data_type type;

	int (*init_status)(void *);
	int (*update_status)(void *);
	int (*check_err_stop)(void *);
	int (*check_done)(void *);
	int (*get_max_pwr_cur)(void *);
	int (*get_max_chg_cur)(void *);
	int (*get_min_chg_cur)(void *);
	int (*reset_status_in_stop)(void *);
	int (*reset_status_in_exit)(void *);

	int (*select_cur_stage)(void *data, int *cur_stage);

	void *data;
};

struct dc_data_desc {
	enum dc_data_type type;
	const char *desc;
};

int direct_charger_data_register(struct dc_data_info *info);
void *direct_charger_get_data(enum dc_data_type type);
int direct_charger_init_data(enum dc_data_type type, void *data);
int direct_charger_init_status(enum dc_data_type type);
int direct_charger_update_status(enum dc_data_type type);
int direct_charger_check_err_status(enum dc_data_type type);
int direct_charger_check_done_status(enum dc_data_type type);
int direct_charger_get_max_pwr_cur(enum dc_data_type type);
int direct_charger_get_max_chg_cur(enum dc_data_type type);
int direct_charger_get_min_chg_cur(enum dc_data_type type);
int direct_charger_reset_status_in_stop(enum dc_data_type type);
int direct_charger_reset_status_in_exit(enum dc_data_type type);
int direct_charger_select_cur_stage(enum dc_data_type type, int *cur_stage);

struct dc_adapter_info *direct_charge_get_adapter(void);
struct dc_battery_info *direct_charge_get_battery(void);
struct dc_cable_info *direct_charge_get_cable(void);
struct dc_ic_device_info *direct_charge_get_ic_device(void);
struct dc_path_manager_info *direct_charge_get_path_manager(void);
struct dc_uevent_info *direct_charge_get_uevent(void);
struct dc_security_info *direct_charge_get_security(void);
struct dc_time_info *direct_charge_get_time(void);
struct dc_thermal_info *direct_charge_get_thermal(void);

int direct_charger_data_min(int a, int b);
int direct_charger_data_max(int a, int b);

#endif /* _DIRECT_CHARGER_DATA_H_ */

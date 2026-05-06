/*
 * direct_charge_path_manager.h
 *
 * direct charge path_manager module
 *
 * Copyright (c) 2020-2020 Honor Device Co., Ltd.
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

#ifndef _DIRECT_CHARGER_PATH_MNG_H_
#define _DIRECT_CHARGER_PATH_MNG_H_

/*
 * define charging path with direct charge
 */
enum dc_path_charging_type {
	PATH_MNG_BEGIN,
	PATH_MNG_NORMAL = PATH_MNG_BEGIN,
	PATH_MNG_LVC,
	PATH_MNG_SC,
	PATH_MNG_END,
};

struct dc_path_manager_config {
	u32 need_wired_sw_off;
	u32 scp_work_on_charger;
	int sc_to_bulk_delay_hiz;
	int delay_hiz_time;
	int sc_to_bulk_delay_eoc;
	int delay_eoc_time;
};

struct dc_path_manager_info {
	struct dc_path_manager_config config;
};

struct dc_path_manager_module {
	int working_mode;
	atomic_t wakelock_cnt;
	struct wakeup_source path_mng_lock;
	struct delayed_work hiz_mode_work;
	struct delayed_work eoc_mode_work;
};

int dc_path_manager_module_init(void);
int dc_path_manager_module_exit(void);

#endif /* _DIRECT_CHARGER_PATH_MNG_H_ */

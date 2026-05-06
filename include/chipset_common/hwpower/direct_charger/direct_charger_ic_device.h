/*
 * direct_charge_ic_device.h
 *
 *  direct charge ic_device module
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

#ifndef _DIRECT_CHARGER_IC_DEVICE_H_
#define _DIRECT_CHARGER_IC_DEVICE_H_

#include <hwpower/direct_charger/multi_ic_check.h>

struct dc_ic_device_config {
	int curr_offset;
	int single_ic_ibat_th;
	int multi_ic_ibat_th;
	int max_tls;

	int multi2single_ibat_delta;

	struct multi_ic_check_para multi_ic_check_info;
	struct multi_ic_check_mode_para multi_ic_mode_para;
	struct dc_comp_para comp_para;
};

struct dc_ic_device_static_info {
	u32 multi_ic_error_cnt;
};

struct dc_ic_device_info {
	/* data: reset in dc sotp */
	int ls_vbus;
	int ls_ibus;
	int tls;

	bool force_single_path_flag;
	u32 multi_ic_start_time;
	int ibat_th;
	int max_cur_th;

	/* static data: reset in plug out*/
	struct dc_ic_device_static_info static_info;

	/* config */
	struct dc_ic_device_config config;
};

int dc_ic_device_module_init(void);
int dc_ic_device_module_exit(void);

#endif /* _DIRECT_CHARGER_IC_DEVICE_H_ */

/*
 * direct_charge_cable.h
 *
 * cable detect for direct charge module
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

#ifndef _DIRECT_CHARGER_CABLE_H_
#define _DIRECT_CHARGER_CABLE_H_

/*
 * define resistance threshold with maximum current
 * support up to 5 parameters list on dts
 */
#define DC_RESIST_LEVEL 5
#define DC_ERROR_RESISTANCE (-99999)
#define DC_MAX_RESISTANCE 10000

enum direct_charge_resist_info {
	DC_RESIST_MIN = 0,
	DC_RESIST_MAX,
	DC_RESIST_CUR_MAX,
	DC_RESIST_TOTAL,
};

struct direct_charge_resist_para {
	int resist_min;
	int resist_max;
	int resist_cur_max;
};

enum direct_charge_cable_type {
	DC_UNKNOWN_CABLE,
	DC_NONSTD_CABLE,
	DC_STD_CABLE,
};

struct dc_cable_config {
	struct direct_charge_resist_para nonstd_resist_para[DC_RESIST_LEVEL];
	struct direct_charge_resist_para std_resist_para[DC_RESIST_LEVEL];
	struct direct_charge_resist_para second_resist_para[DC_RESIST_LEVEL];
	struct direct_charge_resist_para ctc_resist_para[DC_RESIST_LEVEL];
	int std_cable_full_path_res_max;
	int nonstd_cable_full_path_res_max;
	int ctc_cable_full_path_res_max;
	int max_current_for_nonstd_cable;
	int max_current_for_ctc_cable;
	int second_resist_check_en;
	int second_path_res_report_th;
	u32 cc_cable_detect_enable;
};

struct dc_cable_static_info {
	int cable_detect_ok;
};

struct dc_cable_info {
	/* data: reset in dc stop */
	int cc_cable_detect_ok;
	int cable_type;
	int orig_cable_type;

	int full_path_resistance;
	int full_path_res_thld;
	int second_path_res_report_th;
	int second_path_resistance;
	bool second_resist_check_ok;

	int max_cur_th;

	/* static data: reset in plug out*/
	struct dc_cable_static_info static_info;

	/* config */
	struct dc_cable_config config;
};

int dc_cable_module_init(void);
int dc_cable_module_exit(void);

#endif /* _DIRECT_CHARGER_CABLE_H_ */

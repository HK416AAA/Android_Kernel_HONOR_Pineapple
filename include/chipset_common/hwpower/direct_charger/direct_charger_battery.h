/*
 * direct_charge_battery.h
 *
 * battery module for direct charge module
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

#ifndef _DIRECT_CHARGER_BATTERY_H_
#define _DIRECT_CHARGER_BATTERY_H_

#include <hwpower/direct_charge/direct_charge_comp.h>

/*
 * define temprature threshold with maximum current
 * support up to 5 parameters list on dts
 */
#define DC_TEMP_LEVEL 5
#define DC_TEMP_BATTERY2S_LEVEL 14

/* for charge temp hysteresis */
#define DC_LOW_TEMP_MAX 10
#define DC_HIGH_TEMP_MAX 45

enum direct_charge_temp_info {
	DC_TEMP_MIN = 0,
	DC_TEMP_MAX,
	DC_TEMP_CUR_MAX,
	DC_TEMP_TOTAL,
};

enum direct_charge_temp_battery2s_info {
	DC_TEMP_BATTERY2S_MIN = 0,
	DC_TEMP_BATTERY2S_MAX,
	DC_TEMP_BATTERY2S_VOLT,
	DC_TEMP_BATTERY2S_CUR_MAX,
	DC_TEMP_BATTERY2S_TOTAL,
};

struct direct_charge_temp_para {
	int temp_min;
	int temp_max;
	int temp_cur_max;
};

struct direct_charge_temp_volt_para {
	int temp_min;
	int temp_max;
	int volt;
	int temp_cur_max;
};

#define DC_TEMP_CV_LEVEL 8

enum direct_charge_temp_cv_info {
	DC_TEMP_CV_MIN,
	DC_TEMP_CV_MAX,
	DC_TEMP_CV_CUR,
	DC_TEMP_CV_TOTOL,
};

struct direct_charge_temp_cv_para {
	int temp_min;
	int temp_max;
	int cv_curr;
};

struct dc_battery_static_info {
	bool bat_temp_err_flag;
	u32 low_temp_hysteresis;
	u32 high_temp_hysteresis;
};

struct dc_battery_config {
	struct direct_charge_temp_para temp_para[DC_TEMP_LEVEL];
	struct direct_charge_temp_cv_para temp_cv_para[DC_TEMP_CV_LEVEL];
	struct direct_charge_temp_volt_para
		main_temp_battery2s_para[DC_TEMP_BATTERY2S_LEVEL];
	struct direct_charge_temp_volt_para
		aux_temp_battery2s_para[DC_TEMP_BATTERY2S_LEVEL];
	int iin_thermal_default;
	u32 use_higher_vbat;
	u32 dc_use_fg_get_battery_current;
	int ibat_comb;

	u32 orig_low_temp_hysteresis;
	u32 orig_high_temp_hysteresis;
	int max_dc_bat_vol;
	int min_dc_bat_vol;
	int compensate_r;

	// multi battery
	u32 battery2s;
	int compensate_r_main;
	int compensate_r_aux;
};

struct dc_battery_info {
	/* data: reset in dc stop */
	int bat_temp_before_charging;

	int max_batt_temp_cur;
	int min_batt_temp_cur;

	int vbat;
	int ibat;
	int tbat;

	int compensate_v;

	// multi battery
	int main_ibat;
	int aux_ibat;
	int main_batt_temp_cur;
	int aux_batt_temp_cur;
	int dynamic_current_th;

	/* static data: reset in plug out*/
	struct dc_battery_static_info static_info;

	/* config */
	struct dc_battery_config config;
};

int dc_battery_module_init(void);
int dc_battery_module_exit(void);

#endif /* _DIRECT_CHARGER_BATTERY_H_ */

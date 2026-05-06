/*
 * direct_charge_time.h
 *
 * direct charge time module
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

#ifndef _DIRECT_CHARGER_TIME_H_
#define _DIRECT_CHARGER_TIME_H_

/*
 * define dc time threshold with maximum current
 * support up to 5 parameters list on dts
 */
#define DC_TIME_PARA_LEVEL 5
#define DC_CC_STAGE_TIME_PARA_LEVEL 8

enum direct_charge_time_info {
	DC_TIME_INFO_TIME_TH,
	DC_TIME_INFO_IBAT_MAX,
	DC_TIME_INFO_MAX,
};

struct direct_charge_time_para {
	int time_th;
	int ibat_max;
};

struct dc_time_config {
	struct direct_charge_time_para time_para[DC_TIME_PARA_LEVEL];
	u32 cc_stage_max_charge_time[DC_CC_STAGE_TIME_PARA_LEVEL];
};

struct dc_time_static_info {};

struct dc_time_info {
	/* data: reset in dc stop */
	int time_para_parse_ok;
	u32 delta_time;
	unsigned long cc_stage_start_jiffies[DC_CC_STAGE_TIME_PARA_LEVEL];
	unsigned long cc_stage_timeout[DC_CC_STAGE_TIME_PARA_LEVEL];
	int max_cur_th;

	/* static data: reset in plug out*/
	struct dc_time_static_info static_info;

	/* config */
	struct dc_time_config config;
};

int dc_time_module_init(void);
int dc_time_module_exit(void);

#endif /* _DIRECT_CHARGER_TIME_H_ */

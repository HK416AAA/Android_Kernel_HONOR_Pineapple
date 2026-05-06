/*
 * direct_charge_thermal.h
 *
 * direct charge thermal module
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

#ifndef _DIRECT_CHARGER_THERMAL_H_
#define _DIRECT_CHARGER_THERMAL_H_

struct dc_thermal_config {};

struct dc_thermal_static_info {};

struct dc_thermal_info {
	/* data: reset in dc stop */
	int max_cur_th;

	/* static data: reset in plug out*/
	struct dc_thermal_static_info static_info;

	/* config */
	struct dc_thermal_config config;
};

int dc_thermal_module_init(void);
int dc_thermal_module_exit(void);

#endif /* _DIRECT_CHARGER_THERMAL_H_ */

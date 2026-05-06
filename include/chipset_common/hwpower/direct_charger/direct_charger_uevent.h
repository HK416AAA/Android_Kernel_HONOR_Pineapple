/*
 * direct_charge_uevent.h
 *
 * uevent handle for direct charge
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

#ifndef _DIRECT_CHARGER_UEVENT_H_
#define _DIRECT_CHARGER_UEVENT_H_

struct dc_uevent_config {
	u32 is_show_ico_first;
	u32 is_send_cable_type;
	int ui_max_pwr;
	int product_max_pwr;
	int super_ico_current;
};

struct dc_uevent_static_info {
	int quick_charge_flag;
	int super_charge_flag;
};

struct dc_uevent_info {
	/* data: reset in dc stop */
	int max_pwr;
	int max_cur;

	/* static data: reset in plug out*/
	struct dc_uevent_static_info static_info;

	/* config */
	struct dc_uevent_config config;
};

int dc_uevent_module_init(void);
int dc_uevent_module_exit(void);

#endif /* _DIRECT_CHARGER_UEVENT_H_ */

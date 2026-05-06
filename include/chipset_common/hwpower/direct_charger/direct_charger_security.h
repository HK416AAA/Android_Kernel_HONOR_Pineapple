/*
 * direct_charge_security.h
 *
 * direct_charge_security
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

#ifndef _DIRECT_CHARGER_SECURITY_H_
#define _DIRECT_CHARGER_SECURITY_H_

struct dc_security_config {
	int cc_unsafe_sc_enable;
	int sbu_unsafe_sc_enable;

	int cam_flash_stop;
};

struct dc_security_static_info {};

struct dc_security_info {
	/* data: reset in dc stop */
	bool cc_safe;
	bool sbu_safe;
	int max_cur_th;

	/* static data: reset in plug out*/
	struct dc_security_static_info static_info;

	/* config */
	struct dc_security_config config;
};

int dc_security_module_init(void);
int dc_security_module_exit(void);

#endif /* _DIRECT_CHARGER_SECURITY_H_ */

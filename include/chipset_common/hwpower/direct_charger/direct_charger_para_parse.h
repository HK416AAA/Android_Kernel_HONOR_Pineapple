/*
 * direct_charge_para_parse.h
 *
 * parameter parse interface for direct charge
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

#ifndef _DIRECT_CHARGER_PARA_PARSE_H__
#define _DIRECT_CHARGER_PARA_PARSE_H__

int dc_para_parse_dts(struct device_node *np, void *p);
int dc_para_parse_dts_adapter(struct device_node *np, void *p);
int dc_para_parse_dts_battery(struct device_node *np, void *p);
int dc_para_parse_dts_cable(struct device_node *np, void *p);
int dc_para_parse_dts_ic_device(struct device_node *np, void *p);
int dc_para_parse_dts_path_manager(struct device_node *np, void *p);
int dc_para_parse_dts_uevent(struct device_node *np, void *p);
int dc_para_parse_dts_security(struct device_node *np, void *p);
int dc_para_parse_dts_time(struct device_node *np, void *p);

#endif /* _DIRECT_CHARGER_PARA_PARSE_H_ */

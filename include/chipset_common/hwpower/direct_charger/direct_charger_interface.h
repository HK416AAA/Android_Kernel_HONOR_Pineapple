/*
 * direct_charger_interface.h
 *
 * direct_charger_interface
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

#ifndef _DIRECT_CHARGER_INTERFACE_H_
#define _DIRECT_CHARGER_INTERFACE_H_

#include <hwpower/direct_charger/direct_charger_stage.h>

struct dc_if_ops {
	void (*reserved)(void);
};

struct dc_interface_info {
	struct device *dev;
	struct direct_charge_device *dc_info;
	struct dc_if_ops *ops;
};

/* define protocol power supply oprator for direct charge */
struct direct_charge_pps_ops {
	int (*power_supply_enable)(int);
};

/* define cable detect oprator for direct charge */
struct direct_charge_cd_ops {
	int (*cable_detect)(void);
};

int direct_charge_if_ops_register(struct dc_if_ops *ops);

int direct_charge_get_dc_stage(void);
bool direct_charge_in_charging_stage(void);
bool direct_charge_in_charge_done_stage(void);
void hsc_get_fault_notifier(struct atomic_notifier_head **notifier_list);
void sc_get_fault_notifier(struct atomic_notifier_head **notifier_list);
void lvc_get_fault_notifier(struct atomic_notifier_head **notifier_list);
void direct_charge_set_disable_flags(int val, int type);
bool direct_charge_is_battery2s(void);

int direct_charge_pps_ops_register(struct direct_charge_pps_ops *ops);
int direct_charge_cd_ops_register(struct direct_charge_cd_ops *ops);
struct direct_charge_pps_ops *direct_charge_get_pps_ops(void);
struct direct_charge_cd_ops *direct_charge_get_cd_ops(void);
int dc_get_dc_cur_ibat_th_high(void);

#endif /* _DIRECT_CHARGER_INTERFACE_H_ */

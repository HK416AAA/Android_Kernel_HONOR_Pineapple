/*
 * direct_charger_interface.h
 *
 * direct_charger_internal_interface
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

#ifndef _DIRECT_CHARGER_INTERNAL_INTERFACE_H_
#define _DIRECT_CHARGER_INTERNAL_INTERFACE_H_

/* adatper interface */
int dc_adapter_get_type(void);
int dc_adapter_get_voltage(int *voltage);
int dc_adapter_get_current(int *curr);
int dc_adapter_get_current_set(int *curr);
int dc_adapter_get_temp(int *temp);
int dc_adapter_set_voltage(int voltage);
int dc_adapter_set_current(int curr);
int dc_adapter_set_output_enable(int enable);
bool dc_adapter_is_ignore_cable_check(void);
void dc_adapter_set_antifake_result(unsigned int data);
int dc_adapter_get_antifake_data(char *buf, unsigned int len);

/* battery interface */
int dc_battery_get_voltage(int *vbat);
int dc_battery_get_current(int *ibat);
int dc_battery_get_temp(int *tbat);
int dc_battery_get_num(void);

/* charge ic interface */
int dc_ic_device_get_vbus(int *vbus);
int dc_ic_device_get_ibus(int *ibus);
int dc_ic_device_get_temp(int *temp);
int dc_ic_device_get_btb_vbat_with_comp(int mode);
int dc_ic_device_get_close_status(void);
void dc_ic_device_set_ic_error(void);

/* path manager interface */
int dc_path_open_wired_channel(void);
int dc_path_close_wired_channel(void);
void dc_path_open_aux_wired_channel(void);
void dc_path_close_aux_wired_channel(void);

/* uevent interface */
void dc_uevent_send_normal_charging_uevent(void);
void dc_uevent_send_quick_charging_uevent(void);
void dc_uevent_send_super_charging_uevent(void);
void dc_uevent_send_icon(void);

#endif /* _DIRECT_CHARGER_INTERNAL_INTERFACE_H_ */

/*
 * direct_charger_check.h
 *
 * direct charger check driver
 *
 * Copyright (c) Honor Device Co., Ltd. 2020-2021. All rights reserved.
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

#ifndef _DIRECT_CHARGER_CHECK_H_
#define _DIRECT_CHARGER_CHECK_H_

bool direct_charge_get_can_enter_status(void);
bool direct_charge_check_enable_status(void);
bool direct_charge_in_mode_check(void);
void direct_charge_set_can_enter_status(bool status);
int direct_charge_select_mode(void);
void direct_charge_enter_mode(void);
bool direct_charge_check_charge_done(void);
bool direct_charge_check_port_fault(void);
bool direct_charge_check_sbu_fault(void);

#endif /* _DIRECT_CHARGER_CHECK_H_ */

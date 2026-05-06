/*
 * direct_charger_mode.h
 *
 * direct charger mode
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

#ifndef _DIRECT_CHARGER_MODE_H_
#define _DIRECT_CHARGER_MODE_H_

enum direct_charge_mode_type {
	DC_MODE_TYPE_HSC,
	DC_MODE_TYPE_SC,
	DC_MODE_TYPE_LVC,
	DC_MODE_TYPE_END
};

enum direct_charge_mode_error_type {
	DC_MODE_ERROR_ADAPTER_ANTI_FAKE,
	DC_MODE_ERROR_END
};

struct direct_charge_mode_ops {
	int (*select)(int adp_mode);
	int (*set_enable)(int enable, int type);
	int (*enable_buck_charger)(int enable);
	void (*fault_callback)(void *dev_data);
};

void dc_mode_register_ops(struct direct_charge_mode_ops *ops, int dc_mode_type);
int dc_mode_select(int adp_mode);
int dc_mode_set_enable(int enable, int type);
int dc_mode_enable_buck_charger(int working_mode, int enable);
int dc_mode_fault_callback(void *dev_data);

#endif /* _DIRECT_CHARGER_MODE_H_ */

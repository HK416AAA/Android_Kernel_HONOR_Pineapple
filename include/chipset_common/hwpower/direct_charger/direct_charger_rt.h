/*
 * direct_charger_rt.h
 *
 * rt test for direct charge
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

#ifndef _DIRECT_CHARGER_RT_H_
#define _DIRECT_CHARGER_RT_H_

#include <hwpower/direct_charger/direct_charger.h>

/* define rt test time parameters */
enum direct_charge_rt_test_info {
	DC_RT_CURR_TH,
	DC_RT_TEST_TIME,
	DC_RT_TEST_INFO_TOTAL,
};

enum direct_charge_rt_test_mode {
	DC_NORMAL_MODE,
	DC_CHAN1_MODE, /* channel1: single main sc mode */
	DC_CHAN2_MODE, /* channel2: single aux sc mode */
	DC_MODE_TOTAL,
};

struct direct_charge_rt_test_para {
	u32 rt_curr_th;
	u32 rt_test_time;
};

struct direct_charge_rt_test_result {
	bool rt_test_result;
};

void direct_charge_set_rt_test_result(int ibat,
				      struct direct_charge_device *di);
int dc_rt_module_init(void);

#endif /* _DIRECT_CHARGER_RT_H_ */

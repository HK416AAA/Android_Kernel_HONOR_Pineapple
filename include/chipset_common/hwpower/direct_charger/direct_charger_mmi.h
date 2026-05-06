/*
 * direct_charge_mmi.h
 *
 * mmi test for direct charge
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

#ifndef _DIRECT_CHARGER_MMI_H_
#define _DIRECT_CHARGER_MMI_H_

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/types.h>

#define DC_MMI_DFLT_EX_PROT 1
#define DC_MMI_DFLT_TIMEOUT 10
#define DC_MMI_TO_BUFF 5

#define DC_MMI_PROT_DC_SUCC BIT(0)

/*
 * define error code with direct charge
 * DC is simplified identifier with direct-charge
 */
enum dc_mmi_error_code {
	DC_SUCC,
	DC_ERROR_INIT,
	DC_ERROR_BTB_CHECK, /* mmi test individual err */
	DC_ERROR_START_CHARGE,
	DC_ERROR_CUR_TEST,
	DC_ERROR_OPEN_MULTI_CHARGE_PATH,
	DC_ERROR_ADAPTER_START = 10, /* adapter err */
	DC_ERROR_USB_PORT_LEAKAGE_CURRENT,
	DC_ERROR_ADAPTER_VOLTAGE_ACCURACY,
	DC_ERROR_FULL_REISISTANCE,
	DC_ERROR_ADAPTER_ANTI_FAKE,
	DC_ERROR_SCP_DETECT_OTHER,
	DC_ERROR_SCP_DETECT_FAIL,
	DC_ERROR_ADAP_0X80_FAIL,
	DC_ERROR_ADAPTER_DISCONNECT,
	DC_ERROR_ADAPTER_MAX,
	DC_ERROR_IC_START = 30, /* charge ic err */
	DC_ERROR_OPEN_CHARGE_PATH,
	DC_ERROR_OPEN_MULTI_SC,
	DC_ERROR_IC_MAX_TEMP,
	DC_ERROR_GET_IC_INFO,
	DC_ERROR_IC_CLOSE,
	DC_ERROR_IC_KICK_WATCHDOG,
	DC_ERROR_IC_MAX,
	DC_ERROR_SOFTWARE_START = 50, /* software bug err */
	DC_ERROR_LOCAL_NOT_SUPPORT,
	DC_ERROR_SC_DONE,
	DC_ERROR_SOFTWARE_MAX,
};

enum dc_mmi_sysfs_type {
	DC_MMI_SYSFS_BEGIN = 0,
	DC_MMI_SYSFS_TIMEOUT = DC_MMI_SYSFS_BEGIN,
	DC_MMI_SYSFS_LVC_RESULT,
	DC_MMI_SYSFS_SC_RESULT,
	DC_MMI_SYSFS_HSC_RESULT,
	DC_MMI_SYSFS_TEST_STATUS,
	DC_MMI_SYSFS_END,
};

enum dc_mmi_test_para {
	DC_MMI_PARA_TIMEOUT,
	DC_MMI_PARA_EXPT_PORT,
	DC_MMI_PARA_MULTI_SC_TEST,
	DC_MMI_PARA_IBAT_TH,
	DC_MMI_PARA_IBAT_TIMEOUT,
	DC_MMI_PARA_MAX,
};

struct dc_mmi_para {
	int timeout;
	u32 expt_prot;
	int multi_sc_test;
	int ibat_th;
	int ibat_timeout;
};

struct dc_mmi_data {
	u32 prot_state;
	bool ibat_first_match;
	int lvc_succ_flag;
	int sc_succ_flag;
	int hsc_succ_flag;
	bool dc_mmi_test_flag;
	struct notifier_block dc_mmi_nb;
	struct delayed_work timeout_work;
	struct dc_mmi_para dts_para;
};

void dc_mmi_set_succ_flag(int mode, int value);
int dc_mmi_get_succ_flag(int mode);
void dc_mmi_set_test_flag(bool flag);
bool dc_mmi_get_test_flag(void);
int dc_mmi_module_init(void);

#endif /* _DIRECT_CHARGER_MMI_H_ */

/*
 * soc_control.h
 *
 * battery capacity(soc: state of charge) control driver
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

#ifndef _SOC_CONTROL_H_
#define _SOC_CONTROL_H_

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

#define SOC_CTRL_START_TIME 30000 /* 30s */
#define SOC_CTRL_LOOP_TIME 10000 /* 10s */
#define SOC_CTRL_CHG_ENABLE 1
#define SOC_CTRL_CHG_DISABLE 0
#define SOC_CTRL_IIN_LIMIT 100 /* 100ma */
#define SOC_CTRL_IIN_UNLIMIT 0
#define SOC_CTRL_RW_BUF_SIZE 32
#define SOC_CTRL_HZ_ENABLE 1
#define SOC_CTRL_HZ_DISABLE 0

enum soc_ctrl_op_user {
	SOC_CTRL_OP_USER_BEGIN = 0,
	SOC_CTRL_OP_USER_DEFAULT = SOC_CTRL_OP_USER_BEGIN, /* for default */
	SOC_CTRL_OP_USER_RC, /* for rc file */
	SOC_CTRL_OP_USER_HIDL, /* for hidl interface */
	SOC_CTRL_OP_USER_BMS_SOC, /* for bms_soc daemon */
	SOC_CTRL_OP_USER_SHELL, /* for shell command */
	SOC_CTRL_OP_USER_CUST, /* for cust */
	SOC_CTRL_OP_USER_DEMO, /* for demo */
	SOC_CTRL_OP_USER_BSOH, /* for bsoh */
	SOC_CTRL_OP_USER_END,
};

enum soc_ctrl_sysfs_type {
	SOC_CTRL_SYSFS_BEGIN = 0,
	SOC_CTRL_SYSFS_CONTROL = SOC_CTRL_SYSFS_BEGIN,
	SOC_CTRL_SYSFS_STRATEGY,
	SOC_CTRL_SYSFS_END,
};

enum soc_ctrl_event_type {
	SOC_CTRL_DEFAULT_EVENT,
	SOC_CTRL_START_EVENT, /* start event when usb insert */
	SOC_CTRL_STOP_EVENT, /* stop event when usb not insert */
};

enum soc_ctrl_work_mode {
	WORK_IN_DEFAULT_MODE,
	WORK_IN_ENABLE_CHG_MODE,
	WORK_IN_DISABLE_CHG_MODE,
	WORK_IN_BALANCE_MODE,
};

enum soc_ctrl_strategy_type {
	STRATEGY_TYPE_BEGIN,
	STRATEGY_TYPE_CLASS_A = STRATEGY_TYPE_BEGIN,
	STRATEGY_TYPE_CLASS_B,
	STRATEGY_TYPE_CLASS_C,
	STRATEGY_TYPE_END,
};

struct adsp_soc_ctrl_ops {
	int (*set_soc_ctrl_strategy)(int);
	int (*get_soc_ctrl_strategy)(int *);
	int (*set_soc_ctrl_control)(int, int, int);
	int (*get_soc_ctrl_control)(int *, int *, int *);
};

struct soc_ctrl_dev {
	struct device *dev;
	struct notifier_block nb;
	struct delayed_work work;
	struct adsp_soc_ctrl_ops *ops;
	int work_mode;
	int event;
	int enable;
	int min_soc;
	int max_soc;
	int strategy;
};

#if IS_ENABLED(CONFIG_HONOR_SOC_CONTROL)
int soc_control_register_ops(struct adsp_soc_ctrl_ops *ops);
#else
static inline int soc_control_register_ops(struct adsp_soc_ctrl_ops *ops)
{
	return -1;
}
#endif /* CONFIG_HONOR_SOC_CONTROL */

#endif /* _SOC_CONTROL_H_ */

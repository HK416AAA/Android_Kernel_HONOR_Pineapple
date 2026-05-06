/*
 * uscp.h
 *
 * usb port short circuit protect monitor driver
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

#ifndef _USCP_H_
#define _USCP_H_

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/notifier.h>
#include <linux/pm_wakeup.h>

#define DEFAULT_TUSB_THRESHOLD 40
#define USB_TEMP_UPPER_LIMIT 100
#define USB_TEMP_LOWER_LIMIT (-30)
#define GPIO_SWITCH_OPEN 1
#define GPIO_SWITCH_CLOSE 0
#define MONITOR_INTERVAL_SLOW 10000 /* 10000 ms */
#define MONITOR_INTERVAL_FAST 300 /* 300 ms */
#define CHECK_COUNT_DEFAULT_VAL (-1)
#define CHECK_COUNT_INIT_VAL 1100
#define CHECK_COUNT_START_VAL 0
#define CHECK_COUNT_END_VAL 1001
#define CHECK_COUNT_STEP_VAL 1
#define USCP_HIZ_ENABLE 1
#define USCP_HIZ_DISABLE 0
#define USCP_INPUT_CUR_LIMIT 100 /* MA */
#define USCP_INPUT_CUR_NORMAL 3000 /* MA */
#define DT_FOR_START_WORK 2000 /* 2s */
#define USB_PORT_NAME_MAX_LEN 20

struct uscp_temp_info {
	int bat_temp;
	int usb_temp;
	int diff_usb_bat;
};

struct uscp_device_info {
	struct notifier_block event_nb;
	struct delayed_work start_work;
	struct delayed_work check_work;
	int gpio_uscp;
	int uscp_threshold_tusb;
	int open_mosfet_temp;
	int open_hiz_temp;
	int close_mosfet_temp;
	int interval_switch_temp;
	int dmd_hiz_enable;
	int check_interval;
	int check_count;
	struct wakeup_source protect_wakelock;
	bool protect_enable;
	bool protect_mode;
	bool rt_protect_mode; /* real time */
	bool hiz_mode;
	bool dmd_notify_enable;
	bool dmd_notify_enable_hiz;
	char usb_port_name[USB_PORT_NAME_MAX_LEN];
};

#if IS_ENABLED(CONFIG_HONOR_USB_SHORT_CIRCUIT_PROTECT)
bool uscp_is_in_protect_mode(void);
bool uscp_is_in_rt_protect_mode(void);
bool uscp_is_in_hiz_mode(void);
int uscp_get_gpio_status(void);
void uscp_set_gpio_status(const char *name, int value);
#else
static inline bool uscp_is_in_protect_mode(void)
{
	return false;
}

static inline bool uscp_is_in_rt_protect_mode(void)
{
	return false;
}

static inline bool uscp_is_in_hiz_mode(void)
{
	return false;
}

static inline int uscp_get_gpio_status(void)
{
	return 0;
}

static inline void uscp_set_gpio_status(const char *name, int value)
{
	return;
}
#endif /* CONFIG_HONOR_USB_SHORT_CIRCUIT_PROTECT */

#endif /* _USCP_H_ */

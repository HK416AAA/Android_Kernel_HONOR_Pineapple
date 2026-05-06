// SPDX-License-Identifier: GPL-2.0
/*
 * This program is used for create hook in dubai.
 *
 * HONOR POWER
 */

#ifndef DUBAI_VENDOR_HOOK_H
#define DUBAI_VENDOR_HOOK_H

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/capability.h>

typedef void(*dubai_vh_log_irq_wakeup)(const char *);
typedef void(*dubai_vh_set_rtc_timer)(const char *, int, ktime_t);
typedef void(*dubai_vh_update_suspend)(const char *);
typedef void(*dubai_vh_log_uevent)(const char *, unsigned int);
typedef void(*dubai_vh_log_kworker)(unsigned long, unsigned long long);
typedef void(*dubai_vh_log_binder_stats)(int, uid_t, int, uid_t, int);
typedef void(*dubai_vh_reset_clk_list)(void);
typedef void(*dubai_vh_log_enabled_clk)(const char *);
typedef void(*dubai_vh_log_ufs_clock)(bool);

void dubai_log_irq_wakeup_hook(dubai_vh_log_irq_wakeup dubai_wakeup_hook);
void dubai_set_rtc_timer_hook(dubai_vh_set_rtc_timer dubai_alarm_timer_hook);
void dubai_update_suspend_abort_reason_hook(dubai_vh_update_suspend dubai_abort_reason_hook);
void dubai_log_uevent_hook(dubai_vh_log_uevent dubai_kobject_hook);
void dubai_log_kworker_hook(dubai_vh_log_kworker dubai_workqueue_hook);
void dubai_log_binder_stats_hook(dubai_vh_log_binder_stats dubai_binder_hook);
void dubai_reset_clk_list_hook(dubai_vh_reset_clk_list dubai_reset_clk_hook);
void dubai_log_enabled_clock_hook(dubai_vh_log_enabled_clk dubai_clk_hook);
void dubai_log_ufshcd_setup_clocks_hook(dubai_vh_log_ufs_clock dubai_ufs_hook);

extern dubai_vh_log_irq_wakeup kernel_wakeup_hook;
extern dubai_vh_set_rtc_timer kernel_alarm_timer_hook;
extern dubai_vh_update_suspend kernel_abort_reason_hook;
extern dubai_vh_log_uevent kernel_kobject_hook;
extern dubai_vh_log_kworker kernel_workqueue_hook;
extern dubai_vh_log_binder_stats kernel_binder_hook;
extern dubai_vh_reset_clk_list kernel_reset_clk_hook;
extern dubai_vh_log_enabled_clk kernel_clk_hook;
extern dubai_vh_log_ufs_clock kernel_ufs_hook;

#endif // DUBAI_VENDOR_HOOK_H

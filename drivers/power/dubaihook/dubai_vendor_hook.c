/*
 * This program is used for create hook in dubai.
 *
 * HONOR POWER
 */

#include <trace/hooks/dubai_vendor_hook.h>

dubai_vh_log_irq_wakeup kernel_wakeup_hook = NULL;
dubai_vh_set_rtc_timer kernel_alarm_timer_hook = NULL;
dubai_vh_update_suspend kernel_abort_reason_hook = NULL;
dubai_vh_log_uevent kernel_kobject_hook = NULL;
dubai_vh_log_kworker kernel_workqueue_hook = NULL;
dubai_vh_log_binder_stats kernel_binder_hook = NULL;
dubai_vh_log_ufs_clock kernel_ufs_hook = NULL;

void dubai_log_irq_wakeup_hook(dubai_vh_log_irq_wakeup dubai_wakeup_hook)
{
	kernel_wakeup_hook = dubai_wakeup_hook;
}
EXPORT_SYMBOL_GPL(dubai_log_irq_wakeup_hook);

void dubai_set_rtc_timer_hook(dubai_vh_set_rtc_timer dubai_alarm_timer_hook)
{
	kernel_alarm_timer_hook = dubai_alarm_timer_hook;
}
EXPORT_SYMBOL_GPL(dubai_set_rtc_timer_hook);

void dubai_update_suspend_abort_reason_hook(dubai_vh_update_suspend dubai_abort_reason_hook)
{
	kernel_abort_reason_hook = dubai_abort_reason_hook;
}
EXPORT_SYMBOL_GPL(dubai_update_suspend_abort_reason_hook);

void dubai_log_uevent_hook(dubai_vh_log_uevent dubai_kobject_hook)
{
	kernel_kobject_hook = dubai_kobject_hook;
}
EXPORT_SYMBOL_GPL(dubai_log_uevent_hook);

void dubai_log_kworker_hook(dubai_vh_log_kworker dubai_workqueue_hook)
{
	kernel_workqueue_hook = dubai_workqueue_hook;
}
EXPORT_SYMBOL_GPL(dubai_log_kworker_hook);

void dubai_log_binder_stats_hook(dubai_vh_log_binder_stats dubai_binder_hook)
{
	kernel_binder_hook = dubai_binder_hook;
}
EXPORT_SYMBOL_GPL(dubai_log_binder_stats_hook);

void dubai_log_ufshcd_setup_clocks_hook(dubai_vh_log_ufs_clock dubai_ufs_hook)
{
	kernel_ufs_hook = dubai_ufs_hook;
}
EXPORT_SYMBOL_GPL(dubai_log_ufshcd_setup_clocks_hook);
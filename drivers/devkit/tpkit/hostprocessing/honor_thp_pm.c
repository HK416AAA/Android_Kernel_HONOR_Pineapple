#include "honor_thp.h"
#include "honor_thp_pm.h"

#if (defined CONFIG_HONOR_THP_PM)
#include <linux/delay.h>
#include "honor_thp_mt_wrapper.h"
#include <linux/pinctrl/pinctrl.h>
#include <linux/gpio.h>

#if ((IS_ENABLED(CONFIG_HONOR_THP_QCOM)) && (!defined CONFIG_LCD_KIT_DRIVER))
#include "dsi_panel.h"
#endif
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
#include <hwmanufac/runmode_type/runmode_type.h>
#endif

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
struct tpkit_thp_ops {
	int (*power_notify)(enum lcd_kit_ts_pm_type pm_type, int timeout,
			    int panel_index);
};
#endif

static void thp_set_multi_pm_status(struct thp_core_data *cd,
				    enum lcd_kit_ts_pm_type pm_type,
				    int panel_index);
static int thp_power_control_notify(struct thp_core_data *cd,
				    enum lcd_kit_ts_pm_type pm_type,
				    int timeout);
static int pm_type_switch(struct thp_core_data *cd,
			  enum lcd_kit_ts_pm_type pm_type);
static int thp_sub_power_control_notify(enum lcd_kit_ts_pm_type pm_type,
					int timeout);

extern void thp_clear_frame_buffer(struct thp_core_data *cd);
extern struct thp_core_data *thp_get_sub_core_data(void);

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
extern int thp_pen_single_power_control_notify(enum lcd_kit_ts_pm_type pm_type,
					       int timeout);
extern int ts_kit_ops_register(struct ts_kit_ops *ops);
extern int ts_kit_ops_unregister(struct ts_kit_ops *ops);
extern struct lcd_kit_ops *lcd_kit_get_ops(void);
extern struct tpkit_thp_ops *tpkit_get_ops(void);
#endif

#if (!defined(CONFIG_LCD_KIT_DRIVER)) && (!(IS_ENABLED(CONFIG_HONOR_THP_QCOM)))
extern int g_tskit_pt_station_flag;
#endif

#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM))
int g_tskit_pt_station_flag;
#endif

void aod_notify_main_panel_power(struct thp_core_data *cd, int status)
{
	static unsigned int last_status = INVALID_VALUE;
	struct thp_cmd_node cmd;

	thp_log_info(cd, "%s: status is %u\n", __func__, status);
	if (status == last_status) {
		thp_log_info(cd, "%s: repeat event, dont handle!!\n", __func__);
		return;
	}
	memset(&cmd, 0, sizeof(cmd));
	switch (status) {
	case AOD_NOTIFY_TP_SUSPEND:
		cmd.command = TS_SUSPEND;
		break;
	case AOD_NOTIFY_TP_RESUME:
		cmd.command = TS_RESUME;
		break;
	case NOTIFY_TP_STANDBY_IN:
		cmd.command = TS_STANDBY_IN;
		break;
	case NOTIFY_TP_STANDBY_OUT:
		cmd.command = TS_STANDBY_OUT;
		break;
	default:
		cmd.command = TS_INVAILD_CMD;
		break;
	}
	last_status = status;
	if (thp_put_one_cmd(cd, &cmd, NO_SYNC_TIMEOUT))
		thp_log_err(cd, "%s: put cmd error\n", __func__);
}

void aod_notify_sub_panel_power(struct thp_core_data *cd, int status)
{
	struct thp_cmd_node cmd;

	cmd.command = AOD_NOTIFY_SUBPANEL_POWER_CTRL;
	cmd.cmd_param.pub_params.power_ctrl = status;
	if (thp_put_one_cmd(cd, &cmd, NO_SYNC_TIMEOUT))
		thp_log_err(cd, "%s: put cmd error\n", __func__);
}

/*
 * This function is called for recording the system time when tp
 * receive the suspend cmd from lcd driver for proximity feature.
 */
static void thp_prox_suspend_record_time(struct thp_core_data *cd)
{
	get_timestamp(&cd->tp_suspend_record_tv);
	thp_log_info(
		cd,
		"[Proximity_feature] TP early suspend at %ld secs %ld microseconds\n",
		cd->tp_suspend_record_tv.tv_sec,
		cd->tp_suspend_record_tv.tv_usec);
}

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
/* ts_kit_ops */
static int thp_single_power_control_notify(enum lcd_kit_ts_pm_type pm_type,
					   int timeout)
{
	struct thp_core_data *cd = thp_get_core_data();

	if (cd->use_dual_spi_for_pen == 1)
		thp_pen_single_power_control_notify(pm_type, timeout);
	if (cd->use_aod_power_ctrl_notify && cd->aod_notify_tp_power_status) {
		thp_log_info(cd, "%s: use AOD notify, return\n", __func__);
		return 0;
	}
	return thp_power_control_notify(cd, pm_type, timeout);
}

static int thp_multi_power_control_notify(enum lcd_kit_ts_pm_type pm_type,
					  int timeout, int panel_index)
{
	struct thp_core_data *cd = thp_get_core_data();
	int ret;
	struct thp_cmd_node cmd;

	if (!cd) {
		thp_log_err(cd, "%s: tp is not registered\n", __func__);
		return -ENODEV;
	}
	thp_log_info(cd, "%s: called, index:%d, pm_type:%u\n", __func__,
		     panel_index, pm_type);

	if (cd->use_aod_power_ctrl_notify) {
		if ((!panel_index && cd->aod_notify_tp_power_status) ||
		    (panel_index && cd->aod_notify_subpanel_power_status)) {
			thp_log_info(cd, "%s: use AOD notify, return\n",
				     __func__);
			return 0;
		}
	}
	if ((pm_type == TS_AFTER_RESUME) || (pm_type == TS_BEFORE_SUSPEND) ||
	    (pm_type == TS_SUSPEND_DEVICE)) {
		thp_log_info(cd, "%s: ignore %d", __func__, pm_type);
		return 0;
	}
	memset(&cmd, 0, sizeof(cmd));
	if (panel_index == MAIN_TOUCH_PANEL) {
		if (pm_type == TS_RESUME_DEVICE)
			cmd.command = THP_MUTIL_RESUME_THREAD;
		else
			cmd.command = THP_MUTIL_SUSPEND_THREAD;
	} else if (panel_index == SUB_TOUCH_PANEL) {
		if (pm_type == TS_RESUME_DEVICE)
			cmd.command = TSKIT_MUTIL_RESUME_THREAD;
		else
			cmd.command = TSKIT_MUTIL_SUSPEND_THREAD;
	} else {
		thp_log_err(cd, "%s: invalid index: %d\n", __func__,
			    panel_index);
		return -EINVAL;
	}
	cmd.cmd_param.pub_params.dev = cd->thp_dev;
	ret = thp_put_one_cmd(cd, &cmd, 0);
	if (ret)
		thp_log_err(cd, "%s: put cmd error :%d\n", __func__, ret);
	return ret;
}

static bool thp_get_prox_switch_status(void)
{
	struct thp_core_data *cd = thp_get_core_data();

	if (!cd) {
		thp_log_err(cd, "%s: thp_core_data is not inited\n", __func__);
		return 0;
	}
	if (cd->proximity_support == PROX_SUPPORT) {
		thp_log_info(
			cd,
			"[Proximity_feature] %s: need_work_in_suspend = %d!\n",
			__func__, cd->need_work_in_suspend);
		return cd->need_work_in_suspend;
	}
	thp_log_info(cd,
		     "[Proximity_feature] %s :Not support proximity feature!\n",
		     __func__);
	return 0;
}

static int thp_get_status_by_type(int type, int *status)
{
	struct thp_core_data *cd = thp_get_core_data();

	if (status == NULL) {
		thp_log_err(cd, "status is null\n");
		return -EINVAL;
	}

	/*
	 * To avoid easy_wakeup_info.sleep_mode being changed
	 * during suspend, we assign cd->sleep_mode to
	 * easy_wakeup_info.sleep_mode in suspend.
	 * For TDDI, tp suspend must before lcd power off to
	 * make sure of easy_wakeup_info.sleep_mode be assigned.
	 */
	if ((type == TS_GESTURE_FUNCTION) &&
	    (cd->easy_wakeup_info.sleep_mode == TS_GESTURE_MODE) &&
	    cd->support_gesture_mode &&
	    cd->lcd_gesture_mode_support) { /* TDDI need this */
		*status = GESTURE_MODE_OPEN;
		return NO_ERR;
	}

	return -EINVAL;
}

static bool thp_get_afe_download_status(struct timeval *record_tv)
{
	struct thp_core_data *cd = thp_get_core_data();

	if ((record_tv == NULL) || (cd == NULL)) {
		thp_log_err(cd, "%s: record_tv or cd is null\n", __func__);
		return false;
	}

	if (cd->lcd_need_get_afe_status == 0) {
		thp_log_err(cd, "%s: lcd no need get afe status\n", __func__);
		return false;
	}

	if (cd->afe_download_status) {
		record_tv->tv_sec = cd->afe_status_record_tv.tv_sec;
		record_tv->tv_usec = cd->afe_status_record_tv.tv_usec;
	}
	return cd->afe_download_status;
}
#endif
/*
 * Here to count the period of time which is from suspend to a new
 * disable status, if the period is less than 1000ms then call lcdkit
 * power off, otherwise bypass the additional power off.
 */
static bool thp_prox_timeout_check(struct thp_core_data *cd)
{
	long delta_time;
	struct timeval tv;

	memset(&tv, 0, sizeof(tv));
	get_timestamp(&tv);
	thp_log_info(
		cd,
		"[Proximity_feature] check time at %ld seconds %ld microseconds\n",
		tv.tv_sec, tv.tv_usec);
	/* multiply 1000000 to transfor second to us */
	delta_time = (tv.tv_sec - cd->tp_suspend_record_tv.tv_sec) * 1000000 +
		     tv.tv_usec - cd->tp_suspend_record_tv.tv_usec;
	/* divide 1000 to transfor sec to us to ms */
	delta_time /= 1000;
	thp_log_info(cd, "[Proximity_feature] delta_time = %ld ms\n",
		     delta_time);
	if (delta_time >= AFTER_SUSPEND_TIME)
		return true;
	else
		return false;
}

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
/*
 * After lcd driver complete the additional power down,calling this function
 * do something for matching current power status. Such as updating the
 * proximity switch status, sending the screen_off cmd to tp daemon, pulling
 * down the gpios and so on.
 */
static void thp_prox_add_suspend(struct thp_core_data *cd, bool enable)
{
	thp_log_info(cd, "[Proximity_feature] %s call enter\n", __func__);
	/* update the control status based on proximity switch */
	cd->need_work_in_suspend = enable;
	/* notify daemon to do screen off cmd */
	thp_set_status(cd, THP_STATUS_POWER, THP_SUSPEND);
	/* notify daemon to do proximity off cmd */
	thp_set_status(cd, THP_STATUS_AFE_PROXIMITY, THP_PROX_EXIT);
	/* pull down the gpio pin */

	gpio_set_value(cd->thp_dev->gpios->rst_gpio, 0);
#if (!(IS_ENABLED(CONFIG_HONOR_THP_MTK)))
	gpio_set_value(cd->thp_dev->gpios->cs_gpio, 0);
#else
	pinctrl_select_state(cd->pctrl, cd->mtk_pinctrl.cs_low);
#endif
	/* disable the irq */
	if (cd->open_count)
		thp_set_irq_status(cd, THP_IRQ_DISABLE);
	cd->work_status = SUSPEND_DONE;
	/* clean the fingers */
	thp_clean_fingers(cd);
	thp_log_info(cd, "[Proximity_feature] %s call exit\n", __func__);
}
#endif

/*
 * In this function, differentiating lcdkit1.0 and lcdkit 3.0's interfaces,
 * and increasing some judgements, only meet these conditions then
 * the additional power off will be called.
 */
static void thp_prox_add_poweroff(struct thp_core_data *cd, bool enable)
{
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	struct lcd_kit_ops *tp_ops = lcd_kit_get_ops();
#endif

	if ((enable == false) && (cd->onetime_poweroff_done == false)) {
		cd->onetime_poweroff_done = true;
		if (thp_prox_timeout_check(cd)) {
			thp_log_info(
				cd,
				"[Proximity_feature] timeout, bypass poweroff\n");
			return;
		}
#ifdef CONFIG_LCDKIT_DRIVER
		if (!lcdkit_proximity_poweroff())
			thp_prox_add_suspend(cd, enable);
#endif

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
		if (tp_ops && tp_ops->proximity_power_off) {
			if (!tp_ops->proximity_power_off())
				thp_prox_add_suspend(cd, enable);
		} else {
			thp_log_err(cd, "[Proximity_feature] point is null\n");
		}
#endif
	}
}

/*
 * This function receive the proximity switch status and save it for
 *  controlling power operation or cmds transferring to daemon
 * (proximity_on or scrren_off).
 */
int thp_set_prox_switch_status(struct thp_core_data *cd, bool enable)
{
#if ((defined CONFIG_INPUTHUB_20) || (defined CONFIG_HONOR_PS_SENSOR) || \
     (defined CONFIG_HONOR_SENSORS_2_0))
	int ret;
	int report_value[PROX_VALUE_LEN] = {0};
#endif

	if (!cd) {
		thp_log_err(cd, "%s: thp_core_data is not inited\n", __func__);
		return 0;
	}
	if (!atomic_read(&cd->register_flag))
		return 0;

	if (cd->proximity_support == PROX_SUPPORT) {
#if ((defined CONFIG_INPUTHUB_20) || (defined CONFIG_HONOR_PS_SENSOR) || \
     (defined CONFIG_HONOR_SENSORS_2_0))
		report_value[0] = AWAY_EVENT_VALUE;
		ret = thp_prox_event_report(report_value, PROX_EVENT_LEN);
		if (ret < 0)
			thp_log_info(cd, "%s: report event fail\n", __func__);
		thp_log_info(
			cd,
			"[Proximity_feature] %s: default report [far] event!\n",
			__func__);
#endif
		thp_set_status(cd, THP_STATUS_TOUCH_APPROACH, enable);
		if (cd->early_suspended == false) {
			cd->thp_prox_enable = enable;
			thp_log_info(
				cd,
				"[Proximity_feature] %s: 1.Update thp_prox_enable to %d in screen on!\n",
				__func__, cd->thp_prox_enable);
		} else {
			cd->prox_cache_enable = enable;
			thp_log_info(
				cd,
				"[Proximity_feature] %s: 2.Update prox_cache_enable to %d in screen off!\n",
				__func__, cd->prox_cache_enable);
			/*
			 * When disable proximity after suspend,
			 * call power off once.
			 */
			thp_prox_add_poweroff(cd, enable);
		}
		return 0;
	}
	thp_log_info(
		cd, "[Proximity_feature] %s : Not support proximity feature!\n",
		__func__);
	return 0;
}

#ifndef CONFIG_LCD_KIT_DRIVER
static int thp_lcdkit_notifier_callback(struct thp_core_data *cd,
					struct notifier_block *self,
					unsigned long event, void *data)
{
	unsigned long pm_type = event;

	thp_log_debug(cd, "%s: called by lcdkit, pm_type=%lu\n", __func__,
		      pm_type);

	switch (pm_type) {
	case LCDKIT_TS_EARLY_SUSPEND:
		/*
		 * to avoid easy_wakeup_info.sleep_mode changed during suspend,
		 * assign cd->sleep_mode to easy_wakeup_info.sleep_mode once
		 */
		cd->easy_wakeup_info.sleep_mode = cd->sleep_mode;
		if (cd->proximity_support != PROX_SUPPORT) {
			thp_log_info(cd, "%s: early suspend\n", __func__);
			thp_set_status(cd, THP_STATUS_POWER, THP_SUSPEND);
		} else {
			thp_prox_suspend_record_time(cd);
			thp_log_info(cd,
				     "%s:early suspend!thp_prox_enable=%d\n",
				     __func__, cd->thp_prox_enable);
			cd->need_work_in_suspend = cd->thp_prox_enable;
			cd->prox_cache_enable = cd->thp_prox_enable;
			cd->early_suspended = true;
			if (cd->need_work_in_suspend)
				thp_set_status(cd, THP_STATUS_AFE_PROXIMITY,
					       THP_PROX_ENTER);
			else
				thp_set_status(cd, THP_STATUS_POWER,
					       THP_SUSPEND);
		}
		break;

	case LCDKIT_TS_SUSPEND_DEVICE:
		thp_log_info(cd, "%s: suspend\n", __func__);
		thp_clean_fingers(cd);
		break;

	case LCDKIT_TS_BEFORE_SUSPEND:
		thp_log_info(cd, "%s: before suspend\n", __func__);
		thp_suspend(cd);
		break;

	case LCDKIT_TS_RESUME_DEVICE:
		thp_log_info(cd, "%s: resume\n", __func__);
		thp_resume(cd);
		break;

	case LCDKIT_TS_AFTER_RESUME:
		if (cd->proximity_support != PROX_SUPPORT) {
			thp_log_info(cd, "%s: after resume\n", __func__);
			thp_set_status(cd, THP_STATUS_POWER, THP_RESUME);
		} else {
			thp_log_info(
				cd, "%s:after resume!need_work_in_suspend=%d\n",
				__func__, cd->need_work_in_suspend);
			if (cd->need_work_in_suspend)
				thp_set_status(cd, THP_STATUS_AFE_PROXIMITY,
					       THP_PROX_EXIT);
			else
				thp_set_status(cd, THP_STATUS_POWER,
					       THP_RESUME);
		}
		break;
	default:
		break;
	}

	return 0;
}
#endif

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
struct ts_kit_ops thp_ops = {
	.ts_power_notify = thp_single_power_control_notify,
	.get_tp_proxmity = thp_get_prox_switch_status,
	.get_tp_status_by_type = thp_get_status_by_type,
	.ts_multi_power_notify = thp_multi_power_control_notify,
	.get_afe_status = thp_get_afe_download_status,
};
#endif

int thp_lcd_notify_register(struct thp_core_data *cd)
{
	int rc = 0;

#ifndef CONFIG_LCD_KIT_DRIVER
	cd->lcd_notify.notifier_call = thp_lcdkit_notifier_callback;
#endif

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	if ((cd->multi_panel_index == MAIN_TOUCH_PANEL) ||
	    (cd->multi_panel_index == SINGLE_TOUCH_PANEL)) {
		rc = ts_kit_ops_register(&thp_ops);
		if (rc)
			thp_log_info(cd, "%s:ts_kit_ops_register fail\n",
				     __func__);
	}
#endif

	return rc;
}

int thp_lcd_notify_unregister(struct thp_core_data *cd)
{
	int rc = 0;

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	rc = ts_kit_ops_unregister(&thp_ops);
	if (rc)
		thp_log_info(cd, "%s:ts_kit_ops_unregister fail\n", __func__);
#endif
	return rc;
}

bool need_work_in_suspend_switch(struct thp_core_data *cd)
{
	bool result = false;
	unsigned int double_tap_status;
	unsigned int ap_gesture_status;

	if (!cd->use_ap_gesture)
		return (((cd->easy_wakeup_info.sleep_mode == TS_GESTURE_MODE &&
			  cd->support_gesture_mode) ||
			 cd->stylus_gesture_status) &&
			!cd->support_shb_thp);

	double_tap_status =
		(cd->easy_wakeup_info.sleep_mode == TS_GESTURE_MODE) &&
		cd->support_gesture_mode;
	ap_gesture_status = thp_get_status(cd, THP_STATUS_UDFP) ||
			    (cd->aod_touch_status && !cd->talk_mode_flag) ||
			    cd->standby_enable || cd->fullaod_tp_need_work;
	result = (double_tap_status || ap_gesture_status ||
		  cd->stylus_gesture_status) &&
		 !cd->support_shb_thp;
	result = result && (!cd->force_power_off);
	result = result && (!cd->easy_wakeup_info.fold_force_power_off);
	return result;
}

static void thp_after_resume_work_fn(struct thp_core_data *cd)
{
	if (cd->delay_work_for_pm) {
		if (cd->thp_dev->ops->after_resume)
			cd->thp_dev->ops->after_resume(cd->thp_dev);
		thp_set_status(cd, THP_STATUS_POWER, THP_RESUME);
	}
	cd->suspend_resume_waitq_flag = WAITQ_WAKEUP;
	wake_up_interruptible(&(cd->suspend_resume_waitq));
	thp_log_info(cd, "%s: after resume end\n", __func__);
}

static void thp_after_resume_work_main_fn(struct work_struct *work)
{
	struct thp_core_data *cd = thp_get_core_data();

	if (!cd) {
		thp_log_err(cd, "%s: tp is not registered\n", __func__);
		return;
	}
	thp_after_resume_work_fn(cd);
}
DECLARE_WORK(thp_after_resume_work_main, thp_after_resume_work_main_fn);

static void thp_after_resume_work_sub_fn(struct work_struct *work)
{
	struct thp_core_data *cd_sub = thp_get_sub_core_data();

	if (!cd_sub) {
		thp_log_err(cd_sub, "%s: sub tp is not registered\n", __func__);
		return;
	}
	thp_after_resume_work_fn(cd_sub);
}
DECLARE_WORK(thp_after_resume_work_sub, thp_after_resume_work_sub_fn);

static int thp_suspend(struct thp_core_data *cd)
{
	if (cd->suspended) {
		thp_log_info(cd, "%s: already suspended, return\n", __func__);
		cd->pre_suspended = false;
		return 0;
	}
	cd->afe_download_status = false;
	cd->invalid_irq_num = 0; /* clear invalid irq count */
	if (cd->proximity_support == PROX_SUPPORT) {
		if (cd->need_work_in_suspend) {
			thp_log_info(
				cd,
				"[Proximity_feature] %s: Enter prximity mode, no need suspend!\n",
				__func__);
			mutex_lock(&cd->suspend_flag_mutex);
			cd->suspended = true;
			mutex_unlock(&cd->suspend_flag_mutex);
			cd->pre_suspended = false;
			return 0;
		}
	}
	if (need_work_in_suspend_switch(cd)) {
		if ((cd->use_dual_spi_for_pen == 1) &&
		    (cd->thp_dev->ops->clear_interrupt))
			cd->thp_dev->ops->clear_interrupt(cd->thp_dev);
		cd->thp_dev->ops->suspend(cd->thp_dev);
		mutex_lock(&cd->suspend_flag_mutex);
		cd->suspended = true;
		mutex_unlock(&cd->suspend_flag_mutex);
		cd->pre_suspended = false;
		cd->work_status = SUSPEND_DONE;
	} else {
		thp_pinctrl_select_lowpower_state(cd);
		cd->thp_dev->ops->suspend(cd->thp_dev);
		mutex_lock(&cd->suspend_flag_mutex);
		cd->suspended = true;
		mutex_unlock(&cd->suspend_flag_mutex);
		cd->pre_suspended = false;
		cd->work_status = POWER_OFF_DONE;
	}
	return 0;
}

static int thp_resume(struct thp_core_data *cd)
{
	if (!cd->suspended) {
		thp_log_info(cd, "%s: already resumed, return\n", __func__);
		return 0;
	}

	thp_set_irq_wake_status(cd, THP_IRQ_WAKE_DISABLE);

	cd->work_status = BEFORE_RESUME;
	if (cd->support_irq_storm_protect) {
		if (cd->irq_storm_flag) {
			thp_set_irq_status(cd, THP_IRQ_ENABLE);
			cd->irq_storm_flag = false;
		}
	}
	cd->thp_dev->ops->resume(cd->thp_dev);

	/* clear rawdata frame buffer list */
	mutex_lock(&cd->mutex_frame);
	thp_clear_frame_buffer(cd);
	mutex_unlock(&cd->mutex_frame);
	thp_pinctrl_select_normal_state(cd);

	if (cd->proximity_support == PROX_SUPPORT) {
		cd->thp_prox_enable = cd->prox_cache_enable;
		cd->onetime_poweroff_done = false;
		cd->early_suspended = false;
		thp_log_info(
			cd,
			"[Proximity_feature] %s: update thp_prox_enable to %d!\n",
			__func__, cd->thp_prox_enable);
	}
	cd->suspended = false;
	cd->work_status = RESUME_DONE;
	cd->force_power_off = false;
	cd->standby_mode = false;
	cd->fullaod_tp_need_work = false;
	cd->in_standby_state = 0;
	return 0;
}

int thp_screen_switch(int screen_status)
{
	struct thp_cmd_node cmd;
	int ret = 0;
	struct thp_core_data *cd = thp_get_core_data();

	memset(&cmd, 0, sizeof(cmd));
	if (screen_status == SCREEN_FOLDED) {
		if (cd->fold_status != TS_SCREEN_FOLD) {
			cmd.command = TS_SCREEN_FOLD;
			cd->fold_status = TS_SCREEN_FOLD;
		} else {
			thp_log_err(cd, "%s: screen has already folded\n",
				    __func__);
			return ret;
		}
	} else if (screen_status == SCREEN_UNFOLD) {
		if (cd->fold_status != TS_SCREEN_UNFOLD) {
			cmd.command = TS_SCREEN_UNFOLD;
			cd->fold_status = TS_SCREEN_UNFOLD;
		} else {
			thp_log_err(cd, "%s: screen has already unfolded\n",
				    __func__);
			return ret;
		}

	} else {
		cmd.command = TS_INVAILD_CMD;
	}

	cmd.cmd_param.pub_params.dev = cd->thp_dev;
	ret = thp_put_one_cmd(cd, &cmd, 0);
	if (ret)
		thp_log_err(cd, "%s: put cmd error :%d\n", __func__, ret);

	return ret;
}

#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
static void thp_suspend_thread(struct thp_core_data *cd)
{
	if (cd->is_fold_device && cd->suspended) {
		thp_log_info(cd, "%s: now is already suspend, just return\n",
			     __func__);
		return;
	}
	pm_type_switch(cd, TS_EARLY_SUSPEND);
	msleep(cd->suspend_delayms_early_to_before);
	pm_type_switch(cd, TS_BEFORE_SUSPEND);
	msleep(cd->suspend_delayms_before_to_later);
	pm_type_switch(cd, TS_SUSPEND_DEVICE);
}

static void thp_resume_thread(struct thp_core_data *cd)
{
	if (!cd->suspended && cd->wait_afe_screen_on_support) {
		thp_log_info(cd, "%s: already resumed, return\n", __func__);
		return;
	}
	pm_type_switch(cd, TS_RESUME_DEVICE);
	msleep(cd->resume_delayms_early_to_later);
	pm_type_switch(cd, TS_AFTER_RESUME);
}

static void aod_notify_subpanel_suspend(struct thp_core_data *cd)
{
	struct thp_core_data *cd_sub = thp_get_sub_core_data();
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	struct tpkit_thp_ops *ops = tpkit_get_ops();
	int err;
#endif
	if (cd->sub_solution == THP_SOLUTION && !cd_sub) {
		thp_log_err(cd, "%s: sub tp is not registered\n", __func__);
		return;
	}

	if (cd->ts_suspended) {
		thp_log_info(cd, "now is already suspend, just return\n");
		return;
	}
	cd->ts_suspended = true;
	thp_set_multi_pm_status(cd, TS_EARLY_SUSPEND, SUB_TOUCH_PANEL);
	if (cd->sub_solution == THP_SOLUTION) {
		thp_suspend_thread(cd_sub);
	} else {
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
		if (ops && ops->power_notify) {
			err = ops->power_notify(TS_EARLY_SUSPEND,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_EARLY_SUSPEND fail\n",
					    __func__);
			err = ops->power_notify(TS_BEFORE_SUSPEND,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_BEFORE_SUSPEND fail\n",
					    __func__);
			err = ops->power_notify(TS_SUSPEND_DEVICE,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_SUSPEND_DEVICE fail\n",
					    __func__);
		}
#endif
	}
}

static void aod_notify_subpanel_resume(struct thp_core_data *cd)
{
	struct thp_core_data *cd_sub = thp_get_sub_core_data();
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	struct tpkit_thp_ops *ops = tpkit_get_ops();
	int err;
#endif
	if (cd->sub_solution == THP_SOLUTION && !cd_sub) {
		thp_log_err(cd, "%s: sub tp is not registered\n", __func__);
		return;
	}
	cd->ts_suspended = false;
	if (cd->sub_solution == THP_SOLUTION) {
		thp_resume_thread(cd_sub);
	} else {
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
		if (ops && ops->power_notify) {
			err = ops->power_notify(TS_RESUME_DEVICE,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_RESUME_DEVICE fail\n",
					    __func__);
			err = ops->power_notify(TS_AFTER_RESUME,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_AFTER_RESUME fail\n",
					    __func__);
		}
#endif
	}

	thp_set_multi_pm_status(cd, TS_RESUME_DEVICE, SUB_TOUCH_PANEL);
}
#endif

static void notify_tp_standby_in(struct thp_core_data *cd)
{
	struct thp_core_data *cd_main = thp_get_core_data();

	if (!cd_main) {
		thp_log_err(cd, "%s: cd_main not registered\n", __func__);
		return;
	}
	if (!cd->suspended) {
		// screen on into standby first suspend
		if (cd->multi_panel_index == SUB_TOUCH_PANEL)
			cd_main->ts_suspended = true;
		thp_suspend_thread(cd);
	}
	if (cd->thp_dev && cd->thp_dev->ops &&
	    cd->thp_dev->ops->standby_mode_exit_lowpower)
		cd->thp_dev->ops->standby_mode_exit_lowpower(cd->thp_dev);
	// send cmd to ic into standby mode
	cd->thp_dev->ops->tui_enable_switch(cd->thp_dev, 1);
	cd->standby_mode = true;
	thp_log_info(cd, "%s: mode = %d\n", __func__, cd->standby_mode);
}
static void notify_tp_standby_out(struct thp_core_data *cd)
{
	if (cd->thp_dev && cd->thp_dev->ops &&
	    cd->thp_dev->ops->tui_enable_switch) {
		cd->thp_dev->ops->tui_enable_switch(cd->thp_dev, 0);
	} else {
		thp_log_err(cd, "%s: have nullptr ,return\n", __func__);
		return;
	}
	cd->thp_dev->ops->suspend(cd->thp_dev);
	cd->standby_mode = false;
	thp_log_info(cd, "%s: mode = %d\n", __func__, cd->standby_mode);
}

static void notify_tp_subpanel_standby_mode(struct thp_core_data *cd, int type)
{
	struct thp_core_data *cd_sub = thp_get_sub_core_data();

	if (!cd_sub) {
		thp_log_err(cd, "%s: sub tp is not registered\n", __func__);
		return;
	}
	if (type)
		notify_tp_standby_in(cd_sub);
	else
		notify_tp_standby_out(cd_sub);
}

/*
 * Add some delay before screenoff to avoid quick resume-suspend TP-firmware
 * does not download success but thp have notified afe screenoff.
 * Use 'wait_event_interruptible_timeout' wait THP_AFE_STATUS_SCREEN_ON
 * notify and set enough timeout make sure of TP-firmware download success.
 * This solution will cause suspend be more slowly.
 */
static void thp_delay_before_screenoff(struct thp_core_data *cd)
{
	int rc;
	u32 suspend_delay_ms;

	if (!cd || !(cd->thp_dev)) {
		thp_log_err(cd, "%s: cd is null\n", __func__);
		return;
	}

	suspend_delay_ms = cd->thp_dev->timing_config.early_suspend_delay_ms;
	if ((atomic_read(&(cd->afe_screen_on_waitq_flag)) == WAITQ_WAKEUP) ||
	    !suspend_delay_ms) {
		thp_log_info(cd, "%s, do not need wait\n", __func__);
		return;
	}

	thp_log_info(cd, "%s:wait afe screen on complete\n", __func__);
	rc = wait_event_interruptible_timeout(
		cd->afe_screen_on_waitq,
		(atomic_read(&(cd->afe_screen_on_waitq_flag)) == WAITQ_WAKEUP),
		msecs_to_jiffies(suspend_delay_ms));
	if (rc)
		return;
	/* if timeout and condition not true, rc is 0 need report DMD */
	atomic_set(&(cd->afe_screen_on_waitq_flag), WAITQ_WAKEUP);
#if IS_ENABLED(CONFIG_DSM)
	thp_dmd_report(cd, DSM_TPHOSTPROCESSING_DEV_GESTURE_EXP2,
		       "%s, screen on %u ms, but fw not ready\n", __func__,
		       suspend_delay_ms);
#endif
	thp_log_info(cd, "%s, screen on %u ms, but fw not ready\n", __func__,
		     suspend_delay_ms);
}

#define SUSPEND_WAIT_TIMEOUT 2000
static void thp_early_suspend(struct thp_core_data *cd)
{
	int rc;

	thp_log_info(cd, "%s: early suspend,%d\n", __func__,
		     cd->suspend_resume_waitq_flag);
	/*
	 * to avoid easy_wakeup_info.sleep_mode being changed during suspend,
	 * assign cd->sleep_mode to easy_wakeup_info.sleep_mode once
	 */
	cd->easy_wakeup_info.sleep_mode = cd->sleep_mode;
	cd->easy_wakeup_info.fold_force_power_off = cd->fold_force_power_off;
	if (cd->multi_panel_index != SINGLE_TOUCH_PANEL)
		cd->easy_wakeup_info.aod_mode = cd->aod_touch_status;
	/*
	 * TDDI need make sure of firmware download complete,
	 * then lcd send 2810 to screen off,
	 * otherwise gesture mode will enter failed.
	 */
	if (cd->wait_afe_screen_on_support)
		thp_delay_before_screenoff(cd);
	if (cd->delay_work_for_pm) {
		if (cd->suspend_resume_waitq_flag != WAITQ_WAKEUP) {
			thp_log_info(cd, "%s:wait resume complete\n", __func__);
			rc = wait_event_interruptible_timeout(
				cd->suspend_resume_waitq,
				(cd->suspend_resume_waitq_flag == WAITQ_WAKEUP),
				SUSPEND_WAIT_TIMEOUT);
			if (!rc)
				thp_log_err(cd,
					    "%s:wait resume complete timeout\n",
					    __func__);
		}
		thp_set_status(cd, THP_STATUS_POWER, THP_SUSPEND);
		cd->suspend_resume_waitq_flag = WAITQ_WAIT;
	} else {
		if (cd->proximity_support != PROX_SUPPORT) {
			thp_log_info(cd, "%s: early suspend\n", __func__);
			if (cd->multi_panel_index == SINGLE_TOUCH_PANEL)
				thp_set_status(cd, THP_STATUS_POWER,
					       THP_SUSPEND);
			/*
			 * if multi screen and in quickly screen switch,
			 * not notify daemon
			 */
			else if (!cd->quickly_screen_on_off)
				thp_set_status(cd, THP_STATUS_POWER,
					       THP_SUSPEND);
			else
				cd->quickly_screen_on_off = false;
		} else {
			thp_prox_suspend_record_time(cd);
			thp_log_info(
				cd, "%s: early suspend! thp_prox_enable = %d\n",
				__func__, cd->thp_prox_enable);
			cd->need_work_in_suspend = cd->thp_prox_enable;
			cd->prox_cache_enable = cd->thp_prox_enable;
			cd->early_suspended = true;
			if (cd->need_work_in_suspend)
				thp_set_status(cd, THP_STATUS_AFE_PROXIMITY,
					       THP_PROX_ENTER);
			else
				thp_set_status(cd, THP_STATUS_POWER,
					       THP_SUSPEND);
		}
	}
	cd->pre_suspended = true;
}

static void thp_after_resume(struct thp_core_data *cd)
{
	if (cd->unfold_restart_ignore_after_resume) {
		thp_log_info(cd, "%s: unfold restart with gesture, ignore\n",
			     __func__);
		cd->unfold_restart_ignore_after_resume = false;
		return;
	}
	if (cd->delay_work_for_pm) {
		thp_log_info(cd, "%s: after resume called\n", __func__);
		if (cd->multi_panel_index == SUB_TOUCH_PANEL)
			schedule_work(&thp_after_resume_work_sub);
		else
			schedule_work(&thp_after_resume_work_main);
	} else {
		if (cd->proximity_support != PROX_SUPPORT) {
			thp_log_info(cd, "%s: after resume\n", __func__);
			if (cd->multi_panel_index == SINGLE_TOUCH_PANEL)
				thp_set_status(cd, THP_STATUS_POWER,
					       THP_RESUME);
			/*
			 * if multi screen and in quickly screen switch,
			 * not notify daemon
			 */
			else if (!cd->quickly_screen_on_off)
				thp_set_status(cd, THP_STATUS_POWER,
					       THP_RESUME);
		} else {
			thp_log_info(
				cd,
				"%s: after resume! need_work_in_suspend = %d\n",
				__func__, cd->need_work_in_suspend);
			if (cd->need_work_in_suspend)
				thp_set_status(cd, THP_STATUS_AFE_PROXIMITY,
					       THP_PROX_EXIT);
			else
				thp_set_status(cd, THP_STATUS_POWER,
					       THP_RESUME);
		}
	}
	if (cd->wait_afe_screen_on_support &&
	    (atomic_read(&(cd->afe_screen_on_waitq_flag)) != WAITQ_WAIT) &&
	    !cd->quickly_screen_on_off)
		atomic_set(&(cd->afe_screen_on_waitq_flag), WAITQ_WAIT);
	if ((cd->send_bt_status_to_fw) && (cd->support_dual_chip_arch) &&
	    (cd->thp_dev->ops->after_resume)) {
		if (cd->thp_dev->ops->after_resume(cd->thp_dev))
			thp_log_err(cd, "call after resume fail\n");
	}
}

static int pm_type_switch(struct thp_core_data *cd,
			  enum lcd_kit_ts_pm_type pm_type)
{
#ifdef CONFIG_HONOR_SHB_THP
	int ret;
	char fp_event_to_udfp[UDFP_EVENT_DATA_LEN] = {0};
#endif
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
	struct thp_udfp_data udfp_data;
#endif
#if defined(CONFIG_TEE_TUI)
	if (cd->send_tui_exit_in_suspend && tui_enable &&
	    (pm_type == TS_EARLY_SUSPEND)) {
		thp_log_info(cd, "In TUI mode, need exit TUI before suspend\n");
		thp_tui_secos_exit();
	}
#endif
	// if tui is enable, wait tui exit
	if (pm_type == TS_EARLY_SUSPEND) {
		if (thp_vm_wait_ic_tui_exited(cd))
			thp_log_err(cd, "%s: wait ic exit tui timeout\n",
				    __func__);
		else
			thp_log_info(cd, "%s: ic exited tui\n", __func__);
	}

	switch (pm_type) {
	case TS_EARLY_SUSPEND:
		if ((cd->multi_panel_index != SINGLE_TOUCH_PANEL) &&
		    cd->screen_switch_flag) {
			cd->screen_switch_flag = false;
			cd->force_power_off = true;
		}
		thp_early_suspend(cd);
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
		if (cd->use_ap_gesture && thp_get_status(cd, THP_STATUS_UDFP)) {
			memset(&udfp_data, 0, sizeof(udfp_data));
			/* udfp_event = 1 is finger up */
			udfp_data.tpud_data.udfp_event = 1;
			thp_log_info(cd, "%s:suspend send fingerup to fp\n",
				     __func__);
			send_event_to_fingerprint_ud(cd, udfp_data);
		}
#endif

		break;

	case TS_SUSPEND_DEVICE:
		thp_log_info(cd, "%s: suspend\n", __func__);
		break;

	case TS_BEFORE_SUSPEND:
		thp_log_info(cd, "%s: before suspend\n", __func__);
		thp_lcd_status_notifier(cd, TS_BEFORE_SUSPEND);
		if (cd->support_daemon_init_protect &&
		    atomic_read(&(cd->fw_update_protect))) {
			thp_log_info(
				cd,
				"%s: suspend when fw update, return directly\n",
				__func__);
			thp_set_status(cd, THP_STATUS_POWER, THP_RESUME);
			atomic_set(&(cd->resend_suspend_after_fw_update), 1);
			return 0;
		}
		thp_suspend(cd);
#ifdef CONFIG_HONOR_SHB_THP
		if (cd->support_shb_thp_app_switch) {
			ret = send_thp_cmd_to_shb(POWER_OFF);
			if (ret)
				thp_log_err(cd,
					    "%s: send_thp_cmd_to_shb fail\n",
					    __func__);
			break;
		}
		if (cd->support_shb_thp) {
			thp_log_info(
				cd, "%s call poweroff_function_status = 0x%x\n",
				__func__, cd->poweroff_function_status);
			/* 0: power off */
			ret = send_thp_driver_status_cmd(
				0, cd->poweroff_function_status,
				ST_CMD_TYPE_SET_SCREEN_STATUS);
			if (ret)
				thp_log_err(cd, "%s: send_thp_status_off fail",
					    __func__);
		}
#endif
		break;

	case TS_RESUME_DEVICE:
		thp_log_info(cd, "%s: resume\n", __func__);
		if (cd->support_daemon_init_protect &&
		    atomic_read(&(cd->fw_update_protect)) &&
		    atomic_read(&(cd->resend_suspend_after_fw_update))) {
			atomic_set(&(cd->resend_suspend_after_fw_update), 0);
			thp_log_info(
				cd,
				"%s: resume when fw update, return directly\n",
				__func__);
			return 0;
		}
		thp_resume(cd);
		thp_lcd_status_notifier(cd, TS_RESUME_DEVICE);
#ifdef CONFIG_HONOR_SHB_THP
		if (cd->support_shb_thp_app_switch) {
			ret = send_thp_cmd_to_shb(POWER_ON);
			if (ret)
				thp_log_err(cd,
					    "%s: send_thp_cmd_to_shb fail\n",
					    __func__);
			break;
		}
		if (cd->support_shb_thp) {
			/* 1: power on */
			ret = send_thp_driver_status_cmd(
				1, POWER_KEY_ON_CTRL,
				ST_CMD_TYPE_SET_SCREEN_STATUS);
			if (ret)
				thp_log_err(cd, "%s: send_thp_status_on fail",
					    __func__);
		}
		if (cd->tsa_event_to_udfp) {
			/* fp_event_to_udfp[UDFP_EVENT_DATA_LEN - 1] = 1 is finger up */
			fp_event_to_udfp[UDFP_EVENT_DATA_LEN - 1] = 1;
			ret = send_tp_ap_event(UDFP_EVENT_DATA_LEN,
					       fp_event_to_udfp,
					       ST_CMD_TYPE_FINGERPRINT_EVENT);
			if (ret < 0)
				thp_log_err(cd,
					    "%s:tsa_event notify fp err %d\n",
					    __func__, ret);
		}
#endif
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
		if (cd->use_ap_gesture && thp_get_status(cd, THP_STATUS_UDFP)) {
			memset(&udfp_data, 0, sizeof(udfp_data));
			/* udfp_event = 1 is finger up */
			udfp_data.tpud_data.udfp_event = 1;
			thp_log_err(cd, "%s:resume send fingerup to fp\n",
				    __func__);
			send_event_to_fingerprint_ud(cd, udfp_data);
		}
#endif
		break;

	case TS_AFTER_RESUME:
		thp_after_resume(cd);
		if (cd->talk_mode_support)
			cd->talk_mode_flag = 0;
		thp_lcd_status_notifier(cd, TS_AFTER_RESUME);
		break;
	case TS_2ND_POWER_OFF:
		if (cd->thp_dev && cd->thp_dev->ops &&
		    cd->thp_dev->ops->second_poweroff)
			cd->thp_dev->ops->second_poweroff();
		break;
	default:
		break;
	}

	return 0;
}

static int thp_proc_screen_switch_cmd(struct thp_core_data *cd,
				      enum lcd_kit_ts_pm_type pm_type,
				      int timeout, int panel_index)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	struct tpkit_thp_ops *ops = tpkit_get_ops();
#endif
	if (!cd) {
		thp_log_err(cd, "%s: tp is not registered\n", __func__);
		return -ENODEV;
	}
	thp_log_info(cd, "%s: called, index:%d, pm_type:%u\n", __func__,
		     panel_index, pm_type);

	switch (panel_index) {
	case MAIN_TOUCH_PANEL:
		ret = thp_power_control_notify(cd, pm_type, timeout);
		break;
	case SUB_TOUCH_PANEL:
		if (cd->multi_panel_index != SINGLE_TOUCH_PANEL) {
			if (pm_type == TS_SUSPEND_DEVICE)
				cd->ts_suspended = true;
			if (pm_type == TS_AFTER_RESUME)
				cd->ts_suspended = false;
		}
		if (cd->sub_solution == THP_SOLUTION) {
			thp_sub_power_control_notify(pm_type, timeout);
		} else {
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
			if (ops && ops->power_notify)
				ret = ops->power_notify(pm_type, timeout,
							SUB_TOUCH_PANEL);
			else
				thp_log_err(
					cd,
					"%s: ops or ops->power_notify is null\n",
					__func__);
#endif
		}
		break;
	default:
		thp_log_err(cd, "%s: invalid index: %d\n", __func__,
			    panel_index);
		return -EINVAL;
	}
	if (ret)
		thp_log_err(cd, "%s:control notify error\n", __func__);
	return ret;
}

#define PM_TYPE_LIST_LEN 5
#define RESUME_TYPE_LEN 2
static int multi_screen_status_switch(struct thp_core_data *cd,
				      u32 current_status)
{
	struct thp_core_data *cd_sub = thp_get_sub_core_data();
	int i;
	bool gesture_enable_status;
	enum lcd_kit_ts_pm_type pm_type_list[PM_TYPE_LIST_LEN] = {
		TS_RESUME_DEVICE, TS_AFTER_RESUME, TS_EARLY_SUSPEND,
		TS_BEFORE_SUSPEND, TS_SUSPEND_DEVICE};

	if (!cd) {
		thp_log_err(cd, "%s: tp is not registered\n", __func__);
		return -ENODEV;
	}
	gesture_enable_status = (cd->sleep_mode || cd->aod_touch_status ||
				 cd->stylus_gesture_status ||
				 cd->standby_enable) ?
					true :
					false;
#if IS_ENABLED(CONFIG_DSM)
	if (cd->restart_power_on) {
		if (current_status == SCREEN_UNFOLD && gesture_enable_status)
			cd->unfold_restart_ignore_after_resume = true;
		cd->restart_power_on = false;
	} else if (current_status == SCREEN_UNFOLD &&
		   cd->work_status != POWER_OFF_DONE) {
		thp_log_err(
			cd,
			"%s: folded state inner_screen of TP not poweroff, dmd code:%d\n",
			__func__, DSM_TP_POWEROFF_ERROR_NO);
		thp_dmd_report(
			cd, DSM_TP_POWEROFF_ERROR_NO,
			"%s, folded state inner_screen of TP not poweroff\n",
			__func__);
	}
#endif

	if (current_status == SCREEN_FOLDED) {
		cd->fold_force_power_off = true;
		if (cd_sub)
			cd_sub->fold_force_power_off = false;
	} else if (current_status == SCREEN_UNFOLD) {
		cd->fold_force_power_off = false;
		if (cd_sub)
			cd_sub->fold_force_power_off = true;
	}
	if (!gesture_enable_status) {
		thp_log_info(cd, "%s: power off mode, no need switch status\n",
			     __func__);
		return 0;
	}
	if (current_status == SCREEN_FOLDED) {
		cd->force_power_off = true;
		thp_proc_screen_switch_cmd(cd, TS_SCREEN_SWITCH,
					   NO_SYNC_TIMEOUT, MAIN_TOUCH_PANEL);
		if (cd->ts_suspended && cd->suspended) {
			cd->quickly_screen_on_off = true;
			if (cd->aod_touch_status == TS_TOUCH_AOD_OPEN ||
			    cd->standby_enable)
				cd->suspend_fold_flag = true;
			for (i = 0; i < PM_TYPE_LIST_LEN; i++) {
				if (pm_type_list[i] == TS_EARLY_SUSPEND)
					msleep(200); /* 200ms wait ic ready */
				thp_proc_screen_switch_cmd(cd, pm_type_list[i],
							   SHORT_SYNC_TIMEOUT,
							   SUB_TOUCH_PANEL);
				pm_type_switch(cd, pm_type_list[i]);
			}
		} else if (cd->ts_suspended && !cd->suspended) {
			if (cd_sub)
				cd_sub->quickly_screen_on_off = true;
			for (i = 0; i < PM_TYPE_LIST_LEN; i++) {
				if (pm_type_list[i] == TS_EARLY_SUSPEND)
					msleep(200); /* 200ms wait ic ready */
				thp_proc_screen_switch_cmd(cd, pm_type_list[i],
							   SHORT_SYNC_TIMEOUT,
							   SUB_TOUCH_PANEL);
				if ((pm_type_list[i] == TS_RESUME_DEVICE) ||
				    (pm_type_list[i] == TS_AFTER_RESUME))
					continue;
				else
					pm_type_switch(cd, pm_type_list[i]);
			}
		}
		thp_log_info(cd, "%s: switch to SCREEN_FOLDED\n", __func__);
	}
	if (current_status == SCREEN_UNFOLD) {
		/*
		 * if this flag not used in the last time suspend folded,
		 * and next time is resume folded, external screen will freeze,
		 * so set flag false when unfold cover this scense
		 */
		cd->suspend_fold_flag = false;
		thp_proc_screen_switch_cmd(cd, TS_SCREEN_SWITCH,
					   SHORT_SYNC_TIMEOUT, SUB_TOUCH_PANEL);
		if (cd->ts_suspended) {
			if (cd_sub)
				cd_sub->quickly_screen_on_off = true;
			for (i = 0; i < PM_TYPE_LIST_LEN; i++) {
				if (pm_type_list[i] == TS_EARLY_SUSPEND)
					msleep(200); /* 200ms wait ic ready */
				thp_proc_screen_switch_cmd(cd, pm_type_list[i],
							   SHORT_SYNC_TIMEOUT,
							   SUB_TOUCH_PANEL);
			}
		}
		for (i = 0; i < RESUME_TYPE_LEN; i++)
			thp_proc_screen_switch_cmd(cd, pm_type_list[i],
						   SHORT_SYNC_TIMEOUT,
						   MAIN_TOUCH_PANEL);
		thp_log_info(cd, "%s: switch to SCREEN_UNFOLD\n", __func__);
	}
	return 0;
}

static int thp_power_control_notify(struct thp_core_data *cd,
				    enum lcd_kit_ts_pm_type pm_type,
				    int timeout)
{
	struct thp_cmd_node cmd;

	if (!cd) {
		thp_log_err(cd, "%s: tp is not registered\n", __func__);
		return -ENODEV;
	}

	if (thp_is_factory() && cd->always_poweron_in_screenoff) {
		thp_log_info(cd, "%s screenoff cap testing, NO poweroff\n",
			     __func__);
		return 0;
	}
	thp_log_info(cd, "%s: called by lcdkit, pm_type=%d\n", __func__,
		     pm_type);
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
	/* If the AP side AOD display function is enabled, including
	 * click-display, all-day display, and fingerprint, the
	 * AOD controls the TP power status.
	 */
	if (cd->multi_panel_index != SINGLE_TOUCH_PANEL &&
	    pm_type == TS_SCREEN_SWITCH) {
		cd->screen_switch_flag = true;
		return 0;
	}
	if (cd->suspend_resume_control && cd->fast_booting_solution) {
		memset(&cmd, 0, sizeof(cmd));
		if (pm_type == TS_EARLY_SUSPEND)
			cmd.command = TS_SUSPEND;
		else if (pm_type == TS_RESUME_DEVICE)
			cmd.command = TS_RESUME;
		else
			cmd.command = TS_INVAILD_CMD;
		if (thp_put_one_cmd(cd, &cmd, NO_SYNC_TIMEOUT))
			thp_log_err(cd, "%s: put cmd error\n", __func__);
		return 0;
	}
#endif
	return pm_type_switch(cd, pm_type);
}

#define PM_STATUS_BIT1 2
#define PM_STATUS_BIT0 1
static void thp_set_multi_pm_status(struct thp_core_data *cd,
				    enum lcd_kit_ts_pm_type pm_type,
				    int panel_index)
{
	if (panel_index == MAIN_TOUCH_PANEL) {
		if (pm_type == TS_EARLY_SUSPEND)
			cd->current_pm_status &= ~PM_STATUS_BIT1;
		else if (pm_type == TS_RESUME_DEVICE)
			cd->current_pm_status |= PM_STATUS_BIT1;
	} else if (panel_index == SUB_TOUCH_PANEL) {
		if (pm_type == TS_EARLY_SUSPEND)
			cd->current_pm_status &= ~PM_STATUS_BIT0;
		else if (pm_type == TS_RESUME_DEVICE)
			cd->current_pm_status |= PM_STATUS_BIT0;
	}
}

static int thp_sub_power_control_notify(enum lcd_kit_ts_pm_type pm_type,
					int timeout)
{
	struct thp_core_data *cd_sub = thp_get_sub_core_data();

	if (!cd_sub) {
		thp_log_err(cd_sub, "%s: sub tp is not registered\n", __func__);
		return -ENODEV;
	}

	return thp_power_control_notify(cd_sub, pm_type, timeout);
}

static void thp_multi_resume(struct thp_core_data *cd)
{
	int err;

	if (!cd->suspended) {
		thp_log_info(cd, "%s: already resumed, return\n", __func__);
		return;
	}
	err = thp_power_control_notify(cd, TS_RESUME_DEVICE, NO_SYNC_TIMEOUT);
	if (err)
		thp_log_err(cd, "%s: TS_RESUME_DEVICE fail\n", __func__);
	msleep(cd->resume_delayms_early_to_later);
	err = thp_power_control_notify(cd, TS_AFTER_RESUME, NO_SYNC_TIMEOUT);
	if (err)
		thp_log_err(cd, "%s: TS_AFTER_RESUME fail\n", __func__);
	thp_set_multi_pm_status(cd, TS_RESUME_DEVICE, MAIN_TOUCH_PANEL);
}

static void thp_multi_suspend(struct thp_core_data *cd)
{
	int err;

	thp_set_multi_pm_status(cd, TS_EARLY_SUSPEND, MAIN_TOUCH_PANEL);
	err = thp_power_control_notify(cd, TS_EARLY_SUSPEND, NO_SYNC_TIMEOUT);
	if (err)
		thp_log_err(cd, "%s: TS_EARLY_SUSPEND fail\n", __func__);
	err = thp_power_control_notify(cd, TS_BEFORE_SUSPEND, NO_SYNC_TIMEOUT);
	if (err)
		thp_log_err(cd, "%s: TS_BEFORE_SUSPEND fail\n", __func__);
	err = thp_power_control_notify(cd, TS_SUSPEND_DEVICE, NO_SYNC_TIMEOUT);
	if (err)
		thp_log_err(cd, "%s: TS_SUSPEND_DEVICE fail\n", __func__);
}

static void tskit_multi_resume(struct thp_core_data *cd)
{
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	struct tpkit_thp_ops *ops = tpkit_get_ops();
#endif
	int err;

	cd->ts_suspended = false;
	if (cd->sub_solution == THP_SOLUTION) {
		err = thp_sub_power_control_notify(TS_RESUME_DEVICE,
						   SHORT_SYNC_TIMEOUT);
		if (err)
			thp_log_err(cd, "%s: TS_RESUME_DEVICE fail\n",
				    __func__);
		err = thp_sub_power_control_notify(TS_AFTER_RESUME,
						   SHORT_SYNC_TIMEOUT);
		if (err)
			thp_log_err(cd, "%s: TS_AFTER_RESUME fail\n", __func__);
	} else {
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
		if (ops && ops->power_notify) {
			err = ops->power_notify(TS_RESUME_DEVICE,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_RESUME_DEVICE fail\n",
					    __func__);
			err = ops->power_notify(TS_AFTER_RESUME,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_AFTER_RESUME fail\n",
					    __func__);
		}
#endif
	}
	thp_set_multi_pm_status(cd, TS_RESUME_DEVICE, SUB_TOUCH_PANEL);
}

static void tskit_multi_suspend(struct thp_core_data *cd)
{
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	struct tpkit_thp_ops *ops = tpkit_get_ops();
#endif
	int err;

	cd->ts_suspended = true;
	thp_set_multi_pm_status(cd, TS_EARLY_SUSPEND, SUB_TOUCH_PANEL);
	if (cd->sub_solution == THP_SOLUTION) {
		err = thp_sub_power_control_notify(TS_EARLY_SUSPEND,
						   SHORT_SYNC_TIMEOUT);
		if (err)
			thp_log_err(cd, "%s: TS_EARLY_SUSPEND fail\n",
				    __func__);
		err = thp_sub_power_control_notify(TS_BEFORE_SUSPEND,
						   SHORT_SYNC_TIMEOUT);
		if (err)
			thp_log_err(cd, "%s: TS_BEFORE_SUSPEND fail\n",
				    __func__);
		err = thp_sub_power_control_notify(TS_SUSPEND_DEVICE,
						   SHORT_SYNC_TIMEOUT);
		if (err)
			thp_log_err(cd, "%s: TS_SUSPEND_DEVICE fail\n",
				    __func__);
	} else {
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
		if (ops && ops->power_notify) {
			err = ops->power_notify(TS_EARLY_SUSPEND,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_EARLY_SUSPEND fail\n",
					    __func__);
			err = ops->power_notify(TS_BEFORE_SUSPEND,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_BEFORE_SUSPEND fail\n",
					    __func__);
			err = ops->power_notify(TS_SUSPEND_DEVICE,
						SHORT_SYNC_TIMEOUT,
						SUB_TOUCH_PANEL);
			if (err)
				thp_log_err(cd, "%s: TS_SUSPEND_DEVICE fail\n",
					    __func__);
		}
#endif
	}
}

static void aod_notify_subpanel_power(struct thp_core_data *cd,
				      struct thp_cmd_node *in_cmd,
				      struct thp_cmd_node *out_cmd)
{
	static unsigned int last_status = INVALID_VALUE;
	unsigned int status = in_cmd->cmd_param.pub_params.power_ctrl;

	thp_log_info(cd, "%s: status is %u\n", __func__, status);

	if (cd->suspend_fold_flag && status == AOD_NOTIFY_TP_RESUME) {
		thp_log_info(cd, "%s: no need to resume\n", __func__);
		cd->suspend_fold_flag = false;
		return;
	}
	if (status == last_status) {
		thp_log_info(cd, "%s: repeat event, dont handle!!\n", __func__);
		return;
	}

	switch (status) {
	case AOD_NOTIFY_TP_SUSPEND:
		out_cmd->command = AOD_NOTIFY_SUBPANEL_SUSPEND;
		break;
	case AOD_NOTIFY_TP_RESUME:
		out_cmd->command = AOD_NOTIFY_SUBPANEL_RESUME;
		break;
	case NOTIFY_TP_STANDBY_IN:
		out_cmd->command = AOD_NOTIFY_SUBPANEL_STANDBY_IN;
		break;
	case NOTIFY_TP_STANDBY_OUT:
		out_cmd->command = AOD_NOTIFY_SUBPANEL_STANDBY_OUT;
		break;
	default:
		out_cmd->command = TS_INVAILD_CMD;
		break;
	}
	last_status = status;
}

int thp_pm_cmd_proc(struct thp_core_data *cd, struct thp_cmd_node *in_cmd,
		    struct thp_cmd_node *out_cmd)
{
	int error = NO_ERR;

	out_cmd->command = TS_INVAILD_CMD;
	switch (in_cmd->command) {
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
	case TS_SUSPEND:
		thp_suspend_thread(cd);
		break;
	case TS_RESUME:
		thp_resume_thread(cd);
		break;
	case AOD_NOTIFY_SUBPANEL_SUSPEND:
		aod_notify_subpanel_suspend(cd);
		break;
	case AOD_NOTIFY_SUBPANEL_RESUME:
		aod_notify_subpanel_resume(cd);
		break;
	case TS_STANDBY_IN:
		notify_tp_standby_in(cd);
		break;
	case TS_STANDBY_OUT:
		notify_tp_standby_out(cd);
		break;
	case AOD_NOTIFY_SUBPANEL_STANDBY_IN:
		notify_tp_subpanel_standby_mode(cd, 1);
		break;
	case AOD_NOTIFY_SUBPANEL_STANDBY_OUT:
		notify_tp_subpanel_standby_mode(cd, 0);
		break;
#endif
	case THP_MUTIL_RESUME_THREAD:
		thp_multi_resume(cd);
		break;
	case THP_MUTIL_SUSPEND_THREAD:
		thp_multi_suspend(cd);
		break;
	case TSKIT_MUTIL_RESUME_THREAD:
		tskit_multi_resume(cd);
		break;
	case TSKIT_MUTIL_SUSPEND_THREAD:
		tskit_multi_suspend(cd);
		break;
	case TS_SCREEN_FOLD:
		error = multi_screen_status_switch(cd, SCREEN_FOLDED);
		break;
	case TS_SCREEN_UNFOLD:
		error = multi_screen_status_switch(cd, SCREEN_UNFOLD);
		break;
	case AOD_NOTIFY_SUBPANEL_POWER_CTRL:
		aod_notify_subpanel_power(cd, in_cmd, out_cmd);
		break;
	default:
		break;
	}
	return error;
}

int thp_pinctrl_select_normal_state(struct thp_core_data *cd)
{
	int retval = 0;

	retval = pinctrl_select_state(cd->pctrl, cd->pins_default);
	if (retval < 0)
		thp_log_err(cd, "set iomux normal error, %d\n", retval);
	return retval;
}

int thp_pinctrl_select_lowpower_state(struct thp_core_data *cd)
{
	int retval;

	retval = pinctrl_select_state(cd->pctrl, cd->pins_idle);
	if (retval < 0)
		thp_log_err(cd, "set iomux lowpower error, %d\n", retval);
	return retval;
}

int thp_lcdkit_get_project_id(struct thp_core_data *cd, char *buff, int len)
{
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	struct lcd_kit_ops *tp_ops = lcd_kit_get_ops();
#endif
	int rc = 0;

	if (len < THP_PROJECT_ID_LEN + 1) {
		thp_log_err(cd, "%s: buff is too short, len = %d\n", __func__,
			    len);
		return -EINVAL;
	}
	if (cd->is_udp) {
#if (!(IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK)))
		rc = hostprocessing_get_project_id_for_udp(buff, len);
#endif
	} else {
#ifndef CONFIG_LCD_KIT_DRIVER
		rc = hostprocessing_get_project_id(buff, len);
#else
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
		if (tp_ops && tp_ops->get_project_id) {
			rc = tp_ops->get_project_id(buff);
		} else {
			rc = -EINVAL;
			thp_log_err(cd, "%s:get lcd_kit_get_ops fail\n",
				    __func__);
		}
#endif
#endif
	}
	return rc;
}

int is_pt_test_mode(struct thp_device *tdev)
{
	int thp_pt_station_flag = 0;
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	int ret;
	struct lcd_kit_ops *lcd_ops = lcd_kit_get_ops();

	if ((lcd_ops) && (lcd_ops->get_status_by_type)) {
		ret = lcd_ops->get_status_by_type(PT_STATION_TYPE,
						  &thp_pt_station_flag);
		if (ret < 0) {
			thp_log_info(tdev->thp_core,
				     "%s: get thp_pt_station_flag fail\n",
				     __func__);
			return ret;
		}
	}
#else
	thp_pt_station_flag = g_tskit_pt_station_flag &&
			      tdev->test_config.pt_station_test;
#endif

	thp_log_info(tdev->thp_core, "%s thp_pt_station_flag = %d\n", __func__,
		     thp_pt_station_flag);

	return thp_pt_station_flag;
}

bool thp_lcdkit_ready(void)
{
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	return lcd_kit_get_ops() != NULL;
#endif
	return true;
}

#endif //(defined CONFIG_HONOR_THP_PM)

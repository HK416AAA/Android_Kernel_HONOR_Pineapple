/*
 * direct_charger.h
 *
 * direct charger driver
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

#ifndef _DIRECT_CHARGER_H_
#define _DIRECT_CHARGER_H_

#include <linux/module.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/pm_wakeup.h>
#include <dsm/dsm_pub.h>
#include <hwpower/power_dsm.h>
#include <hwpower/power_nv.h>
#include <hwpower/power_wakeup.h>
#include <hwpower/power_time.h>
#include <hwpower/power_delay.h>
#include <hwpower/power_platform_interface.h>
#include <hwpower/power_supply_interface.h>
#include <hwpower/ffc_control.h>
#include <hwpower/honor_charger_type.h>
#include <hwpower/buck_charger_interface.h>
#include <hwpower/honor_charger_manager/honor_charger_control.h>
#include <hwpower/honor_charger_manager/adaptor_test.h>

#include <log/hw_log.h>
#include <hwpower/power_ui_ne.h>
#ifdef CONFIG_TCPC_CLASS
#endif
#include <power_manager/usb/hw_pd_dev.h>
#ifdef CONFIG_HONOR_HW_DEV_DCT
#include <hwmanufac/dev_detect/dev_detect.h>
#endif
#ifdef CONFIG_SUPERSWITCH_FSC
#include <power_manager/usb/superswitch/fsc/core/hw_scp.h>
#endif
#ifdef CONFIG_WIRELESS_CHARGER
#include <power_manager/power/wireless/wireless_charger.h>
#endif
#ifdef CONFIG_DP_AUX_SWITCH
#include "power_manager/dp/dp_aux_switch.h"
#endif
#include <hwpower/direct_charge/direct_charge_error_handle.h>
#include <hwpower/direct_charge/direct_charge_ic_manager.h>
#include <hwpower/direct_charge/direct_charge_comp.h>

#include <hwpower/direct_charger/direct_charge_power_supply.h>
#include <hwpower/direct_charger/direct_charger_check.h>
#include <hwpower/direct_charger/direct_charger_data.h>
#include <hwpower/direct_charger/direct_charger_stage.h>
#include <hwpower/direct_charger/direct_charger_mode.h>
#include <hwpower/direct_charger/direct_charger_interface.h>
#include <hwpower/direct_charger/direct_charger_internal_interface.h>
#include <hwpower/direct_charger/direct_charger_adapter.h>
#include <hwpower/direct_charger/direct_charger_battery.h>
#include <hwpower/direct_charger/direct_charger_cable.h>
#include <hwpower/direct_charger/direct_charger_ic_device.h>
#include <hwpower/direct_charger/direct_charger_path_manager.h>
#include <hwpower/direct_charger/direct_charger_uevent.h>
#include <hwpower/direct_charger/direct_charger_security.h>
#include <hwpower/direct_charger/direct_charger_time.h>
#include <hwpower/direct_charger/direct_charger_thermal.h>
#include <hwpower/direct_charger/multi_ic_check.h>
#include <hwpower/direct_charger/direct_charger_mmi.h>
#include <hwpower/direct_charger/direct_charger_rt.h>
#include <hwpower/adapter_protocol.h>
#include <hwpower/adapter_protocol_scp.h>
#include <hwpower/adapter_protocol_fcp.h>
#include <hwpower/power_event_ne.h>
#include <hwpower/power_dts.h>
#include <hwpower/adapter_protocol.h>
#include <hwpower/power_event_ne.h>
#include <hwpower/power_dts.h>
#include <hwpower/power_gpio.h>

#define DIRECT_CHARGER_DRIVER_NAME "direct_charger"

#define DC_DMDLOG_SIZE 2048
#define REVERSE_OCP_CNT 3
#define OTP_CNT 3
#define ADP_OTP_CNT 3
#define DOUBLE_SIZE 2
#define ERR_NO_STRING_SIZE 256

/* sensor_id#scene_id#stage */
#define DC_THERMAL_REASON_SIZE 16

#define DC_CHARGE_TYPE_LVC 3
#define DC_CHARGE_TYPE_SC 2
#define DC_CHARGE_TYPE_HSC 4

#define DC_ERR_CNT_MAX 8
#define DC_MULTI_ERR_CNT_MAX 4
#define DC_OPEN_RETRY_CNT_MAX 3

#define INVALID (-1)
#define VALID 0
#define WAIT_LS_DISCHARGE 200
#define MAX_TIMES_FOR_SET_ADAPTER_VOL 30
#define MIN_CURRENT_FOR_RES_DETECT 800
#define CURRENT_SET_FOR_RES_DETECT 1000
#define STEP_VOL_START 6000

#define ENABLE 1
#define DISABLE 0

#define DC_ADAPTER_DETECT 1
#define DC_ADAPTER_NOT_DETECT 0

#define VBUS_ON_THRESHOLD 3000
#define VBAT_VBUS_DIFFERENCE 150
#define KICK_WATCHDOG_TIME 1000
#define WATCHDOG_TIMEOUT 2000
#define BATTERY_CAPACITY_HIGH_TH 95
#define BAT_RATED_VOLT 5000
#define POWER_TH_IGNORE_ANTIFAKE 22500

#define DC_LS_RECOVERY_DELAY 500 /* ms */
#define DC_COUL_CURRENT_UNIT_DEVIDE 1000 /* 1000ua equal 1ma */

#define DC_IN_CHARGING_STAGE 0
#define DC_NOT_IN_CHARGING_STAGE (-1)

#define DC_IN_CHARGE_DONE_STAGE 0
#define DC_NOT_IN_CHARGE_DONE_STAGE (-1)

#define DC_SET_DISABLE_FLAGS 1
#define DC_CLEAR_DISABLE_FLAGS 0

#define BASP_PARA_SCALE 100
#define BASP_RATIO_POLICY_ALL 0 /*apply ratio policy for all segments*/
#define BASP_RATIO_POLICY_MAX 1 /*apply ratio policy for max current*/
#define BASP_PARA_LEVEL 10

#define DC_SC_CUR_LEN 64
#define DC_HSC_CUR_LEN 64

/* basp */
#define BASP_VTERM_DEC_MIN 0
#define BASP_VTERM_DEC_MAX 100 /* mv */
#define BASP_VTERM_DEC_DEFAULT 0
#define BASP_ICHG_RATIO_MIN 70
#define BASP_ICHG_RATIO_MAX 100
#define BASP_ICHG_RATIO_DEFAULT 100

#define DC_SINGLEIC_CURRENT_LIMIT 8000
#define DC_SINGLEIC_MAIN_CURRENT_LIMIT 8000
#define DC_SINGLEIC_AUX_CURRENT_LIMIT 5000
#define DC_MULTI_IC_IBAT_TH 4000
#define DC_CURRENT_OFFSET 300
#define MIN_CURRENT_FOR_MULTI_IC 500
#define DC_MULTI_IC_INFO_IBAT_TH 7000

enum charge_status_event {
	VCHRG_POWER_NONE_EVENT = 0,
	VCHRG_NOT_CHARGING_EVENT,
	VCHRG_START_CHARGING_EVENT,
	VCHRG_START_AC_CHARGING_EVENT,
	VCHRG_START_USB_CHARGING_EVENT,
	VCHRG_CHARGE_DONE_EVENT,
	VCHRG_STOP_CHARGING_EVENT,
	VCHRG_POWER_SUPPLY_OVERVOLTAGE,
	VCHRG_POWER_SUPPLY_WEAKSOURCE,
	VCHRG_STATE_WDT_TIMEOUT,
	BATTERY_LOW_WARNING,
	BATTERY_LOW_SHUTDOWN,
	BATTERY_MOVE,
	SWITCH_INTB_WORK_EVENT,
	VCHRG_THERMAL_POWER_OFF,
	WIRELESS_TX_STATUS_CHANGED,
	WIRELESS_COVER_DETECTED,
	VCHRG_CURRENT_FULL_EVENT,
};

/*
 * define operate type for retry with direct charge
 * DC is simplified identifier with direct-charge
 */
enum direct_charge_retry_operate_type {
	DC_RESET_ADAPTER = 0,
	DC_RESET_MASTER,
};

enum direct_charge_succ_flag {
	DC_SUCCESS,
	DC_ERROR,
};

/*
 * define sysfs type with direct charge
 * DC is simplified identifier with direct-charge
 */
enum direct_charge_sysfs_type {
	DC_SYSFS_BEGIN = 0,
	DC_SYSFS_IIN_THERMAL = DC_SYSFS_BEGIN,
	DC_SYSFS_IIN_THERMAL_ICHG_CONTROL,
	DC_SYSFS_ICHG_CONTROL_ENABLE,
	DC_SYSFS_ADAPTER_DETECT,
	DC_SYSFS_IADAPT,
	DC_SYSFS_FULL_PATH_RESISTANCE,
	DC_SYSFS_DIRECT_CHARGE_SUCC,
	DC_SYSFS_SET_RESISTANCE_THRESHOLD,
	DC_SYSFS_SET_CHARGETYPE_PRIORITY,
	DC_SYSFS_THERMAL_REASON,
	DC_SYSFS_AF,
	DC_SYSFS_MULTI_SC_CUR,
	DC_SYSFS_SC_STATE,
	DC_SYSFS_DUMMY_VBAT,
	DC_SYSFS_END,
};

/*
 * define disable type with direct charge
 * DC is simplified identifier with direct-charge
 */
enum direct_charge_disable_type {
	DC_DISABLE_BEGIN = 0,
	DC_DISABLE_SYS_NODE = DC_DISABLE_BEGIN,
	DC_DISABLE_FATAL_ISC_TYPE,
	DC_DISABLE_WIRELESS_TX,
	DC_DISABLE_BATT_CERTIFICATION_TYPE,
	DC_DISABLE_END,
};

/*
 * define fault type for device with direct charge
 * DC is simplified identifier with direct-charge
 */
enum direct_charge_fault_type {
	/* for common */
	DC_FAULT_NON = 0,
	DC_FAULT_VBUS_OVP,
	DC_FAULT_REVERSE_OCP,
	DC_FAULT_OTP,
	DC_FAULT_TSBUS_OTP,
	DC_FAULT_TSBAT_OTP,
	DC_FAULT_TDIE_OTP,
	DC_FAULT_INPUT_OCP,
	DC_FAULT_VDROP_OVP,
	DC_FAULT_AC_OVP,
	DC_FAULT_VBAT_OVP,
	DC_FAULT_IBAT_OCP,
	DC_FAULT_IBUS_OCP,
	DC_FAULT_CONV_OCP,
	/* for ltc7820 device */
	DC_FAULT_LTC7820,
	/* for ina231 device */
	DC_FAULT_INA231,
	/* for cc and vbus short */
	DC_FAULT_CC_SHORT,
	DC_FAULT_I2C_ERROR,
	DC_FAULT_TOTAL,
	/* for hsc aw32280 */
	DC_FAULT_IBUS_OCP_PEAK,
	DC_FAULT_IQ6Q8_OCP_PEAK,
	DC_FAULT_VDROP_MIN,
	DC_FAULT_IBUS_UCP,
	DC_FAULT_IBUS_RCP,
	/* for rt9756 */
	DC_FAULT_VBUS_LOW,
	DC_FAULT_VBUS_HIGH,
};

enum direct_charge_info_type {
	CC_CABLE_DETECT_OK,
};

enum dc_iin_thermal_channel_type {
	DC_CHANNEL_TYPE_BEGIN = 0,
	DC_SINGLE_CHANNEL = DC_CHANNEL_TYPE_BEGIN,
	DC_DUAL_CHANNEL,
	DC_CHANNEL_TYPE_END,
};

struct nty_data {
	unsigned short addr;
	u8 event1;
	u8 event2;
	u8 event3;
	u8 event4;
	u8 event5;
	u8 event6;
	u8 event7;
	const char *ic_name;
	u8 ic_role;
};

struct direct_charge_charge_info {
	int succ_flag;
	const char *ic_name[CHARGE_IC_MAX_NUM];
	int channel_num;
	int ibat_max;
	int ibus[CHARGE_IC_MAX_NUM];
	int vbat[CHARGE_IC_MAX_NUM];
	int vout[CHARGE_IC_MAX_NUM];
	int tbat[CHARGE_IC_MAX_NUM];
};

/*
 * define voltage parameters of different batteries
 * at different temperature threshold
 * support up to 8 parameters list on dts
 */
#define DC_VOLT_GROUP_MAX 16 /* battery num max 6 */
#define DC_BAT_BRAND_LEN_MAX 16
#define DC_VOLT_NODE_LEN_MAX 16

/*
 * define multistage (cc)constant current and (cv)constant voltage
 * support up to 5 parameters list on dts
 */
#define DC_VOLT_LEVEL 8

enum direct_charge_volt_info {
	DC_PARA_VOL_TH = 0,
	DC_PARA_CUR_TH_HIGH,
	DC_PARA_CUR_TH_LOW,
	DC_PARA_VOLT_TOTAL,
};

struct direct_charge_volt_para {
	int vol_th;
	int cur_th_high;
	int cur_th_low;
};

enum direct_charge_bat_info {
	DC_PARA_BAT_ID = 0,
	DC_PARA_TEMP_LOW,
	DC_PARA_TEMP_HIGH,
	DC_PARA_INDEX,
	DC_PARA_BAT_TOTAL,
};

struct direct_charge_bat_para {
	int temp_low;
	int temp_high;
	int parse_ok;
	char batid[DC_BAT_BRAND_LEN_MAX];
	char volt_para_index[DC_VOLT_NODE_LEN_MAX];
};

struct direct_charge_volt_para_group {
	struct direct_charge_volt_para volt_info[DC_VOLT_LEVEL];
	struct direct_charge_bat_para bat_info;
	int stage_size;
};

#define DC_VSTEP_PARA_LEVEL 4

enum direct_charge_vstep_info {
	DC_VSTEP_INFO_CURR_GAP,
	DC_VSTEP_INFO_VSTEP,
	DC_VSTEP_INFO_MAX,
};

struct direct_charge_vstep_para {
	int curr_gap;
	int vstep;
};

struct direct_charge_config {
	struct direct_charge_rt_test_para rt_test_para[DC_MODE_TOTAL];
	struct direct_charge_volt_para_group
		orig_volt_para_group[DC_VOLT_GROUP_MAX];
	struct direct_charge_vstep_para vstep_para[DC_VSTEP_PARA_LEVEL];
	int stage_group_size;

	int vstep;
	int delta_err;
	int delta_err_10v2p25a;
	int delta_err_10v4a;

	int init_delt_vset;
	int path_ibus_th;

	int dc_volt_ratio;

	u32 startup_iin_limit;
	u32 hota_iin_limit;

	int is_align_caculation_work;
	u32 threshold_caculation_interval;
	u32 charge_control_interval;
};

struct direct_charge_sysfs_data {
	int sysfs_enable_charger;
	u32 sysfs_mainsc_enable_charger;
	u32 sysfs_auxsc_enable_charger;
	unsigned int sysfs_disable_charger[DC_DISABLE_END];
	int sysfs_iin_thermal;
	int sysfs_iin_thermal_array[DC_CHANNEL_TYPE_END];
	int sysfs_iin_thermal_ichg_control;
	unsigned int vterm_dec;
	unsigned int ichg_ratio;
	int ichg_control_enable;
	bool ignore_full_path_res;
	u32 dummy_vbat;
	char thermal_reason[DC_THERMAL_REASON_SIZE];
};

struct direct_charge_device {
	struct device *dev;
	struct hrtimer calc_thld_timer;
	struct hrtimer control_timer;
	struct hrtimer kick_wtd_timer;
	struct workqueue_struct *charging_wq;
	struct workqueue_struct *kick_wtd_wq;
	struct work_struct calc_thld_work;
	struct work_struct control_work;
	struct work_struct fault_work;
	struct work_struct kick_wtd_work;
	struct delayed_work hiz_mode_work;
	struct wakeup_source charging_lock;
	struct notifier_block fault_nb;
	struct atomic_notifier_head *fault_notifier_list;
	enum direct_charge_fault_type charge_fault;
	struct nty_data *fault_data;

	int working_mode;
	int cur_mode;
	int local_mode;
	int sc_mode;
	int prot_type;
	int adapter_mode;
	unsigned int dc_stage;
	u32 direct_charge_start_time;

	struct direct_charge_volt_para volt_para[DC_VOLT_LEVEL];
	struct direct_charge_volt_para orig_volt_para[DC_VOLT_LEVEL];
	int stage_size;
	int stage_group_cur;
	int cur_stage;
	int pre_stage;
	int cur_vbat_th;
	int cur_ibat_th_high;
	int cur_ibat_th_low;
	int dc_open_retry_cnt;

	int error_cnt;
	int error_cnt_enable;
	int stop_charging_flag_error;
	int exit_charging_flag;
	int charge_done_flag;
	int dc_err_report_flag;
	int dc_succ_flag;
	int hv_flag;
	int sc_conv_ocp_count;
	int can_stop_kick_wdt;
	int adaptor_test_result_type;
	bool dc_need_detect_adp_mode;
	char dsm_buff[DC_DMDLOG_SIZE];
	struct direct_charge_charge_info curr_info;

	struct direct_charge_config *config;
	struct direct_charge_sysfs_data *sysfs_data;

	struct direct_charge_mode_ops *ops[DC_MODE_TYPE_END];
	struct direct_charge_rt_test_result rt_test_result[DC_MODE_TOTAL];
	int unsupport_get_pps_status;
	int pps_vol_comp;
};

struct direct_charge_device *direct_charge_get_di(void);
bool direct_charge_is_sleep_exit(void);
int direct_charge_get_working_mode(void);
unsigned int direct_charge_get_local_mode(void);
int direct_charge_is_failed(void);
void direct_charge_exit(void);
void direct_charge_set_stop_charging_flag(int value);
int direct_charge_get_stop_charging_flag(void);
void direct_charge_set_error_cnt_enable(int enable);
void direct_charge_set_charge_done_flag(int value);
void direct_charge_set_exit_charging_flag(int value);
void direct_charge_control_work(struct work_struct *work);
void direct_charge_calc_thld_work(struct work_struct *work);
void direct_charge_kick_wtd_work(struct work_struct *work);
enum hrtimer_restart direct_charge_calc_thld_timer_func(struct hrtimer *timer);
enum hrtimer_restart direct_charge_control_timer_func(struct hrtimer *timer);
enum hrtimer_restart direct_charge_kick_wtd_timer_func(struct hrtimer *timer);
int direct_charge_fault_notifier_call(struct notifier_block *nb,
				      unsigned long event, void *data);
int direct_charge_start_charging(void);
void direct_charge_stop_charging(void);
void direct_charge_set_hv_flag(int flag);
void dc_init_parameters(struct direct_charge_device *di);
void direct_charge_wake_lock(void);
void direct_charge_wake_unlock(void);

int direct_charge_open_charging_path(void);
void direct_charge_select_charging_volt_param(struct direct_charge_device *di);
void direct_charge_select_charging_stage(struct direct_charge_device *di);
void direct_charge_select_soh_policy(struct direct_charge_device *di);
void direct_charge_select_charging_param(struct direct_charge_device *di);
void direct_charge_select_charge_path(struct direct_charge_device *di);
void direct_charge_regulation(struct direct_charge_device *di);

int hsc_module_init(void);
void hsc_module_exit(void);
int sc_module_init(void);
void sc_module_exit(void);
int lvc_module_init(void);
void lvc_module_exit(void);

#if IS_ENABLED(CONFIG_WIRELESS_CHARGER)
extern bool wltx_need_disable_wired_dc(void);
#endif /* CONFIG_WIRELESS_CHARGER */

#endif /* _DIRECT_CHARGER_H_ */

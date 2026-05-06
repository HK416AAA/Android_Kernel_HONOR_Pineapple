/*
 * Honor Touchscreen Driver
 *
 * Copyright (c) 2012-2021 Honor Technologies Co., Ltd.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/hwspinlock.h>
#include <linux/kthread.h>
#include <linux/pm_runtime.h>
#include "honor_thp.h"

#include "honor_thp_mt_wrapper.h"
#include "honor_thp_attr.h"
#ifdef CONFIG_HONOR_SHB_THP
#include "honor_thp_log.h"
#endif
#include "hwspinlock_internal.h"

#ifdef CONFIG_INPUTHUB_20
#include "contexthub_recovery.h"
#endif

#ifdef CONFIG_HONOR_PS_SENSOR
#include "ps_sensor.h"
#endif

#ifdef CONFIG_HONOR_SENSORS_2_0
#include "sensor_scp.h"
#endif

#ifdef CONFIG_HONOR_DUBAI_COMMON
#include <log/hwlog_kernel.h>
#endif

#if (!(IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK)))
#include <honor_platform/sensor/hw_comm_pmic.h>
#endif

#ifdef CONFIG_HONOR_BCI_BATTERY
#include <linux/power/honor/honor_bci_battery.h>
#endif

#ifdef CONFIG_HONOR_HW_DEV_DCT
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
#include <hwmanufac/dev_detect/dev_detect.h>
#else
#include <honor_platform/devdetect/hw_dev_dec.h>
#endif
#endif

#if IS_ENABLED(CONFIG_DSM)
#include <dsm/dsm_pub.h>
#endif

#if defined(CONFIG_TEE_TUI)
#include "tui.h"
#endif

#define trace_touch(x...)

struct tp_spi_interface_ops {
	int (*tp_spi_sync)(struct spi_message *message);
};

extern int touch_boost_init(void);
extern int focal_driver_module_init(struct thp_core_data *cd);
extern int syna_driver_module_init(struct thp_core_data *cd);
extern int goodix_driver_module_init(struct thp_core_data *cd);
extern int himax_driver_module_init(struct thp_core_data *cd);
static void thp_wakeup_screenon_waitq(struct thp_core_data *cd);
static void thp_copy_frame(struct thp_core_data *cd);
#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
extern void tp_spi_interface_ops_register(struct tp_spi_interface_ops *ops);
#endif

extern int g_thp_pen_spi_init(void);
extern void g_thp_pen_spi_exit(void);

struct thp_core_data *g_thp_core;
struct thp_core_data *g_thp_core_sub;

DEFINE_MUTEX(boost_init_mutex);
int g_boost_init_flag = 0;

#if IS_ENABLED(CONFIG_TP_QCOM_8450)
int thp_spi_irq_suspend_flag;
EXPORT_SYMBOL(thp_spi_irq_suspend_flag);
#endif

#if (!(IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK)))
static struct hw_comm_pmic_cfg_t tp_pmic_ldo_set;
#endif

#if defined(CONFIG_TEE_TUI)
struct thp_tui_data thp_tui_info;
EXPORT_SYMBOL(thp_tui_info);

struct multi_tp_tui_data tp_tui_data[MAX_SUPPORT_TP_TUI_NUM];
#endif

#if IS_ENABLED(CONFIG_DSM)
#define THP_CHIP_DMD_REPORT_SIZE 1024
#define THP_DSM_BUFF_SIZE 4096
#define THP_DSM_DEVICE_NAME "TPHOSTPROCESSING"
#define THP_DSM_IC_NAME "NNN"
#define THP_DSM_MODULE_NAME "NNN"
#define THP_DSM_DEV_NAME_SINGLE "dsm_tphostprocessing"
#define THP_DSM_DEV_NAME_MAIN "dsm_tphostprocessingmain"
#define THP_DSM_DEV_NAME_SUB "dsm_tphostprocessingsub"

#define PRINTF_ERROR_CODE "printf error\n"
void thp_dmd_report(struct thp_core_data *cd, int dmd_num,
		    const char *psz_format, ...)
{
	va_list args;
	char *input_buf = NULL;
	int ret;
	int buf_length;

	if ((!cd) || (!cd->dsm_thp_dclient)) {
		thp_log_err(cd, "cd or dsm_thp_dclient is NULL\n");
		return;
	}
	if (!psz_format) {
		thp_log_err(cd, "psz_format is NULL\n");
		return;
	}

	input_buf = kzalloc(THP_CHIP_DMD_REPORT_SIZE, GFP_KERNEL);
	if (!input_buf) {
		thp_log_err(cd, "%s: kzalloc failed!\n", __func__);
		return;
	}

	va_start(args, psz_format);
	ret = vsnprintf(input_buf, THP_CHIP_DMD_REPORT_SIZE, psz_format, args);
	if (ret < 0) {
		thp_log_err(cd, "vsnprintf failed, ret: %d\n", ret);
		strcpy(input_buf, PRINTF_ERROR_CODE);
	}
	va_end(args);
	buf_length = strlen(input_buf);
	ret = snprintf(
		(input_buf + buf_length),
		(THP_CHIP_DMD_REPORT_SIZE - buf_length),
		"irq_gpio:%d\tvalue:%d\nreset_gpio:%d\tvalue:%d\nTHP_status:%u\n",
		cd->gpios.irq_gpio, gpio_get_value(cd->gpios.irq_gpio),
		cd->gpios.rst_gpio, gpio_get_value(cd->gpios.rst_gpio),
		thp_get_status_all(cd));
	if (ret < 0)
		thp_log_err(cd, "snprintf failed, ret: %d\n", ret);

	if (!dsm_client_ocuppy(cd->dsm_thp_dclient)) {
		dsm_client_record(cd->dsm_thp_dclient, input_buf);
		dsm_client_notify(cd->dsm_thp_dclient, dmd_num);
		thp_log_info(cd, "%s %s\n", __func__, input_buf);
	}

	kfree(input_buf);
}
#endif

#define THP_DEVICE_NAME "honor_thp"
#define THP_MISC_DEVICE_NAME "thp"
#define RECOVERY_ENTER "1"

static bool recovery_mode = false;
#if (IS_ENABLED(CONFIG_TP_QCOM_8450) || IS_ENABLED(CONFIG_TP_QCOM_8550))
static bool poweroff_charger = false;
#endif

#if ((defined CONFIG_HONOR_THP_MTK) || (IS_ENABLED(CONFIG_TP_QCOM_7XX_8XX)))
struct ud_fp_ops *g_ud_fp_operations;
void fp_ops_register(struct ud_fp_ops *ops)
{
	struct thp_core_data *cd = thp_get_core_data();

	if (g_ud_fp_operations == NULL) {
		g_ud_fp_operations = ops;
		thp_log_info(cd, "%s:g_ud_fp_ops register success\n", __func__);
	} else {
		thp_log_info(cd, "%s:g_ud_fp_ops has been registered\n",
			     __func__);
	}
}

void fp_ops_unregister(struct ud_fp_ops *ops)
{
	struct thp_core_data *cd = thp_get_core_data();

	if (g_ud_fp_operations != NULL) {
		g_ud_fp_operations = NULL;
		thp_log_info(cd, "%s:g_ud_fp_ops_unregister success\n",
			     __func__);
	} else {
		thp_log_info(cd, "%s:g_ud_fp_ops has been unregister\n",
			     __func__);
	}
}

struct ud_fp_ops *fp_get_ops(void)
{
	return g_ud_fp_operations;
}
#endif

#if (IS_ENABLED(CONFIG_TP_QCOM_7XX_8XX))
EXPORT_SYMBOL(fp_ops_register);
EXPORT_SYMBOL(fp_ops_unregister);
EXPORT_SYMBOL(fp_get_ops);
#endif

void get_timestamp(struct timeval *tv)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec / 1000;
}

#ifdef CONFIG_HONOR_SHB_THP
int thp_send_sync_info_to_ap(const char *head)
{
	struct thp_sync_info *rx = NULL;
	struct thp_core_data *cd = thp_get_core_data();

	if (head == NULL) {
		thp_log_err(cd, "thp_sync_info, data is NULL\n");
		return -EINVAL;
	}
	rx = (struct thp_sync_info *)head;
	/* here we do something sending data to afe hal */
	thp_log_info(cd, "thp_sync_info, len:%d\n", rx->size);

	return 0;
}

int thp_send_event_to_drv(const char *data)
{
	char sub_event;
	struct thp_core_data *cd = thp_get_core_data();

	if (!data) {
		thp_log_err(cd, "%s: data is NULL\n", __func__);
		return -EINVAL;
	}
	if (!atomic_read(&cd->register_flag)) {
		thp_log_err(cd, "%s: thp have not be registered\n", __func__);
		return -ENODEV;
	}
	sub_event = data[0];
	switch (sub_event) {
	case THP_SUB_EVENT_SINGLE_CLICK:
		thp_inputkey_report(TS_SINGLE_CLICK);
		break;
	case THP_SUB_EVENT_DOUBLE_CLICK:
		thp_inputkey_report(TS_DOUBLE_CLICK);
		break;
	case THP_SUB_EVENT_STYLUS_SINGLE_CLICK_AND_PRESS:
		thp_input_pen_report(TS_STYLUS_WAKEUP_TO_MEMO);
		break;
	case THP_SUB_EVENT_STYLUS_SINGLE_CLICK:
		thp_input_pen_report(TS_STYLUS_WAKEUP_SCREEN_ON);
		break;
	default:
		thp_log_err(cd, "%s: sub_event is invalid, event = %d\n",
			    __func__, sub_event);
		return -EINVAL;
	}
	thp_log_info(cd, "%s sub_event = %d\n", __func__, sub_event);
	return 0;
}
#endif

#if defined(CONFIG_TEE_TUI)
static void thp_tui_secos_exit(void);
#endif
int thp_spi_sync(struct spi_device *spi, struct spi_message *message)
{
	int ret;

	trace_touch(TOUCH_TRACE_SPI, TOUCH_TRACE_FUNC_IN, "thp");
	ret = spi_sync(spi, message);
	trace_touch(TOUCH_TRACE_SPI, TOUCH_TRACE_FUNC_OUT, "thp");

	return ret;
}

static int thp_is_valid_project_id(const char *id)
{
	int i;

	if ((id == NULL) || (*id == '\0'))
		return false;
	for (i = 0; i < THP_PROJECT_ID_LEN; i++) {
		if (!isascii(*id) || !isalnum(*id))
			return false;
		id++;
	}
	return true;
}

bool thp_is_factory(void)
{
	bool is_factory = false;

#ifdef TP_FACTORY_MODE
	is_factory = true;
#endif
	return is_factory;
}

int thp_project_id_provider(char *project_id, unsigned int len)
{
	struct thp_core_data *cd = thp_get_core_data();

	if (len < THP_PROJECT_ID_LEN) {
		thp_log_err(cd, "%s len is too small\n", __func__);
		return -EINVAL;
	}
	if ((project_id != NULL) && (cd != NULL)) {
		if (thp_is_valid_project_id(cd->project_id)) {
			strncpy(project_id, cd->project_id, THP_PROJECT_ID_LEN);
		} else {
			thp_log_debug(cd, "%s: invalid project id", __func__);
			return -EINVAL;
		}
	}
	return 0;
}
EXPORT_SYMBOL(thp_project_id_provider);

const int get_thp_unregister_ic_num(void)
{
	struct thp_core_data *cd = thp_get_core_data();

	return cd->thp_unregister_ic_num;
}
EXPORT_SYMBOL(get_thp_unregister_ic_num);

struct thp_core_data *thp_get_core_data(void)
{
	return g_thp_core;
}
EXPORT_SYMBOL(thp_get_core_data);

struct thp_core_data *thp_get_sub_core_data(void)
{
	if (g_thp_core_sub && g_thp_core_sub->thp_dev)
		return g_thp_core_sub;

	return NULL;
}

static struct thp_core_data *misc_dev_get_core_data(struct miscdevice *dev)
{
	struct thp_core_data *cd = NULL;

	if (dev == NULL) {
		thp_log_err(cd, "%s: miscdevice is NULL\n", __func__);
		return NULL;
	}

	if (g_thp_core && g_thp_core->thp_misc_device) {
		if (dev->name == g_thp_core->thp_misc_device->name)
			return g_thp_core;
	}
	if (g_thp_core_sub && g_thp_core_sub->thp_misc_device) {
		if (dev->name == g_thp_core_sub->thp_misc_device->name)
			return g_thp_core_sub;
	}
	return NULL;
}

static void thp_wake_up_frame_waitq(struct thp_core_data *cd)
{
	cd->frame_waitq_flag = WAITQ_WAKEUP;
	wake_up_interruptible(&(cd->frame_waitq));
}
static int thp_pinctrl_get_init(struct thp_device *tdev);

#define is_invaild_power_id(x) ((x) >= THP_POWER_ID_MAX)

static char *thp_power_name[THP_POWER_ID_MAX] = {
	"thp-iovdd",
	"thp-vcc",
};

static const char *thp_power_id2name(enum thp_power_id id)
{
	return !is_invaild_power_id(id) ? thp_power_name[id] : 0;
}

int thp_power_supply_get(struct thp_core_data *cd, enum thp_power_id power_id)
{
	struct thp_power_supply *power = NULL;
	int ret;

	if (is_invaild_power_id(power_id)) {
		thp_log_err(cd, "%s: invalid power id %d", __func__, power_id);
		return -EINVAL;
	}

	power = &cd->thp_powers[power_id];
	if (power->type == THP_POWER_UNUSED)
		return 0;
	if (power->use_count) {
		power->use_count++;
		return 0;
	}
	switch (power->type) {
	case THP_POWER_LDO:
		power->regulator = regulator_get(&cd->sdev->dev,
						 thp_power_id2name(power_id));
		if (IS_ERR_OR_NULL(power->regulator)) {
			thp_log_err(cd, "%s:fail to get %s\n", __func__,
				    thp_power_id2name(power_id));
			power->regulator = NULL;
			return -ENODEV;
		}

		ret = regulator_set_voltage(power->regulator, power->ldo_value,
					    power->ldo_value);
		if (ret) {
			thp_log_err(cd, "%s:fail to set %s valude %d\n",
				    __func__, thp_power_id2name(power_id),
				    power->ldo_value);
			regulator_put(power->regulator);
			power->regulator = NULL;
			return ret;
		}
		break;
	case THP_POWER_GPIO:
		ret = gpio_request(power->gpio, thp_power_id2name(power_id));
		if (ret) {
			thp_log_err(cd, "%s:request gpio %d for %s failed\n",
				    __func__, power->gpio,
				    thp_power_id2name(power_id));
			return ret;
		}
		break;

#if (!(IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK)))
	case THP_POWER_PMIC:
		thp_log_info(cd, "%s call %d,%d,%d\n", __func__,
			     power->pmic_power.pmic_num,
			     power->pmic_power.ldo_num,
			     power->pmic_power.value);
		tp_pmic_ldo_set.pmic_num = power->pmic_power.pmic_num;
		tp_pmic_ldo_set.pmic_power_type = power->pmic_power.ldo_num;
		tp_pmic_ldo_set.pmic_power_voltage = power->pmic_power.value;
		break;
#endif

	default:
		thp_log_err(cd, "%s: invalid power type %d\n", __func__,
			    power->type);
		break;
	}
	power->use_count++;
	return 0;
}

int thp_power_supply_put(struct thp_core_data *cd, enum thp_power_id power_id)
{
	struct thp_power_supply *power = NULL;

	if (is_invaild_power_id(power_id)) {
		thp_log_err(cd, "%s: invalid power id %d", __func__, power_id);
		return -EINVAL;
	}

	power = &cd->thp_powers[power_id];
	if (power->type == THP_POWER_UNUSED)
		return 0;

	if (!power->use_count) {
		thp_log_err(cd, "%s:regulator %s not gotten yet\n", __func__,
			    thp_power_id2name(power_id));
		return -ENODEV;
	}

	if ((--power->use_count) > 0)
		return 0;

	switch (power->type) {
	case THP_POWER_LDO:
		if (IS_ERR_OR_NULL(power->regulator)) {
			thp_log_err(cd, "%s:fail to get %s\n", __func__,
				    thp_power_id2name(power_id));
			return -ENODEV;
		}
		regulator_put(power->regulator);
		power->regulator = NULL;
		break;
	case THP_POWER_GPIO:
		gpio_direction_output(power->gpio, 0);
		gpio_free(power->gpio);
		break;
	case THP_POWER_PMIC:
		thp_log_err(cd, "%s: power supply %d\n", __func__, power->type);
		break;

	default:
		thp_log_err(cd, "%s: invalid power type %d\n", __func__,
			    power->type);
		break;
	}
	return 0;
}

int thp_power_supply_ctrl(struct thp_core_data *cd, enum thp_power_id power_id,
			  int status, unsigned int delay_ms)
{
	struct thp_power_supply *power = NULL;
	int rc = 0;

	if (is_invaild_power_id(power_id)) {
		thp_log_err(cd, "%s: invalid power id %d", __func__, power_id);
		return -EINVAL;
	}

	power = &cd->thp_powers[power_id];
	if (power->type == THP_POWER_UNUSED)
		goto exit;

	thp_log_info(cd, "%s:power %s %s\n", __func__,
		     thp_power_id2name(power_id), status ? "on" : "off");

	if (!power->use_count) {
		thp_log_err(cd, "%s:regulator %s not gotten yet\n", __func__,
			    thp_power_id2name(power_id));
		return -ENODEV;
	}
	switch (power->type) {
	case THP_POWER_LDO:
		if (IS_ERR_OR_NULL(power->regulator)) {
			thp_log_err(cd, "%s:fail to get %s\n", __func__,
				    thp_power_id2name(power_id));
			return -ENODEV;
		}
		if (status && power->ldo_transition_value != 0) {
			rc = regulator_set_voltage(power->regulator,
						   power->ldo_value,
						   power->ldo_value);
			if (rc)
				thp_log_err(cd, "%s: set %d err = %d", __func__,
					    power->ldo_value, rc);
		}
		rc = status ? regulator_enable(power->regulator) :
			      regulator_disable(power->regulator);
		if (!status && power->ldo_transition_value != 0) {
			rc = regulator_set_voltage(power->regulator,
						   power->ldo_transition_value,
						   power->ldo_transition_value);
			if (rc)
				thp_log_err(cd, "%s: set %d err = %d", __func__,
					    power->ldo_transition_value, rc);
		}
		break;
	case THP_POWER_GPIO:
		gpio_direction_output(power->gpio, status ? 1 : 0);
		break;

#if (!(IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK)))
	case THP_POWER_PMIC:
		tp_pmic_ldo_set.pmic_power_state = (status ? 1 : 0);
		rc = hw_pmic_power_cfg(TP_PMIC_REQ, &tp_pmic_ldo_set);
		if (rc)
			thp_log_err(cd, "%s:pmic %s failed, %d\n", __func__,
				    thp_power_id2name(power_id), rc);
		break;
#endif

	default:
		thp_log_err(cd, "%s: invalid power type %d\n", __func__,
			    power->type);
		break;
	}
exit:
	if (delay_ms)
		mdelay(delay_ms);
	return rc;
}

#define POWER_CONFIG_NAME_MAX 30
static void thp_paser_pmic_power(struct device_node *thp_node,
				 struct thp_core_data *cd, int power_id)
{
	const char *power_name = NULL;
	char config_name[POWER_CONFIG_NAME_MAX] = {0};
	struct thp_power_supply *power = NULL;
	int rc;

	power_name = thp_power_id2name(power_id);
	power = &cd->thp_powers[power_id];
	snprintf(config_name, (POWER_CONFIG_NAME_MAX - 1), "%s-value",
		 power_name);
	rc = of_property_read_u32(thp_node, config_name,
				  &power->pmic_power.value);
	if (rc)
		thp_log_err(cd, "%s:failed to get %s\n", __func__, config_name);
	snprintf(config_name, (POWER_CONFIG_NAME_MAX - 1), "%s-ldo-num",
		 power_name);
	rc = of_property_read_u32(thp_node, config_name,
				  &power->pmic_power.ldo_num);
	if (rc)
		thp_log_err(cd, "%s:failed to get %s\n", __func__, config_name);
	snprintf(config_name, (POWER_CONFIG_NAME_MAX - 1), "%s-pmic-num",
		 power_name);
	rc = of_property_read_u32(thp_node, config_name,
				  &power->pmic_power.pmic_num);
	if (rc)
		thp_log_err(cd, "%s:failed to get %s\n", __func__, config_name);
	thp_log_info(cd, "%s: to get %d, %d,%d\n", __func__,
		     power->pmic_power.ldo_num, power->pmic_power.pmic_num,
		     power->pmic_power.value);
}

static int thp_parse_one_power(struct device_node *thp_node,
			       struct thp_core_data *cd, int power_id)
{
	const char *power_name = NULL;
	char config_name[POWER_CONFIG_NAME_MAX] = {0};
	struct thp_power_supply *power = NULL;
	int rc;

	power_name = thp_power_id2name(power_id);
	power = &cd->thp_powers[power_id];

	rc = snprintf(config_name, POWER_CONFIG_NAME_MAX - 1, "%s-type",
		      power_name);

	thp_log_info(cd, "%s:parse power: %s\n", __func__, config_name);

	rc = of_property_read_u32(thp_node, config_name, &power->type);
	if (rc || power->type == THP_POWER_UNUSED) {
		thp_log_info(cd, "%s: power %s type not config or 0, unused\n",
			     __func__, config_name);
		return 0;
	}

	switch (power->type) {
	case THP_POWER_GPIO:
		snprintf(config_name, POWER_CONFIG_NAME_MAX - 1, "%s-gpio",
			 power_name);
		power->gpio = of_get_named_gpio(thp_node, config_name, 0);
		if (!gpio_is_valid(power->gpio)) {
			thp_log_err(cd, "%s:failed to get %s\n", __func__,
				    config_name);
			return -ENODEV;
		}
		break;
	case THP_POWER_LDO:
		snprintf(config_name, POWER_CONFIG_NAME_MAX - 1, "%s-value",
			 power_name);
		rc = of_property_read_u32(thp_node, config_name,
					  &power->ldo_value);
		if (rc) {
			thp_log_err(cd, "%s:failed to get %s\n", __func__,
				    config_name);
			return rc;
		}
		memset(config_name, 0, sizeof(config_name));
		snprintf(config_name, POWER_CONFIG_NAME_MAX - 1,
			 "%s-transition-value", power_name);
		rc = of_property_read_u32(thp_node, config_name,
					  &power->ldo_transition_value);
		if (rc) {
			thp_log_info(cd, "%s: not set %s", __func__,
				     config_name);
			power->ldo_transition_value = 0;
		}
		thp_log_info(cd, "%s: set %s ,%d", __func__, config_name,
			     power->ldo_transition_value);
		break;
	case THP_POWER_PMIC:
		thp_paser_pmic_power(thp_node, cd, power_id);
		break;
	default:
		thp_log_err(cd, "%s: invaild power type %d", __func__,
			    power->type);
		break;
	}

	return 0;
}

static int thp_parse_power_config(struct device_node *thp_node,
				  struct thp_core_data *cd)
{
	int rc;
	int i;

	for (i = 0; i < THP_POWER_ID_MAX; i++) {
		rc = thp_parse_one_power(thp_node, cd, i);
		if (rc)
			return rc;
	}

	return 0;
}

int is_valid_project_id(const char *id)
{
	while (*id != '\0') {
		if ((*id & BIT_MASK(PROJECTID_BIT_MASK_NUM)) || (!isalnum(*id)))
			return false;
		id++;
	}

	return true;
}

#define GET_HWLOCK_FAIL 0
int thp_bus_lock(struct thp_core_data *cd)
{
	int ret = 0;
	unsigned long time;
	unsigned long timeout;
	struct hwspinlock *hwlock = cd->hwspin_lock;

	mutex_lock(&cd->spi_mutex);
	if (!cd->use_hwlock)
		return 0;

	timeout = jiffies + msecs_to_jiffies(THP_GET_HARDWARE_TIMEOUT);

	do {
		ret = hwlock->bank->ops->trylock(hwlock);
		if (ret == GET_HWLOCK_FAIL) {
			time = jiffies;
			if (time_after(time, timeout)) {
				thp_log_err(cd,
					    "%s:get hardware_mutex timeout\n",
					    __func__);
				mutex_unlock(&cd->spi_mutex);
				return -ETIME;
			}
		}
	} while (ret == GET_HWLOCK_FAIL);
	return 0;
}

void thp_bus_unlock(struct thp_core_data *cd)
{
	struct hwspinlock *hwlock = cd->hwspin_lock;

	mutex_unlock(&cd->spi_mutex);
	if (cd->use_hwlock)
		hwlock->bank->ops->unlock(hwlock);
}

#define QUEUE_IS_FULL 1
#define QUEUE_IS_NOT_FULL 0
static struct thp_queue *thp_queue_init(struct thp_core_data *cd)
{
	int i;
	struct thp_queue *queue = NULL;

	thp_log_info(cd, "%s start\n", __func__);
	if (cd->thp_queue_buf_len > THP_MAX_FRAME_SIZE) {
		thp_log_err(cd, "%s thp queue node len is invalid\n", __func__);
		return NULL;
	}
	thp_log_info(cd, "%s:thp queue node len is %u\n", __func__,
		     cd->thp_queue_buf_len);
	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (queue == NULL) {
		thp_log_err(cd, "%s queue malloc failed\n", __func__);
		return NULL;
	}
	cd->queue_data_buf =
		vzalloc(cd->thp_queue_buf_len * THP_LIST_MAX_FRAMES);
	if (!cd->queue_data_buf) {
		thp_log_err(cd, "%s queue_data_buf vzalloc failed\n", __func__);
		kfree(queue);
		queue = NULL;
		return NULL;
	}

	for (i = 0; i < THP_LIST_MAX_FRAMES; i++)
		queue->frame_data[i].buf =
			cd->queue_data_buf + (i * cd->thp_queue_buf_len);

	queue->front = 0;
	queue->tail = 0;
	queue->size = THP_LIST_MAX_FRAMES;
	queue->flag = QUEUE_IS_NOT_FULL;
	return queue;
}

static bool thp_queue_is_empty(struct thp_core_data *cd,
			       struct thp_queue *queue)
{
	if (queue == NULL) {
		thp_log_err(cd, "%s queue is NULL\n", __func__);
		return false;
	}
	if ((queue->flag == QUEUE_IS_NOT_FULL) && (queue->front == queue->tail))
		return true;
	else
		return false;
}

static bool thp_queue_is_full(struct thp_core_data *cd, struct thp_queue *queue)
{
	if (queue == NULL) {
		thp_log_err(cd, "%s queue is NULL\n", __func__);
		return false;
	}
	if (queue->flag == QUEUE_IS_FULL)
		return true;
	else
		return false;
}

static bool thp_queue_dequeue(struct thp_core_data *cd, struct thp_queue *queue)
{
	if (queue == NULL) {
		thp_log_err(cd, "%s queue is NULL\n", __func__);
		return false;
	}
	thp_log_debug(cd, "%s in\n", __func__);

	if (thp_queue_is_empty(cd, queue)) {
		thp_log_err(cd, "%s queue is empty\n", __func__);
		return false;
	}
	memset(queue->frame_data[queue->front].buf, 0, cd->thp_queue_buf_len);
	memset(&queue->frame_data[queue->front].tv, 0,
	       sizeof(queue->frame_data[queue->front].tv));
	queue->front = ((queue->front + 1) < queue->size) ? (queue->front + 1) :
							    0;
	queue->flag = QUEUE_IS_NOT_FULL;
	return true;
}

static bool thp_queue_enqueue(struct thp_core_data *cd, struct thp_queue *queue,
			      u8 *read_buf, unsigned int len)
{
	if ((read_buf == NULL) || (queue == NULL)) {
		thp_log_err(cd, "%s read_buf is NULL\n", __func__);
		return false;
	}
	thp_log_debug(cd, "%s in\n", __func__);

	if (thp_queue_is_full(cd, queue)) {
		if (!thp_queue_dequeue(cd, queue)) {
			thp_log_err(cd, "%s dequeue failed\n", __func__);
			return false;
		}
	}
	if (len > cd->thp_queue_buf_len) {
		thp_log_debug(cd, "%s len is too big\n", __func__);
		len = cd->thp_queue_buf_len;
	}
	memcpy(queue->frame_data[queue->tail].buf, read_buf, len);
	get_timestamp(&queue->frame_data[queue->tail].tv);
	queue->tail = ((queue->tail + 1) < queue->size) ? (queue->tail + 1) : 0;
	if (queue->tail == queue->front)
		queue->flag = QUEUE_IS_FULL;
	return true;
}

static struct thp_queue_node *thp_queue_get_head(struct thp_core_data *cd,
						 struct thp_queue *queue)
{
	if (queue == NULL) {
		thp_log_err(cd, "%s queue is NULL\n", __func__);
		return NULL;
	}
	if (thp_queue_is_empty(cd, queue)) {
		thp_log_err(cd, "%s queue is empty\n", __func__);
		return NULL;
	}
	return &queue->frame_data[queue->front];
}

static void thp_queue_clear_all(struct thp_core_data *cd,
				struct thp_queue *queue)
{
	int i;

	if (queue == NULL) {
		thp_log_err(cd, "%s queue is NULL\n", __func__);
		return;
	}

	for (i = 0; i < THP_LIST_MAX_FRAMES; i++) {
		memset(queue->frame_data[i].buf, 0, cd->thp_queue_buf_len);
		memset(&queue->frame_data[i].tv, 0,
		       sizeof(queue->frame_data[i].tv));
	}

	queue->front = 0;
	queue->tail = 0;
	queue->size = THP_LIST_MAX_FRAMES;
	queue->flag = QUEUE_IS_NOT_FULL;
}

static void thp_queue_free(struct thp_core_data *cd, struct thp_queue *queue)
{
	if (queue == NULL) {
		thp_log_err(cd, "%s queue is NULL\n", __func__);
		return;
	}
	vfree(cd->queue_data_buf);
	cd->queue_data_buf = NULL;
	kfree(queue);
	queue = NULL;
}

int thp_set_spi_max_speed(struct thp_core_data *cd, unsigned int speed)
{
	int rc;

	cd->sdev->max_speed_hz = speed;

	thp_log_info(cd, "%s:set max_speed_hz %d\n", __func__, speed);
	rc = thp_bus_lock(cd);
	if (rc) {
		thp_log_err(cd, "%s: get lock failed\n", __func__);
		return rc;
	}
	if (thp_setup_spi(cd)) {
		thp_log_err(cd, "%s: set spi speed fail\n", __func__);
		rc = -EIO;
	}
	thp_bus_unlock(cd);
	return rc;
}

static int thp_wait_frame_waitq(struct thp_core_data *cd)
{
	int t;

	cd->frame_waitq_flag = WAITQ_WAIT;

	/* if not use timeout */
	if (!cd->timeout) {
		t = wait_event_interruptible(cd->frame_waitq,
					     (cd->frame_waitq_flag ==
					      WAITQ_WAKEUP));
		return 0;
	}

	/* if use timeout */
	t = wait_event_interruptible_timeout(
		cd->frame_waitq, (cd->frame_waitq_flag == WAITQ_WAKEUP),
		msecs_to_jiffies(cd->timeout));
	if (!is_tmo(t))
		return 0;

#if IS_ENABLED(CONFIG_DSM)
	thp_log_err(cd, "%s: wait frame timed out, dmd code:%d\n", __func__,
		    DSM_TPHOSTPROCESSING_DEV_STATUS_ERROR_NO);
	if (thp_get_status(cd, THP_STATUS_TUI) == 0)
		thp_dmd_report(cd, DSM_TPHOSTPROCESSING_DEV_STATUS_ERROR_NO,
			       "%s, wait frame timed out\n", __func__);
#endif

	return -ETIMEDOUT;
}

int thp_set_status(struct thp_core_data *cd, int type, int status)
{
	mutex_lock(&cd->status_mutex);
	status ? __set_bit(type, (unsigned long *)&cd->status) :
		 __clear_bit(type, (unsigned long *)&cd->status);
	mutex_unlock(&cd->status_mutex);

	thp_mt_wrapper_wakeup_poll(cd);

	thp_log_info(cd, "%s:type=%d value=%d\n", __func__, type, status);
	return 0;
}

int thp_get_status(struct thp_core_data *cd, int type)
{
	return test_bit(type, (unsigned long *)&cd->status);
}

u32 thp_get_status_all(struct thp_core_data *cd)
{
	return cd->status;
}

void thp_clear_frame_buffer(struct thp_core_data *cd)
{
	struct thp_frame *temp = NULL;
	struct list_head *pos = NULL;
	struct list_head *n = NULL;

	if (cd->use_thp_queue) {
		thp_queue_clear_all(cd, cd->thp_queue);
	} else {
		if (list_empty(&cd->frame_list.list))
			return;
		list_for_each_safe(pos, n, &cd->frame_list.list) {
			temp = list_entry(pos, struct thp_frame, list);
			list_del(pos);
			kfree(temp);
			temp = NULL;
		}
	}
	cd->frame_count = 0;
}

static int thp_spi_transfer(struct thp_core_data *cd, char *tx_buf,
			    char *rx_buf, unsigned int len,
			    unsigned int lock_status)
{
	int rc;
	struct spi_message msg;
	struct spi_device *sdev = cd->sdev;
	struct spi_transfer xfer = {
		.tx_buf = tx_buf,
		.rx_buf = rx_buf,
		.len = len,
#if (!IS_ENABLED(CONFIG_TP_QCOM_8550))
		.delay_usecs = cd->thp_dev->timing_config.spi_transfer_delay_us,
#endif
	};

	if ((cd->pre_suspended || cd->suspended) &&
	    (!cd->need_work_in_suspend)) {
		thp_log_err(cd, "%s - suspended\n", __func__);
		return 0;
	}

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	if (lock_status == NEED_LOCK) {
		rc = thp_bus_lock(cd);
		if (rc < 0) {
			thp_log_err(cd, "%s:get lock failed:%d\n", __func__,
				    rc);
			return rc;
		}
	}
	thp_spi_cs_set(cd, GPIO_HIGH);
	rc = thp_spi_sync(sdev, &msg);
	if (lock_status == NEED_LOCK)
		thp_bus_unlock(cd);

	return rc;
}

void thp_spi_cmd_lock(struct thp_core_data *cd, int timeout)
{
	unsigned long flags;
	bool get_lock = false;
	bool is_timeout = false;
	int wait_jiffies = msecs_to_jiffies(timeout);
	int rc;
	int ret = NO_ERR;

	if (!cd) {
		thp_log_err(cd, "%s: cd is nullptr\n", __func__);
		return;
	}

	do {
		rc = wait_event_timeout(cd->cmd_lock.cmd_wait_queue,
					(cd->cmd_lock.locked == false),
					wait_jiffies);
		if (!rc) {
			thp_log_err(cd, "%s: wait timeout\n", __func__);
			is_timeout = true;
			ret = -ETIMEDOUT;
		} else {
			spin_lock_irqsave(&cd->cmd_lock.spin_lock, flags);
			if (!cd->cmd_lock.locked) {
				cd->cmd_lock.locked = true;
				get_lock = true;
				ret = rc;
			}
			spin_unlock_irqrestore(&cd->cmd_lock.spin_lock, flags);
			if (!get_lock)
				wait_jiffies = rc;
		}
	} while (!(get_lock || is_timeout));
	thp_log_debug(cd, "%s: get_lock: %d. is_timeout: %d\n", __func__,
		      get_lock, is_timeout);

	return;
}

void thp_spi_cmd_unlock(struct thp_core_data *cd)
{
	if (!cd) {
		thp_log_err(cd, "%s: cd is nullptr\n", __func__);
		return;
	}

	thp_log_debug(cd, "%s: called\n", __func__);
	cd->cmd_lock.locked = false;
	wake_up(&(cd->cmd_lock.cmd_wait_queue));
	return;
}

static void thp_spi_cmd_lock_init(struct thp_core_data *cd)
{
	if (!cd) {
		thp_log_err(cd, "%s: cd is nullptr\n", __func__);
		return;
	}

	spin_lock_init(&cd->cmd_lock.spin_lock);
	cd->cmd_lock.locked = false;
	cd->cmd_lock.locked_by_afe = false;
	init_waitqueue_head(&cd->cmd_lock.cmd_wait_queue);
}

/*
 * If irq is disabled/enabled, can not disable/enable again
 * disable - status 0; enable - status not 0
 */
void thp_set_irq_status(struct thp_core_data *cd, int status)
{
	if (!cd) {
		thp_log_err(cd, "%s: cd is nullptr\n", __func__);
		return;
	}

	mutex_lock(&cd->irq_mutex);
	if (cd->irq_enabled != (!!status)) {
		status ? enable_irq(cd->irq) : disable_irq(cd->irq);
		cd->irq_enabled = !!status;
		thp_log_info(cd, "%s: %s irq\n", __func__,
			     status ? "enable" : "disable");
	}
	mutex_unlock(&cd->irq_mutex);
};

void thp_set_irq_wake_status(struct thp_core_data *cd, int status)
{
	if (!cd) {
		thp_log_err(cd, "%s: cd is nullptr\n", __func__);
		return;
	}

	mutex_lock(&cd->irq_mutex);
	if (cd->irq_wake_enabled != (!!status)) {
		status ? enable_irq_wake(cd->irq) : disable_irq_wake(cd->irq);
		cd->irq_wake_enabled = !!status;
		thp_log_info(cd, "%s: %s irq_wake\n", __func__,
			     status ? "enable" : "disable");
	}
	mutex_unlock(&cd->irq_mutex);
}

#ifdef CONFIG_HONOR_SHB_THP
#define POWER_OFF 0
#define POWER_ON 1
static int send_thp_cmd_to_shb(unsigned char power_status)
{
	int ret = -EINVAL;
	struct thp_core_data *cd = thp_get_core_data();

	if (cd == NULL) {
		thp_log_err(cd, "%s: have null ptr\n", __func__);
		return ret;
	}
	if (cd->support_shb_thp == 0) {
		thp_log_info(cd, "%s: not support shb\n", __func__);
		return ret;
	}
	switch (power_status) {
	case POWER_OFF:
		if (!cd->poweroff_function_status) {
			ret = send_thp_close_cmd();
			thp_log_info(cd, "%s: close shb thp, ret is %d\n",
				     __func__, ret);
		} else {
			ret = send_thp_driver_status_cmd(
				power_status, cd->poweroff_function_status,
				ST_CMD_TYPE_SET_SCREEN_STATUS);
			thp_log_info(
				cd,
				"%s: call poweroff_function_status = 0x%x\n",
				__func__, cd->poweroff_function_status);
		}
		break;
	case POWER_ON:
		if (!cd->poweroff_function_status) {
			ret = send_thp_open_cmd();
			thp_log_info(cd, "%s: open shb thp, ret is %d\n",
				     __func__, ret);
		}
		ret = send_thp_driver_status_cmd(power_status,
						 POWER_KEY_ON_CTRL,
						 ST_CMD_TYPE_SET_SCREEN_STATUS);
		thp_log_info(cd, "%s: power on and send status\n", __func__);
		break;
	default:
		thp_log_err(cd, "%s: invaild power_status: %u\n", __func__,
			    power_status);
	}
	if (ret)
		thp_log_err(cd, "%s: ret = %d\n", __func__, ret);
	return ret;
}
#endif

static int thp_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct thp_core_data *cd = misc_dev_get_core_data(filp->private_data);

	thp_log_info(cd, "%s: called\n", __func__);

	if (cd == NULL) {
		thp_log_err(cd, "%s: misc_dev_get_core_data is NULL\n",
			    __func__);
		return -EINVAL;
	}

	mutex_lock(&cd->thp_mutex);
	if (cd->open_count) {
		thp_log_err(cd, "%s: dev have be opened\n", __func__);
		mutex_unlock(&cd->thp_mutex);
		return -EBUSY;
	}

	cd->open_count++;
	mutex_unlock(&cd->thp_mutex);
	cd->reset_flag = 0; /* current isn't in reset status */
	cd->get_frame_block_flag = THP_GET_FRAME_BLOCK;
	cd->get_frame_timeout_retry_count = 0;

	cd->frame_size = THP_MAX_FRAME_SIZE;
#ifdef THP_NOVA_ONLY
	cd->frame_size = NT_MAX_FRAME_SIZE;
#endif
	cd->timeout = THP_DEFATULT_TIMEOUT_MS;

	/*
	 * Daemon default is 0
	 * setting to 1 will trigger daemon to init or restore the status
	 */
	__set_bit(THP_STATUS_WINDOW_UPDATE, (unsigned long *)&cd->status);
	__set_bit(THP_STATUS_TOUCH_SCENE, (unsigned long *)&cd->status);

	thp_log_info(cd, "%s: cd->status = 0x%x\n", __func__, cd->status);
	mutex_lock(&cd->mutex_frame);
	thp_clear_frame_buffer(cd);
	mutex_unlock(&cd->mutex_frame);
	/* restore spi config */
	ret = thp_set_spi_max_speed(cd, cd->spi_config.max_speed_hz);
	if (ret)
		thp_log_err(cd, "%s: thp_set_spi_max_speed error\n", __func__);

	return 0;
}

static int thp_release(struct inode *inode, struct file *filp)
{
	struct thp_core_data *cd = misc_dev_get_core_data(filp->private_data);

	thp_log_info(cd, "%s: called\n", __func__);

	if (cd == NULL) {
		thp_log_err(cd, "%s: misc_dev_get_core_data is NULL\n",
			    __func__);
		return -EINVAL;
	}

	mutex_lock(&cd->thp_mutex);
	cd->open_count--;
	if (cd->open_count < 0) {
		thp_log_err(cd, "%s: abnormal release\n", __func__);
		cd->open_count = 0;
	}
	mutex_unlock(&cd->thp_mutex);

	thp_wake_up_frame_waitq(cd);
	if (cd->cmd_lock.locked_by_afe) {
		cd->cmd_lock.locked_by_afe = false;
		thp_spi_cmd_unlock(cd);
	}

	thp_set_irq_status(cd, THP_IRQ_DISABLE);

	if (cd->locked_by_daemon) {
		thp_log_info(cd, "%s: need unlock here\n", __func__);
		thp_bus_unlock(cd);
		cd->locked_by_daemon = false;
	}

	return 0;
}

static int thp_spi_sync_alloc_mem(struct thp_core_data *cd)
{
	if (cd->spi_sync_rx_buf || cd->spi_sync_tx_buf) {
		thp_log_debug(cd, "%s: has requested memory\n", __func__);
		return 0;
	}

	cd->spi_sync_rx_buf = kzalloc(THP_SYNC_DATA_MAX, GFP_KERNEL);
	if (!cd->spi_sync_rx_buf) {
		thp_log_err(cd, "%s:spi_sync_rx_buf request memory fail\n",
			    __func__);
		goto exit;
	}
	cd->spi_sync_tx_buf = kzalloc(THP_SYNC_DATA_MAX, GFP_KERNEL);
	if (!cd->spi_sync_tx_buf) {
		thp_log_err(cd, "%s:spi_sync_tx_buf request memory fail\n",
			    __func__);
		kfree(cd->spi_sync_rx_buf);
		cd->spi_sync_rx_buf = NULL;
		goto exit;
	}
	return 0;

exit:
	return -ENOMEM;
}

static unsigned int
thp_get_spi_msg_lens(struct thp_core_data *cd,
		     struct thp_ioctl_spi_xfer_data *spi_data,
		     unsigned int xfer_num)
{
	int length = 0;
	int i;

	if (!spi_data || !xfer_num || (xfer_num > MAX_SPI_XFER_DATA_NUM)) {
		thp_log_err(cd, "%s:invalid input\n", __func__);
		return 0;
	}
	for (i = 0; i < xfer_num; i++) {
		if ((spi_data[i].len == 0) ||
		    (spi_data[i].len > THP_MAX_FRAME_SIZE)) {
			thp_log_err(cd, "%s:spi_data[i].len invalid\n",
				    __func__);
			return 0;
		}
		length += spi_data[i].len;
		thp_log_debug(cd, "%s: spi_data[i].len = %d\n", __func__,
			      spi_data[i].len);
	}
	return length;
}

static long thp_ioctl_multiple_spi_xfer_sync(struct thp_core_data *cd,
					     const void __user *data,
					     unsigned int lock_status)
{
	struct thp_ioctl_spi_msg_package ioctl_spi_msg;
	struct thp_ioctl_spi_xfer_data *ioctl_spi_xfer = NULL;
	struct spi_message msg;
	struct spi_transfer *xfer = NULL;
	int rc;
	int i;
	unsigned int current_speed = 0;
	u8 *tx_buf = NULL;
	u8 *rx_buf = NULL;
	unsigned int msg_len;

	memset(&ioctl_spi_msg, 0, sizeof(ioctl_spi_msg));
	if (cd->pre_suspended || cd->suspended || (!data)) {
		thp_log_info(cd, "%s:suspended or invalid data,return\n",
			     __func__);
		return -EIO;
	}
#if defined(CONFIG_TEE_TUI)
	if (tui_enable) {
		thp_log_info(cd, "%s:TUI status,return!\n", __func__);
		return -EIO;
	}
#endif

	if (copy_from_user(&ioctl_spi_msg, data, sizeof(ioctl_spi_msg))) {
		thp_log_err(cd, "%s:Failed to copy spi data\n", __func__);
		return -EFAULT;
	}
	if ((ioctl_spi_msg.xfer_num > MAX_SPI_XFER_DATA_NUM) ||
	    !ioctl_spi_msg.xfer_num) {
		thp_log_err(cd, "xfer_num:%d\n", ioctl_spi_msg.xfer_num);
		return -EINVAL;
	}

	ioctl_spi_xfer = kcalloc(ioctl_spi_msg.xfer_num,
				 sizeof(*ioctl_spi_xfer), GFP_KERNEL);
	if (!ioctl_spi_xfer) {
		thp_log_err(cd, "%s:failed alloc memory for spi_xfer_data\n",
			    __func__);
		return -EFAULT;
	}
	if (ioctl_spi_msg.xfer_data) {
		if (copy_from_user(ioctl_spi_xfer, ioctl_spi_msg.xfer_data,
				   sizeof(*ioctl_spi_xfer) *
					   ioctl_spi_msg.xfer_num)) {
			thp_log_err(cd, "%s:failed copy xfer_data\n", __func__);
			rc = -EINVAL;
			goto exit;
		}
	}
	msg_len = thp_get_spi_msg_lens(cd, ioctl_spi_xfer,
				       ioctl_spi_msg.xfer_num);
	if (msg_len > THP_MAX_FRAME_SIZE || !msg_len) {
		thp_log_err(cd, "%s:invalid msg len :%d\n", __func__, msg_len);
		rc = -EINVAL;
		goto exit;
	}

	xfer = kcalloc(ioctl_spi_msg.xfer_num, sizeof(*xfer), GFP_KERNEL);
	rx_buf = kzalloc(msg_len, GFP_KERNEL);
	tx_buf = kzalloc(msg_len, GFP_KERNEL);
	if (!rx_buf || !tx_buf || !xfer) {
		thp_log_err(cd, "%s:failed alloc buffer\n", __func__);
		rc = -EINVAL;
		goto exit;
	}

	spi_message_init(&msg);
	for (i = 0, msg_len = 0; i < ioctl_spi_msg.xfer_num; i++) {
		if (ioctl_spi_xfer[i].tx) {
			rc = copy_from_user(tx_buf + msg_len,
					    ioctl_spi_xfer[i].tx,
					    ioctl_spi_xfer[i].len);
			if (rc) {
				thp_log_err(cd, "%s:failed copy tx_buf:%d\n",
					    __func__, rc);
				goto exit;
			}
		}
		xfer[i].tx_buf = tx_buf + msg_len;
		xfer[i].rx_buf = rx_buf + msg_len;
		xfer[i].len = ioctl_spi_xfer[i].len;
		xfer[i].cs_change = !!ioctl_spi_xfer[i].cs_change;
#if (!IS_ENABLED(CONFIG_TP_QCOM_8550))
		xfer[i].delay_usecs = ioctl_spi_xfer[i].delay_usecs;
#endif

		thp_log_debug(cd,
			      "%s:%d, cs_change=%d,len =%d,delay_usecs=%d\n",
			      __func__, i, ioctl_spi_xfer[i].cs_change,
			      ioctl_spi_xfer[i].len,
			      ioctl_spi_xfer[i].delay_usecs);
		spi_message_add_tail(&xfer[i], &msg);
		msg_len += ioctl_spi_xfer[i].len;
	}
	if (lock_status == NEED_LOCK) {
		rc = thp_bus_lock(cd);
		if (rc) {
			thp_log_info(cd, "%s:failed get lock:%d", __func__, rc);
			goto exit;
		}
	}

	if (ioctl_spi_msg.speed_hz != 0) {
		thp_log_debug(cd, "%s change to 3.5k-> speed =%d\n", __func__,
			      ioctl_spi_msg.speed_hz);
		current_speed = cd->sdev->max_speed_hz;
		cd->sdev->max_speed_hz = ioctl_spi_msg.speed_hz;
		rc = thp_setup_spi(cd);
		if (rc) {
			thp_log_err(cd, "%s:set max speed failed rc:%d",
				    __func__, rc);
			if (lock_status == NEED_LOCK)
				thp_bus_unlock(cd);
			goto exit;
		}
	}

	rc = thp_spi_sync(cd->sdev, &msg);
	if (rc) {
		thp_log_err(cd, "%s:failed sync msg:%d", __func__, rc);
		if (lock_status == NEED_LOCK)
			thp_bus_unlock(cd);
		goto exit;
	}
	if (ioctl_spi_msg.speed_hz != 0) {
		thp_log_debug(cd, "%s current_speed-> %d\n", __func__,
			      current_speed);
		cd->sdev->max_speed_hz = current_speed;
		rc = thp_setup_spi(cd);
		if (rc) {
			thp_log_err(cd, "%s:set max speed failed rc:%d",
				    __func__, rc);
			if (lock_status == NEED_LOCK)
				thp_bus_unlock(cd);
			goto exit;
		}
	}
	if (lock_status == NEED_LOCK)
		thp_bus_unlock(cd);

	for (i = 0, msg_len = 0; i < ioctl_spi_msg.xfer_num; i++) {
		if (ioctl_spi_xfer[i].rx) {
			rc = copy_to_user(ioctl_spi_xfer[i].rx,
					  rx_buf + msg_len,
					  ioctl_spi_xfer[i].len);
			if (rc) {
				thp_log_err(cd, "%s:copy to rx buff fail:%d",
					    __func__, rc);
				goto exit;
			}
		}
		msg_len += ioctl_spi_xfer[i].len;
	}
exit:
	kfree(ioctl_spi_xfer);
	kfree(xfer);
	kfree(rx_buf);
	kfree(tx_buf);
	return rc;
}

static long thp_ioctl_spi_sync(struct thp_core_data *cd,
			       const void __user *data,
			       unsigned int lock_status)
{
	struct thp_ioctl_spi_sync_data sync_data;
	struct thp_ioctl_spi_sync_data_compat sync_data_compat;
	int rc = 0;
	u8 *tx_buf = NULL;
	u8 *rx_buf = NULL;

	thp_log_debug(cd, "%s: called\n", __func__);
	memset(&sync_data, 0, sizeof(sync_data));
	memset(&sync_data_compat, 0, sizeof(sync_data_compat));

	if ((cd->pre_suspended || cd->suspended) &&
	    (!cd->need_work_in_suspend)) {
		thp_log_info(cd, "%s suspended return!\n", __func__);
		return 0;
	}

	if (!data) {
		thp_log_err(cd, "%s: input parameter null\n", __func__);
		return -EINVAL;
	}

	if (!thp_vm_afe_allowed(cd)) {
		thp_log_err(
			cd,
			"%s: tui mode don't allow to send cmd to tp ic again.\n",
			__func__);
		return 0;
	}

#if defined(CONFIG_TEE_TUI)
	if (tui_enable)
		return 0;
#endif

	if (cd->compat_flag == true) {
		if (copy_from_user(&sync_data_compat, data,
				   sizeof(sync_data_compat))) {
			thp_log_err(cd, "Failed to copy_from_user\n");
			return -EFAULT;
		}
		sync_data.rx = compat_ptr(sync_data_compat.rx);
		sync_data.tx = compat_ptr(sync_data_compat.tx);
		sync_data.size = sync_data_compat.size;
	} else {
		if (copy_from_user(&sync_data, data, sizeof(sync_data))) {
			thp_log_err(cd, "Failed to copy_from_user\n");
			return -EFAULT;
		}
	}

	if (sync_data.size > THP_SYNC_DATA_MAX) {
		thp_log_err(cd, "sync_data.size out of range\n");
		return -EINVAL;
	}

	if (cd->need_huge_memory_in_spi) {
		rc = thp_spi_sync_alloc_mem(cd);
		if (!rc) {
			rx_buf = cd->spi_sync_rx_buf;
			tx_buf = cd->spi_sync_tx_buf;
		} else {
			thp_log_err(cd, "%s:buf request memory fail\n",
				    __func__);
			goto exit;
		}
	} else {
		rx_buf = kzalloc(sync_data.size, GFP_KERNEL);
		tx_buf = kzalloc(sync_data.size, GFP_KERNEL);
		if (!rx_buf || !tx_buf) {
			thp_log_err(
				cd,
				"%s:buf request memory fail,sync_data.size = %d\n",
				__func__, sync_data.size);
			goto exit;
		}
	}
	rc = copy_from_user(tx_buf, sync_data.tx, sync_data.size);
	if (rc) {
		thp_log_err(cd, "%s:copy in buff fail\n", __func__);
		goto exit;
	}
	if (lock_status == NEED_LOCK) {
		if (cd->thp_dev->ops->spi_transfer)
			rc = cd->thp_dev->ops->spi_transfer(tx_buf, rx_buf,
							    sync_data.size);
		else
			rc = thp_spi_transfer(cd, tx_buf, rx_buf,
					      sync_data.size, lock_status);
	} else {
		rc = thp_spi_transfer(cd, tx_buf, rx_buf, sync_data.size,
				      lock_status);
	}
	if (rc) {
		thp_log_err(cd, "%s: transfer error, ret = %d\n", __func__, rc);
		goto exit;
	}

	if (sync_data.rx) {
		rc = copy_to_user(sync_data.rx, rx_buf, sync_data.size);
		if (rc) {
			thp_log_err(cd, "%s:copy out buff fail\n", __func__);
			goto exit;
		}
	}

exit:
	if (!cd->need_huge_memory_in_spi) {
		kfree(rx_buf);
		rx_buf = NULL;

		kfree(tx_buf);
		tx_buf = NULL;
	}
	return rc;
}

static long thp_ioctl_finish_notify(struct thp_core_data *cd, unsigned long arg)
{
	unsigned long event_type = arg;
	int rc;
	struct thp_device_ops *ops = cd->thp_dev->ops;

	thp_log_info(cd, "%s: called\n", __func__);
	switch (event_type) {
	case THP_AFE_NOTIFY_FW_UPDATE:
		rc = ops->afe_notify ?
			     ops->afe_notify(cd->thp_dev, event_type) :
			     0;
		break;
	default:
		thp_log_err(cd, "%s: illegal event type\n", __func__);
		rc = -EINVAL;
	}
	return rc;
}

static long thp_ioctl_get_frame_count(struct thp_core_data *cd,
				      unsigned long arg)
{
	u32 __user *frame_cnt = (u32 *)arg;

	//	if (cd->frame_count)
	//		thp_log_info(cd, "%s:frame_cnt=%d\n", __func__, cd->frame_count);

	if (frame_cnt == NULL) {
		thp_log_err(cd, "%s: input parameter null\n", __func__);
		return -EINVAL;
	}

	if (copy_to_user(frame_cnt, &cd->frame_count, sizeof(u32))) {
		thp_log_err(cd, "%s:copy frame_cnt failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static long thp_ioctl_clear_frame_buffer(struct thp_core_data *cd)
{
	if (cd->frame_count == 0)
		return 0;

	thp_log_info(cd, "%s called\n", __func__);
	mutex_lock(&cd->mutex_frame);
	thp_clear_frame_buffer(cd);
	mutex_unlock(&cd->mutex_frame);
	return 0;
}

static long thp_get_frame_from_thp_queue(struct thp_core_data *cd,
					 struct thp_ioctl_get_frame_data *data)
{
	struct thp_queue_node *temp_frame = NULL;
	long rc = 0;

	if (data->size > cd->thp_queue_buf_len) {
		thp_log_debug(cd, "%s data size too big, set to %u\n", __func__,
			      cd->thp_queue_buf_len);
		data->size = cd->thp_queue_buf_len;
	}
	cd->frame_size = data->size;
	mutex_lock(&cd->suspend_flag_mutex);
	if ((cd->pre_suspended || cd->suspended) &&
	    (!cd->need_work_in_suspend)) {
		thp_log_info(cd, "%s: drv suspended\n", __func__);
		mutex_unlock(&cd->suspend_flag_mutex);
		return -EINVAL;
	}
	thp_set_irq_status(cd, THP_IRQ_ENABLE);
	mutex_unlock(&cd->suspend_flag_mutex);
	if (thp_queue_is_empty(cd, cd->thp_queue) && cd->get_frame_block_flag) {
		if (thp_wait_frame_waitq(cd))
			rc = -ETIMEDOUT;
	}
	mutex_lock(&cd->mutex_frame);
	if (thp_queue_is_empty(cd, cd->thp_queue) == false) {
		temp_frame = thp_queue_get_head(cd, cd->thp_queue);
		if (temp_frame == NULL) {
			thp_log_err(cd, "Failed to temp_frame is NULL\n");
			rc = -EFAULT;
			goto out;
		}
		if (data->buf == NULL) {
			thp_log_err(cd, "Failed to data buf is NULL\n");
			rc = -EFAULT;
			goto out;
		}
		if (copy_to_user(data->buf, temp_frame->buf, cd->frame_size)) {
			thp_log_err(cd, "Failed to copy_to_user\n");
			rc = -EFAULT;
			goto out;
		}
		if (data->tv == NULL) {
			thp_log_err(cd, "Failed to data tv is NULL\n");
			rc = -EFAULT;
			goto out;
		}
		if (copy_to_user(data->tv, &(temp_frame->tv),
				 sizeof(temp_frame->tv))) {
			thp_log_err(cd, "Failed to copy_to_user tv\n");
			rc = -EFAULT;
			goto out;
		}
		if (!thp_queue_dequeue(cd, cd->thp_queue))
			thp_log_err(cd, "%s queue remove failed\n", __func__);
		temp_frame = NULL;
		cd->frame_count--;
		rc = 0;
	} else {
		thp_log_err(cd, "%s:no frame\n", __func__);
		/*
		 * When wait timeout, try to get data.
		 * If timeout and no data, return -ETIMEDOUT
		 */
		if (rc != -ETIMEDOUT)
			rc = -ENODATA;
	}
out:
	mutex_unlock(&cd->mutex_frame);
	trace_touch(TOUCH_TRACE_GET_FRAME, TOUCH_TRACE_FUNC_OUT,
		    rc ? "no frame" : "with frame");
	return rc;
}

static void thp_force_get_frame(struct thp_core_data *cd)
{
	int rc;
	uint8_t *read_buf = NULL;

	thp_log_info(cd, "%s: called\n", __func__);
	read_buf = kzalloc(cd->frame_size, GFP_KERNEL);
	if (!read_buf) {
		thp_log_err(cd, "%s: fail to alloc mem\n", __func__);
		return;
	}

	rc = cd->thp_dev->ops->get_frame(cd->thp_dev, read_buf, cd->frame_size);
	if (rc) {
		thp_log_err(cd, "%s: failed to read frame %d\n", __func__, rc);
		kfree(read_buf);
		return;
	}

	thp_copy_frame(cd);
	thp_wake_up_frame_waitq(cd);
	kfree(read_buf);
}

static long thp_ioctl_get_frame(struct thp_core_data *cd, unsigned long arg,
				unsigned int f_flag)
{
	struct thp_ioctl_get_frame_data data;
	struct thp_ioctl_get_frame_data_compat data_compat;
	void __user *argp = (void __user *)arg;
	long rc = 0;

	memset(&data, 0, sizeof(data));
	memset(&data_compat, 0, sizeof(data_compat));
	trace_touch(TOUCH_TRACE_GET_FRAME, TOUCH_TRACE_FUNC_IN, "thp");

	if (!arg) {
		thp_log_err(cd, "%s: input parameter null\n", __func__);
		return -EINVAL;
	}

	if (!thp_vm_afe_allowed(cd)) {
		thp_log_err(
			cd,
			"%s: tui mode don't allow to send cmd to tp ic again.\n",
			__func__);
		return 0;
	}

	if ((cd->pre_suspended || cd->suspended) &&
	    (!cd->need_work_in_suspend)) {
		thp_log_debug(cd, "%s: drv suspended\n", __func__);
		return -ECONNREFUSED;
	}

	if (cd->compat_flag == true) {
		if (copy_from_user(&data_compat, argp, sizeof(data_compat))) {
			thp_log_err(cd, "Failed to copy_from_user\n");
			return -EFAULT;
		}
		data.buf = compat_ptr(data_compat.buf);
		data.tv = compat_ptr(data_compat.tv);
		data.size = data_compat.size;
	} else {
		if (copy_from_user(&data, argp, sizeof(data))) {
			thp_log_err(cd, "Failed to copy_from_user\n");
			return -EFAULT;
		}
	}

	if (data.buf == 0 || data.size == 0 || data.size > THP_MAX_FRAME_SIZE ||
	    data.tv == 0) {
		thp_log_err(cd, "%s:input buf invalid\n", __func__);
		return -EINVAL;
	}
	if (cd->use_thp_queue)
		return thp_get_frame_from_thp_queue(cd, &data);

	cd->frame_size = data.size;
	mutex_lock(&cd->suspend_flag_mutex);
	if ((cd->pre_suspended || cd->suspended) &&
	    (!cd->need_work_in_suspend)) {
		thp_log_info(cd, "%s: drv suspended\n", __func__);
		mutex_unlock(&cd->suspend_flag_mutex);
		return -EINVAL;
	}
	thp_set_irq_status(cd, THP_IRQ_ENABLE);
	mutex_unlock(&cd->suspend_flag_mutex);
	if (list_empty(&cd->frame_list.list) && cd->get_frame_block_flag) {
		if (thp_wait_frame_waitq(cd)) {
			thp_log_err(cd, "%s: timeout\n", __func__);
			if (cd->get_frame_timeout_retry_count <
			    GET_FRAME_RETRY_COUNT_MAX) {
				thp_force_get_frame(cd);
				cd->get_frame_timeout_retry_count++;
			}
			rc = -ETIMEDOUT;
		}
	}

	mutex_lock(&cd->mutex_frame);
	if (!list_empty(&cd->frame_list.list)) {
		struct thp_frame *temp;

		if (rc != -ETIMEDOUT)
			cd->get_frame_timeout_retry_count = 0;
		temp = list_first_entry(&cd->frame_list.list, struct thp_frame,
					list);
		if (data.buf == NULL) {
			thp_log_err(cd, "Failed to copy_to_user()\n");
			rc = -EFAULT;
			goto out;
		}
		if (copy_to_user(data.buf, temp->frame, cd->frame_size)) {
			thp_log_err(cd, "Failed to copy_to_user()\n");
			rc = -EFAULT;
			goto out;
		}

		if (data.tv == NULL) {
			thp_log_err(cd, "Failed to copy_to_user()\n");
			rc = -EFAULT;
			goto out;
		}
		if (copy_to_user(data.tv, &(temp->tv),
				 sizeof(struct timeval))) {
			thp_log_err(cd, "Failed to copy_to_user()\n");
			rc = -EFAULT;
			goto out;
		}

		list_del(&temp->list);
		kfree(temp);
		cd->frame_count--;
		rc = 0;
	} else {
		thp_log_err(cd, "%s:no frame\n", __func__);
		/*
		 * When wait timeout, try to get data.
		 * If timeout and no data, return -ETIMEDOUT
		 */
		if (rc != -ETIMEDOUT)
			rc = -ENODATA;
	}
out:
	mutex_unlock(&cd->mutex_frame);
	trace_touch(TOUCH_TRACE_GET_FRAME, TOUCH_TRACE_FUNC_OUT,
		    rc ? "no frame" : "with frame");
	return rc;
}

static long thp_ioctl_reset(struct thp_core_data *cd, unsigned long reset)
{
	int iovdd_voltage;
	int iovdd_enable;
	int vcc_voltage;
	int vcc_enable;
	struct thp_power_supply *power = NULL;

	thp_log_info(cd, "%s:set reset status %lu\n", __func__, reset);

	gpio_set_value(cd->gpios.rst_gpio, !!reset);

	if ((!!reset) && (cd->send_bt_status_to_fw) &&
	    (cd->support_dual_chip_arch) && (!cd->enter_stylus3_mmi_test)) {
		if (cd->thp_dev->ops->bt_handler(cd->thp_dev, true))
			thp_log_err(cd, "ioctl reset send bt status fail\n");
	}
	cd->frame_waitq_flag = WAITQ_WAIT;
	cd->reset_flag = !reset;
	power = &cd->thp_powers[THP_IOVDD];
	if (power->regulator) {
		iovdd_enable = regulator_is_enabled(power->regulator);
		iovdd_voltage = regulator_get_voltage(power->regulator);
		thp_log_info(cd, "%s:iovdd_enable = %d, iovdd_voltage = %d\n",
			     __func__, iovdd_enable, iovdd_voltage);
	}
	power = &cd->thp_powers[THP_VCC];
	if (power->regulator) {
		vcc_enable = regulator_is_enabled(power->regulator);
		vcc_voltage = regulator_get_voltage(power->regulator);
		thp_log_info(cd, "%s:vcc_enable = %d, vcc_voltage = %d\n",
			     __func__, vcc_enable, vcc_voltage);
	}
	return 0;
}

static long thp_ioctl_hw_lock_status(struct thp_core_data *cd,
				     unsigned long arg)
{
	thp_log_info(cd, "%s: set hw lock status %lu\n", __func__, arg);
	if (arg) {
		if (thp_bus_lock(cd)) {
			thp_log_err(cd, "%s: get lock failed\n", __func__);
			return -ETIME;
		}
		cd->locked_by_daemon = true;
	} else {
		thp_bus_unlock(cd);
		cd->locked_by_daemon = false;
	}
	return 0;
}

static long thp_ioctl_set_timeout(struct thp_core_data *cd, unsigned long arg)
{
	unsigned int timeout_ms = min_t(unsigned int, arg, THP_WAIT_MAX_TIME);

	if (thp_get_status(cd, THP_STATUS_TUI) == 1) {
		thp_log_info(
			cd,
			"set wait time %d ms.(current %dms), do nothing in tui mode\n",
			timeout_ms, cd->timeout);
		return 0;
	} else {
		thp_log_info(cd, "set wait time %d ms.(current %dms)\n",
			     timeout_ms, cd->timeout);
	}

	if (timeout_ms != cd->timeout) {
		cd->timeout = timeout_ms;
		thp_wake_up_frame_waitq(cd);
	}

	return 0;
}

static long thp_ioctl_set_block(struct thp_core_data *cd, unsigned long arg)
{
	unsigned int block_flag = arg;

	if (block_flag)
		cd->get_frame_block_flag = THP_GET_FRAME_BLOCK;
	else
		cd->get_frame_block_flag = THP_GET_FRAME_NONBLOCK;

	thp_log_info(cd, "%s:set block %d\n", __func__, block_flag);

	thp_wake_up_frame_waitq(cd);
	return 0;
}

static long thp_ioctl_set_irq(struct thp_core_data *cd, unsigned long arg)
{
	unsigned int irq_en = (unsigned int)arg;

	mutex_lock(&cd->suspend_flag_mutex);
	if ((cd->pre_suspended || cd->suspended) &&
	    (!cd->need_work_in_suspend)) {
		thp_log_info(cd, "%s: drv suspended\n", __func__);
		mutex_unlock(&cd->suspend_flag_mutex);
		return -EINVAL;
	}
	thp_set_irq_status(cd, irq_en);
	mutex_unlock(&cd->suspend_flag_mutex);
	return 0;
}

static long thp_ioctl_get_irq_gpio_value(struct thp_core_data *cd,
					 unsigned long arg)
{
	u32 __user *out_value = (u32 *)arg;
	u32 value;

	value = gpio_get_value(cd->gpios.irq_gpio);
	if (copy_to_user(out_value, &value, sizeof(u32))) {
		thp_log_err(cd, "%s:copy value fail\n", __func__);
		return -EFAULT;
	}
	return 0;
}

static long thp_ioctl_set_spi_speed(struct thp_core_data *cd, unsigned long arg)
{
	unsigned long max_speed_hz = arg;
	int rc;

	if (max_speed_hz == cd->sdev->max_speed_hz)
		return 0;
	rc = thp_set_spi_max_speed(cd, max_speed_hz);
	if (rc)
		thp_log_err(cd, "%s: failed to set spi speed\n", __func__);
	return rc;
}

static long thp_ioctl_spi_sync_ssl_bl(struct thp_core_data *cd,
				      const void __user *data)
{
	struct thp_ioctl_spi_sync_data sync_data;
	int rc = 0;
	u8 *tx_buf = NULL;
	u8 *rx_buf = NULL;

	memset(&sync_data, 0, sizeof(sync_data));
	thp_log_debug(cd, "%s: called\n", __func__);

	if (!data) {
		thp_log_err(cd, "%s: data null\n", __func__);
		return -EINVAL;
	}
	if (cd == NULL) {
		thp_log_err(cd, "%s: thp get core data err\n", __func__);
		return -EINVAL;
	}
	if (cd->pre_suspended || cd->suspended)
		return 0;

#if defined(CONFIG_TEE_TUI)
	if (tui_enable)
		return 0;
#endif

	if (copy_from_user(&sync_data, data,
			   sizeof(struct thp_ioctl_spi_sync_data))) {
		thp_log_err(cd, "Failed to copy_from_user()\n");
		return -EFAULT;
	}

	if (sync_data.size > THP_SYNC_DATA_MAX || 0 == sync_data.size) {
		thp_log_err(cd, "sync_data.size out of range\n");
		return -EINVAL;
	}

	rx_buf = kzalloc(sync_data.size, GFP_KERNEL);
	tx_buf = kzalloc(sync_data.size, GFP_KERNEL);
	if (!rx_buf || !tx_buf) {
		thp_log_err(cd,
			    "%s:buf request memory fail,sync_data.size = %d\n",
			    __func__, sync_data.size);
		goto exit;
	}

	if (sync_data.tx) {
		rc = copy_from_user(tx_buf, sync_data.tx, sync_data.size);
		if (rc) {
			thp_log_err(cd, "%s:copy in buff fail\n", __func__);
			goto exit;
		}
	}

	if (cd) {
		if (cd->thp_dev->ops->spi_transfer_one_byte_bootloader)
			rc = cd->thp_dev->ops->spi_transfer_one_byte_bootloader(
				cd->thp_dev, tx_buf, rx_buf, sync_data.size);
		else
			rc = -EINVAL;
		if (rc) {
			thp_log_err(cd, "%s: transfer error, ret = %d\n",
				    __func__, rc);
			goto exit;
		}
	}

	if (sync_data.rx) {
		rc = copy_to_user(sync_data.rx, rx_buf, sync_data.size);
		if (rc) {
			thp_log_err(cd, "%s:copy out buff fail\n", __func__);
			goto exit;
		}
	}

exit:

	kfree(rx_buf);
	kfree(tx_buf);

	return rc;
}

static void thp_wakeup_screenon_waitq(struct thp_core_data *cd)
{
	if (!cd) {
		thp_log_err(cd, "%s: cd is null\n", __func__);
		return;
	}

	if (atomic_read(&(cd->afe_screen_on_waitq_flag)) != WAITQ_WAKEUP) {
		atomic_set(&(cd->afe_screen_on_waitq_flag), WAITQ_WAKEUP);
		wake_up_interruptible(&(cd->afe_screen_on_waitq));
	} else {
		thp_log_info(cd, "afe set status screen on have done\n");
	}
}

static long thp_ioctl_set_afe_status(struct thp_core_data *cd,
				     const void __user *data)
{
	int rc = 0;
	struct thp_ioctl_set_afe_status set_afe_status;

	thp_log_info(cd, "%s: called\n", __func__);
	if (!data) {
		thp_log_err(cd, "%s: data null\n", __func__);
		return -EINVAL;
	}
	if (copy_from_user(&set_afe_status, data,
			   sizeof(struct thp_ioctl_set_afe_status))) {
		thp_log_err(cd, "Failed to copy_from_user()\n");
		return -EFAULT;
	}
	thp_log_info(cd, "%s ->%d,%d,%d\n", __func__, set_afe_status.type,
		     set_afe_status.status, set_afe_status.parameter);

	switch (set_afe_status.type) {
	case THP_AFE_STATUS_FW_UPDATE:
		if (cd->thp_dev->ops->set_fw_update_mode != NULL)
			rc = cd->thp_dev->ops->set_fw_update_mode(
				cd->thp_dev, set_afe_status);
		else
			rc = -EINVAL;
		if (!rc) {
			if (set_afe_status.parameter == NORMAL_WORK_MODE) {
				cd->is_fw_update = 0;
			} else if (set_afe_status.parameter == FW_UPDATE_MODE) {
				cd->is_fw_update = 1;
			} else {
				thp_log_err(cd, "%s->error mode\n", __func__);
				rc = -EINVAL;
			}
			thp_log_info(cd, "%s call is_fw_updating=%d\n",
				     __func__, cd->is_fw_update);
		}
		break;
	case THP_AFE_STATUS_SCREEN_ON:
		if (cd->wait_afe_screen_on_support)
			thp_wakeup_screenon_waitq(cd);
		if (cd->lcd_need_get_afe_status &&
		    (set_afe_status.status == 1)) {
			get_timestamp(&cd->afe_status_record_tv);
			cd->afe_download_status = true;
		}
		thp_lcd_status_notifier(cd, TS_AFE_SCREEN_ON);
		break;
	case THP_AFE_STATUS_TUI:
		thp_log_info(cd, "%s: THP_AFE_STATUS_TUI called %d\n", __func__,
			     set_afe_status.status);
		thp_vm_afe_notify(cd, set_afe_status.status);
		break;
	default:
		thp_log_err(cd, "%s: illegal type\n", __func__);
		rc = -EINVAL;
		break;
	}
	return rc;
}

static long thp_ioctl_get_work_status(struct thp_core_data *cd,
				      unsigned long arg)
{
	u32 __user *work_status = (u32 *)(uintptr_t)arg;
	u32 status;

	if (cd == NULL) {
		thp_log_err(cd, "%s: thp cord data null\n", __func__);
		return -EINVAL;
	}
	if (work_status == NULL) {
		thp_log_err(cd, "%s: input parameter null\n", __func__);
		return -EINVAL;
	}
	if (cd->suspended == true)
		status = THP_WORK_STATUS_SUSPENDED;
	else
		status = THP_WORK_STATUS_RESUMED;

	if (copy_to_user(work_status, &status, sizeof(u32))) {
		thp_log_err(cd, "%s:get wort_status failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static long thp_ioctl_spi_cmd_lock_proc(struct thp_core_data *cd,
					unsigned long arg)
{
	int lock_status = (int)arg;

	if (cd == NULL) {
		thp_log_err(cd, "%s: thp cord data null\n", __func__);
		return -EINVAL;
	}

	if (lock_status == THP_SPI_CMD_LOCK) {
		thp_log_info(cd, "%s: spi cmd lock\n", __func__);
		thp_spi_cmd_lock(cd, SPI_CMD_LOCK_TIMEOUT);
		cd->cmd_lock.locked_by_afe = true;
	} else {
		thp_log_info(cd, "%s: spi cmd unlock\n", __func__);
		thp_spi_cmd_unlock(cd);
		cd->cmd_lock.locked_by_afe = false;
	}
	return NO_ERR;
}

static long thp_ioctl_set_resume_status(struct thp_core_data *cd,
					unsigned long arg)
{
	unsigned long status = arg;

	thp_log_info(cd, "%s:set resume status %lu\n", __func__, status);
	switch (status) {
	case THP_AFE_NOTIFY_RESUME_FINISH:
		if (cd->wait_afe_screen_on_support) {
			thp_log_info(cd, "%s: afe notify resume status\n",
				     __func__);
			thp_wakeup_screenon_waitq(cd);
		}
		break;
	default:
		thp_log_err(cd, "%s: illegal type\n", __func__);
		break;
	}
	return 0;
}

static long thp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret;
	struct thp_core_data *cd = misc_dev_get_core_data(filp->private_data);

	if (cd == NULL) {
		thp_log_err(cd, "%s: misc_dev_get_core_data is NULL\n",
			    __func__);
		return -EINVAL;
	}

	if (thp_vm_tui_enabled(cd)) {
		thp_log_err(
			cd,
			"%s: Cannot service ioctl cmd %x in PVM while trusted touch is enabled\n",
			__func__, cmd);
		return 0;
	}

	switch (cmd) {
	case THP_IOCTL_CMD_GET_FRAME_COMPAT:
	case THP_IOCTL_CMD_GET_FRAME:
		ret = thp_ioctl_get_frame(cd, arg, filp->f_flags);
		break;

	case THP_IOCTL_CMD_RESET:
		ret = thp_ioctl_reset(cd, arg);
		break;

	case THP_IOCTL_CMD_SET_TIMEOUT:
		ret = thp_ioctl_set_timeout(cd, arg);
		break;

	case THP_IOCTL_CMD_SPI_SYNC_COMPAT:
	case THP_IOCTL_CMD_SPI_SYNC:
		ret = thp_ioctl_spi_sync(cd, (void __user *)arg, NEED_LOCK);
		break;

	case THP_IOCTL_CMD_FINISH_NOTIFY:
		ret = thp_ioctl_finish_notify(cd, arg);
		break;
	case THP_IOCTL_CMD_SET_BLOCK:
		ret = thp_ioctl_set_block(cd, arg);
		break;

	case THP_IOCTL_CMD_SET_IRQ:
		ret = thp_ioctl_set_irq(cd, arg);
		break;

	case THP_IOCTL_CMD_GET_FRAME_COUNT:
		ret = thp_ioctl_get_frame_count(cd, arg);
		break;
	case THP_IOCTL_CMD_CLEAR_FRAME_BUFFER:
		ret = thp_ioctl_clear_frame_buffer(cd);
		break;

	case THP_IOCTL_CMD_GET_IRQ_GPIO_VALUE:
		ret = thp_ioctl_get_irq_gpio_value(cd, arg);
		break;

	case THP_IOCTL_CMD_SET_SPI_SPEED:
		ret = thp_ioctl_set_spi_speed(cd, arg);
		break;
	case THP_IOCTL_CMD_SPI_SYNC_SSL_BL:
		ret = thp_ioctl_spi_sync_ssl_bl(cd, (void __user *)arg);
		break;
	case THP_IOCTL_CMD_SET_AFE_STATUS:
		ret = thp_ioctl_set_afe_status(cd, (void __user *)arg);
		break;
	case THP_IOCTL_CMD_SET_RESUME_STATUS:
		ret = thp_ioctl_set_resume_status(cd, arg);
		break;
	case THP_IOCTL_CMD_MUILTIPLE_SPI_XFRE_SYNC:
		ret = thp_ioctl_multiple_spi_xfer_sync(cd, (void __user *)arg,
						       NEED_LOCK);
		break;
	case THP_IOCTL_CMD_HW_LOCK:
		ret = thp_ioctl_hw_lock_status(cd, arg);
		break;
	case THP_IOCTL_CMD_SPI_SYNC_NO_LOCK:
		ret = thp_ioctl_spi_sync(cd, (void __user *)arg,
					 DONOT_NEED_LOCK);
		break;
	case THP_IOCTL_CMD_MUILTIPLE_SPI_XFRE_SYNC_NO_LOCK:
		ret = thp_ioctl_multiple_spi_xfer_sync(cd, (void __user *)arg,
						       DONOT_NEED_LOCK);
		break;
	case THP_IOCTL_CMD_GET_WORK_STATUS:
		ret = thp_ioctl_get_work_status(cd, arg);
		break;
	case THP_IOCTL_CMD_SET_SPI_CMD_LOCK:
		ret = thp_ioctl_spi_cmd_lock_proc(cd, arg);
		break;
	default:
		thp_log_err(cd, "%s: cmd unknown, cmd = 0x%x\n", __func__, cmd);
		ret = 0;
	}

	return ret;
}

static long thp_compat_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	long ret;
	struct thp_core_data *cd = misc_dev_get_core_data(filp->private_data);

	if (cd == NULL) {
		thp_log_err(cd, "%s: misc_dev_get_core_data is NULL\n",
			    __func__);
		return -EINVAL;
	}

	cd->compat_flag = true;
	ret = thp_ioctl(filp, cmd, arg);
	if (ret)
		thp_log_err(cd, "%s: ioctl err %ld\n", __func__, ret);

	return ret;
}

static const struct file_operations g_thp_fops = {
	.owner = THIS_MODULE,
	.open = thp_open,
	.release = thp_release,
	.unlocked_ioctl = thp_ioctl,
	.compat_ioctl = thp_compat_ioctl,
};

static void tp_event_dispatch(struct thp_core_data *cd,
			      struct thp_udfp_data udfp_data)
{
	thp_log_info(cd, "%s: fp_event = %d, aod_event = %u, keyevent = %u\n",
		     __func__, udfp_data.tpud_data.udfp_event,
		     udfp_data.aod_event, udfp_data.key_event);
	if (cd->standby_mode) {
		thp_tui_report_to_input(cd, udfp_data);
		thp_log_info(cd, "%s: send event to input\n", __func__);
		return;
	}
	if (udfp_data.tpud_data.udfp_event != TP_FP_EVENT_MAX) {
		send_event_to_fingerprint_ud(cd, udfp_data);
		thp_udfp_event_to_aod(cd, udfp_data);
	}
	if (udfp_data.aod_event == AOD_VALID_EVENT) {
		thp_aod_click_report(cd, udfp_data);
		thp_lcd_status_notifier(cd, TS_EVENT_HOVER_DOWN);
	}
	if (udfp_data.key_event == TS_DOUBLE_CLICK ||
	    udfp_data.key_event == TS_STYLUS_WAKEUP_TO_MEMO)
		thp_inputkey_report(cd, udfp_data.key_event);
	if (udfp_data.slide_event)
		thp_slide_event_report(cd, udfp_data);
}

static void gesture_event_process(struct thp_core_data *cd)
{
	int rc;
	struct thp_udfp_data udfp_data;

	memset(&udfp_data, 0, sizeof(udfp_data));
	if (!cd->thp_dev->ops->get_event_info) {
		thp_log_err(cd, "%s: ops is NULL\n", __func__);
		return;
	}
	if (cd->standby_mode)
		rc = cd->thp_dev->ops->get_tui_event_info(cd->thp_dev,
							  &udfp_data);
	else
		rc = cd->thp_dev->ops->get_event_info(cd->thp_dev, &udfp_data);
	if (rc) {
		thp_log_err(cd, "%s: get event info fail, ret = %d\n", __func__,
			    rc);
		return;
	}
	tp_event_dispatch(cd, udfp_data);
}

static void thp_copy_frame_to_thp_queue(struct thp_core_data *cd)
{
	static int pre_frame_count = -1;

	mutex_lock(&(cd->mutex_frame));
	/* check for max limit */
	if (cd->frame_count >= THP_LIST_MAX_FRAMES) {
		if (cd->frame_count != pre_frame_count)
			thp_log_err(
				cd,
				"frame_lite buf full start,frame_count:%d\n",
				cd->frame_count);

		if (!thp_queue_dequeue(cd, cd->thp_queue))
			thp_log_err(cd, "%s queue remove failed\n", __func__);
		pre_frame_count = cd->frame_count;
		cd->frame_count--;
	} else if (pre_frame_count >= THP_LIST_MAX_FRAMES) {
		thp_log_err(
			cd,
			"%s:frame buf full exception restored,frame_count:%d\n",
			__func__, cd->frame_count);
		pre_frame_count = cd->frame_count;
	}

	if (!thp_queue_enqueue(cd, cd->thp_queue, cd->frame_read_buf,
			       cd->frame_size))
		thp_log_err(cd, "%s queue insert failed\n", __func__);
	cd->frame_count++;
	mutex_unlock(&(cd->mutex_frame));
}

static void thp_copy_frame(struct thp_core_data *cd)
{
	struct thp_frame *temp = NULL;
	static int pre_frame_count = -1;
	unsigned long len;

	mutex_lock(&(cd->mutex_frame));

	/* check for max limit */
	if (cd->frame_count >= THP_LIST_MAX_FRAMES) {
		//		if (cd->frame_count != pre_frame_count)
		//			thp_log_err(cd, "frame buf full start,frame_count:%d\n",
		//				cd->frame_count);

		temp = list_first_entry(&cd->frame_list.list, struct thp_frame,
					list);
		list_del(&temp->list);
		kfree(temp);
		pre_frame_count = cd->frame_count;
		cd->frame_count--;
	} else if (pre_frame_count >= THP_LIST_MAX_FRAMES) {
		//		thp_log_err(cd,
		//			"%s:frame buf full exception restored,frame_count:%d\n",
		//			__func__, cd->frame_count);
		pre_frame_count = cd->frame_count;
	}

	temp = kzalloc(sizeof(struct thp_frame), GFP_KERNEL);
	if (!temp) {
		thp_log_err(cd, "%s:memory out\n", __func__);
		mutex_unlock(&(cd->mutex_frame));
		return;
	}

	if (cd->frame_size > sizeof(temp->frame)) {
		thp_log_err(cd, "%s: frame size is too large: %u\n", __func__,
			    cd->frame_size);
		len = sizeof(temp->frame);
	} else {
		len = cd->frame_size;
	}
	memcpy(temp->frame, cd->frame_read_buf, len);
	get_timestamp(&(temp->tv));
	list_add_tail(&(temp->list), &(cd->frame_list.list));
	cd->frame_count++;
	mutex_unlock(&(cd->mutex_frame));
}

/*
 * disable the interrupt if the interrupt is enabled,
 * which is only used in irq handler
 */
static void thp_disable_irq_in_irq_process(struct thp_core_data *cd)
{
	int ret;

	/*
	 * Use mutex_trylock to avoid the irq process requesting lock failure,
	 * thus solving the problem that other process calls disable_irq
	 * process is blocked.
	 */
	ret = mutex_trylock(&cd->irq_mutex);
	if (ret) {
		if (cd->irq_enabled) {
			disable_irq_nosync(cd->irq);
			cd->irq_enabled = 0;
			thp_log_info(cd,
				     "%s:disable irq to protect irq storm\n",
				     __func__);
		}
		mutex_unlock(&cd->irq_mutex);
		return;
	}
	thp_log_info(cd, "%s:failed to try lock, only need ignore it\n",
		     __func__);
}

static void protect_for_irq_storm(struct thp_core_data *cd)
{
	struct timeval end_time;
	static struct timeval irq_storm_start_time;
	long delta_time;

	/*
	 * We should not try to disable irq when we
	 * need to handle the irq in screen off state
	 */
	if (!cd->support_irq_storm_protect) {
		if (need_work_in_suspend_switch(cd)) {
			thp_log_err(cd, "%s:ignore the irq\n", __func__);
			return;
		}
	}
	if (cd->invalid_irq_num == 0) {
		get_timestamp(&irq_storm_start_time);
		return;
	}
	if (cd->invalid_irq_num == MAX_INVALID_IRQ_NUM) {
		memset(&end_time, 0, sizeof(end_time));
		cd->invalid_irq_num = 0;
		get_timestamp(&end_time);
		/* multiply 1000000 to transfor second to us */
		delta_time = ((end_time.tv_sec - irq_storm_start_time.tv_sec) *
			      1000000) +
			     end_time.tv_usec - irq_storm_start_time.tv_usec;
		/* divide 1000 to transfor sec to us to ms */
		delta_time /= 1000;
		thp_log_info(cd, "%s:delta_time = %ld ms\n", __func__,
			     delta_time);
		if (delta_time <= (MAX_INVALID_IRQ_NUM / THP_IRQ_STORM_FREQ)) {
			if (cd->support_irq_storm_protect)
				cd->irq_storm_flag = true;
			thp_disable_irq_in_irq_process(cd);
			thp_set_irq_wake_status(cd, THP_IRQ_WAKE_DISABLE);
		}
	}
}

static void thp_irq_thread_suspend_process(struct thp_core_data *cd)
{
	int rc;
	unsigned int gesture_wakeup_value = 0;
	/*
	 * whole process interruption storm protection
	 * when open support_irq_storm_protect
	 */
	if (cd->support_irq_storm_protect) {
		if (!cd->irq_storm_flag) {
			protect_for_irq_storm(cd);
			cd->invalid_irq_num++;
		}
	}
#if IS_ENABLED(CONFIG_TP_QCOM_8450)
	thp_spi_irq_suspend_flag = 1;
	thp_log_info(cd, "thp_spi_irq_suspend_flag = 1\n");
#endif
	if (need_work_in_suspend_switch(cd) &&
	    (cd->work_status != RESUME_DONE)) {
		thp_log_info(cd, "%s:ts gesture mode irq\n", __func__);
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
		if (cd->use_ap_gesture) {
			/* This function includes tpud, AOD click, doubletap */
			gesture_event_process(cd);
			goto exit;
		}
#endif
		if (cd->thp_dev->ops->chip_gesture_report) {
			rc = cd->thp_dev->ops->chip_gesture_report(
				cd->thp_dev, &gesture_wakeup_value);
			if (rc) {
				thp_log_err(
					cd,
					"%s:gesture report failed this irq,rc = %d\n",
					__func__, rc);
				goto exit;
			}
		} else {
			thp_log_err(cd, "%s:gesture not support\n", __func__);
			goto exit;
		}
		thp_inputkey_report(cd, gesture_wakeup_value);
		goto exit;
	}
exit:
	trace_touch(TOUCH_TRACE_IRQ_BOTTOM, TOUCH_TRACE_FUNC_OUT, "thp");
}

static void do_irq_scheduler_restore(struct thp_core_data *cd)
{
	struct cpumask cpumask = {0};
	int i = 0;
	int ret = 0;
	uint8_t little_cpu_list[] = {0, 1};

	for (i = 0; i < sizeof(little_cpu_list); i++)
		cpumask_set_cpu(little_cpu_list[i], &cpumask);
	if (cd->irq_task) {
		ret = set_cpus_allowed_ptr(cd->irq_task, &cpumask);
		if (ret)
			thp_log_err(cd, "%s: set_cpus_allowed_ptr fail: %d\n",
				    __func__, ret);
	}
}

static void do_irq_scheduler_boost(struct thp_core_data *cd)
{
	struct cpumask cpumask = {0};
	int ret;

	cpumask_setall(&cpumask);
	if (cd->irq_task) {
		ret = set_cpus_allowed_ptr(cd->irq_task, &cpumask);
		if (ret)
			thp_log_err(cd, "%s: set allowed_ptr fail: %d \n",
				    __func__, ret);
	}
}

static void irq_scheduler_boost_timer_fn(struct timer_list *timer)
{
	struct thp_core_data *cd = container_of(timer, struct thp_core_data,
						scheduler_boost.timer);
	struct thp_cmd_node cmd = {0};

	cmd.command = IRQ_SCHEDULER_BOOST;
	cmd.cmd_param.pub_params.scheduler_boost_op = IRQ_SCHEDULER_OP_RESTORE;
	thp_put_one_cmd(cd, &cmd, NO_SYNC_TIMEOUT);
}

static void thp_irq_scheduler_boost(struct thp_core_data *cd)
{
	struct thp_cmd_node cmd = {0};

	if (cd->scheduler_boost.boost_handled) {
		cd->scheduler_boost.uh_handled = false;
		cmd.command = IRQ_SCHEDULER_BOOST;
		cmd.cmd_param.pub_params.scheduler_boost_op =
			IRQ_SCHEDULER_OP_BOOST;
		cd->scheduler_boost.boost_handled = false;
		thp_put_one_cmd(cd, &cmd, NO_SYNC_TIMEOUT);
	}
}

static void thp_irq_scheduler_boost_thread(struct thp_core_data *cd)
{
	cd->scheduler_boost.uh_handled = true;
	wake_up_interruptible(&(cd->scheduler_boost.waitq));
}

static void thp_irq_scheduler_boost_init(struct thp_core_data *cd)
{
	cd->scheduler_boost.uh_handled = true;
	cd->scheduler_boost.boost_handled = true;
	init_waitqueue_head(&(cd->scheduler_boost.waitq));
}

static void irq_scheduler_boost_proc(struct thp_core_data *cd,
				     struct thp_cmd_node *in_cmd)
{
	if (in_cmd->cmd_param.pub_params.scheduler_boost_op ==
	    IRQ_SCHEDULER_OP_BOOST) {
		wait_event_interruptible_timeout(
			cd->scheduler_boost.waitq,
			(cd->scheduler_boost.uh_handled == true),
			msecs_to_jiffies(IRQ_SCHEDULER_BOOST_THRESHOLD));
		if (cd->scheduler_boost.uh_handled == false) {
			thp_log_info(cd, "%s: boost start\n", __func__);
			do_irq_scheduler_boost(cd);
			cd->scheduler_boost.timer.function =
				irq_scheduler_boost_timer_fn;
			mod_timer(
				&cd->scheduler_boost.timer,
				jiffies +
					msecs_to_jiffies(
						IRQ_SCHEDULER_BOOST_HOLD_TIME));
		}
		cd->scheduler_boost.boost_handled = true;
	} else {
		thp_log_info(cd, "%s: boost end\n", __func__);
		do_irq_scheduler_restore(cd);
	}
}

static void thp_irq_thread_pre_init(struct thp_core_data *cd)
{
	do_irq_scheduler_restore(cd);
}

static irqreturn_t thp_irq_handler(int irq, void *dev_id)
{
	struct thp_core_data *cd = dev_id;

	if (cd->irq_task)
		thp_irq_scheduler_boost(cd);
	return IRQ_WAKE_THREAD;
}

#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
#define WAIT_SYS_ACTIVE_TIMEOUT 100
#endif
static irqreturn_t thp_irq_thread(int irq, void *dev_id)
{
	struct thp_core_data *cd = dev_id;
	u8 *read_buf = (u8 *)cd->frame_read_buf;
	int rc;

	cd->irq_thread_pid = current->pid;
	if (!cd->irq_task) {
		cd->irq_task = current;
		thp_irq_thread_pre_init(cd);
	}

	trace_touch(TOUCH_TRACE_IRQ_BOTTOM, TOUCH_TRACE_FUNC_IN, "thp");
	if (cd->reset_flag) {
		thp_log_err(cd, "%s:reset state, ignore this irq\n", __func__);
		return IRQ_HANDLED;
	}

#if defined(CONFIG_TEE_TUI)
	if (tui_enable) {
		thp_log_err(cd, "%s:tui_mode, disable irq\n", __func__);
		thp_disable_irq_in_irq_process(cd);
		return IRQ_HANDLED;
	}
#endif
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	atomic_set(&cd->suspend_irq_processing_status, THP_IRQ_PROCESSING);
	rc = wait_event_interruptible_timeout(
		cd->system_active_waitq, (cd->system_active_waitq_flag == true),
		msecs_to_jiffies(WAIT_SYS_ACTIVE_TIMEOUT));
	if (is_tmo(rc))
		goto exit;
#endif
	if (cd->suspended && (!cd->need_work_in_suspend)) {
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
		thp_log_info(cd, "%s: wait time = %dms rc = %d\n", __func__,
			     WAIT_SYS_ACTIVE_TIMEOUT - jiffies_to_msecs(rc),
			     rc);
#endif
		thp_irq_thread_suspend_process(cd);
		goto exit;
	}

	disable_irq_nosync(cd->irq);

	/* get frame */
	rc = cd->thp_dev->ops->get_frame(cd->thp_dev, read_buf, cd->frame_size);
	if (rc) {
		thp_log_err(cd, "%s: failed to read frame %d\n", __func__, rc);
		goto exit_enable_irq;
	}

	trace_touch(TOUCH_TRACE_DATA2ALGO, TOUCH_TRACE_FUNC_IN, "thp");
	if (cd->use_thp_queue)
		thp_copy_frame_to_thp_queue(cd);
	else
		thp_copy_frame(cd);
	thp_wake_up_frame_waitq(cd);
	trace_touch(TOUCH_TRACE_DATA2ALGO, TOUCH_TRACE_FUNC_OUT, "thp");

exit_enable_irq:
	enable_irq(cd->irq);
exit:
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	atomic_set(&cd->suspend_irq_processing_status, THP_IRQ_IDLE);
#endif
	thp_irq_scheduler_boost_thread(cd);
	trace_touch(TOUCH_TRACE_IRQ_BOTTOM, TOUCH_TRACE_FUNC_OUT, "thp");
	return IRQ_HANDLED;
}
void thp_spi_cs_set_sub(u32 control)
{
	thp_spi_cs_set(g_thp_core_sub, control);
}

void thp_spi_cs_set_master(u32 control)
{
	thp_spi_cs_set(g_thp_core, control);
}

void thp_spi_cs_set(struct thp_core_data *cd, u32 control)
{
	int rc = 0;
	struct thp_timing_config *timing = NULL;

	if (!cd || !cd->thp_dev) {
		thp_log_err(cd, "%s:no driver data", __func__);
		return;
	}

	timing = &cd->thp_dev->timing_config;

	if (control == SSP_CHIP_SELECT) {
		rc = gpio_direction_output(cd->gpios.cs_gpio, control);
		ndelay(timing->spi_sync_cs_low_delay_ns);
		if (timing->spi_sync_cs_low_delay_us && (!cd->is_fw_update))
			udelay(timing->spi_sync_cs_low_delay_us);
	} else {
		rc = gpio_direction_output(cd->gpios.cs_gpio, control);
		ndelay(timing->spi_sync_cs_hi_delay_ns);
	}

	if (rc < 0)
		thp_log_err(cd, "%s:fail to set gpio cs", __func__);
}
EXPORT_SYMBOL(thp_spi_cs_set);

#define THP_PROJECTID_LEN 9
#define THP_PROJECTID_PRODUCT_NAME_LEN 4
#define THP_PROJECTID_IC_NAME_LEN 2
#define THP_PROJECTID_VENDOR_NAME_LEN 3

struct thp_vendor {
	char *vendor_id;
	char *vendor_name;
};

struct thp_ic_name {
	char *ic_id;
	char *ic_name;
};

static struct thp_vendor thp_vendor_table[] = {
	{"000", "ofilm"},   {"030", "mutto"},  {"080", "jdi"},
	{"090", "samsung"}, {"100", "lg"},     {"101", "lg"},
	{"102", "lg"},	    {"110", "tianma"}, {"111", "tianma"},
	{"112", "tianma"},  {"113", "tianma"}, {"120", "cmi"},
	{"130", "boe"},	    {"131", "boe"},    {"132", "boe"},
	{"140", "ctc"},	    {"160", "sharp"},  {"170", "auo"},
	{"250", "txd"},	    {"270", "tcl"},
};

static struct thp_ic_name thp_ic_table[] = {
	{"32", "rohm"},	     {"47", "rohm"},	  {"49", "novatech"},
	{"59", "himax"},     {"60", "himax"},	  {"61", "himax"},
	{"62", "synaptics"}, {"65", "novatech"},  {"66", "himax"},
	{"68", "focaltech"}, {"69", "synaptics"}, {"71", "novatech"},
	{"77", "novatech"},  {"78", "goodix"},	  {"79", "ilitek"},
	{"86", "synaptics"}, {"88", "novatech"},  {"91", "synaptics"},
	{"96", "synaptics"}, {"9A", "focaltech"}, {"9B", "synaptics"},
	{"9C", "novatech"},  {"9D", "focaltech"}, {"9I", "novatech"},
	{"9J", "novatech"},  {"9L", "himax"},	  {"9M", "novatech"},
	{"9N", "focaltech"}, {"9Y", "novatech"},  {"A5", "synaptics"},
	{"A7", "novatech"},  {"A8", "synaptics"}, {"A9", "focaltech"},
	{"A6", "ilitek"},    {"AG", "novatech"},  {"AH", "ilitek"},
	{"AJ", "novatech"},  {"AK", "goodix"},	  {"AL", "goodix"},
	{"AM", "goodix"},    {"AS", "himax"},
};

static int thp_projectid_to_vender_name(struct thp_core_data *cd,
					const char *project_id,
					char **vendor_name, int project_id_len)
{
	char temp_buf[THP_PROJECTID_LEN + 1] = {'0'};
	int i;

	if (strlen(project_id) > project_id_len)
		thp_log_err(cd, "%s:project_id has a wrong length\n", __func__);
	strncpy(temp_buf,
		project_id + THP_PROJECTID_PRODUCT_NAME_LEN +
			THP_PROJECTID_IC_NAME_LEN,
		THP_PROJECTID_VENDOR_NAME_LEN);

	for (i = 0; i < ARRAY_SIZE(thp_vendor_table); i++) {
		if (!strncmp(thp_vendor_table[i].vendor_id, temp_buf,
			     strlen(thp_vendor_table[i].vendor_id))) {
			*vendor_name = thp_vendor_table[i].vendor_name;
			return 0;
		}
	}

	return -ENODATA;
}

static int thp_projectid_to_ic_name(struct thp_core_data *cd,
				    const char *project_id, char **ic,
				    int project_id_len)
{
	char temp_buf[THP_PROJECTID_LEN + 1] = {'0'};
	int i;

	if (strlen(project_id) > project_id_len)
		thp_log_err(cd, "%s:project_id has a wrong length\n", __func__);
	strncpy(temp_buf, project_id + THP_PROJECTID_PRODUCT_NAME_LEN,
		THP_PROJECTID_IC_NAME_LEN);

	for (i = 0; i < ARRAY_SIZE(thp_ic_table); i++) {
		if (!strncmp(thp_ic_table[i].ic_id, temp_buf,
			     strlen(thp_ic_table[i].ic_id))) {
			*ic = thp_ic_table[i].ic_name;
			return 0;
		}
	}

	return -ENODATA;
}

int thp_init_chip_info(struct thp_core_data *cd)
{
	int rc = 0;

	rc = thp_lcdkit_get_project_id(cd, cd->project_id,
				       THP_PROJECT_ID_LEN + 1);
	if (rc)
		thp_log_err(cd, "%s:get project id form LCD fail\n", __func__);
	else
		thp_log_info(cd, "%s:project id :%s\n", __func__,
			     cd->project_id);

	cd->project_id[THP_PROJECT_ID_LEN] = '\0';

	rc = thp_projectid_to_vender_name(cd, cd->project_id,
					  (char **)&cd->vendor_name,
					  THP_PROJECT_ID_LEN + 1);
	if (rc)
		thp_log_info(cd, "%s:vendor name parse fail\n", __func__);

	rc = thp_projectid_to_ic_name(cd, cd->project_id, (char **)&cd->ic_name,
				      THP_PROJECT_ID_LEN + 1);
	if (rc)
		thp_log_info(cd, "%s:ic name parse fail\n", __func__);
	return rc;
}

#if (IS_ENABLED(CONFIG_HONOR_THP_MTK))
static int thp_mtk_pinctrl_get_init(struct thp_device *tdev)
{
	int ret = 0;
	struct thp_core_data *cd = tdev->thp_core;

	cd->mtk_pinctrl.cs_high =
		pinctrl_lookup_state(cd->pctrl, PINCTRL_STATE_CS_HIGH);
	if (IS_ERR_OR_NULL(cd->mtk_pinctrl.cs_high)) {
		thp_log_err(cd, "Can not lookup %s pinstate\n",
			    PINCTRL_STATE_CS_HIGH);
		ret = -EINVAL;
		return ret;
	}

	cd->mtk_pinctrl.cs_low =
		pinctrl_lookup_state(cd->pctrl, PINCTRL_STATE_CS_LOW);
	if (IS_ERR_OR_NULL(cd->mtk_pinctrl.cs_low)) {
		thp_log_err(cd, "Can not lookup %s pinstate\n",
			    PINCTRL_STATE_CS_LOW);
		ret = -EINVAL;
		return ret;
	}

	if (cd->change_spi_driving_force) {
		cd->mtk_pinctrl.spi_status = pinctrl_lookup_state(
			cd->pctrl, PINCTRL_STATE_SPI_STATUS);
		if (IS_ERR_OR_NULL(cd->mtk_pinctrl.spi_status)) {
			thp_log_err(cd, "Can not lookup %s pinstate\n",
				    PINCTRL_STATE_SPI_STATUS);
			ret = -EINVAL;
			return ret;
		}
		ret = pinctrl_select_state(cd->pctrl,
					   cd->mtk_pinctrl.spi_status);
		if (ret < 0)
			thp_log_err(cd, "change spi driving force failed\n");
	}
	return ret;
}
#endif

static int thp_setup_irq(struct thp_core_data *cd)
{
	int rc;
	int irq;
	char *node = NULL;
	unsigned long irq_flag_type;
	u32 current_trigger_mode;

	if (!cd) {
		thp_log_err(cd, "%s: thp_core_data is null\n", __func__);
		return -EINVAL;
	}

	irq = gpio_to_irq(cd->gpios.irq_gpio);
	/*
	 * IRQF_TRIGGER_RISING 0x00000001
	 * IRQF_TRIGGER_FALLING 0x00000002
	 * IRQF_TRIGGER_HIGH 0x00000004
	 * IRQF_TRIGGER_LOW 0x00000008
	 * IRQF_NO_SUSPEND 0x00004000
	 */
	current_trigger_mode = cd->irq_flag;
	thp_log_info(cd, "%s:current_trigger_mode->0x%x\n", __func__,
		     current_trigger_mode);

	irq_flag_type = IRQF_ONESHOT | current_trigger_mode;
	if (cd->multi_panel_index != SINGLE_TOUCH_PANEL) {
		node = kzalloc(sizeof(char) * MULTI_PANEL_NODE_BUF_LEN,
			       GFP_KERNEL);
		if (!node) {
			thp_log_err(cd, "%s:node is null\n", __func__);
			rc = -ENOMEM;
			kfree(node);
			node = NULL;
			return rc;
		}
		rc = snprintf(node, MULTI_PANEL_NODE_BUF_LEN, "%s%d", "thp",
			      cd->multi_panel_index);
		if (rc < 0) {
			thp_log_err(cd, "%s: snprintf err\n", __func__);
			return rc;
		}
		rc = request_threaded_irq(irq, thp_irq_handler, thp_irq_thread,
					  irq_flag_type, node, cd);
	} else {
		rc = request_threaded_irq(irq, thp_irq_handler, thp_irq_thread,
					  irq_flag_type, "thp", cd);
	}

	if (rc) {
		thp_log_err(cd, "%s: request irq fail\n", __func__);
		kfree(node);
		node = NULL;
		return rc;
	}
	mutex_lock(&cd->irq_mutex);
	disable_irq(irq);
	cd->irq_enabled = false;
	mutex_unlock(&cd->irq_mutex);
	thp_log_info(cd, "%s: disable irq\n", __func__);
	cd->irq = irq;

	return 0;
}

static int thp_setup_gpio(struct thp_core_data *cd)
{
	int rc;

	thp_log_info(cd, "%s: called\n", __func__);

	rc = gpio_request(cd->gpios.rst_gpio, "thp_reset");
	if (rc) {
		thp_log_err(cd, "%s:gpio_request %d failed\n", __func__,
			    cd->gpios.rst_gpio);
		return rc;
	}

	rc = gpio_request(cd->gpios.cs_gpio, "thp_cs");
	if (rc) {
		thp_log_err(cd, "%s:gpio_request %d failed\n", __func__,
			    cd->gpios.cs_gpio);
		gpio_free(cd->gpios.rst_gpio);
		return rc;
	}
	gpio_direction_output(cd->gpios.cs_gpio, GPIO_HIGH);
	thp_log_info(cd, "%s:set cs gpio %d deault hi\n", __func__,
		     cd->gpios.cs_gpio);

	rc = gpio_request(cd->gpios.irq_gpio, "thp_int");
	if (rc) {
		thp_log_err(cd, "%s: irq gpio %d request failed\n", __func__,
			    cd->gpios.irq_gpio);
		gpio_free(cd->gpios.rst_gpio);
		gpio_free(cd->gpios.cs_gpio);
		return rc;
	}
	rc = gpio_direction_input(cd->gpios.irq_gpio);
	if (rc)
		thp_log_info(cd, "%s:gpio_direction_input failed\n", __func__);

	return 0;
}

int thp_setup_spi(struct thp_core_data *cd)
{
	int rc;

#if (IS_ENABLED(CONFIG_HONOR_THP_MTK))
	cd->mtk_spi_config.cs_setuptime =
		cd->thp_dev->timing_config.spi_sync_cs_low_delay_ns;
#endif
	rc = spi_setup(cd->sdev);
	if (rc) {
		thp_log_err(cd, "%s: spi setup fail\n", __func__);
		return rc;
	}

	return 0;
}
int thp_set_spi_com_mode(struct thp_core_data *cd, u8 spi_com_mode)
{
	int rc;

	if (!cd) {
		thp_log_err(cd, "%s: tdev null\n", __func__);
		return -EINVAL;
	}

	if (spi_com_mode != SPI_DMA_MODE && spi_com_mode != SPI_POLLING_MODE) {
		thp_log_err(cd, "%s ->error mode\n", __func__);
		return -EINVAL;
	}
	cd->spi_config.pl022_spi_config.com_mode = spi_com_mode;
	cd->sdev->controller_data = &cd->spi_config.pl022_spi_config;
	rc = thp_setup_spi(cd);
	thp_log_info(cd, "%s rc->%d\n", __func__, rc);
	return rc;
}

#if defined(CONFIG_TEE_TUI)
static u32 thp_get_multi_pm_status(void)
{
	struct thp_core_data *cd = thp_get_core_data();

	thp_log_info(cd, "%s current_pm_status:%u\n", __func__,
		     cd->current_pm_status);
	return cd->current_pm_status;
}

__attribute__((weak)) int i2c_init_secos(struct i2c_adapter *adap)
{
	return 0;
}

__attribute__((weak)) int i2c_exit_secos(struct i2c_adapter *adap)
{
	return 0;
}

#if (IS_ENABLED(CONFIG_HONOR_THP_MTK))
int spi_exit_secos(unsigned int spi_bus_id)
{
	struct thp_core_data *cd = thp_get_core_data();
	int rc;

	rc = thp_setup_spi(cd);
	thp_log_info(cd, "%s rc = %d\n", __func__, rc);

	return 0;
}

int spi_init_secos(unsigned int spi_bus_id)
{
	struct thp_core_data *cd = thp_get_core_data();

	thp_log_info(cd, "%s enter\n", __func__);
	return 0;
}
#endif

void thp_tui_secos_init(void)
{
	struct thp_core_data *cd = thp_get_core_data();
	int t;
#ifdef CONFIG_HONOR_SHB_THP
	int ret;
#endif

	if (!cd) {
		thp_log_err(cd, "%s: core not inited\n", __func__);
		return;
	}

	/* NOTICE: should not change this path unless ack daemon */
	thp_set_status(THP_STATUS_TUI, 1);
	cd->thp_ta_waitq_flag = WAITQ_WAIT;

	thp_log_info(cd, "%s: busid=%d. disable irq=%d\n", __func__,
		     cd->spi_config.bus_id, cd->irq);
	t = wait_event_interruptible_timeout(
		cd->thp_ta_waitq, (cd->thp_ta_waitq_flag == WAITQ_WAKEUP), HZ);
	thp_log_info(cd, "%s: wake up finish\n", __func__);

#ifdef CONFIG_HONOR_SHB_THP
	if (cd->support_shb_thp) {
		if (cd->thp_dev && cd->thp_dev->ops &&
		    cd->thp_dev->ops->switch_int_sh) {
			ret = cd->thp_dev->ops->switch_int_sh(cd->thp_dev);
			if (ret)
				thp_log_err(cd, "%s: switch to sh fail",
					    __func__);
		}
		thp_set_irq_status(cd, THP_IRQ_DISABLE);
		ret = send_thp_driver_status_cmd(TP_SWITCH_ON, 0,
						 ST_CMD_TYPE_SET_TUI_STATUS);
		if (ret)
			thp_log_err(cd, "%s: send thp tui on fail", __func__);
	} else {
#endif
		thp_set_irq_status(cd, THP_IRQ_DISABLE);
		if (cd->no_need_secos_bus_init == 0)
			spi_init_secos(cd->spi_config.bus_id);
#ifdef CONFIG_HONOR_SHB_THP
	}
#endif
	tui_enable = 1;
	thp_log_info(cd, "%s set tui_enable:%d\n", __func__, tui_enable);
}

void thp_tui_secos_exit(void)
{
	struct thp_core_data *cd = thp_get_core_data();
#ifdef CONFIG_HONOR_SHB_THP
	int ret;
#endif

	if (!cd) {
		thp_log_err(cd, "%s: core not inited\n", __func__);
		return;
	}
	if (cd->send_tui_exit_in_suspend && (!tui_enable)) {
		thp_log_err(cd, "%s TUI has exit\n", __func__);
		return;
	}
	tui_enable = 0;
	thp_log_info(cd, "%s: busid=%u tui_enable=%u\n", __func__,
		     cd->spi_config.bus_id, tui_enable);
#ifdef CONFIG_HONOR_SHB_THP
	if (cd->support_shb_thp) {
		if (cd->thp_dev && cd->thp_dev->ops &&
		    cd->thp_dev->ops->switch_int_ap) {
			ret = cd->thp_dev->ops->switch_int_ap(cd->thp_dev);
			if (ret)
				thp_log_err(cd, "%s: switch to ap fail",
					    __func__);
		}
		ret = send_thp_driver_status_cmd(TP_SWITCH_OFF, 0,
						 ST_CMD_TYPE_SET_TUI_STATUS);
		if (ret)
			thp_log_err(cd, "%s: send thp tui off fail", __func__);
	} else {
#endif
		if (cd->no_need_secos_bus_init == 0)
			spi_exit_secos(cd->spi_config.bus_id);
#ifdef CONFIG_HONOR_SHB_THP
	}
#endif
	mutex_lock(&cd->suspend_flag_mutex);
	if (cd->suspended && (!cd->need_work_in_suspend)) {
		thp_log_info(cd, "%s: drv suspended\n", __func__);
		mutex_unlock(&cd->suspend_flag_mutex);
		return;
	}
	thp_set_irq_status(cd, THP_IRQ_ENABLE);
	mutex_unlock(&cd->suspend_flag_mutex);
	thp_set_status(THP_STATUS_TUI, 0);
	thp_log_info(cd, "%s end\n", __func__);
}

static int thp_tui_switch(void *data, int secure)
{
	struct thp_core_data *cd = thp_get_core_data();

	thp_log_info(cd, "%s:tui secure is %d\n", __func__, secure);

	if (secure)
		thp_tui_secos_init();
	else
		thp_tui_secos_exit();
	return 0;
}

static int multi_tp_tui_switch(void *data, int secure)
{
	int ret = 0;
	struct thp_core_data *cd = thp_get_core_data();

	switch (thp_get_multi_pm_status()) {
	case M_ON_S_OFF:
		memcpy(&thp_tui_info, &tp_tui_data[MAIN_TOUCH_PANEL],
		       sizeof(thp_tui_info));
		ret = tp_tui_data[MAIN_TOUCH_PANEL].tui_drv_switch(data,
								   secure);
		thp_log_info(cd, "%s:device_name: %s  ret:%d\n", __func__,
			     thp_tui_info.project_id, ret);
		break;
	case M_OFF_S_ON:
		memcpy(&thp_tui_info, &tp_tui_data[SUB_TOUCH_PANEL],
		       sizeof(thp_tui_info));
		ret = tp_tui_data[SUB_TOUCH_PANEL].tui_drv_switch(data, secure);
		thp_log_info(cd, "%s:device_name: %s  ret:%d\n", __func__,
			     thp_tui_info.project_id, ret);
		break;
	default:
		thp_log_info(cd, "invalid mode\n");
	}
	return ret;
}

#define IC_THP_SHB_TUI_COMMON_ID "shb_tui"
#define IC_THP_SHB_TUI_COMMON_ID_LEN 8

static void thp_tui_init(struct thp_core_data *cd)
{
	int rc;

	if (!cd) {
		thp_log_err(cd, "%s: core not inited\n", __func__);
		return;
	}
	cd->current_pm_status = M_ON_S_ON;
	tui_enable = 0;
	if (cd->support_shb_thp && cd->support_shb_thp_tui_common_id) {
#ifdef CONFIG_HONOR_SHB_THP
		rc = snprintf(thp_tui_info.project_id,
			      IC_THP_SHB_TUI_COMMON_ID_LEN,
			      IC_THP_SHB_TUI_COMMON_ID);
		if (rc < 0)
			thp_log_err(cd, "snprintf failed, rc: %d\n", rc);
#endif
		thp_log_info(cd, "%s thp_tui_info.project_id:%s\n", __func__,
			     thp_tui_info.project_id);
	} else {
		strncpy(thp_tui_info.project_id, cd->project_id,
			THP_PROJECT_ID_LEN);
		thp_tui_info.project_id[THP_PROJECT_ID_LEN] = '\0';
		thp_log_info(cd, "%s thp_tui_info.project_id:%s\n", __func__,
			     thp_tui_info.project_id);
	}
	thp_tui_info.frame_data_addr = cd->frame_data_addr;
	thp_log_info(cd, "%s thp_tui_info.get_frame_addr %d\n", __func__,
		     thp_tui_info.frame_data_addr);
	memcpy(&tp_tui_data[MAIN_TOUCH_PANEL]
			.tp_tui_normalized_data.thp_tui_data,
	       &thp_tui_info, sizeof(thp_tui_info));
	if (cd->multi_panel_index == SINGLE_TOUCH_PANEL) {
		rc = register_tui_driver(thp_tui_switch, "tp", &thp_tui_info);
		if (rc != 0) {
			thp_log_err(cd, "%s reg thp_tui_switch fail: %d\n",
				    __func__, rc);
			return;
		}
	} else {
		if (cd->multi_panel_index == MAIN_TOUCH_PANEL) {
			thp_log_err(cd,
				    "%s register_tui_driver thp_tui_switch\n",
				    __func__);
			tp_tui_data[MAIN_TOUCH_PANEL].tui_drv_switch =
				thp_tui_switch;
			thp_log_info(cd, "%s thp_tui_info.project_id:%s\n",
				     __func__, thp_tui_info.project_id);
			rc = register_tui_driver(multi_tp_tui_switch, "tp",
						 &thp_tui_info);
			if (rc != 0) {
				thp_log_err(cd,
					    "%s reg thp_tui_switch fail: %d\n",
					    __func__, rc);
				return;
			}
		}
	}
}
#endif

static int thp_pinctrl_get_init(struct thp_device *tdev)
{
	int ret = 0;

	tdev->thp_core->pctrl = devm_pinctrl_get(&tdev->sdev->dev);
	if (IS_ERR(tdev->thp_core->pctrl)) {
		thp_log_err(tdev->thp_core, "failed to devm pinctrl get\n");
		ret = -EINVAL;
		return ret;
	}

	tdev->thp_core->pins_default =
		pinctrl_lookup_state(tdev->thp_core->pctrl, "default");
	if (IS_ERR(tdev->thp_core->pins_default)) {
		thp_log_err(tdev->thp_core,
			    "failed to pinctrl lookup state default\n");
		ret = -EINVAL;
		goto err_pinctrl_put;
	}

	tdev->thp_core->pins_idle =
		pinctrl_lookup_state(tdev->thp_core->pctrl, "idle");
	if (IS_ERR(tdev->thp_core->pins_idle)) {
		thp_log_err(tdev->thp_core,
			    "failed to pinctrl lookup state idle\n");
		ret = -EINVAL;
		goto err_pinctrl_put;
	}

#if (IS_ENABLED(CONFIG_HONOR_THP_MTK))
	ret = thp_mtk_pinctrl_get_init(tdev);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s: mtk pinctrl init failed\n",
			    __func__);
		goto err_pinctrl_put;
	}
#endif

	return 0;

err_pinctrl_put:
	devm_pinctrl_put(tdev->thp_core->pctrl);
	return ret;
}

const char *thp_get_vendor_name(void)
{
	struct thp_core_data *cd = thp_get_core_data();

	return (cd && cd->thp_dev) ? cd->thp_dev->ic_name : 0;
}
EXPORT_SYMBOL(thp_get_vendor_name);

static int thp_project_in_tp(struct thp_core_data *cd)
{
	int ret = 0;
	unsigned int value = 0;

	if (cd->project_in_tp && cd->thp_dev->ops->get_project_id)
		ret = cd->thp_dev->ops->get_project_id(
			cd->thp_dev, cd->project_id, THP_PROJECT_ID_LEN);

	if (ret) {
		strncpy(cd->project_id, cd->project_id_dummy,
			THP_PROJECT_ID_LEN);
		thp_log_info(cd, "%s:get projectfail ,use dummy id:%s\n",
			     __func__, cd->project_id);
	}

	/* projectid_from_panel_ver: open 1, close 0 */
	if (cd->projectid_from_panel_ver == 1) {
		thp_log_info(cd, "%s SVX value from LCD is %u\n", __func__,
			     value);
		value = value >> 4; /* right offset 4 bits */
		if (value == SV1_PANEL) {
			strncpy(cd->project_id, PROJECTID_SV1,
				THP_PROJECT_ID_LEN);
			thp_log_info(cd, "%s:project id SV1 is :%s\n", __func__,
				     cd->project_id);
		} else if (value == SV2_PANEL) {
			strncpy(cd->project_id, PROJECTID_SV2,
				THP_PROJECT_ID_LEN);
			thp_log_info(cd, "%s:project id SV2 is :%s\n", __func__,
				     cd->project_id);
		}
	}

	return ret;
}

static int thp_edit_product_in_project_id(struct thp_core_data *cd,
					  char *project_id,
					  unsigned int project_id_length,
					  const char *product)
{
	size_t len;

	if (product == NULL) {
		thp_log_err(cd, "%s:product is NULL\n", __func__);
		return -EINVAL;
	}
	len = strlen(product);

	thp_log_info(cd, "%s:product len is %zu\n", __func__, len);
	if ((len > project_id_length) || (len == 0))
		return -EINVAL;
	memcpy(project_id, product, len);

	return 0;
}

static int thp_project_id_mapping(struct thp_core_data *cd)
{
	size_t len;

	if ((cd->target_project_id == NULL) || (cd->map_project_id == NULL)) {
		thp_log_err(cd, "%s: target_map_project_id is NULL\n",
			    __func__);
		return -EINVAL;
	}
	len = strlen(cd->target_project_id);
	if ((len > THP_PROJECT_ID_LEN) || (len == 0))
		return -EINVAL;
	if (memcmp(cd->target_project_id, cd->project_id, len) == 0)
		memcpy(cd->project_id, cd->map_project_id, len);
	return 0;
}

static int thp_project_init(struct thp_core_data *cd)
{
	int ret = thp_project_in_tp(cd);

	if (cd->edit_product_in_project_id)
		ret = thp_edit_product_in_project_id(
			cd, cd->project_id, THP_PROJECT_ID_LEN, cd->product);
	if (cd->support_project_id_mapping)
		ret = thp_project_id_mapping(cd);
	if (ret != 0)
		return -EINVAL;

	return 0;
}

int thp_misc_init(struct thp_core_data *cd)
{
	int rc;
	struct miscdevice *misc_device = NULL;
	char *node = NULL;

	misc_device = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
	if (!misc_device) {
		thp_log_err(cd, "%s: misc_device out of memory\n", __func__);
		return -ENOMEM;
	}

	misc_device->name = THP_MISC_DEVICE_NAME;
	if (cd->sub_solution == THP_SOLUTION) {
		node = kzalloc(sizeof(char) * MULTI_PANEL_NODE_BUF_LEN,
			       GFP_KERNEL);
		if (!node) {
			thp_log_err(cd, "%s:node is null\n", __func__);
			rc = -ENOMEM;
			goto err_register_misc;
		}
		rc = snprintf(node, MULTI_PANEL_NODE_BUF_LEN, "%s%d",
			      THP_MISC_DEVICE_NAME, cd->multi_panel_index);
		if (rc < 0) {
			thp_log_err(cd, "%s: multi snprintf err\n", __func__);
			goto err_register_misc;
		}
		misc_device->name = (const char *)node;
	}

	misc_device->minor = MISC_DYNAMIC_MINOR;
	misc_device->fops = &g_thp_fops;
	rc = misc_register(misc_device);
	if (rc) {
		thp_log_err(cd, "%s: failed to register misc device\n",
			    __func__);
		goto err_register_misc;
	}
	cd->thp_misc_device = misc_device;

	return rc;

err_register_misc:
	if (node)
		kfree(node);
	if (misc_device)
		kfree(misc_device);
	return rc;
}

int thp_misc_exit(struct thp_core_data *cd)
{
	if (cd->thp_misc_device)
		misc_deregister(cd->thp_misc_device);
	if (cd->thp_misc_device->name)
		kfree(cd->thp_misc_device->name);
	if (cd->thp_misc_device)
		kfree(cd->thp_misc_device);
	return 0;
}

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
static int shared_control_gpio_init(struct thp_core_data *cd)
{
	int ret;
	int value = of_get_named_gpio(cd->thp_node, "shared_control_gpio", 0);

	thp_log_info(cd, "shared_control_gpio = %d\n", value);
	if (!gpio_is_valid(value)) {
		thp_log_err(cd, "%s: get shared_control_gpio failed\n",
			    __func__);
		return -EINVAL;
	}
	cd->gpios.shared_control_gpio = value;
	ret = gpio_request(cd->gpios.shared_control_gpio, "thp_shared");
	if (ret) {
		thp_log_err(cd, "%s:gpio_request %d failed\n", __func__, ret);
		cd->gpios.shared_control_gpio = 0;
		return ret;
	}
	return NO_ERR;
}

static int thp_spi_interface(struct spi_message *message)
{
	int ret;
	struct thp_core_data *cd_main = g_thp_core;
	struct thp_core_data *cd_sub = g_thp_core_sub;
	struct thp_core_data *cd_proc = NULL;

	if (!message) {
		thp_log_err(cd_proc, "%s: recv message from ir is null\n",
			    __func__);
		return -EINVAL;
	}
	if (cd_main && (!cd_main->thp_dev) && cd_main->sdev) {
		cd_proc = cd_main;
	} else if (cd_sub && (!cd_sub->thp_dev) && cd_sub->sdev) {
		cd_proc = cd_sub;
	} else if (cd_main && cd_main->thp_dev &&
		   (cd_main->work_status == POWER_OFF_DONE)) {
		cd_proc = cd_main;
	} else if (cd_sub && cd_sub->thp_dev &&
		   (cd_sub->work_status == POWER_OFF_DONE)) {
		cd_proc = cd_sub;
	} else {
		thp_log_err(cd_proc, "%s: cd_main and cd_sub busy or null\n",
			    __func__);
		return -EBUSY;
	}
	thp_log_info(cd_proc, "%s: TP recv message from ir\n", __func__);
	if (!cd_proc->gpios.shared_control_gpio) {
		ret = shared_control_gpio_init(cd_proc);
		if (ret) {
			thp_log_err(cd_proc, "%s: gpio init fail, ret is %d\n",
				    __func__, ret);
			return ret;
		}
	}
	thp_bus_lock(cd_proc);
	gpio_set_value(cd_proc->gpios.shared_control_gpio, GPIO_HIGH);
	ret = thp_spi_sync(cd_proc->sdev, message);
	if (ret < 0) {
		thp_log_err(cd_proc, "%s: spi_sync fail, ret is %d\n", __func__,
			    ret);
		gpio_set_value(cd_proc->gpios.shared_control_gpio, GPIO_LOW);
		thp_bus_unlock(cd_proc);
		return -EIO;
	}
	gpio_set_value(cd_proc->gpios.shared_control_gpio, GPIO_LOW);
	thp_bus_unlock(cd_proc);
	return NO_ERR;
}

static struct tp_spi_interface_ops g_tp_spi_interface = {
	.tp_spi_sync = thp_spi_interface,
};
#endif

#define GOODIX_IC_NAME "goodix"
#define SYNAPTICS_IC_NAME "synaptics"
static int thp_core_init(struct thp_core_data *cd)
{
	int rc;

	/* step 1 : init mutex */
	mutex_init(&cd->mutex_frame);
	mutex_init(&cd->irq_mutex);
	mutex_init(&cd->thp_mutex);
	mutex_init(&cd->status_mutex);
	mutex_init(&cd->suspend_flag_mutex);
	mutex_init(&cd->msgq_mutex);
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
	mutex_init(&cd->aod_power_ctrl_mutex);
#endif
	if (cd->support_gesture_mode)
		mutex_init(&cd->thp_wrong_touch_lock);
	if (cd->support_grip_recognition)
		mutex_init(&cd->grip_event_mutex);
	dev_set_drvdata(&cd->sdev->dev, cd);
	cd->ic_name = cd->thp_dev->ic_name;
	cd->prox_cache_enable = false;
	cd->need_work_in_suspend = false;
	cd->thp_prox_enable = false;
	cd->onetime_poweroff_done = false;
	cd->work_status = RESUME_DONE;
	cd->event_anomaly_count = 0;

#if IS_ENABLED(CONFIG_DSM)
	if (cd->ic_name)
		cd->dsm_thp.ic_name = cd->ic_name;
	if (strlen(cd->project_id))
		cd->dsm_thp.module_name = cd->project_id;
	rc = dsm_update_client_vendor_info(&cd->dsm_thp);
	if (rc)
		thp_log_err(cd, "%s: failed to DSM update vendor info\n",
			    __func__);
#endif
	if (!strcmp(cd->ic_name, GOODIX_IC_NAME))
		cd->ic_type = IC_TYPE_GOODIX;
	else if (!strcmp(cd->ic_name, SYNAPTICS_IC_NAME))
		cd->ic_type = IC_TYPE_SYNA;
	rc = thp_project_init(cd);
	if (rc)
		thp_log_err(cd, "%s: failed to get project id form tp ic\n",
			    __func__);

	rc = thp_misc_init(cd);
	if (rc) {
		thp_log_err(cd, "%s: failed to init misc device\n", __func__);
		goto err_register_misc;
	}

	mutex_lock(&boost_init_mutex);
	if (!g_boost_init_flag) {
		touch_boost_init();
		g_boost_init_flag = 1;
	}
	mutex_unlock(&boost_init_mutex);

	rc = thp_mt_wrapper_init(cd);
	if (rc) {
		thp_log_err(cd, "%s: failed to init input_mt_wrapper\n",
			    __func__);
		goto err_init_wrapper;
	}

	rc = thp_init_sysfs(cd);
	if (rc) {
		thp_log_err(cd, "%s: failed to create sysfs\n", __func__);
		goto err_init_sysfs;
	}

	rc = thp_setup_irq(cd);
	if (rc) {
		thp_log_err(cd, "%s: failed to set up irq\n", __func__);
		goto err_init_sysfs;
	}

	rc = thp_lcd_notify_register(cd);
	if (rc) {
		thp_log_err(cd, "%s: failed to register lcd notify\n",
			    __func__);
		goto err_lcd_register;
	}

	rc = thp_charger_notifier_register(cd);
	if (rc) {
		thp_log_err(cd, "%s: failed to register charger notify\n",
			    __func__);
		goto err_charger_register;
	}
#ifdef CONFIG_HONOR_HW_DEV_DCT
	set_hw_dev_flag(DEV_I2C_TOUCH_PANEL);
#endif

#if defined(CONFIG_TEE_TUI)
	thp_tui_init(cd);
#endif

#ifdef CONFIG_HONOR_SHB_THP
	if (cd->support_shb_thp_log) {
		if (thp_log_init())
			thp_log_err(cd, "%s: failed to init thp log thread\n",
				    __func__);
	} else {
		thp_log_info(cd, "%s: sensorhub thp log is disabled\n",
			     __func__);
	}
#endif
	atomic_set(&cd->register_flag, 1);
	thp_set_status(cd, THP_STATUS_POWER, 1);
	return 0;

err_lcd_register:
	thp_lcd_notify_unregister(cd);
err_charger_register:
	thp_charger_notifier_unregister(cd);
err_init_sysfs:
	thp_mt_wrapper_exit(cd);
err_init_wrapper:
	thp_misc_exit(cd);
err_register_misc:
	mutex_destroy(&cd->mutex_frame);
	mutex_destroy(&cd->irq_mutex);
	mutex_destroy(&cd->thp_mutex);
	mutex_destroy(&cd->aod_status_mutex);
	mutex_destroy(&cd->msgq_mutex);
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
	mutex_destroy(&cd->aod_power_ctrl_mutex);
#endif
	if (cd->support_gesture_mode)
		mutex_destroy(&cd->thp_wrong_touch_lock);
	return rc;
}

static int thp_parse_test_config(struct thp_core_data *cd,
				 struct device_node *test_node,
				 struct thp_test_config *config)
{
	int rc;
	unsigned int value = 0;

	if (!test_node || !config) {
		thp_log_info(cd, "%s: input dev null\n", __func__);
		return -ENODEV;
	}

	rc = of_property_read_u32(test_node, "pt_station_test", &value);
	if (!rc) {
		config->pt_station_test = value;
		thp_log_info(cd, "%s:pt_test_flag %d\n", __func__, value);
	}

	return 0;
}

struct device_node *thp_get_dev_node(struct thp_core_data *cd,
				     struct thp_device *dev)
{
	struct device_node *dev_node =
		of_get_child_by_name(cd->thp_node, dev->ic_name);

	if (!dev_node && dev->dev_node_name)
		return of_get_child_by_name(cd->thp_node, dev->dev_node_name);

	return dev_node;
}

static void thp_chip_detect(struct thp_core_data *cd,
			    struct thp_cmd_node *in_cmd)
{
	int ret;
	struct thp_device *dev = NULL;

	if (in_cmd == NULL) {
		thp_log_err(cd, "%s:input is NULL\n", __func__);
		return;
	}
	dev = in_cmd->cmd_param.pub_params.dev;
	ret = thp_register_dev(cd, dev);
	if (ret)
		thp_log_err(cd, "%s,register failed\n", __func__);
}

static void thp_get_active_doze_ratio(struct thp_core_data *cd)
{
#ifdef CONFIG_HONOR_DUBAI_COMMON
	int ret;
	unsigned long long active_time = 0;
	unsigned long long doze_time = 0;

	if (!cd->thp_dev) {
		thp_log_err(cd, "%s: thp_dev is null\n", __func__);
		return;
	}
	if (cd->work_status == SUSPEND_DONE) {
		if (cd->thp_dev->ops->get_active_doze_time) {
			ret = cd->thp_dev->ops->get_active_doze_time(
				cd->thp_dev, &active_time, &doze_time);
			if (ret) {
				thp_log_err(cd, "%s failed to get ratio :%d\n",
					    __func__, ret);
				return;
			}
			thp_log_info(cd, "active=%llu dozing=%llu", active_time,
				     doze_time);
			HWDUBAI_LOGE("DUBAI_TAG_TP_DURATION",
				     "active=%llu dozing=%llu", active_time,
				     doze_time);
		}
	}
#endif
}

static void thp_lowpower_status_ctrl(struct thp_cmd_node *proc_cmd)
{
	struct thp_core_data *cd = proc_cmd->cmd_param.prv_params;

	if (!cd || !cd->thp_dev) {
		thp_log_err(cd, "%s: cd or thp_dev is null\n", __func__);
		return;
	}
	if (cd->suspended) {
		if (cd->thp_dev->ops && cd->thp_dev->ops->tp_lowpower_ctrl) {
			cd->thp_dev->ops->tp_lowpower_ctrl(
				cd->thp_dev,
				proc_cmd->cmd_param.pub_params.params);
			return;
		}
		thp_log_info(cd, "%s not support tp_lowpower_ctrl", __func__);
		return;
	}
	thp_log_info(cd, "%s not suspend ignore tp_lowpower notify", __func__);
	return;
}

static void thp_fullaod_gesture_support(struct thp_cmd_node *proc_cmd)
{
	struct thp_core_data *cd = proc_cmd->cmd_param.prv_params;

	if (!cd || !cd->thp_dev) {
		thp_log_err(cd, "%s: cd or thp_dev is null\n", __func__);
		return;
	}
	if (cd->suspended) {
		if (cd->thp_dev->ops->tp_fullaod_gesture_support) {
			cd->thp_dev->ops->tp_fullaod_gesture_support(
				cd->thp_dev,
				proc_cmd->cmd_param.pub_params.params);
			return;
		}
		thp_log_info(cd, "%s not support fullaod gesture", __func__);
		return;
	}
	thp_log_info(cd, "%s not suspend ignore fullaod notify", __func__);
	return;
}
void thp_send_detect_cmd(struct thp_core_data *cd, struct thp_device *dev,
			 int timeout)
{
	int error;
	struct thp_cmd_node cmd;

	thp_log_info(cd, "%s: called\n", __func__);
	if (dev == NULL) {
		thp_log_info(cd, "%s: input is invalid\n", __func__);
		return;
	}
	cd->thp_unregister_ic_num++;
	thp_log_info(cd, "%s:thp_unregister_ic_num:%d", __func__,
		     cd->thp_unregister_ic_num);
	memset(&cmd, 0, sizeof(cmd));
	cmd.command = TS_CHIP_DETECT;
	cmd.cmd_param.pub_params.dev = dev;
	error = thp_put_one_cmd(cd, &cmd, timeout);
	if (error)
		thp_log_err(cd, "%s: put cmd error :%d\n", __func__, error);
}

void thp_time_delay(struct thp_core_data *cd, unsigned int time)
{
	if (time == 0)
		return;
	if ((time < TIME_DELAY_MS_MAX) && cd->use_mdelay)
		mdelay(time);
	else
		thp_do_time_delay(time);
}

int thp_register_dev(struct thp_core_data *cd, struct thp_device *dev)
{
	int rc = -EINVAL;

	if ((dev == NULL) || (cd == NULL)) {
		thp_log_err(cd, "%s: input null\n", __func__);
		goto register_err;
	}
	thp_log_info(cd, "%s: called\n", __func__);
	/* check device configed ot not */
	if (!thp_get_dev_node(cd, dev)) {
		thp_log_info(cd, "%s: not config in dts\n", __func__);
		goto register_err;
	}

	if (atomic_read(&cd->register_flag)) {
		thp_log_err(cd, "%s: thp have registerd\n", __func__);
		goto register_err;
	}

	if (!cd->project_in_tp && cd->ic_name && dev->ic_name &&
	    strcmp(cd->ic_name, dev->ic_name)) {
		thp_log_err(cd,
			    "%s:driver support ic mismatch connected device\n",
			    __func__);
		goto register_err;
	}

	dev->thp_core = cd;
	dev->gpios = &cd->gpios;
	dev->sdev = cd->sdev;
	cd->thp_dev = dev;
	cd->is_fw_update = 0;

	rc = thp_parse_timing_config(cd, cd->thp_node, &dev->timing_config);
	if (rc) {
		thp_log_err(cd, "%s: timing config parse fail\n", __func__);
		goto register_err;
	}

	rc = thp_parse_test_config(cd, cd->thp_node, &dev->test_config);
	if (rc) {
		thp_log_err(cd, "%s: special scene config parse fail\n",
			    __func__);
		goto register_err;
	}

	rc = dev->ops->init(dev);
	if (rc) {
		thp_log_err(cd, "%s: dev init fail\n", __func__);
		goto dev_init_err;
	}

	rc = thp_pinctrl_get_init(dev);
	if (rc) {
		thp_log_err(cd, "%s:pinctrl get init fail\n", __func__);
		goto dev_init_err;
	}

	rc = thp_setup_spi(cd);
	if (rc) {
		thp_log_err(cd, "%s: spi dev init fail\n", __func__);
		goto dev_init_err;
	}

	rc = dev->ops->detect(dev);
	if (rc) {
		thp_log_err(cd, "%s: chip detect fail\n", __func__);
		goto dev_init_err;
	}

	rc = thp_pinctrl_select_normal_state(cd);
	if (rc) {
		thp_log_err(cd, "%s:thp_pinctrl_select_normal fail\n",
			    __func__);
		goto dev_init_err;
	}

	rc = thp_core_init(cd);
	if (rc) {
		thp_log_err(cd, "%s: core init\n", __func__);
		goto dev_init_err;
	}
	if (cd->fast_booting_solution) {
		cd->thp_unregister_ic_num--;
		thp_log_info(cd, "%s:thp_unregister_ic_num :%d", __func__,
			     cd->thp_unregister_ic_num);
	}
	if (cd->use_dual_spi_for_pen) {
		rc = g_thp_pen_spi_init();
		if (rc != 0)
			thp_log_err(cd, "pen device register failed!");
	}
	return 0;
dev_init_err:
	cd->thp_dev = 0;
register_err:
	if (cd && cd->fast_booting_solution) {
		cd->thp_unregister_ic_num--;
		thp_log_info(cd, "%s:thp_unregister_ic_num :%d", __func__,
			     cd->thp_unregister_ic_num);
#if IS_ENABLED(CONFIG_DSM)
		if (!cd->thp_unregister_ic_num &&
		    !atomic_read(&cd->register_flag)) {
			thp_log_err(cd,
				    "%s:ALL TP IC register fail, DMD code: %d",
				    __func__,
				    DSM_TPHOSTPROCESSING_DEV_IC_ALL_FAIL);
			thp_dmd_report(cd, DSM_TPHOSTPROCESSING_DEV_IC_ALL_FAIL,
				       "%s, ALL TP IC register fail \n",
				       __func__);
		}
#endif
	}
	return rc;
}
// EXPORT_SYMBOL(thp_register_dev);

int thp_parse_spi_config(struct device_node *spi_cfg_node,
			 struct thp_core_data *cd)
{
	int rc;
	unsigned int value;
	struct thp_spi_config *spi_config = NULL;
	struct pl022_config_chip *pl022_spi_config = NULL;

	if (!spi_cfg_node || !cd) {
		thp_log_info(cd, "%s: input null\n", __func__);
		return -ENODEV;
	}

	spi_config = &cd->spi_config;
	pl022_spi_config = &cd->spi_config.pl022_spi_config;

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "spi-max-frequency", &value);
	if (!rc) {
		spi_config->max_speed_hz = value;
		thp_log_info(cd, "%s:spi-max-frequency configed %d\n", __func__,
			     value);
	}

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "spi-bus-id", &value);
	if (!rc) {
		spi_config->bus_id = (u8)value;
		thp_log_info(cd, "%s:spi-bus-id configed %d\n", __func__,
			     value);
	}

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "spi-mode", &value);
	if (!rc) {
		spi_config->mode = value;
		thp_log_info(cd, "%s:spi-mode configed %d\n", __func__, value);
	}

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "bits-per-word", &value);
	if (!rc) {
		spi_config->bits_per_word = value;
		thp_log_info(cd, "%s:bits-per-word configed %d\n", __func__,
			     value);
	}

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "pl022,interface", &value);
	if (!rc) {
		pl022_spi_config->iface = value;
		thp_log_info(cd, "%s: pl022,interface parsed\n", __func__);
	}
	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "pl022,com-mode", &value);
	if (!rc) {
		pl022_spi_config->com_mode = value;
		thp_log_info(cd, "%s:com_mode parsed\n", __func__);
	}
	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "pl022,rx-level-trig", &value);
	if (!rc) {
		pl022_spi_config->rx_lev_trig = value;
		thp_log_info(cd, "%s:rx-level-trig parsed\n", __func__);
	}

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "pl022,tx-level-trig", &value);
	if (!rc) {
		pl022_spi_config->tx_lev_trig = value;
		thp_log_info(cd, "%s:tx-level-trig parsed\n", __func__);
	}

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "pl022,ctrl-len", &value);
	if (!rc) {
		pl022_spi_config->ctrl_len = value;
		thp_log_info(cd, "%s:ctrl-len parsed\n", __func__);
	}

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "pl022,wait-state", &value);
	if (!rc) {
		pl022_spi_config->wait_state = value;
		thp_log_info(cd, "%s:wait-state parsed\n", __func__);
	}

	value = 0;
	rc = of_property_read_u32(spi_cfg_node, "pl022,duplex", &value);
	if (!rc) {
		pl022_spi_config->duplex = value;
		thp_log_info(cd, "%s:duplex parsed\n", __func__);
	}
#if (!IS_ENABLED(CONFIG_TP_QCOM_8550))
	if (cd->multi_panel_index == SUB_TOUCH_PANEL)
		cd->spi_config.pl022_spi_config.cs_control = thp_spi_cs_set_sub;
	else
		cd->spi_config.pl022_spi_config.cs_control =
			thp_spi_cs_set_master;
#endif
	cd->spi_config.pl022_spi_config.hierarchy = SSP_MASTER;

	if (!cd->spi_config.max_speed_hz)
		cd->spi_config.max_speed_hz = THP_SPI_SPEED_DEFAULT;
	if (!cd->spi_config.mode)
		cd->spi_config.mode = SPI_MODE_0;
	if (!cd->spi_config.bits_per_word)
		/* spi_config.bits_per_word default value is 8 */
		cd->spi_config.bits_per_word = 8;

#if (IS_ENABLED(CONFIG_HONOR_THP_MTK))
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	/* tx ordering, 1-msb first send; 0-lsb first end */
	cd->mtk_spi_config.rx_mlsb = 1;
	/* rx ordering, 1-msb first send; 0-lsb first end */
	cd->mtk_spi_config.tx_mlsb = 1;
	/* control cs polarity, 0-active low; 1-active high */
	cd->mtk_spi_config.cs_pol = 0;
	/* control sample edge, 0-positive edge; 1-negative edge */
	cd->mtk_spi_config.sample_sel = 0;
	/* cs setup time, 0-default time */
	cd->mtk_spi_config.cs_setuptime = 0;
#else
	/* control sample edge, 0-positive edge; 1-negative edge */
	cd->mtk_spi_config.sample_sel = 0;
	/* cs setup time, 0-default time */
	cd->mtk_spi_config.cs_setuptime = 0;
#endif
#endif
	cd->sdev->mode = spi_config->mode;
	cd->sdev->max_speed_hz = spi_config->max_speed_hz;
	cd->sdev->bits_per_word = spi_config->bits_per_word;
#if (!(IS_ENABLED(CONFIG_HONOR_THP_MTK)))
	cd->sdev->controller_data = &spi_config->pl022_spi_config;
#else
	cd->sdev->controller_data = (void *)&(cd->mtk_spi_config);
#endif

	return 0;
}
EXPORT_SYMBOL(thp_parse_spi_config);

int thp_parse_timing_config(struct thp_core_data *cd,
			    struct device_node *timing_cfg_node,
			    struct thp_timing_config *timing)
{
	int rc;
	unsigned int value;

	if (!timing_cfg_node || !timing) {
		thp_log_info(cd, "%s: input null\n", __func__);
		return -ENODEV;
	}

	rc = of_property_read_u32(timing_cfg_node, "boot_reset_hi_delay_ms",
				  &value);
	if (!rc) {
		timing->boot_reset_hi_delay_ms = value;
		thp_log_info(cd, "%s:boot_reset_hi_delay_ms configed %d\n",
			     __func__, value);
	}

	rc = of_property_read_u32(timing_cfg_node, "boot_reset_low_delay_ms",
				  &value);
	if (!rc) {
		timing->boot_reset_low_delay_ms = value;
		thp_log_info(cd, "%s:boot_reset_low_delay_ms configed %d\n",
			     __func__, value);
	}

	rc = of_property_read_u32(timing_cfg_node, "boot_reset_after_delay_ms",
				  &value);
	if (!rc) {
		timing->boot_reset_after_delay_ms = value;
		thp_log_info(cd, "%s:boot_reset_after_delay_ms configed %d\n",
			     __func__, value);
	}

	rc = of_property_read_u32(timing_cfg_node,
				  "resume_reset_after_delay_ms", &value);
	if (!rc) {
		timing->resume_reset_after_delay_ms = value;
		thp_log_info(cd, "%s:resume_reset_after_delay_ms configed %d\n",
			     __func__, value);
	}

	rc = of_property_read_u32(timing_cfg_node,
				  "suspend_reset_after_delay_ms", &value);
	if (!rc) {
		timing->suspend_reset_after_delay_ms = value;
		thp_log_info(cd,
			     "%s:suspend_reset_after_delay configed_ms %d\n",
			     __func__, value);
	}

	rc = of_property_read_u32(timing_cfg_node, "spi_sync_cs_hi_delay_ns",
				  &value);
	if (!rc) {
		timing->spi_sync_cs_hi_delay_ns = value;
		thp_log_info(cd, "%s:spi_sync_cs_hi_delay_ns configed_ms %d\n",
			     __func__, value);
	}

	rc = of_property_read_u32(timing_cfg_node, "spi_sync_cs_low_delay_ns",
				  &value);
	if (!rc) {
		timing->spi_sync_cs_low_delay_ns = value;
		thp_log_info(cd, "%s:spi_sync_cs_low_delay_ns configed_ms %d\n",
			     __func__, value);
	}
	rc = of_property_read_u32(timing_cfg_node, "spi_sync_cs_low_delay_us",
				  &value);
	if (!rc) {
		timing->spi_sync_cs_low_delay_us = value;
		thp_log_info(cd, "%s:spi_sync_cs_low_delay_us = %d\n", __func__,
			     value);
	} else {
		timing->spi_sync_cs_low_delay_us = 0;
	}

	rc = of_property_read_u32(timing_cfg_node, "boot_vcc_on_after_delay_ms",
				  &value);
	if (!rc) {
		timing->boot_vcc_on_after_delay_ms = value;
		thp_log_info(cd,
			     "%s:boot_vcc_on_after_delay_ms configed_ms %d\n",
			     __func__, value);
	}
	rc = of_property_read_u32(timing_cfg_node,
				  "boot_vddio_on_after_delay_ms", &value);
	if (!rc) {
		timing->boot_vddio_on_after_delay_ms = value;
		thp_log_info(cd,
			     "%s:boot_vddio_on_after_delay_ms configed_ms %d\n",
			     __func__, value);
	}
	rc = of_property_read_u32(timing_cfg_node, "spi_transfer_delay_us",
				  &value);
	if (!rc) {
		timing->spi_transfer_delay_us = value;
		thp_log_info(cd, "%s:spi_transfer_delay_us = %d\n", __func__,
			     value);
	} else {
		timing->spi_transfer_delay_us = 0;
	}
	if (!of_property_read_u32(timing_cfg_node, "early_suspend_delay_ms",
				  &value)) {
		timing->early_suspend_delay_ms = value;
		thp_log_info(cd, "%s:early_suspend_delay_ms configed_ms %u\n",
			     __func__, value);
	}
	return 0;
}
EXPORT_SYMBOL(thp_parse_timing_config);

int thp_parse_trigger_config(struct device_node *thp_node,
			     struct thp_core_data *cd)
{
	int rc;
	unsigned int value = 0;

	thp_log_debug(cd, "%s:Enter!\n", __func__);
	rc = of_property_read_u32(thp_node, "irq_flag", &value);
	if (!rc) {
		cd->irq_flag = value;
		thp_log_info(cd, "%s:cd->irq_flag %d\n", __func__, value);
	} else {
		cd->irq_flag = IRQF_TRIGGER_FALLING;
		thp_log_info(cd, "%s:cd->irq_flag defaule => %d\n", __func__,
			     cd->irq_flag);
	}
	return 0;
}
EXPORT_SYMBOL(thp_parse_trigger_config);

static void thp_parse_extra_feature_config(struct device_node *thp_node,
					   struct thp_core_data *cd)
{
	int rc;
	unsigned int value = 0;

	rc = of_property_read_u32(thp_node, "get_spi_hw_info_enable", &value);
	if (!rc) {
		cd->get_spi_hw_info_enable = value;
		thp_log_info(cd, "%s: get_spi_hw_info_enable %u\n", __func__,
			     value);
	}

	rc = of_property_read_u32(thp_node, "support_factory_mode_extra_cmd",
				  &value);
	if (!rc) {
		cd->support_factory_mode_extra_cmd = value;
		thp_log_info(cd, "%s: support_factory_mode_extra_cmd %u\n",
			     __func__, value);
	}
	rc = of_property_read_u32(thp_node, "support_b_protocol", &value);
	if (!rc) {
		cd->support_b_protocol = value;
		thp_log_info(cd, "%s: support_b_protocol %u\n", __func__,
			     value);
	}
	rc = of_property_read_u32(thp_node, "support_grip_recognition", &value);
	if (!rc) {
		cd->support_grip_recognition = value;
		thp_log_info(cd, "%s: support_grip_recognition %u\n", __func__,
			     value);
	}
	rc = of_property_read_u32(thp_node, "support_irq_storm_protect",
				  &value);
	if (!rc) {
		cd->support_irq_storm_protect = value;
		thp_log_info(cd, "%s: support_irq_storm_protect %u\n", __func__,
			     value);
	}

	cd->support_control_cs_off = 0;
	rc = of_property_read_u32(thp_node, "support_control_cs_off", &value);
	if (!rc) {
		cd->support_control_cs_off = value;
		thp_log_info(cd, "%s:support_control_cs_off %u\n", __func__,
			     value);
	}

	if (of_find_property(thp_node, "honor-udp", NULL))
		cd->is_udp = true;
	else
		cd->is_udp = false;

	cd->sub_solution = TSKIT_SOLUTION;
	if (!of_property_read_u32(thp_node, "sub_solution", &value))
		cd->sub_solution = value;
	thp_log_info(cd, "%s: sub_solution %u\n", __func__, cd->sub_solution);

	rc = of_property_read_u32(thp_node, "support_shb_thp_tui_common_id",
				  &value);
	if (!rc) {
		cd->support_shb_thp_tui_common_id = value;
		thp_log_info(cd, "%s: support_shb_thp_tui_common_id %u\n",
			     __func__, value);
	}

	rc = of_property_read_u32(thp_node, "support_diff_resolution", &value);
	if (!rc) {
		cd->support_diff_resolution = value;
		thp_log_info(cd, "%s: support_diff_resolution %u\n", __func__,
			     value);
	}

	rc = of_property_read_u32(thp_node, "need_notify_to_roi_algo", &value);
	if (!rc) {
		cd->need_notify_to_roi_algo = value;
		thp_log_info(cd, "%s: need_notify_to_roi_algo %u\n", __func__,
			     value);
	}

	rc = of_property_read_u32(thp_node, "support_daemon_init_protect",
				  &value);
	if (!rc) {
		cd->support_daemon_init_protect = value;
		thp_log_info(cd, "%s: support_daemon_init_protect %u\n",
			     __func__, value);
	}
	cd->support_reuse_ic_type = 0;
	rc = of_property_read_u32(thp_node, "support_reuse_ic_type", &value);
	if (!rc) {
		cd->support_reuse_ic_type = value;
		thp_log_info(cd, "%s: support_reuse_ic_type %u\n", __func__,
			     value);
	}
	rc = of_property_read_u32(thp_node, "use_thp_queue", &value);
	if (!rc) {
		cd->use_thp_queue = value;
		thp_log_info(cd, "%s: use_thp_queue %u\n", __func__, value);
	}
	cd->thp_queue_buf_len = THP_QUEUE_NODE_BUFF_MAX_LEN;
	rc = of_property_read_u32(thp_node, "thp_queue_buf_len", &value);
	if (!rc) {
		cd->thp_queue_buf_len = value;
		thp_log_info(cd, "%s: thp_queue_buf_len %u\n", __func__, value);
	}
	rc = of_property_read_u32(thp_node, "supported_charger", &value);
	if (!rc) {
		cd->supported_charger = value;
		thp_log_info(cd, "%s:supported_charger %u\n", __func__, value);
	}

	rc = of_property_read_u32(thp_node, "support_suspend_cs_low", &value);
	if (!rc) {
		cd->support_suspend_cs_low = value;
		thp_log_info(cd, "%s:support_suspend_cs_low configed: %u\n",
			     __func__, value);
	}

	cd->res_scale = 1;
	cd->res_scale_pen = 1;
	if (!recovery_mode) {
		rc = of_property_read_u32(thp_node,
					  "support_dynamic_resolution", &value);
		if (!rc) {
			cd->support_dynamic_resolution = value;
			thp_log_info(
				cd,
				"%s:support_dynamic_resolution configed: %u\n",
				__func__, value);
		}
		if (cd->support_dynamic_resolution) {
			rc = of_property_read_u32(thp_node, "res_scale",
						  &value);
			if (!rc) {
				if (thp_is_factory())
					cd->res_scale = 1;
				else
					cd->res_scale = value;
				thp_log_info(cd, "%s:res_scale:%u\n", __func__,
					     cd->res_scale);
			}
			rc = of_property_read_u32(thp_node, "res_scale_pen",
						  &value);
			if (!rc) {
				if (thp_is_factory())
					cd->res_scale_pen = 1;
				else
					cd->res_scale_pen = value;
				thp_log_info(cd, "%s:res_scale_pen:%u\n",
					     __func__, cd->res_scale_pen);
			}
		}
	}

	cd->lowpower_support = 1;
	rc = of_property_read_u32(thp_node, "lowpower_support", &value);
	if (!rc) {
		cd->lowpower_support = value;
		thp_log_info(cd, "%s: lowpower_support = %d\n", __func__,
			     value);
	}

	cd->support_pen_wakeup_gesture = 0;
	rc = of_property_read_u32(thp_node, "support_pen_wakeup_gesture",
				  &value);
	if (!rc) {
		cd->support_pen_wakeup_gesture = value;
		thp_log_info(cd, "%s:support_pen_wakeup_gesture %u\n", __func__,
			     value);
	}

	cd->use_dual_spi_for_pen = 0;
	rc = of_property_read_u32(thp_node, "use_dual_spi_for_pen", &value);
	if (!rc) {
		cd->use_dual_spi_for_pen = value;
		thp_log_info(cd, "%s:use_dual_spi_for_pen %u\n", __func__,
			     value);
	}

	cd->support_get_project_id_late = 0;
	rc = of_property_read_u32(thp_node, "support_get_project_id_late",
				  &value);
	if (!rc) {
		cd->support_get_project_id_late = value;
		thp_log_info(cd, "%s:support_get_project_id_late %u\n",
			     __func__, value);
	}
}

static void thp_parse_project_id_map_config(struct device_node *thp_node,
					    struct thp_core_data *cd)
{
	unsigned int value;

	cd->support_project_id_mapping = 0;
	if (!of_property_read_u32(thp_node, "support_project_id_mapping",
				  &value)) {
		cd->support_project_id_mapping = value;
		thp_log_info(cd, "%s: support_project_id_mapping %u\n",
			     __func__, value);
	}
	if (!(cd->support_project_id_mapping))
		return;
	cd->target_project_id = NULL;
	cd->map_project_id = NULL;
	if (!of_property_read_string(thp_node, "target_project_id",
				     (const char **)&cd->target_project_id))
		thp_log_info(cd, "%s: target_project_id %s\n", __func__,
			     cd->target_project_id);
	if (!of_property_read_string(thp_node, "map_project_id",
				     (const char **)&cd->map_project_id))
		thp_log_info(cd, "%s: map_project_id %s\n", __func__,
			     cd->map_project_id);
}

int thp_parse_feature_config(struct device_node *thp_node,
			     struct thp_core_data *cd)
{
	int rc;
	unsigned int value = 0;

	thp_log_debug(cd, "%s:Enter!\n", __func__);
	rc = of_property_read_u32(thp_node, "need_huge_memory_in_spi", &value);
	if (!rc) {
		cd->need_huge_memory_in_spi = value;
		thp_log_info(cd, "%s:need_huge_memory_in_spi configed %u\n",
			     __func__, value);
	}
	rc = of_property_read_u32(thp_node, "self_control_power", &value);
	if (!rc) {
		cd->self_control_power = value;
		thp_log_info(cd, "%s:self_control_power configed %u\n",
			     __func__, value);
	}
	rc = of_property_read_u32(thp_node, "project_in_tp", &value);
	if (!rc) {
		cd->project_in_tp = value;
		thp_log_info(cd, "%s:project_in_tp: %u\n", __func__, value);
	}

	cd->project_id_dummy = "dummy";
	rc = of_property_read_string(thp_node, "project_id_dummy",
				     (const char **)&cd->project_id_dummy);
	if (!rc)
		thp_log_info(cd, "%s:project_id_dummy configed %s\n", __func__,
			     cd->project_id_dummy);

	rc = of_property_read_u32(thp_node, "supported_func_indicater", &value);
	if (!rc) {
		cd->supported_func_indicater = value;
		thp_log_info(cd, "%s:supported_func_indicater configed %u\n",
			     __func__, value);
	}

	rc = of_property_read_u32(thp_node, "use_hwlock", &value);
	if (!rc) {
		cd->use_hwlock = value;
		thp_log_info(cd, "%s:use_hwlock configed %u\n", __func__,
			     value);
	}

	rc = of_property_read_u32(thp_node, "support_shb_thp", &value);
	if (!rc) {
		cd->support_shb_thp = value;
		thp_log_info(cd, "%s:support_shb_thp configed %u\n", __func__,
			     value);
	}

	rc = of_property_read_u32(thp_node, "support_shb_thp_log", &value);
	if (!rc) {
		cd->support_shb_thp_log = value;
		thp_log_info(cd, "%s:support_shb_thp_log configed %u\n",
			     __func__, value);
	}

	rc = of_property_read_u32(thp_node, "support_shb_thp_app_switch",
				  &value);
	if (!rc) {
		cd->support_shb_thp_app_switch = value;
		thp_log_info(cd, "%s:support_shb_thp_app_switch configed %u\n",
			     __func__, value);
	}

	rc = of_property_read_u32(thp_node, "delay_work_for_pm", &value);
	if (!rc) {
		cd->delay_work_for_pm = value;
		thp_log_info(cd, "%s:delay_work_for_pm configed %u\n", __func__,
			     value);
	}
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
	rc = of_property_read_u32(thp_node, "use_ap_gesture", &value);
	if (!rc) {
		cd->use_ap_gesture = value;
		thp_log_info(cd, "%s:use_ap_gesture configed %u\n", __func__,
			     value);
	}
	rc = of_property_read_u32(thp_node, "use_aod_power_ctrl_notify",
				  &value);
	if (!rc) {
		cd->use_aod_power_ctrl_notify = value;
		thp_log_info(cd, "%s:use_aod_power_ctrl_notify config %u\n",
			     __func__, value);
	}
	if (cd->use_aod_power_ctrl_notify) {
		if (!of_property_read_u32(thp_node,
					  "suspend_delayms_early_to_before",
					  &value)) {
			cd->suspend_delayms_early_to_before = value;
			thp_log_info(cd,
				     "%s:suspend_delayms_early_to_before %u\n",
				     __func__, value);
		}
		if (!of_property_read_u32(thp_node,
					  "suspend_delayms_before_to_later",
					  &value)) {
			cd->suspend_delayms_before_to_later = value;
			thp_log_info(cd,
				     "%s:suspend_delayms_before_to_later %u\n",
				     __func__, value);
		}
		if (!of_property_read_u32(thp_node,
					  "resume_delayms_early_to_later",
					  &value)) {
			cd->resume_delayms_early_to_later = value;
			thp_log_info(cd,
				     "%s:resume_delayms_early_to_later %u\n",
				     __func__, value);
		}
	}
#endif
	rc = of_property_read_u32(thp_node, "support_gesture_mode", &value);
	if (!rc) {
		if (thp_is_factory())
			value = 0;
		cd->support_gesture_mode = value;
		thp_log_info(cd, "%s:support_gesture_mode configed %u\n",
			     __func__, cd->support_gesture_mode);
		rc = of_property_read_u32(thp_node, "gesture_from_sensrohub",
					  &value);
		if (!rc) {
			cd->gesture_from_sensorhub = value;
			thp_log_info(cd,
				     "%s:gesture from sensorhub configed %u\n",
				     __func__, value);
		}
	}

	rc = of_property_read_u32(thp_node, "aod_display_support", &value);
	if (!rc) {
		cd->aod_display_support = value;
		thp_log_info(cd, "%s:aod_display_support configed %u\n",
			     __func__, value);
	}

	rc = of_property_read_u32(thp_node, "tsa_event_to_udfp", &value);
	if (!rc) {
		cd->tsa_event_to_udfp = value;
		thp_log_info(cd, "%s:tsa_event_to_udfp configed %u\n", __func__,
			     value);
	}

	if (!of_property_read_u32(thp_node, "lcd_gesture_mode_support",
				  &value)) {
		cd->lcd_gesture_mode_support = value;
		thp_log_info(cd, "%s:lcd_gesture_mode_support configed %u\n",
			     __func__, value);
	}
	/*
	 * TDDI(TP powered by LCD) download fw in afe screen on,
	 * need wait interruptible to make sure of screen on done.
	 */
	if (!of_property_read_u32(thp_node, "wait_afe_screen_on_support",
				  &value)) {
		cd->wait_afe_screen_on_support = value;
		thp_log_info(cd, "%s:wait_afe_screen_on_support configed %u\n",
			     __func__, value);
	}

#if defined(CONFIG_TEE_TUI)
	if (!of_property_read_u32(thp_node, "send_tui_exit_in_suspend",
				  &value)) {
		cd->send_tui_exit_in_suspend = value;
		thp_log_info(cd, "%s:send_tui_exit_in_suspend configed %u\n",
			     __func__, value);
	}
	if (!of_property_read_u32(thp_node, "no_need_secos_bus_init", &value)) {
		cd->no_need_secos_bus_init = value;
		thp_log_info(cd, "%s:no_need_secos_bus_init configed %u\n",
			     __func__, value);
	}
#endif

	rc = of_property_read_u32(thp_node, "support_ring_feature", &value);
	if (!rc) {
		cd->support_ring_feature = value;
		/* open or close ring switch by user, this is initial value */
		cd->ring_setting_switch = value;
		thp_log_info(cd, "%s:support_ring_feature configed %u\n",
			     __func__, value);
	}

	rc = of_property_read_u32(thp_node, "support_ring_setting_switch",
				  &value);
	if (!rc) {
		cd->support_ring_setting_switch = value;
		thp_log_info(cd, "%s:support_ring_setting_switch configed %u\n",
			     __func__, value);
	}

	rc = of_property_read_u32(thp_node, "support_fingerprint_switch",
				  &value);
	if (!rc) {
		cd->support_fingerprint_switch = value;
		thp_log_info(cd, "%s:support_fingerprint_switch configed %u\n",
			     __func__, value);
	}
	rc = of_property_read_u32(thp_node, "pen_mt_enable_flag", &value);
	if (!rc) {
		cd->pen_mt_enable_flag = value;
		thp_log_info(cd, "%s:pen_mt_enable_flag configed %u\n",
			     __func__, value);
	}
	rc = of_property_read_u32(thp_node, "support_extra_key_event_input",
				  &value);
	if (!rc) {
		cd->support_extra_key_event_input = value;
		thp_log_info(cd,
			     "%s:support_extra_key_event_input configed %u\n",
			     __func__, value);
	}
	rc = of_property_read_u32(thp_node, "hide_product_info_en", &value);
	if (!rc) {
		if (thp_is_factory())
			value = 0;
		cd->hide_product_info_en = value;
		thp_log_info(cd, "%s:hide_product_info_en %u\n", __func__,
			     cd->hide_product_info_en);
	}
	rc = of_property_read_u32(thp_node, "support_oem_info", &value);
	if (!rc) {
		cd->support_oem_info = value;
		thp_log_info(cd, "%s:support_oem_info %u\n", __func__, value);
	}

	rc = of_property_read_u32(thp_node, "projectid_from_panel_ver", &value);
	if (!rc) {
		cd->projectid_from_panel_ver = value;
		thp_log_info(cd, "%s:projectid_from_panel_ver %u\n", __func__,
			     value);
	}

	cd->support_vendor_ic_type = 0;
	rc = of_property_read_u32(thp_node, "support_vendor_ic_type", &value);
	if (!rc) {
		cd->support_vendor_ic_type = value;
		thp_log_info(cd, "%s:support_vendor_ic_type %u\n", __func__,
			     value);
	}

	rc = of_property_read_u32(thp_node, "pen_supported", &value);
	if (!rc) {
		cd->pen_supported = value;
		thp_log_info(cd, "%s:pen_supported %u\n", __func__, value);
	}
	rc = of_property_read_u32(thp_node, "spi_autosleep_short_delay",
				  &value);
	if (!rc) {
		cd->spi_autosleep_short_delay = value;
		thp_log_info(cd, "%s:spi_autosleep_short_delay %u\n", __func__,
			     value);
	}
	rc = of_property_read_u32(thp_node, "touchpad_supported", &value);
	if (!rc) {
		cd->touchpad_supported = value;
		thp_log_info(cd, "%s:touchpad_supported %u\n", __func__, value);
	}
	rc = of_property_read_u32(thp_node, "support_get_active_doze_time",
				  &value);
	if (!rc) {
		cd->support_get_active_doze_time = value;
		thp_log_info(cd, "%s:support_get_active_doze_time %u\n",
			     __func__, value);
	}
	rc = of_property_read_u32(thp_node, "pen_change_protocol", &value);
	if (!rc) {
		cd->pen_change_protocol = value;
		thp_log_info(cd, "%s:pen_change_protocol %u\n", __func__,
			     value);
	}
	cd->edit_product_in_project_id = 0;
	if (!of_property_read_u32(thp_node, "edit_product_in_project_id",
				  &value)) {
		cd->edit_product_in_project_id = value;
		thp_log_info(cd, "%s: edit_product_in_project_id %u\n",
			     __func__, value);
	}
	if (cd->edit_product_in_project_id) {
		cd->product = NULL;
		if (!of_property_read_string(thp_node, "product",
					     (const char **)&cd->product))
			thp_log_info(cd, "%s:product configed %s\n", __func__,
				     cd->product);
	}
	cd->need_resume_reset = 0;
	rc = of_property_read_u32(thp_node, "need_resume_reset", &value);
	if (!rc) {
		cd->need_resume_reset = value;
		thp_log_info(cd, "%s:need_resume_reset %u\n", __func__, value);
	}
	cd->support_dual_chip_arch = 0;
	if (!of_property_read_u32(thp_node, "support_dual_chip_arch", &value)) {
		cd->support_dual_chip_arch = value;
		thp_log_info(cd, "%s: support_dual_chip_arch %u\n", __func__,
			     value);
	}

	thp_parse_project_id_map_config(thp_node, cd);

#if (IS_ENABLED(CONFIG_HONOR_THP_MTK))
	value = 0;
	cd->change_spi_driving_force = 0;
	rc = of_property_read_u32(thp_node, "change_spi_driving_force", &value);
	if (!rc) {
		cd->change_spi_driving_force = value;
		thp_log_info(cd, "%s:change_spi_driving_force %u\n", __func__,
			     value);
	}
#endif
	cd->gesture_retry_times = 20; /* defult gesture retry times:20 */
	rc = of_property_read_u32(thp_node, "gesture_retry_times", &value);
	if (!rc) {
		cd->gesture_retry_times = value;
		thp_log_info(cd, "%s:gesture_retry_times %u\n", __func__,
			     value);
	}
	cd->lcd_need_get_afe_status = 0;
	if (!of_property_read_u32(thp_node, "lcd_need_get_afe_status",
				  &value)) {
		cd->lcd_need_get_afe_status = value;
		thp_log_info(cd, "%s: lcd_need_get_afe_status %u\n", __func__,
			     value);
	}
	rc = of_property_read_u32(thp_node, "send_bt_status_to_fw", &value);
	if (!rc) {
		cd->send_bt_status_to_fw = value;
		thp_log_info(cd, "%s: send_bt_status_to_fw %u\n", __func__,
			     value);
	}
	rc = of_property_read_u32(thp_node,
				  "support_report_interval_adjustment", &value);
	if (!rc) {
		cd->support_interval_adjustment = value;
		rc = of_property_read_u32(thp_node, "time_interval", &value);
		if (!rc) {
			cd->time_interval = value;
			thp_log_info(cd, "%s:support_adjustment %u\n", __func__,
				     cd->time_interval);
		} else {
			cd->support_interval_adjustment = 0;
		}
		rc = of_property_read_u32(thp_node, "time_min_interval",
					  &value);
		if (!rc) {
			cd->time_min_interval = value;
			thp_log_info(cd, "%s:support_adjustment %u\n", __func__,
				     cd->time_min_interval);
		} else {
			cd->support_interval_adjustment = 0;
		}
	}
	rc = of_property_read_u32(thp_node, "support_screen_switch", &value);
	if (!rc) {
		cd->support_screen_switch = value;
		thp_log_info(cd, "%s: support_screen_switch %u\n", __func__,
			     value);
	}

	rc = of_property_read_u32(thp_node, "iovdd_power_on_delay_ms", &value);
	if (!rc) {
		cd->iovdd_power_on_delay_ms = value;
		thp_log_info(cd, "%s:iovdd_power_on_delay_ms %u\n", __func__,
			     cd->iovdd_power_on_delay_ms);
	} else {
		cd->iovdd_power_on_delay_ms = 1; /* default delay 1 ms */
	}

	rc = of_property_read_u32(thp_node, "is_fold_device", &value);
	if (!rc) {
		cd->is_fold_device = value;
		thp_log_info(cd, "%s: is_fold_device = %d\n", __func__, value);
	}

	rc = of_property_read_u32(thp_node, "irq_boost_touch", &value);
	if (!rc) {
		cd->irq_boost_touch = value;
		thp_log_info(cd, "%s: irq_boost_touch = %d\n", __func__, value);
	}
	return 0;
}
EXPORT_SYMBOL(thp_parse_feature_config);

int is_tp_detected(struct thp_core_data *cd)
{
	int ret = TP_DETECT_SUCC;

	if (!cd) {
		thp_log_err(cd, "%s: thp_core_data is not inited\n", __func__);
		return TP_DETECT_FAIL;
	}
	if (!atomic_read(&cd->register_flag))
		ret = TP_DETECT_FAIL;

	thp_log_info(
		cd,
		"[Proximity_feature] %s : Check if tp is in place, ret = %d\n",
		__func__, ret);
	return ret;
}

#define THP_PRINT_BYTES_PER_LINE 20
void thp_print_buf(struct thp_core_data *cd, unsigned char *buf, int buf_len)
{
	/* print 20 bytes per line */
	int i = 0;
	int line_count = buf_len / THP_PRINT_BYTES_PER_LINE;
	int last_buf_len = buf_len % THP_PRINT_BYTES_PER_LINE;

	for (i = 0; i < line_count; i++) {
		thp_log_info(cd, "tp_debug_info[%d~%d] %*ph\n",
			     i * THP_PRINT_BYTES_PER_LINE,
			     (i + 1) * THP_PRINT_BYTES_PER_LINE - 1,
			     THP_PRINT_BYTES_PER_LINE,
			     buf + i * THP_PRINT_BYTES_PER_LINE);
	}

	if (last_buf_len != 0)
		thp_log_info(cd, "tp_debug_info[%d~%d] %*ph\n",
			     line_count * THP_PRINT_BYTES_PER_LINE, buf_len - 1,
			     last_buf_len,
			     buf + line_count * THP_PRINT_BYTES_PER_LINE);
}

static int thp_parse_config(struct thp_core_data *cd,
			    struct device_node *thp_node)
{
	int rc;
	unsigned int value;

	if (!thp_node) {
		thp_log_err(cd, "%s:thp not config in dts, exit\n", __func__);
		return -ENODEV;
	}

	cd->multi_panel_index = SINGLE_TOUCH_PANEL;
	if (!of_property_read_u32(thp_node, "multi_panel_index", &value))
		cd->multi_panel_index = value;
	thp_log_info(cd, "%s: multi_panel_index %u\n", __func__,
		     cd->multi_panel_index);

	rc = thp_parse_spi_config(thp_node, cd);
	if (rc) {
		thp_log_err(cd, "%s: spi config parse fail\n", __func__);
		return rc;
	}

	rc = thp_parse_power_config(thp_node, cd);
	if (rc) {
		thp_log_err(cd, "%s: power config parse fail\n", __func__);
		return rc;
	}

	cd->irq_flag = IRQF_TRIGGER_FALLING;
	rc = of_property_read_u32(thp_node, "irq_flag", &value);
	if (!rc) {
		cd->irq_flag = value;
		thp_log_info(cd, "%s:irq_flag parsed\n", __func__);
	}
	cd->fast_booting_solution = 0;
	rc = of_property_read_u32(thp_node, "fast_booting_solution", &value);
	if (!rc) {
		cd->fast_booting_solution = value;
		thp_log_info(cd, "%s:fast_booting_solution parsed:%d\n",
			     __func__, cd->fast_booting_solution);
	}
	rc = of_property_read_u32(thp_node, "suspend_resume_control", &value);
	if (!rc) {
		cd->suspend_resume_control = value;
		thp_log_info(cd, "%s:suspend_resume_control parsed:%d\n",
			     __func__, cd->suspend_resume_control);
	}
	cd->use_mdelay = 0;
	if (!of_property_read_u32(thp_node, "use_mdelay", &value)) {
		cd->use_mdelay = value;
		thp_log_info(cd, "%s:use_mdelay parsed:%u\n", __func__,
			     cd->use_mdelay);
	}
	cd->proximity_support = PROX_NOT_SUPPORT;
	rc = of_property_read_u32(thp_node, "proximity_support", &value);
	if (!rc) {
		cd->proximity_support = value;
		thp_log_info(cd, "%s:parsed success, proximity_support = %u\n",
			     __func__, cd->proximity_support);
	} else {
		thp_log_info(cd, "%s:parsed failed, proximity_support = %u\n",
			     __func__, cd->proximity_support);
	}

	cd->platform_type = THP_PLATFORM_HONOR;
	rc = of_property_read_u32(thp_node, "platform_type", &value);
	if (!rc) {
		cd->platform_type = value;
		thp_log_info(cd, "%s:parsed success, platform_type = %u\n",
			     __func__, cd->platform_type);
	} else {
		thp_log_info(cd, "%s:parsed failed, platform_type = %u\n",
			     __func__, cd->platform_type);
	}

	cd->holster_support = 1;
	rc = of_property_read_u32(thp_node, "holster_support", &value);
	if (!rc) {
		cd->holster_support = value;
		thp_log_info(cd, "%s:parse success, holster_support = %u\n",
			     __func__, cd->holster_support);
	}

	cd->support_esd_event = 0;
	rc = of_property_read_u32(thp_node, "support_esd_event", &value);
	if (!rc) {
		cd->support_esd_event = value;
		thp_log_info(cd, "%s:parse success, support_esd_event = %u\n",
			     __func__, cd->support_esd_event);
	}

	rc = of_property_read_u32(thp_node, "talk_mode_support", &value);
	if (!rc) {
		cd->talk_mode_support = value;
		thp_log_info(cd, "%s:parse success, talk_mode_support = %u\n",
			     __func__, cd->talk_mode_support);
	}

	cd->support_25D_anti_fake = 0;
	rc = of_property_read_u32(thp_node, "support_25D_anti_fake", &value);
	if (!rc) {
		cd->support_25D_anti_fake = value;
		thp_log_info(cd,
			     "%s:parse success, support_25D_anti_fake = %u\n",
			     __func__, cd->support_25D_anti_fake);
	}

	value = of_get_named_gpio(thp_node, "irq_gpio", 0);
	thp_log_info(cd, "irq gpio_ = %d\n", value);
	if (!gpio_is_valid(value)) {
		thp_log_err(cd, "%s: get irq_gpio failed\n", __func__);
		return rc;
	}
	cd->gpios.irq_gpio = value;

	value = of_get_named_gpio(thp_node, "rst_gpio", 0);
	thp_log_info(cd, "rst_gpio = %d\n", value);
	if (!gpio_is_valid(value)) {
		thp_log_err(cd, "%s: get rst_gpio failed\n", __func__);
		return rc;
	}
	cd->gpios.rst_gpio = value;

	value = of_get_named_gpio(thp_node, "cs_gpio", 0);
	thp_log_info(cd, "cs_gpio = %d\n", value);
	if (!gpio_is_valid(value)) {
		thp_log_err(cd, "%s: get cs_gpio failed\n", __func__);
		return rc;
	}
	cd->gpios.cs_gpio = value;

	thp_parse_feature_config(thp_node, cd);
	thp_parse_extra_feature_config(thp_node, cd);

	cd->thp_node = thp_node;
	return 0;
}

static int thp_cmd_sync_allocate(struct thp_core_data *cd,
				 struct thp_cmd_node *cmd, int timeout)
{
	struct thp_cmd_sync *sync = NULL;

	if (timeout == 0) {
		cmd->sync = NULL;
		return 0;
	}
	sync = kzalloc(sizeof(*sync), GFP_KERNEL);
	if (sync == NULL) {
		thp_log_err(cd, "failed to kzalloc completion\n");
		return -ENOMEM;
	}
	init_completion(&sync->done);
	atomic_set(&sync->timeout_flag, TS_NOT_TIMEOUT);
	cmd->sync = sync;
	return 0;
}

int thp_put_one_cmd(struct thp_core_data *cd, struct thp_cmd_node *cmd,
		    int timeout)
{
	int error = -EIO;
	unsigned long flags;
	struct thp_cmd_queue *q = NULL;

	if ((cmd == NULL) || (cd == NULL)) {
		thp_log_err(cd, "%s:null pointer\n", __func__);
		goto out;
	}
	if ((!atomic_read(&cd->register_flag)) &&
	    (cmd->command != TS_CHIP_DETECT)) {
		thp_log_err(cd, "%s: not initialize\n", __func__);
		goto out;
	}
	if (thp_cmd_sync_allocate(cd, cmd, timeout)) {
		thp_log_debug(cd, "%s:allocate success\n", __func__);
		/*
		 * When the command execution timeout the memory occupied
		 * by sync will be released  in the command execution module,
		 * else the memory will be released after waiting for the
		 * command return normally.
		 */
		goto out;
	}
	q = &cd->queue;
	spin_lock_irqsave(&q->spin_lock, flags);
	smp_wmb(); /* Make sure queue has assigned correctly */
	if (q->cmd_count == q->queue_size) {
		spin_unlock_irqrestore(&q->spin_lock, flags);
		thp_log_err(cd, "%s:queue is full\n", __func__);
		WARN_ON(1);
		error = -EIO;
		goto free_sync;
	}
	memcpy(&q->ring_buff[q->wr_index], cmd, sizeof(*cmd));
	q->cmd_count++;
	q->wr_index++;
	q->wr_index %= q->queue_size;
	smp_mb(); /* Make sure queue is refreshed correctly */
	spin_unlock_irqrestore(&q->spin_lock, flags);
	thp_log_debug(cd, "%s:%d in ring buff\n", __func__, cmd->command);
	error = NO_ERR;
	wake_up_process(cd->thp_task); /* wakeup thp process */
	if (timeout && (cmd->sync != NULL) &&
	    !(wait_for_completion_timeout(&cmd->sync->done,
					  abs(timeout) * HZ))) {
		atomic_set(&cmd->sync->timeout_flag, TS_TIMEOUT);
		thp_log_err(cd, "%s:wait cmd respone timeout\n", __func__);
		error = -EBUSY;
		goto out;
	}
	smp_wmb(); /* Make sure code has been completed before function ends */
free_sync:
	kfree(cmd->sync);
	cmd->sync = NULL;
out:
	return error;
}

void notify_tp_lowpower_status(struct thp_core_data *cd, int status)
{
	struct thp_cmd_node cmd = {0};
	struct thp_core_data *cd_main = thp_get_core_data();

	if (!cd) {
		thp_log_err(cd, "%s: cd is null\n", __func__);
		return;
	}
	cmd.command = NOTIFY_TP_LOWPOWER_STATUS;
	cmd.cmd_param.pub_params.params = status;
	cmd.cmd_param.prv_params = cd;
	if (thp_put_one_cmd(cd_main, &cmd, NO_SYNC_TIMEOUT))
		thp_log_err(cd_main, "%s: put cmd error\n", __func__);
}

void aod_notify_fullaod_doze_status(struct thp_core_data *cd, int status)
{
	struct thp_cmd_node cmd = {0};
	struct thp_core_data *cd_main = thp_get_core_data();

	if (!cd) {
		thp_log_err(cd, "%s: cd is null\n", __func__);
		return;
	}
	cmd.command = AOD_NOTIFY_FULLAOD_DOZE_STATUS;
	cmd.cmd_param.pub_params.params = status;
	cmd.cmd_param.prv_params = cd;
	if (thp_put_one_cmd(cd_main, &cmd, NO_SYNC_TIMEOUT))
		thp_log_err(cd_main, "%s: put cmd error\n", __func__);
}

static int get_one_cmd(struct thp_core_data *cd, struct thp_cmd_node *cmd)
{
	unsigned long flags;
	int error = -EIO;
	struct thp_cmd_queue *q = NULL;

	if (unlikely(!cmd)) {
		thp_log_err(cd, "%s:find null pointer\n", __func__);
		goto out;
	}

	q = &cd->queue;
	spin_lock_irqsave(&q->spin_lock, flags);
	smp_wmb(); /* Make sure queue has assigned correctly */
	if (!q->cmd_count) {
		thp_log_debug(cd, "%s: queue is empty\n", __func__);
		spin_unlock_irqrestore(&q->spin_lock, flags);
		goto out;
	}
	memcpy(cmd, &q->ring_buff[q->rd_index], sizeof(*cmd));
	q->cmd_count--;
	q->rd_index++;
	q->rd_index %= q->queue_size;
	smp_mb(); /* Make sure queue is refreshed correctly */
	spin_unlock_irqrestore(&q->spin_lock, flags);
	thp_log_debug(cd, "%s:%d from ring buff\n", __func__, cmd->command);
	error = NO_ERR;
out:
	return error;
}

static bool thp_task_continue(struct thp_core_data *cd)
{
	bool task_continue = true;
	unsigned long flags;

	thp_log_debug(cd, "%s:prepare enter idle\n", __func__);
	while (task_continue) {
		if (unlikely(kthread_should_stop())) {
			task_continue = false;
			goto out;
		}
		spin_lock_irqsave(&cd->queue.spin_lock, flags);
		/*
		 * Make sure the memory and assignment are completed
		 * before updating the current process.
		 */
		smp_wmb();
		if (cd->queue.cmd_count) {
			set_current_state(TASK_RUNNING);
			thp_log_debug(cd, "%s:TASK_RUNNING\n", __func__);
			goto out_unlock;
		} else {
			set_current_state(TASK_INTERRUPTIBLE);
			thp_log_debug(cd, "%s:TASK_INTERRUPTIBLE\n", __func__);
			spin_unlock_irqrestore(&cd->queue.spin_lock, flags);
			schedule();
		}
	}

out_unlock:
	spin_unlock_irqrestore(&cd->queue.spin_lock, flags);
out:
	return task_continue;
}

static int thp_proc_command(struct thp_core_data *cd, struct thp_cmd_node *cmd)
{
	int error = NO_ERR;
	struct thp_cmd_sync *sync = NULL;
	struct thp_cmd_node *proc_cmd = cmd;
	struct thp_cmd_node *out_cmd = &cd->pang_cmd_buff;

	if (!cmd) {
		thp_log_err(cd, "%s:invalid cmd\n", __func__);
		goto out;
	}
	sync = cmd->sync;
	/* discard timeout cmd */
	if (sync && (atomic_read(&sync->timeout_flag) == TS_TIMEOUT)) {
		kfree(sync);
		goto out;
	}
	while (true) {
		out_cmd->command = TS_INVAILD_CMD;
		error = thp_pm_cmd_proc(cd, proc_cmd, out_cmd);
		switch (proc_cmd->command) {
		case TS_CHIP_DETECT:
			thp_chip_detect(cd, proc_cmd);
			break;
		case THP_TOUCH_DRIVER_CMD:
			if (cd->thp_dev->ops->thp_cmd_proc)
				cd->thp_dev->ops->thp_cmd_proc(
					cd->thp_dev, proc_cmd, out_cmd);
			break;
		case TS_GET_ACTIVE_DOZE_RATIO:
			thp_get_active_doze_ratio(cd);
			break;
		case AOD_NOTIFY_FULLAOD_DOZE_STATUS:
			thp_fullaod_gesture_support(proc_cmd);
			break;
		case IRQ_SCHEDULER_BOOST:
			irq_scheduler_boost_proc(cd, proc_cmd);
			break;
		case NOTIFY_TP_LOWPOWER_STATUS:
			thp_lowpower_status_ctrl(proc_cmd);
			break;
		default:
			break;
		}

		thp_log_debug(cd, "%s:command :%d process result:%d\n",
			      __func__, proc_cmd->command, error);
		if (out_cmd->command != TS_INVAILD_CMD) {
			thp_log_debug(cd,
				      "%s:related command :%d  need process\n",
				      __func__, out_cmd->command);
			/* ping - pang */
			swap(proc_cmd, out_cmd);
		} else {
			break;
		}
	}
	/* notify wait threads by completion */
	if (sync) {
		smp_mb(); /* Make sure current timeout_flag is up to date */
		thp_log_debug(cd, "%s:wakeup threads in waitqueue\n", __func__);
		if (atomic_read(&sync->timeout_flag) == TS_TIMEOUT)
			kfree(sync);
		else
			complete(&sync->done);
	}

out:
	return error;
}

static int thp_thread(void *thp_core)
{
	struct thp_core_data *cd = (struct thp_core_data *)thp_core;
	static const struct sched_param param = {
		/* The priority of thread scheduling is 99 */
		.sched_priority = 99,
	};
	/*
	 * Make sure buff is properly refreshed
	 * before the process is executed.
	 */
	smp_wmb();
	thp_log_info(cd, "%s: in\n", __func__);
	memset(&cd->ping_cmd_buff, 0, sizeof(cd->ping_cmd_buff));
	memset(&cd->pang_cmd_buff, 0, sizeof(cd->pang_cmd_buff));
	/*
	 * Make sure buff is properly refreshed
	 * before the process is executed.
	 */
	smp_mb();
	sched_setscheduler(current, SCHED_RR, &param);
	while (!cd->thp_probe_done)
		wait_for_completion(&cd->thp_probe_completion);
	while (thp_task_continue(cd)) {
		/* get one command */
		while (!get_one_cmd(cd, &cd->ping_cmd_buff)) {
			thp_proc_command(cd, &cd->ping_cmd_buff);
			memset(&cd->ping_cmd_buff, 0,
			       sizeof(cd->ping_cmd_buff));
			memset(&cd->pang_cmd_buff, 0,
			       sizeof(cd->pang_cmd_buff));
		}
	}
	thp_log_err(cd, "%s: stop\n", __func__);
	return NO_ERR;
}

static int thp_thread_init(struct thp_core_data *cd)
{
	if (cd->multi_panel_index == SINGLE_TOUCH_PANEL)
		cd->thp_task =
			kthread_create(thp_thread, cd, "ts_thread:%d", 0);
	else
		cd->thp_task = kthread_create(thp_thread, cd, "ts_thread:%d",
					      cd->multi_panel_index);

	if (IS_ERR(cd->thp_task)) {
		kfree(cd->frame_read_buf);
		kfree(cd);
		g_thp_core = NULL;
		thp_log_err(cd, "%s: creat thread failed\n", __func__);
		return -ENODEV;
	}
	cd->queue.rd_index = 0;
	cd->queue.wr_index = 0;
	cd->queue.cmd_count = 0;
	cd->queue.queue_size = THP_CMD_QUEUE_SIZE;
	spin_lock_init(&cd->queue.spin_lock);
	init_completion(&cd->thp_probe_completion);
	smp_mb(); /* Make sure queue is initialized before wake up the task */
	wake_up_process(cd->thp_task);
	return 0;
}

static int thp_probe(struct spi_device *sdev)
{
	struct thp_core_data *thp_core = NULL;
	u8 *frame_read_buf = NULL;
	int rc;

#if (IS_ENABLED(CONFIG_TP_QCOM_8450) || IS_ENABLED(CONFIG_TP_QCOM_8550))
	if (poweroff_charger == true && recovery_mode == false) {
		thp_log_info(thp_core, "%s: in poweroff_charger\n", __func__);
		return 0;
	}
#endif

	thp_core = kzalloc(sizeof(struct thp_core_data), GFP_KERNEL);
	if (!thp_core) {
		thp_log_err(thp_core, "%s: thp_core out of memory\n", __func__);
		return -ENOMEM;
	}

	frame_read_buf = kzalloc(THP_MAX_FRAME_SIZE, GFP_KERNEL);
	if (!frame_read_buf) {
		thp_log_err(thp_core, "%s: frame_read_buf out of memory\n",
			    __func__);
		kfree(thp_core);
		return -ENOMEM;
	}

	thp_core->frame_read_buf = frame_read_buf;
	thp_core->sdev = sdev;
	thp_core->dev = &sdev->dev;
	thp_core->frame_size = THP_MAX_FRAME_SIZE;
	thp_core->restart_power_on = true;
	thp_core->fullaod_support_gesture = 1;
	rc = thp_parse_config(thp_core, sdev->dev.of_node);
	if (rc) {
		thp_log_err(thp_core, "%s: parse dts fail\n", __func__);
		kfree(thp_core->frame_read_buf);
		kfree(thp_core);
		return -ENODEV;
	}

	if (!thp_lcdkit_ready()) {
		if (!thp_core->self_control_power) {
			thp_log_info(thp_core,
				     "%s: lcdkit not init, need wait\n",
				     __func__);
			kfree(thp_core->frame_read_buf);
			kfree(thp_core);
			return -EPROBE_DEFER;
		}
	}
	if (thp_core->use_thp_queue) {
		thp_core->thp_queue = thp_queue_init(thp_core);
		if (thp_core->thp_queue == NULL) {
			thp_log_err(thp_core, "%s: kzalloc frame fail\n",
				    __func__);
			kfree(thp_core->frame_read_buf);
			thp_core->frame_read_buf = NULL;
			kfree(thp_core);
			thp_core = NULL;
			return -ENOMEM;
		}
	}
	if (thp_core->multi_panel_index == SUB_TOUCH_PANEL &&
	    recovery_mode == true) {
		thp_log_info(thp_core, "fold device in recovery\n");
		return 0;
	}
	rc = thp_init_chip_info(thp_core);
	if (rc)
		thp_log_err(thp_core, "%s: chip info init fail\n", __func__);

	mutex_init(&thp_core->spi_mutex);
	thp_log_info(thp_core, "%s:use_hwlock = %d\n", __func__,
		     thp_core->use_hwlock);
	if (thp_core->use_hwlock) {
		thp_core->hwspin_lock =
			hwspin_lock_request_specific(TP_HWSPIN_LOCK_CODE);
		if (!thp_core->hwspin_lock)
			thp_log_err(thp_core,
				    "%s: hwspin_lock request failed\n",
				    __func__);
	}
	thp_irq_scheduler_boost_init(thp_core);
#if IS_ENABLED(CONFIG_DSM)
	memset(&thp_core->dsm_thp, 0, sizeof(thp_core->dsm_thp));
	thp_core->dsm_thp.ic_name = THP_DSM_IC_NAME;
	thp_core->dsm_thp.module_name = THP_DSM_MODULE_NAME;
	thp_core->dsm_thp.device_name = THP_DSM_DEVICE_NAME;
	thp_core->dsm_thp.buff_size = THP_DSM_BUFF_SIZE;
	thp_core->dsm_thp.name = THP_DSM_DEV_NAME_SINGLE;

	if (thp_core->multi_panel_index != SINGLE_TOUCH_PANEL) {
		if (thp_core->multi_panel_index == SUB_TOUCH_PANEL)
			thp_core->dsm_thp.name = THP_DSM_DEV_NAME_SUB;
		else
			thp_core->dsm_thp.name = THP_DSM_DEV_NAME_MAIN;
	}
	thp_log_info(thp_core, "%s: dsm_thp.name is %s\n", __func__,
		     thp_core->dsm_thp.name);

	thp_core->dsm_thp_dclient = dsm_register_client(&thp_core->dsm_thp);
#endif
	atomic_set(&thp_core->register_flag, 0);
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	atomic_set(&thp_core->suspend_irq_processing_status, THP_IRQ_IDLE);
	init_waitqueue_head(&(thp_core->system_active_waitq));
	thp_core->system_active_waitq_flag = true;
#endif
	INIT_LIST_HEAD(&thp_core->frame_list.list);
	init_waitqueue_head(&(thp_core->frame_waitq));
	init_waitqueue_head(&(thp_core->thp_ta_waitq));
	init_waitqueue_head(&(thp_core->thp_event_waitq));
	init_waitqueue_head(&(thp_core->suspend_resume_waitq));
	thp_core->event_flag = false;
	thp_core->suspend_resume_waitq_flag = WAITQ_WAKEUP;
	if (thp_core->wait_afe_screen_on_support) {
		init_waitqueue_head(&(thp_core->afe_screen_on_waitq));
		atomic_set(&(thp_core->afe_screen_on_waitq_flag), WAITQ_WAKEUP);
		thp_log_info(thp_core, "%s, init afe_screen_on_waitq done\n",
			     __func__);
	}
	spi_set_drvdata(sdev, thp_core);
	thp_spi_cmd_lock_init(thp_core);

	if (thp_core->multi_panel_index == SUB_TOUCH_PANEL)
		g_thp_core_sub = thp_core;
	else
		g_thp_core = thp_core;

	rc = thp_vm_register(thp_core);
	if (rc == TUI_VM_REGISTER_SUCCESS_TVM) {
		thp_log_info(thp_core, "%s, vm register success in tvm\n",
			     __func__);
		rc = thp_thread_init(thp_core);
		if (rc)
			thp_log_err(thp_core,
				    "%s: thp_thread_init fail rc: %d\n",
				    __func__, rc);
		thp_core->thp_probe_done = true;
		complete_all(&thp_core->thp_probe_completion);
		return 0;
	} else if (rc < 0) {
		thp_log_err(thp_core, "%s: Failed to register tui vm rc:%d\n",
			    __func__, rc);
	}

	/* Hw Resource Init */
	rc = thp_setup_gpio(thp_core);
	if (rc) {
		thp_log_err(thp_core, "%s: spi setup gpio fail\n", __func__);
		return rc;
	}

#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	if (thp_core->spi_autosleep_short_delay)
		pm_runtime_set_autosuspend_delay(
			sdev->controller->dev.parent,
			thp_core->spi_autosleep_short_delay);
#endif

#if IS_ENABLED(CONFIG_DRIVER_BRIGE)
	tp_spi_interface_ops_register(&g_tp_spi_interface);
#endif

	if (thp_core->fast_booting_solution) {
		thp_core->thp_unregister_ic_num = 0;
		rc = thp_thread_init(thp_core);
		if (rc)
			thp_log_err(thp_core,
				    "%s: thp_thread_init fail rc: %d\n",
				    __func__, rc);

		syna_driver_module_init(thp_core);

		goodix_driver_module_init(thp_core);
		focal_driver_module_init(thp_core);
		thp_core->thp_probe_done = true;
		complete_all(&thp_core->thp_probe_completion);
	}
	thp_log_info(thp_core, "%s:out\n", __func__);
	return 0;
}

static void thp_remove(struct spi_device *sdev)
{
	struct thp_core_data *cd = spi_get_drvdata(sdev);

	thp_log_info(cd, "%s: in\n", __func__);

	if (cd->use_thp_queue) {
		mutex_lock(&cd->mutex_frame);
		thp_queue_free(cd, cd->thp_queue);
		mutex_unlock(&cd->mutex_frame);
	}
	thp_vm_unregister(cd);

	if (atomic_read(&cd->register_flag)) {
		thp_sysfs_release(cd);
		thp_charger_notifier_unregister(cd);
		thp_lcd_notify_unregister(cd);

		thp_misc_exit(cd);
		mutex_destroy(&cd->mutex_frame);
		thp_mt_wrapper_exit(cd);
	}

	kfree(cd->frame_read_buf);
	kfree(cd->spi_sync_rx_buf);
	cd->spi_sync_rx_buf = NULL;

	kfree(cd->spi_sync_tx_buf);
	cd->spi_sync_tx_buf = NULL;

	kfree(cd);
	cd = NULL;

#if defined(CONFIG_TEE_TUI)
	unregister_tui_driver("tp");
#endif
}

#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
static int thp_system_suspend(struct device *dev)
{
	struct spi_device *sdev = to_spi_device(dev);
	struct thp_core_data *cd = spi_get_drvdata(sdev);

	if (!cd) {
		thp_log_err(cd, "%s: thp_core_data is null\n", __func__);
		return 0;
	}
	thp_log_info(cd, "%s call\n", __func__);

	cd->system_active_waitq_flag = false;
	if (atomic_read(&cd->suspend_irq_processing_status) != THP_IRQ_IDLE) {
		thp_log_err(cd, "%s spi_status is running can't go to sleep\n",
			    __func__);
		cd->system_active_waitq_flag = true;
		return -EBUSY;
	}
	wake_up_interruptible(&(cd->system_active_waitq));
	return 0;
}
static int thp_system_resume(struct device *dev)
{
	int ret;
	struct thp_cmd_node cmd = {0};
	struct spi_device *sdev = to_spi_device(dev);
	struct thp_core_data *cd = spi_get_drvdata(sdev);

	if (!cd) {
		thp_log_err(cd, "%s: thp_core_data is null\n", __func__);
		return 0;
	}
	thp_log_info(cd, "%s call\n", __func__);
	cd->system_active_waitq_flag = true;
	wake_up_interruptible(&(cd->system_active_waitq));
	if (cd->support_get_active_doze_time) {
		cmd.command = TS_GET_ACTIVE_DOZE_RATIO;
		ret = thp_put_one_cmd(cd, &cmd, NO_SYNC_TIMEOUT);
		if (ret)
			thp_log_err(cd, "%s: put cmd error :%d\n", __func__,
				    ret);
	}
	return 0;
}

static const struct dev_pm_ops spi_driver_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(thp_system_suspend, thp_system_resume)};
#endif

static const struct of_device_id g_thp_psoc_match_table[] = {
	{
		.compatible = "honor,thp",
	},
	{},
};

static const struct spi_device_id g_thp_device_id[] = {{THP_DEVICE_NAME, 0},
						       {}};
MODULE_DEVICE_TABLE(spi, g_thp_device_id);

static struct spi_driver g_thp_spi_driver = {
	.probe = thp_probe,
	.remove = thp_remove,
	.id_table = g_thp_device_id,
	.driver =
		{
			.name = THP_DEVICE_NAME,
			.owner = THIS_MODULE,
			.bus = &spi_bus_type,
			.of_match_table = g_thp_psoc_match_table,
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
			.pm = &spi_driver_pm_ops,
#endif
		},
};

static int __init g_thp_spi_init(void)
{
	struct thp_core_data *cd = NULL;
	int ret = 0;

	thp_log_info(cd, "%s called\n", __func__);
	ret = spi_register_driver(&g_thp_spi_driver);
	if (ret != 0)
		thp_log_err(cd, "spi register driver failed!");

	thp_log_info(cd, "%s end\n", __func__);
	return ret;
}

static void __exit g_thp_spi_exit(void)
{
	spi_unregister_driver(&g_thp_spi_driver);
}

module_param(recovery_mode, bool, 0600);
MODULE_PARM_DESC(recovery_mode, "recovery cmdlineinfo");
#if (IS_ENABLED(CONFIG_TP_QCOM_8450) || IS_ENABLED(CONFIG_TP_QCOM_8550))
module_param(poweroff_charger, bool, 0600);
MODULE_PARM_DESC(poweroff_charger, "poweroff_charger cmdlineinfo");
#endif
module_init(g_thp_spi_init);
module_exit(g_thp_spi_exit);

MODULE_AUTHOR("Honor Device Company");
MODULE_DESCRIPTION("Honor THP Driver");
MODULE_LICENSE("GPL");

/*
 * Thp driver code for synaptics
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

#if IS_ENABLED(CONFIG_DSM)
#include <dsm/dsm_pub.h>
#endif
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/string.h>
#include "honor_thp.h"

#include <linux/time.h>
#include <linux/syscalls.h>

#define SYNAPTICS_IC_NAME "synaptics"
#define THP_SYNA_DEV_NODE_NAME "synaptics_thp"
#define SYNA_FRAME_SIZE 1092

#define MESSAGE_HEADER_SIZE 4
#define MESSAGE_MARKER 0xa5
#define UBL_FN_NUMBER 0x35
#define MESSAGE_PADDING 0x5a
#define FRAME_LENGTH (2 * 18 * 30)
#define MESSAGE_DATA_NUM 32
#define VCC_DELAY 12
#define IOVDD_DELAY 30
#define POWER_OFF_DELAY_FOR_3909 5
#define NO_DELAY 0
#define SYNA_CMD_LEN 5
#define SYNA_ONE_CMD_LEN 6
#define SYNA_TUI_CMD_LEN 4
#define SYNA_CMD_GESTURE_LEN 8
#define SYNA_CMD_GET_DEBUG_LEN 4
#define SYNA_DEBUG_INFO_LEN 320
#define SYNA_DEBUG_INFO_LEN_L 0x40
#define SYNA_DEBUG_INFO_LEN_H 0x01

#define DOUBLE_TAP_FLAG 2
#define TUI_FINGER_DOWN 3
#define TUI_FINGER_UP 4

#define SYNA_CMD_GESTURE_MAX 152
#define NEED_WORK_IN_SUSPEND 1
#define NO_NEED_WORK_IN_SUSPEND 0
#define FIRST_FRAME_USEFUL_LEN 2

#define BOOT_CONFIG_SIZE 8
#define BOOT_CONFIG_SLOTS 16
#define ID_PART_NUM_LEN 16
#define ID_BUILD_LEN 4
#define ID_WRITE_LEN 2
#define BOOT_DATA 2
#define BOOT_START 4
#define REFLASH_READ_LEN 9
#define IC_PROJECT_ID_START 4
#define REFLASH_CMD_LEN_LOW 0x06
#define REFLASH_CMD_LEN_HIGH 0x00
#define CHIP_DETECT_TMP_BUF 30
#define SYNA_COMMAMD_LEN 3

#define RMI_ADDR_FIRST 0x80
#define RMI_ADDR_SECOND 0xEE
#define TOUCH_EVENT_TYPE 0xff
#define TUI_EVENT_TYPE 0xC0
#define TOUCH_RESET_TYPE 0x10

#define SPI_READ_WRITE_SIZE 384
#define RMI_CMD_LEN 2
#define FRAME_HEAD_LEN 4
#define MOVE_8BIT 8
#define MOVE_16BIT 16
#define MOVE_24BIT 24
#define FRAME_CMD_LEN 4
#define DYNAMIC_CMD_LEN 6

#define CMD_SET_DYNAMIC_CONFIG 0x24
#define CMD_DYNAMIC_CONFIG_LEN 3
#define SYNA_ENTER_GUESTURE_MODE 1
#define SYNA_EXIT_GUESTURE_MODE 1
#define SYNA_RETRY_TIMES 5
#define TOUCH_REPORT_CONFIG_SIZE 128
#define GESTURE_REPORT_SIZE 7
#define SYNA_RESPONSE_LEN 10
#define SPI_RETRY_TIMES 20
#define SPI_DELAY_MS 5
#define SYNA_MAX_CMD_TIMEOUT 200
#define SYNA_CMD_LENGTH_MAX 32

#define WAIT_FOR_SPI_BUS_READ_DELAY 5
#define SUPPORT_GET_FRAME_READ_ONCE 1
#define TOUCH_GESTURE_CMD 4

#define THP_GESTURE_DOUBLE_CLICK (1 << 1)
#define THP_GESTURE_FINGER (1 << 3)

#define FP_VALID_AREA_FINGER_DOWN (1 << 0)
#define FP_VALID_AREA_FINGER_UP (1 << 1)
#define FP_CORE_AREA_FINGER_DOWN (1 << 2)
#define FP_CORE_AREA_FINGER_UP (1 << 3)
#define FULLAOD_SLIDE_EVENT 5

#define SYNA_GESTURE_ONE_CMD 0x20
#define SYNA_SINGLE_CLICK_ONE_CMD 0x10
#define SYNA_FINGER_ONE_CMD 0x08
#define DEBUG_OPEN_VALUE 0x64
#define DEBUG_LOG_LENGTH 102

#define STYLUS_EVENT_FLAG 0x20
#define SYNA_STYLUS_CMD 0x40
#define PEN_CMD_LEN 4

#define IGNORE_IRQ_COUNT 1

#define READ_RETRY_TIMES 3
#define READ_RETRY_LENGTH_MAX 500

#define SET_AP_CMD_RETRY_TIMES 3
#define MAX_FINGER_NUMS 10
#define STANDBY_DATA_LEN_MAX 86

enum dynamic_config_id {
	DC_UNKNOWN = 0x00,
	DC_NO_DOZE,
	DC_DISABLE_NOISE_MITIGATION,
	DC_INHIBIT_FREQUENCY_SHIFT,
	DC_REQUESTED_FREQUENCY,
	DC_DISABLE_HSYNC,
	DC_REZERO_ON_EXIT_DEEP_SLEEP,
	DC_CHARGER_CONNECTED,
	DC_NO_BASELINE_RELAXATION,
	DC_IN_WAKEUP_GESTURE_MODE,
	DC_STIMULUS_FINGERS,
	DC_GRIP_SUPPRESSION_ENABLED,
	DC_ENABLE_THICK_GLOVE,
	DC_ENABLE_LOZE = 216,
	DC_SCENE_SWITCH = 0xCB,
};

enum status_code {
	STATUS_IDLE = 0x00,
	STATUS_OK = 0x01,
	STATUS_BUSY = 0x02,
	STATUS_CONTINUED_READ = 0x03,
	STATUS_RECEIVE_BUFFER_OVERFLOW = 0x0c,
	STATUS_PREVIOUS_COMMAND_PENDING = 0x0d,
	STATUS_NOT_IMPLEMENTED = 0x0e,
	STATUS_ERROR = 0x0f,
	STATUS_INVALID = 0xff,
};

enum report_type {
	REPORT_IDENTIFY = 0x10,
	REPORT_TOUCH = 0x11,
	REPORT_DELTA = 0x12,
	REPORT_RAW = 0x13,
	REPORT_PRINTF = 0x82,
	REPORT_STATUS = 0x83,
	REPORT_FRAME = 0xC0,
	REPORT_HDL = 0xfe,
};

enum report_touch_type {
	NO_GESTURE_DETECT = 0x00,
	DOUBLE_TAP,
	SWIPE,
	SINGLE_TAP = 0x80,
};

enum boot_mode {
	MODE_APPLICATION = 0x01,
	MODE_BOOTLOADER = 0x0b,
};

enum command {
	CMD_GET_BOOT_INFO = 0x10,
	CMD_READ_FLASH = 0x13,
	CMD_RUN_BOOTLOADER_FIRMWARE = 0x1f,
};
/*
 * SYNA_DISPATCH_RESPONCE: this irq event is a responce to cmd form driver
 * SYNA_DISPATCH_REPORT: all event else, could be a frame，gesture event,
 * 			 or resp to cmd from afe hal
 */
enum syna_irq_dispatch {
	SYNA_DISPATCH_RESPONCE,
	SYNA_DISPATCH_REPORT,
};

enum syna_ic_type {
	SYNA3909 = 1,
};

enum syna_thp_cmd_type {
	SYNA_ASYNC_IC_CMD,
	SYNA_GET_DEBUG_INFO_AND_RESET,
	SYNA_THP_INVALID_CMD,
};

struct syna_async_ic_cmd {
	uint8_t cmd[SYNA_CMD_LENGTH_MAX];
	int cmd_length;
};

struct syna_tcm_message {
	unsigned char header;
	unsigned char code;
	unsigned short length;
	unsigned char data[0];
};

struct syna_tcm_cmd_sync {
	bool local_resp_waitting;
	struct completion local_cmd_completion;
	union {
		struct syna_tcm_message message;
		char buf[THP_MAX_FRAME_SIZE];
	} resp;
};

struct syna_thp_cmd {
	enum syna_thp_cmd_type command;
	union {
		struct syna_async_ic_cmd async_ic_cmd;
	} params;
};

struct syna_tcm_identification {
	unsigned char version;
	unsigned char mode;
	unsigned char part_number[ID_PART_NUM_LEN];
	unsigned char build_id[ID_BUILD_LEN];
	unsigned char max_write_size[ID_WRITE_LEN];
	int tui_finger_last_status;
	/*
	 * stylus_gesture_status_only means enable instant write only
	 * no single, double click and FP
	 */
	unsigned int stylus_gesture_status_only;
	unsigned int standby_enable_only;
	bool stylus_gesture_status_lowpower;
	bool standby_enable_in_lowpower;
	struct syna_tcm_cmd_sync cmd_sync;
};
struct syna_tcm_boot_info {
	unsigned char version;
	unsigned char status;
	unsigned char asic_id[BOOT_DATA];
	unsigned char write_block_size_words;
	unsigned char erase_page_size_words[BOOT_DATA];
	unsigned char max_write_payload_size[BOOT_DATA];
	unsigned char last_reset_reason;
	unsigned char pc_at_time_of_last_reset[BOOT_DATA];
	unsigned char boot_config_start_block[BOOT_DATA];
	unsigned char boot_config_size_blocks[BOOT_DATA];
	unsigned char display_config_start_block[BOOT_START];
	unsigned char display_config_length_blocks[BOOT_DATA];
	unsigned char backup_display_config_start_block[BOOT_START];
	unsigned char backup_display_config_length_blocks[BOOT_DATA];
	unsigned char custom_otp_start_block[BOOT_DATA];
	unsigned char custom_otp_length_blocks[BOOT_DATA];
};

static unsigned char *tx_buf;
unsigned char *spi_read_buf;
unsigned char *spi_write_buf;
struct spi_transfer *spi_xfer;
static unsigned int get_project_id_flag;
static unsigned int need_power_off;
static unsigned int ignore_irq_after_reset;
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
static struct udfp_mode_status ud_mode_status;
#endif
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
static struct timeval lowpower_switch_time;
#endif

static int pt_mode_set_for_3909(const struct thp_device *tdev);
static int touch_driver_event_read_and_dispatch(struct thp_device *tdev,
						unsigned char *buf,
						size_t buf_size,
						size_t read_size);
static int touch_driver_send_one_cmd(struct thp_device *tdev,
				     unsigned char *cmd, int len);
static int touch_driver_prepare(struct thp_device *tdev);
static void touch_driver_unprepare(struct thp_device *tdev);

static int touch_driver_spi_alloc_mem(struct spi_device *client,
				      unsigned int count, unsigned int size)
{
	static unsigned int buf_size;
	static unsigned int xfer_count;
	struct thp_core_data *cd = spi_get_drvdata(client);

	if (count > xfer_count) {
		kfree(spi_xfer);
		spi_xfer = kcalloc(count, sizeof(*spi_xfer), GFP_KERNEL);
		if (!spi_xfer) {
			thp_log_err(cd, "Failed to allocate memory for xfer\n");
			xfer_count = 0;
			return -ENOMEM;
		}
		xfer_count = count;
	} else {
		memset(spi_xfer, 0, count * sizeof(*spi_xfer));
	}

	if (size > buf_size) {
		if (buf_size)
			kfree(tx_buf);
		tx_buf = kmalloc(size, GFP_KERNEL);
		if (!tx_buf) {
			thp_log_err(cd, "Failed to allocate memory for buf\n");
			buf_size = 0;
			return -ENOMEM;
		}
		buf_size = size;
	}

	return 0;
}

static int touch_driver_spi_read(struct spi_device *client, unsigned char *data,
				 unsigned int length)
{
	int retval;
	int retry = 0;
	struct spi_message msg;
	struct thp_core_data *cd = spi_get_drvdata(client);

	spi_message_init(&msg);

	retval = touch_driver_spi_alloc_mem(client, 1, length);
	if (retval < 0) {
		thp_log_err(cd, "Failed to allocate memory\n");
		goto exit;
	}

	memset(tx_buf, 0xff, length);
	spi_xfer[0].len = length;
	spi_xfer[0].tx_buf = tx_buf;
	spi_xfer[0].rx_buf = data;
	spi_message_add_tail(&spi_xfer[0], &msg);
	while (retry < SPI_RETRY_TIMES) {
		retval = thp_spi_sync(client, &msg);
		if (retval == 0) {
			retval = length;
			break;
		} else {
			thp_log_err(cd, "SPI transfer failed, error = %d\n",
				    retval);
			retry++;
			msleep(SPI_DELAY_MS);
			continue;
		}
	}

exit:

	return retval;
}

static int touch_driver_spi_write(struct spi_device *client,
				  unsigned char *data, unsigned int length)
{
	int retval;
	struct spi_message msg;
	struct thp_core_data *cd = spi_get_drvdata(client);

	spi_message_init(&msg);

	retval = touch_driver_spi_alloc_mem(client, 1, 0);
	if (retval < 0) {
		thp_log_err(cd, "Failed to allocate memory\n");
		goto exit;
	}
#if (!(IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK)))
	spi_xfer[0].len = length;
	spi_xfer[0].tx_buf = data;
#else
	memcpy(tx_buf, data, length);
	spi_xfer[0].len = length;
	spi_xfer[0].tx_buf = tx_buf;
#endif
	spi_message_add_tail(&spi_xfer[0], &msg);
	retval = thp_spi_sync(client, &msg);
	if (retval == 0) {
		retval = length;
	} else {
		thp_log_err(cd, "Failed to complete SPI transfer, error = %d\n",
			    retval);
	}

exit:

	return retval;
}

static int touch_driver_send_cmd(struct spi_device *client, char *buf,
				 unsigned int length)
{
	int ret;
	int retry_times = SYNA_RETRY_TIMES;
	uint8_t *resp_buf = spi_read_buf;
	struct thp_core_data *cd = spi_get_drvdata(client);

	if (resp_buf == NULL) {
		thp_log_err(cd, "resp_buf is NULL\n");
		return -ENOMEM;
	}
	memset(resp_buf, 0, SYNA_RESPONSE_LEN);
	if (thp_bus_lock(cd) < 0) {
		thp_log_err(cd, "%s:get lock failed\n", __func__);
		return -EINVAL;
	}

	ret = touch_driver_spi_write(client, buf, length);
	if (ret != length) {
		thp_log_err(cd, "%s, Failed to write command\n", __func__);
		goto exit;
	}
	while (retry_times) {
		msleep(50); /* delay 50ms for make sure fw response cmd */
		ret = touch_driver_spi_read(client, resp_buf,
					    SYNA_RESPONSE_LEN);
		if ((ret != SYNA_RESPONSE_LEN) ||
		    (resp_buf[0] != MESSAGE_MARKER)) {
			thp_log_err(cd, "Fail to read response %x\n",
				    resp_buf[0]);
			goto exit;
		}
		thp_log_info(cd, "%s, 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
			     resp_buf[0], resp_buf[1], resp_buf[2],
			     resp_buf[3]);
		/* resp_buf[1]: fw response status */
		if (resp_buf[1] == STATUS_OK) {
			ret = NO_ERR;
			break;
		}

		retry_times--;
	}
exit:
	thp_bus_unlock(cd);
#if IS_ENABLED(CONFIG_DSM)
	if (ret)
		thp_dmd_report(cd, DSM_TPHOSTPROCESSING_DEV_GESTURE_EXP1,
			       "%s, 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
			       resp_buf[0], resp_buf[1], resp_buf[2],
			       resp_buf[3]);
#endif
	return ret;
}

static int touch_driver_disable_frame(struct thp_device *tdev, int value)
{
	const uint8_t cmd_disable[FRAME_CMD_LEN] = {0x06, 0x01, 0x00, 0xC0};
	const uint8_t cmd_enable[FRAME_CMD_LEN] = {0x05, 0x01, 0x00, 0xC0};
	uint8_t *cmd_buf = spi_write_buf;

	thp_log_info(tdev->thp_core, "%s, input value is %d\n", __func__,
		     value);

	if (cmd_buf == NULL) {
		thp_log_err(tdev->thp_core, "cmd_buf is NULL\n");
		return -ENOMEM;
	}
	if (value) /* value 1: disable frame */
		memcpy(cmd_buf, cmd_disable, FRAME_CMD_LEN);
	else
		memcpy(cmd_buf, cmd_enable, FRAME_CMD_LEN);

	return touch_driver_send_cmd(tdev->thp_core->sdev, cmd_buf,
				     FRAME_CMD_LEN);
}

static int touch_driver_set_dynamic_config(struct thp_device *tdev,
					   enum dynamic_config_id id,
					   unsigned int value)
{
	int index = 0;
	uint8_t *out_buf = spi_write_buf;

	if (out_buf == NULL) {
		thp_log_err(tdev->thp_core, "out_buf is NULL\n");
		return -ENOMEM;
	}
	out_buf[index++] = CMD_SET_DYNAMIC_CONFIG;
	out_buf[index++] = (unsigned char)CMD_DYNAMIC_CONFIG_LEN;
	out_buf[index++] = (unsigned char)(CMD_DYNAMIC_CONFIG_LEN >> MOVE_8BIT);
	out_buf[index++] = (unsigned char)id;
	out_buf[index++] = (unsigned char)value;
	out_buf[index++] = (unsigned char)(value >> MOVE_8BIT);

	thp_log_info(tdev->thp_core, "%s, type: %d, value: %u\n", __func__, id,
		     value);
	return touch_driver_send_cmd(tdev->thp_core->sdev, out_buf,
				     DYNAMIC_CMD_LEN);
}

static int touch_driver_parse_gesture_data(struct thp_device *tdev,
					   const char *data,
					   unsigned int *gesture_wakeup_value)
{
	unsigned int data_length;

	/* data[2]: length's low ,data[3]: length's high */
	data_length = (unsigned int)((data[3] << MOVE_8BIT) | data[2]);
	thp_log_info(tdev->thp_core, "%s, data length is %u\n", __func__,
		     data_length);
	/* data[4]: report touch type */
	if (data_length == GESTURE_REPORT_SIZE && data[4] == DOUBLE_TAP) {
		mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
		if (tdev->thp_core->easy_wakeup_info.off_motion_on == true) {
			tdev->thp_core->easy_wakeup_info.off_motion_on = false;
			*gesture_wakeup_value = TS_DOUBLE_CLICK;
		}
		mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
		return NO_ERR;
	}

	return -EINVAL;
}

static int touch_driver_wrong_touch(struct thp_device *tdev)
{
	if (!tdev->thp_core) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}

	if (tdev->thp_core->support_gesture_mode) {
		mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
		tdev->thp_core->easy_wakeup_info.off_motion_on = true;
		mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
		thp_log_info(tdev->thp_core, "%s, done\n", __func__);
	}
	return 0;
}

static int touch_driver_gesture_report(struct thp_device *tdev,
				       unsigned int *gesture_wakeup_value)
{
	unsigned char *data = spi_read_buf;
	int retval;
	u32 i;

	thp_log_info(tdev->thp_core, "%s enter\n", __func__);
	if ((!tdev->thp_core) || (!tdev->thp_core->sdev)) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}
	if ((gesture_wakeup_value == NULL) ||
	    (!tdev->thp_core->support_gesture_mode)) {
		thp_log_err(tdev->thp_core, "%s, gesture not support\n",
			    __func__);
		return -EINVAL;
	}
	if (!data) {
		thp_log_err(tdev->thp_core, "%s:data is NULL\n", __func__);
		return -EINVAL;
	}
	memset(data, 0, SPI_READ_WRITE_SIZE);
	retval = thp_bus_lock(tdev->thp_core);
	if (retval < 0) {
		thp_log_err(tdev->thp_core, "%s:get lock failed\n", __func__);
		return -EINVAL;
	}
	/* wait spi bus resume */
	for (i = 0; i < tdev->thp_core->gesture_retry_times; i++) {
		retval = touch_driver_spi_read(tdev->thp_core->sdev, data,
					       TOUCH_REPORT_CONFIG_SIZE);
		if (retval == TOUCH_REPORT_CONFIG_SIZE)
			break;
		thp_log_info(tdev->thp_core,
			     "%s: spi not work normal, ret %d retry\n",
			     __func__, retval);
		msleep(WAIT_FOR_SPI_BUS_READ_DELAY);
	}
	thp_bus_unlock(tdev->thp_core);

	if ((data[0] != MESSAGE_MARKER) ||
	    (data[1] != REPORT_TOUCH)) { /* data[1]: fw response status */
		thp_log_err(tdev->thp_core, "%s, data0~1: 0x%02x 0x%02x\n",
			    __func__, data[0], data[1]);
		return -EINVAL;
	}
	retval = touch_driver_parse_gesture_data(tdev, data,
						 gesture_wakeup_value);

	thp_log_info(tdev->thp_core, "%s exit\n", __func__);
	return retval;
}

int touch_driver_parse_ic_feature_config(struct device_node *thp_node,
					 struct thp_core_data *cd)
{
	int rc;
	unsigned int value = 0;

	thp_log_debug(cd, "%s:Enter!\n", __func__);
	cd->support_get_frame_read_once = 0;
	rc = of_property_read_u32(thp_node, "support_get_frame_read_once",
				  &value);
	if (!rc) {
		cd->support_get_frame_read_once = value;
		thp_log_info(cd, "%s:support_get_frame_read_once configed %u\n",
			     __func__, value);
	}

	rc = of_property_read_u32(
		thp_node, "support_ignore_first_irq_after_reset", &value);
	if (!rc) {
		cd->support_ignore_first_irq_after_reset = value;
		thp_log_info(cd, "%s:support_ignore_first_irq_after_reset %u\n",
			     __func__, value);
	}
	cd->get_frame_size_max = 0;
	rc = of_property_read_u32(thp_node, "get_frame_size_max", &value);
	if (!rc) {
		cd->get_frame_size_max = value;
		thp_log_info(cd, "%s:get_frame_size_max configed %u\n",
			     __func__, value);
	}
	cd->send_one_cmd_for_ap = 0;
	rc = of_property_read_u32(thp_node, "send_one_cmd_for_ap", &value);
	if (!rc) {
		cd->send_one_cmd_for_ap = value;
		thp_log_info(cd, "%s:send_one_cmd_for_ap %u\n", __func__,
			     value);
	}
	return 0;
}

static int touch_driver_init(struct thp_device *tdev)
{
	int rc;
	struct thp_core_data *cd = tdev->thp_core;
	struct device_node *syna_node =
		of_get_child_by_name(cd->thp_node, THP_SYNA_DEV_NODE_NAME);

	thp_log_info(cd, "%s: called\n", __func__);
	if (!syna_node) {
		thp_log_info(cd, "%s: dev not config in dts\n", __func__);
		return -ENODEV;
	}
	rc = thp_parse_spi_config(syna_node, cd);
	if (rc)
		thp_log_err(tdev->thp_core, "%s: spi config parse fail\n",
			    __func__);
	rc = thp_parse_timing_config(tdev->thp_core, syna_node,
				     &tdev->timing_config);
	if (rc)
		thp_log_err(tdev->thp_core, "%s: timing config parse fail\n",
			    __func__);
	rc = thp_parse_feature_config(syna_node, cd);
	if (rc)
		thp_log_err(tdev->thp_core, "%s: feature_config fail\n",
			    __func__);
	rc = touch_driver_parse_ic_feature_config(syna_node, cd);
	if (rc)
		thp_log_err(tdev->thp_core, "%s: ic_feature_config fail\n",
			    __func__);
	rc = thp_parse_trigger_config(syna_node, cd);
	if (rc)
		thp_log_err(tdev->thp_core, "%s: trigger_config fail\n",
			    __func__);
	rc = of_property_read_u32(syna_node, "support_deepsleep_mode",
				  &cd->support_deepsleep_mode);
	if (rc)
		cd->support_deepsleep_mode = 0;
	thp_log_info(cd, "%s: support_deepsleep_mode: %u\n", __func__,
		     cd->support_deepsleep_mode);
	return 0;
}

static int touch_driver_power_init(struct thp_core_data *cd)
{
	int ret;

	ret = thp_power_supply_get(cd, THP_VCC);
	if (ret)
		thp_log_err(cd, "%s: fail to get vcc power\n", __func__);
	ret = thp_power_supply_get(cd, THP_IOVDD);
	if (ret)
		thp_log_err(cd, "%s: fail to get power\n", __func__);
	return 0;
}

static int touch_driver_power_release(struct thp_core_data *cd)
{
	int ret;

	ret = thp_power_supply_put(cd, THP_VCC);
	if (ret)
		thp_log_err(cd, "%s: fail to release vcc power\n", __func__);
	ret = thp_power_supply_put(cd, THP_IOVDD);
	if (ret)
		thp_log_err(cd, "%s: fail to release power\n", __func__);
	return ret;
}

static int touch_driver_power_on(struct thp_device *tdev)
{
	int ret;
	struct thp_core_data *cd = tdev->thp_core;

	thp_log_info(cd, "%s:called\n", __func__);

	if (!cd) {
		thp_log_err(tdev->thp_core, "%s: cd null\n", __func__);
		return -EINVAL;
	}

	gpio_direction_input(tdev->gpios->irq_gpio);
	gpio_direction_output(tdev->gpios->rst_gpio, GPIO_LOW);
#if (!IS_ENABLED(CONFIG_HONOR_THP_MTK))
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);
#else
	pinctrl_select_state(tdev->thp_core->pctrl,
			     tdev->thp_core->mtk_pinctrl.cs_low);
#endif
	ret = thp_power_supply_ctrl(tdev->thp_core, THP_VCC, THP_POWER_ON,
				    1); /* delay 1ms */
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power ctrl fail\n", __func__);
	ret = thp_power_supply_ctrl(tdev->thp_core, THP_IOVDD, THP_POWER_ON,
				    cd->iovdd_power_on_delay_ms);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power ctrl vddio fail\n",
			    __func__);
#if (!IS_ENABLED(CONFIG_HONOR_THP_MTK))
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_HIGH);
#else
	pinctrl_select_state(tdev->thp_core->pctrl,
			     tdev->thp_core->mtk_pinctrl.cs_high);
#endif
	gpio_direction_output(tdev->gpios->rst_gpio, GPIO_HIGH);
	if (cd->support_vendor_ic_type == SYNA3909)
		thp_log_debug(cd, "%s: reset high without delay\n", __func__);
	else
		thp_do_time_delay(tdev->timing_config.boot_reset_hi_delay_ms);
	return ret;
}

static int touch_driver_power_off_for_3909(struct thp_device *tdev)
{
	int ret;
	struct thp_core_data *cd = tdev->thp_core;

	thp_log_debug(tdev->thp_core, "%s: in\n", __func__);
	if ((!tdev->gpios) || !cd) {
		thp_log_err(tdev->thp_core, "%s: have null ptr\n", __func__);
		return -EINVAL;
	}
	if (cd->support_deepsleep_mode) {
		thp_log_info(tdev->thp_core,
			     "%s: no power_off,  And in deepsleep mode~\n",
			     __func__);
		return pt_mode_set_for_3909(tdev);
	}
#if (!IS_ENABLED(CONFIG_HONOR_THP_MTK))
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);
#else
	if (cd->support_control_cs_off)
		pinctrl_select_state(tdev->thp_core->pctrl,
				     tdev->thp_core->mtk_pinctrl.cs_low);
#endif
	mdelay(POWER_OFF_DELAY_FOR_3909);
	gpio_direction_output(tdev->gpios->rst_gpio, GPIO_LOW);
	mdelay(POWER_OFF_DELAY_FOR_3909);
	ret = thp_power_supply_ctrl(tdev->thp_core, THP_IOVDD, THP_POWER_OFF,
				    POWER_OFF_DELAY_FOR_3909);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power ctrl fail\n", __func__);
	ret = thp_power_supply_ctrl(tdev->thp_core, THP_VCC, THP_POWER_OFF,
				    POWER_OFF_DELAY_FOR_3909);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power ctrl vcc fail\n",
			    __func__);

	return ret;
}

static int touch_driver_power_off(struct thp_device *tdev)
{
	int ret;

	if ((!tdev->thp_core) || (!tdev->gpios)) {
		thp_log_err(tdev->thp_core, "%s: have null ptr\n", __func__);
		return -EINVAL;
	}
	if (tdev->thp_core->support_vendor_ic_type == SYNA3909)
		return touch_driver_power_off_for_3909(tdev);
	gpio_direction_output(tdev->gpios->rst_gpio, GPIO_LOW);
	mdelay(tdev->timing_config.suspend_reset_after_delay_ms);
	ret = thp_power_supply_ctrl(tdev->thp_core, THP_VCC, THP_POWER_OFF,
				    VCC_DELAY);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power ctrl vcc fail\n",
			    __func__);
	ret = thp_power_supply_ctrl(tdev->thp_core, THP_IOVDD, THP_POWER_OFF,
				    IOVDD_DELAY);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power ctrl fail\n", __func__);
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);

	return ret;
}

static int touch_driver_get_flash_cmd(struct thp_device *tdev,
				      struct syna_tcm_boot_info *boot_info,
				      unsigned char *cmd_buf,
				      unsigned int cmd_len)
{
	int index = 0;
	unsigned int addr_words;
	unsigned int length_words;
	unsigned char *start_block = boot_info->boot_config_start_block;

	if (cmd_len != REFLASH_READ_LEN) {
		thp_log_err(tdev->thp_core, "%s:input invalidl\n", __func__);
		return -EINVAL;
	}
	addr_words = ((unsigned int)start_block[0] & 0x000000FF) |
		     (((unsigned int)start_block[1] << MOVE_8BIT) & 0x0000FF00);
	addr_words *= boot_info->write_block_size_words;
	length_words = BOOT_CONFIG_SIZE * BOOT_CONFIG_SLOTS;
	cmd_buf[index++] = CMD_READ_FLASH;
	cmd_buf[index++] = REFLASH_CMD_LEN_LOW;
	cmd_buf[index++] = REFLASH_CMD_LEN_HIGH;
	cmd_buf[index++] = (unsigned char)addr_words;
	cmd_buf[index++] = (unsigned char)(addr_words >> MOVE_8BIT);
	cmd_buf[index++] = (unsigned char)(addr_words >> MOVE_16BIT);
	cmd_buf[index++] = (unsigned char)(addr_words >> MOVE_24BIT);
	cmd_buf[index++] = (unsigned char)length_words;
	cmd_buf[index++] = (unsigned char)(length_words >> MOVE_8BIT);
	return 0;
}

#define ENTER_BOOTLOADER_MODE_DELAY 50
#define GET_BOOT_INFO_DELAY 10
#define GET_PROJECTID_DELAY 20

static void
touch_driver_enter_bootloader_mode(struct thp_device *tdev,
				   struct syna_tcm_identification *id_info)
{
	struct spi_device *sdev = tdev->thp_core->sdev;
	int retval;
	unsigned char cmd = CMD_RUN_BOOTLOADER_FIRMWARE;
	unsigned char *temp_buf = spi_read_buf;

	if (id_info->mode == MODE_APPLICATION) {
		retval = touch_driver_spi_write(sdev, &cmd, sizeof(cmd));
		if (retval < 0)
			thp_log_err(tdev->thp_core, "%s:spi write failed\n",
				    __func__);
		msleep(ENTER_BOOTLOADER_MODE_DELAY);
		retval = touch_driver_spi_read(sdev, temp_buf,
					       BOOT_CONFIG_SIZE *
						       BOOT_CONFIG_SLOTS * 2);
		if (retval < 0)
			thp_log_err(tdev->thp_core, "%s:spi read failed\n",
				    __func__);
		if (temp_buf[1] == REPORT_IDENTIFY)
			memcpy(id_info, &temp_buf[MESSAGE_HEADER_SIZE],
			       sizeof(*id_info));
		thp_log_info(tdev->thp_core,
			     "%s: value = 0x%x,expect value = 0x10\n", __func__,
			     temp_buf[1]);
	}
}

static int touch_driver_get_boot_info(struct thp_device *tdev,
				      struct syna_tcm_boot_info *pboot_info)
{
	struct spi_device *sdev = tdev->thp_core->sdev;
	int retval;
	unsigned char cmd;
	unsigned char *temp_buf = spi_read_buf;

	cmd = CMD_GET_BOOT_INFO;
	retval = touch_driver_spi_write(sdev, &cmd, sizeof(cmd));
	if (retval < 0)
		thp_log_err(tdev->thp_core, "%s:spi write failed\n", __func__);
	msleep(GET_BOOT_INFO_DELAY);
	touch_driver_spi_read(sdev, temp_buf,
			      (BOOT_CONFIG_SIZE * BOOT_CONFIG_SLOTS * 2));
	if (retval < 0)
		thp_log_err(tdev->thp_core, "%s:spi read failed\n", __func__);
	if (temp_buf[1] != STATUS_OK) {
		thp_log_err(tdev->thp_core, "%s:fail to get boot info\n",
			    __func__);
		return -EINVAL;
	}
	memcpy(pboot_info, &temp_buf[MESSAGE_HEADER_SIZE], sizeof(*pboot_info));
	return 0;
}

static void
touch_driver_reflash_read_boot_config(struct thp_device *tdev,
				      struct syna_tcm_identification *id_info)
{
	int retval;
	unsigned char *temp_buf = spi_read_buf;
	unsigned char *out_buf = spi_write_buf;
	struct syna_tcm_boot_info boot_info;
	struct spi_device *sdev = tdev->thp_core->sdev;

	retval = thp_bus_lock(tdev->thp_core);
	if (retval < 0) {
		thp_log_err(tdev->thp_core, "%s:get lock failed\n", __func__);
		return;
	}
	touch_driver_enter_bootloader_mode(tdev, id_info);

	thp_log_info(tdev->thp_core, "%s:id_info.mode = %d\n", __func__,
		     id_info->mode);
	if (id_info->mode == MODE_BOOTLOADER) {
		retval = touch_driver_get_boot_info(tdev, &boot_info);
		if (retval < 0) {
			thp_log_err(tdev->thp_core,
				    "%s:fail to get boot info\n", __func__);
			goto exit;
		}
	}
	memcpy(&boot_info, &temp_buf[MESSAGE_HEADER_SIZE], sizeof(boot_info));
	retval = touch_driver_get_flash_cmd(tdev, &boot_info, out_buf,
					    REFLASH_READ_LEN);
	if (retval < 0) {
		thp_log_err(tdev->thp_core, "%s:fail to get flash cmd\n",
			    __func__);
		goto exit;
	}
	retval = touch_driver_spi_write(sdev, out_buf, REFLASH_READ_LEN);
	if (retval < 0)
		thp_log_err(tdev->thp_core, "%s:reflash read failed\n",
			    __func__);
	msleep(GET_PROJECTID_DELAY);
	retval = touch_driver_spi_read(
		sdev, temp_buf, (BOOT_CONFIG_SIZE * BOOT_CONFIG_SLOTS * 2));
	if (retval < 0) {
		thp_log_err(tdev->thp_core, "%s:fail to read boot config\n",
			    __func__);
		goto exit;
	}
	/* success get project iD from tp ic */
	get_project_id_flag = 1;
	memcpy(tdev->thp_core->project_id, &temp_buf[IC_PROJECT_ID_START],
	       THP_PROJECT_ID_LEN);
	if (tdev->thp_core->project_id[0] == 0)
		memcpy(tdev->thp_core->project_id,
		       &temp_buf[IC_PROJECT_ID_START], THP_PROJECT_ID_LEN);
exit:
	thp_bus_unlock(tdev->thp_core);
}

static int touch_driver_chip_detect_for_tddi(struct thp_device *tdev)
{
	unsigned char *rmiaddr = spi_write_buf;
	unsigned char fnnum = 0;
	int rc;

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	rc = thp_bus_lock(tdev->thp_core);
	if (rc < 0) {
		thp_log_err(tdev->thp_core, "%s:get lock failed\n", __func__);
		rc = -EINVAL;
		goto exit;
	}
	rmiaddr[0] = RMI_ADDR_FIRST;
	rmiaddr[1] = RMI_ADDR_SECOND;
	rc = touch_driver_spi_write(tdev->thp_core->sdev, rmiaddr, RMI_CMD_LEN);
	if (rc < 0)
		thp_log_err(tdev->thp_core, "%s: spi write failed\n", __func__);
	rc = touch_driver_spi_read(tdev->thp_core->sdev, &fnnum, sizeof(fnnum));
	if (rc < 0)
		thp_log_err(tdev->thp_core, "%s:spi read failed\n", __func__);
	thp_bus_unlock(tdev->thp_core);
	if ((fnnum != UBL_FN_NUMBER) && (fnnum != MESSAGE_MARKER)) {
		thp_log_err(tdev->thp_core, "%s: fnnum error: 0x%02x\n",
			    __func__, fnnum);
		rc = -ENODEV;
		goto exit;
	}
	thp_log_err(tdev->thp_core, "%s: fnnum error: 0x%02x\n", __func__,
		    fnnum);
	return 0;
exit:
	return rc;
}

static int touch_driver_chip_detect_3909(struct thp_device *tdev)
{
	int ret;
	unsigned char *tx_buf = spi_read_buf;
	struct syna_tcm_identification id_info;
	int i;

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);

	ret = touch_driver_power_init(tdev->thp_core);
	if (ret)
		thp_log_err(tdev->thp_core, "%s: power init failed\n",
			    __func__);
	ret = touch_driver_power_on(tdev);
	if (ret)
		thp_log_err(tdev->thp_core, "%s: power on failed\n", __func__);

	thp_do_time_delay(tdev->timing_config.boot_reset_hi_delay_ms);

	ret = thp_bus_lock(tdev->thp_core);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s:get lock failed\n", __func__);
		return -EINVAL;
	}
	ret = touch_driver_spi_read(tdev->thp_core->sdev, tx_buf,
				    MESSAGE_DATA_NUM);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s: failed to read data\n",
			    __func__);
		thp_bus_unlock(tdev->thp_core);
		return -ENODEV;
	}
	thp_bus_unlock(tdev->thp_core);

	if (tx_buf[0] != MESSAGE_MARKER) {
		thp_log_err(tdev->thp_core, "%s: message_marker error\n",
			    __func__);
		for (i = 0; i < MESSAGE_DATA_NUM; i++)
			thp_log_info(tdev->thp_core, "buf[i] = %d\n",
				     tx_buf[i]);
		ret = touch_driver_power_off(tdev);
		if (ret)
			thp_log_err(tdev->thp_core, "%s: power off failed\n",
				    __func__);
		ret = touch_driver_power_release(tdev->thp_core);
		if (ret < 0) {
			thp_log_err(tdev->thp_core, "%s: power ctrl Failed\n",
				    __func__);
			return ret;
		}
		return -ENODEV;
	}
	thp_log_info(tdev->thp_core, "%s:device detected\n", __func__);

	memcpy(&id_info, &tx_buf[MESSAGE_HEADER_SIZE], sizeof(id_info));
	touch_driver_reflash_read_boot_config(tdev, &id_info);
	thp_log_info(tdev->thp_core, "%s: message_marker succ\n", __func__);
	return 0;
}

static int touch_driver_chip_detect(struct thp_device *tdev)
{
	int ret = -EINVAL;

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);

	ret = touch_driver_prepare(tdev);
	if (ret) {
		thp_log_err(tdev->thp_core, "%s: prepare fail ret: %d\n",
			    __func__, ret);
		goto exit;
	}

	if (tdev->thp_core->self_control_power) {
		ret = touch_driver_chip_detect_3909(tdev);
		if (ret < 0) {
			thp_log_err(tdev->thp_core, "%s: fail\n", __func__);
			goto exit;
		}
	} else {
		ret = touch_driver_chip_detect_for_tddi(tdev);
		if (ret < 0) {
			thp_log_err(tdev->thp_core, "%s: fail\n", __func__);
			goto exit;
		}
	}
	thp_log_info(tdev->thp_core, "%s: succ\n", __func__);
	return 0;
exit:
	touch_driver_unprepare(tdev);
	if (tdev->thp_core->fast_booting_solution) {
		kfree(tdev->tx_buff);
		tdev->tx_buff = NULL;
		kfree(tdev->rx_buff);
		tdev->rx_buff = NULL;
		kfree(tdev);
		tdev = NULL;
	}
	return ret;
}

#define SYNA_FRAME_SIZE_MAX 2256
#define SYNA_FRAME_STATUS_ERROR 0xFF
static int touch_driver_get_frame_read_once(struct thp_device *tdev,
					    char *frame_buf, unsigned int len)
{
	unsigned int length = SYNA_FRAME_SIZE_MAX;
	int retval;

	if (length > len) {
		thp_log_err(tdev->thp_core, "%s:frame len error len = %u\n",
			    __func__, len);
		return -EINVAL;
	}
	if (tdev->thp_core->get_frame_size_max)
		length = tdev->thp_core->get_frame_size_max;
	retval = touch_driver_event_read_and_dispatch(
		tdev, (unsigned char *)frame_buf, len, length);
	if (retval != SYNA_DISPATCH_REPORT)
		return -ENODATA;

	if (frame_buf[1] == SYNA_FRAME_STATUS_ERROR) {
		thp_log_err(tdev->thp_core, "%s: should ignore this irq\n",
			    __func__);
		return -ENODATA;
	}
	return NO_ERR;
}

static int touch_driver_get_frame(struct thp_device *tdev, char *frame_buf,
				  unsigned int len)
{
	unsigned char *data = spi_read_buf;
	unsigned int length;
	int retval;

	if (tdev->thp_core->support_get_frame_read_once ==
	    SUPPORT_GET_FRAME_READ_ONCE)
		return touch_driver_get_frame_read_once(tdev, frame_buf, len);

	if (!data) {
		thp_log_err(tdev->thp_core, "%s:data is NULL\n", __func__);
		return -EINVAL;
	}

	if (tdev->thp_core->support_ignore_first_irq_after_reset &&
	    (ignore_irq_after_reset > 0)) {
		ignore_irq_after_reset--;
		thp_log_err(tdev->thp_core,
			    "%s:ignore_irq_after_reset count is %u\n", __func__,
			    ignore_irq_after_reset);
		return -EINVAL;
	}

	retval = thp_bus_lock(tdev->thp_core);
	if (retval < 0) {
		thp_log_err(tdev->thp_core, "%s:get lock failed\n", __func__);
		return -EINVAL;
	}
	retval = touch_driver_spi_read(tdev->thp_core->sdev, data,
				       FRAME_HEAD_LEN);
	if (retval < 0) {
		thp_log_err(tdev->thp_core, "%s: Failed to read length\n",
			    __func__);
		goto ERROR;
	}
	if (data[1] == 0xFF) {
		thp_log_err(tdev->thp_core, "%s: should ignore this irq\n",
			    __func__);
		retval = -ENODATA;
		goto ERROR;
	}
	if (data[0] != MESSAGE_MARKER) {
		thp_log_err(tdev->thp_core, "%s: incorrect marker: 0x%02x\n",
			    __func__, data[0]);
		if (data[1] == STATUS_CONTINUED_READ) {
			// just in case
			thp_log_err(tdev->thp_core, "%s: continued Read\n",
				    __func__);
			touch_driver_spi_read(
				tdev->thp_core->sdev, tdev->rx_buff,
				THP_MAX_FRAME_SIZE); /* drop one transaction */
		}
		retval = -ENODATA;
		goto ERROR;
	}

	length = (data[3] << 8) | data[2];
	if (length > (THP_MAX_FRAME_SIZE - FIRST_FRAME_USEFUL_LEN)) {
		thp_log_info(tdev->thp_core, "%s: out of length\n", __func__);
		length = THP_MAX_FRAME_SIZE - FIRST_FRAME_USEFUL_LEN;
	}
	if (length) {
		retval = touch_driver_spi_read(
			tdev->thp_core->sdev,
			frame_buf + FIRST_FRAME_USEFUL_LEN,
			length + FIRST_FRAME_USEFUL_LEN); /* read packet */
		if (retval < 0) {
			thp_log_err(tdev->thp_core,
				    "%s: Failed to read length\n", __func__);
			goto ERROR;
		}
	}
	thp_bus_unlock(tdev->thp_core);
	memcpy(frame_buf, data, FRAME_HEAD_LEN);
	return 0;

ERROR:
	thp_bus_unlock(tdev->thp_core);
	return retval;
}

static void touch_driver_gesture_mode_enable_switch(struct thp_device *tdev,
						    unsigned int value)
{
	int ret_disable_frame;
	int ret_set_config;

	ret_disable_frame = touch_driver_disable_frame(tdev, value);
	ret_set_config = touch_driver_set_dynamic_config(
		tdev, DC_IN_WAKEUP_GESTURE_MODE, value);
	if (ret_disable_frame || ret_set_config)
		thp_log_err(tdev->thp_core, "%s, ret is %d %d\n", __func__,
			    ret_disable_frame, ret_set_config);
	mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
	tdev->thp_core->easy_wakeup_info.off_motion_on = true;
	mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
}

static int touch_driver_resume(struct thp_device *tdev)
{
	int ret;
	struct spi_device *sdev = NULL;
	/* report irq to ap cmd */
	char report_to_ap_cmd[SYNA_CMD_LEN] = {0xC7, 0x02, 0x00, 0x2E, 0x00};
	enum ts_sleep_mode gesture_status;

#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	ud_mode_status.lowpower_mode = 0;
#endif

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);

	if (!tdev->thp_core || !tdev->thp_core->sdev) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}
	sdev = tdev->thp_core->sdev;
	gesture_status = tdev->thp_core->easy_wakeup_info.sleep_mode;
	if (tdev->thp_core->self_control_power) {
		if (is_pt_test_mode(tdev)) {
			gpio_direction_output(tdev->gpios->rst_gpio, GPIO_LOW);
			mdelay(tdev->timing_config.resume_reset_after_delay_ms);
			gpio_direction_output(tdev->gpios->rst_gpio, GPIO_HIGH);
		} else if (need_power_off == NEED_WORK_IN_SUSPEND) {
			thp_log_info(tdev->thp_core, "%s:change irq to AP\n",
				     __func__);
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
			if (tdev->thp_core->use_ap_gesture)
				report_to_ap_cmd[3] = 0x4C; /* udfp cmd 0x4C */
#endif
			touch_driver_send_one_cmd(tdev, report_to_ap_cmd,
						  sizeof(report_to_ap_cmd));
		} else {
			ret = touch_driver_power_on(tdev);
			if (ret < 0) {
				thp_log_err(tdev->thp_core,
					    "%s: power on Failed\n", __func__);
				return ret;
			}
		}
	} else {
		if (gesture_status == TS_GESTURE_MODE &&
		    tdev->thp_core->lcd_gesture_mode_support) {
			thp_log_info(tdev->thp_core, "gesture mode exit\n");
			gpio_direction_output(tdev->gpios->rst_gpio, GPIO_LOW);
			mdelay(10); /* 10ms sequence delay */
			gpio_direction_output(tdev->gpios->rst_gpio, GPIO_HIGH);
		} else {
#if (!IS_ENABLED(CONFIG_HONOR_THP_MTK))
			gpio_set_value(tdev->gpios->cs_gpio, 1);
#else
			pinctrl_select_state(
				tdev->thp_core->pctrl,
				tdev->thp_core->mtk_pinctrl.cs_high);
#endif
			/* keep TP rst  high before LCD  reset hign */
			gpio_direction_output(tdev->gpios->rst_gpio, GPIO_HIGH);
		}
	}
	thp_log_info(tdev->thp_core, "%s: called end\n", __func__);
	return 0;
}

#define SYNA_READ_PT_MODE_FLAG_RETRY_TIMES 10
#define SYNA_PT_PKG_HEAD 0xa5
#define SYNA_TP_BUSY 0x0d
#define SYNA_PT_PKG_HEAD_OFFSET 1
#define SYNA_PT_PKG_LEN 0
#define SYNA_NO_RETRY 0
#define SYNA_RETRY 1

static int pt_mode_set_for_3909(const struct thp_device *tdev)
{
	int ret;
	int i;
	unsigned char *data = spi_read_buf;
	/* pt station cmd */
	u8 syna_sleep_cmd[SYNA_COMMAMD_LEN] = {0x2C, 0x00, 0x00};

	ret = thp_bus_lock(tdev->thp_core);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s:get lock failed\n", __func__);
		return -EINVAL;
	}
	memset(spi_write_buf, 0, SPI_READ_WRITE_SIZE);
	memcpy(spi_write_buf, syna_sleep_cmd, SYNA_COMMAMD_LEN);
	ret = touch_driver_spi_write(tdev->thp_core->sdev, spi_write_buf,
				     SYNA_COMMAMD_LEN);
	if (ret < 0)
		thp_log_err(tdev->thp_core, "Failed to send active command\n");
	thp_log_info(tdev->thp_core, "%s write ret = %d\n", __func__, ret);

	/* TP chip needs to read redundant frame,then goto idle */
	for (i = 0; i < SYNA_READ_PT_MODE_FLAG_RETRY_TIMES; i++) {
		ret = touch_driver_spi_read(tdev->thp_core->sdev, data,
					    FRAME_HEAD_LEN);
		if (ret < 0)
			thp_log_err(tdev->thp_core,
				    "%s: Failed to read length\n", __func__);
		thp_log_debug(tdev->thp_core, "data: %*ph\n", FRAME_HEAD_LEN,
			      data);
		ret = touch_driver_spi_read(tdev->thp_core->sdev, tdev->rx_buff,
					    THP_MAX_FRAME_SIZE);
		if (ret < 0)
			thp_log_err(tdev->thp_core,
				    "%s: Failed to read length\n", __func__);
		thp_log_debug(tdev->thp_core, "rx_buff: %*ph\n", FRAME_HEAD_LEN,
			      tdev->rx_buff);
		/* read 0xa5 0x1 0x0 0x0 is success from tp ic */
		if ((data[0] == SYNA_PT_PKG_HEAD) &&
		    (data[1] == SYNA_PT_PKG_HEAD_OFFSET) &&
		    (data[2] == SYNA_PT_PKG_LEN) &&
		    (data[3] == SYNA_PT_PKG_LEN)) {
			thp_log_info(tdev->thp_core, "%s :get flag success\n",
				     __func__);
			break;
		}
		thp_log_err(tdev->thp_core, "%s :get flag failed\n", __func__);
		ret = -EINVAL;
	}
	thp_bus_unlock(tdev->thp_core);
	return ret;
}

static int pt_mode_set(const struct thp_device *tdev)
{
	int ret;
	unsigned char *data = spi_read_buf;
	/* pt station cmd */
	u8 syna_sleep_cmd[SYNA_COMMAMD_LEN] = {0x2C, 0x00, 0x00};

	if (tdev->thp_core == NULL) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}
	if (tdev->thp_core->support_vendor_ic_type == SYNA3909)
		return pt_mode_set_for_3909(tdev);

	ret = thp_bus_lock(tdev->thp_core);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s:get lock failed\n", __func__);
		return -EINVAL;
	}
	memset(spi_write_buf, 0, SPI_READ_WRITE_SIZE);
	memcpy(spi_write_buf, syna_sleep_cmd, SYNA_COMMAMD_LEN);
	ret = touch_driver_spi_write(tdev->thp_core->sdev, spi_write_buf,
				     SYNA_COMMAMD_LEN);
	if (ret < 0)
		thp_log_err(tdev->thp_core,
			    "Failed to send syna active command\n");
	ret = touch_driver_spi_read(tdev->thp_core->sdev, data, FRAME_HEAD_LEN);
	if (ret < 0)
		thp_log_err(tdev->thp_core, "%s: Failed to read length\n",
			    __func__);
	ret = touch_driver_spi_read(tdev->thp_core->sdev, tdev->rx_buff,
				    THP_MAX_FRAME_SIZE);
	if (ret < 0)
		thp_log_err(tdev->thp_core, "%s: Failed to read length\n",
			    __func__);
	thp_bus_unlock(tdev->thp_core);
	return ret;
}

#ifdef CONFIG_HONOR_SHB_THP
#define INPUTHUB_POWER_SWITCH_START_BIT 9
#define INPUTHUB_POWER_SWITCH_START_OFFSET 1
static void touch_driver_get_poweroff_status(void)
{
	struct thp_core_data *cd = thp_get_core_data();
	unsigned int finger_status = !!(thp_get_status(THP_STATUS_UDFP));
	unsigned int stylus_status = (thp_get_status(THP_STATUS_STYLUS)) |
				     (thp_get_status(THP_STATUS_STYLUS3));

	cd->poweroff_function_status =
		(cd->double_click_switch << THP_DOUBLE_CLICK_ON) |
		(finger_status << THP_TPUD_ON) |
		(cd->ring_setting_switch << THP_RING_SUPPORT) |
		(cd->ring_switch << THP_RING_ON) |
		(stylus_status << THP_PEN_MODE_ON) |
		(cd->phone_status << THP_PHONE_STATUS) |
		(cd->single_click_switch << THP_SINGLE_CLICK_ON) |
		(cd->volume_side_status << THP_VOLUME_SIDE_STATUS_LEFT);
	if (cd->aod_display_support)
		cd->poweroff_function_status |=
			(cd->aod_touch_status << THP_AOD_TOUCH_STATUS);
	if ((cd->power_switch >= POWER_KEY_OFF_CTRL) &&
	    (cd->power_switch < POWER_MAX_STATUS))
		/*
	 * cd->poweroff_function_status 0~8 bit saved function flag
	 * eg:double_click, finger_status, ring_switch,and so on.
	 * cd->poweroff_function_status 9~16 bit saved screen-on-off reason flag
	 * cd->power_switch is a value(1~8) which stand for screen-on-off reason
	 */
		cd->poweroff_function_status |=
			(1 << (INPUTHUB_POWER_SWITCH_START_BIT +
			       cd->power_switch -
			       INPUTHUB_POWER_SWITCH_START_OFFSET));
}
#endif

static void need_work_in_suspend_for_shb(struct thp_device *tdev)
{
	int ret;
	char report_to_shb_cmd[SYNA_CMD_LEN] = {0xC7, 0x02, 0x00, 0x2E, 0x01};

	thp_log_info(tdev->thp_core, "%s:change irq to sensorhub\n", __func__);
	ret = thp_bus_lock(tdev->thp_core);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s:get lock failed\n", __func__);
		return;
	}
	memset(spi_write_buf, 0, SPI_READ_WRITE_SIZE);
	memcpy(spi_write_buf, report_to_shb_cmd, SYNA_CMD_LEN);
	ret = touch_driver_spi_write(tdev->thp_core->sdev, spi_write_buf,
				     SYNA_CMD_LEN);
	if (ret < 0)
		thp_log_err(tdev->thp_core, "%s: send cmd failed\n", __func__);
	thp_bus_unlock(tdev->thp_core);
}

#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
static int touch_driver_tui_enable_switch(struct thp_device *tdev,
					  u8 switch_value)
{
	unsigned char enable_tui_first_cmd[SYNA_TUI_CMD_LEN] = {0x06, 0x01,
								0x00, 0xc0};
	unsigned char enable_tui_second_cmd[SYNA_TUI_CMD_LEN] = {0xc7, 0x01,
								 0x00, 0x14};
	unsigned char disable_tui_cmd[SYNA_TUI_CMD_LEN] = {0xc7, 0x01, 0x00,
							   0x15};
	struct syna_tcm_identification *private_data =
		(struct syna_tcm_identification *)tdev->private_data;

	if (switch_value) {
		touch_driver_send_one_cmd(tdev, enable_tui_first_cmd,
					  sizeof(enable_tui_first_cmd));
		/* delay 5ms for avoiding ic not entering tui mode */
		mdelay(5);
		touch_driver_send_one_cmd(tdev, enable_tui_second_cmd,
					  sizeof(enable_tui_second_cmd));
		thp_log_info(tdev->thp_core, "enable tui mode\n");
	} else {
		touch_driver_send_one_cmd(tdev, disable_tui_cmd,
					  sizeof(disable_tui_cmd));
		thp_log_info(tdev->thp_core, "disable tui mode\n");
	}
	private_data->tui_finger_last_status = TUI_FINGER_UP;

	thp_log_info(tdev->thp_core, "%s, send cmd to TP IC suc\n", __func__);
	return 0;
}

// when tp ic is reset, let tp ic enter TUI mode again
static int touch_driver_set_tui_one_cmd(struct thp_device *tdev)
{
	unsigned char report_to_ap_cmd[SYNA_TUI_CMD_LEN] = {0xC7, 0x01, 0x00,
							    0x14};

	thp_log_info(tdev->thp_core, "%s enter tui again\n", __func__);
	touch_driver_send_one_cmd(tdev, report_to_ap_cmd,
				  sizeof(report_to_ap_cmd));
	return 0;
}

static int touch_driver_set_ap_one_cmd(struct thp_device *tdev)
{
	unsigned char report_to_ap_cmd[SYNA_ONE_CMD_LEN] = {0xC7, 0x03, 0x00,
							    0x4C, 0x02, 0x00};
	/* 05 C0: open frame and stylus_gesture uploading
	 * C7 1A: open stylus uplink signal
	 */
	unsigned char cmd_enable[FRAME_CMD_LEN] = {0x05, 0x01, 0x00, 0xC0};
	unsigned char pen_enable_cmd[PEN_CMD_LEN] = {0xC7, 0x01, 0x00, 0x1A};
	unsigned int finger_status;
	struct thp_core_data *cd = tdev->thp_core;

	finger_status = !!(thp_get_status(tdev->thp_core, THP_STATUS_UDFP));
	/* cmd[5]: determine different status */
	if (tdev->thp_core->easy_wakeup_info.sleep_mode == TS_GESTURE_MODE)
		report_to_ap_cmd[5] += SYNA_GESTURE_ONE_CMD;
	if (finger_status)
		report_to_ap_cmd[5] += SYNA_FINGER_ONE_CMD;
	if (cd->aod_touch_status || cd->standby_enable)
		report_to_ap_cmd[5] += SYNA_SINGLE_CLICK_ONE_CMD;
	if (tdev->thp_core->stylus_gesture_status)
		report_to_ap_cmd[5] += SYNA_STYLUS_CMD;
	thp_log_info(tdev->thp_core, "ap cmd: %*ph\n", SYNA_ONE_CMD_LEN,
		     report_to_ap_cmd);

	touch_driver_send_one_cmd(tdev, report_to_ap_cmd,
				  sizeof(report_to_ap_cmd));

	if (thp_get_status(tdev->thp_core, THP_STATUS_STYLUS3) &&
	    tdev->thp_core->stylus_gesture_status) {
		mdelay(2);
		touch_driver_send_one_cmd(tdev, cmd_enable, sizeof(cmd_enable));
		mdelay(2);
		touch_driver_send_one_cmd(tdev, pen_enable_cmd,
					  sizeof(pen_enable_cmd));
	}

	return 0;
}

static int touch_driver_set_ap_state(struct thp_device *tdev)
{
	unsigned char report_to_ap_cmd[SYNA_CMD_LEN] = {0xC7, 0x02, 0x00, 0x4C,
							0x01};
	unsigned char report_to_ap_cmd_gesture[SYNA_CMD_GESTURE_LEN] = {
		0xC7, 0x05, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00};
	unsigned char syna_sleep_cmd[SYNA_COMMAMD_LEN] = {0x2C, 0x00, 0x00};
	unsigned int finger_status;
	struct syna_tcm_identification *private_data =
		(struct syna_tcm_identification *)tdev->private_data;

	if ((private_data->stylus_gesture_status_only &&
	     !thp_get_status(tdev->thp_core, THP_STATUS_STYLUS3)) ||
	    (private_data->standby_enable_only &&
	     !tdev->thp_core->in_standby_state)) {
		touch_driver_send_one_cmd(tdev, syna_sleep_cmd,
					  sizeof(syna_sleep_cmd));
		if (private_data->stylus_gesture_status_only)
			private_data->stylus_gesture_status_lowpower = true;
		else
			private_data->standby_enable_in_lowpower = true;
		thp_log_info(
			tdev->thp_core,
			"gesture enable only set lowpower state, stylus:%d,standby:%d\n",
			private_data->stylus_gesture_status_only,
			private_data->standby_enable_only);
		return 0;
	}
	finger_status = !!(thp_get_status(tdev->thp_core, THP_STATUS_UDFP));
	if (!tdev->thp_core) {
		thp_log_err(tdev->thp_core, "%s:have null pointer\n", __func__);
		return -EINVAL;
	}
	if (tdev->thp_core->send_one_cmd_for_ap) {
		touch_driver_set_ap_one_cmd(tdev);
		return 0;
	}
	touch_driver_send_one_cmd(tdev, report_to_ap_cmd,
				  sizeof(report_to_ap_cmd));
	if (tdev->thp_core->easy_wakeup_info.sleep_mode == TS_GESTURE_MODE)
		report_to_ap_cmd_gesture[TOUCH_GESTURE_CMD] |=
			THP_GESTURE_DOUBLE_CLICK;
	if (finger_status)
		report_to_ap_cmd_gesture[TOUCH_GESTURE_CMD] |=
			THP_GESTURE_FINGER;
	touch_driver_send_one_cmd(tdev, report_to_ap_cmd_gesture,
				  sizeof(report_to_ap_cmd_gesture));
	return 0;
}
#endif

static int touch_driver_start_compute_active_doze_time(struct thp_device *tdev)
{
	unsigned char start_compute_cmd[] = {0xC7, 0x01, 0x00, 0x5A};

	return touch_driver_send_one_cmd(tdev, start_compute_cmd,
					 sizeof(start_compute_cmd));
}

static int touch_driver_get_active_doze_time(struct thp_device *tdev,
					     unsigned long long *active_time,
					     unsigned long long *doze_time)
{
	int ret;
	unsigned int *active;
	unsigned int *doze;
	struct syna_tcm_identification *syna_tcm = NULL;
	unsigned char get_time_cmd[] = {0xC7, 0x01, 0x00, 0x5B};

	if (!tdev)
		return -EINVAL;
	if (!tdev->thp_core || !active_time || !doze_time) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}
	syna_tcm = (struct syna_tcm_identification *)tdev->private_data;
	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	ret = touch_driver_send_one_cmd(tdev, get_time_cmd,
					sizeof(get_time_cmd));
	if (ret < 0) {
		thp_log_err(tdev->thp_core,
			    "%s: get active_doze_time fail :%d\n", __func__,
			    ret);
		return -EIO;
	}
	/*
	 * small end model; 4~7 bytes of data is active time
	 * 8~11 bytes of data is doze time
	 * the active data unit is 8.3ms
         * the doze data unit is 10ms
	 */
	active = (unsigned int *)&(syna_tcm->cmd_sync.resp.buf[4]);
	doze = (unsigned int *)&(syna_tcm->cmd_sync.resp.buf[8]);
	*active_time = ((uint64_t)(*active)) * 25 / 3;
	*doze_time = ((uint64_t)(*doze)) * 10;
	return NO_ERR;
}

static void need_work_in_suspend_for_ap(struct thp_device *tdev)
{
	int ret;
	char report_to_ap_cmd[SYNA_CMD_LEN] = {0xC7, 0x02, 0x00, 0x2A, 0x01};

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
	if (tdev->thp_core->use_ap_gesture) {
		ret = touch_driver_set_ap_state(tdev);
		if (ret)
			thp_log_err(tdev->thp_core, "failed to set ap state\n");
		return;
	}
#endif
	thp_log_info(tdev->thp_core, "%s:thp need work in suspend\n", __func__);
	if (tdev->thp_core->easy_wakeup_info.sleep_mode == TS_GESTURE_MODE) {
		thp_log_info(tdev->thp_core, "%s:thp gesture mode enter\n",
			     __func__);
		ret = thp_bus_lock(tdev->thp_core);
		if (ret < 0) {
			thp_log_err(tdev->thp_core, "%s:get lock fail,ret=%d\n",
				    __func__, ret);
			return;
		}
		memset(spi_write_buf, 0, SPI_READ_WRITE_SIZE);
		memcpy(spi_write_buf, report_to_ap_cmd,
		       sizeof(report_to_ap_cmd));
		ret = touch_driver_spi_write(tdev->thp_core->sdev,
					     spi_write_buf, SYNA_CMD_LEN);
		thp_bus_unlock(tdev->thp_core);
		if (ret < 0)
			thp_log_err(tdev->thp_core, "%s:send cmd fail,ret=%d\n",
				    __func__, ret);
		mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
		tdev->thp_core->easy_wakeup_info.off_motion_on = true;
		mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
	}
}

static int touch_driver_suspend(struct thp_device *tdev)
{
	int ret;
	struct spi_device *sdev = NULL;
	struct thp_core_data *cd = tdev->thp_core;
	unsigned int gesture_status;
	unsigned int finger_status;
	unsigned int stylus_status;
	unsigned char pen_disable_cmd[PEN_CMD_LEN] = {0xC7, 0x01, 0x00, 0x1B};
	struct syna_tcm_identification *private_data =
		(struct syna_tcm_identification *)tdev->private_data;

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);

	if (!tdev->thp_core || !tdev->thp_core->sdev || !cd) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}
	sdev = tdev->thp_core->sdev;
	gesture_status = !!(tdev->thp_core->easy_wakeup_info.sleep_mode);
	finger_status = !!(thp_get_status(tdev->thp_core, THP_STATUS_UDFP));
	stylus_status = thp_get_status(tdev->thp_core, THP_STATUS_STYLUS3);
	private_data->stylus_gesture_status_only =
		(cd->stylus_gesture_status && !cd->aod_touch_status &&
		 !gesture_status && !finger_status && !cd->standby_enable);
	private_data->standby_enable_only =
		(!cd->stylus_gesture_status && !cd->aod_touch_status &&
		 !gesture_status && !finger_status && cd->standby_enable);
	thp_log_info(
		tdev->thp_core,
		"%s:gesture_status:%u,finger_status:%u,stylus_status=%u,standby_enable:%u\n",
		__func__, gesture_status, finger_status, stylus_status,
		cd->standby_enable);
	thp_log_info(
		tdev->thp_core,
		"%s:ring_support:%d,ring_switch:%u,phone_status:%u,ring_setting_switch:%u\n",
		__func__, cd->support_ring_feature, cd->ring_switch,
		cd->phone_status, cd->ring_setting_switch);
	thp_log_info(tdev->thp_core, "%s:aod_touch_status:%u\n", __func__,
		     cd->aod_touch_status);
	thp_log_info(tdev->thp_core,
		     "%s:force_power_off:%u, fold_force_power_off:%u\n",
		     __func__, cd->force_power_off,
		     cd->easy_wakeup_info.fold_force_power_off);

	if (((gesture_status == TS_GESTURE_MODE) || finger_status ||
	     cd->ring_setting_switch || cd->stylus_gesture_status ||
	     cd->aod_touch_status || cd->standby_enable) &&
	    !(cd->force_power_off ||
	      cd->easy_wakeup_info.fold_force_power_off)) {
		thp_set_irq_status(cd, THP_IRQ_ENABLE);
		if ((stylus_status > 0) && (cd->stylus_gesture_status == 0))
			touch_driver_send_one_cmd(tdev, pen_disable_cmd,
						  sizeof(pen_disable_cmd));
#ifdef CONFIG_HONOR_SHB_THP
		if (cd->support_shb_thp)
			touch_driver_get_poweroff_status();
		if (tdev->thp_core->tsa_event_to_udfp && finger_status) {
			ret = send_tp_ap_event(sizeof(finger_status),
					       (void *)&finger_status,
					       ST_CMD_TYPE_FINGERPRINT_SWITCH);
			thp_log_info(tdev->thp_core,
				     "%s:tsa_event_to_udfp, ret = %d\n",
				     __func__, ret);
		}
#endif
		need_power_off = NEED_WORK_IN_SUSPEND;
		thp_set_irq_wake_status(cd, THP_IRQ_WAKE_ENABLE);
	} else {
		if (cd->support_shb_thp)
			/* 0:all function was closed */
			cd->poweroff_function_status = 0;
		need_power_off = NO_NEED_WORK_IN_SUSPEND;
	}
	if (tdev->thp_core->self_control_power) {
		if (is_pt_test_mode(tdev)) {
			thp_log_info(tdev->thp_core, "%s: suspend PT mode\n",
				     __func__);
			ret = pt_mode_set(tdev);
			if (ret < 0)
				thp_log_err(tdev->thp_core,
					    "%s: failed to set pt mode\n",
					    __func__);
		} else if (need_power_off == NEED_WORK_IN_SUSPEND) {
			if (cd->support_shb_thp)
				need_work_in_suspend_for_shb(tdev);
			else
				need_work_in_suspend_for_ap(tdev);
			if (cd->support_get_active_doze_time) {
				ret = touch_driver_start_compute_active_doze_time(
					tdev);
				if (ret < 0)
					thp_log_err(
						tdev->thp_core,
						"%s: start active_doze_time fail :%d\n",
						__func__, ret);
			}
		} else {
			thp_set_irq_status(cd, THP_IRQ_DISABLE);
			ret = touch_driver_power_off(tdev);
			if (ret < 0) {
				thp_log_err(tdev->thp_core,
					    "%s: power off Failed\n", __func__);
				return ret;
			}
		}
	} else {
		if (gesture_status == TS_GESTURE_MODE &&
		    cd->lcd_gesture_mode_support) {
			thp_log_info(tdev->thp_core, "gesture mode enter\n");
			/*
			 * 120ms sequence delay,
			 * make sure gesture mode enable success.
			 */
			msleep(120);
			touch_driver_gesture_mode_enable_switch(
				tdev, SYNA_ENTER_GUESTURE_MODE);
		} else {
			gpio_direction_output(tdev->gpios->rst_gpio, GPIO_LOW);
#if (!IS_ENABLED(CONFIG_HONOR_THP_MTK))
			gpio_set_value(tdev->gpios->cs_gpio, 0);
#else
			if (cd->support_control_cs_off)
				pinctrl_select_state(tdev->thp_core->pctrl,
						     tdev->thp_core->pins_idle);
			else
				pinctrl_select_state(
					tdev->thp_core->pctrl,
					tdev->thp_core->mtk_pinctrl.cs_low);
#endif
		}
	}
	thp_log_info(tdev->thp_core, "%s: called end\n", __func__);
	return 0;
}

static void touch_driver_exit(struct thp_device *tdev)
{
	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	if (tdev) {
		touch_driver_unprepare(tdev);
		kfree(tdev->tx_buff);
		tdev->tx_buff = NULL;
		kfree(tdev->rx_buff);
		tdev->rx_buff = NULL;
		kfree(tdev);
		tdev = NULL;
	}
	if (tx_buf) {
		kfree(tx_buf);
		tx_buf = NULL;
	}
	if (spi_xfer) {
		kfree(spi_xfer);
		spi_xfer = NULL;
	}
}

static int touch_driver_get_project_id(struct thp_device *tdev, char *buf,
				       unsigned int len)
{
	if (!buf) {
		thp_log_err(tdev->thp_core, "%s: buff null\n", __func__);
		return -EINVAL;
	}

	if (tdev->thp_core->self_control_power && (!get_project_id_flag)) {
		strncpy(tdev->thp_core->project_id,
			tdev->thp_core->project_id_dummy, THP_PROJECT_ID_LEN);
		thp_log_info(tdev->thp_core, "%s:use dummy project id:%s\n",
			     __func__, tdev->thp_core->project_id);
	}
	strncpy(buf, tdev->thp_core->project_id, len);

	return 0;
}

static int touch_driver_event_read_and_dispatch(struct thp_device *tdev,
						unsigned char *buf,
						size_t buf_size,
						size_t read_size)
{
	struct thp_core_data *cd = tdev->thp_core;
	struct syna_tcm_identification *syna_tcm =
		(struct syna_tcm_identification *)tdev->private_data;
	struct syna_tcm_cmd_sync *cmd_sync = &syna_tcm->cmd_sync;
	struct syna_tcm_message message_header = {0};
	int ret = NO_ERR;
	int dispatch = 0;
	int read_length = 0;
	int resp_buf_len = 0;

	if (buf_size < MESSAGE_HEADER_SIZE || read_size > buf_size)
		return -EINVAL;
	if (read_size < MESSAGE_HEADER_SIZE && read_size != 0)
		return -EINVAL;

	ret = thp_bus_lock(cd);
	if (ret < 0) {
		thp_log_err(cd, "%s:get lock failed\n", __func__);
		return -EIO;
	}

	/* first read message header */
	if (read_size != 0) {
		ret = touch_driver_spi_read(cd->sdev, buf, read_size);
		if (ret < 0) {
			thp_log_err(cd, "%s: failed to read, ret: %d\n",
				    __func__, ret);
			ret = -EIO;
			goto out;
		}
		resp_buf_len = read_size;
		memcpy(&message_header, buf, MESSAGE_HEADER_SIZE);
	} else {
		ret = touch_driver_spi_read(cd->sdev, (char *)buf,
					    MESSAGE_HEADER_SIZE);
		if (ret < 0) {
			thp_log_err(cd, "%s: failed to read header, ret: %d\n",
				    __func__, ret);
			ret = -EIO;
			goto out;
		}
		memcpy(&message_header, buf, MESSAGE_HEADER_SIZE);
		thp_log_info(cd, "%s: irq_header: %*ph\n\n", __func__,
			     MESSAGE_HEADER_SIZE,
			     (unsigned char *)(&message_header));
	}

	/* second check message header */
	if (message_header.header == MESSAGE_MARKER) {
		if (message_header.code == STATUS_CONTINUED_READ) {
			/* this event is not complete, drop this event */
			touch_driver_spi_read(tdev->thp_core->sdev,
					      tdev->rx_buff,
					      THP_MAX_FRAME_SIZE);
			ret = -ENODATA;
			goto out;
		}
		if (message_header.length >=
		    THP_MAX_FRAME_SIZE - MESSAGE_MARKER) {
			thp_log_err(cd, "%s: header over length\n", __func__);
			touch_driver_spi_read(tdev->thp_core->sdev,
					      tdev->rx_buff,
					      THP_MAX_FRAME_SIZE);
			ret = -ENODATA;
			goto out;
		}
	} else {
		thp_log_err(cd, "%s: invalid message header: %02x\n", __func__,
			    message_header.header);
		ret = -EFAULT;
	}

	/* third, read message body */
	if (ret >= 0 && read_size == 0) {
		if (message_header.length) {
			if (message_header.length + MESSAGE_HEADER_SIZE >
			    buf_size)
				read_length = buf_size - FIRST_FRAME_USEFUL_LEN;
			else
				read_length = message_header.length +
					      FIRST_FRAME_USEFUL_LEN;
			ret = touch_driver_spi_read(
				cd->sdev, buf + FIRST_FRAME_USEFUL_LEN,
				read_length);
			if (ret < 0) {
				thp_log_err(
					cd,
					"%s: failed to read message body: %d\n",
					__func__, ret);
				ret = -EIO;
				goto out;
			}
			/* replace message header */
			memcpy(buf, &message_header, MESSAGE_HEADER_SIZE);
		}
		resp_buf_len = MESSAGE_HEADER_SIZE + read_length -
			       FIRST_FRAME_USEFUL_LEN;
	}

	if (ret < 0) {
		memset(buf, 0, buf_size);
		memset(&message_header, 0, sizeof(message_header));
		resp_buf_len = MESSAGE_HEADER_SIZE;
	}

	/* forth, check how to dispatch this event */
	if (cmd_sync->local_resp_waitting) {
		if (message_header.code <= REPORT_IDENTIFY) {
			/* there is a cmd is waiting for resp */
			dispatch = SYNA_DISPATCH_RESPONCE;
			memcpy(cmd_sync->resp.buf, buf, resp_buf_len);
			complete_all(&cmd_sync->local_cmd_completion);
			cmd_sync->local_resp_waitting = false;
		} else {
			dispatch = SYNA_DISPATCH_REPORT;
		}
	} else {
		dispatch = SYNA_DISPATCH_REPORT;
	}
	if (ret >= 0)
		ret = dispatch;

out:
	thp_bus_unlock(cd);
	return ret;
}

static int touch_driver_wait_resp(struct syna_tcm_cmd_sync *cmd_sync,
				  int timeout)
{
	int ret;

	cmd_sync->local_resp_waitting = true;
	ret = wait_for_completion_timeout(&cmd_sync->local_cmd_completion,
					  timeout);
	cmd_sync->local_resp_waitting = false;
	return ret;
}

int touch_driver_send_one_cmd_sync(struct thp_device *tdev, unsigned char *cmd,
				   int len)
{
	struct thp_core_data *cd = tdev->thp_core;
	struct syna_tcm_identification *syna_tcm =
		(struct syna_tcm_identification *)tdev->private_data;
	struct syna_tcm_cmd_sync *cmd_sync = &syna_tcm->cmd_sync;
	int ret = NO_ERR;
	bool wait_resp_and_resend = false;
	int timeout = msecs_to_jiffies(SYNA_MAX_CMD_TIMEOUT);

	if (!cmd || (len <= 0)) {
		thp_log_err(cd, "%s: have null pointer\n", __func__);
		return -EINVAL;
	}

	thp_spi_cmd_lock(cd, SPI_CMD_LOCK_TIMEOUT);
resend:
	ret = thp_bus_lock(cd);
	if (ret < 0) {
		thp_log_err(cd, "%s:get lock failed\n", __func__);
		ret = -EIO;
	}

	thp_log_info(cd, "%s: set_cmd: %*ph\n", __func__, len, cmd);
	memset(spi_write_buf, 0, SPI_READ_WRITE_SIZE);
	memcpy(spi_write_buf, cmd, len);

	/* send cmd */
	reinit_completion(&cmd_sync->local_cmd_completion);
	ret = touch_driver_spi_write(cd->sdev, spi_write_buf, len);
	if (ret < 0) {
		thp_log_err(cd, "%s: failed to send command, ret: %d\n",
			    __func__, ret);
		thp_bus_unlock(cd);
		ret = -EIO;
		goto out;
	}
	cmd_sync->local_resp_waitting = true;
	thp_bus_unlock(cd);

wait_resp:
	/* wait for resp */
	ret = touch_driver_wait_resp(cmd_sync, timeout);
	if (ret <= 0) {
		thp_log_err(cd, "%s: wait for resp timeout\n", __func__);
		ret = -ETIMEDOUT;
		goto out;
	}
	timeout = ret;

	/* get resp */
	if (cmd_sync->resp.message.header != MESSAGE_MARKER) {
		thp_log_err(cd, "%s: ic resp err, reset\n", __func__);
		ret = -EFAULT;
		goto out;
	}

	switch (cmd_sync->resp.message.code) {
	case STATUS_OK:
		thp_log_info(cd, "%s: get flag ok\n", __func__);
		if (wait_resp_and_resend) {
			wait_resp_and_resend = false;
			goto resend;
		}
		ret = NO_ERR;
		break;
	case STATUS_PREVIOUS_COMMAND_PENDING:
		thp_log_info(cd, "%s: ic busy now\n", __func__);
		wait_resp_and_resend = true;
		reinit_completion(&cmd_sync->local_cmd_completion);
		goto wait_resp;
		break;
	case REPORT_IDENTIFY:
		thp_log_info(cd, "%s: ic reboot, resend\n", __func__);
		goto resend;
		break;
	case STATUS_ERROR:
		thp_log_err(cd, "%s: ic err, reset\n", __func__);
		ret = -EFAULT;
		break;
	default:
		thp_log_info(cd, "%s: unknow resp, code: %02x\n", __func__,
			     cmd_sync->resp.message.code);
		ret = NO_ERR;
		break;
	}

out:
	thp_spi_cmd_unlock(cd);
	if (ret == -EFAULT) {
		gpio_set_value(cd->gpios.rst_gpio, GPIO_LOW);
		mdelay(2);
		gpio_set_value(cd->gpios.rst_gpio, GPIO_HIGH);
	}
	return ret;
}

static int touch_driver_send_one_cmd(struct thp_device *tdev,
				     unsigned char *cmd, int len)
{
	struct thp_core_data *cd = tdev->thp_core;
	struct thp_cmd_node cmd_node = {0};
	struct syna_thp_cmd *syna_thp_cmd = NULL;

	if (in_irq_thread(cd)) {
		syna_thp_cmd = kzalloc(sizeof(struct syna_thp_cmd), GFP_KERNEL);
		thp_log_info(tdev->thp_core, "%s: in_irq_thread\n", __func__);
		if (!syna_thp_cmd) {
			thp_log_err(tdev->thp_core,
				    "%s: syna_thp_cmd malloc fail\n", __func__);
			return -ENOMEM;
		}
		syna_thp_cmd->command = SYNA_ASYNC_IC_CMD;
		memcpy(syna_thp_cmd->params.async_ic_cmd.cmd, cmd, len);
		syna_thp_cmd->params.async_ic_cmd.cmd_length = len;
		cmd_node.command = THP_TOUCH_DRIVER_CMD;
		cmd_node.cmd_param.prv_params = syna_thp_cmd;
		thp_put_one_cmd(cd, &cmd_node, NO_SYNC_TIMEOUT);
		return NO_ERR;
	} else {
		return touch_driver_send_one_cmd_sync(tdev, cmd, len);
	}
}

static void get_debug_info_and_reset_proc(struct thp_device *tdev)
{
	unsigned char get_debug_cmd[SYNA_CMD_GET_DEBUG_LEN] = {0xC7, 0x01, 0x00,
							       0x06};
	struct syna_tcm_identification *syna_tcm =
		(struct syna_tcm_identification *)tdev->private_data;
	struct thp_core_data *cd = tdev->thp_core;
	unsigned char *resp = syna_tcm->cmd_sync.resp.buf;
	int resp_len =
		syna_tcm->cmd_sync.resp.message.length + MESSAGE_HEADER_SIZE;
	int ret = NO_ERR;

	ret = touch_driver_send_one_cmd(tdev, get_debug_cmd,
					SYNA_CMD_GET_DEBUG_LEN);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s: get debug info fail\n",
			    __func__);
		return;
	}

	thp_print_buf(tdev->thp_core, resp, resp_len);
	gpio_set_value(cd->gpios.rst_gpio, GPIO_LOW);
	mdelay(2);
	gpio_set_value(cd->gpios.rst_gpio, GPIO_HIGH);
}

static void get_debug_info_and_reset(struct thp_device *tdev)
{
	struct thp_cmd_node cmd_node = {0};
	struct syna_thp_cmd *syna_thp_cmd = NULL;
	struct thp_core_data *cd = tdev->thp_core;

	syna_thp_cmd = kzalloc(sizeof(struct syna_thp_cmd), GFP_KERNEL);
	thp_log_info(tdev->thp_core, "%s: in_irq_thread\n", __func__);
	if (!syna_thp_cmd) {
		thp_log_err(tdev->thp_core, "%s: syna_thp_cmd malloc fail\n",
			    __func__);
		return;
	}
	syna_thp_cmd->command = SYNA_GET_DEBUG_INFO_AND_RESET;
	cmd_node.command = THP_TOUCH_DRIVER_CMD;
	cmd_node.cmd_param.prv_params = syna_thp_cmd;

	thp_put_one_cmd(cd, &cmd_node, NO_SYNC_TIMEOUT);
	return;
}

static void syna_async_ic_cmd_proc(struct thp_device *tdev,
				   struct syna_thp_cmd *syna_thp_cmd)
{
	unsigned char *cmd;
	int cmd_length;
	int ret;

	if (tdev == NULL || syna_thp_cmd == NULL) {
		thp_log_err(tdev->thp_core, "%s: input is null\n", __func__);
		return;
	}
	cmd = syna_thp_cmd->params.async_ic_cmd.cmd;
	cmd_length = syna_thp_cmd->params.async_ic_cmd.cmd_length;

	ret = touch_driver_send_one_cmd(tdev, cmd, cmd_length);
	thp_log_info(tdev->thp_core, "%s: send cmd ret: %d \n", __func__, ret);

	return;
}

void touch_driver_thp_cmd_proc(struct thp_device *tdev,
			       struct thp_cmd_node *in_cmd,
			       struct thp_cmd_node *out_cmd)
{
	struct syna_thp_cmd *syna_thp_cmd = NULL;

	if (tdev == NULL || in_cmd == NULL) {
		thp_log_err(tdev->thp_core, "%s: input is null\n", __func__);
		return;
	}

	if (in_cmd->command != THP_TOUCH_DRIVER_CMD) {
		thp_log_err(tdev->thp_core, "%s: invalid thp_cmd_node\n",
			    __func__);
		return;
	}

	syna_thp_cmd = (struct syna_thp_cmd *)in_cmd->cmd_param.prv_params;
	if (syna_thp_cmd == NULL) {
		thp_log_err(tdev->thp_core, "%s: async cmd is null\n",
			    __func__);
		return;
	}

	switch (syna_thp_cmd->command) {
	case SYNA_ASYNC_IC_CMD:
		syna_async_ic_cmd_proc(tdev, syna_thp_cmd);
		break;
	case SYNA_GET_DEBUG_INFO_AND_RESET:
		get_debug_info_and_reset_proc(tdev);
		break;
	default:
		break;
	}
	kfree(syna_thp_cmd);
	syna_thp_cmd = NULL;

	return;
}

#ifdef CONFIG_HONOR_SHB_THP
static int touch_driver_switch_int_sh(struct thp_device *tdev)
{
	int ret;
	struct spi_device *sdev = NULL;
	/* report irq to sensorhub cmd */
	unsigned char report_to_shb_cmd[SYNA_CMD_LEN] = {0xC7, 0x02, 0x00, 0x2E,
							 0x01};

	if ((tdev->thp_core == NULL) || (tdev->thp_core->sdev == NULL)) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}
	sdev = tdev->thp_core->sdev;

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	ret = thp_bus_lock(tdev->thp_core);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s: get lock failed\n", __func__);
		return -EINVAL;
	}
	memset(spi_write_buf, 0, SPI_READ_WRITE_SIZE);
	memcpy(spi_write_buf, report_to_shb_cmd, SYNA_CMD_LEN);
	ret = touch_driver_spi_write(sdev, spi_write_buf, SYNA_CMD_LEN);
	if (ret < 0)
		thp_log_err(tdev->thp_core, "%s: send cmd failed\n", __func__);
	thp_bus_unlock(tdev->thp_core);
	return 0;
}

static int touch_driver_switch_int_ap(struct thp_device *tdev)
{
	int ret;
	struct spi_device *sdev = NULL;
	/* report irq to ap cmd */
	unsigned char report_to_ap_cmd[SYNA_CMD_LEN] = {0xC7, 0x02, 0x00, 0x2E,
							0x00};

	if ((tdev->thp_core == NULL) || (tdev->thp_core->sdev == NULL)) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}
	sdev = tdev->thp_core->sdev;

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	ret = thp_bus_lock(tdev->thp_core);
	if (ret < 0) {
		thp_log_err(tdev->thp_core, "%s: get lock failed\n", __func__);
		return -EINVAL;
	}
	memset(spi_write_buf, 0, SPI_READ_WRITE_SIZE);
	memcpy(spi_write_buf, report_to_ap_cmd, SYNA_CMD_LEN);
	ret = touch_driver_spi_write(sdev, spi_write_buf, SYNA_CMD_LEN);
	if (ret < 0)
		thp_log_err(tdev->thp_core, "%s: send cmd failed\n", __func__);
	thp_bus_unlock(tdev->thp_core);
	return 0;
}
#endif

#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
static int touch_driver_set_lowpower_state(struct thp_device *tdev, u8 state)
{
	unsigned char syna_sleep_cmd[SYNA_COMMAMD_LEN] = {0x2C, 0x00, 0x00};
	struct thp_core_data *cd = tdev->thp_core;
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	struct timeval lowpower_receive;
	long delta_time;
#endif

	thp_log_info(tdev->thp_core, "%s: called state = %u\n", __func__,
		     state);

	if (cd->work_status != SUSPEND_DONE) {
		thp_log_info(tdev->thp_core, "%s: resumed, not handle lp\n",
			     __func__);
		return NO_ERR;
	}
	if (ud_mode_status.lowpower_mode == state) {
		thp_log_info(tdev->thp_core, "%s:don't repeat old status %u\n",
			     __func__, state);
		return 0;
	}
	if (state)
		syna_sleep_cmd[0] = 0x2C; /* enable lowpower */
	else
		syna_sleep_cmd[0] = 0x2D; /* disable lowpower */
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	get_timestamp(&lowpower_receive);
	delta_time = ((lowpower_receive.tv_sec - lowpower_switch_time.tv_sec) *
		      1000000) +
		     lowpower_receive.tv_usec - lowpower_switch_time.tv_usec;
	delta_time /= 1000;
	if (delta_time < 10) {
		thp_log_info(tdev->thp_core,
			     "%s: lowpower switch too fast [%ld ms]\n",
			     __func__, delta_time);
		get_timestamp(&lowpower_switch_time);
		mdelay(10);
	}
#endif
	touch_driver_send_one_cmd(tdev, syna_sleep_cmd, sizeof(syna_sleep_cmd));
	ud_mode_status.lowpower_mode = state;
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	get_timestamp(&lowpower_switch_time);
#endif
	return 0;
}

static int touch_driver_standby_exit_lowpower(struct thp_device *tdev)
{
	unsigned char syna_sleep_cmd[SYNA_COMMAMD_LEN] = {0x2D, 0x00, 0x00};
	struct syna_tcm_identification *private_data =
		(struct syna_tcm_identification *)tdev->private_data;

	if (private_data->standby_enable_in_lowpower) {
		touch_driver_send_one_cmd(tdev, syna_sleep_cmd,
					  sizeof(syna_sleep_cmd));
		private_data->standby_enable_in_lowpower = false;
	}
	return 0;
}

static int touch_send_slide_cmd(struct thp_device *tdev, u8 state)
{
	int ret;
	// set slide event cmd enable : 0x5C disable : 0x5D
	unsigned char enable_slide_cmd[] = {0xC7, 0x01, 0x00, 0x5C};
	unsigned char disable_slide_cmd[] = {0xC7, 0x01, 0x00, 0x5D};
	int cmd_len = sizeof(enable_slide_cmd);

	ret = touch_driver_send_one_cmd(
		tdev, (state ? enable_slide_cmd : disable_slide_cmd), cmd_len);
	if (ret) {
		thp_log_err(tdev->thp_core, "%s: send %s_slide_cmd fail\n",
			    __func__, state ? "enable" : "disable");
		return -EIO;
	}
	return ret;
}
static void touch_driver_fullaod_gesture_support(struct thp_device *tdev,
						 u8 state)
{
	int ret;
	int standby_mode_tmp = tdev->thp_core->standby_enable;
	int double_gesture_mode = tdev->thp_core->easy_wakeup_info.sleep_mode;

	if (state) {
		if (tdev->thp_core->fullaod_support_gesture) {
			ret = touch_send_slide_cmd(tdev, state);
			if (!double_gesture_mode)
				tdev->thp_core->easy_wakeup_info.sleep_mode =
					TS_GESTURE_MODE;
			ret = touch_driver_set_ap_state(tdev);
			tdev->thp_core->easy_wakeup_info.sleep_mode =
				double_gesture_mode;
		} else {
			tdev->thp_core->aod_touch_status = 0;
			tdev->thp_core->standby_enable = 0;
			/*
			 fullaod_tp_need_work: syna wait response in suspend thread
			 when response irq come need need_work_in_suspend_switch is true
			 */
			tdev->thp_core->fullaod_tp_need_work = true;
			ret = touch_driver_set_ap_state(tdev);
			/* restore all status */
			tdev->thp_core->aod_touch_status = 1;
			tdev->thp_core->fullaod_tp_need_work = false;
			tdev->thp_core->standby_enable = standby_mode_tmp;
		}
	} else {
		ret = touch_send_slide_cmd(tdev, state);
		ret = touch_driver_set_ap_state(tdev);
	}
	if (ret)
		thp_log_err(tdev->thp_core, "failed to set cmd\n");
	return;
}

static int touch_driver_set_aod_state(struct thp_device *tdev, u8 state,
				      struct thp_aod_window window)
{
	return 0;
}

enum touch_event_type {
	TOUCH_EVENT_TYPE_NORMAL,
	TOUCH_EVENT_TYPE_TUI,
};

static void parse_tui_touch_event_info(struct thp_device *tdev,
				       const char *read_buff,
				       struct thp_udfp_data *udfp_data, int len)
{
	struct syna_tcm_identification *private_data =
		(struct syna_tcm_identification *)tdev->private_data;
	unsigned int tmp_event;
	unsigned int tracking_id_valid;

	if ((!read_buff) || (!udfp_data) || (len <= 0)) {
		thp_log_err(tdev->thp_core, "%s: invalid data\n", __func__);
		return;
	}

	/* syna format: byte[16~17] = x  byte[18~19] = y */
	udfp_data->tpud_data.tp_x = read_buff[16] + (read_buff[17] << 8);
	udfp_data->tpud_data.tp_y = read_buff[18] + (read_buff[19] << 8);

	/* syna format: byte[20~23] = udfp_event
	 * standby mult fingers format
	 * byte[24~25] = touch number
	 * byte[26 + 2*n~27 + 2*n] = 1/0 tracking id valid/invalid
	 * byte[46 + 2*n~47 + 2*n] = x
	 * byte[66 + 2*n~67 + 2*n] = y
	 */
	if (tdev->thp_core->standby_mode) {
		if (len < STANDBY_DATA_LEN_MAX) {
			thp_log_err(tdev->thp_core,
				    "%s: standby mode len not right\n",
				    __func__);
			return;
		}
		for (int i = 0; i < MAX_FINGER_NUMS; i++) {
			tracking_id_valid = read_buff[26 + 2 * i] +
					    (read_buff[26 + 2 * i + 1] << 8);
			if (tracking_id_valid) {
				udfp_data->coor_data[i].x =
					read_buff[46 + 2 * i] +
					(read_buff[46 + 2 * i + 1] << 8);
				udfp_data->coor_data[i].y =
					read_buff[66 + 2 * i] +
					(read_buff[66 + 2 * i + 1] << 8);
				udfp_data->coor_data[i].valid = 1;
				udfp_data->coor_data[i].tracking_id = i;
			}
		}
	}
	tmp_event = read_buff[20] + (read_buff[21] << 8) +
		    (read_buff[22] << 16) + (read_buff[23] << 24);
	if (tmp_event == TUI_FINGER_DOWN &&
	    private_data->tui_finger_last_status == TUI_FINGER_UP) {
		udfp_data->tpud_data.udfp_event = TP_EVENT_FINGER_DOWN;
		private_data->tui_finger_last_status = tmp_event;
	} else if (tmp_event == TUI_FINGER_UP &&
		   private_data->tui_finger_last_status == TUI_FINGER_DOWN) {
		udfp_data->tpud_data.udfp_event = TP_EVENT_FINGER_UP;
		private_data->tui_finger_last_status = tmp_event;
	}

	thp_log_info(tdev->thp_core,
		     "%s: x %d, y %d, touch event type %d, converted type %d\n",
		     __func__, udfp_data->tpud_data.tp_x,
		     udfp_data->tpud_data.tp_y, tmp_event,
		     udfp_data->tpud_data.udfp_event);
}

static void parse_touch_event_info(struct thp_device *tdev,
				   const char *read_buff,
				   struct thp_udfp_data *udfp_data, int len)
{
	unsigned int tmp_event;
	unsigned int finger_status;
	struct thp_core_data *cd = NULL;

	if ((!read_buff) || (!udfp_data) || (len <= 0)) {
		thp_log_err(tdev->thp_core, "%s: invalid data\n", __func__);
		return;
	}
	cd = tdev->thp_core;
	finger_status = !!(thp_get_status(tdev->thp_core, THP_STATUS_UDFP));
	/* syna format: byte[16:17] = x_coor byte[18:19] = y_coor */
	udfp_data->tpud_data.tp_x = read_buff[16] + (read_buff[17] << 8);
	udfp_data->tpud_data.tp_y = read_buff[18] + (read_buff[19] << 8);
	/* syna format: byte[20~23] = udfp_event */
	tmp_event = read_buff[20] + (read_buff[21] << 8) +
		    (read_buff[22] << 16) + (read_buff[23] << 24);
	thp_log_info(tdev->thp_core, "touch event = %u\n", tmp_event);
	udfp_data->tpud_data.udfp_event = TP_FP_EVENT_MAX;
	if (tmp_event == FULLAOD_SLIDE_EVENT)
		udfp_data->slide_event = VALID_SLIDE_EVNET;
	if (finger_status) {
		if (tmp_event == FP_CORE_AREA_FINGER_DOWN)
			udfp_data->tpud_data.udfp_event = TP_EVENT_FINGER_DOWN;
		if (tmp_event == FP_CORE_AREA_FINGER_UP)
			udfp_data->tpud_data.udfp_event = TP_EVENT_FINGER_UP;
	}
	if (cd->aod_touch_status || cd->standby_enable) {
		if ((tmp_event == FP_VALID_AREA_FINGER_DOWN) ||
		    (tmp_event == FP_VALID_AREA_FINGER_UP))
			udfp_data->aod_event = AOD_VALID_EVENT;
	}
}
static void
prase_touch_information_normalize_debug_info(struct thp_device *tdev,
					     const char *read_buff)
{
	u8 j;
	struct debug_info_struct *debug_info_struct =
		(struct debug_info_struct *)read_buff;

	thp_log_info(tdev->thp_core, "tp_info: finger_num: %d\n",
		     debug_info_struct->finger_num);
	thp_log_info(
		tdev->thp_core,
		"tp_info: abs_max: %d, abs_min: %d, trans_max: %d, trans_min: %d\n",
		debug_info_struct->abs_max, debug_info_struct->abs_min,
		debug_info_struct->trans_max, debug_info_struct->trans_min);
	thp_log_info(
		tdev->thp_core,
		"tp_info: max_raw: %d, min_raw: %d, finger_threshold: %d, hope_freq_count: %d\n",
		debug_info_struct->max_raw, debug_info_struct->min_raw,
		debug_info_struct->finger_threshold,
		debug_info_struct->hope_freq_count);
	thp_log_info(
		tdev->thp_core,
		"tp_info: baseline_update_count: %d, baseline_reset_count: %d\n",
		debug_info_struct->baseline_update_count,
		debug_info_struct->baseline_reset_count);
	thp_log_info(
		tdev->thp_core,
		"tp_info: baseline_change_reason: %d, current_freq: %d, noise: %d\n",
		debug_info_struct->baseline_change_reason,
		debug_info_struct->current_freq, debug_info_struct->noise);
	for (j = 0; j < 5; j++) {
		thp_log_info(
			tdev->thp_core,
			"tp_info: type: %d, click_reason: %d, x: %d, y: %d, count: %d\n",
			debug_info_struct->click[j].type,
			debug_info_struct->click[j].click_reason,
			debug_info_struct->click[j].x,
			debug_info_struct->click[j].y,
			debug_info_struct->click[j].count);
	}
	thp_log_info(tdev->thp_core, "tp_info: fw_status: %d\n",
		     debug_info_struct->fw_status);
	for (j = 0; j < 3; j++) {
		thp_log_info(tdev->thp_core,
			     "tp_info: fingerprint_signal: %d, %d, %d\n",
			     debug_info_struct->fingerprint_signal[3 * j + 0],
			     debug_info_struct->fingerprint_signal[3 * j + 1],
			     debug_info_struct->fingerprint_signal[3 * j + 2]);
	}
	thp_log_info(
		tdev->thp_core,
		"tp_info: fingerprint_area: %d, fingerprintdown_sensornumber: %d, int_count: %d\n",
		debug_info_struct->fingerprint_area,
		debug_info_struct->fingerprintdown_sensornumber,
		debug_info_struct->int_count);
	for (j = 0; j < 6; j++) {
		thp_log_info(tdev->thp_core,
			     "tp_info: reserve: %d, %d, %d, %d\n",
			     debug_info_struct->reserve[4 * j + 0],
			     debug_info_struct->reserve[4 * j + 1],
			     debug_info_struct->reserve[4 * j + 2],
			     debug_info_struct->reserve[4 * j + 3]);
	}
}
static void prase_touch_debug_info(struct thp_device *tdev,
				   const char *read_buff)
{
	int i;

	/*
	 * Print screen off data, output 100 buf log.
	 * Print 5 lines of data, 20 data per line.
	 */
	for (i = 0; i < 100 / 20; i++) {
		thp_log_info(tdev->thp_core, "tp_debug_info[%d~%d] %*ph\n",
			     20 * i, 20 * (i + 1) - 1, 20, read_buff + 20 * i);
	}
}
static int parse_event_info(struct thp_device *tdev, const char *read_buff,
			    struct thp_udfp_data *udfp_data, int len,
			    enum touch_event_type type)
{
	unsigned int udfp_event;
	unsigned char report_to_ap_cmd[SYNA_CMD_GESTURE_LEN] = {
		0xC7, 0x05, 0x00, 0x33, 0x00, 0x00, 0x00, 0x00};

	if ((!read_buff) || (!udfp_data) || (len <= 0)) {
		thp_log_err(tdev->thp_core, "%s: invalid data\n", __func__);
		return -EINVAL;
	}
	/*
	*"len" is length of debug info，len==152 supports standardized information printing
        * len==100, keep the original printing.
	*/
	if (len == 152) {
		prase_touch_information_normalize_debug_info(tdev,
							     read_buff + 4);
		return -ENODATA;
	} else if (len == 100) {
		prase_touch_debug_info(tdev, read_buff);
		return -ENODATA;
	}
	udfp_data->tpud_data.udfp_event = TP_FP_EVENT_MAX;
	/* read_buff[12~15] = 0xff touch event */
	if ((read_buff[12] == TOUCH_EVENT_TYPE) &&
	    (read_buff[13] == TOUCH_EVENT_TYPE) &&
	    (read_buff[14] == TOUCH_EVENT_TYPE) &&
	    (read_buff[15] == TOUCH_EVENT_TYPE)) {
		if (type == TOUCH_EVENT_TYPE_TUI)
			parse_tui_touch_event_info(tdev, read_buff, udfp_data,
						   len);
		else
			parse_touch_event_info(tdev, read_buff, udfp_data, len);
	} else if (read_buff[12] == STYLUS_EVENT_FLAG) {
		/* read_buff[12] = 0x20 stylus event */
		udfp_data->key_event = TS_STYLUS_WAKEUP_TO_MEMO;
	} else {
		/* read_buff[12~15] = gesture event */
		udfp_event = read_buff[12] + (read_buff[13] << 8) +
			     (read_buff[14] << 16) + (read_buff[15] << 24);
		thp_log_info(tdev->thp_core, "gesture event = %d\n",
			     udfp_event);
		if (udfp_event == DOUBLE_TAP_FLAG) {
			udfp_data->key_event = TS_DOUBLE_CLICK;
			/* cmd[4] event */
			report_to_ap_cmd[TOUCH_GESTURE_CMD] |=
				THP_GESTURE_DOUBLE_CLICK;
		}
		touch_driver_send_one_cmd(tdev, report_to_ap_cmd,
					  SYNA_CMD_GESTURE_LEN);
	}
	return 0;
}

static int firmware_reboot_event_process(struct thp_device *tdev,
					 enum touch_event_type type)
{
	if (type == TOUCH_EVENT_TYPE_TUI)
		return touch_driver_set_tui_one_cmd(tdev);
	else
		return touch_driver_set_ap_one_cmd(tdev);
}

static int __touch_driver_get_event_info(struct thp_device *tdev,
					 struct thp_udfp_data *udfp_data,
					 enum touch_event_type type)
{
	unsigned char *data = spi_read_buf;
	struct thp_core_data *cd = tdev->thp_core;
	int retval;
	struct syna_tcm_message *message = NULL;
	static int set_ap_cmd_retry = 0;
	static int read_data_retry = 0;

	thp_log_info(tdev->thp_core, "%s enter %d\n", __func__,
		     cd->support_esd_event);

	if ((!tdev->thp_core) || (!tdev->thp_core->sdev) || (!data)) {
		thp_log_err(tdev->thp_core, "%s: have null pointer\n",
			    __func__);
		return -EINVAL;
	}

	/* read and dispatch event */
	retval = touch_driver_event_read_and_dispatch(tdev, tdev->rx_buff,
						      THP_MAX_FRAME_SIZE, 0);
	message = (struct syna_tcm_message *)tdev->rx_buff;

	/* if this is not a report event, return */
	if (retval != SYNA_DISPATCH_REPORT && message->code != REPORT_IDENTIFY)
		return -ENODATA;

	if (message->code == REPORT_IDENTIFY) {
		/* ic reboot */
		if (cd->support_esd_event == 1) {
			thp_log_info(tdev->thp_core, "%s: firmware reboot\n",
				     __func__);
			retval = firmware_reboot_event_process(tdev, type);
			udfp_data->tpud_data.udfp_event = TP_EVENT_FINGER_UP;
			return -ENODATA;
		}
	} else {
		/* report message process*/
		if (message->length > SYNA_CMD_GESTURE_MAX) {
			/* this could be a screen on report */
			goto err_invalid_length;

		} else {
			read_data_retry = 0;
			set_ap_cmd_retry = 0;
			thp_log_info(tdev->thp_core, "%s: parse_event_info\n",
				     __func__);
			retval = parse_event_info(tdev, tdev->rx_buff,
						  udfp_data, message->length,
						  type);
		}
	}
	return retval;

err_invalid_length:
	read_data_retry++;
	if (read_data_retry >= READ_RETRY_TIMES) {
		read_data_retry = 0;
		set_ap_cmd_retry++;
		if (set_ap_cmd_retry >= SET_AP_CMD_RETRY_TIMES) {
			set_ap_cmd_retry = 0;
			thp_log_err(tdev->thp_core,
				    "%s: set ap cmd too many times, reset\n",
				    __func__);
			get_debug_info_and_reset(tdev);
		} else {
			/* resend suspend cmd */
			retval = touch_driver_set_ap_one_cmd(tdev);
		}
	}
	return -ENODATA;
}

static int touch_driver_get_event_info(struct thp_device *tdev,
				       struct thp_udfp_data *udfp_data)
{
	return __touch_driver_get_event_info(tdev, udfp_data,
					     TOUCH_EVENT_TYPE_NORMAL);
}

static int touch_driver_get_tui_event_info(struct thp_device *tdev,
					   struct thp_udfp_data *udfp_data)
{
	return __touch_driver_get_event_info(tdev, udfp_data,
					     TOUCH_EVENT_TYPE_TUI);
}

static int touch_driver_prepare(struct thp_device *tdev)
{
	if (spi_read_buf == NULL) {
		spi_read_buf = kzalloc(SPI_READ_WRITE_SIZE, GFP_KERNEL);
		if (!spi_read_buf) {
			thp_log_err(tdev->thp_core, "%s:spi_read_buf fail\n",
				    __func__);
			return -ENOMEM;
		}
	}
	if (spi_write_buf == NULL) {
		spi_write_buf = kzalloc(SPI_READ_WRITE_SIZE, GFP_KERNEL);
		if (!spi_write_buf) {
			thp_log_err(tdev->thp_core, "%s:spi_write_buf fail\n",
				    __func__);
			kfree(spi_read_buf);
			return -ENOMEM;
		}
	}
	return NO_ERR;
}

static void touch_driver_unprepare(struct thp_device *tdev)
{
	if (spi_read_buf != NULL)
		kfree(spi_read_buf);
	if (spi_write_buf != NULL)
		kfree(spi_write_buf);
	spi_read_buf = NULL;
	spi_write_buf = NULL;
}

static int touch_driver_suspend_connect_pen(struct thp_device *tdev)
{
	struct syna_tcm_identification *private_data =
		(struct syna_tcm_identification *)tdev->private_data;
	unsigned char pen_enable_cmd[PEN_CMD_LEN] = {0xC7, 0x01, 0x00, 0x1A};
	unsigned char syna_sleep_cmd[SYNA_COMMAMD_LEN] = {0x2D, 0x00, 0x00};
	unsigned char report_to_ap_cmd[SYNA_ONE_CMD_LEN] = {0xC7, 0x03, 0x00,
							    0x4C, 0x02, 0x40};
	unsigned char cmd_enable[FRAME_CMD_LEN] = {0x05, 0x01, 0x00, 0xC0};

	if (private_data->stylus_gesture_status_lowpower) {
		touch_driver_send_one_cmd(tdev, syna_sleep_cmd,
					  sizeof(syna_sleep_cmd));
		mdelay(10);
		touch_driver_send_one_cmd(tdev, report_to_ap_cmd,
					  sizeof(report_to_ap_cmd));
		private_data->stylus_gesture_status_lowpower = false;
	}
	mdelay(2);
	touch_driver_send_one_cmd(tdev, cmd_enable, sizeof(cmd_enable));
	mdelay(2);
	touch_driver_send_one_cmd(tdev, pen_enable_cmd, sizeof(pen_enable_cmd));
	return 0;
}
#endif

static int touch_driver_suspend_disconnect_pen(struct thp_device *tdev)
{
	unsigned char pen_disable_cmd[PEN_CMD_LEN] = {0xC7, 0x01, 0x00, 0x1B};

	touch_driver_send_one_cmd(tdev, pen_disable_cmd,
				  sizeof(pen_disable_cmd));
	return 0;
}

struct thp_device_ops syna_dev_ops = {
	.init = touch_driver_init,
	.detect = touch_driver_chip_detect,
	.get_frame = touch_driver_get_frame,
	.resume = touch_driver_resume,
	.suspend = touch_driver_suspend,
	.get_project_id = touch_driver_get_project_id,
	.exit = touch_driver_exit,
	.chip_wrong_touch = touch_driver_wrong_touch,
	.chip_gesture_report = touch_driver_gesture_report,
	.tui_enable_switch = touch_driver_tui_enable_switch,
	.get_active_doze_time = touch_driver_get_active_doze_time,
#ifdef CONFIG_HONOR_SHB_THP
	.switch_int_sh = touch_driver_switch_int_sh,
	.switch_int_ap = touch_driver_switch_int_ap,
#endif
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
	.get_event_info = touch_driver_get_event_info,
	.tp_lowpower_ctrl = touch_driver_set_lowpower_state,
	.standby_mode_exit_lowpower = touch_driver_standby_exit_lowpower,
	.tp_aod_event_ctrl = touch_driver_set_aod_state,
	.get_tui_event_info = touch_driver_get_tui_event_info,
	.suspend_disconnect_pen = touch_driver_suspend_disconnect_pen,
	.tp_fullaod_gesture_support = touch_driver_fullaod_gesture_support,
#endif
	.thp_cmd_proc = touch_driver_thp_cmd_proc,
	.suspend_connect_pen = touch_driver_suspend_connect_pen,
	.prepare = touch_driver_prepare,
};

struct thp_device *syna_driver_module_alloc(struct thp_core_data *cd)
{
	struct thp_device *dev = NULL;
	struct syna_tcm_identification *device_info = NULL;
	int len;

	thp_log_debug(cd, "%s: called\n", __func__);

	dev = kzalloc(sizeof(struct thp_device), GFP_KERNEL);
	if (!dev) {
		thp_log_err(cd, "%s: dev out of memory\n", __func__);
		return NULL;
	}

	len = sizeof(struct syna_tcm_identification);
	device_info = kzalloc(len, GFP_KERNEL);
	if (!device_info) {
		thp_log_err(cd, "%s: device_info out of memory\n", __func__);
		goto err_device_info;
	}

	memset(device_info, 0, len);
	device_info->tui_finger_last_status = TUI_FINGER_UP;
	init_completion(&device_info->cmd_sync.local_cmd_completion);
	dev->private_data = device_info;

	dev->tx_buff = kzalloc(THP_MAX_FRAME_SIZE, GFP_KERNEL);
	if (!dev->tx_buff) {
		thp_log_err(cd, "%s: tx out of memory\n", __func__);
		goto err_tx_buff;
	}
	dev->rx_buff = kzalloc(THP_MAX_FRAME_SIZE, GFP_KERNEL);
	if (!dev->rx_buff) {
		thp_log_err(cd, "%s: rx out of memory\n", __func__);
		goto err_rx_buff;
	}

	dev->ic_name = SYNAPTICS_IC_NAME;
	dev->dev_node_name = THP_SYNA_DEV_NODE_NAME;
	dev->ops = &syna_dev_ops;

	return dev;

err_rx_buff:
	kfree(dev->tx_buff);
	dev->tx_buff = NULL;
err_tx_buff:
	kfree(device_info);
	device_info = NULL;
err_device_info:
	kfree(dev);
	dev = NULL;

	return NULL;
}

int syna_driver_module_init(struct thp_core_data *cd)
{
	int rc;
	struct thp_device *dev;

	dev = syna_driver_module_alloc(cd);
	if (!dev)
		return -ENOMEM;

	if (cd && cd->fast_booting_solution) {
		thp_send_detect_cmd(cd, dev, NO_SYNC_TIMEOUT);
		/*
		 * The thp_register_dev will be called later to complete
		 * the real detect action.If it fails, the detect function will
		 * release the resources requested here.
		 */
		return 0;
	}

	rc = thp_register_dev(cd, dev);
	if (rc) {
		thp_log_err(cd, "%s: register fail\n", __func__);
		goto err;
	}
#if IS_ENABLED(CONFIG_HONOR_THP_QCOM)
	get_timestamp(&lowpower_switch_time);
#endif
	thp_log_info(cd, "%s: register success\n", __func__);
	return rc;
err:
	if (dev) {
		kfree(dev->tx_buff);
		dev->tx_buff = NULL;

		kfree(dev->rx_buff);
		dev->rx_buff = NULL;

		kfree(dev->private_data);
		dev->private_data = NULL;

		kfree(dev);
		dev = NULL;
	}
	return rc;
}

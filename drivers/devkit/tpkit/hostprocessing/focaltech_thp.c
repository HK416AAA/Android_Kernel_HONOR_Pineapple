/*
 * Thp driver code for focaltech
 *
 * Copyright (c) 2012-2020 Honor Technologies Co., Ltd.
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
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include "honor_thp.h"
#include <linux/workqueue.h>

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define FOCALTECH_IC_NAME "focaltech"
#define THP_FOCALTECH_DEV_NODE_NAME "focaltech"

#define CMD_UNLOCK 0x55
#define CMD_READ_ID 0x90
#define CMD_READ_PID 0xC2
#define DELAY_AFTER_HWRST 14
#define CHIP_DETECT_FAILE_ONE 0x00
#define CHIP_DETECT_FAILE_TWO 0xFF
#define CHIP_DETECT_FAILE_THR 0xEF
#define CHIP_DETECT_RETRY_NUMB 3

#define SPI_READ_CMD 0xA0
#define SPI_WRITE_CMD 0x00
#define SPI_HEADER_BYTE 0x07
#define SPI_CRC_BYTE 0x02
#define SPI_RETRY_NUMBER 3
#define FTS_OFF_STS 3
#define CRC_REG_VALUE 0xFFFF
#define CRC_CHECK_BIT_SIZE 8
#define CRC_CHECK_CODE 0x8408
#define CRC_BUF_MIN_SIZE 2
#define CRC_LOW_BIT_CHECK 0x01
#define CRC_BIT_CHECK 1

#define ADDR_FRAME 0x3A
#define ADDR_SLEEP_IN 0x52
#define FTS_REG_GESTURE_EN 0xD0
#define FTS_REG_GESTURE_CFG 0xD1
#define FTS_REG_GESTURE_DATA 0xD3
#define FTS_GESTURE_DATA_LEN 30
#define GESTURE_STR_LEN_MAX 152
#define GESTURE_OFF_ID 0
#define GESTURE_ID_DOUBLE_CLICK 0x24
#define GESTURE_ID_STYLUS 0x25
#define GESTURE_DATA_ENABLE 1
#define GESTURE_INFO_BIT 3
#define GESTURE_INFO_VALUE 0x80
#define DELAY_RESET_LOW 2

#define FTS_NEED_WORK_IN_SUSPEND 1
#define FTS_NO_NEED_WORK_IN_SUSPEND 0

unsigned int fts_need_power_off = 0;

/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/
enum fts_ic_type {
	FT3D81_SERIES = 4,
};

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
/* spi interface */
static int touch_driver_spi_transfer(const char *tx_buf, char *rx_buf,
				     unsigned int len)
{
	int rc = 0;
	struct thp_core_data *cd = thp_get_core_data();
	struct spi_device *sdev = cd->sdev;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.tx_buf = tx_buf,
		.rx_buf = rx_buf,
		.len = len,
	};

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	rc = thp_bus_lock(cd);
	if (rc < 0) {
		thp_log_err(cd, "%s:get lock failed:%d\n", __func__, rc);
		return rc;
	}
	rc = thp_spi_sync(sdev, &msg);
	thp_bus_unlock(cd);

	return rc;
}

static int rdata_crc_check(u8 *rdata, u32 rlen)
{
	u32 i = 0;
	u32 j = 0;
	u16 crc_read = 0;
	u16 crc_calc = CRC_REG_VALUE;
	struct thp_core_data *cd = thp_get_core_data();

	if (rdata == NULL) {
		thp_log_err(cd, "%s: rdata is NULL\n", __func__);
		return -EIO;
	}

	if (rlen < CRC_BUF_MIN_SIZE) {
		thp_log_err(cd,
			    "%s: thp_check_frame_valid buf less than 2: %d\n",
			    __func__, rlen);
		return -EIO;
	}

	for (i = 0; i < (rlen - CRC_BUF_MIN_SIZE); i++) {
		crc_calc ^= rdata[i];
		for (j = 0; j < CRC_CHECK_BIT_SIZE; j++) {
			if (crc_calc & CRC_LOW_BIT_CHECK)
				crc_calc = (crc_calc >> CRC_BIT_CHECK) ^
					   CRC_CHECK_CODE;
			else
				crc_calc = (crc_calc >> CRC_BIT_CHECK);
		}
	}

	/* rlen - 1 is to get the last element of rdata array */
	crc_read = (u16)(rdata[rlen - 1] << CRC_CHECK_BIT_SIZE) +
		   rdata[rlen - CRC_BUF_MIN_SIZE];
	if (crc_calc != crc_read) {
		thp_log_err(cd, "%s: ecc fail:cal %x != buf %x\n", __func__,
			    crc_calc, crc_read);
		return -EIO;
	}
	return 0;
}

/* spi_v2 read/write protocol */
static int touch_driver_spi_write(u8 addr, u8 *data, u32 datalen)
{
	int ret = 0;
	int i = 0;
	u32 txlen = 0;
	u32 txlen_max = SPI_HEADER_BYTE + datalen;
	struct thp_core_data *cd = thp_get_core_data();
	struct thp_device *tdev = cd->thp_dev;
	struct spi_device *sdev = cd->sdev;
	struct spi_message msg;
	struct spi_transfer xfer;
	unsigned char *w_buf = NULL;
	unsigned char *r_buf = NULL;

	if (!tdev || !tdev->tx_buff || !tdev->rx_buff) {
		thp_log_err(cd, "%s: tdev/tx_buff/rx_buff null\n", __func__);
		return -ENOMEM;
	}

	if (datalen >= THP_MAX_FRAME_SIZE - SPI_HEADER_BYTE) {
		thp_log_err(cd, "%s: data_len: %u illegal\n", __func__,
			    datalen);
		return -ENOMEM;
	}

	ret = thp_bus_lock(tdev->thp_core);
	if (ret < 0) {
		thp_log_err(cd, "%s:get lock failed:%d\n", __func__, ret);
		return ret;
	}

	w_buf = tdev->tx_buff;
	r_buf = tdev->rx_buff;
	memset(tdev->tx_buff, 0xFF, txlen_max);
	memset(tdev->rx_buff, 0, txlen_max);

	w_buf[txlen++] = addr;
	w_buf[txlen++] = SPI_WRITE_CMD;
	w_buf[txlen++] = (datalen >> 8) & 0xFF;
	w_buf[txlen++] = datalen & 0xFF;
	if (data && (datalen > 0)) {
		memcpy(&w_buf[SPI_HEADER_BYTE], &data[0], datalen);
		txlen = SPI_HEADER_BYTE + datalen;
	}

	memset(&xfer, 0, sizeof(xfer));
	xfer.tx_buf = w_buf;
	xfer.rx_buf = r_buf;
	xfer.len = txlen;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	for (i = 0; i < SPI_RETRY_NUMBER; i++) {
		ret = thp_spi_sync(sdev, &msg);
		if ((0 == ret) && ((r_buf[FTS_OFF_STS] & 0xA0) == 0)) {
			break;
		} else {
			thp_log_info(
				cd,
				"data write(addr:%x[%x]),status:%x,retry:%d,ret:%d",
				addr, w_buf[0], r_buf[FTS_OFF_STS], i, ret);
		}
	}

	thp_bus_unlock(tdev->thp_core);
	return ret;
}

static int touch_driver_spi_read(u8 addr, u8 *data, u32 datalen)
{
	struct thp_core_data *cd = thp_get_core_data();
	struct thp_device *tdev = cd->thp_dev;
	struct spi_device *sdev = cd->sdev;
	struct spi_message msg;
	struct spi_transfer xfers;
	int ret = 0;
	int i = 0;
	unsigned char *w_buf = NULL;
	unsigned char *r_buf = NULL;
	u32 txlen = 0;
	u32 txlen_max = SPI_HEADER_BYTE + datalen + SPI_CRC_BYTE;

	if (!tdev || !tdev->tx_buff || !tdev->rx_buff || !data) {
		thp_log_info(cd, "%s: tdev/tx_buff/rx_buff/data null\n",
			     __func__);
		return -ENOMEM;
	}

	if ((!datalen) ||
	    (datalen >= THP_MAX_FRAME_SIZE - SPI_HEADER_BYTE - SPI_CRC_BYTE)) {
		thp_log_info(cd, "%s: data_len: %u illegal\n", __func__,
			     datalen);
		return -ENOMEM;
	}

	ret = thp_bus_lock(tdev->thp_core);
	if (ret < 0) {
		thp_log_err(cd, "%s:get lock failed:%d\n", __func__, ret);
		return ret;
	}

	w_buf = tdev->tx_buff;
	r_buf = tdev->rx_buff;
	memset(tdev->tx_buff, 0xFF, txlen_max);
	memset(tdev->rx_buff, 0, txlen_max);

	w_buf[txlen++] = addr;
	w_buf[txlen++] = SPI_READ_CMD;
	w_buf[txlen++] = (datalen >> 8) & 0xFF;
	w_buf[txlen++] = datalen & 0xFF;
	txlen = SPI_HEADER_BYTE + datalen + SPI_CRC_BYTE;

	memset(&xfers, 0, sizeof(xfers));
	xfers.tx_buf = w_buf;
	xfers.rx_buf = r_buf;
	xfers.len = txlen;
	spi_message_init(&msg);
	spi_message_add_tail(&xfers, &msg);

	for (i = 0; i < SPI_RETRY_NUMBER; i++) {
		ret = thp_spi_sync(sdev, &msg);
		if (0 == ret) {
			memcpy(data, &r_buf[SPI_HEADER_BYTE], datalen);
			/* crc check */
			ret = rdata_crc_check(r_buf + SPI_HEADER_BYTE,
					      txlen - SPI_HEADER_BYTE);
			if (ret < 0) {
				thp_log_info(
					cd,
					"data read(addr:%x) crc abnormal,retry:%d\n",
					addr, i);
				continue;
			}
			break;
		} else {
			thp_log_info(
				cd,
				"data read(addr:%x) status:%x,retry:%d,ret:%d\n",
				addr, r_buf[FTS_OFF_STS], i, ret);
			ret = -EIO;
		}
	}

	if (ret < 0) {
		thp_log_err(cd, "data read(addr:%x[%x]) %s,status:%x,ret:%d\n",
			    addr, w_buf[0],
			    (i >= SPI_RETRY_NUMBER) ? "crc abnormal" : "fail",
			    r_buf[FTS_OFF_STS], ret);
	}

	thp_bus_unlock(tdev->thp_core);
	return ret;
}

static int touch_driver_hardware_reset(struct thp_device *tdev)
{
#ifndef CONFIG_HONOR_THP_MTK
	gpio_direction_output(tdev->gpios->rst_gpio, THP_RESET_LOW);
	thp_do_time_delay(tdev->timing_config.boot_reset_low_delay_ms);
	gpio_set_value(tdev->gpios->rst_gpio, THP_RESET_HIGH);
	thp_do_time_delay(tdev->timing_config.boot_reset_hi_delay_ms);
#else
	if (tdev->thp_core->support_pinctrl == 0) {
		thp_log_info(tdev->thp_core, "%s: not support pinctrl\n",
			     __func__);
		return -EINVAL;
	}
	pinctrl_select_state(tdev->thp_core->pctrl,
			     tdev->thp_core->mtk_pinctrl.reset_low);
	thp_do_time_delay(tdev->timing_config.boot_reset_low_delay_ms);
	pinctrl_select_state(tdev->thp_core->pctrl,
			     tdev->thp_core->mtk_pinctrl.reset_high);
	thp_do_time_delay(tdev->timing_config.boot_reset_hi_delay_ms);
#endif
	return 0;
}

static bool touch_driver_valid_project_id(char *id, int size)
{
	int i = 0;
	for (i = 0; i < THP_PROJECT_ID_LEN; i++) {
		if ((!isalnum(id[i]))) {
			return false;
		}
	}
	return true;
}

static int thp_parse_extra_config(struct device_node *thp_node,
				  struct thp_core_data *cd)
{
	int rc;
	unsigned int value = 0;

	if (!thp_node || !cd) {
		thp_log_info(cd, "%s: input null\n", __func__);
		return -ENODEV;
	}
	rc = of_property_read_u32(thp_node, "reset_status_in_suspend_mode",
				  &value);
	if (!rc) {
		cd->reset_status_in_suspend_mode = value;
		thp_log_info(cd, "%s: reset_status_in_suspend_mode %u\n",
			     __func__, value);
	}
	rc = of_property_read_u32(thp_node, "need_set_reset_gpio_high", &value);
	if (!rc) {
		thp_log_info(cd, "%s: need_set_reset_gpio_high %u\n", __func__,
			     value);
	}
	return 0;
}

static int touch_driver_power_init(struct thp_core_data *cd)
{
	int ret;

	ret = thp_power_supply_get(cd, THP_VCC);
	if (ret)
		thp_log_err(cd, "%s: vcc fail\n", __func__);
	ret = thp_power_supply_get(cd, THP_IOVDD);
	if (ret)
		thp_log_err(cd, "%s: THP_IOVDD fail\n", __func__);

	return 0;
}

static void touch_driver_power_release(struct thp_core_data *cd)
{
	int ret;

	ret = thp_power_supply_put(cd, THP_VCC);
	if (ret)
		thp_log_err(cd, "%s: fail to release vcc power\n", __func__);

	ret = thp_power_supply_put(cd, THP_IOVDD);
	if (ret)
		thp_log_err(cd, "%s: fail to release IOVDD power\n", __func__);
}

static int touch_driver_power_on(struct thp_device *tdev)
{
	int ret;

	if (tdev == NULL) {
		thp_log_err(tdev->thp_core, "%s: tdev null\n", __func__);
		return -EINVAL;
	}
	thp_log_info(tdev->thp_core, "%s:called\n", __func__);
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_HIGH);

	ret = thp_power_supply_ctrl(tdev->thp_core, THP_VCC, THP_POWER_ON, 0);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power VCC fail\n", __func__);
	ret = thp_power_supply_ctrl(tdev->thp_core, THP_IOVDD, THP_POWER_ON, 8);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power IOVDD fail\n", __func__);
	thp_log_info(tdev->thp_core, "%s pull up tp ic reset\n", __func__);
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_HIGH);
	return ret;
}

static int touch_driver_power_off(struct thp_device *tdev)
{
	int ret;

	if (tdev == NULL) {
		thp_log_err(tdev->thp_core, "%s: tdev null\n", __func__);
		return -EINVAL;
	}
	thp_log_info(tdev->thp_core, "%s pull down tp ic reset\n", __func__);
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_LOW);
	thp_do_time_delay(tdev->timing_config.suspend_reset_after_delay_ms);

	ret = thp_power_supply_ctrl(tdev->thp_core, THP_IOVDD, THP_POWER_OFF,
				    0);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power IOVDD fail\n", __func__);
	ret = thp_power_supply_ctrl(tdev->thp_core, THP_VCC, THP_POWER_OFF, 0);
	if (ret)
		thp_log_err(tdev->thp_core, "%s:power VCC fail\n", __func__);
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);

	return ret;
}

static int touch_driver_init(struct thp_device *tdev)
{
	struct thp_core_data *cd = NULL;
	struct device_node *fts_node = NULL;

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	if (!tdev) {
		thp_log_info(tdev->thp_core, "%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	cd = tdev->thp_core;
	fts_node =
		of_get_child_by_name(cd->thp_node, THP_FOCALTECH_DEV_NODE_NAME);
	if (!fts_node) {
		thp_log_info(tdev->thp_core, "%s: dev not config in dts\n",
			     __func__);
		return -ENODEV;
	}

	if (thp_parse_spi_config(fts_node, cd))
		thp_log_err(tdev->thp_core, "%s: spi config parse fail\n",
			    __func__);

	if (thp_parse_timing_config(tdev->thp_core, fts_node,
				    &tdev->timing_config))
		thp_log_err(tdev->thp_core, "%s: timing config parse fail\n",
			    __func__);

	if (thp_parse_feature_config(fts_node, cd))
		thp_log_err(tdev->thp_core, "%s: feature_config fail\n",
			    __func__);
	if (thp_parse_extra_config(fts_node, cd))
		thp_log_err(tdev->thp_core, "%s: extra_config fail\n",
			    __func__);

	return 0;
}

static int touch_driver_chip_detect(struct thp_device *tdev)
{
	int rc = 0;
	int i = 0;
	unsigned char val[2] = {0};

	if (!tdev) {
		thp_log_info(tdev->thp_core, "%s: input dev null\n", __func__);
		return -ENOMEM;
	}

	if (tdev->thp_core->self_control_power) {
		touch_driver_power_init(tdev->thp_core);
		touch_driver_power_on(tdev);
	}

	for (i = 0; i < CHIP_DETECT_RETRY_NUMB; i++) {
		rc = touch_driver_hardware_reset(tdev);
		if (rc) {
			thp_log_err(tdev->thp_core, "%s:TP reset fail\n",
				    __func__);
		}

		mdelay(DELAY_AFTER_HWRST + i * 4);
		rc = touch_driver_spi_write(CMD_UNLOCK, NULL, 0);
		if (rc) {
			thp_log_err(tdev->thp_core,
				    "%s:write 0x55 command fail\n", __func__);
		}

		mdelay(DELAY_AFTER_HWRST + i * 4);
		rc = touch_driver_spi_read(CMD_READ_ID, val, 2);
		if (rc) {
			thp_log_err(tdev->thp_core,
				    "%s:write 0x90 command fail\n", __func__);
		} else {
			if ((val[0] == CHIP_DETECT_FAILE_ONE) ||
			    (val[0] == CHIP_DETECT_FAILE_TWO) ||
			    (val[0] == CHIP_DETECT_FAILE_THR)) {
				thp_log_err(
					tdev->thp_core,
					"%s:chip id read fail, ret=%d, i=%d, data=%x\n",
					__func__, rc, i, val[0]);
				rc = -EIO;
			} else {
				thp_log_info(
					tdev->thp_core,
					"%s:chip id read success, chip id:0x%X,0x%X\n",
					__func__, val[0], val[1]);
				return 0;
			}
		}
		msleep(50); /* Delay 50ms to retry if reading ic id failed */
	}

	thp_log_err(tdev->thp_core, "%s:chip detect fail, read=0x%x%x\n",
		    __func__, val[0], val[1]);

	if (tdev->thp_core->self_control_power) {
		touch_driver_power_off(tdev);
		touch_driver_power_release(tdev->thp_core);
	}

	if (tdev->thp_core->fast_booting_solution) {
		kfree(tdev->tx_buff);
		tdev->tx_buff = NULL;
		kfree(tdev->rx_buff);
		tdev->rx_buff = NULL;
		kfree(tdev);
		tdev = NULL;
	}

	return rc;
}

static int touch_driver_get_project_id(struct thp_device *tdev, char *buf,
				       unsigned int len)
{
	int i = 0;
	int rc = 0;
	unsigned char val[THP_PROJECT_ID_LEN + 1] = {0};

	if (!tdev || !buf) {
		thp_log_err(tdev->thp_core, "%s: thp_dev/buf null\n", __func__);
		return -ENOMEM;
	}

	if ((!len) || (len > THP_PROJECT_ID_LEN)) {
		thp_log_err(tdev->thp_core, "%s: len(%d) is invalid\n",
			    __func__, len);
		return -EINVAL;
	}

	for (i = 0; i < CHIP_DETECT_RETRY_NUMB; i++) {
		rc = touch_driver_hardware_reset(tdev);
		if (rc) {
			thp_log_err(tdev->thp_core, "%s:TP reset fail\n",
				    __func__);
		}

		mdelay(DELAY_AFTER_HWRST + i * 4);
		rc = touch_driver_spi_write(CMD_UNLOCK, NULL, 0);
		if (rc) {
			thp_log_err(tdev->thp_core,
				    "%s:write 0x55 command fail\n", __func__);
		}

		mdelay(DELAY_AFTER_HWRST + i * 4);
		rc = touch_driver_spi_read(CMD_READ_ID, val, 2);
		if (rc) {
			thp_log_err(tdev->thp_core,
				    "%s:write 0x90 command fail\n", __func__);
		} else {
			if ((val[0] == CHIP_DETECT_FAILE_ONE) ||
			    (val[0] == CHIP_DETECT_FAILE_TWO) ||
			    (val[0] == CHIP_DETECT_FAILE_THR)) {
				thp_log_err(tdev->thp_core,
					"%s:id read fail, ret=%d, i=%d, data=%x\n",
					__func__, rc, i, val[0]);
			} else {
				thp_log_info(tdev->thp_core,
					     "%s:read id:0x%X%X\n", __func__,
					     val[0], val[1]);
				rc = touch_driver_spi_read(CMD_READ_PID, val,
							   THP_PROJECT_ID_LEN);
				if (rc) {
					thp_log_err(
						tdev->thp_core,
						"%s:write 0xC2 command fail\n",
						__func__);
				} else if (touch_driver_valid_project_id(
						   val,
						   THP_PROJECT_ID_LEN + 1)) {
					strncpy(buf, val, len);
					thp_log_info(tdev->thp_core,
						     "%s: get project id: %s\n",
						     __func__, buf);
					break;
				} else {
					thp_log_err(tdev->thp_core,
						    "%s:check pid(%s) fail\n",
						    __func__, val);
				}
			}
		}
		msleep(50);
	}

	if (i >= CHIP_DETECT_RETRY_NUMB) {
		thp_log_err(tdev->thp_core, "%s:read project id fail\n",
			    __func__);
		rc = -EIO;
	}

	touch_driver_hardware_reset(tdev);

	return rc;
}

static int touch_driver_get_frame(struct thp_device *tdev, char *buf,
				  unsigned int len)
{
	int rc = 0;

	if (!tdev) {
		thp_log_info(tdev->thp_core, "%s: input dev null\n", __func__);
		return -ENOMEM;
	}

	if (!buf) {
		thp_log_info(tdev->thp_core, "%s: input buf null\n", __func__);
		return -ENOMEM;
	}

	if ((!len) || (len >= THP_MAX_FRAME_SIZE - 1)) {
		thp_log_info(tdev->thp_core, "%s: read len: %u illegal\n",
			     __func__, len);
		return -ENOMEM;
	}

	rc = touch_driver_spi_read(ADDR_FRAME, buf, len);
	if (rc) {
		thp_log_err(tdev->thp_core, "%s:write 0x3A command fail\n",
			    __func__);
		return rc;
	}

	return 0;
}

#ifdef CONFIG_HONOR_THP_MTK
static void touch_driver_pinctrl_resume(struct thp_device *tdev)
{
	if (tdev->thp_core->support_pinctrl == 0) {
		thp_log_info(tdev->thp_core, "%s: not support pinctrl\n",
			     __func__);
		return;
	}
	pinctrl_select_state(tdev->thp_core->pctrl,
			     tdev->thp_core->mtk_pinctrl.cs_high);
	if (tdev->thp_core->reset_status_in_suspend_mode)
		thp_log_info(tdev->thp_core, "%s: reset retains high\n",
			     __func__);
	else
		pinctrl_select_state(tdev->thp_core->pctrl,
				     tdev->thp_core->mtk_pinctrl.reset_high);
	thp_do_time_delay(tdev->timing_config.resume_reset_after_delay_ms);
}

static void touch_driver_pinctrl_suspend(struct thp_device *tdev)
{
	if (tdev->thp_core->support_pinctrl == 0) {
		thp_log_info(tdev->thp_core, "%s: not support pinctrl\n",
			     __func__);
		return;
	}
	if (tdev->thp_core->reset_status_in_suspend_mode)
		thp_log_info(tdev->thp_core, "%s: reset retains high\n",
			     __func__);
	else
		pinctrl_select_state(tdev->thp_core->pctrl,
				     tdev->thp_core->mtk_pinctrl.reset_low);
	pinctrl_select_state(tdev->thp_core->pctrl,
			     tdev->thp_core->mtk_pinctrl.cs_low);
	thp_do_time_delay(tdev->timing_config.suspend_reset_after_delay_ms);
}
#endif

static void touch_driver_power_on_tddi(struct thp_device *tdev)
{
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_HIGH);
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_HIGH);
	thp_do_time_delay(tdev->timing_config.resume_reset_after_delay_ms);
}

static void touch_driver_power_off_tddi(struct thp_device *tdev)
{
	gpio_set_value(tdev->gpios->rst_gpio, GPIO_LOW);
	gpio_set_value(tdev->gpios->cs_gpio, GPIO_LOW);
	thp_do_time_delay(tdev->timing_config.suspend_reset_after_delay_ms);
}

static int touch_driver_resume(struct thp_device *tdev)
{
	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	if ((!tdev) || (!tdev->thp_core)) {
		thp_log_info(tdev->thp_core, "%s: input dev null\n", __func__);
		return -ENOMEM;
	}

#ifndef CONFIG_HONOR_THP_MTK
	if (fts_need_power_off) {
		thp_log_info(tdev->thp_core, "%s: gesture mode in\n", __func__);
		gpio_set_value(tdev->gpios->cs_gpio, GPIO_HIGH);
		gpio_set_value(tdev->gpios->rst_gpio, GPIO_LOW);
		msleep(DELAY_RESET_LOW);
		gpio_set_value(tdev->gpios->rst_gpio, GPIO_HIGH);
	} else {
		if (tdev->thp_core->self_control_power)
			touch_driver_power_on(tdev);
		else
			touch_driver_power_on_tddi(tdev);
	}
#else
	touch_driver_pinctrl_resume(tdev);
#endif
	return 0;
}

static int touch_driver_wrong_touch(struct thp_device *tdev)
{
	if ((!tdev) || (!tdev->thp_core)) {
		thp_log_err(tdev->thp_core, "%s: input dev is null\n",
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

static void touch_driver_enter_gesture_mode(struct thp_device *tdev)
{
	int rc = 0;
	u8 val = GESTURE_DATA_ENABLE;

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	rc = touch_driver_spi_write(FTS_REG_GESTURE_EN, &val, 1);
	if (rc) {
		thp_log_err(tdev->thp_core, "%s: write 0xD0 to 1 failed",
			    __func__);
	}
	mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
	tdev->thp_core->easy_wakeup_info.off_motion_on = true;
	mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
}

static void gesture_process_oled(struct thp_device *tdev,
				 unsigned int gesture_status,
				 unsigned int finger_status)
{
	u8 reg_value = 0;
	struct thp_core_data *cd = thp_get_core_data();
	int rc = 0;
	unsigned int stylus3_status;

	if (gesture_status) {
		thp_log_info(cd, "enable double click");
		reg_value |= 1 << 1; /* bit1 set to 1 */
	}
	if (finger_status) {
		thp_log_info(cd, "enable finger print");
		reg_value |= 1 << 4; /* bit4 set to 1 */
	}
	if (cd->aod_touch_status) {
		thp_log_info(cd, "enable single click");
		reg_value |= 1 << 0; /* bit0 set to 1 */
	}
	stylus3_status = atomic_read(&tdev->thp_core->last_stylus3_status);
	if (cd->stylus_gesture_status && (stylus3_status > 0)) {
		thp_log_info(cd, "enable stylus click");
		reg_value |= 1 << 5; /* bit5 set to 1 */
	}
	thp_log_info(cd, "write to reg 0xd1 %x\n", reg_value);
	touch_driver_spi_write(FTS_REG_GESTURE_CFG, &reg_value, 1);
	if (rc) {
		thp_log_err(cd, "%s: write 0xD1 to %x failed\n", __func__,
			    reg_value);
	}
	touch_driver_enter_gesture_mode(tdev);
}

static void gesture_process_tddi(struct thp_device *tdev,
				 unsigned int gesture_status)
{
	if (gesture_status == TS_GESTURE_MODE &&
	    tdev->thp_core->lcd_gesture_mode_support) {
		thp_log_info(tdev->thp_core, "%s: TS_GESTURE_MODE\n", __func__);
		touch_driver_enter_gesture_mode(tdev);
	}
}

static void parse_gesture_info(u8 *gesture_buffer, int size)
{
	int cnt = 0;
	char str[GESTURE_STR_LEN_MAX];
	int str_len_max = GESTURE_STR_LEN_MAX;
	int i;
	struct thp_core_data *cd = thp_get_core_data();

	if ((gesture_buffer[GESTURE_INFO_BIT] & GESTURE_INFO_VALUE) ==
	    GESTURE_INFO_VALUE) {
		for (i = 0; i < FTS_GESTURE_DATA_LEN; i++)
			cnt += snprintf(str + cnt, str_len_max - cnt, "%02X ",
					gesture_buffer[i]);
		thp_log_info(cd, "%s: %s\n", __func__, str);
	}
}

static int touch_driver_report_gesture(struct thp_device *tdev,
				       unsigned int *gesture_wakeup_value)
{
	int rc = 0;
	u8 gesture_data[FTS_GESTURE_DATA_LEN] = {0};
	u8 gesture_enable = 0;
	u8 gesture_id = 0;

	if ((!tdev) || (!tdev->thp_core) || (!tdev->thp_core->sdev)) {
		thp_log_info(tdev->thp_core, "%s: input dev null\n", __func__);
		return -ENOMEM;
	}
	if ((!gesture_wakeup_value) ||
	    (!tdev->thp_core->support_gesture_mode)) {
		thp_log_err(tdev->thp_core, "%s, gesture not support\n",
			    __func__);
		return -EINVAL;
	}

	rc = touch_driver_spi_read(FTS_REG_GESTURE_EN, &gesture_enable, 1);
	if (rc) {
		thp_log_err(tdev->thp_core, "%s: write 0xD0 failed", __func__);
		return rc;
	}

	if (gesture_enable != GESTURE_DATA_ENABLE) {
		thp_log_err(tdev->thp_core, "gesture is disabled");
		return -EIO;
	}

	rc = touch_driver_spi_read(FTS_REG_GESTURE_DATA, gesture_data,
				   FTS_GESTURE_DATA_LEN);
	if (rc) {
		thp_log_err(tdev->thp_core, "%s: write 0xD3 failed", __func__);
		return rc;
	}

	parse_gesture_info(gesture_data, FTS_GESTURE_DATA_LEN);
	gesture_id = gesture_data[GESTURE_OFF_ID];
	thp_log_info(tdev->thp_core, "%s: gesture ID:%d\n", __func__,
		     gesture_id);
	if (gesture_id == GESTURE_ID_DOUBLE_CLICK) {
		thp_log_info(tdev->thp_core, "%s: find double click gesture\n",
			     __func__);
		mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
		if (tdev->thp_core->easy_wakeup_info.off_motion_on == true) {
			tdev->thp_core->easy_wakeup_info.off_motion_on = false;
			*gesture_wakeup_value = TS_DOUBLE_CLICK;
		}
		mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
	} else if (gesture_id == GESTURE_ID_STYLUS) {
		thp_log_info(tdev->thp_core, "%s: find stylus gesture\n",
			     __func__);
		mutex_lock(&tdev->thp_core->thp_wrong_touch_lock);
		if (tdev->thp_core->easy_wakeup_info.off_motion_on == true) {
			tdev->thp_core->easy_wakeup_info.off_motion_on = false;
			*gesture_wakeup_value = TS_STYLUS_WAKEUP_TO_MEMO;
		}
		mutex_unlock(&tdev->thp_core->thp_wrong_touch_lock);
	}

	return 0;
}

static int touch_driver_bt_handler(struct thp_device *tdev, bool delay_enable)
{
	struct thp_core_data *cd = thp_get_core_data();
	struct thp_easy_wakeup_info *info = NULL;
	unsigned int stylus3_status;

	if (!cd->support_gesture_mode || !cd->support_pen_wakeup_gesture) {
		thp_log_err(tdev->thp_core, "%s: gesture not support\n",
			    __func__);
		return -EINVAL;
	}

	stylus3_status = atomic_read(&tdev->thp_core->last_stylus3_status);
	info = &cd->easy_wakeup_info;

	if ((stylus3_status > 0) && (cd->stylus_gesture_status == 1)) {
		cd->sleep_mode = TS_GESTURE_MODE;
	} else {
		if (info->easy_wakeup_gesture)
			cd->sleep_mode = TS_GESTURE_MODE;
		else
			cd->sleep_mode = TS_POWER_OFF_MODE;
	}

	thp_log_info(tdev->thp_core,
		     "%s: status:%u, stylus_gesture_status:%u, sleep_mode:%u\n",
		     __func__, stylus3_status, cd->stylus_gesture_status,
		     cd->sleep_mode);

	return 0;
}

static int touch_driver_suspend(struct thp_device *tdev)
{
	unsigned int gesture_status;
	unsigned int stylus3_status;
	struct thp_core_data *cd = thp_get_core_data();
	unsigned int finger_status = !!(thp_get_status(cd, THP_STATUS_UDFP));

	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	if ((!tdev) || (!tdev->thp_core)) {
		thp_log_info(tdev->thp_core, "%s: tdev null\n", __func__);
		return -ENOMEM;
	}

	gesture_status = tdev->thp_core->easy_wakeup_info.easy_wakeup_gesture;
	stylus3_status = atomic_read(&tdev->thp_core->last_stylus3_status);
	if (gesture_status || finger_status || cd->aod_touch_status ||
	    (cd->stylus_gesture_status && (stylus3_status > 0))) {
		fts_need_power_off = FTS_NEED_WORK_IN_SUSPEND;
		if (tdev->thp_core->self_control_power)
			gesture_process_oled(tdev, gesture_status,
					     finger_status);
		else
			gesture_process_tddi(tdev, gesture_status);

		thp_set_irq_wake_status(cd, THP_IRQ_WAKE_ENABLE);
		return 0;
	}

	if (is_pt_test_mode(tdev)) {
		thp_log_info(tdev->thp_core, "%s: PT sleep mode\n", __func__);
		if (touch_driver_spi_write(ADDR_SLEEP_IN, NULL, 0)) {
			thp_log_info(tdev->thp_core,
				     "%s: write 0x52 command failed\n",
				     __func__);
		}

	} else {
#ifndef CONFIG_HONOR_THP_MTK
		fts_need_power_off = FTS_NO_NEED_WORK_IN_SUSPEND;
		if (tdev->thp_core->self_control_power)
			touch_driver_power_off(tdev);
		else
			touch_driver_power_off_tddi(tdev);
#else
		touch_driver_pinctrl_suspend(tdev);
#endif
	}

	thp_log_info(tdev->thp_core, "%s: called end\n", __func__);
	return 0;
}

static void touch_driver_exit(struct thp_device *tdev)
{
	thp_log_info(tdev->thp_core, "%s: called\n", __func__);
	if (tdev) {
		kfree(tdev->tx_buff);
		tdev->tx_buff = NULL;
		kfree(tdev->rx_buff);
		tdev->rx_buff = NULL;
		kfree(tdev);
		tdev = NULL;
	}
}
static int touch_driver_second_poweroff(void)
{
	struct thp_core_data *cd = thp_get_core_data();
	int rc = 0;

	rc = touch_driver_spi_write(0x62, NULL, 0);
	if (rc) {
		thp_log_err(cd, "%s: write 0x62 command failed\n", __func__);
	}

	return rc;
}

struct thp_device_ops fts_dev_ops = {
	.init = touch_driver_init,
	.detect = touch_driver_chip_detect,
	.get_project_id = touch_driver_get_project_id,
	.get_frame = touch_driver_get_frame,
	.resume = touch_driver_resume,
	.suspend = touch_driver_suspend,
	.exit = touch_driver_exit,
	.spi_transfer = touch_driver_spi_transfer,
	.chip_wrong_touch = touch_driver_wrong_touch,
	.chip_gesture_report = touch_driver_report_gesture,
	.bt_handler = touch_driver_bt_handler,
	.second_poweroff = touch_driver_second_poweroff,
};

int focal_driver_module_init(struct thp_core_data *cd)
{
	int rc = 0;
	struct thp_device *dev = NULL;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		thp_log_err(cd, "%s: thp device malloc fail\n", __func__);
		return -ENOMEM;
	}

	dev->tx_buff = kzalloc(THP_MAX_FRAME_SIZE, GFP_KERNEL);
	dev->rx_buff = kzalloc(THP_MAX_FRAME_SIZE, GFP_KERNEL);
	if (!dev->tx_buff || !dev->rx_buff) {
		thp_log_err(cd, "%s: out of memory\n", __func__);
		rc = -ENOMEM;
		goto err;
	}

	dev->ic_name = FOCALTECH_IC_NAME;
	dev->ops = &fts_dev_ops;
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

	return rc;
err:
	kfree(dev->tx_buff);
	dev->tx_buff = NULL;
	kfree(dev->rx_buff);
	dev->rx_buff = NULL;
	kfree(dev);
	dev = NULL;
	return rc;
}

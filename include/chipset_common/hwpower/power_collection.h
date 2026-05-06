/*
 * power_collection.h
 *
 * collection module
 *
 * Copyright (c) 2023-2023 Honor Device Co., Ltd.
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

#ifndef _POWER_COLLECTION_H_
#define _POWER_COLLECTION_H_

#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <securec.h>
#include <log/hw_log.h>

#define PWR_CLT_MAX_CHAN_NUM 10
#define PWR_CLT_MAX_FILTER_NUM 5
#define PWR_CLT_MAX_EVENT_NUM 5
#define PWR_CLT_MAX_ITEM_NUM 20
#define PWR_CLT_MAX_LINES_NUM 20
#define PWR_CLT_MAX_BUFF_LEN 4096

#define PWR_CLT_MAX_CALCU_NUM 3600
#define PWR_CLT_MAX_SAMPLE_CNT 36000

#define PWR_COLLECT_READ_FAIL (-1)
#define PWR_COLLECT_READ_FILTER (-2)

enum pwr_collect_system_type {
	PWR_COLLECT_SYSTEM_TYPE_BEGIN = 0,
	PWR_COLLECT_SYSTEM_TYPE_AP,
	PWR_COLLECT_SYSTEM_TYPE_ADSP,
	PWR_COLLECT_SYSTEM_TYPE_END,
};

enum pwr_collect_type {
	PWR_COLLECT_TYPE_BEGIN = 0,
	PWR_COLLECT_TYPE_REAL_TIME,
	PWR_COLLECT_TYPE_NON_REAL_TIME,
	PWR_COLLECT_TYPE_END,
};

enum pwr_collect_direction {
	PWR_COLLECT_DIR_BEGIN = 0,
	PWR_COLLECT_DIR_HEAD,
	PWR_COLLECT_DIR_TAIL,
	PWR_COLLECT_DIR_END,
};

enum pwr_collect_notify_event {
	PWR_COLLECT_NOTIFY_EVENT_BEGIN = 0,
	PWR_COLLECT_NOTIFY_EVENT_READ_DATA,
	PWR_COLLECT_NOTIFY_EVENT_COLLECT_STOP,
	PWR_COLLECT_NOTIFY_EVENT_COLLECT_DELETE,
	PWR_COLLECT_NOTIFY_EVENT_END,
};

enum pwr_collect_item_type {
	PWR_COLLECT_ITEM_TYPE_BEGIN,
	PWR_COLLECT_ITEM_TYPE_VBAT,
	PWR_COLLECT_ITEM_TYPE_IBAT,
	PWR_COLLECT_ITEM_TYPE_TBAT,
	PWR_COLLECT_ITEM_TYPE_SOC,
	PWR_COLLECT_ITEM_TYPE_ADP_TYPE,
	PWR_COLLECT_ITEM_TYPE_CHG_ST,
	PWR_COLLECT_ITEM_TYPE_SCN_ST,
	PWR_COLLECT_ITEM_TYPE_TBAT_AUX,
	PWR_COLLECT_ITEM_TYPE_SOC_AUX,
	PWR_COLLECT_ITEM_TYPE_CHG_ST_AUX,
	PWR_COLLECT_ITEM_TYPE_COL_TIME,
	PWR_COLLECT_ITEM_TYPE_END,
};

enum pwr_collect_data_type {
	PWR_COLLECT_DATA_TYPE_BEGIN,
	PWR_COLLECT_DATA_TYPE_UNSIGNED_CHAR,
	PWR_COLLECT_DATA_TYPE_CHAR,
	PWR_COLLECT_DATA_TYPE_UNSIGNED_SHORT,
	PWR_COLLECT_DATA_TYPE_SHORT,
	PWR_COLLECT_DATA_TYPE_UNSIGNED_INT,
	PWR_COLLECT_DATA_TYPE_END,
};

struct pwr_collect_item {
	unsigned char item_type;
	unsigned char data_type;
	unsigned char data_offset;
};

enum pwr_collect_calculator_type {
	PWR_COLLECT_CALCU_TYPE_BEGIN,
	PWR_COLLECT_CALCU_TYPE_AVG,
	PWR_COLLECT_CALCU_TYPE_ABS_AVG,
	PWR_COLLECT_CALCU_TYPE_MAX,
	PWR_COLLECT_CALCU_TYPE_MIN,
	PWR_COLLECT_CALCU_TYPE_END,
};

enum pwr_collect_logical_type {
	PWR_COLLECT_LOGICAL_TYPE_BEGIN,
	PWR_COLLECT_LOGICAL_TYPE_EQ, // ==
	PWR_COLLECT_LOGICAL_TYPE_NE, // !=
	PWR_COLLECT_LOGICAL_TYPE_GT, // >
	PWR_COLLECT_LOGICAL_TYPE_GE, // >=
	PWR_COLLECT_LOGICAL_TYPE_LT, // <
	PWR_COLLECT_LOGICAL_TYPE_LE, // <=
	PWR_COLLECT_LOGICAL_TYPE_END,
};

/*
 * Ibat.Avg.60.<.10       --->  Ibat Avg in 60 seconds < 10mA
 * Ibat.Avg.1200.<.100    --->  Ibat Avg in 1200 seconds < 100mA
 */
struct power_collect_filter {
	unsigned char item_index;
	unsigned char calcu_type;
	unsigned char logi_type;
	int var_left;
	int var_right;
};

enum pwr_collect_event_type {
	PWR_COLLECT_EVENT_TYPE_BEGIN = 0,
	PWR_COLLECT_EVENT_TYPE_PLUGIN,
	PWR_COLLECT_EVENT_TYPE_PLUGOUT,
	PWR_COLLECT_EVENT_TYPE_CHG_DONE, // buck charge done
	PWR_COLLECT_EVENT_TYPE_CHG_DC, // in direct charging
	PWR_COLLECT_EVENT_TYPE_CHG_BUCK, // in buck charging
	PWR_COLLECT_EVENT_TYPE_CHG,
	PWR_COLLECT_EVENT_TYPE_CHGDIS,
	PWR_COLLECT_EVENT_TYPE_SCNON,
	PWR_COLLECT_EVENT_TYPE_SCNOFF,
	PWR_COLLECT_EVENT_TYPE_DC_CHG_DONE,
	PWR_COLLECT_EVENT_TYPE_POWER_ON,
	PWR_COLLECT_EVENT_TYPE_POWER_OFF,
	PWR_COLLECT_EVENT_TYPE_END,
};

enum pwr_collect_action_type {
	PWR_COLLECT_ACTION_TYPE_BEGIN = 0,
	PWR_COLLECT_ACTION_TYPE_EN_CHG,
	PWR_COLLECT_ACTION_TYPE_DIS_CHG,
	PWR_COLLECT_ACTION_TYPE_END,
};

/*
 * ChgDone.ChgDis     --->  Disable charge when charge done
 */
struct power_collect_event_action {
	unsigned char evt_type;
	unsigned char act_type;
};

struct power_collect_data {
	unsigned short timestamp;
	unsigned short vbat;
	short ibat;
	short tbat;
	unsigned short soc;
	unsigned short adp_type;
	unsigned short chg_st;
	unsigned short scn_st;
	short tbat_aux;
	unsigned short soc_aux;
	unsigned short chg_st_aux;
	unsigned int col_time;
};

struct power_collect_orig_config {
	unsigned char interval;
	unsigned char type;
	const char *name;
	const char *item_list;
	const char *filter_list;
	const char *start_event;
	const char *stop_event;
	const char *event_action_list;
};

struct power_collect_channel_config {
	unsigned char channel_seq;
	unsigned char interval;
	unsigned char type;
	unsigned char direction;
	unsigned char data_size;
	struct pwr_collect_item collect_items[PWR_CLT_MAX_ITEM_NUM];
	struct power_collect_filter filter[PWR_CLT_MAX_FILTER_NUM];
	unsigned char start_event;
	unsigned char stop_event;
	struct power_collect_event_action evt_act[PWR_CLT_MAX_EVENT_NUM];
};

struct power_collect_channel {
	const char *name;
	int channel_seq;
	struct power_collect_channel_config chan_cfg;
	char *chan_data_read;
	char *chan_data_write;
	char chan_data_1[PWR_CLT_MAX_BUFF_LEN];
	char chan_data_2[PWR_CLT_MAX_BUFF_LEN];
	int start_flag;
	int stop_flag;
	int sample_cnt;
	int read_index;
	int write_index;
	int rd_num;
	struct blocking_notifier_head event_nh;
};

struct power_channel_ops {
	int (*create)(int channel_seq);
	int (*delete)(int channel_seq);
	int (*take_action)(int channel_seq,
			   enum pwr_collect_action_type act_type);
};

struct power_channel_info {
	enum pwr_collect_system_type ctype;
	struct power_collect_channel *chan[PWR_CLT_MAX_CHAN_NUM];
	struct power_channel_ops ops;
	struct completion power_collection_delete_completion;
};

struct power_collection_device {
	struct device *dev;
	struct power_channel_info *chan_info;
	struct mutex chan_lock;
	int con_data[PWR_CLT_MAX_CALCU_NUM];
	int channel_seq;
};

enum power_log_sysfs_type {
	POWER_COLLECTION_SYSFS_BEGIN = 0,
	POWER_COLLECTION_SYSFS_CHANNEL_SEQ = POWER_COLLECTION_SYSFS_BEGIN,
	POWER_COLLECTION_SYSFS_CHANNEL,
	POWER_COLLECTION_SYSFS_CREATE_CHANNEL,
	POWER_COLLECTION_SYSFS_DELETE_CHANNEL,
	POWER_COLLECTION_SYSFS_END,
};

int power_collection_create_channel(struct power_collect_orig_config *chan_cfg);
int power_collection_delete_channel(int channel_seq);
int power_collection_register_channel_nb(int channel_seq,
					 struct notifier_block *nb);
int power_collection_read_channel(int channel_seq, char *data, int max_size,
				  int continue_flag, int direction);
int power_collection_channel_take_action(int channel_seq,
					 enum pwr_collect_action_type act_type);
int power_collection_register(struct power_channel_info *info);
void power_collection_chan_lock(void);
void power_collection_chan_unlock(void);
#endif /* _POWER_COLLECTION_H_ */

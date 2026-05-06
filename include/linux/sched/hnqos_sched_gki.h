/*
 * hnqos_sched_gki.h
 *
 * Qos schedule GKI implementation
 *
 * Copyright (c) 2019-2023 Honor Device Co., Ltd.
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

#ifndef HNQOS_SCHED_GKI_H
#define HNQOS_SCHED_GKI_H

#include <linux/sched.h>

enum DYNAMIC_QOS_TYPE {
	DYNAMIC_QOS_BINDER = 0,
	DYNAMIC_QOS_RWSEM,
	DYNAMIC_QOS_MUTEX,
	DYNAMIC_QOS_FUTEX,
	DYNAMIC_QOS_TYPE_MAX,
};

enum DYNAMIC_QOS_VALUE {
	VALUE_QOS_INVALID = -1,
	VALUE_QOS_LOW = 0,
	VALUE_QOS_NORMAL,
	VALUE_QOS_HIGH,
	VALUE_QOS_CRITICAL,
	VALUE_QOS_MAX,
};

#endif

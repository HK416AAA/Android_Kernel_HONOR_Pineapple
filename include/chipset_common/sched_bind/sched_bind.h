/*
 * Copyright (c) Honor Technologies Co., Ltd. 2022-2023. All rights reserved.
 * Description: sched bind header
 */

#include <linux/slab.h>
#include <linux/version.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>

#include "securec.h"

void bind_db_update(u32 pthread_msg);
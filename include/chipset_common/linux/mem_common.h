/*
 * mem_common.h	declare functions shared by different modules
 *
 * Copyright(C) 2023 Honor Device Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _MEM_COMMON_H
#define _MEM_COMMON_H
#include <linux/psi.h>

static inline bool task_is_mem_vip(struct task_struct *tsk)
{
	int adj;
	int prio;

	rcu_read_lock();
	adj = tsk->signal->oom_score_adj; /* top-app */
	prio = tsk->prio - MAX_RT_PRIO; /* nice: -20; RT_PRIO */
	rcu_read_unlock();

	return adj <= 0 && prio <= 0;
}

#endif //end of define _MEM_COMMON_H

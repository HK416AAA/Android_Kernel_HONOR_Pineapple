/*
 * Copyright (c) Honor Device Co., Ltd. 2019-2023. All rights reserved.
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
 * trans rtg thread for QOS GKI
 */

#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <trace/hooks/sched.h>
#include <trace/events/sched.h>
#include <linux/sched/hnqos_sched_gki.h>
#include "include/trans_rtg_gki.h"
#include "include/rtg.h"
#include "include/set_rtg.h"
#include "include/iaware_rtg.h"

enum OPRATION_TYPE {
	OPERATION_RTG_ENQUEUE,
	OPERATION_RTG_DEQUEUE,
};

static int g_trans_depth = DEFAULT_TRANS_DEPTH;
static int g_max_thread_num = DEFAULT_MAX_THREADS;
static atomic_t g_rtg_thread_num = ATOMIC_INIT(0);

static bool is_depth_valid(const struct task_struct *from, int qos)
{
	if (g_trans_depth != DEFAULT_TRANS_DEPTH && g_trans_depth <= 0)
		return false;

	if ((g_trans_depth > 0) && (from->rtg_depth != STATIC_RTG_DEPTH) &&
	    (from->rtg_depth >= g_trans_depth))
		return false;

	// if from is not in rtg, invalid
	if (from->rtg_depth == 0 &&
	    (from->prio != DEFAULT_RT_PRIO + 1 || qos != VALUE_QOS_CRITICAL))
		return false;

	return true;
}

unsigned int honor_get_group_id_gki(struct task_struct *p)
{
	unsigned int ret = 0;
	trace_android_rvh_sched_get_group_id(p, &ret);
	return ret;
}

void trans_rtg_sched_enqueue_for_gki(struct task_struct *task,
				     struct task_struct *from,
				     unsigned int type, int qos)
{
	int ret;
	unsigned int grpid;
	int prio;
#ifdef CONFIG_HN_SET_ASYNC_BINDER_RT
	int policy = SCHED_NORMAL;
	struct sched_param sp = {0};
#endif

	if (!task || !from || type != DYNAMIC_QOS_BINDER)
		return;

	if (!is_depth_valid(from, qos)) {
#ifdef CONFIG_RTG_DEBUG_LOG
		pr_err("RTG-XXXXX - trans_rtg_sched_enqueue !is_depth_valid\n");
#endif
		return;
	}

	// if task is already rtg, return
	if (task->rtg_depth == STATIC_RTG_DEPTH || task->rtg_depth > 0) {
		pr_err("RTG-XXXXX - trans_rtg_sched_enqueue pid:%d STATIC_RTG_DEPTH or rtg_depth > 0 \n",
		       task->pid);
		return;
	}

	if (from->rtg_depth == STATIC_RTG_DEPTH)
		task->rtg_depth = 1;
	else
		task->rtg_depth = from->rtg_depth + 1;

	if (get_enable_type() == TRANS_ENABLE) {
		pr_err("RTG-XXXXX - trans_rtg_sched_enqueue TRANS_ENABLE \n");
		return;
	}

	grpid = honor_get_group_id_gki(from);
	if (grpid == DEFAULT_RT_FRAME_ID || grpid == DEFAULT_CGROUP_COLOC_ID ||
	    (qos == VALUE_QOS_CRITICAL &&
	     (from->prio == DEFAULT_RT_PRIO ||
	      from->prio == DEFAULT_RT_PRIO + 1))) {
		if (g_max_thread_num != DEFAULT_MAX_THREADS &&
		    atomic_read(&g_rtg_thread_num) >= g_max_thread_num) {
			task->rtg_depth = 0;
			pr_err("RTG-XXXXX - trans_rtg_sched_enqueue >= g_max_thread_num pid:%d\n",
			       task->pid);
			return;
		}
		prio = DEFAULT_RT_PRIO;
	} else if (grpid == DEFAULT_AUX_ID) {
		prio = (from->prio < MAX_RT_PRIO ? from->prio : NOT_RT_PRIO);
	} else {
		task->rtg_depth = 0;
		pr_err("RTG-XXXXX - trans_rtg_sched_enqueue >= unknow pid:%d\n",
		       task->pid);
		return;
	}

	if (grpid == DEFAULT_RT_FRAME_ID ||
	    (qos == VALUE_QOS_CRITICAL &&
	     (from->prio == DEFAULT_RT_PRIO ||
	      from->prio == DEFAULT_RT_PRIO + 1))) {
		grpid = DEFAULT_CGROUP_COLOC_ID;
	}

	get_task_struct(task);
#ifdef CONFIG_HN_SET_ASYNC_BINDER_RT
	// qos will transfer rtg for the task, restore the prio set by async binder rt before
	if ((prio >= DEFAULT_RT_PRIO && prio < MAX_RT_PRIO) &&
	    task->need_next_recovery) {
		task->need_next_recovery = false;
		sched_setscheduler_nocheck(task, policy, &sp);
	}
#endif
	ret = set_rtg_sched(task, true, grpid, prio);
	if (ret < 0) {
		task->rtg_depth = 0;
		put_task_struct(task);
		if (ret != SET_RTG_SCHED_ERR_CLASS_INVALID) {
			pr_err("RTG-XXXXX - trans_rtg_sched_enqueue >= failed set_rtg_sched() pid:%d ret = %d\n",
			       task->pid, ret);
		}
		return;
	}

	if (grpid == DEFAULT_RT_FRAME_ID)
		atomic_inc(&g_rtg_thread_num);

	put_task_struct(task);

	trace_sched_rtg(task, from, type, OPERATION_RTG_ENQUEUE);
}
EXPORT_SYMBOL_GPL(trans_rtg_sched_enqueue_for_gki);

void trans_rtg_sched_dequeue_for_gki(struct task_struct *task,
				     unsigned int type)
{
	int ret;
	unsigned int grpid;
	int depth;

	if (!task || type != DYNAMIC_QOS_BINDER)
		return;

	// if task is not rtg or task is orig task, return
	if (task->rtg_depth == 0 || task->rtg_depth == STATIC_RTG_DEPTH)
		return;

	depth = task->rtg_depth;
	task->rtg_depth = 0;

	if (get_enable_type() == TRANS_ENABLE)
		return;

	get_task_struct(task);
	ret = set_rtg_sched(task, false, 0, DEFAULT_RT_PRIO);
	if (ret < 0) {
		task->rtg_depth = depth;
		put_task_struct(task);
		return;
	}

	grpid = honor_get_group_id_gki(task);
	if ((grpid == DEFAULT_RT_FRAME_ID) &&
	    (atomic_read(&g_rtg_thread_num) > 0))
		atomic_dec(&g_rtg_thread_num);

	put_task_struct(task);

	trace_sched_rtg(task, NULL, type, OPERATION_RTG_DEQUEUE);
}
EXPORT_SYMBOL_GPL(trans_rtg_sched_dequeue_for_gki);

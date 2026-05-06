
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2015,2017,2019-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "touch-boost: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/time.h>
#include <linux/sysfs.h>
#include <linux/pm_qos.h>
#include "honor_touch_boost.h"

#define touch_boost_attr_rw(_name)                  \
	static struct kobj_attribute _name##_attr = \
		__ATTR(_name, 0644, show_##_name, store_##_name)

#define show_one(file_name)                                                   \
	static ssize_t show_##file_name(                                      \
		struct kobject *kobj, struct kobj_attribute *attr, char *buf) \
	{                                                                     \
		return scnprintf(buf, PAGE_SIZE, "%u\n", file_name);          \
	}

#define store_one(file_name)                                            \
	static ssize_t store_##file_name(struct kobject *kobj,          \
					 struct kobj_attribute *attr,   \
					 const char *buf, size_t count) \
	{                                                               \
		sscanf(buf, "%u", &file_name);                          \
		return count;                                           \
	}

struct cpu_sync {
	int cpu;
	unsigned int touch_boost_min;
	unsigned int touch_boost_freq;
};

unsigned int sysctl_touch_boost_ms;
unsigned int sysctl_touch_boost_freq[8];
unsigned int sysctl_sched_boost_on_touch;
unsigned int min_max_possible_capacity = 1024;
unsigned int max_possible_capacity = 1024;
unsigned int sched_boost_type;
enum sched_boost_policy boost_policy;
__read_mostly bool sched_freq_aggr_en;
unsigned int sysctl_sched_boost;

static DEFINE_PER_CPU(struct cpu_sync, sync_info);
static struct workqueue_struct *touch_boost_wq;

static struct work_struct touch_boost_work;

static bool sched_boost_active;

static struct delayed_work touch_boost_rem;
static u64 last_touch_time;

#define MIN_TOUCH_INTERVAL (150 * USEC_PER_MSEC)
#define NO_BOOST 0
#define FULL_THROTTLE_BOOST 1
#define CONSERVATIVE_BOOST 2
#define RESTRAINED_BOOST 3
#define RESTRAINED_BOOST_DISABLE -3

static DEFINE_PER_CPU(struct freq_qos_request, qos_req);
static DEFINE_MUTEX(boost_mutex);
static unsigned int one_hundred_thousand = 100000;
/*
 * Scheduler boost type and boost policy might at first seem unrelated,
 * however, there exists a connection between them that will allow us
 * to use them interchangeably during placement decisions. We'll explain
 * the connection here in one possible way so that the implications are
 * clear when looking at placement policies.
 *
 * When policy = SCHED_BOOST_NONE, type is either none or RESTRAINED
 * When policy = SCHED_BOOST_ON_ALL or SCHED_BOOST_ON_BIG, type can
 * neither be none nor RESTRAINED.
 */
static void set_boost_policy(int type)
{
	if (type == NO_BOOST || type == RESTRAINED_BOOST) {
		boost_policy = SCHED_BOOST_NONE;
		return;
	}

	if (hmp_capable()) {
		boost_policy = SCHED_BOOST_ON_BIG;
		return;
	}

	boost_policy = SCHED_BOOST_ON_ALL;
}

static bool verify_boost_params(int type)
{
	return type >= RESTRAINED_BOOST_DISABLE && type <= RESTRAINED_BOOST;
}

static void sched_no_boost_nop(void)
{
}

static void sched_full_throttle_boost_enter(void)
{
#ifndef CONFIG_ARCH_QTI_VM
	core_ctl_set_boost(true);
#endif
	touch_enable_frequency_aggregation(true);
}

static void sched_full_throttle_boost_exit(void)
{
#ifndef CONFIG_ARCH_QTI_VM
	core_ctl_set_boost(false);
#endif
	touch_enable_frequency_aggregation(false);
}

static void sched_conservative_boost_enter(void)
{
}

static void sched_conservative_boost_exit(void)
{
}

static void sched_restrained_boost_enter(void)
{
	touch_enable_frequency_aggregation(true);
}

static void sched_restrained_boost_exit(void)
{
	touch_enable_frequency_aggregation(false);
}

struct sched_boost_data {
	int refcount;
	void (*enter)(void);
	void (*exit)(void);
};

static struct sched_boost_data sched_boosts[] = {
	[NO_BOOST] =
		{
			.refcount = 0,
			.enter = sched_no_boost_nop,
			.exit = sched_no_boost_nop,
		},
	[FULL_THROTTLE_BOOST] =
		{
			.refcount = 0,
			.enter = sched_full_throttle_boost_enter,
			.exit = sched_full_throttle_boost_exit,
		},
	[CONSERVATIVE_BOOST] =
		{
			.refcount = 0,
			.enter = sched_conservative_boost_enter,
			.exit = sched_conservative_boost_exit,
		},
	[RESTRAINED_BOOST] =
		{
			.refcount = 0,
			.enter = sched_restrained_boost_enter,
			.exit = sched_restrained_boost_exit,
		},
};

#define SCHED_BOOST_START FULL_THROTTLE_BOOST
#define SCHED_BOOST_END (RESTRAINED_BOOST + 1)

static int sched_effective_boost(void)
{
	int i;

	/*
	 * The boosts are sorted in descending order by
	 * priority.
	 */
	for (i = SCHED_BOOST_START; i < SCHED_BOOST_END; i++) {
		if (sched_boosts[i].refcount >= 1)
			return i;
	}

	return NO_BOOST;
}

static void sched_boost_disable(int type)
{
	struct sched_boost_data *sb = &sched_boosts[type];
	int next_boost, prev_boost = sched_boost_type;

	if (sb->refcount <= 0)
		return;

	sb->refcount--;

	if (sb->refcount)
		return;

	next_boost = sched_effective_boost();
	if (next_boost == prev_boost)
		return;
	/*
	 * This boost's refcount becomes zero, so it must
	 * be disabled. Disable it first and then apply
	 * the next boost.
	 */
	sched_boosts[prev_boost].exit();
	sched_boosts[next_boost].enter();
}

static void sched_boost_enable(int type)
{
	struct sched_boost_data *sb = &sched_boosts[type];
	int next_boost, prev_boost = sched_boost_type;

	sb->refcount++;

	if (sb->refcount != 1)
		return;

	/*
	 * This boost enable request did not come before.
	 * Take this new request and find the next boost
	 * by aggregating all the enabled boosts. If there
	 * is a change, disable the previous boost and enable
	 * the next boost.
	 */

	next_boost = sched_effective_boost();
	if (next_boost == prev_boost)
		return;

	sched_boosts[prev_boost].exit();
	sched_boosts[next_boost].enter();
}

static void sched_boost_disable_all(void)
{
	int i;
	int prev_boost = sched_boost_type;

	if (prev_boost != NO_BOOST) {
		sched_boosts[prev_boost].exit();
		for (i = SCHED_BOOST_START; i < SCHED_BOOST_END; i++)
			sched_boosts[i].refcount = 0;
	}
}

static void _sched_set_boost(int type)
{
	if (type == 0)
		sched_boost_disable_all();
	else if (type > 0)
		sched_boost_enable(type);
	else
		sched_boost_disable(-type);

	/*
	 * sysctl_sched_boost holds the boost request from
	 * user space which could be different from the
	 * effectively enabled boost. Update the effective
	 * boost here.
	 */

	sched_boost_type = sched_effective_boost();
	sysctl_sched_boost = sched_boost_type;
	set_boost_policy(sysctl_sched_boost);
}

static int touch_sched_set_boost(int type)
{
	int ret = 0;

	mutex_lock(&boost_mutex);
	if (verify_boost_params(type))
		_sched_set_boost(type);
	else
		ret = -EINVAL;
	mutex_unlock(&boost_mutex);
	return ret;
}

static void boost_adjust_notify(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	struct cpu_sync *s = &per_cpu(sync_info, cpu);
	unsigned int tb_min = s->touch_boost_min;
	struct freq_qos_request *req = &per_cpu(qos_req, cpu);
	int ret;

	pr_debug("CPU%u policy min before boost: %u kHz\n", cpu, policy->min);
	pr_debug("CPU%u boost min: %u kHz\n", cpu, tb_min);

	ret = freq_qos_update_request(req, tb_min);

	if (ret < 0)
		pr_err("Failed to update freq constraint in boost_adjust: %d\n",
		       tb_min);

	pr_debug("CPU%u policy min after boost: %u kHz\n", cpu, policy->min);
}

static void update_policy_online(void)
{
	unsigned int i;
	struct cpufreq_policy *policy;
	struct cpumask online_cpus;

	/* Re-evaluate policy to trigger adjust notifier for online CPUs */
	cpus_read_lock();
	online_cpus = *cpu_online_mask;
	for_each_cpu(i, &online_cpus) {
		policy = cpufreq_cpu_get(i);
		if (!policy) {
			pr_err("%s: cpufreq policy not found for cpu%d\n",
			       __func__, i);
			cpus_read_unlock();
			return;
		}

		cpumask_andnot(&online_cpus, &online_cpus,
			       policy->related_cpus);
		boost_adjust_notify(policy);
	}
	cpus_read_unlock();
}

static void do_touch_boost_rem(struct work_struct *work)
{
	unsigned int i, ret;
	struct cpu_sync *t_sync_info;

	/* Reset the touch_boost_min for all CPUs in the system */
	pr_debug("Resetting touch boost min for all CPUs\n");
	for_each_possible_cpu(i) {
		t_sync_info = &per_cpu(sync_info, i);
		t_sync_info->touch_boost_min = 0;
	}

	/* Update policies for all online CPUs */
	update_policy_online();

	if (sched_boost_active) {
		ret = touch_sched_set_boost(0);
		if (!ret)
			pr_err("touch-boost: sched boost disable failed\n");
		sched_boost_active = false;
	}
}

static void do_touch_boost(struct work_struct *work)
{
	unsigned int i, ret;
	struct cpu_sync *t_sync_info;

	cancel_delayed_work_sync(&touch_boost_rem);
	if (sched_boost_active) {
		touch_sched_set_boost(0);
		sched_boost_active = false;
	}

	/* Set the touch_boost_min for all CPUs in the system */
	pr_debug("Setting touch boost min for all CPUs\n");
	for (i = 0; i < 8; i++) {
		t_sync_info = &per_cpu(sync_info, i);
		t_sync_info->touch_boost_min = sysctl_touch_boost_freq[i];
	}

	/* Update policies for all online CPUs */
	update_policy_online();

	/* Enable scheduler boost to migrate tasks to big cluster */
	if (sysctl_sched_boost_on_touch > 0) {
		ret = touch_sched_set_boost(sysctl_sched_boost_on_touch);
		if (ret)
			pr_err("touch-boost: sched boost enable failed\n");
		else
			sched_boost_active = true;
	}

	queue_delayed_work(touch_boost_wq, &touch_boost_rem,
			   msecs_to_jiffies(sysctl_touch_boost_ms));
}

void boost_touch_event(void)
{
	u64 now;
	int cpu;
	int enabled = 0;

	for_each_possible_cpu(cpu) {
		if (sysctl_touch_boost_freq[cpu] > 0) {
			enabled = 1;
			break;
		}
	}
	if (!enabled)
		return;

	now = ktime_to_us(ktime_get());
	if (now - last_touch_time < MIN_TOUCH_INTERVAL)
		return;

	if (work_pending(&touch_boost_work))
		return;

	queue_work(touch_boost_wq, &touch_boost_work);
	last_touch_time = ktime_to_us(ktime_get());
}

struct ctl_table touch_boost_sysctls[] = {
	{
		.procname = "touch_boost_ms",
		.data = &sysctl_touch_boost_ms,
		.maxlen = sizeof(unsigned int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = SYSCTL_ZERO,
		.extra2 = &one_hundred_thousand,
	},
	{
		.procname = "touch_boost_freq",
		.data = &sysctl_touch_boost_freq,
		.maxlen = sizeof(unsigned int) * 8,
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = SYSCTL_ZERO,
		.extra2 = SYSCTL_INT_MAX,
	},
	{
		.procname = "sched_boost_on_touch",
		.data = &sysctl_sched_boost_on_touch,
		.maxlen = sizeof(unsigned int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = SYSCTL_ZERO,
		.extra2 = SYSCTL_INT_MAX,
	},
	{},
};

struct kobject *touch_boost_kobj;
int touch_boost_init(void)
{
	int cpu, ret;
	struct cpu_sync *s;
	struct cpufreq_policy *policy;
	struct freq_qos_request *req;
	struct ctl_table_header *tb;

	touch_boost_wq = alloc_workqueue("touchboost_wq", WQ_HIGHPRI, 0);
	if (!touch_boost_wq)
		return -EFAULT;

	INIT_WORK(&touch_boost_work, do_touch_boost);
	INIT_DELAYED_WORK(&touch_boost_rem, do_touch_boost_rem);

	for_each_possible_cpu(cpu) {
		s = &per_cpu(sync_info, cpu);
		s->cpu = cpu;
		req = &per_cpu(qos_req, cpu);
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_err("%s: cpufreq policy not found for cpu %d\n",
			       __func__, cpu);
			return -ESRCH;
		}

		ret = freq_qos_add_request(&policy->constraints, req,
					   FREQ_QOS_MIN, policy->min);
		if (ret < 0) {
			pr_err("%s: Failed to add freq constraint %d\n",
			       __func__, ret);
			return ret;
		}
	}

	tb = register_sysctl_table(touch_boost_sysctls);
	return 0;
}

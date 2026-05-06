/*
 * Copyright (c) Honor Technologies Co., Ltd. 2020. All rights reserved.
 * Description: hyperhold header file
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author:	Lin Kunli <linkunli.>
 *		Wang Xin<wang23.>
 *
 * Create: 2022-8-27
 *
 */

#ifdef CONFIG_HYPERHOLD_GKI

#ifndef HYPERHOLD_GKI_MEMCG_CONTROL_H
#define HYPERHOLD_GKI_MEMCG_CONTROL_H

#include <linux/device.h>
#include <linux/memcontrol.h>
#include <linux/cpumask.h>
#include <linux/version.h>

#include "zram_drv.h"

#define MEM_CGROUP_NAME_MAX_LEN 100
#define APPSCORE_MAX_LEN 100
#define HYPERHOLD_MEMCG_INFO_ITEMS 2
#define HYPERHOLD_APPSCORE_ITEMS 2
#define APPSCORE_MAX 1000
#define APPSCORE_MIN 0

#define PAGES_PER_1MB (1 << 8)
//TODO change this value to dynamic
#define MEMCG_SMALL_ANON_THRESHOLD PAGES_PER_1MB * 10

enum hh_mem_type {
	HH_INACTIVE_ANON,
	HH_ACTIVE_ANON,
	HH_ANON,
	HH_INACTIVE_FILE,
	HH_ACTIVE_FILE,
	HH_FILE,
	HH_ANON_AND_FILE,
	HH_UNEVICTABLE,
	HH_NR_MM_TYPE_ALL,
};

struct memcg_reclaim {
	atomic64_t ub_ufs2zram_ratio;
	atomic_t ub_zram2ufs_ratio;
	atomic_t app_score;
	atomic_t fault_type;
	atomic_t ub_mem2zram_ratio;
	atomic_t refault_threshold;
	atomic64_t nr_can_shrink;
	/* anon refault */
	unsigned long long reclaimed_pagefault;
};

struct mem_cgroup_ext {
	atomic_t valid;
	int memcg_id;
	unsigned long zram_lru;
	unsigned long ext_lru;
	struct list_head link_list;
	spinlock_t zram_init_lock;
	struct zram *zram;
	struct mem_cgroup *sys_memcg;
	unsigned int mcg_ext_id;

	atomic64_t zram_stored_size;
	atomic64_t zram_page_size;
	unsigned long zram_watermark;

	atomic_t hyperhold_extcnt;
	atomic_t hyperhold_peakextcnt;

	atomic64_t hyperhold_stored_pages;
	atomic64_t hyperhold_stored_size;
	atomic64_t hyperhold_ext_notify_free;

	atomic64_t hyperhold_outcnt;
	atomic64_t hyperhold_incnt;
	atomic64_t hyperhold_allfaultcnt;
	atomic64_t hyperhold_faultcnt;

	atomic64_t hyperhold_outextcnt;
	atomic64_t hyperhold_inextcnt;

	atomic64_t zram_idle_size;
	atomic64_t zram_idle_page;

	bool in_swapin;
	atomic64_t hyperhold_cold_writeback;

	struct memcg_reclaim memcg_reclaimed;
	struct list_head score_node;
	char name[MEM_CGROUP_NAME_MAX_LEN];
};

static inline unsigned long
hyperhold_memcg_page_state_local(struct mem_cgroup *memcg, int idx)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	struct lruvec *lruvec;
	struct pglist_data *pgdata = NODE_DATA(0);

	lruvec = mem_cgroup_lruvec(memcg, pgdata);
	return lruvec_page_state_local(lruvec, idx);
#else

	long x = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		x += per_cpu(memcg->vmstats_percpu->state[idx], cpu);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;

#endif
}

struct mem_cgroup *memcg_from_memcg_id(unsigned short memcg_id);
struct mem_cgroup_ext *memcg_ext_from_memcg_ext_id(unsigned int memcg_ext_id);
struct mem_cgroup *memcg_from_memcg_ext(struct mem_cgroup_ext *memcg_ext);
struct mem_cgroup *memcg_from_memcg_ext_id(unsigned int memcg_ext_id);
unsigned short memcg_id_from_memcg(struct mem_cgroup *memcg);
unsigned short memcg_id_from_memcg_ext(struct mem_cgroup_ext *memcg_ext);
unsigned short memcg_id_from_memcg_ext_id(unsigned int memcg_ext_id);
struct mem_cgroup_ext *memcg_ext_from_memcg(struct mem_cgroup *memcg);
struct mem_cgroup_ext *memcg_ext_from_memcg_id(unsigned short memcg_id);
unsigned int memcg_ext_id_from_memcg(struct mem_cgroup *memcg);
unsigned int memcg_ext_id_from_memcg_id(unsigned short memcg_id);
unsigned int memcg_ext_id_from_memcg_ext(struct mem_cgroup_ext *memcg_ext);
bool hyperhold_memcg_anon_is_small(struct mem_cgroup *memcg);
int hyperhold_get_memcg_score(struct mem_cgroup_ext *memcg_ext);
unsigned long hyperhold_get_memcg_size(struct mem_cgroup *memcg,
				       enum hh_mem_type type);

unsigned long hyperhold_mcg_shrink(unsigned short mcgid,
				   unsigned long nr_to_shrink);
bool mem_cgroup_ext_check(struct mem_cgroup_ext *mcg_ext);
struct mem_cgroup_ext *get_next_memcg_ext(struct mem_cgroup_ext *mcg_ext_prev);
void get_next_memcg_ext_break(struct mem_cgroup_ext *memcg_ext);
void memcg_app_score_sort_ext(struct mem_cgroup_ext *target);
void memcg_ext_init(void);
ssize_t hyperhold_appscore_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t hyperhold_appscore_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t len);
ssize_t hyperhold_force_swapout_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len);
ssize_t hyperhold_force_swapin_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len);
ssize_t hyperhold_memcg_info_show(struct device *dev,
				  struct device_attribute *attr, char *buf);
ssize_t hyperhold_force_shrink_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len);
void force_shrink_anon_enable(void);
void force_shrink_anon_disable(void);
#ifdef CONFIG_HYPERHOLD_GKI_DEBUG
ssize_t hyperhold_memcg_stat_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len);
ssize_t hyperhold_memcg_stat_show(struct device *dev,
				  struct device_attribute *attr, char *buf);
#endif
#endif

#endif

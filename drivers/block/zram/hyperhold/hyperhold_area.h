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
 * Author:	He Biao <hebiao6.>
 *		Wang Cheng Ke <wangchengke2.>
 *		Wang Fa <fa.wang.>
 *
 * Create: 2020-4-16
 *
 */

#ifndef _HYPERHOLD_AREA_H
#define _HYPERHOLD_AREA_H

#include <linux/memcontrol.h>
#include "hyperhold_internal.h"

struct hyperhold_area {
	unsigned long size;
	int nr_objs;
	int nr_exts;
	int nr_partition_exts;
	int nr_mcgs;

	unsigned long *bitmap;
	atomic_t next_alloc_bit;

	struct hh_list_table *ext_table;
	struct hh_list_head *ext;

	struct hh_list_table *obj_table;
	struct hh_list_head *rmap;
	struct hh_list_head *lru;

	atomic_t stored_exts;
	atomic_t *ext_stored_pages;
#ifdef CONFIG_HYPERHOLD_CACHE
	atomic64_t *ext_stored_size;
#endif
#ifdef CONFIG_HYPERHOLD_GKI
	struct mutex idle_lock;
	int idle_pid;
	struct mutex shrink_lock;
	int shrink_pid;
	spinlock_t mcg_ext_lock;
	unsigned long *mcg_ext_bitmap;
	unsigned long *mcg_ext_rmap;
	unsigned int mcg_id_cnt[HH_MEM_CGROUP_EXT_ID_MAX + 1];
#else
	unsigned int mcg_id_cnt[MEM_CGROUP_ID_MAX + 1];
#endif
#ifdef CONFIG_HYPERHOLD_CACHE
	unsigned int cache_high_wm;
	unsigned int cache_low_wm;
	struct hyperhold_cache *cache;
#ifdef CONFIG_SHRINKER_LOCKLESS_OPT
	struct shrinker *cache_shrinker;
#else
	struct shrinker cache_shrinker;
#endif
	atomic64_t cache_shrinker_runs;
	atomic64_t cache_shrinker_reclaim_pages;
	atomic64_t cache_shrink_time;

	atomic64_t cache_move_runs;
	atomic64_t cache_move_exts;
	atomic64_t cache_move_time;

	bool fault_out_cache_enable;
	bool batch_out_cache_enable;
	bool reclaim_in_cache_enable;
#endif
};
#ifdef CONFIG_HYPERHOLD_CACHE
int hp_cache_ext_wm(int tot, unsigned int wm);
#endif
#ifdef CONFIG_HYPERHOLD_GKI
int hyperhold_alloc_extent(struct hyperhold_area *area,
			   struct mem_cgroup_ext *mcg);
#ifdef CONFIG_HYPERHOLD_CACHE
int get_memcg_extent(struct hyperhold_area *area, struct mem_cgroup_ext *mcg,
		     bool (*filter)(struct hyperhold_area *area, int ext_id));
#else
int get_memcg_extent(struct hyperhold_area *area, struct mem_cgroup_ext *mcg);
#endif
int get_memcg_zram_entry(struct hyperhold_area *area,
			 struct mem_cgroup_ext *mcg);
int alloc_bitmap(unsigned long *bitmap, int max, int last_bit);
#else
struct mem_cgroup *get_mem_cgroup(unsigned short mcg_id);
int hyperhold_alloc_extent(struct hyperhold_area *area, struct mem_cgroup *mcg);
#ifdef CONFIG_HYPERHOLD_CACHE
int get_memcg_extent(struct hyperhold_area *area, struct mem_cgroup *mcg,
		     bool (*filter)(struct hyperhold_area *area, int ext_id));
#else
int get_memcg_extent(struct hyperhold_area *area, struct mem_cgroup *mcg);
#endif
int get_memcg_zram_entry(struct hyperhold_area *area, struct mem_cgroup *mcg);
#endif

int obj_idx(struct hyperhold_area *area, int idx);
int ext_idx(struct hyperhold_area *area, int idx);
int mcg_idx(struct hyperhold_area *area, int idx);
int extent_bit2id(struct hyperhold_area *area, int bit);
int extent_id2bit(struct hyperhold_area *area, int id);

void free_hyperhold_area(struct hyperhold_area *area);
struct hyperhold_area *alloc_hyperhold_area(unsigned long ori_size,
					    unsigned long comp_size);
void hyperhold_free_extent(struct hyperhold_area *area, int ext_id);
int get_extent(struct hyperhold_area *area, int ext_id);
void put_extent(struct hyperhold_area *area, int ext_id);
int get_extent_zram_entry(struct hyperhold_area *area, int ext_id);
#ifdef CONFIG_HYPERHOLD_CACHE
unsigned long hyperhold_shrink_cache(struct hyperhold_area *area,
				     unsigned long nr_pages);
bool hp_cache_init(struct hyperhold_area *area);
#endif
#endif

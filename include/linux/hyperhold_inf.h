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
 */

#ifndef HYPERHOLD_INF_H
#define HYPERHOLD_INF_H

#define EXTENT_SHIFT 15
#define EXTENT_SIZE (1UL << EXTENT_SHIFT)
#define EXTENT_PG_CNT (EXTENT_SIZE >> PAGE_SHIFT)
#define EXTENT_SECTOR_SIZE (EXTENT_PG_CNT << 3)

#define MIN_RECLAIM_ZRAM_SZ (1024 * 1024)
#define FT_RECLAIM_SZ 500

enum hyperhold_mcg_member {
	MCG_ZRAM_STORED_SZ = 0,
	MCG_ZRAM_PG_SZ,
	MCG_DISK_STORED_SZ,
	MCG_DISK_STORED_PG_SZ,
	MCG_ANON_FAULT_CNT,
	MCG_DISK_FAULT_CNT,
	MCG_SWAPOUT_CNT,
	MCG_SWAPOUT_SZ,
	MCG_SWAPIN_CNT,
	MCG_SWAPIN_SZ,
	MCG_DISK_SPACE,
	MCG_DISK_SPACE_PEAK,
};

#ifdef CONFIG_HYPERHOLD_GKI
//control system level shrink
struct hh_scan_control {
	//we want to reclaim nr_to_shrink pages
	unsigned long nr_to_shrink;
	//pages all memecgs can be reclaimed
	unsigned long total_can_shrink;
	//pages reclaimed
	unsigned long nr_shrinked;

	//skip reclaim if memcg anon is small or not
	unsigned int memcg_small_skipped : 1;
	//skip score 0 app or not
	unsigned int score_0_skipped : 1;
	//if buffer is check before each reclaim
	unsigned int buffer_check : 1;
	//try to reclaim pages from all memcgs or not
	unsigned int fair_shrink : 1;
	//ub_mem2zram_ratio is considered or not
	unsigned int mem2zram_check : 1;
};
#endif /* ifdef CONFIG_HYPERHOLD_GKI */

#ifdef CONFIG_HYPERHOLD_DEBUG
unsigned long hyperhold_stored_size(void);
unsigned long hyperhold_eswap_used(void);
unsigned long hyperhold_eswap_total(void);
#endif /* ifdef CONFIG_HYPERHOLD_DEBUG */

#if (defined CONFIG_HYPERHOLD_CORE) || (defined CONFIG_HYPERHOLD_GKI)
extern void hyperhold_mem_cgroup_remove(struct mem_cgroup *memcg);
extern unsigned long hyperhold_reclaim_in(unsigned long size);
extern unsigned long hyperhold_reclaim_in_memcg(struct mem_cgroup *memcg,
						unsigned long size);
extern unsigned long hyperhold_reclaim_in_sync(unsigned long size);
extern unsigned long hyperhold_get_zram_used_pages(void);
extern unsigned long long hyperhold_get_zram_pagefault(void);
extern bool hyperhold_reclaim_work_running(void);
extern unsigned long long
hyperhold_read_mcg_stats(struct mem_cgroup *mcg,
			 enum hyperhold_mcg_member mcg_member);
extern bool hyperhold_enable(void);
extern int hyperhold_batch_out(struct mem_cgroup *mcg, unsigned long size,
			       bool preload);

#ifdef CONFIG_HYPERHOLD_GKI
void hyperhold_shrink_async(struct mem_cgroup *memcg,
			    struct hh_scan_control *sc);
#endif /* ifdef CONFIG_HYPERHOLD_GKI */

#ifdef CONFIG_HYPERHOLD_CORE
extern int hyperhold_permcg_reclaim(struct mem_cgroup *memcg, void *data);
#endif /* ifdef CONFIG_HYPERHOLD_CORE */

#ifdef CONFIG_HYPERHOLD_GKI
ssize_t hyperhold_psi_show(char *buf, ssize_t buf_size, ssize_t off);
#else //ifdef CONFIG_HYPERHOLD_GKI
#ifdef CONFIG_HYPERHOLD_CORE
void hyperhold_psi_show(struct seq_file *m);
#endif // ifdef CONFIG_HYPERHOLD_CORE
#endif // ifdef CONFIG_HYPERHOLD_GKI

#ifdef CONFIG_HYPERHOLD_DEBUG_FS
extern const struct file_operations proc_hyperhold_operations;
#endif // ifdef CONFIG_HYPERHOLD_DEBUG_FS

#else /* if (defined CONFIG_HYPERHOLD_CORE || defined CONFIG_HYPERHOLD_GKI) */
static inline int hyperhold_batch_out(struct mem_cgroup *mcg,
				      unsigned long size, bool preload)
{
	return 0;
}

static inline unsigned long hyperhold_reclaim_in(unsigned long size)
{
	return 0;
}

unsigned long hyperhold_reclaim_in_memcg(struct mem_cgroup *memcg,
					 unsigned long size)
{
	return 0;
}

unsigned long hyperhold_reclaim_in_sync(unsigned long size)
{
	return 0;
}

static inline unsigned long hyperhold_get_zram_used_pages(void)
{
	return 0;
}

static inline unsigned long long hyperhold_get_zram_pagefault(void)
{
	return 0;
}

static inline bool hyperhold_reclaim_work_running(void)
{
	return false;
}

static inline void hyperhold_mem_cgroup_remove(struct mem_cgroup *memcg)
{
}

static inline unsigned long long
hyperhold_read_mcg_stats(struct mem_cgroup *mcg,
			 enum hyperhold_mcg_member mcg_member)
{
	return 0;
}

static inline bool hyperhold_enable(void)
{
	return 0;
}

static inline void hyperhold_psi_show(struct seq_file *m)
{
}
#endif /* if (defined CONFIG_HYPERHOLD_CORE || CONFIG_HYPERHOLD_GKI) */

#endif

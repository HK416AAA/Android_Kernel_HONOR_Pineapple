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

#ifndef HYPERHOLD_H
#define HYPERHOLD_H

#ifdef CONFIG_HYPERHOLD_GKI
#include "hyperhold_gki_memcg_control.h"
#endif

#if (defined CONFIG_HYPERHOLD_CORE) || (defined CONFIG_HYPERHOLD_GKI)
extern void hyperhold_init(struct zram *zram);

#ifdef CONFIG_HYPERHOLD_GKI
void hyperhold_gki_zram_exit(void);
ssize_t hyperhold_cold_writeback_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len);
#ifdef CONFIG_HYPERHOLD_GKI_DEBUG
ssize_t hyperhold_gki_debug_level_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len);
ssize_t hyperhold_gki_debug_level_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf);
#endif /* ifdef CONFIG_HYPERHOLD_GKI_DEBUG */
#endif /* CONFIG_HYPERHOLD_GKI */
extern void hyperhold_track(struct zram *zram, u32 index,
			    struct mem_cgroup *memcg);
extern void hyperhold_untrack(struct zram *zram, u32 index);
extern int hyperhold_fault_out(struct zram *zram, u32 index);
extern bool hyperhold_delete(struct zram *zram, u32 index);
extern unsigned long zram_zsmalloc(struct zs_pool *zs_pool, size_t size,
				   gfp_t gfp);
#ifdef CONFIG_HYPERHOLD_DEBUG_FS
extern ssize_t hyperhold_ft_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len);
extern ssize_t hyperhold_ft_show(struct device *dev,
				 struct device_attribute *attr, char *buf);
extern ssize_t hyperhold_swap_time_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len);
extern ssize_t hyperhold_swap_time_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);
extern ssize_t hyperhold_io_time_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len);
extern ssize_t hyperhold_io_time_show(struct device *dev,
				      struct device_attribute *attr, char *buf);
#endif /* CONFIG_HYPERHOLD_DEBUG_FS */
#ifdef CONFIG_HYPERHOLD_CACHE
extern ssize_t hyperhold_cache_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len);
extern ssize_t hyperhold_cache_show(struct device *dev,
				    struct device_attribute *attr, char *buf);
#endif
extern ssize_t hyperhold_report_show(struct device *dev,
				     struct device_attribute *attr, char *buf);
extern ssize_t hyperhold_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len);
extern ssize_t hyperhold_enable_show(struct device *dev,
				     struct device_attribute *attr, char *buf);
extern ssize_t hyperhold_mapper_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len);
extern ssize_t hyperhold_mapper_show(struct device *dev,
				     struct device_attribute *attr, char *buf);
extern ssize_t hyperhold_cryptokey_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len);
extern ssize_t hyperhold_cryptokey_show(struct device *dev,
					struct device_attribute *attr,
					char *buf);

extern struct task_struct *get_task_from_proc(struct inode *inode);

#ifdef CONFIG_HYPERHOLD_FILE
extern ssize_t hyperhold_space_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len);
extern ssize_t hyperhold_space_show(struct device *dev,
				    struct device_attribute *attr, char *buf);
extern ssize_t hyperhold_space_reset_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len);
#endif

extern void hyperhold_idle_inc(struct zram *zram, u32 index);
extern void hyperhold_idle_dec(struct zram *zram, u32 index);
extern void hyperhold_idle_reclaim(void);

#else /* if (defined CONFIG_HYPERHOLD_CORE || defined CONFIG_HYPERHOLD_GKI) */
static inline void hyperhold_init(struct zram *zram)
{
}
static inline void hyperhold_track(struct zram *zram, u32 index,
				   struct mem_cgroup *mem_cgroup)
{
}
static inline void hyperhold_untrack(struct zram *zram, u32 index)
{
}
static inline int hyperhold_fault_out(struct zram *zram, u32 index)
{
	return 0;
}
static inline bool hyperhold_delete(struct zram *zram, u32 index)
{
	return true;
}
static inline ssize_t hyperhold_enable_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t len)
{
	return len;
}

static inline ssize_t hyperhold_enable_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	return 0;
}
#ifdef CONFIG_HYPERHOLD_CACHE
static inline ssize_t hyperhold_cache_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	return len;
}
static inline ssize_t hyperhold_cache_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return 0;
}
#endif
#ifdef CONFIG_HYPERHOLD_DEBUG_FS
static inline ssize_t hyperhold_ft_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	return len;
}

static inline ssize_t
hyperhold_ft_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
static inline ssize_t hyperhold_swap_time_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t len)
{
	return len;
}
static inline ssize_t hyperhold_swap_time_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	return 0;
}
static inline ssize_t hyperhold_io_time_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t len)
{
	return len;
}
static inline ssize_t hyperhold_io_time_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	return 0;
}
#endif

static inline ssize_t hyperhold_report_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	return 0;
}

static inline void hyperhold_idle_inc(struct zram *zram, u32 index)
{
}
static inline void hyperhold_idle_dec(struct zram *zram, u32 index)
{
}
static inline void hyperhold_idle_reclaim(void)
{
}
static inline ssize_t hyperhold_cryptokey_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t len)
{
	return len;
}
static inline ssize_t hyperhold_cryptokey_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	return 0;
}
#endif /* if (defined CONFIG_HYPERHOLD_CORE || CONFIG_HYPERHOLD_GKI */

#endif /* hyperhold_h */

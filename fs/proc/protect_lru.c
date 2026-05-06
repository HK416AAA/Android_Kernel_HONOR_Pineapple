/*
 * Copyright (c) Honor Device Co., Ltd. 2017-2020. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Protect lru of task support. It's between normal lru and mlock,
 * that means we will reclaim protect lru pages as late as possible.
 */
#include <linux/protect_lru.h>

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>
#include <linux/xattr.h>
#include <linux/swap.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <linux/mm_inline.h>
#include <linux/bug.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#ifdef CONFIG_HONOR_MEMLOCK_STATS
#include <linux/mem_lock_info.h>
#endif
#include "internal.h"
#include "../internal.h"

#define MAX_BUFFER_LEN 256
#define MAX_LEN (10 + MAX_BUFFER_LEN)
#define DECIMAL_BASE 10
#define PAGES_PER_MBYTES_SHIFT (20 - PAGE_SHIFT)
#define PERCENT_DENOMINATOR 100
#define RECLAIM_COUNT 10

enum Protect_Data_Index {
	PROTECT_FD,
	PROTECT_LEVEL,
	PROTECT_MAX,
};

static int sysctl_zero;
static int sysctl_one = 1;
static int sysctl_one_hundred = 100;
static struct mutex handler_mutex;
static unsigned long sysctl_ulong_zero;
static unsigned long protect_max_mbytes[PROTECT_LEVELS_MAX];
static unsigned long protect_cur_mbytes[PROTECT_LEVELS_MAX];

unsigned long protect_lru_enable __read_mostly = 1;
unsigned long protect_reclaim_ratio __read_mostly = 50;

#if defined(CONFIG_OVERLAY_FS) && (defined(CONFIG_MEMCG_PROTECT_LRU) || \
				   defined(CONFIG_HN_CGROUP_WORKINGSET))
extern struct file *get_real_file(struct file *filp);
#endif

struct mem_cgroup *prot_mem_cgroup[PROTECT_LEVELS_MAX] = {0, 0, 0};

static struct cgroup_subsys_state *mem_cgroup_css(struct mem_cgroup *memcg)
{
	return &memcg->css;
}

/*lint -e548*/
static bool is_prot_memcg_overlimit(struct mem_cgroup *memcg)
{
	struct page_counter *counter = &memcg->memory;
	unsigned long val;
	unsigned long limit;

	val = page_counter_read(counter);

	limit = counter->max;

	if (!limit)
		return false;

	if (val + SWAP_CLUSTER_MAX > limit)
		return true;

	return false;
}

static bool is_prot_memcg_overratio(void)
{
	unsigned long prot_file = 0;
	unsigned long all_file = 0;
	struct mem_cgroup *memcg = NULL;
	struct zone *zone = NULL;
	int i;

	for (i = 0; i < PROTECT_LEVELS_MAX; i++) {
		if (!prot_mem_cgroup[i])
			continue;
		memcg = prot_mem_cgroup[i];
		prot_file += page_counter_read(&memcg->memory);
	}

	if (!prot_file)
		return false;

	for_each_populated_zone(zone)
		all_file += zone_page_state(zone, NR_ZONE_ACTIVE_FILE) +
			    zone_page_state(zone, NR_ZONE_INACTIVE_FILE);

	if (!protect_reclaim_ratio || (prot_file * PERCENT_DENOMINATOR >
				       protect_reclaim_ratio * all_file))
		return true;

	return false;
}

static int move_prot_memcg_page(struct mem_cgroup *from, struct mem_cgroup *to,
				struct folio *folio)
{
	struct lruvec *from_lruvec = NULL;
	struct lruvec *lruvec = NULL;
	int ret = -EBUSY;
	unsigned long flags;
#ifdef CONFIG_HONOR_MEMLOCK_STATS
	unsigned long time_start;
#endif
	/* Skip if it is a non lru page. */
	if (!folio_test_lru(folio))
		return ret;

	/* Skip if it is a free page. */
	if (!folio_try_get(folio))
		return ret;

	/* Isolate form lru list. */
	from_lruvec = folio_lruvec(folio);
	if (!folio_test_clear_lru(folio)) {
		/* Another thread is already isolating this folio */
		goto put_folio;
	}
	lruvec_del_folio(from_lruvec, folio);

	/* Move to a new memcg. */
	if (!protect_memcg_move_account(folio, 0, from, to)) {
		ret = 0;
		WARN_ON(folio_memcg(folio) != root_mem_cgroup);
		folio_clear_protect(folio);
	}

	if (folio_test_protect(folio))
		WARN_ON(!is_prot_memcg(folio_memcg(folio), false));

	lruvec = folio_lruvec(folio);
	if (!is_prot_memcg(folio_memcg(folio), false)) {
#ifdef CONFIG_HONOR_MEMLOCK_STATS
		time_start = ktime_get_mono_fast_ns();
#endif
		spin_lock_irqsave(&lruvec->lru_lock, flags);
#ifdef CONFIG_HONOR_MEMLOCK_STATS
		count_memlock_time(&lrulock_stat,
				   ktime_get_mono_fast_ns() - time_start);
#endif
	}
	lruvec_add_folio(lruvec, folio);
	folio_set_lru(folio);
	if (!is_prot_memcg(folio_memcg(folio), false))
		spin_unlock_irqrestore(&lruvec->lru_lock, flags);

put_folio:
	if (unlikely(folio_put_testzero(folio))) {
		spin_unlock(&from_lruvec->lru_lock);
		__folio_put(folio);
		spin_lock(&from_lruvec->lru_lock);
	}

	return ret;
}

/*
 * Return 0 for non protect memcg, return num for protect memcg.
 */
int is_prot_memcg(struct mem_cgroup *memcg, bool boot)
{
	char memcg_name[NAME_MAX + 1];
	char prot_memcg_name[NAME_MAX + 1];
	int i;

	if (!memcg)
		return 0;

	if (mem_cgroup_disabled())
		return 0;

	if (boot) {
		struct cgroup_subsys_state *css = mem_cgroup_css(memcg);

		cgroup_name(css->cgroup, memcg_name, sizeof(memcg_name));
		for (i = 0; i < PROTECT_LEVELS_MAX; i++) {
			if (sprintf(prot_memcg_name, "protect_lru_%d", i + 1) ==
			    -1)
				return 0;
			if (!strcmp(memcg_name, prot_memcg_name))
				return i + 1;
		}
	} else {
		for (i = 0; i < PROTECT_LEVELS_MAX; i++) {
			if (prot_mem_cgroup[i] && memcg == prot_mem_cgroup[i])
				return i + 1;
		}
	}

	return 0;
}
#ifdef CONFIG_LRU_GEN
static int shrink_lru_gen_prot_memcg(struct lruvec *lruvec)
{
	int type = LRU_GEN_FILE;
	int seq, gen, zone;
	int moved = 0;
	unsigned long scan;
	struct folio *folio;
	struct lru_gen_folio *lrugen = &lruvec->lrugen;
	struct mem_cgroup *memcg = lruvec_memcg(lruvec);

	seq = lrugen->min_seq[type];
retry:
	if (seq > lruvec->lrugen.max_seq)
		return moved;
	gen = lru_gen_from_seq(seq);

	for (zone = MAX_NR_ZONES - 1; zone >= 0; zone--) {
		struct list_head *head = &lrugen->folios[gen][type][zone];

		/*
		 * As move to the root memcg, so there is
		 * no need to call precharge.
		 */
		for (scan = 0; scan < SWAP_CLUSTER_MAX && !list_empty(head);
		     scan++) {
			folio = lru_to_folio(head);
			if (!move_prot_memcg_page(memcg, root_mem_cgroup,
						  folio))
				moved++;
		}
	}

	if (moved < SWAP_CLUSTER_MAX) {
		seq += 1;
		goto retry;
	}

	return moved;
}
#else
static int shrink_lru_gen_prot_memcg(struct lruvec *lruvec)
{
	return 0;
}
#endif
int shrink_prot_memcg(struct mem_cgroup *memcg)
{
	struct lruvec *lruvec = NULL;
	struct list_head *head = NULL;
	struct folio *folio = NULL;
	unsigned long scan;
	unsigned long flags;
	int nid;
	int moved = 0;
#ifdef CONFIG_HONOR_MEMLOCK_STATS
	unsigned long time_start;
#endif

	if (!memcg)
		return moved;
	/*
	 * A memcg includes at least one lru list, so we may shrink the lru
	 * which have added pages just now.
	 */

	for_each_node_state(nid, N_MEMORY) {
		lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(nid));
#ifdef CONFIG_HONOR_MEMLOCK_STATS
		time_start = ktime_get_mono_fast_ns();
#endif
		spin_lock_irqsave(&lruvec->lru_lock, flags);
#ifdef CONFIG_HONOR_MEMLOCK_STATS
		count_memlock_time(&lrulock_stat,
				   ktime_get_mono_fast_ns() - time_start);
#endif
		if (lru_gen_enabled()) {
			moved += shrink_lru_gen_prot_memcg(lruvec);
			spin_unlock_irqrestore(&lruvec->lru_lock, flags);
			continue;
		}
		head = &lruvec->lists[LRU_INACTIVE_FILE];
		/* Shrink inactive list first. */
		if (list_empty(head))
			head = &lruvec->lists[LRU_ACTIVE_FILE];

		if (list_empty(head))
			head = &lruvec->lists[LRU_UNEVICTABLE];

		/*
		 * As move to the root memcg, so there is
		 * no need to call precharge.
		 */
		for (scan = 0; scan < SWAP_CLUSTER_MAX && !list_empty(head);
		     scan++) {
			folio = list_entry(head->prev, struct folio, lru);
			if (!move_prot_memcg_page(memcg, root_mem_cgroup,
						  folio))
				moved++;
		}

		/*
		 * We can not sched here, because it
		 * may be called from atomic opt.
		 */
		spin_unlock_irqrestore(&lruvec->lru_lock, flags);
	}

	protect_memcg_cancel_charge(memcg, moved);

	return moved;
}

void shrink_prot_memcg_by_overlimit(struct mem_cgroup *memcg)
{
	int count = 0;

	if (mem_cgroup_disabled())
		return;

	if (!is_prot_memcg(memcg, false))
		return;

	while (is_prot_memcg_overlimit(memcg) && count < RECLAIM_COUNT) {
		shrink_prot_memcg(memcg);
		count++;
	}
}

void shrink_prot_memcg_by_overratio(void)
{
	struct mem_cgroup *memcg = NULL;
	int i;
	int count = 0;

	if (mem_cgroup_disabled())
		return;

	/*
	 * This loop may exceed 300 times by test, expend
	 * more than 20ms, so add 10 loop times limit.
	 */
	while (is_prot_memcg_overratio() && count < RECLAIM_COUNT) {
		for (i = 0; i < PROTECT_LEVELS_MAX; i++) {
			if (!prot_mem_cgroup[i])
				continue;
			memcg = prot_mem_cgroup[i];
			shrink_prot_memcg(memcg);
		}
		count++;
	}
}

int protect_memcg_css_online(struct cgroup_subsys_state *css,
			     struct mem_cgroup *memcg)
{
	int num;
	struct mem_cgroup *parent = NULL;

	if (!css || !memcg)
		return -EINVAL;

	parent = mem_cgroup_from_css(css->parent);
	/* Do not create a child memcg under protect memcg. */
	if (is_prot_memcg(parent, true))
		return -EINVAL;

	/* Protect memcg could only be created under the root memcg. */
	num = is_prot_memcg(memcg, true);
	if (!is_valid_protect_level(num))
		return 0;

	if (parent != root_mem_cgroup)
		return -EINVAL;

	/* Init protect memcg ptr. */
	WARN_ON(prot_mem_cgroup[num - 1]);
	prot_mem_cgroup[num - 1] = memcg; /*lint !e676*/
	pr_info("protect_lru: online memcg,num=%d\n", num);
	return 0;
}

void protect_memcg_css_offline(struct mem_cgroup *memcg)
{
	int num;
	unsigned long prot_file;

	if (!memcg)
		return;

	num = is_prot_memcg(memcg, false);
	if (is_valid_protect_level(num)) {
		/* Empty the protect memcg */
		do {
			lru_add_drain_all();
			shrink_prot_memcg(memcg);
			prot_file = memcg_page_state(memcg, NR_FILE_PAGES);
			cond_resched();
		} while (prot_file);

		WARN_ON(!prot_mem_cgroup[num - 1]);
		prot_mem_cgroup[num - 1] = NULL; /*lint !e676*/
		pr_info("protect_lru: offline memcg,num=%d\n", num);
	}
}

struct mem_cgroup *get_protect_memcg(struct folio *folio,
				     struct mem_cgroup **memcgp)
{
	struct mem_cgroup *memcg = NULL;

	if (!folio || !memcgp || !folio_test_protect(folio))
		return memcg;

	WARN_ON(folio_test_swapcache(folio));
	WARN_ON(folio_memcg(folio));

	rcu_read_lock();
	memcg = *memcgp;
	if (memcg && !css_tryget_online(&memcg->css)) {
		folio_clear_protect(folio);
		memcg = NULL;
	}

	if (is_prot_memcg(memcg, false) && !memcg->memory.max) {
		folio_clear_protect(folio);
		memcg = NULL;
	}
	rcu_read_unlock();

	return memcg;
}

struct mem_cgroup *get_protect_file_memcg(struct folio *folio,
					  struct address_space *mapping)
{
	struct mem_cgroup *memcg = NULL;
	struct inode *inode = NULL;
	int num;

	if (!folio || !mapping || !protect_lru_enable)
		return memcg;

	inode = mapping->host;
	if (inode)
		num = inode->i_protect;
	else
		num = 0;

	if (unlikely(is_valid_protect_level(num)) && prot_mem_cgroup[num - 1]) {
		WARN_ON(folio_memcg(folio));
		folio_set_protect(folio);
		memcg = prot_mem_cgroup[num - 1];
	}

	return memcg;
}

unsigned long get_protected_pages(void)
{
	unsigned long nr_pages = 0;
	int i;

	if (mem_cgroup_disabled())
		return nr_pages;

	for (i = 0; i < PROTECT_LEVELS_MAX; i++) {
		if (prot_mem_cgroup[i])
			nr_pages +=
				protect_memcg_usage(prot_mem_cgroup[i], false);
	}

	return nr_pages;
}

static int protect_switch_handler(struct ctl_table *table, int write,
				  void __user *buffer, size_t *length,
				  loff_t *ppos)
{
	int ret;

	if (mem_cgroup_disabled())
		return -EINVAL;

	mutex_lock(&handler_mutex);
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	mutex_unlock(&handler_mutex);
	return ret;
}

static int protect_reclaim_ratio_handler(struct ctl_table *table, int write,
					 void __user *buffer, size_t *length,
					 loff_t *ppos)
{
	int ret;
	int i;
	unsigned long enable;

	if (mem_cgroup_disabled())
		return -EINVAL;

	mutex_lock(&handler_mutex);
	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (ret || !write)
		goto out;

	enable = protect_lru_enable;
	protect_lru_enable = 0;
	/* Shrink until reach the protect file ratio. */
	while (is_prot_memcg_overratio()) {
		lru_add_drain_all();
		for (i = 0; i < PROTECT_LEVELS_MAX; i++) {
			if (prot_mem_cgroup[i])
				shrink_prot_memcg(prot_mem_cgroup[i]);
		}
		cond_resched();
	}
	protect_lru_enable = enable;
out:
	mutex_unlock(&handler_mutex);
	return ret;
}

static int protect_max_mbytes_handler(struct ctl_table *table, int write,
				      void __user *buffer, size_t *length,
				      loff_t *ppos)
{
	int ret;
	int i;
	unsigned long enable;

	if (mem_cgroup_disabled())
		return -EINVAL;

	mutex_lock(&handler_mutex);
	ret = proc_doulongvec_minmax(table, write, buffer, length, ppos);

	if (ret || !write)
		goto out;

	enable = protect_lru_enable;
	protect_lru_enable = 0;
	for (i = 0; i < PROTECT_LEVELS_MAX; i++) {
		if (!prot_mem_cgroup[i])
			continue;

		protect_memcg_drain_all_stock(prot_mem_cgroup[i]);
		lru_add_drain_all();
		ret = protect_memcg_resize_limit(
			prot_mem_cgroup[i],
			protect_max_mbytes[i] << PAGES_PER_MBYTES_SHIFT);

		if (ret)
			break;
	}

	for (; i < PROTECT_LEVELS_MAX; i++)
		protect_max_mbytes[i] = prot_mem_cgroup[i]->memory.max >>
					PAGES_PER_MBYTES_SHIFT;

	protect_lru_enable = enable;
out:
	mutex_unlock(&handler_mutex);
	return ret;
}

static int protect_cur_mbytes_handler(struct ctl_table *table, int write,
				      void __user *buffer, size_t *length,
				      loff_t *ppos)
{
	int i;

	if (mem_cgroup_disabled())
		return -EINVAL;

	mutex_lock(&handler_mutex);
	for (i = 0; i < PROTECT_LEVELS_MAX; i++) {
		if (prot_mem_cgroup[i]) {
			protect_cur_mbytes[i] =
				protect_memcg_usage(prot_mem_cgroup[i], false);
			protect_cur_mbytes[i] >>= PAGES_PER_MBYTES_SHIFT;
		} else {
			protect_cur_mbytes[i] = 0;
		}
	}
	mutex_unlock(&handler_mutex);

	return proc_doulongvec_minmax(table, write, buffer, length, ppos);
}
// clang-format off
/*lint -e545*/
struct ctl_table protect_lru_table[] = {
	{
		.procname = "protect_max_mbytes",
		.data = &protect_max_mbytes,
		.maxlen = sizeof(protect_max_mbytes),
		.mode = 0640,
		.proc_handler = protect_max_mbytes_handler,
		.extra1 = &sysctl_ulong_zero,
	},
	{
		.procname = "protect_cur_mbytes",
		.data = &protect_cur_mbytes,
		.maxlen = sizeof(protect_cur_mbytes),
		.mode = 0440,
		.proc_handler = protect_cur_mbytes_handler,
	},
	{
		.procname = "protect_lru_enable",
		.data = &protect_lru_enable,
		.maxlen = sizeof(int),
		.mode = 0640,
		.proc_handler = protect_switch_handler,
		.extra1 = &sysctl_zero,
		.extra2 = &sysctl_one,
	},
	{
		.procname = "protect_reclaim_ratio",
		.data = &protect_reclaim_ratio,
		.maxlen = sizeof(int),
		.mode = 0640,
		.proc_handler = protect_reclaim_ratio_handler,
		.extra1 = &sysctl_one,
		.extra2 = &sysctl_one_hundred,
	},
	{}
};
// clang-format on

/*lint +e545*/

static int protect_lru_set(struct file *filp, int level)
{
	struct inode *inode = file_inode(filp);
#if defined(CONFIG_OVERLAY_FS)
	struct file *real_file = NULL;
#endif

	if (!inode)
		return -EINVAL;

	inode->i_protect = level;
#if defined(CONFIG_OVERLAY_FS)
	real_file = get_real_file(filp);
	inode = file_inode(real_file);
	if (inode)
		inode->i_protect = level;
#endif
	pr_info("prot_lru: %s, %d\n", filp->f_path.dentry->d_name.name, level);

	return 0;
}

static int parse_protect_data(char *start, int *fd, int *level)
{
	int index = 0;
	char *p = start;
	char *str_level = NULL;

	while ((*p) != '\0') {
		if (*p != ' ') {
			p++;
			continue;
		}

		if (index == PROTECT_FD) {
			index++;
			*p = '\0';
			p++;
			while (*p == ' ')
				p++;
			str_level = p;
		} else if (index == PROTECT_LEVEL) {
			index++;
			*p = '\0';
			p++;
			while (*p == ' ')
				p++;
			break;
		}
	}

	if ((index != PROTECT_MAX) || kstrtoint(start, DECIMAL_BASE, fd) ||
	    kstrtoint(str_level, DECIMAL_BASE, level))
		return -EINVAL;
	else
		return 0;
}

/*
 * Format: 'fd level path'
 */
static ssize_t protect_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *ppos)
{
	char proctectData[MAX_LEN] = {0};
	char *start = NULL;
	int ret;
	int fd = -1;
	int level = 0;
	struct fd f;

	if ((count > sizeof(proctectData) - 1) || (count <= 0))
		return -EINVAL;

	if (copy_from_user(proctectData, buffer, count))
		return -EFAULT;

	proctectData[count] = '\0';
	start = strstrip(proctectData);
	if (strlen(start) <= 0)
		return -EINVAL;

	ret = parse_protect_data(start, &fd, &level);
	if (ret)
		return ret;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;

	if (level >= 0 && level <= PROTECT_LEVELS_MAX) {
		ret = protect_lru_set(f.file, level);
	} else {
		pr_err("set protect lru: level is not right\n");
		ret = -EINVAL;
	}
	fdput(f);

	return ret ? ret : count;
}

static const struct proc_ops protect_proc_fops = {
	.proc_write = protect_write,
	.proc_lseek = noop_llseek,
};

static int __init proc_protect_init(void)
{
	mutex_init(&handler_mutex);
	proc_create("protect_lru", 0220, NULL, &protect_proc_fops);
	return 0;
}

fs_initcall(proc_protect_init);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic show_mem() implementation
 *
 * Copyright (C) 2008 Johannes Weiner <hannes@saeurebad.de>
 */

#include <linux/mm.h>
#include <linux/cma.h>
#include <trace/hooks/mm.h>

#ifdef CONFIG_SUNRECLAIM_DBG_PROCNODE
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#endif

#ifdef CONFIG_SUNRECLAIM_DEBUG
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/vmstat.h>
#include "../mm/slab.h"
#define ONE_GB (1UL << 30)
#endif

#ifdef CONFIG_SUNRECLAIM_DBG_PROCNODE
#define PROCNODENAME "driver/sunreclaim_dbg"
#endif

#ifdef CONFIG_SUNRECLAIM_DEBUG
static unsigned long sunreclaim_threshold = ONE_GB;
#endif

void __show_mem(unsigned int filter, nodemask_t *nodemask, int max_zone_idx)
{
	pg_data_t *pgdat;
	unsigned long total = 0, reserved = 0, highmem = 0;
#ifdef CONFIG_SUNRECLAIM_DEBUG
	unsigned long sunreclaim;
#endif
	printk("Mem-Info:\n");
	__show_free_areas(filter, nodemask, max_zone_idx);

	for_each_online_pgdat(pgdat) {
		int zoneid;

		for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {
			struct zone *zone = &pgdat->node_zones[zoneid];
			if (!populated_zone(zone))
				continue;

			total += zone->present_pages;
			reserved += zone->present_pages - zone_managed_pages(zone);

			if (is_highmem_idx(zoneid))
				highmem += zone->present_pages;
		}
	}

	printk("%lu pages RAM\n", total);
	printk("%lu pages HighMem/MovableOnly\n", highmem);
	printk("%lu pages reserved\n", reserved);
#ifdef CONFIG_CMA
	printk("%lu pages cma reserved\n", totalcma_pages);
#endif
#ifdef CONFIG_MEMORY_FAILURE
	printk("%lu pages hwpoisoned\n", atomic_long_read(&num_poisoned_pages));
#endif
	trace_android_vh_show_mem(filter, nodemask);

#ifdef CONFIG_SUNRECLAIM_DEBUG
	sunreclaim = global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B);
	if (sunreclaim*4*1024 > sunreclaim_threshold) {
		struct kmem_cache *s;
		struct slabinfo sinfo;

		if (!mutex_trylock(&slab_mutex)) {
			pr_warn("sunreclaim_dbg: excessive unreclaimable slab but cannot dump stats\n");
			return;
		}

		pr_info("Unreclaimable slab info:\n");
		pr_info("<name>                      <num_objs>          <objsize>\n");

		list_for_each_entry(s, &slab_caches, list) {
			if (s->flags & SLAB_RECLAIM_ACCOUNT)
				continue;

			get_slabinfo(s, &sinfo);

			if (sinfo.num_objs > 0)
				pr_info("%-17s %10lu %10u\n", s->name, sinfo.num_objs, s->size);
		}
		mutex_unlock(&slab_mutex);
	}
#endif
}

EXPORT_SYMBOL_GPL(__show_mem);

#ifdef CONFIG_SUNRECLAIM_DBG_PROCNODE
static ssize_t sunreclaim_dbg_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *data)
{
	int rv;

	rv = kstrtoul_from_user(buffer, count, 10, &sunreclaim_threshold);
	if (rv < 0)
		return rv;

	return (ssize_t)count;
}

static int sunreclaim_dbg_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%lu\n", sunreclaim_threshold);
	return 0;
}

static int sunreclaim_dbg_open(struct inode *inode, struct file *file)
{
	return single_open(file, sunreclaim_dbg_show, NULL);
}

#if (KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE)
static const struct proc_ops sunreclaim_dbg_fops = {
	.proc_open = sunreclaim_dbg_open,
	.proc_write = sunreclaim_dbg_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations sunreclaim_dbg_fops = {
	.owner = THIS_MODULE,
	.open = sunreclaim_dbg_open,
	.write = sunreclaim_dbg_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static int __init sunreclaim_dbg_procfs_init(void)
{
	struct proc_dir_entry *proc_entry_sunreclaim = NULL;

	proc_entry_sunreclaim = proc_create(PROCNODENAME, 0660, NULL, &sunreclaim_dbg_fops);
	if (!proc_entry_sunreclaim) {
		pr_err("can't create /proc/%s\n", PROCNODENAME);
		return -ENOMEM;
	}

	return 0;
}

static void __exit sunreclaim_dbg_procfs_exit(void)
{
	remove_proc_entry(PROCNODENAME, NULL);
}

module_init(sunreclaim_dbg_procfs_init);
module_exit(sunreclaim_dbg_procfs_exit);

MODULE_AUTHOR("Honor Technologies Co., Ltd.");
MODULE_DESCRIPTION("sunreclaim_dbg driver");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
MODULE_IMPORT_NS(MINIDUMP);
#endif
MODULE_LICENSE("GPL v2");
#endif /*CONFIG_SUNRECLAIM_DBG_PROCNODE*/

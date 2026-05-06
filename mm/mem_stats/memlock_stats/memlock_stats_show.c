/*
 * memlock_stats_show.c
 *
 * Show memory lock accounting data
 *
 * Copyright (c) 2020-2020 Honor Device Co., Ltd
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
 */

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <linux/mem_lock_info.h>

static ssize_t memlock_info_stats_write(struct file *file,
					const char __user *buf, size_t count,
					loff_t *ppos)
{
	init_memlock_time_stat(&mmaplock_stat);
	init_memlock_time_stat(&zonelock_stat);
	init_memlock_time_stat(&lrulock_stat);
	return count;
}

static int memlock_info_stats_show(struct seq_file *m, void *v)
{
	unsigned int i;
	seq_printf(m, "%s  %lu  %lu  ", mmaplock_stat.name,
		   mmaplock_stat.count_total,
		   mmaplock_stat.total_time / (mmaplock_stat.count_total ?: 1));
	for (i = 0; i < MEMLOCK_STATS_MAX; i++) {
		seq_printf(m, " %lu", mmaplock_stat.counts[i]);
	}
	seq_printf(m, "\n");
	seq_printf(m, "%s  %lu  %lu  ", zonelock_stat.name,
		   zonelock_stat.count_total,
		   zonelock_stat.total_time / (zonelock_stat.count_total ?: 1));
	for (i = 0; i < MEMLOCK_STATS_MAX; i++) {
		seq_printf(m, " %lu", zonelock_stat.counts[i]);
	}
	seq_printf(m, "\n");
	seq_printf(m, "%s  %lu  %lu  ", lrulock_stat.name,
		   lrulock_stat.count_total,
		   lrulock_stat.total_time / (lrulock_stat.count_total ?: 1));
	for (i = 0; i < MEMLOCK_STATS_MAX; i++) {
		seq_printf(m, " %lu", lrulock_stat.counts[i]);
	}
	seq_printf(m, "\n");

	return 0;
}

static int memlock_info_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, memlock_info_stats_show, NULL);
}

static const struct proc_ops memlock_proc_info_fops = {
	.proc_open = memlock_info_stats_open,
	.proc_write = memlock_info_stats_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init proc_memlock_stats_init(void)
{
	proc_create("memlock_stats_info", 0440, NULL, &memlock_proc_info_fops);
	init_memlock_time_stat(&mmaplock_stat);
	init_memlock_time_stat(&zonelock_stat);
	init_memlock_time_stat(&lrulock_stat);
	return 0;
}
fs_initcall(proc_memlock_stats_init);

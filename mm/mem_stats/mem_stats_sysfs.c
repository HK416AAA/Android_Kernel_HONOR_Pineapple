/*
 * Copyright (c) Honor Device Co., Ltd. 2023. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/migrate.h>

/* rule:
 *   mem_total   free_min
 *      8 GB       8 MB
 *      12GB       12MB
 *      16GB       16MB
 */
#define free_min(nr) ((nr) >> 10)
/* max file_page、anon_page value is (totalram_pages*5/8) */
#define used_max(nr) ((nr)*5 >> 3)

/* if isolated > inactive/32, predicate the value anomal*/
#define ISOLATED_THRESHOLD 5
/* the interval of report buddy info */
#define BUDDY_REPORT_INTERVAL (2 * 60 * 60)
/* the interval of report mem event */
#define REPORT_INTERVAL (8 * 60 * 60)
/* the minimum interval of report event */
#define MIN_REPORT_INTERVAL 10

unsigned long free_page_min;
EXPORT_SYMBOL_GPL(free_page_min);
unsigned long used_page_max;
EXPORT_SYMBOL_GPL(used_page_max);
unsigned long isolated_threshold;
EXPORT_SYMBOL_GPL(isolated_threshold);
unsigned int report_rs_interval;
unsigned int buddy_report_rs_interval;
DEFINE_RATELIMIT_STATE(report_rs, REPORT_INTERVAL *HZ, 1);
EXPORT_SYMBOL_GPL(report_rs);
DEFINE_RATELIMIT_STATE(buddy_report_rs, BUDDY_REPORT_INTERVAL *HZ, 1);
EXPORT_SYMBOL_GPL(buddy_report_rs);

static ssize_t free_page_min_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buffer)
{
	return sysfs_emit(buffer, "%lu\n", free_page_min);
}

static ssize_t free_page_min_store(struct kobject *kobj,
				   struct kobj_attribute *attr, const char *buf,
				   size_t count)
{
	unsigned long min;
	int err;

	err = kstrtoul(buf, 0, &min);
	if (err)
		return err;

	free_page_min = min;
	return count;
}

static ssize_t used_page_max_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buffer)
{
	return sysfs_emit(buffer, "%lu\n", used_page_max);
}

static ssize_t used_page_max_store(struct kobject *kobj,
				   struct kobj_attribute *attr, const char *buf,
				   size_t count)
{
	unsigned long max;
	int err;

	err = kstrtoul(buf, 0, &max);
	if (err)
		return err;

	used_page_max = max;
	return count;
}

static ssize_t isolated_threshold_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *buffer)
{
	return sysfs_emit(buffer, "%lu\n", isolated_threshold);
}

static ssize_t isolated_threshold_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long threshold;
	int err;

	err = kstrtoul(buf, 0, &threshold);
	if (err)
		return err;

	isolated_threshold = threshold;
	return count;
}

static ssize_t report_rs_interval_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *buffer)
{
	return sysfs_emit(buffer, "%u\n", report_rs_interval);
}

static ssize_t report_rs_interval_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long interval;
	int err;

	err = kstrtoul(buf, 0, &interval);
	if (err)
		return err;

	if (interval >= MIN_REPORT_INTERVAL) {
		report_rs_interval = (unsigned int)interval;
		report_rs.interval = (unsigned int)interval * HZ;
	}

	return count;
}

static ssize_t buddy_report_rs_interval_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buffer)
{
	return sysfs_emit(buffer, "%u\n", buddy_report_rs_interval);
}

static ssize_t buddy_report_rs_interval_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	unsigned long interval;
	int err;

	err = kstrtoul(buf, 0, &interval);
	if (err)
		return err;

	if (interval >= MIN_REPORT_INTERVAL) {
		buddy_report_rs_interval = (unsigned int)interval;
		buddy_report_rs.interval = (unsigned int)interval * HZ;
	}

	return count;
}

static struct kobj_attribute free_page_min_attr = __ATTR_RW(free_page_min);
static struct kobj_attribute used_page_max_attr = __ATTR_RW(used_page_max);
static struct kobj_attribute isolated_threshold_attr =
	__ATTR_RW(isolated_threshold);
static struct kobj_attribute report_rs_interval_attr =
	__ATTR_RW(report_rs_interval);
static struct kobj_attribute buddy_report_rs_interval_attr =
	__ATTR_RW(buddy_report_rs_interval);

static struct attribute *mem_stats_attr[] = {
	&free_page_min_attr.attr,
	&used_page_max_attr.attr,
	&isolated_threshold_attr.attr,
	&report_rs_interval_attr.attr,
	&buddy_report_rs_interval_attr.attr,
	NULL,
};

static const struct attribute_group mem_stats_group = {
	.attrs = mem_stats_attr,
};

static int __init mem_stats_init_sysfs(void)
{
	int err;
	unsigned long nr_total;
	struct kobject *mem_stats_kobj;

	nr_total = totalram_pages();
	free_page_min = free_min(nr_total);
	used_page_max = used_max(nr_total);
	isolated_threshold = ISOLATED_THRESHOLD;
	report_rs_interval = report_rs.interval / HZ;
	buddy_report_rs_interval = buddy_report_rs.interval / HZ;

	mem_stats_kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
	if (!mem_stats_kobj) {
		return -ENOMEM;
	}

	mem_stats_kobj = kobject_create_and_add("mem_stats", mm_kobj);
	if (unlikely(!mem_stats_kobj)) {
		pr_err("failed to create mem_stats kobject\n");
		goto kobject_init_and_add_fail;
	}

	err = sysfs_create_group(mem_stats_kobj, &mem_stats_group);
	if (err) {
		pr_err("failed to register mem_stats group\n");
		goto kobject_init_and_add_fail;
	}

	return 0;

kobject_init_and_add_fail:
	kobject_put(mem_stats_kobj);
	return -ENOMEM;
}
subsys_initcall(mem_stats_init_sysfs);
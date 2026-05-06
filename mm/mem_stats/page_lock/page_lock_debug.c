/*
 * page_lock_debug.c
 *
 * Copyright(C) 2022 Honor Device Co., Ltd. All rights reserved.
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
 */

#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/memblock.h>
#include <linux/stacktrace.h>
#include <linux/jump_label.h>
#include <linux/migrate.h>
#include <linux/stackdepot.h>
#include <linux/seq_file.h>
#include <linux/sched/clock.h>
#include <linux/page_lock_debug.h>

#include "../../mm/internal.h"

#define PAGE_LOCK_STACK_DEPTH 16

struct page_lock_info {
	struct task_struct *owner;
	u64 ts_nsec;
	depot_stack_handle_t handle;
};

static bool need_page_lock_info(void)
{
	return true;
}

static void init_page_lock_info(void)
{
}

struct page_ext_operations page_lock_info_ops = {
	.size = sizeof(struct page_lock_info),
	.need = need_page_lock_info,
	.init = init_page_lock_info,
};

static inline struct page_lock_info *
get_page_lock_info(struct page_ext *page_ext)
{
	return (void *)page_ext + page_lock_info_ops.offset;
}

static noinline depot_stack_handle_t save_stack(gfp_t flags)
{
	unsigned long entries[PAGE_LOCK_STACK_DEPTH];
	depot_stack_handle_t handle;
	unsigned int nr_entries;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 2);
	handle = stack_depot_save(entries, nr_entries, flags);

	return handle;
}

void __set_page_lock_info(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_lock_info *page_lock_info;

	if (unlikely(!page_ext))
		return;

	page_lock_info = get_page_lock_info(page_ext);
	page_lock_info->owner = current;
	page_lock_info->ts_nsec = local_clock();

	if (unlikely(in_interrupt() || irqs_disabled() || in_atomic()))
		page_lock_info->handle = save_stack(GFP_ATOMIC | __GFP_NOWARN);
	else
		page_lock_info->handle = save_stack(GFP_NOWAIT | __GFP_NOWARN);
	__set_bit(PAGE_EXT_LOCK_DEBUG, &page_ext->flags);

	page_ext_put(page_ext);
}
EXPORT_SYMBOL_GPL(__set_page_lock_info);

void __reset_page_lock_info(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_lock_info *page_lock_info;

	if (unlikely(!page_ext))
		return;

	page_lock_info = get_page_lock_info(page_ext);
	__clear_bit(PAGE_EXT_LOCK_DEBUG, &page_ext->flags);
	page_lock_info->owner = NULL;
	page_lock_info->ts_nsec = 0;

	page_ext_put(page_ext);
}
EXPORT_SYMBOL_GPL(__reset_page_lock_info);

static ssize_t print_page_lock_info(char __user *buf, size_t count,
				    unsigned long pfn, struct page *page,
				    struct page_lock_info *page_lock_info)
{
	int ret;
	char *kbuf;
	depot_stack_handle_t handle;

	count = min_t(size_t, count, PAGE_SIZE);
	kbuf = kmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = snprintf(kbuf, count,
		       "Page pfn 0x%lx,  pid %d, name %s, lock ts %llu us\n",
		       page_to_pfn(page), page_lock_info->owner->pid,
		       page_lock_info->owner->comm,
		       (local_clock() - page_lock_info->ts_nsec) / 1000);

	handle = READ_ONCE(page_lock_info->handle);
	if (!handle) {
		pr_info("page lock allocation stack trace missing\n");
	} else {
		unsigned long *entries;
		unsigned int nr_entries;

		nr_entries = stack_depot_fetch(handle, &entries);
		ret += stack_trace_snprint(kbuf + ret, count - ret, entries,
					   nr_entries, 0);
	}

	if (ret >= count)
		goto err;

	if (copy_to_user(buf, kbuf, ret))
		ret = -EFAULT;

	kfree(kbuf);
	return ret;

err:
	kfree(kbuf);
	return -ENOMEM;
}

static ssize_t read_page_lock_debug(struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	unsigned long pfn;
	struct page *page;
	struct page_ext *page_ext;
	struct page_lock_info *page_lock_info;

	page = NULL;
	pfn = min_low_pfn + *ppos;

	/* Find a valid PFN or the start of a MAX_ORDER_NR_PAGES area */
	while (!pfn_valid(pfn) && (pfn & (MAX_ORDER_NR_PAGES - 1)) != 0)
		pfn++;

	drain_all_pages(NULL);

	/* Find an allocated page */
	for (; pfn < max_pfn; pfn++) {
		struct page_lock_info page_lock_info_tmp;

		/*
		 * If the new page is in a new MAX_ORDER_NR_PAGES area,
		 * validate the area as existing, skip it if not
		 */
		if ((pfn & (MAX_ORDER_NR_PAGES - 1)) == 0 && !pfn_valid(pfn)) {
			pfn += MAX_ORDER_NR_PAGES - 1;
			continue;
		}

		page = pfn_to_page(pfn);
		if (PageBuddy(page)) {
			unsigned long freepage_order = buddy_order_unsafe(page);

			if (freepage_order < MAX_ORDER)
				pfn += (1UL << freepage_order) - 1;
			continue;
		}

		page_ext = page_ext_get(page);
		if (unlikely(!page_ext))
			continue;

		/*
		 * Some pages could be missed by concurrent allocation or free,
		 * because we don't hold the zone lock.
		 */
		if (!test_bit(PAGE_EXT_LOCK_DEBUG, &page_ext->flags))
			goto ext_put_continue;

		page_lock_info = get_page_lock_info(page_ext);

		/* Record the next PFN to read in the file offset */
		*ppos = (pfn - min_low_pfn) + 1;

		page_lock_info_tmp = *page_lock_info;
		page_ext_put(page_ext);
		return print_page_lock_info(buf, count, pfn, page,
					    &page_lock_info_tmp);
ext_put_continue:
		page_ext_put(page_ext);
	}
	return 0;
}

static const struct file_operations proc_page_lock_debug_operations = {
	.read = read_page_lock_debug,
};

static int __init page_lock_debug_init(void)
{
	debugfs_create_file("page_lock_debug", 0400, NULL, NULL,
			    &proc_page_lock_debug_operations);

	return 0;
}

late_initcall(page_lock_debug_init)

	MODULE_AUTHOR("Honor Technologies Co., Ltd.");
MODULE_DESCRIPTION("page lock debug");
MODULE_LICENSE("GPL v2");
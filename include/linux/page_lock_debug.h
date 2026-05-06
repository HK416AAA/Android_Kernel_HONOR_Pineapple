/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGE_LOCK_DEBUG_H
#define __LINUX_PAGE_LOCK_DEBUG_H

#ifdef CONFIG_DEBUG_PAGE_LOCK
extern struct page_ext_operations page_lock_info_ops;

extern void __reset_page_lock_info(struct page *page);
extern void __set_page_lock_info(struct page *page);

static inline void reset_page_lock_info(struct page *page)
{
	__reset_page_lock_info(page);
}

static inline void set_page_lock_info(struct page *page)
{
	__set_page_lock_info(page);
}
#else
static inline void reset_page_lock_info(struct page *page)
{
}

static inline void set_page_lock_info(struct page *page)
{
}
#endif /* CONFIG_DEBUG_PAGE_LOCK */
#endif /* __LINUX_PAGE_LOCK_DEBUG_H */

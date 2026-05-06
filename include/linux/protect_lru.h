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
 * Provide external call interfaces of protect_lru
 */
#ifndef PROTECT_LRU_H
#define PROTECT_LRU_H

#if defined(CONFIG_MEMCG_PROTECT_LRU)
#include <linux/sysctl.h>
#include <linux/memcontrol.h>
#include <linux/fs.h>
#include <linux/version.h>

extern struct ctl_table protect_lru_table[];
extern unsigned long protect_lru_enable __read_mostly;

struct mem_cgroup *get_protect_memcg(struct folio *folio,
				     struct mem_cgroup **memcgp);
struct mem_cgroup *get_protect_file_memcg(struct folio *folio,
					  struct address_space *mapping);
unsigned long get_protected_pages(void);
int protect_memcg_css_online(struct cgroup_subsys_state *css,
			     struct mem_cgroup *memcg);
void protect_memcg_css_offline(struct mem_cgroup *memcg);
void shrink_prot_memcg_by_overlimit(struct mem_cgroup *memcg);
void shrink_prot_memcg_by_overratio(void);
int shrink_prot_memcg(struct mem_cgroup *memcg);
int is_prot_memcg(struct mem_cgroup *memcg, bool boot);

static inline bool is_valid_protect_level(int num)
{
	return (num > 0) && (num <= PROTECT_LEVELS_MAX);
}

static inline void protect_add_page_cache_rollback(struct folio *folio)
{
	if (folio_test_protect(folio))
		folio_clear_protect(folio);
}
#endif

#endif

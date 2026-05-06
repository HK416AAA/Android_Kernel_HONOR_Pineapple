/*
 * Copyright (c) Honor Technologies Co., Ltd. 2022. All rights reserved.
 * Description: hyperhold gki implement
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
 * Author:	Lin Kunli <linkunli.>
 * 		Wang Xin <wangxin23.>
 * 		Yi Pengxiang <yipengxiang.>
 * 		Tian xiaobin <tianxiaobin.>
 *
 * Create: 2022-7-12
 *
 */

#ifdef CONFIG_HYPERHOLD_GKI
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/hyperhold_inf.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/vmscan.h>

#include "hyperhold_internal.h"
#include "hyperhold_area.h"
#include "hyperhold_list.h"
#include "hyperhold_gki_memcg_control.h"
#include "hyperhold.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
static unsigned long mcgid2mcg[MEM_CGROUP_ID_MAX] = {0};
spinlock_t mcg_alloc_lock;
#endif

/* memcg score head */
enum score_head_init_status { SCORE_HEAD_NOT_INITED, SCORE_HEAD_INITED };
static atomic_t gki_score_head_inited = ATOMIC_INIT(0);
static struct list_head gki_score_head;
static DEFINE_SPINLOCK(gki_score_list_lock);
static struct workqueue_struct *hyperhold_gki_memcgext_workqueue;
struct memcg_ext_release_req {
	struct delayed_work delayed_work;
	unsigned int interval;
	struct mem_cgroup_ext *mcg_ext;
};

enum scan_balance {
	SCAN_EQUAL,
	SCAN_FRACT,
	SCAN_ANON,
	SCAN_FILE,
};

#ifdef CONFIG_HYPERHOLD_GKI_DEBUG
static atomic_t hyperhold_gki_memcg_id = ATOMIC_INIT(0);
#define HYPERHOLD_ESWAP_STAT_GKI_ITEMS 1
#endif

static void unregister_memcg_cgroup_data_hook(struct mem_cgroup *memcg)
{
	if (!memcg)
		return;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	memcg->android_oem_data1[0] = 0;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	memcg->android_vendor_data1 = 0;
#else
	memcg->android_oem_data1 = 0;
#endif
}

static int register_mem_cgroup_data_hook(struct mem_cgroup *memcg,
					 struct mem_cgroup_ext *memcg_ext)
{
	unsigned short memcg_id = 0;
	if (!memcg || !memcg_ext)
		return -EINVAL;

	memcg_id = memcg_id_from_memcg(memcg);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	if (memcg->android_oem_data1[0]) {
		hh_print(HHLOG_ERR, "memcg:%d hook is not null on register\n",
			 memcg_id);
		return -EINVAL;
	}
	memcg->android_oem_data1[0] = (u64)memcg_ext;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	if (memcg->android_vendor_data1) {
		hh_print(HHLOG_ERR, "memcg:%d hook is not null on register\n",
			 memcg_id);
		return -EINVAL;
	}
	memcg->android_vendor_data1 = (u64)memcg_ext;
#else
	if (memcg->android_oem_data1) {
		hh_print(HHLOG_ERR, "memcg:%d hook is not null on register\n",
			 memcg_id);
		return -EINVAL;
	}
	memcg->android_oem_data1 = (u64)memcg_ext;
#endif
	return 0;
}

/* functions between memcg memcg_id memcg_ext memcg_ext_id */
struct mem_cgroup *memcg_from_memcg_id(unsigned short memcg_id)
{
	struct mem_cgroup *memcg = NULL;
	if (memcg_id > 0 && memcg_id < MEM_CGROUP_ID_MAX) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
		spin_lock(&mcg_alloc_lock);
		memcg = (struct mem_cgroup *)mcgid2mcg[memcg_id];
		spin_unlock(&mcg_alloc_lock);
#else
		memcg = mem_cgroup_from_id(memcg_id);
#endif
	}
	return memcg;
}

struct mem_cgroup_ext *memcg_ext_from_memcg_ext_id(unsigned int memcg_ext_id)
{
	struct hyperhold_area *area = NULL;
	struct zram *zram = global_settings.zram;
	struct mem_cgroup_ext *memcg_ext = NULL;

	if (!zram) {
		hh_print(HHLOG_ERR, "zram is NULL\n");
		goto done;
	}

	area = zram->area;
	if (!area) {
		hh_print(HHLOG_ERR, "area in zram is null\n");
		goto done;
	}

	if (memcg_ext_id > 0 && memcg_ext_id < HH_MEM_CGROUP_EXT_ID_MAX) {
		spin_lock(&(area->mcg_ext_lock));
		if (test_bit(memcg_ext_id, area->mcg_ext_bitmap)) {
			memcg_ext = (struct mem_cgroup_ext *)
					    area->mcg_ext_rmap[memcg_ext_id];
		}
		spin_unlock(&(area->mcg_ext_lock));
	}
done:
	return memcg_ext;
}

struct mem_cgroup *memcg_from_memcg_ext(struct mem_cgroup_ext *memcg_ext)
{
	struct mem_cgroup *memcg = NULL;

	if (memcg_ext)
		memcg = memcg_ext->sys_memcg;
	return memcg;
}

struct mem_cgroup *memcg_from_memcg_ext_id(unsigned int memcg_ext_id)
{
	struct mem_cgroup_ext *memcg_ext = NULL;
	struct mem_cgroup *memcg = NULL;

	memcg_ext = memcg_ext_from_memcg_ext_id(memcg_ext_id);
	memcg = memcg_from_memcg_ext(memcg_ext);

	return memcg;
}

unsigned short memcg_id_from_memcg(struct mem_cgroup *memcg)
{
	if (memcg)
		return memcg->id.id;
	else
		return 0;
}

unsigned short memcg_id_from_memcg_ext(struct mem_cgroup_ext *memcg_ext)
{
	if (!memcg_ext || !memcg_ext->sys_memcg)
		return 0;
	return memcg_ext->sys_memcg->id.id;
}

unsigned short memcg_id_from_memcg_ext_id(unsigned int memcg_ext_id)
{
	struct mem_cgroup_ext *memcg_ext = NULL;

	memcg_ext = memcg_ext_from_memcg_ext_id(memcg_ext_id);

	if (!memcg_ext)
		return 0;
	return memcg_ext->sys_memcg->id.id;
}

struct mem_cgroup_ext *memcg_ext_from_memcg(struct mem_cgroup *memcg)
{
	struct mem_cgroup_ext *memcg_ext = NULL;

	if (!memcg)
		return NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	memcg_ext = (struct mem_cgroup_ext *)memcg->android_oem_data1[0];
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	memcg_ext = (struct mem_cgroup_ext *)memcg->android_vendor_data1;
#else
	memcg_ext = (struct mem_cgroup_ext *)memcg->android_oem_data1;
#endif
	return memcg_ext;
}

struct mem_cgroup_ext *memcg_ext_from_memcg_id(unsigned short memcg_id)
{
	struct mem_cgroup_ext *memcg_ext = NULL;
	struct mem_cgroup *memcg = NULL;

	memcg = memcg_from_memcg_id(memcg_id);
	memcg_ext = memcg_ext_from_memcg(memcg);

	return memcg_ext;
}

unsigned int memcg_ext_id_from_memcg(struct mem_cgroup *memcg)
{
	struct mem_cgroup_ext *memcg_ext = NULL;

	if (!memcg)
		return 0;

	memcg_ext = memcg_ext_from_memcg(memcg);

	if (!memcg_ext)
		return 0;
	return memcg_ext->mcg_ext_id;
}

unsigned int memcg_ext_id_from_memcg_id(unsigned short memcg_id)
{
	struct mem_cgroup *memcg = NULL;

	memcg = memcg_from_memcg_id(memcg_id);

	return memcg_ext_id_from_memcg(memcg);
}

unsigned int memcg_ext_id_from_memcg_ext(struct mem_cgroup_ext *memcg_ext)
{
	if (!memcg_ext)
		return 0;
	return memcg_ext->mcg_ext_id;
}
/* end of memcg memcg_id memcg_ext memcg_ext_id convert*/

bool hyperhold_memcg_anon_is_small(struct mem_cgroup *memcg)
{
	bool ret = true;

	if (!memcg)
		return true;

	if (hyperhold_get_memcg_size(memcg, HH_ANON) <
	    MEMCG_SMALL_ANON_THRESHOLD)
		ret = true;
	else
		ret = false;

	return ret;
}

int hyperhold_get_memcg_score(struct mem_cgroup_ext *memcg_ext)
{
	if (!memcg_ext)
		return -EINVAL;
	return atomic_read(&memcg_ext->memcg_reclaimed.app_score);
}

unsigned long hyperhold_get_memcg_size(struct mem_cgroup *memcg,
				       enum hh_mem_type type)
{
	unsigned long sz_inactive_anon = 0;
	unsigned long sz_active_anon = 0;
	unsigned long sz_inactive_file = 0;
	unsigned long sz_active_file = 0;
	unsigned long sz_unevictable = 0;
	unsigned long total_sz = 0;

	if (!memcg)
		return 0;

	if (type >= HH_INACTIVE_ANON)
		sz_inactive_anon = hyperhold_memcg_page_state_local(
			memcg, NR_LRU_BASE + LRU_INACTIVE_ANON);
	if (type >= HH_ACTIVE_ANON)
		sz_active_anon = hyperhold_memcg_page_state_local(
			memcg, NR_LRU_BASE + LRU_ACTIVE_ANON);
	if (type >= HH_INACTIVE_FILE)
		sz_inactive_file = hyperhold_memcg_page_state_local(
			memcg, NR_LRU_BASE + LRU_INACTIVE_FILE);
	if (type >= HH_ACTIVE_FILE)
		sz_active_file = hyperhold_memcg_page_state_local(
			memcg, NR_LRU_BASE + LRU_ACTIVE_FILE);
	if (type >= HH_UNEVICTABLE)
		sz_unevictable = hyperhold_memcg_page_state_local(
			memcg, NR_LRU_BASE + LRU_UNEVICTABLE);

	total_sz = sz_inactive_anon + sz_active_anon + sz_inactive_file +
		   sz_active_file + sz_unevictable;

	return total_sz;
}

static void tune_hyperhold_scan_type_callback(void *ignore,
					      enum scan_balance *scan_balance)
{
	struct hyperhold_area *area = NULL;

	if (unlikely(!global_settings.zram))
		return;

	area = global_settings.zram->area;
	if (unlikely(!area))
		return;

	if (unlikely(current->pid == area->shrink_pid)) {
		*scan_balance = SCAN_ANON;
		return;
	}

	if (unlikely(current->pid == hyperhold_gki_zswapd_pid_get()))
		*scan_balance = SCAN_ANON;
	return;
}

static inline void memcg_ext_inner_init(void)
{
	unsigned long flags;
	if (SCORE_HEAD_INITED == atomic_read(&gki_score_head_inited))
		return;

	hyperhold_gki_memcgext_workqueue =
		create_workqueue("proc_memcgext_destroy");
	if (unlikely(!hyperhold_gki_memcgext_workqueue)) {
		return;
	}

	spin_lock_irqsave(&gki_score_list_lock, flags);
	INIT_LIST_HEAD(&gki_score_head);
	atomic_set(&gki_score_head_inited, SCORE_HEAD_INITED);
	spin_unlock_irqrestore(&gki_score_list_lock, flags);
}

bool mem_cgroup_ext_check(struct mem_cgroup_ext *mcg_ext)
{
	int id = 0;
	if (!mcg_ext)
		return false;
	id = memcg_ext_id_from_memcg_ext(mcg_ext);
	return id > 0 && id <= HH_MEM_CGROUP_EXT_ID_MAX;
}

struct mem_cgroup_ext *get_next_memcg_ext(struct mem_cgroup_ext *mcg_ext_prev)
{
	struct mem_cgroup_ext *memcg_ext = NULL;
	struct mem_cgroup *memcg = NULL;
	struct list_head *pos = NULL;
	unsigned long flags;

	if (SCORE_HEAD_NOT_INITED == atomic_read(&gki_score_head_inited))
		return memcg_ext;

	spin_lock_irqsave(&gki_score_list_lock, flags);

	if (unlikely(!mcg_ext_prev))
		pos = &gki_score_head;
	else
		pos = &mcg_ext_prev->score_node;

	if (list_empty(pos)) /* deleted node */
		goto unlock;

	if (pos->next == &gki_score_head)
		goto unlock;

	memcg_ext = list_entry(pos->next, struct mem_cgroup_ext, score_node);
	memcg = memcg_from_memcg_ext(memcg_ext);
	if (!memcg || !css_tryget_online(&memcg->css))
		memcg_ext = NULL;

unlock:
	spin_unlock_irqrestore(&gki_score_list_lock, flags);
	memcg = memcg_from_memcg_ext(mcg_ext_prev);
	if (memcg)
		css_put(&memcg->css);

	return memcg_ext;
}

void get_next_memcg_ext_break(struct mem_cgroup_ext *memcg_ext)
{
	struct mem_cgroup *memcg = NULL;

	memcg = memcg_from_memcg_ext(memcg_ext);

	if (memcg_ext && memcg)
		css_put(&memcg->css);
}

void memcg_app_score_sort_ext(struct mem_cgroup_ext *target)
{
	struct list_head *pos = NULL;
	unsigned long flags;

	if (SCORE_HEAD_NOT_INITED == atomic_read(&gki_score_head_inited))
		return;

	spin_lock_irqsave(&gki_score_list_lock, flags);
	list_for_each(pos, &gki_score_head) {
		struct mem_cgroup_ext *memcg_ext =
			list_entry(pos, struct mem_cgroup_ext, score_node);
		if (atomic_read(&memcg_ext->memcg_reclaimed.app_score) <
		    atomic_read(&target->memcg_reclaimed.app_score))
			break;
	}
	list_move_tail(&target->score_node, pos);
	spin_unlock_irqrestore(&gki_score_list_lock, flags);
}

static inline void memcg_app_score_list_del(struct mem_cgroup_ext *target)
{
	unsigned long flags;
	spin_lock_irqsave(&gki_score_list_lock, flags);
	list_del_init(&target->score_node);
	spin_unlock_irqrestore(&gki_score_list_lock, flags);
}

static void memcg_ext_release_work(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct memcg_ext_release_req *rq;
	struct mem_cgroup_ext *mcg_ext;
	struct mem_cgroup *mcg;
	struct zram *zram;
	bool zram_lru_empty;
	bool ext_lru_empty;
	unsigned int memcg_ext_id = 0;
	unsigned int max_interval = 3600000;

	hh_print(HHLOG_INFO, "memcg_ext_release_work enter\n");
	if (!work)
		return;
	delayed_work = to_delayed_work(work);
	rq = container_of(delayed_work, struct memcg_ext_release_req,
			  delayed_work);
	if (!rq || !rq->mcg_ext)
		return;
	mcg_ext = rq->mcg_ext;
	mcg = memcg_from_memcg_ext(mcg_ext);
	zram = mcg_ext->zram;
	memcg_ext_id = memcg_ext_id_from_memcg_ext(mcg_ext);

	if (!mcg || !mcg_ext)
		goto free_rq;

	if (atomic_read(&mcg_ext->valid) == 0)
		goto free_rq;

	if (!zram)
		goto out;

	if (!zram->area || !zram->area->obj_table || !zram->area->ext_table)
		goto out;

	/* wait here until memcg has no data in zram_lru and ext_lru, cause there
	 * is no way to notify swap layer to modify swap_entry_t if we release
	 * zram_lru and ext_lru.
	*/
	zram_lru_empty = hh_list_empty(mcg_idx(zram->area, memcg_ext_id),
				       zram->area->obj_table);
	ext_lru_empty = hh_list_empty(mcg_idx(zram->area, memcg_ext_id),
				      zram->area->ext_table);
	if (!zram_lru_empty || !ext_lru_empty) {
		hh_print(HHLOG_DEBUG,
			 "wait release, memcg name:%s,memcg_ext:%d,mcgid:%d\n",
			 mcg_ext->name, memcg_ext_id, mcg_ext->memcg_id);
		if (unlikely(!hyperhold_gki_memcgext_workqueue))
			goto out;

		rq->interval = rq->interval << 1;
		if (rq->interval >= max_interval) {
			hh_print(HHLOG_ERR, "memcg unrelease %s, mcgid:%d\n",
					mcg_ext->name, mcg_ext->memcg_id);
			goto free_rq;
		}

		queue_delayed_work(hyperhold_gki_memcgext_workqueue,
				   &rq->delayed_work,
				   msecs_to_jiffies(rq->interval));
		return;
	}
	hh_print(HHLOG_DEBUG, "memcg_ext: %d releaed\n", memcg_ext_id);

out:
	if (0 != atomic_read(&mcg_ext->valid)) {
		atomic_set(&mcg_ext->valid, 0);
		css_put(&mcg->css);
		hh_print(HHLOG_DEBUG,
			 "released, memcg name:%s,memcg_ext:%d,mcgid:%d\n",
			 mcg_ext->name, memcg_ext_id, mcg_ext->memcg_id);
	}
free_rq:
	hyperhold_free(rq);
	hh_print(HHLOG_INFO, "memcg_ext_release_work end id %d\n",
		 memcg_ext_id);
}

static void memcg_ext_release(struct mem_cgroup_ext *mcg_ext)
{
	struct memcg_ext_release_req *req;

	if (!hyperhold_gki_memcgext_workqueue)
		return;

	req = hyperhold_malloc(sizeof(struct memcg_ext_release_req), true,
			       false);
	if (unlikely(!req)) {
		req = hyperhold_malloc(sizeof(struct memcg_ext_release_req),
				       true, false);
		if (unlikely(!req)) {
			hh_print(HHLOG_ERR,
				 "alloc memcg_ext_release_req fail!\n");
			return;
		}
	}
	req->mcg_ext = mcg_ext;
	req->interval = 1000;
	hh_print(HHLOG_INFO, "mcg_ext_id %d\n", mcg_ext->mcg_ext_id);
	INIT_DELAYED_WORK(&req->delayed_work, memcg_ext_release_work);
	queue_delayed_work(hyperhold_gki_memcgext_workqueue, &req->delayed_work,
			   msecs_to_jiffies(1));
}

/*
 * the mem_cgroup_ext is created in mem_cgroup_alloc but the mem_cgroup_ext is not
 * added into appscore list in this function. hyperhold gki add the mem_cgroup_ext
 * into appscore list in the function css_online
 * */
#define HHGKI_MEMCG_SKIP 7
static void mem_cgroup_alloc_callback(void *ignore, struct mem_cgroup *mcg)
{
	struct mem_cgroup_ext *mcg_ext = NULL;
	unsigned short memcg_id = 0;
	int err = 0;

	memcg_id = memcg_id_from_memcg(mcg);
	if (memcg_id <= HHGKI_MEMCG_SKIP) {
		hh_print(HHLOG_DEBUG,
			 "hyperhold gki does not track memcg id: %d\n",
			 memcg_id);
		return;
	}

	hh_print(
		HHLOG_DEBUG,
		"alloc and init struct memcg_ext for memcg: %d, but this memcg_ext is not added to appscore\n",
		memcg_id);

	mcg_ext = kzalloc(sizeof(struct mem_cgroup_ext), GFP_ATOMIC);
	if (!mcg_ext) {
		hh_print(HHLOG_ERR,
			 "alloc struct memcg_ext failed for memcg:%d\n",
			 memcg_id);
		return;
	}

	mcg_ext->sys_memcg = mcg;
	INIT_LIST_HEAD(&mcg_ext->score_node);
	spin_lock_init(&mcg_ext->zram_init_lock);
	atomic_set(&mcg_ext->memcg_reclaimed.app_score, 300);
	atomic64_set(&mcg_ext->memcg_reclaimed.ub_ufs2zram_ratio, 100);
	atomic_set(&mcg_ext->memcg_reclaimed.ub_zram2ufs_ratio, 10);
	atomic_set(&mcg_ext->memcg_reclaimed.ub_mem2zram_ratio, 60);
	atomic_set(&mcg_ext->memcg_reclaimed.refault_threshold, 50);
	atomic64_set(&mcg_ext->memcg_reclaimed.nr_can_shrink, 0);
	atomic64_set(&mcg_ext->hyperhold_cold_writeback, 0);
	atomic_set(&mcg_ext->valid, 0);

	err = register_mem_cgroup_data_hook(mcg, mcg_ext);
	if (err < 0) {
		hh_print(HHLOG_ERR,
			 "register mem_cgroup_data_hook for memcg %d error\n",
			 memcg_id);
		kfree(mcg_ext);
		return;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
	spin_lock(&mcg_alloc_lock);
	mcgid2mcg[memcg_id] = (u64)mcg;
	spin_unlock(&mcg_alloc_lock);
#endif

	hh_print(HHLOG_DEBUG, "mem_cgroup_alloc_callback mcg_id=%d", memcg_id);
	return;
}

static void mem_cgroup_css_online_callback(void *ignore,
					   struct cgroup_subsys_state *css,
					   struct mem_cgroup *mcg)
{
	struct mem_cgroup_ext *mcg_ext = memcg_ext_from_memcg(mcg);
	unsigned short memcg_id = memcg_id_from_memcg(mcg);

	if (!mcg_ext) {
		hh_print(HHLOG_DEBUG,
			 "memcg: %d has not assocated with a memcg_ext\n",
			 memcg_id);
		return;
	}
	cgroup_path(css->cgroup, mcg_ext->name, MEM_CGROUP_NAME_MAX_LEN);

	if (css_tryget_online(&mcg->css))
		atomic_set(&mcg_ext->valid, 1);
	else {
		hh_print(HHLOG_ERR,
			 "mem_cgroup_alloc_callback get css failed mcg_id: %d",
			 memcg_id);
		return;
	}

	hh_print(
		HHLOG_DEBUG,
		"mem_cgroup: %s memcg_id: %d get css online, add it to appscore\n",
		mcg_ext->name, memcg_id);

	memcg_app_score_sort_ext(mcg_ext);
	mcg_ext->memcg_id = memcg_id;
	hh_print(HHLOG_INFO, "memcg online: %s, memcg_id:%d, ext_id: %d\n",
		 mcg_ext->name, mcg_ext->memcg_id, mcg_ext->mcg_ext_id);
	return;
}
static void mem_cgroup_css_offline_callback(void *ignore,
					    struct cgroup_subsys_state *css,
					    struct mem_cgroup *mcg)
{
	struct mem_cgroup_ext *memcg_ext = NULL;
	unsigned short memcg_id = 0;
	unsigned int memcg_ext_id = 0;

	if (!mcg)
		return;

	memcg_id = memcg_id_from_memcg(mcg);
	memcg_ext = memcg_ext_from_memcg(mcg);
	if (NULL == memcg_ext) {
		hh_print(HHLOG_DEBUG,
			 "memcg: %d does not assocated with a memcg_ext\n",
			 memcg_id);
		return;
	}

	memcg_ext_id = memcg_ext_id_from_memcg(mcg);

	hh_print(
		HHLOG_DEBUG,
		"mem_cgroup:%d, memcg_ext: %d is offline and the memcg_ext is going to release\n",
		memcg_id, memcg_ext_id);
	memcg_app_score_list_del(memcg_ext);
	memcg_ext_release(memcg_ext);

	hh_print(HHLOG_INFO,
		 "memcg offline: %s, memcg_id:%d, ext_id: %d %px %px\n",
		 memcg_ext->name, memcg_id, memcg_ext_id,
		 &(memcg_ext->score_node), memcg_ext->score_node.next);
	return;
}

static void mem_cgroup_id_remove_callback(void *ignore, struct mem_cgroup *mcg)
{
	unsigned short memcg_id = 0;
	if (!mcg)
		return;
	memcg_id = memcg_id_from_memcg(mcg);

	if (memcg_id > 0 && memcg_id < HHGKI_MEMCG_SKIP)
		return;
	if (memcg_id == 0 || memcg_id > MEM_CGROUP_ID_MAX)
		return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
	spin_lock(&mcg_alloc_lock);
	mcgid2mcg[memcg_id] = 0;
	spin_unlock(&mcg_alloc_lock);
#endif

	hh_print(HHLOG_INFO, "mem_cgroup id: %d removed", memcg_id);
	return;
}

static void mem_cgroup_free_callback(void *ignore, struct mem_cgroup *mcg)
{
	struct zram *zram = NULL;
	struct mem_cgroup_ext *memcg_ext;
	struct hyperhold_area *area;
	unsigned int memcg_ext_id = 0;

	if (!mcg)
		return;
	/*
	 * the memcg id has removed in memcg_cgroup_id_remove before the callback is invocaked,
	 * we can not get the memcg id in this function.
	 */
	memcg_ext = memcg_ext_from_memcg(mcg);
	if (!memcg_ext) {
		hh_print(HHLOG_DEBUG,
			 "memcg is not assocated with a memcg_ext\n");
		return;
	}

	memcg_ext_id = memcg_ext_id_from_memcg_ext(memcg_ext);
	if (memcg_ext_id == 0)
		goto out;

	hh_print(HHLOG_DEBUG, "struct mem_cgroup for: %s is going to freed ",
		 memcg_ext->name);

	hyperhold_mem_cgroup_remove(mcg);

	zram = global_settings.zram;
	if (!zram) {
		hh_print(HHLOG_ERR, "zram is null\n");
		goto out;
	}
	area = zram->area;
	if (!area) {
		hh_print(HHLOG_ERR, "area in zram is null\n");
		goto out;
	}

	spin_lock(&area->mcg_ext_lock);
	test_and_clear_bit(memcg_ext_id, area->mcg_ext_bitmap);
	area->mcg_ext_rmap[memcg_ext_id] = 0;
	spin_unlock(&area->mcg_ext_lock);
	hh_print(HHLOG_INFO, "memcg free: %s, memcg_id:%d, ext_id: %d\n",
		 memcg_ext->name, memcg_ext->memcg_id, memcg_ext_id);

out:
	unregister_memcg_cgroup_data_hook(mcg);
	kfree(memcg_ext);
	return;
}

void memcg_ext_init(void)
{
	memcg_ext_inner_init();
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
	spin_lock_init(&mcg_alloc_lock);
#endif
	register_trace_android_vh_mem_cgroup_alloc(mem_cgroup_alloc_callback,
						   NULL);
	register_trace_android_vh_mem_cgroup_css_online(
		mem_cgroup_css_online_callback, NULL);
	register_trace_android_vh_mem_cgroup_css_offline(
		mem_cgroup_css_offline_callback, NULL);
	register_trace_android_vh_mem_cgroup_id_remove(
		mem_cgroup_id_remove_callback, NULL);
	register_trace_android_vh_mem_cgroup_free(mem_cgroup_free_callback,
						  NULL);
	register_trace_android_vh_tune_scan_type(
		tune_hyperhold_scan_type_callback, NULL);
}

ssize_t hyperhold_memcg_info_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;
	struct list_head *pos = NULL;
	unsigned long flags;
	unsigned short memcg_id = 0;

	if (SCORE_HEAD_NOT_INITED == atomic_read(&gki_score_head_inited))
		return 0;

	spin_lock_irqsave(&gki_score_list_lock, flags);
	list_for_each(pos, &gki_score_head) {
		struct mem_cgroup_ext *memcg_ext =
			list_entry(pos, struct mem_cgroup_ext, score_node);
		if (memcg_ext) {
			memcg_id = memcg_id_from_memcg_ext(memcg_ext);
			size += scnprintf(buf + size, PAGE_SIZE - size,
					  "%s,%d\n", memcg_ext->name, memcg_id);
		}
	}
	spin_unlock_irqrestore(&gki_score_list_lock, flags);

	return size;
}

ssize_t hyperhold_force_swapout_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	int ret;
	unsigned long mcgid;
	unsigned long size;
	struct mem_cgroup *memcg = NULL;

	ret = kstrtoul(buf, 0, &mcgid);
	if (unlikely(ret)) {
		hh_print(HHLOG_ERR, "mcgid is error!\n");
		return -EINVAL;
	}

	if (mcgid == 0 || mcgid > MEM_CGROUP_ID_MAX) {
		hh_print(HHLOG_ERR, "memcgid: %lu error\n", mcgid);
		return -EINVAL;
	}

	hh_print(HHLOG_INFO, "force swapout mcgid is :%lu\n", mcgid);

	memcg = memcg_from_memcg_id(mcgid);
	if (!memcg || !css_tryget_online(&memcg->css)) {
		hh_print(HHLOG_ERR,
			 "memcg is NULL or tryget failed for memcg_id: %lu\n",
			 mcgid);
		return -EINVAL;
	}

	size = FT_RECLAIM_SZ * MIN_RECLAIM_ZRAM_SZ;
	ret = hyperhold_reclaim_in_memcg(memcg, size);

	css_put(&memcg->css);

	hh_print(HHLOG_INFO, "force swapout memcgid: %lu, size: %lu\n", mcgid,
		 ret);

	return len;
}

ssize_t hyperhold_force_swapin_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	int ret;
	unsigned long mcgid;
	unsigned long size;
	struct mem_cgroup_ext *memcg_ext;
	struct mem_cgroup *memcg;
	const unsigned int ratio = 100;

	ret = kstrtoul(buf, 0, &mcgid);
	if (unlikely(ret)) {
		hh_print(HHLOG_ERR, "mcgid is error!\n");
		return -EINVAL;
	}

	if (unlikely(mcgid == 0 || mcgid > MEM_CGROUP_ID_MAX)) {
		hh_print(HHLOG_ERR, "memcg id is error\n");
		return -EINVAL;
	}

	hh_print(HHLOG_INFO, "force swapin mcgid is :%lu\n", mcgid);
	memcg_ext = memcg_ext_from_memcg_id((unsigned short)mcgid);
	memcg = memcg_from_memcg_id((unsigned short)mcgid);

	if (NULL == memcg_ext) {
		hh_print(HHLOG_ERR, "memcg: %d is not tracked by hyperhold\n",
			 mcgid);
		return -EINVAL;
	}

	size = hyperhold_read_mcg_ext_stats(memcg_ext, MCG_DISK_STORED_SZ);
	size = atomic64_read(&memcg_ext->memcg_reclaimed.ub_ufs2zram_ratio) *
	       size / ratio;
	size = (((size) + EXTENT_SIZE - 1) & (~(EXTENT_SIZE - 1)));

	hyperhold_batch_out(memcg, size, false);
	hh_print(HHLOG_DEBUG, "HHGKI: hyperhold_batch_out ret\n");

	return len;
}

ssize_t hyperhold_force_shrink_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	unsigned short mcgid;
	int ret;
	unsigned long nr_to_reclaim;
	struct zram *zram = global_settings.zram;
	struct hyperhold_area *area = NULL;

	if (!zram)
		return -EINVAL;
	area = zram->area;
	if (!area)
		return -EINVAL;

	if (sscanf(buf, "%hu,%lu", &mcgid, &nr_to_reclaim) != 2) {
		hh_print(HHLOG_ERR, "force_shrink invalid\n");
		return -EINVAL;
	}

	//TODO take mem2zram into consideration
	hh_print(HHLOG_INFO, "force swap mcgid is :%lu\n", mcgid);
	mutex_lock(&area->shrink_lock);
	area->shrink_pid = current->pid;
	ret = hyperhold_mcg_shrink(mcgid, nr_to_reclaim / PAGE_SIZE);
	mutex_unlock(&area->shrink_lock);

	if (unlikely(!ret))
		hh_print(HHLOG_ERR, "force swap mcgid:%lu error!\n", mcgid);
	else
		hh_print(HHLOG_INFO, "force swap mcgid:%lu success!\n", mcgid);

	return len;
}

#ifdef CONFIG_HYPERHOLD_GKI_DEBUG
ssize_t hyperhold_memcg_stat_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	int mcg_id;

	if (sscanf(buf, "%d", &mcg_id) != HYPERHOLD_ESWAP_STAT_GKI_ITEMS) {
		hh_print(HHLOG_ERR, "memcg id invalid\n");
		return -EINVAL;
	}

	atomic_set(&hyperhold_gki_memcg_id, mcg_id);
	return len;
}

ssize_t hyperhold_memcg_stat_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	unsigned long mcg_zram_stored_size;
	unsigned long mcg_disk_stored_size;
	ssize_t size;
	unsigned short mcg_id = 0;
	unsigned int memcg_ext_id = 0;
	struct mem_cgroup_ext *mcg_ext;

	mcg_id = atomic_read(&hyperhold_gki_memcg_id);
	mcg_ext = memcg_ext_from_memcg_id(mcg_id);
	memcg_ext_id = memcg_ext_id_from_memcg_ext(mcg_ext);

	if (!mcg_ext) {
		return scnprintf(buf, PAGE_SIZE, "memcg id invalid: %d\n",
				 mcg_id);
		return 0;
	}

	mcg_zram_stored_size =
		hyperhold_read_mcg_ext_stats(mcg_ext, MCG_ZRAM_STORED_SZ);
	mcg_disk_stored_size =
		hyperhold_read_mcg_ext_stats(mcg_ext, MCG_DISK_STORED_SZ);

	size = scnprintf(buf, PAGE_SIZE,
			 "mcgid:%d ,memcg_ext_id:%d\n"
			 "mcg_zram_stored_size:%lu\n"
			 "mcg_disk_stored_size:%lu\n",
			 mcg_id, memcg_ext_id, mcg_zram_stored_size,
			 mcg_disk_stored_size);
	return size;
}
#endif

ssize_t hyperhold_appscore_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t len)
{
	int mcg_id = 0;
	int appscore = 0;
	struct mem_cgroup_ext *mcg_ext = NULL;
	struct mem_cgroup *mcg = NULL;

	if (sscanf(buf, "%d,%d", &mcg_id, &appscore) !=
	    HYPERHOLD_APPSCORE_ITEMS) {
		hh_print(HHLOG_ERR, "appscore invalid\n");
		return -EINVAL;
	}

	if (mcg_id < 0 || mcg_id > MEM_CGROUP_ID_MAX) {
		hh_print(HHLOG_ERR, "memcg %d is invalid\n", mcg_id);
		return -EINVAL;
	}

	if (appscore > APPSCORE_MAX || appscore < APPSCORE_MIN) {
		hh_print(HHLOG_ERR, "appscore %d invalid\n", appscore);
		return -EINVAL;
	}

	mcg = memcg_from_memcg_id(mcg_id);
	if (!mcg || !css_tryget_online(&mcg->css))
		return -EINVAL;

	mcg_ext = memcg_ext_from_memcg(mcg);
	if (!mcg_ext) {
		css_put(&mcg->css);
		hh_print(HHLOG_ERR, "memcg %d is invalid\n", mcg_id);
		return -EINVAL;
	}

	atomic_set(&mcg_ext->memcg_reclaimed.app_score, appscore);
	memcg_app_score_sort_ext(mcg_ext);
	css_put(&mcg->css);

	return len;
}

ssize_t hyperhold_appscore_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t size = 0;
	unsigned int memcg_ext_id = 0;
	unsigned short memcg_id = 0;
	struct list_head *pos = NULL;
	unsigned long flags;

	if (SCORE_HEAD_NOT_INITED == atomic_read(&gki_score_head_inited))
		return size;

	spin_lock_irqsave(&gki_score_list_lock, flags);
	list_for_each(pos, &gki_score_head) {
		struct mem_cgroup_ext *memcg_ext =
			list_entry(pos, struct mem_cgroup_ext, score_node);
		memcg_ext_id = memcg_ext_id_from_memcg_ext(memcg_ext);
		memcg_id = memcg_id_from_memcg_ext(memcg_ext);
		size += scnprintf(
			buf + size, PAGE_SIZE - size, "%d,%d,%d\n", memcg_id,
			memcg_ext_id,
			atomic_read(&memcg_ext->memcg_reclaimed.app_score));
	}
	spin_unlock_irqrestore(&gki_score_list_lock, flags);

	return size;
}

ssize_t hyperhold_memcg_eswap_info(int type, char *buf, int mcg_id)
{
	unsigned long swap_out_cnt = 0;
	unsigned long swap_out_size = 0;
	unsigned long swap_in_size = 0;
	unsigned long swap_in_cnt = 0;
	unsigned long page_fault_cnt = 0;
	unsigned long cur_eswap_size = 0;
	unsigned long max_eswap_size = 0;
	ssize_t size = 0;
	struct mem_cgroup_ext *mcg_ext = NULL;

	if (type == HYPERHOLD_MEMCG_ESWAP_SINGLE) {
		mcg_ext = memcg_ext_from_memcg_id(mcg_id);
		if (!mcg_ext)
			return 0;
		swap_out_cnt =
			hyperhold_read_mcg_ext_stats(mcg_ext, MCG_SWAPOUT_CNT);
		swap_out_size =
			hyperhold_read_mcg_ext_stats(mcg_ext, MCG_SWAPOUT_SZ);
		swap_in_size =
			hyperhold_read_mcg_ext_stats(mcg_ext, MCG_SWAPIN_SZ);
		swap_in_cnt =
			hyperhold_read_mcg_ext_stats(mcg_ext, MCG_SWAPIN_CNT);
		page_fault_cnt = hyperhold_read_mcg_ext_stats(
			mcg_ext, MCG_DISK_FAULT_CNT);
		cur_eswap_size =
			hyperhold_read_mcg_ext_stats(mcg_ext, MCG_DISK_SPACE);
		max_eswap_size = hyperhold_read_mcg_ext_stats(
			mcg_ext, MCG_DISK_SPACE_PEAK);
	} else if (type == HYPERHOLD_MEMCG_ESWAP_ALL) {
		while ((mcg_ext = get_next_memcg_ext(mcg_ext))) {
			swap_out_cnt += hyperhold_read_mcg_ext_stats(
				mcg_ext, MCG_SWAPOUT_CNT);
			swap_out_size += hyperhold_read_mcg_ext_stats(
				mcg_ext, MCG_SWAPOUT_SZ);
			swap_in_size += hyperhold_read_mcg_ext_stats(
				mcg_ext, MCG_SWAPIN_SZ);
			swap_in_cnt += hyperhold_read_mcg_ext_stats(
				mcg_ext, MCG_SWAPIN_CNT);
			page_fault_cnt += hyperhold_read_mcg_ext_stats(
				mcg_ext, MCG_DISK_FAULT_CNT);
			cur_eswap_size += hyperhold_read_mcg_ext_stats(
				mcg_ext, MCG_DISK_SPACE);
			max_eswap_size += hyperhold_read_mcg_ext_stats(
				mcg_ext, MCG_DISK_SPACE_PEAK);
		}
	} else {
		return scnprintf(buf, PAGE_SIZE, "param error\n");
	}

	size = scnprintf(buf, PAGE_SIZE,
			 "swapOutTotal:%lu\n"
			 "swapOutSize:%lu MB\n"
			 "swapInSize:%lu MB\n"
			 "swapInTotal:%lu\n"
			 "pageInTotal:%lu\n"
			 "swapSizeCur:%lu MB\n"
			 "swapSizeMax:%lu MB\n",
			 swap_out_cnt, swap_out_size / SZ_1M,
			 swap_in_size / SZ_1M, swap_in_cnt, page_fault_cnt,
			 cur_eswap_size / SZ_1M, max_eswap_size / SZ_1M);

	return size;
}

#endif

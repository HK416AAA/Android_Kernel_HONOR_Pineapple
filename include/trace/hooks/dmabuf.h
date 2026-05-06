/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dmabuf

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DMA_BUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DMA_BUF_H

struct dma_buf;

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_ignore_dmabuf_vmap_bounds,
	     TP_PROTO(struct dma_buf *dma_buf, bool *ignore_bounds),
	     TP_ARGS(dma_buf, ignore_bounds));

struct dma_buf_sysfs_entry;
DECLARE_RESTRICTED_HOOK(android_rvh_dma_buf_stats_teardown,
		TP_PROTO(struct dma_buf_sysfs_entry *sysfs_entry, bool *skip_sysfs_release),
		TP_ARGS(sysfs_entry, skip_sysfs_release), 1);

#endif /* _TRACE_HOOK_DMA_BUF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

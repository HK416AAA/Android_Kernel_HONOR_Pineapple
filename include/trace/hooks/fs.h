/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FS_H

#include <trace/hooks/vendor_hooks.h>
#ifdef CONFIG_HONOR_F2FS_DSM
#include <linux/tracepoint.h>
DECLARE_HOOK(android_vh_f2fs_dsm,
	TP_PROTO(char *name, int len),
	TP_ARGS(name, len));

DECLARE_HOOK(android_vh_f2fs_dsm_get,
	TP_PROTO(unsigned long code, char *err_msg),
	TP_ARGS(code, err_msg));
#endif
DECLARE_HOOK(android_vh_ep_create_wakeup_source,
	TP_PROTO(char *name, int len),
	TP_ARGS(name, len));

DECLARE_HOOK(android_vh_timerfd_create,
	TP_PROTO(char *name, int len),
	TP_ARGS(name, len));
#endif /* _TRACE_HOOK_FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

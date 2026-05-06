#ifdef CONFIG_FS_IO_TRACE
#undef TRACE_SYSTEM
#define TRACE_SYSTEM android_fs

#if !defined(_TRACE_ANDROID_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ANDROID_FS_H

#include <linux/fs.h>
#include <linux/tracepoint.h>
#include <trace/events/android_fs_template.h>
#include <linux/printk.h>
#include "securec.h"
DEFINE_EVENT(android_fs_data_start_template, android_fs_dataread_start,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes,
		 pid_t pid, char *pathname, char *command),
	TP_ARGS(inode, offset, bytes, pid, pathname, command));

DEFINE_EVENT(android_fs_data_end_template, android_fs_dataread_end,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes),
	TP_ARGS(inode, offset, bytes));

DEFINE_EVENT(android_fs_data_start_template, android_fs_datawrite_start,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes,
		 pid_t pid, char *pathname, char *command),
	TP_ARGS(inode, offset, bytes, pid, pathname, command));

DEFINE_EVENT(android_fs_data_end_template, android_fs_datawrite_end,
	TP_PROTO(struct inode *inode, loff_t offset, int bytes),
	     TP_ARGS(inode, offset, bytes));

#endif /* _TRACE_ANDROID_FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

#ifndef ANDROID_FSTRACE_GET_PATHNAME
#define ANDROID_FSTRACE_GET_PATHNAME

/* Sizes an on-stack array, so careful if sizing this up ! */
#define MAX_TRACE_PATHBUF_LEN	256

static inline char *
android_fstrace_get_pathname(char *buf, int buflen, struct inode *inode)
{
	char *path;
	struct dentry *d;
	errno_t err;
	/*
	 * d_obtain_alias() will either iput() if it locates an existing
	 * dentry or transfer the reference to the new dentry created.
	 * So get an extra reference here.
	 */
	ihold(inode);
	d = d_obtain_alias(inode);
	if (likely(!IS_ERR(d))) {
		path = dentry_path_raw(d, buf, buflen);
		if (unlikely(IS_ERR(path))) {
			err = strncpy_s(buf, MAX_TRACE_PATHBUF_LEN, "ERROR", sizeof("ERROR"));
			if (err != EOK) {
				printk(KERN_ERR "strncpy_s buf failed\n");
			}
			path = buf;
		}
		dput(d);
	} else {
		err = strncpy_s(buf, MAX_TRACE_PATHBUF_LEN, "ERROR", sizeof("ERROR"));
		if (err != EOK) {
			printk(KERN_ERR "strncpy_s buf failed\n");
		}
		path = buf;
	}
	return path;
}
#endif
#endif

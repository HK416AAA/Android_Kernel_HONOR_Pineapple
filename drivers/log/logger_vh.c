// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Honor Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Logger vendor hook implement module
 * Author:  chenliucan
 * Create:  2024-07-11
 */
#define pr_fmt(fmt) "logger_vh: %s: " fmt, __func__

#include <log/hiview_hievent.h>
#include <log/hwlog_kernel.h>
#include <log/log_usertype.h>

#include <linux/module.h>

#include "securec.h"
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <log/hw_logger.h>
#include <trace/hooks/fuse.h>
#include <uapi/linux/fuse.h>
#include <linux/wait.h>
#include <linux/jhash.h>
#include <../fs/fuse/fuse_i.h>

#define SYSTEM_SERVER_UID 1000

#define alloc_node_and_add(i, suffix)                                 \
	({                                                            \
		struct file_extention_list *ext##i =                  \
			kmalloc(sizeof(*ext##i), GFP_KERNEL);         \
		if (ext##i) {                                         \
			ext##i->name = (char *)suffix;                \
			hash_add(g_file_extention_map, &ext##i->node, \
				 filename_hash(ext##i->name));        \
		}                                                     \
	})

static DEFINE_HASHTABLE(g_file_extention_map, 4);
struct file_extention_list {
	char *name;
	struct hlist_node node;
};

static unsigned int filename_hash(const char *name)
{
	return jhash(name, strlen(name), 0);
}

static bool check_suffix_in_hashmap(const char *suffix)
{
	unsigned int key;
	struct file_extention_list *ext;

	if (!suffix)
		return false;

	key = filename_hash(suffix);
	hash_for_each_possible(g_file_extention_map, ext, node, key) {
		if (!strcmp(suffix, ext->name))
			return true;
	}
	return false;
}

void hievent_queue_request_and_unlock_ogki(void *ignore,
					   wait_queue_head_t *waitq, bool sync)
{
	struct fuse_iqueue *fiq;
	struct fuse_req *req;
	struct fuse_args *args;
	const char *ch;
	char *suffix;
	bool ret;
	int opcode;

	if (!sync || current_uid().val != SYSTEM_SERVER_UID)
		return;

	fiq = container_of(waitq, struct fuse_iqueue, waitq);
	req = list_last_entry(&fiq->pending, struct fuse_req, list);
	args = req->args;
	opcode = req->in.h.opcode;
	if (opcode == FUSE_UNLINK)
		ch = (const char *)args->in_args[0].value;
	else if (opcode == FUSE_RENAME2 || opcode == FUSE_RENAME)
		ch = (const char *)args->in_args[1].value;
	else
		return;

	suffix = strrchr(ch, '.');
	if (!suffix)
		return;
	ret = check_suffix_in_hashmap(suffix);
	if (ret) {
		if (opcode == FUSE_UNLINK)
			file_delete_event(ch);
		else if (opcode == FUSE_RENAME2 || opcode == FUSE_RENAME)
			file_rename_event((const char *)args->in_args[1].value,
					  (const char *)args->in_args[2].value);
	}
}

static void ext_hashmap_init(void)
{
	int i;
	static const char * const media_suffix[] = {
		".jpeg",  ".jpg",   ".png",   ".bmp",	".gif",	  ".heic",
		".heics", ".heif",  ".hif",   ".heifs", ".avif",  ".webp",
		".dng",	  ".raf",   ".nrw",   ".rw2",	".pef",	  ".srw",
		".arw",	  ".tiff",  ".tif",   ".svg",	".svgz",  ".dwg",
		".dxf",	  ".cur",   ".ico",   ".wpng",	".aac",	  ".amr",
		".mp3",	  ".flac",  ".wav",   ".ape",	".rtttl", ".xmf",
		".rtx",	  ".mid",   ".mxmf",  ".midi",	".imy",	  ".3gp",
		".m4a",	  ".ota",   ".ogg",   ".3ga",	".3gpp",  ".ac3",
		".a52",	  ".ac4",   ".m4b",   ".m4p",	".f4a",	  ".f4b",
		".f4p",	  ".isma",  ".m3u",   ".smf",	".mka",	  ".ra",
		".mp2",	  ".mp1",   ".mpa",   ".m4r",	".snd",	  ".m3u8",
		".avi",	  ".mp4",   ".ts",    ".mov",	".mkv",	  ".rmhd",
		".rmvb",  ".rm",    ".webm",  ".m4v",	".m2ts",  ".mts",
		".wrf",	  ".rv",    ".3gpp2", ".3gp2",	".3g2",	  ".divx",
		".mpeg",  ".yt",    ".mpeg2", ".mpv2",	".mp2v",  ".m2v",
		".m2t",	  ".mpeg1", ".mpv1",  ".mp1v",	".m1v",	  ".hndgp",
		".hnhgp",
	};

	for (i = 0; i < ARRAY_SIZE(media_suffix); i++)
		alloc_node_and_add(i, media_suffix[i]);
}
static int __init logger_vh_init(void)
{
	int ret;

	pr_debug("logger vendor hooks init start.\n");
	ret = register_trace_android_vh_queue_request_and_unlock(
		hievent_queue_request_and_unlock_ogki, NULL);
	if (!ret)
		ext_hashmap_init();
	else
		pr_err("hievent_queue_request_and_unlock vh ogki register failed\n");
	pr_debug("logger vendor hooks init end.\n");

	return 0;
}

static void destory_ext_hashtable(void)
{
	struct file_extention_list *ext;
	struct hlist_node *tmp;
	int bkt;

	hash_for_each_safe(g_file_extention_map, bkt, tmp, ext, node) {
		hlist_del(&ext->node);
		kfree(ext);
	}
}

static void __exit logger_vh_exit(void)
{
	destory_ext_hashtable();
	pr_info("logger vendor hook module exit!!!\n");
}

module_init(logger_vh_init);
module_exit(logger_vh_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Logger vendor hook implement module");

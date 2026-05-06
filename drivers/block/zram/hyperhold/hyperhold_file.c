/*
 * Copyright (c) Honor Technologies Co., Ltd. 2020. All rights reserved.
 * Description: hyperhold implement
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
 * Author:	He Biao <hebiao6.>
 *		Wang Cheng Ke <wangchengke2.>
 *		Wang Fa <fa.wang.>
 *
 * Create: 2020-4-16
 *
 */
#ifdef CONFIG_HYPERHOLD_FILE

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/swap.h>
#include <linux/blkdev.h>
#include <linux/statfs.h>
#include <uapi/linux/f2fs.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include "zram_drv.h"
#include "hyperhold_internal.h"
#include "hyperhold_area.h"

#define hp_file_extent_shift_mb(s) ((s) >> (20 - EXTENT_SHIFT))
#define hp_file_convert_mb(s) ((s)*1024 * 1024)
#define hp_file_B_2_GB(s) ((s) >> 30)
#define HP_FILE_ALIGN_SECTION (2 * 1024 * 1024)
#define HP_FILE_ALIGN_SECTION_PAGES (2 * 1024 * 1024 / PAGE_SIZE)
#define HP_FILE_EXPAND_RATIO 80
#define HP_FILE_MIN_FREE_ROM_GB  10

#define HP_BUF (buf + strlen(buf))
#define HP_LEN ((len > strlen(buf)) ? (len - strlen(buf)) : 0)

/*must align 4*256k*/
static atomic64_t hp_total_sz =
	ATOMIC_LONG_INIT(2UL * 1024UL * 1024UL * 1024UL);
static atomic64_t hp_partition_sz =
	ATOMIC_LONG_INIT(2UL * 1024UL * 1024UL * 1024UL);

static unsigned long last_file_expand_time;
static unsigned long file_expand_skip_interval = 1000;
static atomic_t file_expand_ratio = ATOMIC_INIT(HP_FILE_EXPAND_RATIO);
static atomic_t ctl_file_expand_fail = ATOMIC_INIT(0);
static struct workqueue_struct *hyperhold_expand_workqueue;
static atomic64_t file_expand_fail = ATOMIC_INIT(0);
static atomic64_t file_read_io_fail = ATOMIC_INIT(0);
static atomic64_t file_write_io_fail = ATOMIC_INIT(0);
static atomic_t rom_vaild = ATOMIC_INIT(0);
static atomic_t file_pined = ATOMIC_INIT(0);
static atomic_t user_closed = ATOMIC_INIT(0);
static DEFINE_SPINLOCK(fbdev_spin_lock);

inline bool hp_str_eq(const char *buf, const char *target)
{
	if (!buf || !target) {
		hh_print(HHLOG_ERR, "param error!");
		return false;
	}
	return !strncmp(buf, target, strlen(target));
}

inline bool hp_str_get_u64(const char *buf, const char *target, u64 *v)
{
	if (!buf || !target || !v) {
		hh_print(HHLOG_ERR, "param error!");
		return false;
	}
	return !strncmp(buf, target, strlen(target)) &&
	       !kstrtou64(buf + strlen(target), 0, v);
}

inline bool hp_str_get_str(const char *buf, const char *target, char *v,
			   size_t size)
{
	int ret;

	if (!buf || !target || !v) {
		hh_print(HHLOG_ERR, "param error!");
		return false;
	}
	if (hp_str_eq(buf, target)) {
		ret = snprintf(v, size, "%s", buf + strlen(target));
		return ret > 0 && ret < size;
	}
	return false;
}

int hyperhold_set_type(u64 value)
{
	if (value == HP_PARTITION_ARCHIVAL) {
		atomic_set(&global_settings.space_type, HP_PARTITION_ARCHIVAL);
	} else if (value == HP_PARTITION_FILE_ARCHIVAL) {
		atomic_set(&global_settings.space_type,
			   HP_PARTITION_FILE_ARCHIVAL);
	} else {
		hh_print(HHLOG_ERR, "hyperhold space val is error!");
		return -EINVAL;
	}
	return 0;
}

u64 hyperhold_get_total_sz(void)
{
	return atomic64_read(&hp_total_sz);
}

int hyperhold_set_total_sz(u64 size)
{
	/*must align 2*1024*1024*/
	size = hp_file_convert_mb(size);
	size = ALIGN_DOWN(size, HP_FILE_ALIGN_SECTION);

	atomic64_set(&hp_total_sz, size);
	hh_print(HHLOG_ERR, "hyperhold space size: %llu",
		 atomic64_read(&hp_total_sz));
	return 0;
}

u64 hyperhold_get_partition_sz(void)
{
	return atomic64_read(&hp_partition_sz);
}

int hyperhold_set_partition_sz(u64 size)
{
	/*must align 4*256k, f2fs align 2M*/
	size = hp_file_convert_mb(size);
	size = ALIGN_DOWN(size, HP_FILE_ALIGN_SECTION);
	atomic64_set(&hp_partition_sz, size);
	hh_print(HHLOG_DEBUG, "hyperhold space size: %llu",
		 atomic64_read(&hp_partition_sz));
	return 0;
}

bool hyperhold_user_closed(void)
{
	return atomic_read(&user_closed) != 0;
}

void hyperhold_set_user_closed(u64 status)
{
	if (status != 0)
		atomic_set(&user_closed, 1);
}

void hyperhold_free_sis(struct swap_info_struct *sis)
{
	hh_print(HHLOG_INFO, "sis free");

	while (!RB_EMPTY_ROOT(&sis->swap_extent_root)) {
		struct rb_node *rb = sis->swap_extent_root.rb_node;
		struct swap_extent *se =
			rb_entry(rb, struct swap_extent, rb_node);

		rb_erase(rb, &sis->swap_extent_root);
		kfree(se);
	}

	kvfree(sis);
}

void hyperhold_free_sis_all(struct swap_info_struct *sis)
{
	hh_print(HHLOG_INFO, "sis free");

	while (!RB_EMPTY_ROOT(&sis->swap_extent_root)) {
		struct rb_node *rb = sis->swap_extent_root.rb_node;
		struct swap_extent *se =
			rb_entry(rb, struct swap_extent, rb_node);

		rb_erase(rb, &sis->swap_extent_root);
		kfree(se);
	}

	if (sis->swap_file) {
		struct file *filp = sis->swap_file;
		struct address_space *mapping = filp->f_mapping;

		if (mapping->a_ops->swap_deactivate)
			mapping->a_ops->swap_deactivate(filp);
	}
	kvfree(sis);
}

void hyperhold_exit_fbdev(void)
{
	unsigned long flags;
	struct hyperhold_file_bdev *fbdev = &global_settings.fbdev;

	spin_lock_irqsave(&fbdev_spin_lock, flags);

	if (fbdev->sis)
		hyperhold_free_sis_all(fbdev->sis);
	fbdev->sis = NULL;
	fbdev->bdev = NULL;

	if (fbdev->filp) {
		filp_close(fbdev->filp, NULL);
		fbdev->filp = NULL;
	}
	spin_unlock_irqrestore(&fbdev_spin_lock, flags);
	hh_print(HHLOG_ERR, "file fbd exit\n");
}

struct swap_info_struct *hyperhold_alloc_sis(struct file *filp, u64 from,
					     u64 len)
{
	struct swap_info_struct *sis = NULL;
	struct address_space *mapping = NULL;
	struct inode *inode = NULL;
	unsigned long nr_pages;
	sector_t span;
	int ret;
	struct kstatfs statfs_buf;
	unsigned long long rom_free;

	if (!!atomic_read(&ctl_file_expand_fail)) {
		hh_print(HHLOG_ERR, "ctl expand fail");
		return ERR_PTR(-EINVAL);
	}
	mapping = filp->f_mapping;
	if (!mapping) {
		hh_print(HHLOG_ERR, "mapping err");
		return ERR_PTR(-EINVAL);
	}
	inode = mapping->host;
	if (!inode || !inode->i_fop->fallocate ||
	    !mapping->a_ops->swap_activate) {
		hh_print(HHLOG_ERR, "not support swap_activate");
		return ERR_PTR(-EINVAL);
	}

	if (!filp->f_path.dentry || !inode->i_sb->s_op->statfs) {
		hh_print(HHLOG_ERR, "not support statfs");
		return ERR_PTR(-EINVAL);
	}
	memset(&statfs_buf, 0, sizeof(statfs_buf));
	inode->i_sb->s_op->statfs(filp->f_path.dentry, &statfs_buf);
	rom_free = statfs_buf.f_bfree;
	rom_free = hp_file_B_2_GB(rom_free * statfs_buf.f_bsize);
	hh_print(HHLOG_ERR, "rom_free: %llu", rom_free);
	if (rom_free < HP_FILE_MIN_FREE_ROM_GB) {
		hh_print(HHLOG_ERR, "rom nosapce");
		return ERR_PTR(-ENOMEM);
	}

	nr_pages = i_size_read(inode) / PAGE_SIZE;
	nr_pages = ALIGN_DOWN(nr_pages, HP_FILE_ALIGN_SECTION_PAGES);
	hh_print(HHLOG_ERR, "before expand pages 0x%lx, ino %lu", nr_pages,
		 inode->i_ino);

	hh_print(HHLOG_ERR, "fallocate from %llu, len %llu", from, len);
	ret = inode->i_fop->fallocate(filp, 0, from, len);
	if (ret < 0) {
		hh_print(HHLOG_ERR, "fallocate %d", ret);
		return ERR_PTR(ret);
	}

	nr_pages = (from + len) / PAGE_SIZE;
	nr_pages = ALIGN_DOWN(nr_pages, HP_FILE_ALIGN_SECTION_PAGES);
	hh_print(HHLOG_ERR, "afore expand  pages 0x%lx, ino %lu", nr_pages,
		 inode->i_ino);
	if (nr_pages < HP_FILE_ALIGN_SECTION_PAGES) {
		hh_print(HHLOG_ERR, "pages 0x%lx too small", nr_pages);
		return ERR_PTR(-EINVAL);
	}

	sis = kvzalloc(sizeof(*sis), GFP_KERNEL);
	if (!sis) {
		hh_print(HHLOG_ERR, "fbd nomem");
		return ERR_PTR(-ENOMEM);
	}

	sis->max = nr_pages;
	sis->swap_file = filp;
	sis->swap_extent_root = RB_ROOT;
	hh_print(HHLOG_ERR, "new sis");

	ret = mapping->a_ops->swap_activate(sis, filp, &span);
	if (ret < 0) {
		hh_print(HHLOG_ERR, "fbd activate %d", ret);
		hyperhold_free_sis(sis);
		return ERR_PTR(ret);
	}
	hh_print(HHLOG_INFO, "span 0x%lx", (long)span);
	hh_print(HHLOG_INFO, "alloc end");
	return sis;
}

bool hyperhold_expand_sis(void)
{
	unsigned long flags;
	struct hyperhold_file_bdev *fbdev = NULL;
	struct swap_info_struct *sis = NULL;
	struct swap_info_struct *old_sis = NULL;
	int sis_status;
	int exts_bit;
	unsigned long curr_time;

	fbdev = &global_settings.fbdev;
	if (!fbdev->filp) {
		hh_print(HHLOG_ERR, "filp not init");
		return false;
	}

	sis_status = atomic_cmpxchg(&fbdev->in_expand, HP_NOT_IN_EXPAND,
				    HP_IN_EXPAND);
	if (sis_status == HP_IN_EXPAND) {
		hh_print(HHLOG_ERR, "sis alread in expand");
		return false;
	}

	if (fbdev->max < fbdev->from + fbdev->stride) {
		hh_print(HHLOG_ERR, "sis expand max size");
		goto out;
	}
	curr_time = jiffies;
	sis = hyperhold_alloc_sis(fbdev->filp, fbdev->from, fbdev->stride);
	if (IS_ERR(sis)) {
		atomic64_inc(&file_expand_fail);
		goto out;
	}
	fbdev->allo_sis_time += (jiffies - curr_time);
	hh_print(HHLOG_ERR, "got sis 0x%p", sis);
	curr_time = jiffies;
	spin_lock_irqsave(&fbdev_spin_lock, flags);
	old_sis = fbdev->sis;
	fbdev->sis = sis;
	fbdev->from += fbdev->stride;
	exts_bit = fbdev->from >> EXTENT_SHIFT;
	atomic_set(&fbdev->max_exts_bit, exts_bit);
	spin_unlock_irqrestore(&fbdev_spin_lock, flags);
	fbdev->replace_sis_time += (jiffies - curr_time);
	hh_print(HHLOG_ERR, "file current max ext bits:%d",
		 atomic_read(&fbdev->max_exts_bit));
	fbdev->expand_count += 1;
out:
	if (old_sis)
		hyperhold_free_sis(old_sis);

	sis_status = atomic_cmpxchg(&fbdev->in_expand, HP_IN_EXPAND,
				    HP_NOT_IN_EXPAND);
	if (sis_status != HP_IN_EXPAND) {
		hh_print(HHLOG_ERR, "sis expand error");
		return false;
	}

	return true;
}

bool hyperhold_rom_check_ok(void)
{
	return atomic_read(&rom_vaild) != 0;
}

static ssize_t hyperhold_pin_file_write(struct file *file, const char __user *buf,
					 size_t count, loff_t *ppos)
{
	int ret;
	__u32 pin = 0;
	long ioctl_ret;

	struct file *filp = global_settings.fbdev.filp;

	if (!filp) {
		hh_print(HHLOG_ERR, "filp not init");
		return -EFAULT;
	}
	if (atomic_read(&file_pined) != 0) {
		hh_print(HHLOG_ERR, "already pin");
		return count;
	}
	atomic_set(&file_pined, 1);

	ret = get_user(pin, (__u32 __user *)buf);
	if (ret > 0) {
		hh_print(HHLOG_ERR, "copy err");
		goto err_out;
	}

	if (pin == 0) {
		hh_print(HHLOG_ERR, "err pin %u", pin);
		goto err_out;
	}

	ioctl_ret = filp->f_op->unlocked_ioctl(filp, F2FS_IOC_SET_PIN_FILE, (unsigned long)buf);
	hh_print(HHLOG_ERR, "ioctl_ret:%ld, pin:%u", ioctl_ret, pin);
	if (ioctl_ret)
		goto err_out;
	goto out;

err_out:
	atomic_set(&file_pined, 0);
out:
	return count;
}

static int hyperhold_pin_file_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", atomic_read(&file_pined));
	return 0;
}

static int hyperhold_pin_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, hyperhold_pin_file_show, NULL);
}

static const struct proc_ops hp_file_pin_ops = {
	.proc_open = hyperhold_pin_file_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = hyperhold_pin_file_write,
};

static void hyperhold_pin_file_init(void)
{
	proc_create("hp_pin", 0660, NULL, &hp_file_pin_ops);
}

bool hyperhold_init_fbdev_info(int fd, int rom_valid, char *file_name,
			       u64 expand_len)
{
	size_t temp;
	unsigned long flags;
	struct file *filp = NULL;
	struct block_device *bdev = NULL;
	struct inode *inode = NULL;
	struct hyperhold_file_bdev *fbdev = NULL;
	u64 expand_max;
	bool result = true;

	if (fd < 0 || !file_name) {
		hh_print(HHLOG_ERR, "info err");
		return false;
	}
	filp = fget(fd);
	if (!filp) {
		hh_print(HHLOG_ERR, "filp get err");
		return false;
	}
	inode = filp->f_mapping->host;
	bdev = inode->i_sb->s_bdev;
	if (!bdev) {
		hh_print(HHLOG_ERR, "get dev failed!\n");
		result = false;
		goto err_out;
	}
	hh_print(HHLOG_ERR, "got filp");

	expand_max =
		atomic64_read(&hp_total_sz) - atomic64_read(&hp_partition_sz);
	expand_max = ALIGN_DOWN(expand_max, HP_FILE_ALIGN_SECTION);
	atomic_set(&rom_vaild, rom_valid);
	hh_print(HHLOG_ERR, "expand_max %llu", expand_max);

	fbdev = &global_settings.fbdev;
	spin_lock_irqsave(&fbdev_spin_lock, flags);
	if (fbdev->filp) {
		hh_print(HHLOG_ERR, "already set filp");
		result = false;
		goto out;
	}
	temp = snprintf(fbdev->file_name, sizeof(fbdev->file_name), "%s",
			file_name);
	if (temp < 0 || temp > sizeof(fbdev->file_name)) {
		result = false;
		goto out;
	}

	fbdev->fd = fd;
	fbdev->ino = inode->i_ino;
	fbdev->filp = filp;
	fbdev->bdev = bdev;
	fbdev->sis = NULL;
	atomic_set(&fbdev->max_exts_bit, 0);
	fbdev->from = 0;
	fbdev->stride = expand_len;
	fbdev->max = expand_max;
	fbdev->expand_count = 0;
	fbdev->allo_sis_time = 0;
	fbdev->replace_sis_time = 0;
	atomic_set(&fbdev->in_expand, HP_NOT_IN_EXPAND);
out:
	spin_unlock_irqrestore(&fbdev_spin_lock, flags);
	if (fbdev->filp)
		hyperhold_pin_file_init();

err_out:
	if (!result) {
		if (filp)
			fput(filp);
	}

	return result;
}

bool hyperhold_parse_file(char *param[], int max_index)
{
	int index = 1;
	char *element = NULL;
	u64 value;
	char str_value[50];
	size_t temp;
	int fd = -1;
	int rom_valid = 0;
	char file_name[HYPERHOLD_NAME_LEN];
	u64 expand_len = hp_file_convert_mb(256);

	while (index < max_index) {
		value = 0;
		memset(str_value, 0, sizeof(str_value));
		element = param[index];
		hh_print(HHLOG_ERR, "index %d element %s", index, element);
		if (hp_str_get_u64(element, "FD=", &value)) {
			fd = (int)value;
		} else if (hp_str_get_u64(element, "ROM_VAILD=", &value)) {
			rom_valid = (int)value;
		} else if (hp_str_get_str(element, "FILE=", str_value,
					  sizeof(str_value))) {
			temp = snprintf(file_name, sizeof(file_name), "%s",
					str_value);
			if (temp < 0 || temp > sizeof(file_name))
				return false;
		} else if (hp_str_get_u64(element, "EXPAND_SIZE=", &value)) {
			expand_len = hp_file_convert_mb(value);
			expand_len =
				ALIGN_DOWN(expand_len, HP_FILE_ALIGN_SECTION);
		}
		index++;
	}

	hh_print(HHLOG_ERR, "fd %d", fd);
	hh_print(HHLOG_ERR, "rom_valid %d", rom_valid);
	hh_print(HHLOG_ERR, "file_name %s", file_name);
	hh_print(HHLOG_ERR, "expand_size %llu", expand_len);

	return hyperhold_init_fbdev_info(fd, rom_valid, file_name, expand_len);
}

bool hyperhold_parse_ctl(char *param[], int max_index)
{
	int index = 1;
	char *element = NULL;
	u64 value;
	char str_value[50];
	int expand_fail = 0;
	int expand_ratio = HP_FILE_EXPAND_RATIO;

	while (index < max_index) {
		value = 0;
		memset(str_value, 0, sizeof(str_value));
		element = param[index];
		hh_print(HHLOG_ERR, "index %d element %s", index, element);
		if (hp_str_get_u64(element, "EXPAND_FAIL=", &value))
			expand_fail = value;
		else if (hp_str_get_u64(element, "EXPAND_RATIO=", &value))
			expand_ratio = value;
		index++;
	}

	hh_print(HHLOG_ERR, "expand_fail %d", expand_fail);
	hh_print(HHLOG_ERR, "expand_ratio %d", expand_ratio);
	atomic_set(&ctl_file_expand_fail, expand_fail);
	atomic_set(&file_expand_ratio, expand_ratio);

	return true;
}

bool hyperhold_parse_space_all(char *param[], int max_index)
{
	bool ret = true;
	int index = 1;
	char *element = NULL;
	u64 value;

	while (index < max_index) {
		value = 0;
		element = param[index];
		hh_print(HHLOG_ERR, "index %d element %s", index, element);
		if (hp_str_get_u64(element, "SPACE_TYPE=", &value))
			hyperhold_set_type(value);
		else if (hp_str_get_u64(element, "TOTAL_SIZE=", &value))
			hyperhold_set_total_sz(value);
		else if (hp_str_get_u64(element, "PARTITION_SIZE=", &value))
			hyperhold_set_partition_sz(value);
		else if (hp_str_get_u64(element, "USER_CLOSEED=", &value))
			hyperhold_set_user_closed(value);
		index++;
	}
	return ret;
}

bool hyperhold_parse(char *param[], int max_index)
{
	char *element;

	element = param[0];

	if (!element) {
		hh_print(HHLOG_ERR, "element NULL");
		return false;
	}
	hh_print(HHLOG_ERR, "index 0 element %s", element);
	if (hp_str_eq(element, "TYPE=SPACE_ALL")) {
		return hyperhold_parse_space_all(param, max_index);
	} else if (hp_str_eq(element, "TYPE=FILE")) {
		return hyperhold_parse_file(param, max_index);
	} else if (hp_str_eq(element, "TYPE=EXPAND")) {
		hyperhold_wakeup_expand();
		return true;
	} else if (hp_str_eq(element, "TYPE=CTL")) {
		return hyperhold_parse_ctl(param, max_index);
	}
	return false;
}

bool hyperhold_split_param(const char *buf, size_t len)
{
	char delims[] = ";";
	char *token = NULL;
	int index = 0;
	int size;
	char *result[10];
	char *element;
	char *dumpbuf, **dumpbuf_ref;
	size_t temp_size;
	bool ret = true;

	dumpbuf = hyperhold_malloc(len + 1, true, true);
	if (!dumpbuf) {
		hh_print(HHLOG_ERR, "param buf error");
		return false;
	}
	size = snprintf(dumpbuf, len + 1, "%s", buf);
	if (size < 0 || size > len + 1) {
		ret = false;
		goto errout;
	}
	dumpbuf_ref = &dumpbuf;
	memset(result, 0, sizeof(result));
	token = strsep(dumpbuf_ref, delims);
	while (token != NULL && strlen(token) > 0 && index < 10) {
		hh_print(HHLOG_ERR, "hyperhold space index %d param %s", index,
			 token);
		temp_size = strlen(token) + 1;
		element = hyperhold_malloc(temp_size, true, true);
		size = snprintf(element, temp_size, "%s", token);
		if (size > 0 && size < temp_size) {
			result[index] = element;
		} else {
			hh_print(HHLOG_ERR, "param error %s", dumpbuf);
			ret = false;
			goto out;
		}

		index++;
		token = strsep(dumpbuf_ref, delims);
	}

	ret = hyperhold_parse(result, index);

out:
	while (index >= 0) {
		element = result[index];
		if (element)
			hyperhold_free(element);
		index--;
	}
errout:
	hyperhold_free(dumpbuf);
	return ret;
}

bool hyperhold_file_info_inited(void)
{
	int space_type = atomic_read(&global_settings.space_type);

	if (unlikely(space_type == HP_NOT_INIT))
		return false;
	if (space_type == HP_PARTITION_FILE_ARCHIVAL)
		return global_settings.fbdev.filp != NULL;

	return true;
}

bool hyperhold_is_file_arch(void)
{
	return atomic_read(&global_settings.space_type) ==
	       HP_PARTITION_FILE_ARCHIVAL;
}

int hyperhold_adjust_current_max(int patition_max, int max)
{
	int adjust = patition_max;
	struct hyperhold_file_bdev *fbdev = NULL;
	unsigned long flags;

	if (!hyperhold_is_file_arch())
		return max;

	if (unlikely(patition_max > max)) {
		hh_print(HHLOG_ERR, "param err");
		return max;
	}
	fbdev = &global_settings.fbdev;
	if (unlikely(!fbdev->filp)) {
		hh_print(HHLOG_ERR, "file not init");
		return adjust;
	}

	spin_lock_irqsave(&fbdev_spin_lock, flags);
	adjust = atomic_read(&fbdev->max_exts_bit);
	if (unlikely(adjust + patition_max > max))
		adjust = max;
	else
		adjust += patition_max;
	spin_unlock_irqrestore(&fbdev_spin_lock, flags);

	return adjust;
}

unsigned long hyperhold_fbd_index_to_sector(unsigned long index)
{
	struct rb_node *rb;
	struct swap_extent *se;
	unsigned long block;
	struct hyperhold_file_bdev *fbdev;
	unsigned long flags;
	pgoff_t offset = index;

	fbdev = &global_settings.fbdev;

	spin_lock_irqsave(&fbdev_spin_lock, flags);

	rb = fbdev->sis->swap_extent_root.rb_node;
	while (rb) {
		se = rb_entry(rb, struct swap_extent, rb_node);
		if (offset < se->start_page)
			rb = rb->rb_left;
		else if (offset >= se->start_page + se->nr_pages)
			rb = rb->rb_right;
		else {
			block = se->start_block + (offset - se->start_page);
			spin_unlock_irqrestore(&fbdev_spin_lock, flags);
			return block * PAGE_SECTORS;
		}
	}

	spin_unlock_irqrestore(&fbdev_spin_lock, flags);

	return 0;
}

void hyperhold_fill_entry_addr(struct zram *zram,
			       struct hyperhold_entry *io_entry)
{
	struct hyperhold_area *area = zram->area;
	int io_ext_id = io_entry->ext_id;
	int io_ext_bit = extent_id2bit(area, io_entry->ext_id);

	WARN_ON_ONCE(io_ext_id < 0 || io_ext_id >= area->nr_exts || io_ext_bit < 0);

	if (!hyperhold_file_info_inited()) {
		hh_print(HHLOG_DEBUG, "file not inited\n");
		return;
	}
	if (!global_settings.fbdev.sis) {
		hh_print(HHLOG_DEBUG, "sis not inited\n");
		return;
	}

	if (io_ext_bit < area->nr_partition_exts) {
		io_entry->addr = (sector_t)io_ext_id * EXTENT_SECTOR_SIZE;
	} else {
		int fbd_ext_id = io_ext_id - area->nr_partition_exts;
		uint32_t page_idx = fbd_ext_id * EXTENT_PG_CNT;

		io_entry->io_bdev = global_settings.fbdev.bdev;
		io_entry->addr = hyperhold_fbd_index_to_sector(page_idx);
	}
	hh_print(HHLOG_DEBUG, "ext_id %d %d addr %llu\n", io_ext_id,
		 area->nr_partition_exts, (u64)io_entry->addr);
}

int hyperhold_expand_workqueue_init(void)
{
	hyperhold_expand_workqueue =
		alloc_workqueue("proc_hyperhold_expand", WQ_CPU_INTENSIVE, 0);
	if (unlikely(!hyperhold_expand_workqueue)) {
		hh_print(HHLOG_ERR, "hyperhold_expand_workqueue not inited\n");
		hyperhold_expand_workqueue = NULL;
		return -EFAULT;
	}
	return 0;
}

static void hyperhold_expand_work(struct work_struct *work)
{
	hyperhold_expand_sis();
	kfree(work);
}

void hyperhold_wakeup_expand(void)
{
	unsigned long long total, used;
	int ratio = 0;
	struct hyperhold_stat *stat;
	unsigned long curr_interval;
	struct hyperhold_file_bdev *fbdev;
	struct work_struct *expand_work;

	curr_interval = jiffies_to_msecs(jiffies - last_file_expand_time);
	if (curr_interval < file_expand_skip_interval)
		return;

	last_file_expand_time = jiffies;
	if (!hyperhold_expand_workqueue || !hyperhold_enable() ||
	    !hyperhold_is_file_arch())
		return;

	fbdev = &global_settings.fbdev;
	stat = global_settings.stat;
	if (!global_settings.zram || !global_settings.zram->area ||
	    !fbdev->filp || !stat)
		return;

	if (!hyperhold_rom_check_ok())
		return;

	used = atomic64_read(&stat->ext_cnt);
	total = atomic_read(&fbdev->max_exts_bit) +
		global_settings.zram->area->nr_partition_exts;
	if (total > 0)
		ratio = used * 100 / total;
	if (ratio < atomic_read(&file_expand_ratio) &&
	    fbdev->expand_count != 0) {
		return;
	}
	hh_print(HHLOG_ERR, "used ratio %d", ratio);
	if (atomic_read(&fbdev->in_expand) == HP_IN_EXPAND)
		return;
	if (fbdev->from + fbdev->stride > fbdev->max)
		return;

	expand_work = kzalloc(sizeof(struct work_struct), GFP_KERNEL);
	if (!expand_work)
		return;
	INIT_WORK(expand_work, hyperhold_expand_work);
	queue_work(hyperhold_expand_workqueue, expand_work);
}

void hyperhold_file_io_fail(enum hyperhold_scenario scenario, int ext_id)
{
	if (!hyperhold_is_file_arch() || !global_settings.fbdev.filp)
		return;
	if (unlikely(!global_settings.zram || !global_settings.zram->area))
		return;
	if (ext_id <= global_settings.zram->area->nr_partition_exts)
		return;

	switch (scenario) {
	case HYPERHOLD_FAULT_OUT:
	case HYPERHOLD_PRE_OUT:
	case HYPERHOLD_BATCH_OUT:
		atomic64_inc(&file_read_io_fail);
		break;
	case HYPERHOLD_RECLAIM_IN:
		atomic64_inc(&file_write_io_fail);
		break;
	default:
		break;
	}
}

void hyperhold_fbdev_info(char *buf, int len)
{
	struct hyperhold_file_bdev *fbdev;
	struct hyperhold_stat *stat;
	unsigned long flags;
	unsigned int allo_time, replace_time;
	unsigned long long total, used;
	int ratio = 0, file_ratio = 0;

	fbdev = &global_settings.fbdev;
	if (!fbdev->filp || !fbdev->sis)
		return;

	stat = global_settings.stat;
	if (!global_settings.zram || !global_settings.zram->area || !stat)
		return;

	spin_lock_irqsave(&fbdev_spin_lock, flags);
	allo_time = jiffies_delta_to_msecs(fbdev->allo_sis_time);
	replace_time = jiffies_delta_to_msecs(fbdev->replace_sis_time);

	used = atomic64_read(&stat->ext_cnt);
	total = atomic_read(&fbdev->max_exts_bit) +
		global_settings.zram->area->nr_partition_exts;
	if (total > 0)
		ratio = used * 100 / total;

	if (used > global_settings.zram->area->nr_partition_exts) {
		used = used - global_settings.zram->area->nr_partition_exts;
		total = atomic_read(&fbdev->max_exts_bit);
		if (total > 0)
			file_ratio = used * 100 / total;
	} else {
		used = 0;
	}

	snprintf(HP_BUF, HP_LEN,
		 "file_fd:%d\n"
		 "file_ino:%lu\n"
		 "file_rom_vaild:%d\n"
		 "file_filp:0x%p\n"
		 "file_bdev:0x%p\n"
		 "file_sis:0x%p\n"
		 "file_max_exts_bit:%d\n"
		 "file_expand_count:%d\n"
		 "file_expand_fail_count:%llu\n"
		 "file_allo_sis_time:%u ms\n"
		 "file_replace_sis_time:%u ms\n"
		 "file_used:%llu MB\n"
		 "file_current:%llu MB\n"
		 "file_stride:%llu MB\n"
		 "file_max:%llu MB\n"
		 "file_used_ratio:%d\n"
		 "file_all_used_ratio:%d\n"
		 "file_expand_ratio:%d\n"
		 "file_read_io_fail:%llu\n"
		 "file_write_io_fail:%llu\n",
		 fbdev->fd, fbdev->ino, atomic_read(&rom_vaild), fbdev->filp,
		 fbdev->bdev, fbdev->sis, atomic_read(&fbdev->max_exts_bit),
		 fbdev->expand_count, atomic64_read(&file_expand_fail),
		 allo_time, replace_time, hp_file_extent_shift_mb(used),
		 hp_align_mb(fbdev->from), hp_align_mb(fbdev->stride),
		 hp_align_mb(fbdev->max), file_ratio, ratio,
		 atomic_read(&file_expand_ratio),
		 atomic64_read(&file_read_io_fail),
		 atomic64_read(&file_write_io_fail));
	spin_unlock_irqrestore(&fbdev_spin_lock, flags);
}

void hyperhold_area_info(char *buf, int len)
{
	struct hyperhold_area *area;
	int bit_size;

	if (!global_settings.zram)
		return;
	area = global_settings.zram->area;
	if (!area)
		return;
	bit_size = hp_file_extent_shift_mb(atomic_read(&area->next_alloc_bit));
	snprintf(HP_BUF, HP_LEN,
		 "area_nr_exts:%d\n"
		 "area_nr_partition_exts:%d\n"
		 "area_next_alloc_bit:%d\n"
		 "area_next_alloc_bit_size:%d MB\n",
		 area->nr_exts, area->nr_partition_exts,
		 atomic_read(&area->next_alloc_bit), bit_size);
}

void hyperhold_file_show(char *buf, int len)
{
	hyperhold_fbdev_info(HP_BUF, HP_LEN);
	hyperhold_area_info(HP_BUF, HP_LEN);
}

#endif

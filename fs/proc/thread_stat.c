// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include "securec.h"
#include "internal.h"

#define STACK_LOG_MAX_SIZE 4096
#define STAT_MAGIC_NUMBER 0xFADE1101
#define MAGIC_NUMBER_STR_LEN 10
#define MAGIC_NUMBER_BASE 16
#define PROC_MAX_NUM 3
#define NOT_FOUND -1

#define for_each_proc_thread_stack(data, array) \
		for (; i < PROC_MAX_NUM && ((data) = &(array)[i]); i++)

struct proc_thread_stack {
	pid_t pid;
	char proc_name[TASK_COMM_LEN];
	size_t stack_len;
	char *stack_info;
};

static DEFINE_MUTEX(cache_mutex);
static struct proc_thread_stack proc_thread_stack_cache[PROC_MAX_NUM];
static int last_update_stack_cache_index = -1;

static void clean_proc_thread_stack(struct proc_thread_stack *proc_stack_entry)
{
	if (proc_stack_entry->stack_info)
		vfree(proc_stack_entry->stack_info);
	memset_s(proc_stack_entry, sizeof(*proc_stack_entry), 0, sizeof(*proc_stack_entry));
}

const int is_invalid_proc_thread(struct proc_thread_stack *proc_stack_entry)
{
	struct task_struct* task;
	int ret = 0;

	task = get_pid_task(find_vpid(proc_stack_entry->pid), PIDTYPE_PID);
	if (!task)
		return -ESRCH;
	if (strcmp(proc_stack_entry->proc_name, task->comm) != 0) {
		ret = -EINVAL;
	}
	put_task_struct(task);

	return ret;
}

static int get_free_stack_cache_index(void)
{
	int i = 0;
	struct proc_thread_stack *proc_stack_entry;
	for_each_proc_thread_stack(proc_stack_entry, proc_thread_stack_cache) {
		if (proc_stack_entry->pid == 0) {
			return i;
		}
		if (is_invalid_proc_thread(proc_stack_entry)) {
			return i;
		}
	}
	if (i == PROC_MAX_NUM)
		i = (last_update_stack_cache_index + 1) % PROC_MAX_NUM;

	return i;
}

static int get_stack_cache_index_for_proc(struct task_struct *task)
{
	int i = 0;
	struct proc_thread_stack *proc_stack_entry;
	for_each_proc_thread_stack(proc_stack_entry, proc_thread_stack_cache) {
		if (proc_stack_entry->pid == task->pid &&
			strcmp(proc_stack_entry->proc_name, task->comm) == 0) {
			return i;
		}
	}

	return NOT_FOUND;
}

static int init_stack_cache_entry(struct task_struct *task, size_t entry_stack_size,
			struct proc_thread_stack *proc_stack_entry)
{
	int ret = 0;

	clean_proc_thread_stack(proc_stack_entry);
	proc_stack_entry->pid = task->pid;
	ret = strncpy_s(proc_stack_entry->proc_name, TASK_COMM_LEN, task->comm,
		min(strlen(task->comm), (size_t)(TASK_COMM_LEN - 1)));
	if (ret != 0) {
		pr_err("strncpy_s procname error: %d\n", ret);
		return ret;
	}
	proc_stack_entry->stack_len = 0;
	proc_stack_entry->stack_info = vzalloc(entry_stack_size);
	if (!proc_stack_entry->stack_info) {
		pr_err("vzalloc error\n");
		ret = -ENOMEM;
	}

	return ret;
}

static int get_target_cache_index(struct task_struct *task)
{
	int target_index = get_stack_cache_index_for_proc(task);
	if (target_index == NOT_FOUND) {
		target_index = get_free_stack_cache_index();
	}

	return target_index;
}

static inline bool is_valid_index(int index)
{
	if (index >= 0 && index < PROC_MAX_NUM)
		return true;
	return false;
}

static int store_stack_cache_from_buf(struct file *file, const char *buf_pos, size_t kstack_size)
{
	int ret = 0, cache_index = 0;
	struct task_struct *task = NULL;
	struct proc_thread_stack *thread_stack = NULL;

	task = get_proc_task(file_inode(file));
	if (!task)
		return -ESRCH;

	mutex_lock(&cache_mutex);
	cache_index = get_target_cache_index(task);
	if (!is_valid_index(cache_index)) {
		pr_err("invalid index: %d\n", cache_index);
		ret = -EINVAL;
		goto out;
	}

	thread_stack = &proc_thread_stack_cache[cache_index];
	if (thread_stack == NULL) {
		ret = -EINVAL;
		goto out;
	}
	ret = init_stack_cache_entry(task, kstack_size, thread_stack);
	if (ret) {
		pr_err("init_stack_cache_entry failed\n");
		goto out;
	}

	ret = memcpy_s(thread_stack->stack_info, kstack_size, buf_pos, kstack_size);
	if (ret == 0) {
		thread_stack->stack_len = kstack_size;
		last_update_stack_cache_index = cache_index;
	} else {
		pr_err("thread_stat: copy stack error\n");
		clean_proc_thread_stack(thread_stack);
	}

out:
	mutex_unlock(&cache_mutex);
	put_task_struct(task);
	return ret;
}

inline bool is_valid_number_len(size_t number_str_len)
{
	return (number_str_len == MAGIC_NUMBER_STR_LEN) ? true : false;
}

static unsigned long get_number_from_buf(char **buf, bool update_pos)
{
	unsigned long num_ret = 0;
	char *number_buf, *next_line;
	int ret = 0;

	if (buf == NULL) {
		pr_err("get_number_from_buf: buf null\n");
		return num_ret;
	}
	number_buf = *buf;
	if (number_buf == NULL) {
		pr_err("get_number_from_buf: number_buf null\n");
		return num_ret;
	}

	next_line = strchr(number_buf, '\n');
	if (next_line) {
		*next_line = '\0';
		if (!is_valid_number_len(strlen(number_buf))) {
			pr_err("get_number_from_buf: number len invalid\n");
			return num_ret;
		}
		ret = kstrtoul(number_buf, MAGIC_NUMBER_BASE, &num_ret);
		if (ret) {
			pr_err("convert str to ul error: %d\n", ret);
			return num_ret;
		}
		if (update_pos) {
			*buf = next_line + 1;
		}
	}
	return num_ret;
}

static inline bool write_access_check(char **buf)
{
	unsigned long input = get_number_from_buf(buf, true);
	if (input == STAT_MAGIC_NUMBER) {
		return true;
	}
	pr_err("thread_stat_write: bad magic number, input = %lx\n", input);
	return false;
}

static inline size_t get_stack_buf_size(size_t buf_count, char *buf_start, char *stack_start)
{
	if (buf_start >= stack_start) {
		pr_err("thread_stat: wrong stack size\n");
		return 0;
	}
	return (size_t) (buf_count - (stack_start - buf_start));
}

static ssize_t thread_stat_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	char *kbuf = NULL, *buf_pos;
	int err = 0;
	size_t stack_size;

	if (count > STACK_LOG_MAX_SIZE) {
		pr_err("thread_stat_write: count too large\n");
		count = STACK_LOG_MAX_SIZE;
	}

	kbuf = vzalloc(count);
	if (!kbuf) {
		pr_err("thread_stat_write: vzalloc error\n");
		err = -ENOMEM;
		goto out;
	}
	if (copy_from_user(kbuf, buf, count)) {
		pr_err("thread_stat_write: copy to kbuf error, count = %zu\n", count);
		err = -EFAULT;
		goto out;
	}
	pr_debug("thread_stat_write:kbuf: %s, ppos:%lld\n", kbuf, *ppos);

	buf_pos = kbuf;
	if (!write_access_check(&buf_pos)) {
		err = -EACCES;
		goto out;
	}
	stack_size = get_stack_buf_size(count, kbuf, buf_pos);
	err = store_stack_cache_from_buf(file, buf_pos, stack_size);
	pr_info("thread_stat_write: count = %zu, ppos:%lld, stack_size:%zu, err:%d\n",
			count, *ppos, stack_size, err);

out:
	if (kbuf) {
		vfree(kbuf);
		kbuf = NULL;
	}
	return err < 0 ? err : count;
}

static ssize_t thread_stat_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct task_struct *task = get_proc_task(file_inode(file));
	struct proc_thread_stack *thread_stack = NULL;
	int ret = 0, cache_index = 0;

	if (!task)
		return -ESRCH;

	mutex_lock(&cache_mutex);
	cache_index = get_stack_cache_index_for_proc(task);
	if (!is_valid_index(cache_index)) {
		pr_err("thread_stat_read: invalid cache index: %d\n", cache_index);
		ret = -EINVAL;
		goto out;
	}

	thread_stack = &proc_thread_stack_cache[cache_index];
	if (thread_stack == NULL) {
		pr_err("thread_stat_read: thread_stack null\n");
		ret = -EINVAL;
		goto out;
	}
	if (thread_stack->stack_info) {
		ret = simple_read_from_buffer(buf, count, ppos, thread_stack->stack_info,
				thread_stack->stack_len);
	}
	pr_info("thread_stat_read: count=%zu, stacklen=%zu, ppos=%lld, ret(size) = %d\n", count,
		thread_stack->stack_len, *ppos, ret);
	clean_proc_thread_stack(thread_stack);

out:
	mutex_unlock(&cache_mutex);
	put_task_struct(task);
	return ret;
}

const struct file_operations proc_thread_stat_operations = {
	.read		= thread_stat_read,
	.write		= thread_stat_write,
	.llseek		= generic_file_llseek,
};
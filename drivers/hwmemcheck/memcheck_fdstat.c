#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/sched/task.h>
#include <linux/sched.h>
#include <linux/version.h>

unsigned int get_next_fd_by_pid(pid_t pid)
{
	struct task_struct *task = NULL;
	struct files_struct *files = NULL;
	unsigned int count = 0;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	files = task->files;
	if (!files) {
		put_task_struct(task);
		return -EINVAL;
	}

	spin_lock(&files->file_lock);
	count = files->next_fd;
	spin_unlock(&files->file_lock);
	put_task_struct(task);

	return count;
}
EXPORT_SYMBOL(get_next_fd_by_pid);

static inline struct file *get_accurate_file(struct files_struct *files,
					     unsigned int fd)
{
	struct file *file = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	file = files_lookup_fd_locked(files, fd);
#else
	file = fcheck_files(files, fd);
#endif
	return file;
}

unsigned int get_inuse_fd_by_pid(pid_t pid)
{
	struct task_struct *task = NULL;
	struct files_struct *files = NULL;
	struct fdtable *fds;
	unsigned int count = 0;
	int i;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	files = task->files;
	if (!files) {
		put_task_struct(task);
		return -EINVAL;
	}

	rcu_read_lock();
	spin_lock(&files->file_lock);
	fds = files_fdtable(task->files);
	for (i = 0; i < fds->max_fds; i++) {
		if (get_accurate_file(task->files, i)) {
			count++;
		}
	}
	spin_unlock(&files->file_lock);
	rcu_read_unlock();
	put_task_struct(task);

	return count;
}
EXPORT_SYMBOL(get_inuse_fd_by_pid);

unsigned int get_pipe_fd_by_pid(int pid)
{
	struct task_struct *task = NULL;
	struct files_struct *files = NULL;
	struct file *file = NULL;
	struct fdtable *fds;
	unsigned int count = 0;
	int i;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!task) {
		rcu_read_unlock();
		return -ESRCH;
	}
	get_task_struct(task);
	rcu_read_unlock();

	files = task->files;
	if (!files) {
		put_task_struct(task);
		return -EINVAL;
	}

	rcu_read_lock();
	spin_lock(&files->file_lock);
	fds = files_fdtable(task->files);
	for (i = 0; i < fds->max_fds; i++) {
		file = get_accurate_file(task->files, i);
		if (file) {
			if (S_ISFIFO(file_inode(file)->i_mode)) {
				count++;
			}
		}
	}
	spin_unlock(&files->file_lock);
	rcu_read_unlock();
	put_task_struct(task);

	return count;
}
EXPORT_SYMBOL(get_pipe_fd_by_pid);
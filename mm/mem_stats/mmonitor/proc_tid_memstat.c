#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/delayacct.h>
#include <linux/pid_namespace.h>
#include <linux/pid.h>
#include <linux/spinlock.h>

#ifdef CONFIG_MEMORY_MONITOR
int proc_tid_memstat(struct seq_file *m, struct pid_namespace *ns,
		     struct pid *pid, struct task_struct *task)
{
	unsigned long flags;
	unsigned long long allocpages_delay_total;
	unsigned long long allocpages_delay_count;
	unsigned long long allocpages_delay_max;
	unsigned long long allocpages_delay_max_order;
	if (unlikely(!(task->delays)))
		seq_printf(m, "task: %s, %d, delays is NULL.\n", task->comm,
			   task->pid);
	else {
		spin_lock_irqsave(&task->delays->allocpages_lock, flags);
		allocpages_delay_total =
			(unsigned long long)task->delays->allocpages_delay;
		allocpages_delay_count =
			(unsigned long long)task->delays->allocpages_count;
		allocpages_delay_max =
			(unsigned long long)task->delays->allocpages_delay_max;
		allocpages_delay_max_order =
			(unsigned long long)
				task->delays->allocpages_delay_max_order;
		spin_unlock_irqrestore(&task->delays->allocpages_lock, flags);
		seq_printf(m, "task: %s, %d, delays exist.\n", task->comm,
			   task->pid);
		seq_printf(m, "Allocpages delay:\n");
		seq_printf(
			m,
			"Total: %llu ns, count: %llu, max_delay: %llu ns, max_delay_order: %llu\n",
			allocpages_delay_total, allocpages_delay_count,
			allocpages_delay_max, allocpages_delay_max_order);
	}

	return 0;
}
#endif

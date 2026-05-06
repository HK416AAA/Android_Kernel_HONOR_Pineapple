// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/sched.h>

#define LOGGER_RV_BLOCK "block"
#define LOGGER_RV_ABORT "abort"
#define LOGGER_RV_LOG "log"
#define LOGGER_RV_EVENT "event"

extern ssize_t send_rv_event(const char *msg, ssize_t msg_len);

static char func_name[NAME_MAX];
module_param_string(func, func_name, NAME_MAX, S_IRUGO);
MODULE_PARM_DESC(func, "Function to kretprobe");

static char action_name[NAME_MAX];
module_param_string(action, action_name, NAME_MAX, S_IRUGO);
MODULE_PARM_DESC(action, "the action to do");

static int ret_block_handler(struct kretprobe_instance *ri,
			     struct pt_regs *regs)
{
	while (1) {
	}

	return 0;
}
NOKPROBE_SYMBOL(ret_block_handler);

static int ret_abort_handler(struct kretprobe_instance *ri,
			     struct pt_regs *regs)
{
	BUG_ON(true);

	return 0;
}
NOKPROBE_SYMBOL(ret_abort_handler);

static int ret_log_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	pr_err(" rv log from function %s \n",
	       get_kretprobe(ri)->kp.symbol_name);

	return 0;
}
NOKPROBE_SYMBOL(ret_log_handler);

static int ret_event_handler(struct kretprobe_instance *ri,
			     struct pt_regs *regs)
{
	return 0;
}
NOKPROBE_SYMBOL(ret_event_handler);

static struct kretprobe rv_kretprobe = {
	.handler = ret_log_handler,
};

int rv_kretprobe_init(void)
{
	int ret;

	rv_kretprobe.kp.symbol_name = func_name;

	if (!strncmp(action_name, LOGGER_RV_BLOCK, sizeof(LOGGER_RV_BLOCK)))
		rv_kretprobe.handler = ret_block_handler;
	else if (!strncmp(action_name, LOGGER_RV_ABORT,
			  sizeof(LOGGER_RV_ABORT)))
		rv_kretprobe.handler = ret_abort_handler;
	else if (!strncmp(action_name, LOGGER_RV_EVENT,
			  sizeof(LOGGER_RV_EVENT)))
		rv_kretprobe.handler = ret_event_handler;
	else
		rv_kretprobe.handler = ret_log_handler;

	ret = register_kretprobe(&rv_kretprobe);
	if (ret < 0) {
		pr_err("register_kretprobe failed, returned %d\n", ret);
		return ret;
	}

	return 0;
}

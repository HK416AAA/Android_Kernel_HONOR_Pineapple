#ifndef __HONOR_THP_VM_NEW_H
#define __HONOR_THP_VM_NEW_H

#include "honor_thp.h"
#include "honor_thp_qts.h"

enum thp_vm_side {
	THP_VM_SIDE_PVM = 0,
	THP_VM_SIDE_TVM = 1,
};

enum thp_afe_notify_tp_status {
	THP_AFE_NOTIFY_TP_EXIT = 0,
	THP_AFE_NOTIFY_TP_ENTER = 1,
};

#define TUI_ENABLE 1
#define TUI_DISABLE 0

#define TUI_VM_REGISTER_SUCCESS_TVM 1
#define TUI_VM_REGISTER_SUCCESS_PVM 0

struct thp_vm_core {
#ifdef CONFIG_HONOR_TRUSTED_TOUCH
	struct qts_data qts_data;
	struct completion tp_ic_tui_entered;
	struct completion tp_ic_tui_exited;
	atomic_t tp_ic_tui_enabled;
	struct task_struct *tui_task;
	struct platform_device *platform_dev;
#else
	int nul;
#endif
};

#if (defined CONFIG_HONOR_TRUSTED_TOUCH) && (IS_ENABLED(CONFIG_HONOR_THP_QCOM))

int thp_vm_register(struct thp_core_data *cd);
void thp_vm_unregister(struct thp_core_data *cd);
int thp_vm_enter(struct thp_core_data *cd);
int thp_vm_exit(struct thp_core_data *cd);
void thp_vm_afe_notify(struct thp_core_data *cd,
		       enum thp_afe_notify_tp_status status);
int thp_vm_wait_ic_tui_exited(struct thp_core_data *cd);
bool thp_vm_afe_allowed(struct thp_core_data *cd);
bool thp_vm_tui_enabled(struct thp_core_data *cd);
int thp_vm_init_sysfs(struct thp_core_data *cd, struct kobject *kobj);

#else

static inline int thp_vm_register(struct thp_core_data *cd)
{
	return TUI_VM_REGISTER_SUCCESS_PVM;
}
static inline void thp_vm_unregister(struct thp_core_data *cd)
{
	return;
}
static inline int thp_vm_enter(struct thp_core_data *cd)
{
	return 0;
}
static inline int thp_vm_exit(struct thp_core_data *cd)
{
	return 0;
}
static inline void thp_vm_afe_notify(struct thp_core_data *cd,
				     enum thp_afe_notify_tp_status status)
{
	return;
}
static inline int thp_vm_wait_ic_tui_exited(struct thp_core_data *cd)
{
	return 0;
}
static inline bool thp_vm_afe_allowed(struct thp_core_data *cd)
{
	return true;
}
static inline bool thp_vm_tui_enabled(struct thp_core_data *cd)
{
	return false;
}
static inline int thp_vm_init_sysfs(struct thp_core_data *cd,
				    struct kobject *kobj)
{
	return 0;
}

#endif

#define thp_vm_log_err(cd, msg, ...)                                       \
	do {                                                               \
		if (cd) {                                                  \
			if (cd->multi_panel_index == SUB_TOUCH_PANEL) {    \
				printk(KERN_ERR "[E/THP1_VM] " msg,        \
				       ##__VA_ARGS__);                     \
			} else {                                           \
				printk(KERN_ERR "[E/THP0_VM] " msg,        \
				       ##__VA_ARGS__);                     \
			}                                                  \
		} else {                                                   \
			printk(KERN_ERR "[E/THP_VM] " msg, ##__VA_ARGS__); \
		}                                                          \
	} while (0)
#define thp_vm_log_info(cd, msg, ...)                                       \
	do {                                                                \
		if (cd) {                                                   \
			if (cd->multi_panel_index == SUB_TOUCH_PANEL) {     \
				printk(KERN_INFO "[I/THP1_VM] " msg,        \
				       ##__VA_ARGS__);                      \
			} else {                                            \
				printk(KERN_INFO "[I/THP0_VM] " msg,        \
				       ##__VA_ARGS__);                      \
			}                                                   \
		} else {                                                    \
			printk(KERN_INFO "[I/THP_VM] " msg, ##__VA_ARGS__); \
		}                                                           \
	} while (0)
#define thp_vm_log_debug(cd, msg, ...)                                       \
	do {                                                                 \
		if (g_thp_log_cfg) {                                         \
			if (cd) {                                            \
				if (cd->multi_panel_index ==                 \
				    SUB_TOUCH_PANEL) {                       \
					printk(KERN_INFO "[D/THP1_VM] " msg, \
					       ##__VA_ARGS__);               \
				} else {                                     \
					printk(KERN_INFO "[D/THP0_VM] " msg, \
					       ##__VA_ARGS__);               \
				}                                            \
			} else {                                             \
				printk(KERN_INFO "[D/THP_VM] " msg,          \
				       ##__VA_ARGS__);                       \
			}                                                    \
		}                                                            \
	} while (0)

#endif // (defined CONFIG_HONOR_TRUSTED_TOUCH) && (IS_ENABLED(CONFIG_HONOR_THP_QCOM))

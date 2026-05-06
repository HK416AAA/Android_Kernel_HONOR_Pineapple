#ifndef __HONOR_THP_QTS_H
#define __HONOR_THP_QTS_H

#include <linux/interrupt.h>

#if (defined CONFIG_HONOR_TRUSTED_TOUCH) && (IS_ENABLED(CONFIG_HONOR_THP_QCOM))
#include "linux/gunyah/gh_msgq.h"
#include "linux/gunyah/gh_rm_drv.h"
#include <linux/gunyah/gh_irq_lend.h>
#include <linux/gunyah/gh_mem_notifier.h>
#include <linux/kernel.h>

extern u8 g_thp_log_cfg;
struct thp_core_data;

enum qts_trusted_touch_mode_config {
	QTS_TRUSTED_TOUCH_VM_MODE,
	QTS_TRUSTED_TOUCH_MODE_NONE
};

enum qts_client {
	QTS_CLIENT_INVALID_TOUCH,
	QTS_CLIENT_PRIMARY_TOUCH,
	QTS_CLIENT_SECONDARY_TOUCH,
	QTS_CLIENT_MAX,
};

#define QTS_TRUSTED_TOUCH_EXTRA_MSGQ_PRIMARY 0x30
#define QTS_TRUSTED_TOUCH_EXTRA_MSGQ_SECONDARY 0x31
#define DEFAULT_QTS_TRUSTED_TOUCH_MSGQ 0x24

struct qts_vendor_callback_ops {
	int (*enable_touch_irq)(struct thp_core_data *cd, bool en);
	irqreturn_t (*irq_handler)(int irq, struct thp_core_data *cd);
	int (*get_irq_num)(struct thp_core_data *cd);
	int (*pre_la_tui_enable)(struct thp_core_data *cd);
	int (*post_la_tui_enable)(struct thp_core_data *cd);
	int (*pre_la_tui_disable)(struct thp_core_data *cd);
	int (*post_la_tui_disable)(struct thp_core_data *cd);
	int (*pre_le_tui_enable)(struct thp_core_data *cd);
	int (*post_le_tui_enable)(struct thp_core_data *cd);
	int (*pre_le_tui_disable)(struct thp_core_data *cd);
	int (*post_le_tui_disable)(struct thp_core_data *cd);
	void (*tui_abort)(struct thp_core_data *cd);
};

struct qts_trusted_touch_vm_info {
	enum gh_irq_label irq_label;
	enum gh_mem_notifier_tag mem_tag;
	enum gh_vm_names vm_name;
	const char *trusted_touch_type;
	u32 hw_irq;
	gh_memparcel_handle_t vm_mem_handle;
	u32 *iomem_bases;
	u32 *iomem_sizes;
	u32 iomem_list_size;
	void *mem_cookie;
	atomic_t vm_state;
	spinlock_t spin_lock;
};

struct qts_data {
	struct spi_device *spi;
	struct device *dev;
	struct device_node *dp;
	struct thp_core_data *cd;
	struct device_node *dp_overlay;
	enum qts_client client_index;

	/* Resources */
	u32 irq_gpio;
	u32 irq_flag;
	u32 reset_gpio;
	u32 reset_gpio_flags;
	int irq;
	bool irq_disabled;

	struct mutex transition_lock;
	struct completion tvm_release_finished;

	/* vendor callback ops */
	struct qts_vendor_callback_ops *vendor_ops;

	/* TUI */
	bool tui_supported;
	struct qts_trusted_touch_vm_info vm_info;
	struct mutex qts_clk_io_ctrl_mutex;
	const char *touch_environment;
	struct clk *core_clk;
	struct clk *iface_clk;
	atomic_t trusted_touch_initialized;
	atomic_t trusted_touch_enabled;
	atomic_t trusted_touch_transition;
	atomic_t trusted_touch_event;
	atomic_t trusted_touch_abort_status;
	atomic_t delayed_tvm_probe_pending;
	atomic_t delayed_pvm_probe_pending;
	atomic_t trusted_touch_mode;
	atomic_t irq_state;

	/* extra item */
	void *extra_msgq;
};

#include "honor_thp.h"
#define thp_qts_log_err(cd, msg, ...)                                      \
	do {                                                               \
		if (cd) {                                                  \
			if (cd->multi_panel_index == SUB_TOUCH_PANEL) {    \
				printk(KERN_ERR "[E/THP1_QTS] " msg,       \
				       ##__VA_ARGS__);                     \
			} else {                                           \
				printk(KERN_ERR "[E/THP0_QTS] " msg,       \
				       ##__VA_ARGS__);                     \
			}                                                  \
		} else {                                                   \
			printk(KERN_ERR "[E/THP_VM] " msg, ##__VA_ARGS__); \
		}                                                          \
	} while (0)
#define thp_qts_log_info(cd, msg, ...)                                       \
	do {                                                                 \
		if (cd) {                                                    \
			if (cd->multi_panel_index == SUB_TOUCH_PANEL) {      \
				printk(KERN_INFO "[I/THP1_QTS] " msg,        \
				       ##__VA_ARGS__);                       \
			} else {                                             \
				printk(KERN_INFO "[I/THP0_QTS] " msg,        \
				       ##__VA_ARGS__);                       \
			}                                                    \
		} else {                                                     \
			printk(KERN_INFO "[I/THP_QTS] " msg, ##__VA_ARGS__); \
		}                                                            \
	} while (0)
#define thp_qts_log_debug(cd, msg, ...)                                       \
	do {                                                                  \
		if (g_thp_log_cfg) {                                          \
			if (cd) {                                             \
				if (cd->multi_panel_index ==                  \
				    SUB_TOUCH_PANEL) {                        \
					printk(KERN_INFO "[D/THP1_QTS] " msg, \
					       ##__VA_ARGS__);                \
				} else {                                      \
					printk(KERN_INFO "[D/THP0_QTS] " msg, \
					       ##__VA_ARGS__);                \
				}                                             \
			} else {                                              \
				printk(KERN_INFO "[D/THP_QTS] " msg,          \
				       ##__VA_ARGS__);                        \
			}                                                     \
		}                                                             \
	} while (0)

struct qts_vendor_data {
	struct thp_core_data *cd;
	struct qts_vendor_callback_ops *vendor_ops;
	struct device *device;
	struct spi_device *sdev;
};

struct thp_udfp_data;
int qts_client_register(struct qts_data *qts_data,
			struct qts_vendor_data *vendor_data);
void qts_client_unregister(struct qts_data *qts_data);
int qts_dt_overlay_register(struct qts_data *qts_data,
			    struct device_node *node_overlay);
int qts_tui_enter(struct qts_data *qts_data);
int qts_tui_exit(struct qts_data *qts_data);
int qts_tui_is_enable(struct qts_data *qts_data);
void qts_report_to_htee(struct qts_data *qts_data,
			struct thp_udfp_data *udfp_data);
int qts_extra_mesg_send(struct qts_data *qts_data, uint8_t *buf, int len);
int qts_extra_mesg_recv(struct qts_data *qts_data, uint8_t *buf, int len);

#endif
#endif

#include "honor_thp_qts.h"

#if (defined CONFIG_HONOR_TRUSTED_TOUCH) && (IS_ENABLED(CONFIG_HONOR_THP_QCOM))
#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/kobject.h>

#define QTS_NAME "qti-ts"

enum trusted_touch_pvm_states {
	TRUSTED_TOUCH_PVM_INIT,
	PVM_I2C_RESOURCE_ACQUIRED,
	PVM_INTERRUPT_DISABLED,
	PVM_IOMEM_LENT,
	PVM_IOMEM_LENT_NOTIFIED,
	PVM_IRQ_LENT,
	PVM_IRQ_LENT_NOTIFIED,
	PVM_IOMEM_RELEASE_NOTIFIED,
	PVM_IRQ_RELEASE_NOTIFIED,
	PVM_ALL_RESOURCES_RELEASE_NOTIFIED,
	PVM_IRQ_RECLAIMED,
	PVM_IOMEM_RECLAIMED,
	PVM_INTERRUPT_ENABLED,
	PVM_I2C_RESOURCE_RELEASED,
	TRUSTED_TOUCH_PVM_STATE_MAX
};

enum trusted_touch_tvm_states {
	TRUSTED_TOUCH_TVM_INIT,
	TVM_IOMEM_LENT_NOTIFIED,
	TVM_IRQ_LENT_NOTIFIED,
	TVM_ALL_RESOURCES_LENT_NOTIFIED,
	TVM_IOMEM_ACCEPTED,
	TVM_I2C_SESSION_ACQUIRED,
	TVM_IRQ_ACCEPTED,
	TVM_INTERRUPT_ENABLED,
	TVM_INTERRUPT_DISABLED,
	TVM_IRQ_RELEASED,
	TVM_I2C_SESSION_RELEASED,
	TVM_IOMEM_RELEASED,
	TRUSTED_TOUCH_TVM_STATE_MAX
};

#define TRUSTED_TOUCH_MEM_LABEL 0x7

#define TRUSTED_TOUCH_EVENT_LEND_FAILURE -1
#define TRUSTED_TOUCH_EVENT_LEND_NOTIFICATION_FAILURE -2
#define TRUSTED_TOUCH_EVENT_ACCEPT_FAILURE -3
#define TRUSTED_TOUCH_EVENT_FUNCTIONAL_FAILURE -4
#define TRUSTED_TOUCH_EVENT_RELEASE_FAILURE -5
#define TRUSTED_TOUCH_EVENT_RECLAIM_FAILURE -6
#define TRUSTED_TOUCH_EVENT_I2C_FAILURE -7
#define TRUSTED_TOUCH_EVENT_NOTIFICATIONS_PENDING 5

#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/sort.h>
#include <linux/atomic.h>
#include <linux/pinctrl/qcom-pinctrl.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/component.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/regulator/consumer.h>
#include <linux/debugfs.h>
#include <linux/wait.h>
#include <linux/time.h>

static void qts_trusted_touch_abort_handler(struct qts_data *qts_data,
					    int error);

static int g_trusted_touch_msgq = DEFAULT_QTS_TRUSTED_TOUCH_MSGQ;
static void *g_input_msgq;

static struct gh_acl_desc *qts_vm_get_acl(enum gh_vm_names vm_name)
{
	struct gh_acl_desc *acl_desc;
	gh_vmid_t vmid;

#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
	ghd_rm_get_vmid(vm_name, &vmid);
#else
	gh_rm_get_vmid(vm_name, &vmid);
#endif

	acl_desc = kzalloc(offsetof(struct gh_acl_desc, acl_entries[1]),
			   GFP_KERNEL);
	if (!acl_desc)
		return ERR_PTR(ENOMEM);

	acl_desc->n_acl_entries = 1;
	acl_desc->acl_entries[0].vmid = vmid;
	acl_desc->acl_entries[0].perms = GH_RM_ACL_R | GH_RM_ACL_W;

	return acl_desc;
}

static struct gh_sgl_desc *
qts_vm_get_sgl(struct qts_trusted_touch_vm_info *vm_info)
{
	struct gh_sgl_desc *sgl_desc;
	int i;

	sgl_desc = kzalloc(offsetof(struct gh_sgl_desc,
				    sgl_entries[vm_info->iomem_list_size]),
			   GFP_KERNEL);
	if (!sgl_desc)
		return ERR_PTR(ENOMEM);

	sgl_desc->n_sgl_entries = vm_info->iomem_list_size;

	for (i = 0; i < vm_info->iomem_list_size; i++) {
		sgl_desc->sgl_entries[i].ipa_base = vm_info->iomem_bases[i];
		sgl_desc->sgl_entries[i].size = vm_info->iomem_sizes[i];
	}

	return sgl_desc;
}

static int qts_populate_vm_info_iomem(struct qts_data *qts_data)
{
	int i, gpio, rc = 0;
	int num_regs, num_sizes, num_gpios, list_size;
	struct resource res;
	struct device_node *np = qts_data->dev->of_node;
	struct qts_trusted_touch_vm_info *vm_info = &qts_data->vm_info;

	num_regs = of_property_count_u32_elems(np, "trusted-touch-io-bases");
	if (num_regs < 0) {
		thp_qts_log_err(qts_data->cd,
				"Invalid number of IO regions specified\n");
		return -EINVAL;
	}

	num_sizes = of_property_count_u32_elems(np, "trusted-touch-io-sizes");
	if (num_sizes < 0) {
		thp_qts_log_err(qts_data->cd,
				"Invalid number of IO regions specified\n");
		return -EINVAL;
	}

	if (num_regs != num_sizes) {
		thp_qts_log_err(qts_data->cd,
				"IO bases and sizes array lengths mismatch\n");
		return -EINVAL;
	}

	num_gpios = of_gpio_named_count(np, "trusted-touch-vm-gpio-list");
	if (num_gpios < 0) {
		thp_qts_log_info(qts_data->cd,
				 "Ignoring invalid trusted gpio list: %d\n",
				 num_gpios);
		num_gpios = 0;
	}

	list_size = num_regs + num_gpios;
	vm_info->iomem_list_size = list_size;
	vm_info->iomem_bases = devm_kcalloc(qts_data->dev, list_size,
					    sizeof(*vm_info->iomem_bases),
					    GFP_KERNEL);
	if (!vm_info->iomem_bases)
		return -ENOMEM;

	vm_info->iomem_sizes = devm_kcalloc(qts_data->dev, list_size,
					    sizeof(*vm_info->iomem_sizes),
					    GFP_KERNEL);
	if (!vm_info->iomem_sizes)
		return -ENOMEM;

	for (i = 0; i < num_gpios; ++i) {
		gpio = of_get_named_gpio(np, "trusted-touch-vm-gpio-list", i);
		if (gpio < 0 || !gpio_is_valid(gpio)) {
			thp_qts_log_err(qts_data->cd,
					"Invalid gpio %d at position %d\n",
					gpio, i);
			return gpio;
		}

		if (!msm_gpio_get_pin_address(gpio, &res)) {
			thp_qts_log_err(qts_data->cd,
					"Failed to retrieve gpio-%d resource\n",
					gpio);
			return -ENODATA;
		}

		vm_info->iomem_bases[i] = res.start;
		vm_info->iomem_sizes[i] = resource_size(&res);
	}

	rc = of_property_read_u32_array(np, "trusted-touch-io-bases",
					&vm_info->iomem_bases[i],
					list_size - i);
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"Failed to read trusted touch io bases:%d\n",
				rc);
		return rc;
	}

	rc = of_property_read_u32_array(np, "trusted-touch-io-sizes",
					&vm_info->iomem_sizes[i],
					list_size - i);
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"Failed to read trusted touch io sizes:%d\n",
				rc);
		return rc;
	}

	return 0;
}

static int qts_populate_vm_info(struct qts_data *qts_data)
{
	int rc;
	struct qts_trusted_touch_vm_info *vm_info;
	struct device_node *np = qts_data->dp;

	vm_info = &qts_data->vm_info;
	vm_info->vm_name = GH_TRUSTED_VM;
	rc = of_property_read_u32(np, "trusted-touch-spi-irq",
				  &vm_info->hw_irq);
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"Failed to read trusted touch SPI irq:%d\n",
				rc);
		return rc;
	}

	rc = qts_populate_vm_info_iomem(qts_data);
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"Failed to read trusted touch mmio ranges:%d\n",
				rc);
		return rc;
	}

	rc = of_property_read_string(np, "trusted-touch-type",
				     &vm_info->trusted_touch_type);
	if (rc) {
		thp_qts_log_info(qts_data->cd,
				 "No trusted touch type selection made\n");
		vm_info->mem_tag = GH_MEM_NOTIFIER_TAG_TOUCH_PRIMARY;
		vm_info->irq_label = GH_IRQ_LABEL_TRUSTED_TOUCH_PRIMARY;
		rc = 0;
	} else if (!strcmp(vm_info->trusted_touch_type, "primary")) {
		vm_info->mem_tag = GH_MEM_NOTIFIER_TAG_TOUCH_PRIMARY;
		vm_info->irq_label = GH_IRQ_LABEL_TRUSTED_TOUCH_PRIMARY;
		qts_data->client_index = QTS_CLIENT_PRIMARY_TOUCH;
	} else if (!strcmp(vm_info->trusted_touch_type, "secondary")) {
		vm_info->mem_tag = GH_MEM_NOTIFIER_TAG_TOUCH_SECONDARY;
		vm_info->irq_label = GH_IRQ_LABEL_TRUSTED_TOUCH_SECONDARY;
		qts_data->client_index = QTS_CLIENT_SECONDARY_TOUCH;
	} else {
		thp_qts_log_err(qts_data->cd, "invalid trusted_touch_type\n");
		qts_data->client_index = QTS_CLIENT_INVALID_TOUCH;
	}

	return 0;
}

static void qts_destroy_vm_info(struct qts_data *qts_data)
{
	kfree(qts_data->vm_info.iomem_sizes);
	kfree(qts_data->vm_info.iomem_bases);
}

static void qts_vm_deinit(struct qts_data *qts_data)
{
	if (qts_data->vm_info.mem_cookie)
		gh_mem_notifier_unregister(qts_data->vm_info.mem_cookie);
	qts_destroy_vm_info(qts_data);
}

static int qts_trusted_touch_get_vm_state(struct qts_data *qts_data)
{
	return atomic_read(&qts_data->vm_info.vm_state);
}

static void qts_trusted_touch_set_vm_state(struct qts_data *qts_data, int state)
{
	thp_qts_log_debug(qts_data->cd, "state %d\n", state);
	atomic_set(&qts_data->vm_info.vm_state, state);
}

#ifdef CONFIG_ARCH_QTI_VM
static int qts_vm_mem_release(struct qts_data *qts_data);
static void qts_trusted_touch_tvm_vm_mode_disable(struct qts_data *qts_data);
static void qts_trusted_touch_abort_tvm(struct qts_data *qts_data);
static void qts_trusted_touch_event_notify(struct qts_data *qts_data,
					   int event);
static void qts_dt_parse_overlay(struct qts_data *qts_data);

static void qts_irq_enable(struct qts_data *qts_data, bool en)
{
	if (en) {
		if (qts_data->irq_disabled) {
			thp_qts_log_info(qts_data->cd, "qts irq enable\n");
			enable_irq(qts_data->irq);
			qts_data->irq_disabled = false;
		}
	} else {
		if (!qts_data->irq_disabled) {
			thp_qts_log_info(qts_data->cd, "qts irq disable\n");
			disable_irq_nosync(qts_data->irq);
			qts_data->irq_disabled = true;
		}
	}
}

static irqreturn_t qts_irq_handler(int irq, void *data)
{
	struct qts_data *qts_data = data;

	if (!mutex_trylock(&qts_data->transition_lock))
		return IRQ_HANDLED;

	qts_irq_enable(qts_data, false);
	qts_data->vendor_ops->irq_handler(irq, qts_data->cd);
	qts_irq_enable(qts_data, true);
	mutex_unlock(&qts_data->transition_lock);
	return IRQ_HANDLED;
}

static int qts_irq_registration(struct qts_data *qts_data)
{
	int ret = 0;

	thp_qts_log_debug(qts_data->cd, "irq:%d, flag:%x\n", qts_data->irq,
			  qts_data->irq_flag);
	ret = request_threaded_irq(qts_data->irq, NULL, qts_irq_handler,
				   qts_data->irq_flag | IRQF_ONESHOT, QTS_NAME,
				   qts_data);
	if (ret != 0)
		thp_qts_log_err(qts_data->cd, "request_threaded_irq failed\n");
	if (ret == 0)
		qts_irq_enable(qts_data, false);

	return ret;
}

static int qts_sgl_cmp(const void *a, const void *b)
{
	struct gh_sgl_entry *left = (struct gh_sgl_entry *)a;
	struct gh_sgl_entry *right = (struct gh_sgl_entry *)b;

	return (left->ipa_base - right->ipa_base);
}

static int qts_vm_compare_sgl_desc(struct qts_data *qts_data,
				   struct gh_sgl_desc *expected,
				   struct gh_sgl_desc *received)
{
	int idx;

	if (expected->n_sgl_entries != received->n_sgl_entries)
		return -E2BIG;
	sort(received->sgl_entries, received->n_sgl_entries,
	     sizeof(received->sgl_entries[0]), qts_sgl_cmp, NULL);
	sort(expected->sgl_entries, expected->n_sgl_entries,
	     sizeof(expected->sgl_entries[0]), qts_sgl_cmp, NULL);

	for (idx = 0; idx < expected->n_sgl_entries; idx++) {
		struct gh_sgl_entry *left = &expected->sgl_entries[idx];
		struct gh_sgl_entry *right = &received->sgl_entries[idx];

		if ((left->ipa_base != right->ipa_base) ||
		    (left->size != right->size)) {
			thp_qts_log_err(
				qts_data->cd,
				"sgl mismatch: left_base:%d right base:%d left size:%d right size:%d\n",
				left->ipa_base, right->ipa_base, left->size,
				right->size);

			return -EINVAL;
		}
	}
	return 0;
}

static int qts_vm_handle_vm_hardware(struct qts_data *qts_data)
{
	int rc = 0;

	if (atomic_read(&qts_data->delayed_tvm_probe_pending)) {
		qts_dt_parse_overlay(qts_data);
		rc = qts_irq_registration(qts_data);
		if (rc) {
			thp_qts_log_err(qts_data->cd,
					"irq registration failure on TVM!\n");
			return rc;
		}
		atomic_set(&qts_data->delayed_tvm_probe_pending, 0);
	}
	qts_irq_enable(qts_data, true);
	qts_trusted_touch_set_vm_state(qts_data, TVM_INTERRUPT_ENABLED);
	return rc;
}

static void qts_trusted_touch_tvm_vm_mode_enable(struct qts_data *qts_data)
{
	struct gh_sgl_desc *sgl_desc, *expected_sgl_desc;
	struct gh_acl_desc *acl_desc;
	struct irq_data *irq_data;
	int rc = 0;
	int irq = 0;

	/* make sure all resources is ready */
	mutex_lock(&qts_data->transition_lock);
	atomic_set(&qts_data->trusted_touch_transition, 1);
	if (qts_trusted_touch_get_vm_state(qts_data) !=
	    TVM_ALL_RESOURCES_LENT_NOTIFIED) {
		thp_qts_log_info(qts_data->cd,
				 "All lend notifications not received\n");
		qts_trusted_touch_event_notify(
			qts_data, TRUSTED_TOUCH_EVENT_NOTIFICATIONS_PENDING);
		mutex_unlock(&qts_data->transition_lock);
		return;
	}

	if (qts_data->vendor_ops->pre_le_tui_enable)
		qts_data->vendor_ops->pre_le_tui_enable(qts_data->cd);

	acl_desc = qts_vm_get_acl(GH_TRUSTED_VM);
	if (IS_ERR(acl_desc)) {
		thp_qts_log_err(qts_data->cd,
				"failed to populated acl data:rc=%d\n",
				PTR_ERR(acl_desc));
		goto accept_fail;
	}

	/* accept mem */
	sgl_desc = gh_rm_mem_accept(
		qts_data->vm_info.vm_mem_handle, GH_RM_MEM_TYPE_IO,
		GH_RM_TRANS_TYPE_LEND,
		GH_RM_MEM_ACCEPT_VALIDATE_ACL_ATTRS |
			GH_RM_MEM_ACCEPT_VALIDATE_LABEL | GH_RM_MEM_ACCEPT_DONE,
		TRUSTED_TOUCH_MEM_LABEL, acl_desc, NULL, NULL, 0);
	if (IS_ERR_OR_NULL(sgl_desc)) {
		thp_qts_log_err(qts_data->cd,
				"failed to do mem accept :rc=%d\n",
				PTR_ERR(sgl_desc));
		goto acl_fail;
	}
	qts_trusted_touch_set_vm_state(qts_data, TVM_IOMEM_ACCEPTED);

	/* Initiate session on tvm */
	rc = pm_runtime_get_sync(qts_data->spi->master->dev.parent);
	if (rc < 0) {
		thp_qts_log_err(qts_data->cd, "failed to get sync rc:%d\n", rc);
		goto acl_fail;
	}
	qts_trusted_touch_set_vm_state(qts_data, TVM_I2C_SESSION_ACQUIRED);

	expected_sgl_desc = qts_vm_get_sgl(&qts_data->vm_info);
	if (qts_vm_compare_sgl_desc(qts_data, expected_sgl_desc, sgl_desc)) {
		thp_qts_log_err(qts_data->cd, "IO sg list does not match\n");
		goto sgl_cmp_fail;
	}

	kfree(expected_sgl_desc);
	kfree(acl_desc);

	irq = gh_irq_accept(qts_data->vm_info.irq_label, -1,
			    qts_data->irq_flag);
	if (irq < 0) {
		thp_qts_log_err(qts_data->cd, "failed to accept irq\n");
		goto accept_fail;
	}
	qts_trusted_touch_set_vm_state(qts_data, TVM_IRQ_ACCEPTED);

	irq_data = irq_get_irq_data(irq);
	if (!irq_data) {
		thp_qts_log_err(qts_data->cd,
				"Invalid irq data for trusted touch\n");
		goto accept_fail;
	}
	if (!irq_data->hwirq) {
		thp_qts_log_err(qts_data->cd, "Invalid irq in irq data\n");
		goto accept_fail;
	}
	if (irq_data->hwirq != qts_data->vm_info.hw_irq) {
		thp_qts_log_err(qts_data->cd, "Invalid irq lent\n");
		goto accept_fail;
	}

	thp_qts_log_info(qts_data->cd, "irq:returned from accept:%d\n", irq);
	qts_data->irq = irq;

	rc = qts_vm_handle_vm_hardware(qts_data);
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"Delayed probe failure on TVM!\n");
		goto accept_fail;
	}

	/* tui enter success */
	atomic_set(&qts_data->trusted_touch_enabled, 1);
	if (qts_data->vendor_ops->post_le_tui_enable)
		qts_data->vendor_ops->post_le_tui_enable(qts_data->cd);

	thp_qts_log_info(qts_data->cd,
			 "Irq, iomem are accepted and trusted touch enabled\n");
	atomic_set(&qts_data->trusted_touch_transition, 0);
	mutex_unlock(&qts_data->transition_lock);
	return;
sgl_cmp_fail:
	kfree(expected_sgl_desc);
acl_fail:
	kfree(acl_desc);
accept_fail:
	qts_trusted_touch_abort_handler(qts_data,
					TRUSTED_TOUCH_EVENT_ACCEPT_FAILURE);
	atomic_set(&qts_data->trusted_touch_transition, 0);
	mutex_unlock(&qts_data->transition_lock);
}

static void qts_vm_irq_on_lend_callback(void *data, unsigned long notif_type,
					enum gh_irq_label label)
{
	struct qts_data *qts_data = data;
	unsigned long flags;

	thp_qts_log_debug(qts_data->cd,
			  "received irq lend request for label:%d\n", label);
	spin_lock_irqsave(&qts_data->vm_info.spin_lock, flags);
	if (qts_trusted_touch_get_vm_state(qts_data) == TVM_IOMEM_LENT_NOTIFIED)
		qts_trusted_touch_set_vm_state(qts_data,
					       TVM_ALL_RESOURCES_LENT_NOTIFIED);
	else
		qts_trusted_touch_set_vm_state(qts_data, TVM_IRQ_LENT_NOTIFIED);
	spin_unlock_irqrestore(&qts_data->vm_info.spin_lock, flags);
}

static void qts_vm_mem_on_lend_handler(enum gh_mem_notifier_tag tag,
				       unsigned long notif_type,
				       void *entry_data, void *notif_msg)
{
	struct gh_rm_notif_mem_shared_payload *payload;
	struct qts_trusted_touch_vm_info *vm_info;
	struct qts_data *qts_data;
	unsigned long flags;

	qts_data = (struct qts_data *)entry_data;
	vm_info = &qts_data->vm_info;
	if (!vm_info) {
		thp_qts_log_err(qts_data->cd, "Invalid vm_info\n");
		return;
	}

	if (notif_type != GH_RM_NOTIF_MEM_SHARED || tag != vm_info->mem_tag) {
		thp_qts_log_err(qts_data->cd,
				"Invalid command passed from rm\n");
		return;
	}

	if (!entry_data || !notif_msg) {
		thp_qts_log_err(qts_data->cd,
				"Invalid entry data passed from rm\n");
		return;
	}

	payload = (struct gh_rm_notif_mem_shared_payload *)notif_msg;
	if (payload->trans_type != GH_RM_TRANS_TYPE_LEND ||
	    payload->label != TRUSTED_TOUCH_MEM_LABEL) {
		thp_qts_log_err(qts_data->cd,
				"Invalid label or transaction type\n");
		return;
	}

	vm_info->vm_mem_handle = payload->mem_handle;
	thp_qts_log_info(qts_data->cd,
			 "received mem lend request with handle:%d\n",
			 vm_info->vm_mem_handle);
	spin_lock_irqsave(&qts_data->vm_info.spin_lock, flags);
	if (qts_trusted_touch_get_vm_state(qts_data) == TVM_IRQ_LENT_NOTIFIED)
		qts_trusted_touch_set_vm_state(qts_data,
					       TVM_ALL_RESOURCES_LENT_NOTIFIED);
	else
		qts_trusted_touch_set_vm_state(qts_data,
					       TVM_IOMEM_LENT_NOTIFIED);
	spin_unlock_irqrestore(&qts_data->vm_info.spin_lock, flags);
}

static int qts_vm_mem_release(struct qts_data *qts_data)
{
	int rc = 0;

	if (!qts_data->vm_info.vm_mem_handle) {
		thp_qts_log_err(qts_data->cd, "Invalid memory handle\n");
		return -EINVAL;
	}
	/* real release mem */
	rc = gh_rm_mem_release(qts_data->vm_info.vm_mem_handle, 0);
	if (rc)
		thp_qts_log_err(qts_data->cd, "VM mem release failed: rc=%d\n",
				rc);

	/* notify pvm */
	rc = gh_rm_mem_notify(qts_data->vm_info.vm_mem_handle,
			      GH_RM_MEM_NOTIFY_OWNER_RELEASED,
			      qts_data->vm_info.mem_tag, 0);
	if (rc)
		thp_qts_log_err(qts_data->cd,
				"Failed to notify mem release to PVM: rc=%d\n",
				rc);

	thp_qts_log_info(qts_data->cd, "vm mem release success\n");

	qts_data->vm_info.vm_mem_handle = 0;
	return rc;
}

static void qts_trusted_touch_tvm_vm_mode_disable(struct qts_data *qts_data)
{
	int rc = 0;

	mutex_lock(&qts_data->transition_lock);
	atomic_set(&qts_data->trusted_touch_transition, 1);
	if (atomic_read(&qts_data->trusted_touch_abort_status)) {
		qts_trusted_touch_abort_tvm(qts_data);
		mutex_unlock(&qts_data->transition_lock);
		return;
	}

	if (qts_data->vendor_ops->pre_le_tui_disable)
		qts_data->vendor_ops->pre_le_tui_disable(qts_data->cd);

	/* disable irq */
	qts_irq_enable(qts_data, false);
	qts_trusted_touch_set_vm_state(qts_data, TVM_INTERRUPT_DISABLED);

	/* release irq */
	rc = gh_irq_release(qts_data->vm_info.irq_label);
	if (rc) {
		thp_qts_log_err(qts_data->cd, "Failed to release irq rc:%d\n",
				rc);
		goto error;
	} else {
		qts_trusted_touch_set_vm_state(qts_data, TVM_IRQ_RELEASED);
	}

	/* notify pvm */
	rc = gh_irq_release_notify(qts_data->vm_info.irq_label);
	if (rc)
		thp_qts_log_err(qts_data->cd,
				"Failed to notify release irq rc:%d\n", rc);
	thp_qts_log_info(qts_data->cd, "vm irq release success\n");

	pm_runtime_put_sync(qts_data->spi->master->dev.parent);

	qts_trusted_touch_set_vm_state(qts_data, TVM_I2C_SESSION_RELEASED);

	/* release mem */
	rc = qts_vm_mem_release(qts_data);
	if (rc) {
		thp_qts_log_err(qts_data->cd, "Failed to release mem rc:%d\n",
				rc);
		goto error;
	} else {
		qts_trusted_touch_set_vm_state(qts_data, TVM_IOMEM_RELEASED);
	}
	qts_trusted_touch_set_vm_state(qts_data, TRUSTED_TOUCH_TVM_INIT);
	atomic_set(&qts_data->trusted_touch_enabled, 0);

	if (qts_data->vendor_ops->post_le_tui_disable)
		qts_data->vendor_ops->post_le_tui_disable(qts_data->cd);

	thp_qts_log_info(
		qts_data->cd,
		"Irq, iomem are released and trusted touch disabled\n");

	/* every thing is ok, unlock*/
	mutex_unlock(&qts_data->transition_lock);
	return;
error:
	qts_trusted_touch_abort_handler(qts_data,
					TRUSTED_TOUCH_EVENT_RELEASE_FAILURE);
	mutex_unlock(&qts_data->transition_lock);
}

static int qts_handle_trusted_touch_tvm(struct qts_data *qts_data, int value)
{
	int err = 0;

	switch (value) {
	case 0:
		if ((atomic_read(&qts_data->trusted_touch_enabled) == 0) &&
		    (atomic_read(&qts_data->trusted_touch_abort_status) == 0)) {
			thp_qts_log_err(qts_data->cd,
					"Trusted touch is already disabled\n");
			break;
		}
		if (atomic_read(&qts_data->trusted_touch_mode) ==
		    QTS_TRUSTED_TOUCH_VM_MODE) {
			qts_trusted_touch_tvm_vm_mode_disable(qts_data);
		} else {
			thp_qts_log_err(qts_data->cd,
					"Unsupported trusted touch mode\n");
		}
		break;

	case 1:
		if (atomic_read(&qts_data->trusted_touch_enabled)) {
			thp_qts_log_err(qts_data->cd,
					"Trusted touch usecase underway\n");
			err = -EBUSY;
			break;
		}
		if (atomic_read(&qts_data->trusted_touch_mode) ==
		    QTS_TRUSTED_TOUCH_VM_MODE) {
			qts_trusted_touch_tvm_vm_mode_enable(qts_data);
		} else {
			thp_qts_log_err(qts_data->cd,
					"Unsupported trusted touch mode\n");
		}
		break;

	default:
		thp_qts_log_err(qts_data->cd, "unsupported value: %lu\n",
				value);
		err = -EINVAL;
		break;
	}

	return err;
}

static void qts_trusted_touch_abort_tvm(struct qts_data *qts_data)
{
	int rc = 0;
	int vm_state = qts_trusted_touch_get_vm_state(qts_data);

	if (vm_state >= TRUSTED_TOUCH_TVM_STATE_MAX) {
		thp_qts_log_err(qts_data->cd, "invalid tvm driver state: %d\n",
				vm_state);
		return;
	}

	switch (vm_state) {
	case TVM_INTERRUPT_ENABLED:
		qts_irq_enable(qts_data, false);
		fallthrough;
	case TVM_IRQ_ACCEPTED:
	case TVM_INTERRUPT_DISABLED:
		rc = gh_irq_release(qts_data->vm_info.irq_label);
		if (rc)
			thp_qts_log_err(qts_data->cd,
					"Failed to release irq rc:%d\n", rc);
		rc = gh_irq_release_notify(qts_data->vm_info.irq_label);
		if (rc)
			thp_qts_log_err(qts_data->cd,
					"Failed to notify irq release rc:%d\n",
					rc);
		fallthrough;
	case TVM_I2C_SESSION_ACQUIRED:
	case TVM_IOMEM_ACCEPTED:
	case TVM_IRQ_RELEASED:
		pm_runtime_put_sync(qts_data->spi->master->dev.parent);
		fallthrough;
	case TVM_I2C_SESSION_RELEASED:
		rc = qts_vm_mem_release(qts_data);
		if (rc)
			thp_qts_log_err(qts_data->cd,
					"Failed to release mem rc:%d\n", rc);
		fallthrough;
	case TVM_IOMEM_RELEASED:
	case TVM_ALL_RESOURCES_LENT_NOTIFIED:
	case TRUSTED_TOUCH_TVM_INIT:
	case TVM_IRQ_LENT_NOTIFIED:
	case TVM_IOMEM_LENT_NOTIFIED:
		atomic_set(&qts_data->trusted_touch_enabled, 0);
	}

	atomic_set(&qts_data->trusted_touch_abort_status, 0);
	qts_trusted_touch_set_vm_state(qts_data, TRUSTED_TOUCH_TVM_INIT);
}

static void qts_dt_parse_overlay(struct qts_data *qts_data)
{
	unsigned int irq_flag;
	int rc;
	struct device_node *np = qts_data->dp_overlay;

	if (!np) {
		thp_qts_log_err(qts_data->cd, "%s dp_overlay not set\n",
				__func__);
		return;
	}

	rc = of_property_read_u32(np, "tui_irq_flag", &irq_flag);
	if (!rc) {
		thp_qts_log_info(qts_data->cd, "%s tui_irq_flag overlay\n",
				 __func__);
		qts_data->irq_flag = irq_flag;
	}
}

#else

static void qts_bus_put(struct qts_data *qts_data);

static void qts_trusted_touch_abort_pvm(struct qts_data *qts_data)
{
	int rc = 0;
	int vm_state = qts_trusted_touch_get_vm_state(qts_data);

	if (vm_state >= TRUSTED_TOUCH_PVM_STATE_MAX) {
		thp_qts_log_err(qts_data->cd, "Invalid driver state: %d\n",
				vm_state);
		return;
	}

	switch (vm_state) {
	case PVM_IRQ_RELEASE_NOTIFIED:
	case PVM_ALL_RESOURCES_RELEASE_NOTIFIED:
	case PVM_IRQ_LENT:
	case PVM_IRQ_LENT_NOTIFIED:
		rc = gh_irq_reclaim(qts_data->vm_info.irq_label);
		if (rc) {
			thp_qts_log_err(qts_data->cd,
					"failed to reclaim irq on pvm rc:%d\n",
					rc);
			return;
		}
		fallthrough;
	case PVM_IRQ_RECLAIMED:
	case PVM_IOMEM_LENT:
	case PVM_IOMEM_LENT_NOTIFIED:
	case PVM_IOMEM_RELEASE_NOTIFIED:
#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
		rc = ghd_rm_mem_reclaim(qts_data->vm_info.vm_mem_handle, 0);
#else
		rc = gh_rm_mem_reclaim(qts_data->vm_info.vm_mem_handle, 0);
#endif
		if (rc) {
			thp_qts_log_err(
				qts_data->cd,
				"failed to reclaim iomem on pvm rc:%d\n", rc);
			qts_trusted_touch_set_vm_state(
				qts_data, PVM_IOMEM_RELEASE_NOTIFIED);
			return;
		}
		qts_data->vm_info.vm_mem_handle = 0;
		fallthrough;
	case PVM_IOMEM_RECLAIMED:
	case PVM_INTERRUPT_DISABLED:
		if (qts_data->vendor_ops->enable_touch_irq)
			qts_data->vendor_ops->enable_touch_irq(qts_data->cd,
							       true);
		fallthrough;
	case PVM_I2C_RESOURCE_ACQUIRED:
	case PVM_INTERRUPT_ENABLED:
		qts_bus_put(qts_data);
		fallthrough;
	case TRUSTED_TOUCH_PVM_INIT:
	case PVM_I2C_RESOURCE_RELEASED:
		atomic_set(&qts_data->trusted_touch_enabled, 0);
		atomic_set(&qts_data->trusted_touch_transition, 0);
	}

	atomic_set(&qts_data->trusted_touch_abort_status, 0);

	qts_trusted_touch_set_vm_state(qts_data, TRUSTED_TOUCH_PVM_INIT);
}

static int qts_clk_prepare_enable(struct qts_data *qts_data)
{
	int ret;

	ret = clk_prepare_enable(qts_data->iface_clk);
	if (ret) {
		thp_qts_log_err(qts_data->cd,
				"error on clk_prepare_enable(iface_clk):%d\n",
				ret);
		return ret;
	}

	ret = clk_prepare_enable(qts_data->core_clk);
	if (ret) {
		clk_disable_unprepare(qts_data->iface_clk);
		thp_qts_log_err(qts_data->cd,
				"error clk_prepare_enable(core_clk):%d\n", ret);
	}
	return ret;
}

static void qts_clk_disable_unprepare(struct qts_data *qts_data)
{
	clk_disable_unprepare(qts_data->core_clk);
	clk_disable_unprepare(qts_data->iface_clk);
}

static int qts_bus_get(struct qts_data *qts_data)
{
	int rc = 0;
	struct device *dev = NULL;

	dev = qts_data->spi->master->dev.parent;

	mutex_lock(&qts_data->qts_clk_io_ctrl_mutex);
	rc = pm_runtime_get_sync(dev);
	if (rc >= 0 && qts_data->core_clk != NULL &&
	    qts_data->iface_clk != NULL) {
		rc = qts_clk_prepare_enable(qts_data);
		if (rc)
			pm_runtime_put_sync(dev);
	}

	mutex_unlock(&qts_data->qts_clk_io_ctrl_mutex);
	return rc;
}

static void qts_bus_put(struct qts_data *qts_data)
{
	struct device *dev = NULL;

	dev = qts_data->spi->master->dev.parent;

	mutex_lock(&qts_data->qts_clk_io_ctrl_mutex);
	if (qts_data->core_clk != NULL && qts_data->iface_clk != NULL)
		qts_clk_disable_unprepare(qts_data);
	pm_runtime_put_sync(dev);
	mutex_unlock(&qts_data->qts_clk_io_ctrl_mutex);
}

static struct gh_notify_vmid_desc *qts_vm_get_vmid(gh_vmid_t vmid)
{
	struct gh_notify_vmid_desc *vmid_desc;

	vmid_desc =
		kzalloc(offsetof(struct gh_notify_vmid_desc, vmid_entries[1]),
			GFP_KERNEL);
	if (!vmid_desc)
		return ERR_PTR(ENOMEM);

	vmid_desc->n_vmid_entries = 1;
	vmid_desc->vmid_entries[0].vmid = vmid;
	return vmid_desc;
}

static void qts_trusted_touch_pvm_vm_mode_disable(struct qts_data *qts_data)
{
	int rc = 0;

	atomic_set(&qts_data->trusted_touch_transition, 1);

	if (atomic_read(&qts_data->trusted_touch_abort_status)) {
		qts_trusted_touch_abort_pvm(qts_data);
		return;
	}

	if (qts_trusted_touch_get_vm_state(qts_data) !=
	    PVM_ALL_RESOURCES_RELEASE_NOTIFIED) {
		thp_qts_log_err(
			qts_data->cd,
			"all release notifications are not received yet\n");
		/* wait for tvm release mem */
		if (!wait_for_completion_timeout(&qts_data->tvm_release_finished,
						 msecs_to_jiffies(200)))
			thp_qts_log_err(qts_data->cd,
					"tvm resources release delay 200 ms\n");
		else
			thp_qts_log_info(qts_data->cd,
					 "tvm resources release finished\n");
	}

	if (qts_data->vendor_ops->pre_la_tui_disable)
		qts_data->vendor_ops->pre_la_tui_disable(qts_data->cd);

		/* reclaim mem */
#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
	rc = ghd_rm_mem_reclaim(qts_data->vm_info.vm_mem_handle, 0);
#else
	rc = gh_rm_mem_reclaim(qts_data->vm_info.vm_mem_handle, 0);
#endif
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"Trusted touch VM mem reclaim failed rc:%d\n",
				rc);
		goto error;
	}
	qts_trusted_touch_set_vm_state(qts_data, PVM_IOMEM_RECLAIMED);
	qts_data->vm_info.vm_mem_handle = 0;
	thp_qts_log_info(qts_data->cd, "vm mem reclaim success!\n");

	/* reclaim irq */
	rc = gh_irq_reclaim(qts_data->vm_info.irq_label);
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"failed to reclaim irq on pvm rc:%d\n", rc);
		goto error;
	}
	qts_trusted_touch_set_vm_state(qts_data, PVM_IRQ_RECLAIMED);
	thp_qts_log_debug(qts_data->cd, "vm irq reclaim success!\n");

	/* exit tui, relase spi bus */
	qts_bus_put(qts_data);
	qts_trusted_touch_set_vm_state(qts_data, PVM_I2C_RESOURCE_RELEASED);
	qts_trusted_touch_set_vm_state(qts_data, TRUSTED_TOUCH_PVM_INIT);

	/* enable irq again */
	if (qts_data->vendor_ops->enable_touch_irq)
		qts_data->vendor_ops->enable_touch_irq(qts_data->cd, true);
	qts_trusted_touch_set_vm_state(qts_data, PVM_INTERRUPT_ENABLED);

	/* tui exit success */
	atomic_set(&qts_data->trusted_touch_enabled, 0);
	atomic_set(&qts_data->trusted_touch_transition, 0);

	/* notify vendor to exit irq */
	if (qts_data->vendor_ops->post_la_tui_disable) {
		rc = qts_data->vendor_ops->post_la_tui_disable(qts_data->cd);
		if (rc) {
			thp_qts_log_err(qts_data->cd,
					"post_la_tui_disable err: %d\n", rc);
			goto error;
		}
	}

	thp_qts_log_info(
		qts_data->cd,
		"Irq, iomem are reclaimed and trusted touch disabled\n");
	return;
error:
	qts_trusted_touch_abort_handler(qts_data,
					TRUSTED_TOUCH_EVENT_RECLAIM_FAILURE);
}

static void qts_vm_irq_on_release_callback(void *data, unsigned long notif_type,
					   enum gh_irq_label label)
{
	struct qts_data *qts_data = data;

	if (notif_type != GH_RM_NOTIF_VM_IRQ_RELEASED) {
		thp_qts_log_err(qts_data->cd, "invalid notification type\n");
		return;
	}

	if (qts_trusted_touch_get_vm_state(qts_data) ==
	    PVM_IOMEM_RELEASE_NOTIFIED) {
		qts_trusted_touch_set_vm_state(
			qts_data, PVM_ALL_RESOURCES_RELEASE_NOTIFIED);
		complete_all(&qts_data->tvm_release_finished);
	} else
		qts_trusted_touch_set_vm_state(qts_data,
					       PVM_IRQ_RELEASE_NOTIFIED);
}

static void qts_vm_mem_on_release_handler(enum gh_mem_notifier_tag tag,
					  unsigned long notif_type,
					  void *entry_data, void *notif_msg)
{
	struct gh_rm_notif_mem_released_payload *release_payload;
	struct qts_trusted_touch_vm_info *vm_info;
	struct qts_data *qts_data;

	qts_data = (struct qts_data *)entry_data;
	vm_info = &qts_data->vm_info;
	if (!vm_info) {
		thp_qts_log_err(qts_data->cd, "Invalid vm_info\n");
		return;
	}

	if (notif_type != GH_RM_NOTIF_MEM_RELEASED) {
		thp_qts_log_err(qts_data->cd, "Invalid notification type\n");
		return;
	}

	if (tag != vm_info->mem_tag) {
		thp_qts_log_err(qts_data->cd, "Invalid tag\n");
		return;
	}

	if (!entry_data || !notif_msg) {
		thp_qts_log_err(qts_data->cd,
				"Invalid data or notification message\n");
		return;
	}

	release_payload = (struct gh_rm_notif_mem_released_payload *)notif_msg;
	if (release_payload->mem_handle != vm_info->vm_mem_handle) {
		thp_qts_log_err(qts_data->cd, "Invalid mem handle detected\n");
		return;
	}

	if (qts_trusted_touch_get_vm_state(qts_data) ==
	    PVM_IRQ_RELEASE_NOTIFIED) {
		qts_trusted_touch_set_vm_state(
			qts_data, PVM_ALL_RESOURCES_RELEASE_NOTIFIED);
		complete_all(&qts_data->tvm_release_finished);
	} else
		qts_trusted_touch_set_vm_state(qts_data,
					       PVM_IOMEM_RELEASE_NOTIFIED);
}

static int qts_vm_mem_lend(struct qts_data *qts_data)
{
	struct gh_acl_desc *acl_desc;
	struct gh_sgl_desc *sgl_desc;
	struct gh_notify_vmid_desc *vmid_desc;
	gh_memparcel_handle_t mem_handle;
	gh_vmid_t trusted_vmid;
	int rc = 0;

	acl_desc = qts_vm_get_acl(GH_TRUSTED_VM);
	if (IS_ERR(acl_desc)) {
		thp_qts_log_err(
			qts_data->cd,
			"Failed to get acl of IO memories for Trusted touch\n");
		rc = PTR_ERR(acl_desc);
		return rc;
	}

	sgl_desc = qts_vm_get_sgl(&qts_data->vm_info);
	if (IS_ERR(sgl_desc)) {
		thp_qts_log_err(
			qts_data->cd,
			"Failed to get sgl of IO memories for Trusted touch\n");
		rc = PTR_ERR(sgl_desc);
		goto sgl_error;
	}

#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
	rc = ghd_rm_mem_lend(GH_RM_MEM_TYPE_IO, 0, TRUSTED_TOUCH_MEM_LABEL,
			     acl_desc, sgl_desc, NULL, &mem_handle);
#else
	rc = gh_rm_mem_lend(GH_RM_MEM_TYPE_IO, 0, TRUSTED_TOUCH_MEM_LABEL,
			    acl_desc, sgl_desc, NULL, &mem_handle);
#endif
	if (rc) {
		thp_qts_log_err(
			qts_data->cd,
			"Failed to lend IO memories for Trusted touch rc:%d\n",
			rc);
		goto error;
	}

	thp_qts_log_debug(qts_data->cd, "vm mem lend success\n");

	qts_trusted_touch_set_vm_state(qts_data, PVM_IOMEM_LENT);

#if (KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE)
	ghd_rm_get_vmid(GH_TRUSTED_VM, &trusted_vmid);
#else
	gh_rm_get_vmid(GH_TRUSTED_VM, &trusted_vmid);
#endif

	vmid_desc = qts_vm_get_vmid(trusted_vmid);

	rc = gh_rm_mem_notify(mem_handle, GH_RM_MEM_NOTIFY_RECIPIENT_SHARED,
			      qts_data->vm_info.mem_tag, vmid_desc);
	if (rc) {
		thp_qts_log_err(
			qts_data->cd,
			"Failed to notify mem lend to hypervisor rc:%d\n", rc);
		goto vmid_error;
	}

	qts_trusted_touch_set_vm_state(qts_data, PVM_IOMEM_LENT_NOTIFIED);

	qts_data->vm_info.vm_mem_handle = mem_handle;
vmid_error:
	kfree(vmid_desc);
error:
	kfree(sgl_desc);
sgl_error:
	kfree(acl_desc);

	return rc;
}

static int qts_trusted_touch_pvm_vm_mode_enable(struct qts_data *qts_data)
{
	int rc = 0;
	struct qts_trusted_touch_vm_info *vm_info = &qts_data->vm_info;

	/* begin tui transition process */
	atomic_set(&qts_data->trusted_touch_transition, 1);
	/* init locks */
	mutex_lock(&qts_data->transition_lock);
	reinit_completion(&qts_data->tvm_release_finished);

	/* notify vendor prepare transition in vendor */
	if (qts_data->vendor_ops->pre_la_tui_enable) {
		rc = qts_data->vendor_ops->pre_la_tui_enable(qts_data->cd);
		if (rc) {
			thp_qts_log_err(qts_data->cd,
					"pre_la_tui_enable report err rc: %d\n",
					rc);
			goto error;
		}
	}

	/* diasble irq */
	thp_qts_log_info(qts_data->cd, "disable irq for tui here\n");
	if (qts_data->vendor_ops->enable_touch_irq)
		qts_data->vendor_ops->enable_touch_irq(qts_data->cd, false);
	atomic_set(&qts_data->irq_state, 0);
	qts_trusted_touch_set_vm_state(qts_data, PVM_INTERRUPT_DISABLED);

	/* client session start and resource acquire */
	if (qts_bus_get(qts_data) < 0) {
		thp_qts_log_err(qts_data->cd, "qts_bus_get failed\n");
		rc = -EIO;
		goto error;
	}
	qts_trusted_touch_set_vm_state(qts_data, PVM_I2C_RESOURCE_ACQUIRED);

	/* lend mem */
	rc = qts_vm_mem_lend(qts_data);
	if (rc) {
		thp_qts_log_err(qts_data->cd, "Failed to lend memory\n");
		goto abort_handler;
	}
	thp_qts_log_debug(qts_data->cd, "vm mem lend success\n");

	/* enter tui mode the first time, get irq num */
	if (atomic_read(&qts_data->delayed_pvm_probe_pending)) {
		if (qts_data->vendor_ops->get_irq_num)
			qts_data->irq =
				qts_data->vendor_ops->get_irq_num(qts_data->cd);

		atomic_set(&qts_data->delayed_pvm_probe_pending, 0);
	}

	/* lend irq */
	rc = gh_irq_lend_v2(vm_info->irq_label, vm_info->vm_name, qts_data->irq,
			    &qts_vm_irq_on_release_callback, qts_data);
	if (rc) {
		thp_qts_log_err(qts_data->cd, "Failed to lend irq\n");
		goto abort_handler;
	}
	thp_qts_log_debug(qts_data->cd, "vm irq lend success for irq:%d\n",
			  qts_data->irq);
	qts_trusted_touch_set_vm_state(qts_data, PVM_IRQ_LENT);

	/* notify tvm */
	rc = gh_irq_lend_notify(vm_info->irq_label);
	if (rc) {
		thp_qts_log_err(qts_data->cd, "Failed to notify irq\n");
		goto abort_handler;
	}
	qts_trusted_touch_set_vm_state(qts_data, PVM_IRQ_LENT_NOTIFIED);

	if (qts_data->vendor_ops->post_la_tui_enable)
		qts_data->vendor_ops->post_la_tui_enable(qts_data->cd);

	/* end tui transition process */
	mutex_unlock(&qts_data->transition_lock);
	atomic_set(&qts_data->trusted_touch_transition, 0);
	atomic_set(&qts_data->trusted_touch_enabled, 1);
	thp_qts_log_info(qts_data->cd,
			 "Irq, iomem are lent and trusted touch enabled\n");
	return rc;

abort_handler:
	qts_trusted_touch_abort_handler(qts_data,
					TRUSTED_TOUCH_EVENT_LEND_FAILURE);

error:
	mutex_unlock(&qts_data->transition_lock);
	if (qts_data->vendor_ops->tui_abort) {
		qts_data->vendor_ops->tui_abort(qts_data->cd);
	}

	return rc;
}

int qts_handle_trusted_touch_pvm(struct qts_data *qts_data, int value)
{
	int err = 0;

	switch (value) {
	case 0:
		if (atomic_read(&qts_data->trusted_touch_enabled) == 0 &&
		    (atomic_read(&qts_data->trusted_touch_abort_status) == 0)) {
			thp_qts_log_err(qts_data->cd,
					"Trusted touch is already disabled\n");
			break;
		}
		if (atomic_read(&qts_data->trusted_touch_mode) ==
		    QTS_TRUSTED_TOUCH_VM_MODE) {
			qts_trusted_touch_pvm_vm_mode_disable(qts_data);
		} else {
			thp_qts_log_err(qts_data->cd,
					"Unsupported trusted touch mode\n");
		}
		break;

	case 1:
		if (atomic_read(&qts_data->trusted_touch_enabled)) {
			thp_qts_log_err(qts_data->cd,
					"Trusted touch usecase underway\n");
			err = -EBUSY;
			break;
		}
		if (atomic_read(&qts_data->trusted_touch_mode) ==
		    QTS_TRUSTED_TOUCH_VM_MODE) {
			err = qts_trusted_touch_pvm_vm_mode_enable(qts_data);
		} else {
			thp_qts_log_err(qts_data->cd,
					"Unsupported trusted touch mode\n");
		}
		break;

	default:
		thp_qts_log_err(qts_data->cd, "unsupported value: %lu\n",
				value);
		err = -EINVAL;
		break;
	}
	return err;
}
#endif

static void qts_trusted_touch_event_notify(struct qts_data *qts_data, int event)
{
	atomic_set(&qts_data->trusted_touch_event, event);
	sysfs_notify(&qts_data->dev->kobj, NULL, "trusted_touch_event");
}

static void qts_trusted_touch_abort_handler(struct qts_data *qts_data,
					    int error)
{
	atomic_set(&qts_data->trusted_touch_abort_status, error);
	thp_qts_log_info(qts_data->cd, "TUI session aborted with failure:%d\n",
			 error);
	qts_trusted_touch_event_notify(qts_data, error);
}

static int qts_vm_init(struct qts_data *qts_data)
{
	int rc = 0;
	struct qts_trusted_touch_vm_info *vm_info;
	void *mem_cookie;

	rc = qts_populate_vm_info(qts_data);
	if (rc) {
		thp_qts_log_err(qts_data->cd, "Cannot setup vm pipeline\n");
		rc = -EINVAL;
		goto fail;
	}

	vm_info = &qts_data->vm_info;
#ifdef CONFIG_ARCH_QTI_VM
	mem_cookie = gh_mem_notifier_register(
		vm_info->mem_tag, qts_vm_mem_on_lend_handler, qts_data);
	if (!mem_cookie) {
		thp_qts_log_err(qts_data->cd,
				"Failed to register on lend mem notifier\n");
		rc = -EINVAL;
		goto init_fail;
	}
	vm_info->mem_cookie = mem_cookie;
	rc = gh_irq_wait_for_lend_v2(vm_info->irq_label, GH_PRIMARY_VM,
				     &qts_vm_irq_on_lend_callback, qts_data);
	qts_trusted_touch_set_vm_state(qts_data, TRUSTED_TOUCH_TVM_INIT);
#else
	mem_cookie = gh_mem_notifier_register(
		vm_info->mem_tag, qts_vm_mem_on_release_handler, qts_data);
	if (!mem_cookie) {
		thp_qts_log_err(qts_data->cd,
				"Failed to register on release mem notifier\n");
		rc = -EINVAL;
		goto init_fail;
	}
	vm_info->mem_cookie = mem_cookie;
	spin_lock_init(&qts_data->vm_info.spin_lock);
	qts_trusted_touch_set_vm_state(qts_data, TRUSTED_TOUCH_PVM_INIT);
#endif
	return rc;
init_fail:
	qts_vm_deinit(qts_data);
fail:
	return rc;
}

static void qts_dt_parse_trusted_touch_info(struct qts_data *qts_data)
{
	struct device_node *np = qts_data->dp;
	int rc = 0;
	const char *selection;
	const char *environment;
	int trusted_touch_msgq;
	unsigned int irq_flag;

	rc = of_property_read_string(np, "trusted-touch-mode", &selection);
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"No trusted touch mode selection made\n");
		atomic_set(&qts_data->trusted_touch_mode,
			   QTS_TRUSTED_TOUCH_MODE_NONE);
		return;
	}

	if (!strcmp(selection, "vm_mode")) {
		atomic_set(&qts_data->trusted_touch_mode,
			   QTS_TRUSTED_TOUCH_VM_MODE);
		thp_qts_log_debug(qts_data->cd,
				  "Selected trusted touch mode to VM mode\n");
	} else {
		atomic_set(&qts_data->trusted_touch_mode,
			   QTS_TRUSTED_TOUCH_MODE_NONE);
		thp_qts_log_err(qts_data->cd, "Invalid trusted_touch mode\n");
	}

	rc = of_property_read_string(np, "touch-environment", &environment);
	if (rc)
		thp_qts_log_err(qts_data->cd,
				"No trusted touch mode environment\n");

	rc = of_property_read_u32(np, "tui_irq_flag", &irq_flag);
	if (rc) {
		thp_qts_log_err(qts_data->cd,
				"Not set tui_irq_flag, use default\n");
		irq_flag = IRQF_TRIGGER_RISING;
	}

	if (qts_data->client_index == QTS_CLIENT_PRIMARY_TOUCH) {
		rc = of_property_read_u32(np, "trusted-touch-msgq",
					  &trusted_touch_msgq);
		if (rc) {
			thp_qts_log_err(
				qts_data->cd,
				"%s: Trusted touch message queue not set\n",
				__func__);
			trusted_touch_msgq = DEFAULT_QTS_TRUSTED_TOUCH_MSGQ;
		}
		g_trusted_touch_msgq = trusted_touch_msgq;
	}

	qts_data->touch_environment = environment;
	qts_data->tui_supported = true;
	qts_data->irq_flag = irq_flag;
	thp_qts_log_debug(qts_data->cd, "Trusted touch environment:%s\n",
			  qts_data->touch_environment);
}

static void qts_trusted_touch_init(struct qts_data *qts_data)
{
	int rc = 0;

	atomic_set(&qts_data->trusted_touch_initialized, 0);
	qts_dt_parse_trusted_touch_info(qts_data);

	if (atomic_read(&qts_data->trusted_touch_mode) ==
	    QTS_TRUSTED_TOUCH_MODE_NONE)
		return;

	/* Get clocks */
	qts_data->core_clk = devm_clk_get(qts_data->dev->parent, "m-ahb");

	if (IS_ERR(qts_data->core_clk)) {
		qts_data->core_clk = NULL;
		thp_qts_log_err(qts_data->cd, "core_clk is not defined\n");
	}

	qts_data->iface_clk = devm_clk_get(qts_data->dev->parent, "se-clk");

	if (IS_ERR(qts_data->iface_clk)) {
		qts_data->iface_clk = NULL;
		thp_qts_log_err(qts_data->cd, "iface_clk is not defined\n");
	}

	if (atomic_read(&qts_data->trusted_touch_mode) ==
	    QTS_TRUSTED_TOUCH_VM_MODE) {
		rc = qts_vm_init(qts_data);
		if (rc)
			thp_qts_log_err(qts_data->cd, "Failed to init VM\n");
	}
	atomic_set(&qts_data->trusted_touch_initialized, 1);
}

#ifdef CONFIG_ARCH_QTI_VM
DEFINE_MUTEX(g_input_msgq_create_mutex);
#endif
static int qts_trusted_msgq_init(struct qts_data *qts_data)
{
	int rc = 0;
	if (atomic_read(&qts_data->trusted_touch_mode) ==
	    QTS_TRUSTED_TOUCH_VM_MODE) {
		if (qts_data->client_index == QTS_CLIENT_SECONDARY_TOUCH) {
			qts_data->extra_msgq = gh_msgq_register(
				QTS_TRUSTED_TOUCH_EXTRA_MSGQ_SECONDARY);
		} else if (qts_data->client_index == QTS_CLIENT_PRIMARY_TOUCH) {
			qts_data->extra_msgq = gh_msgq_register(
				QTS_TRUSTED_TOUCH_EXTRA_MSGQ_PRIMARY);
		}
		if (!qts_data->extra_msgq) {
			thp_qts_log_err(qts_data->cd,
					"failed to register extra mesg\n");
			return -EINVAL;
		}
		thp_qts_log_err(qts_data->cd, "register extra mesg success\n");
	} else {
		qts_data->extra_msgq = NULL;
		g_input_msgq = NULL;
		return -EINVAL;
	}

#ifdef CONFIG_ARCH_QTI_VM
	mutex_lock(&g_input_msgq_create_mutex);
	if (!g_input_msgq) {
		g_input_msgq = gh_msgq_register(g_trusted_touch_msgq);
		if (!g_input_msgq) {
			thp_qts_log_err(
				qts_data->cd,
				"Failed to register on lend mem notifier\n");
			rc = -EINVAL;
			mutex_unlock(&g_input_msgq_create_mutex);
		}
		thp_qts_log_info(qts_data->cd, "%s g_input_msgq register suc\n",
				 __func__);
	} else {
		thp_qts_log_info(qts_data->cd, "msgq already register\n");
	}
	mutex_unlock(&g_input_msgq_create_mutex);
#endif

	return rc;
}

int qts_client_register(struct qts_data *qts_data,
			struct qts_vendor_data *vendor_data)
{
	int rc = 0;
	thp_qts_log_debug(qts_data->cd, "QTS client register starts\n");

	if (!qts_data || !vendor_data) {
		return -EINVAL;
	}
	if (!vendor_data->cd || !vendor_data->vendor_ops ||
	    !vendor_data->device || !vendor_data->sdev) {
		return -EINVAL;
	}

	memset(qts_data, 0, sizeof(struct qts_data));

	qts_data->spi = vendor_data->sdev;
	qts_data->dev = vendor_data->device;
	qts_data->vendor_ops = vendor_data->vendor_ops;
	qts_data->cd = vendor_data->cd;

	qts_data->dp = qts_data->dev->of_node;

	qts_trusted_touch_init(qts_data);

	if (atomic_read(&qts_data->trusted_touch_mode) ==
	    QTS_TRUSTED_TOUCH_MODE_NONE)
		return 0;

	rc = qts_trusted_msgq_init(qts_data);
	if (rc) {
		thp_qts_log_err(qts_data->cd, "qts_trusted_msgq_init err: %d\n",
				rc);
		return rc;
	}

	mutex_init(&(qts_data->qts_clk_io_ctrl_mutex));
	mutex_init(&qts_data->transition_lock);
	init_completion(&qts_data->tvm_release_finished);

#ifdef CONFIG_ARCH_QTI_VM
	atomic_set(&qts_data->delayed_tvm_probe_pending, 1);
#else
	atomic_set(&qts_data->delayed_pvm_probe_pending, 1);
#endif

	thp_qts_log_debug(qts_data->cd, "client register end\n");
	return 0;
}

void qts_client_unregister(struct qts_data *qts_data)
{
	if (!qts_data)
		return;

	qts_vm_deinit(qts_data);
}

int qts_tui_enter(struct qts_data *qts_data)
{
	int err = 0;

	thp_qts_log_info(qts_data->cd, "TUI trusted_touch_enable\n");
#ifdef CONFIG_ARCH_QTI_VM
	err = qts_handle_trusted_touch_tvm(qts_data, 1);
	if (err) {
		thp_qts_log_err(qts_data->cd,
				"Failed to handle trusted touch in tvm\n");
		return -EINVAL;
	}
#else
	err = qts_handle_trusted_touch_pvm(qts_data, 1);
	if (err) {
		thp_qts_log_err(qts_data->cd,
				"Failed to handle trusted touch in pvm\n");
		return -EINVAL;
	}
#endif
	return err;
}

int qts_tui_exit(struct qts_data *qts_data)
{
	int err = 0;

	thp_qts_log_info(qts_data->cd, "TUI trusted_touch_disable\n");
#ifdef CONFIG_ARCH_QTI_VM
	err = qts_handle_trusted_touch_tvm(qts_data, 0);
	if (err) {
		thp_qts_log_err(qts_data->cd,
				"Failed to handle trusted touch in tvm\n");
		return -EINVAL;
	}
#else
	err = qts_handle_trusted_touch_pvm(qts_data, 0);
	if (err) {
		thp_qts_log_err(qts_data->cd,
				"Failed to handle trusted touch in pvm\n");
		return -EINVAL;
	}
#endif
	return err;
}

int qts_dt_overlay_register(struct qts_data *qts_data,
			    struct device_node *node_overlay)
{
	if (qts_data == NULL) {
		thp_qts_log_err(qts_data->cd, "qts_data is null\n");
		return -EINVAL;
	}

	qts_data->dp_overlay = node_overlay;
	return 0;
}

int qts_tui_is_enable(struct qts_data *qts_data)
{
	int status = atomic_read(&qts_data->trusted_touch_enabled);
	return status;
}

void qts_report_to_htee(struct qts_data *qts_data,
			struct thp_udfp_data *udfp_data)
{
// These event type and structure should be same with htee side.
#define TYPE_TOUCH 64
#define TYPE_RELEASE 32
	struct event_node {
		int32_t status;
		int32_t x;
		int32_t y;
	};
	struct event_node event;
	int ret = 0;

	if (qts_data == NULL || g_input_msgq == NULL) {
		thp_qts_log_err(
			qts_data->cd,
			"qts_data is null or g_input_msgq not registered\n");
		return;
	}

	event.x = udfp_data->tpud_data.tp_x;
	event.y = udfp_data->tpud_data.tp_y;

	if (udfp_data->tpud_data.udfp_event == TP_EVENT_FINGER_DOWN)
		event.status = TYPE_TOUCH;
	else if (udfp_data->tpud_data.udfp_event == TP_EVENT_FINGER_UP)
		event.status = TYPE_RELEASE;
	else
		return;

	ret = gh_msgq_send(g_input_msgq, &event, sizeof(event),
			   GH_MSGQ_TX_PUSH);
	if (ret < 0)
		thp_qts_log_err(qts_data->cd,
				"%s failed to send input data: %d\n", __func__,
				ret);
	thp_qts_log_info(qts_data->cd, "%s ->done,(%u, %u, %d)\n", __func__,
			 event.x, event.y, event.status);
}

int qts_extra_mesg_send(struct qts_data *qts_data, uint8_t *buf, int len)
{
	int ret;

	if (qts_data->extra_msgq == NULL)
		return -EINVAL;
	ret = gh_msgq_send(qts_data->extra_msgq, buf, len, 0);
	if (ret)
		thp_qts_log_err(qts_data->cd, "%s:Failed to send message: %d\n",
				__func__, ret);
	return ret;
}

int qts_extra_mesg_recv(struct qts_data *qts_data, uint8_t *buf, int len)
{
	int ret;
	size_t recv_size = 0;

	if (qts_data->extra_msgq == NULL)
		return -EINVAL;
	ret = gh_msgq_recv(qts_data->extra_msgq, buf, len, &recv_size, 0);
	if (ret)
		thp_qts_log_err(qts_data->cd, "%s:Failed to recv message: %d\n",
				__func__, ret);
	return ret;
}

#endif

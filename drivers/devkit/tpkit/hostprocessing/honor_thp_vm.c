#include "honor_thp_vm.h"
#include "honor_thp.h"
#include <linux/kernel.h>

#if (defined CONFIG_HONOR_TRUSTED_TOUCH) && (IS_ENABLED(CONFIG_HONOR_THP_QCOM))

extern struct thp_device *syna_driver_module_alloc(struct thp_core_data *cd);
extern struct thp_device *goodix_driver_module_alloc(struct thp_core_data *cd);
extern struct device_node *thp_get_dev_node(struct thp_core_data *cd,
					    struct thp_device *dev);
extern int thp_setup_spi(struct thp_core_data *cd);

static struct kset *g_qts_kset;

static int enable_touch_irq(struct thp_core_data *cd, bool en)
{
	en ? enable_irq(cd->irq) : disable_irq(cd->irq);
	return 0;
}

static irqreturn_t irq_handler(int irq, struct thp_core_data *cd)
{
	int rc;
	struct thp_udfp_data udfp_data;

	cd->irq_thread_pid = current->pid;
	memset(&udfp_data, 0, sizeof(udfp_data));
	if (!cd->thp_dev->ops->get_tui_event_info) {
		thp_vm_log_err(cd, "%s: ops is NULL\n", __func__);
		return IRQ_HANDLED;
	}

	rc = cd->thp_dev->ops->get_tui_event_info(cd->thp_dev, &udfp_data);
	if (rc) {
		thp_vm_log_err(cd, "%s: get event info fail, ret = %d\n",
			       __func__, rc);
		return IRQ_HANDLED;
	}

	qts_report_to_htee(&cd->vm_core->qts_data, &udfp_data);

	return IRQ_HANDLED;
}

static int get_irq_num(struct thp_core_data *cd)
{
	return cd->irq;
}

static int pre_la_tui_enable(struct thp_core_data *cd)
{
	int rc = NO_ERR;

	reinit_completion(&cd->vm_core->tp_ic_tui_entered);
	reinit_completion(&cd->vm_core->tp_ic_tui_exited);

	thp_set_status(cd, THP_STATUS_TUI, 1);
	if (!wait_for_completion_timeout(&cd->vm_core->tp_ic_tui_entered,
					 msecs_to_jiffies(2000))) {
		thp_vm_log_err(cd, "TP IC don't enter TUI mode in 2000 ms\n");
		rc = -EBUSY;
		goto end;
	}

	if (cd->thp_dev && cd->thp_dev->ops &&
	    cd->thp_dev->ops->tui_enable_switch) {
		rc = cd->thp_dev->ops->tui_enable_switch(cd->thp_dev,
							 TUI_ENABLE);
		if (rc) {
			thp_vm_log_err(cd, "%s: TUI enable failed \n",
				       __func__);
			atomic_set(&cd->vm_core->tp_ic_tui_enabled, 0);
			goto end;
		} else {
			atomic_set(&cd->vm_core->tp_ic_tui_enabled, 1);
		}
	}

end:
	return rc;
}

static int post_la_tui_enable(struct thp_core_data *cd)
{
	thp_vm_log_debug(cd, "%s: called \n", __func__);
	return 0;
}

static int pre_la_tui_disable(struct thp_core_data *cd)
{
	thp_vm_log_debug(cd, "%s: called \n", __func__);
	return 0;
}

static int post_la_tui_disable(struct thp_core_data *cd)
{
	int rc = NO_ERR;

	if (cd->thp_dev && cd->thp_dev->ops &&
	    cd->thp_dev->ops->tui_enable_switch) {
		rc = cd->thp_dev->ops->tui_enable_switch(cd->thp_dev,
							 TUI_DISABLE);
		if (rc) {
			thp_vm_log_err(cd, "%s: TUI disable failed\n",
				       __func__);
			goto end;
		} else {
			atomic_set(&cd->vm_core->tp_ic_tui_enabled, 0);
		}
	}

	thp_set_status(cd, THP_STATUS_TUI, 0);
	if (!wait_for_completion_timeout(&cd->vm_core->tp_ic_tui_exited,
					 msecs_to_jiffies(2000))) {
		thp_vm_log_err(cd, "TP IC don't exit TUI mode in 2000 ms\n");
		rc = -EBUSY;
		goto end;
	}

end:
	return rc;
}

static int pre_le_tui_enable(struct thp_core_data *cd)
{
	thp_vm_log_debug(cd, "%s: called \n", __func__);
	return 0;
}

static int post_le_tui_enable(struct thp_core_data *cd)
{
	thp_vm_log_debug(cd, "%s: called \n", __func__);
	return 0;
}

static int pre_le_tui_disable(struct thp_core_data *cd)
{
	thp_vm_log_debug(cd, "%s: called \n", __func__);
	return 0;
}

static int post_le_tui_disable(struct thp_core_data *cd)
{
	thp_vm_log_debug(cd, "%s: called \n", __func__);
	return 0;
}

static void tui_abort(struct thp_core_data *cd)
{
	thp_set_status(cd, THP_STATUS_TUI, 0);
}

struct qts_vendor_callback_ops g_qts_vendor_callback_ops = {
	.enable_touch_irq = enable_touch_irq,
	.irq_handler = irq_handler,
	.get_irq_num = get_irq_num,
	.pre_la_tui_enable = pre_la_tui_enable,
	.post_la_tui_enable = post_la_tui_enable,
	.pre_la_tui_disable = pre_la_tui_disable,
	.post_la_tui_disable = post_la_tui_disable,
	.pre_le_tui_enable = pre_le_tui_enable,
	.post_le_tui_enable = post_le_tui_enable,
	.pre_le_tui_disable = pre_le_tui_disable,
	.post_le_tui_disable = post_le_tui_disable,
	.tui_abort = tui_abort,
};

static inline int extra_mesg_send(struct thp_vm_core *vm_core, uint8_t *buf,
				  int len)
{
	return qts_extra_mesg_send(&vm_core->qts_data, buf, len);
}

static inline int extra_mesg_recv(struct thp_vm_core *vm_core, uint8_t *buf,
				  int len)
{
	return qts_extra_mesg_recv(&vm_core->qts_data, buf, len);
}

#ifdef CONFIG_ARCH_QTI_VM
static int tvm_register_dev(struct thp_core_data *cd, struct thp_device *dev)
{
	int rc = -EINVAL;
	struct device_node *thp_node = NULL;

	if ((dev == NULL) || (cd == NULL)) {
		thp_vm_log_err(cd, "%s: input null\n", __func__);
		return -EINVAL;
	}

	thp_log_info(cd, "%s: called\n", __func__);
	/* check device configed ot not */
	thp_node = thp_get_dev_node(cd, dev);
	if (!thp_node) {
		thp_vm_log_info(cd, "%s: not config in dts\n", __func__);
		goto register_err;
	}

	dev->thp_core = cd;
	dev->gpios = &cd->gpios;
	dev->sdev = cd->sdev;
	cd->thp_dev = dev;
	cd->is_fw_update = 0;

	rc = thp_parse_timing_config(cd, cd->thp_node, &dev->timing_config);
	if (rc) {
		thp_vm_log_err(cd, "%s: timing config parse fail\n", __func__);
		goto register_err;
	}

	rc = dev->ops->init(dev);
	if (rc) {
		thp_vm_log_err(cd, "%s: dev init fail\n", __func__);
		goto register_err;
	}

	rc = thp_setup_spi(cd);
	if (rc) {
		thp_vm_log_err(cd, "%s: spi dev init fail\n", __func__);
		goto register_err;
	}

	if (dev->ops->prepare) {
		rc = dev->ops->prepare(dev);
		if (rc) {
			thp_vm_log_err(cd, "%s: prepare fail rc: %d\n",
				       __func__, rc);
			goto register_err;
		}
	}

	rc = qts_dt_overlay_register(&cd->vm_core->qts_data, thp_node);
	if (rc) {
		thp_vm_log_err(cd, "%s: qts overlay register fail rc: %d\n",
			       __func__, rc);
		goto register_err;
	}

	mutex_init(&cd->mutex_frame);
	mutex_init(&cd->irq_mutex);
	mutex_init(&cd->thp_mutex);
	mutex_init(&cd->status_mutex);
	mutex_init(&cd->suspend_flag_mutex);
	mutex_init(&cd->msgq_mutex);
	mutex_init(&cd->aod_power_ctrl_mutex);

	dev_set_drvdata(&cd->sdev->dev, cd);
	atomic_set(&cd->register_flag, 1);
	thp_set_status(cd, THP_STATUS_POWER, 1);
	rc = thp_init_sysfs(cd);
	if (rc) {
		thp_vm_log_err(cd, "%s: sysfs init fail: %d\n", __func__, rc);
		goto register_err;
	}

	return rc;

register_err:
	if (dev->ops->exit)
		dev->ops->exit(dev);

	cd->thp_dev = 0;
	return rc;
}

static int tvm_touch_driver_thread(void *thp_core)
{
	int ic_type;
	int vm_ready = 1;
	int ret;
	struct thp_core_data *cd = (struct thp_core_data *)thp_core;
	struct thp_device *tdev = NULL;

	thp_vm_log_info(cd, "%s: call \n", __func__);

	if (!cd || !cd->vm_core) {
		thp_vm_log_err(cd, "%s: cd is null or vm not registered\n",
			       __func__);
		return -EINVAL;
	}

	ret = extra_mesg_send(cd->vm_core, (uint8_t *)&vm_ready,
			      sizeof(vm_ready));
	if (ret) {
		thp_vm_log_err(cd, "%s: send msg to main fail, ret: %d\n",
			       __func__, ret);
		return -EIO;
	}

	ret = extra_mesg_recv(cd->vm_core, (uint8_t *)&ic_type,
			      sizeof(ic_type));
	if (ret) {
		thp_vm_log_err(cd, "%s: recv msg from main fail, ret: %d\n",
			       __func__, ret);
		return -EIO;
	}

	switch (ic_type) {
	case IC_TYPE_GOODIX:
		tdev = goodix_driver_module_alloc(cd);
		break;
	case IC_TYPE_SYNA:
		tdev = syna_driver_module_alloc(cd);
		break;
	default:
		thp_vm_log_err(cd, "%s:invalid ic type: %d\n", __func__,
			       ic_type);
		break;
	}
	tvm_register_dev(cd, tdev);
	return NO_ERR;
}

static int tvm_touch_driver_thread_init(struct thp_core_data *cd)
{
	if (!cd || !cd->vm_core) {
		thp_vm_log_err(cd, "%s: cd is null or vm not registered\n",
			       __func__);
		return -EINVAL;
	}
	cd->vm_core->tui_task = kthread_create(tvm_touch_driver_thread, cd,
					       "pvm_touch_driver_thread:%d", 0);
	wake_up_process(cd->vm_core->tui_task);
	return NO_ERR;
}
#else

static int pvm_touch_driver_thread(void *thp_core)
{
	int ic_type;
	int vm_ready = 0;
	int ret;
	struct thp_core_data *cd = (struct thp_core_data *)thp_core;

	thp_vm_log_info(cd, "%s: call \n", __func__);

	if (!cd || !cd->vm_core) {
		thp_vm_log_err(cd, "%s: cd is null or vm not registered\n",
			       __func__);
		return -EINVAL;
	}

	ret = extra_mesg_recv(cd->vm_core, (uint8_t *)&vm_ready,
			      sizeof(vm_ready));
	if (ret) {
		thp_vm_log_err(cd, "%s: recv msg from vm fail, ret: %d\n",
			       __func__, ret);
		return -EIO;
	}

	ic_type = cd->ic_type;
	if (vm_ready) {
		ret = extra_mesg_send(cd->vm_core, (uint8_t *)&ic_type,
				      sizeof(ic_type));
		if (ret) {
			thp_vm_log_err(cd, "%s: send msg to vm fail, ret: %d\n",
				       __func__, ret);
			return -EIO;
		}
	}

	return NO_ERR;
}

static int pvm_touch_driver_thread_init(struct thp_core_data *cd)
{
	if (!cd || !cd->vm_core) {
		thp_vm_log_err(cd, "%s: cd is null or vm not registered\n",
			       __func__);
		return -EINVAL;
	}
	cd->vm_core->tui_task = kthread_create(pvm_touch_driver_thread, cd,
					       "pvm_touch_driver_thread:%d", 0);
	wake_up_process(cd->vm_core->tui_task);
	return NO_ERR;
}
#endif

int thp_vm_register(struct thp_core_data *cd)
{
	int rc = 0;
	int tui_mode;
	struct thp_vm_core *vm_core = NULL;
	struct qts_vendor_data qts_vendor_data;

	vm_core = kzalloc(sizeof(struct thp_vm_core), GFP_KERNEL);
	if (!vm_core) {
		thp_vm_log_err(cd, "%s: vm_core out of memary\n", __func__);
		return -ENOMEM;
	}

	cd->vm_core = vm_core;

	qts_vendor_data.cd = cd;
	qts_vendor_data.vendor_ops = &g_qts_vendor_callback_ops;
	qts_vendor_data.device = cd->dev;
	qts_vendor_data.sdev = cd->sdev;

	init_completion(&cd->vm_core->tp_ic_tui_entered);
	init_completion(&cd->vm_core->tp_ic_tui_exited);

	rc = qts_client_register(&cd->vm_core->qts_data, &qts_vendor_data);
	if (rc) {
		thp_vm_log_err(cd, "%s: qts_client_register err: %d\n",
			       __func__, rc);
		thp_vm_unregister(cd);
		return rc;
	}
	tui_mode = atomic_read(&cd->vm_core->qts_data.trusted_touch_mode);
	if (tui_mode == QTS_TRUSTED_TOUCH_VM_MODE) {
#ifdef CONFIG_ARCH_QTI_VM
		tvm_touch_driver_thread_init(cd);
#else
		pvm_touch_driver_thread_init(cd);
#endif
	}
#ifdef CONFIG_ARCH_QTI_VM
	return TUI_VM_REGISTER_SUCCESS_TVM;
#else
	return TUI_VM_REGISTER_SUCCESS_PVM;
#endif
}

void thp_vm_unregister(struct thp_core_data *cd)
{
	if (!cd || !cd->vm_core) {
		thp_vm_log_debug(cd, "%s: cd is null or vm not registered\n",
				 __func__);
		return;
	}
	qts_client_unregister(&cd->vm_core->qts_data);
	kfree(cd->vm_core);
	cd->vm_core = NULL;
	return;
}

void thp_vm_afe_notify(struct thp_core_data *cd,
		       enum thp_afe_notify_tp_status status)
{
	if (!cd || !cd->vm_core) {
		thp_vm_log_err(cd, "%s: cd is null or vm not registered\n",
			       __func__);
		return;
	}
	if (status == THP_AFE_NOTIFY_TP_EXIT) {
		complete_all(&cd->vm_core->tp_ic_tui_exited);
	} else if (status == THP_AFE_NOTIFY_TP_ENTER) {
		complete_all(&cd->vm_core->tp_ic_tui_entered);
	}
}

int thp_vm_enter(struct thp_core_data *cd)
{
	return 0;
}

int thp_vm_exit(struct thp_core_data *cd)
{
	return 0;
}

int thp_vm_wait_ic_tui_exited(struct thp_core_data *cd)
{
	if (!cd || !cd->vm_core) {
		thp_vm_log_debug(cd, "%s: cd is null or vm not registered\n",
				 __func__);
		return 0;
	}
	if (atomic_read(&cd->vm_core->tp_ic_tui_enabled)) {
		if (!wait_for_completion_timeout(&cd->vm_core->tp_ic_tui_exited,
						 msecs_to_jiffies(5000)))
			return -1;
	}
	return 0;
}

bool thp_vm_afe_allowed(struct thp_core_data *cd)
{
	if (!cd || !cd->vm_core) {
		thp_vm_log_debug(cd, "%s: cd is null or vm not registered\n",
				 __func__);
		return true;
	}
	return !(atomic_read(&cd->vm_core->tp_ic_tui_enabled) &&
		 thp_get_status(cd, THP_STATUS_TUI) == 1);
}

bool thp_vm_tui_enabled(struct thp_core_data *cd)
{
	if (!cd || !cd->vm_core) {
		thp_vm_log_debug(cd, "%s: cd is null or vm not registered\n",
				 __func__);
		return false;
	}
	return qts_tui_is_enable(&cd->vm_core->qts_data);
}

static ssize_t trusted_touch_type_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct thp_core_data *cd = dev_get_drvdata(dev);
	struct qts_trusted_touch_vm_info *vm_info = NULL;

	if (!cd || !cd->vm_core) {
		thp_vm_log_debug(cd, "%s: cd is null or vm not registered\n",
				 __func__);
		return false;
	}

	vm_info = &cd->vm_core->qts_data.vm_info;
	if (vm_info->mem_tag == GH_MEM_NOTIFIER_TAG_TOUCH_PRIMARY) {
		thp_vm_log_info(cd, "%s: primary\n", __func__);
		return snprintf(buf, PAGE_SIZE - 1, "primary");
	} else if (vm_info->mem_tag == GH_MEM_NOTIFIER_TAG_TOUCH_SECONDARY) {
		thp_vm_log_info(cd, "%s: secondary\n", __func__);
		return snprintf(buf, PAGE_SIZE - 1, "secondary");
	} else {
		thp_vm_log_info(cd, "%s: invalid tag for touch type %d\n",
				__func__, vm_info->mem_tag);
	}

	return 0;
}

static ssize_t thp_trusted_touch_enable_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	int status;
	struct thp_core_data *cd = dev_get_drvdata(dev);

	if (!cd || !cd->vm_core) {
		thp_vm_log_err(cd, "%s: cd is null or vm not registered\n",
			       __func__);
		return -EINVAL;
	}

	status = qts_tui_is_enable(&cd->vm_core->qts_data);
	thp_log_info(cd, "%s: %u\n", __func__, status);

	return scnprintf(buf, PAGE_SIZE, "%d", status);
}

static ssize_t thp_trusted_touch_enable_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct thp_core_data *cd = dev_get_drvdata(dev);
	int status = 0;
	int err = 0;
	unsigned long value;

	if (!cd || !cd->vm_core) {
		thp_vm_log_err(cd, "%s: cd is null or vm not registered\n",
			       __func__);
		return -EINVAL;
	}

	status = qts_tui_is_enable(&cd->vm_core->qts_data);
	thp_vm_log_info(cd, "%s: previous staus: %u\n", __func__, status);

	if (count > 2) {
		thp_vm_log_err(cd, "%s: invalid input count\n", __func__);
		return -EINVAL;
	}
	err = kstrtoul(buf, 10, &value);
	if (err != 0) {
		thp_vm_log_err(cd, "%s: invalid input err: %d\n", __func__,
			       err);
		return err;
	}

	if (!atomic_read(&(cd->vm_core->qts_data.trusted_touch_initialized))) {
		thp_vm_log_err(cd, "%s: vm not initialized\n", __func__);
		return -EIO;
	}
	switch (value) {
	case 0:
		err = qts_tui_exit(&cd->vm_core->qts_data);
		break;
	case 1:
		err = qts_tui_enter(&cd->vm_core->qts_data);
		break;
	default:
		err = -EINVAL;
	}
	if (err)
		thp_vm_log_err(cd, "%s: failed to switch tui status to: %d\n",
			       __func__, value);
	else
		thp_vm_log_info(cd, "%s: switch tui status to: %d success\n",
				__func__, value);
	return count;
}

static DEVICE_ATTR(trusted_touch_enable, 0660, thp_trusted_touch_enable_show,
		   thp_trusted_touch_enable_store);
static DEVICE_ATTR(trusted_touch_type, 0444, trusted_touch_type_show, NULL);

static struct attribute *thp_vm_attributes[] = {
	&dev_attr_trusted_touch_enable.attr,
	&dev_attr_trusted_touch_type.attr,
	NULL,
};

static const struct attribute_group thp_vm_attr_group = {
	.attrs = thp_vm_attributes,
};

DEFINE_MUTEX(g_qts_create_mutex);
int thp_vm_init_sysfs(struct thp_core_data *cd, struct kobject *kobj)
{
	int rc = NO_ERR;

	if (cd == NULL || kobj == NULL) {
		thp_vm_log_err(cd, "%s: input is NULL\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&g_qts_create_mutex);
	if (g_qts_kset == NULL) {
		g_qts_kset = kset_create_and_add("qts", NULL, kernel_kobj);
		if (!g_qts_kset) {
			thp_vm_log_err(cd, "qts kset create failed\n");
			mutex_unlock(&g_qts_create_mutex);
			return -ENODEV;
		}
	}
	mutex_unlock(&g_qts_create_mutex);

	rc = sysfs_create_group(kobj, &thp_vm_attr_group);
	if (rc) {
		thp_vm_log_err(cd, "sysfs_create_group() failed!!\n");
		sysfs_remove_group(kobj, &thp_vm_attr_group);
		return -ENOMEM;
	}
	thp_vm_log_info(cd, "sysfs_create_group success\n");

	if ((cd->multi_panel_index == MAIN_TOUCH_PANEL) ||
	    (cd->multi_panel_index == SINGLE_TOUCH_PANEL)) {
		rc = sysfs_create_link(&g_qts_kset->kobj, kobj, "primary");
		if (rc) {
			thp_vm_log_err(
				cd, "%s: fail create primary link error = %d\n",
				__func__, rc);
			return -ENODEV;
		}
	} else if (cd->multi_panel_index == SUB_TOUCH_PANEL) {
		rc = sysfs_create_link(&g_qts_kset->kobj, kobj, "secondary");
		if (rc) {
			thp_vm_log_err(
				cd,
				"%s: fail create secondary link error = %d\n",
				__func__, rc);
			return -ENODEV;
		}
	}

	thp_vm_log_info(cd, "%s succeeded\n", __func__);

	return rc;
}

#endif //(defined CONFIG_HONOR_TRUSTED_TOUCH) && (IS_ENABLED(CONFIG_HONOR_THP_QCOM))

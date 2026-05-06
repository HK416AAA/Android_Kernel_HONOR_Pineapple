/*
  * Copyright (c) Honor Device Co., Ltd. 2023-2023. All rights reserved.
  * Description: honor subsystem data monitor
  * Author: chenliucan
  * Create: 2023-08-22
  */
#define DEBUG
#define pr_fmt(fmt) "hn_monitor: %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/soc/qcom/qmi.h>
#include "honor_monitor.h"
#include "securec.h"

#define QMI_PING_SERVICE_VERSION_V01 1
#define QMI_PING_SERVICE_ID_V01 15
#define QMI_ROOTPD_INSTANCE_ID_V01 32

#define QMI_HN_DATA_REQ1_TLV_TYPE 0x01
#define QMI_HN_DATA_RESP1_TLV_TYPE 0x02
#define QMI_HN_DATA_OPT1_TLV_TYPE 0x10
#define QMI_HN_DATA_OPT2_TLV_TYPE 0x11

#define QMI_HN_DATA_SIZE_V01 2048
#define TEST_MAX_NAME_SIZE_V01 255
#define MAX_ITEM_LEN 128

#define QMI_HN_DATA_REQ_MAX_MSG_LEN_V01 2312
#define QMI_HN_DATA_RESP_MAX_MSG_LEN_V01 2319

#define QMI_HN_DATA_REQ_V01 0x0029
#define QMI_HN_DATA_RESP_V01 0x0029

#define QMI_HN_SUB_MSGID_GET_ISLAND_INFO 4

struct hn_msg_type {
	int sub_msgid;
	char sub_data;
};

struct user_info_type {
	int is_beta_ver;
	int is_factory_ver;
};

struct hiview_reprot_type {
	int reprot_id;
	char pn_name[MAX_ITEM_LEN];
	char f1_name[MAX_ITEM_LEN];
	char f2_name[MAX_ITEM_LEN];
};

// clang-format off
enum usleep_internal_state {
	/* Steady mode state */
	SLEEP_INTERNAL_STATE_IN_DDR			= 0x00000001, /* Normal operational mode */
	SLEEP_INTERNAL_STATE_ACTIVE			= 0x00000002, /* Island operational mode */
	SLEEP_INTERNAL_STATE_STDY_RSVD4			= 0x00000004,
	SLEEP_INTERNAL_STATE_STDY_RSVD8			= 0x00000008,

	SLEEP_INTERNAL_STATE_STDY_MASK			= 0x0000000F,

	/* Entry transitions */
	SLEEP_INTERNAL_STATE_INIT_ENTRY			= 0x00000010, /* Initial entry request stage -
								       * Setup and notification callbacks */
	SLEEP_INTERNAL_STATE_PREPARE_ENTRY		= 0x00000020, /* Performing PRE-HW/kernel entry routines */
	SLEEP_INTERNAL_STATE_STAGE1_ENTRY		= 0x00000040, /* Performing HW/Kernel stage 1 entry */
	SLEEP_INTERNAL_STATE_CONT_ENTRY			= 0x00000080, /* Continuing entry after Kernel stage 1 */
	SLEEP_INTERNAL_STATE_CMPLT_ENTRY		= 0x00000100, /* Completing entry after any HW
								       * requirements are complete */
	SLEEP_INTERNAL_STATE_RSVD2_ENTRY		= 0x00000200,
	SLEEP_INTERNAL_STATE_RSVD4_ENTRY		= 0x00000400,
	SLEEP_INTERNAL_STATE_RSVD8_ENTRY		= 0x00000800,

	SLEEP_INTERNAL_STATE_ENTRY_MASK			= 0x00000FF0,

	/* Normal exit transitions */
	SLEEP_INTERNAL_STATE_INIT_EXIT			= 0x00001000, /* Initial exit stage requested */
	SLEEP_INTERNAL_STATE_STAGE1_EXIT		= 0x00002000, /* Initial exit stage, where exit preperation
								       * is starting */
	SLEEP_INTERNAL_STATE_STAGE2_EXIT		= 0x00004000, /* Exit prep is done, continuing with
								       * first kernel stage exit */
	SLEEP_INTERNAL_STATE_RSVD8_EXIT			= 0x00008000,
	SLEEP_INTERNAL_STATE_RSVD10_EXIT		= 0x00010000,
	SLEEP_INTERNAL_STATE_RSVD20_EXIT		= 0x00020000,
	SLEEP_INTERNAL_STATE_RSVD40_EXIT		= 0x00040000,
	SLEEP_INTERNAL_STATE_RSVD80_EXIT		= 0x00080000,

	SLEEP_INTERNAL_STATE_EXIT_MASK			= 0x000FF000,

	/* Fatal error exit transitions */
	SLEEP_INTERNAL_STATE_FATAL_EXIT			= 0x00100000, /* Fatal error exit stage */

	/* Exited states */
	SLEEP_INTERNAL_STATE_FAST_EXIT			= 0x01000000,
	SLEEP_INTERNAL_STATE_FULL_EXIT			= 0x02000000,

	SLEEP_INTERNAL_STATE_MAX
};
// clang-format on

enum usleep_island_type {
	ISLD_SPEC0 = 0x0001,
	ISLD_SPEC1 = 0x0002,
	ISLD_SPEC2 = 0x0004,
	ISLD_SPEC3 = 0x0008,
	ISLD_SPEC4 = 0x0010,
	ISLD_SPEC5 = 0x0020,
	ISLD_SPEC6 = 0x0040,
	ISLD_SPEC7 = 0x0080,
	ISLD_SPEC8 = 0x0100,
	ISLD_SPEC9 = 0x0200,
	ISLD_SPEC10 = 0x0400,
	ISLD_SPEC11 = 0x0800,
	ISLD_SPEC12 = 0x1000,
	ISLD_SPEC13 = 0x2000,
	ISLD_SPEC14 = 0x4000,
	ISLD_SPEC15 = 0x8000
};

/* Enum values for early exit tracking */
enum usleep_trans_entry_status {
	UIMAGE_EARLY_EXIT_CANCEL = 0, /* Kernel island entry canceled */
	UIMAGE_EARLY_EXIT_OK_STG1, /* Caught exit request while entering island */
	UIMAGE_EARLY_EXIT_OK_STG2, /* Island entry caught exit request */
	UIMAGE_ENTER_SUCCESS, /* Island entry completed successfully */
	UIMAGE_ENTER_STATUS_MAX = UIMAGE_ENTER_SUCCESS
};

struct sleep_stats_generic {
	u32 count; /* measurement count */
	u64 total; /* measurement total */
	u64 max; /* measurement maximum value */
	u64 min; /* measurement minimum value*/
};

/*Data structure for tracking the mode transition times */
struct usleep_transition_profiling {
	/* Timestamp of last uSleep entry points */
	u64 entry_start_time;
	u64 entry_end_time;

	/* Timestamp of last uSleep exit points */
	u64 exit_start_time;
	u64 exit_end_time;

	/* Simple count of the times we exited while in the middle of entry at various points */
	u32 early_exits_cnt[3];

	/* Statistics for total time spent in island */
	struct sleep_stats_generic time_in_island;
	u64 start_of_island_operation; /* Timestamp of the start of
									 * island operation */
	u64 last_time_in_island; /* Amount of time spent in the last
									 * island cycle */
	enum usleep_trans_entry_status
		last_island_entry_status; /* Status of last island entry */
};

struct usleep_global_data {
	enum usleep_internal_state state; /* Current internal uSleep state */
	enum usleep_island_type active_island; /* Specific island that's active */
	u64 ddr_wakeup_timer; /* DDR wakeup timer deadline */
};

struct test_name_type_v01 {
	u32 name_len;
	char name[TEST_MAX_NAME_SIZE_V01];
};

static struct qmi_elem_info test_name_type_v01_ei[] = {
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
		.offset = offsetof(struct test_name_type_v01, name_len),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = TEST_MAX_NAME_SIZE_V01,
		.elem_size = sizeof(char),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
		.offset = offsetof(struct test_name_type_v01, name),
	},
	{}};

struct hn_data_req_msg_v01 {
	u32 data_len;
	u8 data[QMI_HN_DATA_SIZE_V01];

	u8 client_name_valid;
	struct test_name_type_v01 client_name;
};

static struct qmi_elem_info hn_data_req_msg_v01_ei[] = {
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof(u32),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_HN_DATA_REQ1_TLV_TYPE,
		.offset = offsetof(struct hn_data_req_msg_v01, data_len),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = QMI_HN_DATA_SIZE_V01,
		.elem_size = sizeof(u8),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = QMI_HN_DATA_REQ1_TLV_TYPE,
		.offset = offsetof(struct hn_data_req_msg_v01, data),
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_HN_DATA_OPT1_TLV_TYPE,
		.offset =
			offsetof(struct hn_data_req_msg_v01, client_name_valid),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct test_name_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_HN_DATA_OPT1_TLV_TYPE,
		.offset = offsetof(struct hn_data_req_msg_v01, client_name),
		.ei_array = test_name_type_v01_ei,
	},
	{}};

struct hn_data_resp_msg_v01 {
	struct qmi_response_type_v01 resp;

	u8 data_valid;
	u32 data_len;
	u8 data[QMI_HN_DATA_SIZE_V01];

	u8 service_name_valid;
	struct test_name_type_v01 service_name;
};

static struct qmi_elem_info hn_data_resp_msg_v01_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_HN_DATA_RESP1_TLV_TYPE,
		.offset = offsetof(struct hn_data_resp_msg_v01, resp),
		.ei_array = qmi_response_type_v01_ei,
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_HN_DATA_OPT1_TLV_TYPE,
		.offset = offsetof(struct hn_data_resp_msg_v01, data_valid),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof(u32),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_HN_DATA_OPT1_TLV_TYPE,
		.offset = offsetof(struct hn_data_resp_msg_v01, data_len),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = QMI_HN_DATA_SIZE_V01,
		.elem_size = sizeof(u8),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = QMI_HN_DATA_OPT1_TLV_TYPE,
		.offset = offsetof(struct hn_data_resp_msg_v01, data),
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_HN_DATA_OPT2_TLV_TYPE,
		.offset = offsetof(struct hn_data_resp_msg_v01,
				   service_name_valid),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct test_name_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = QMI_HN_DATA_OPT2_TLV_TYPE,
		.offset = offsetof(struct hn_data_resp_msg_v01, service_name),
		.ei_array = test_name_type_v01_ei,
	},
	{}};

struct hn_subsys_qmi_monitor {
	struct qmi_handle qmi;
	struct sockaddr_qrtr hnctl;
	struct dentry *hn_debug_dir;
	struct dentry *hn_usleep;
	struct platform_device *pdev;
	struct hn_msg_type hn_cmd;
	int hnctl_version;
	int hnctl_instance;
};

static struct hn_subsys_qmi_monitor g_monitor;

int honor_monitor_stats_show(void)
{
	struct qmi_handle *qmi = &g_monitor.qmi;
	struct hn_data_req_msg_v01 *req;
	struct hn_data_resp_msg_v01 *resp;
	struct usleep_global_data *sleepdata;
	struct usleep_transition_profiling *profilingdata;
	u32 profiling_len;
	u32 num;
	struct qmi_txn txn;
	int i;
	int ret;

	if (!g_monitor.hnctl_version || !g_monitor.hnctl_instance) {
		pr_err("qmi handler invalid\n");
		return -1;
	}

	g_monitor.hn_cmd.sub_msgid = QMI_HN_SUB_MSGID_GET_ISLAND_INFO;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp) {
		kfree(req);
		return -ENOMEM;
	}

	memcpy_s(req->data, sizeof(req->data), &g_monitor.hn_cmd,
		 sizeof(struct hn_msg_type));
	req->data_len = sizeof(struct hn_msg_type);
	pr_debug("req data len:%d\n", req->data_len);

	ret = qmi_txn_init(qmi, &txn, hn_data_resp_msg_v01_ei, resp);
	if (ret < 0) {
		pr_err("qmi_txn_init fail\n");
		goto out;
	}

	ret = qmi_send_request(qmi, &g_monitor.hnctl, &txn, QMI_HN_DATA_REQ_V01,
			       QMI_HN_DATA_REQ_MAX_MSG_LEN_V01,
			       hn_data_req_msg_v01_ei, req);
	if (ret < 0) {
		pr_err("qmi_send_request fail, ret:%d\n", ret);
		qmi_txn_cancel(&txn);
		goto out;
	}

	ret = qmi_txn_wait(&txn, 1 * HZ);
	if (ret < 0) {
		pr_err("qmi_txn_wait fail, ret:%d\n", ret);
		goto out;
	} else if (!resp->data_valid ||
		   resp->data_len < sizeof(struct usleep_global_data)) {
		pr_err("resp len error valid:%d, len:%d != usleep_global_data size:%d\n",
		       resp->data_valid, resp->data_len,
		       sizeof(struct usleep_global_data));
		goto out;
	}

	sleepdata = (struct usleep_global_data *)resp->data;
	profiling_len = resp->data_len - sizeof(struct usleep_global_data);
	num = profiling_len / sizeof(struct usleep_transition_profiling);
	profilingdata =
		(struct usleep_transition_profiling
			 *)(resp->data + sizeof(struct usleep_global_data));

	for (i = 0; i < num; i++) {
		pr_debug("island spec%d:\n", i);
		pr_debug("count=%lu  total=%lu(ms)  avs=%lu(ms)\n",
			 profilingdata[i].time_in_island.count,
			 profilingdata[i].time_in_island.total / 19200,
			 profilingdata[i].time_in_island.total /
				 (profilingdata[i].time_in_island.count *
				  19200));
	}
	ret = 0;
out:
	kfree(resp);
	kfree(req);
	return ret;
}
EXPORT_SYMBOL(honor_monitor_stats_show);

static ssize_t usleep_write(struct file *file, const char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	int ret = count;
	honor_monitor_stats_show();
	return ret;
}

static const struct file_operations hn_usleep_fops = {
	.open = simple_open,
	.write = usleep_write,
};

static int hn_monitor_qmi_new_server(struct qmi_handle *qmi,
				     struct qmi_service *svc)
{
	struct hn_subsys_qmi_monitor *monitor = &g_monitor;
	if (svc->version == QMI_PING_SERVICE_VERSION_V01) {
		pr_debug("server version match:%d\n", svc->version);
		if (svc->instance == QMI_ROOTPD_INSTANCE_ID_V01) {
			pr_debug("server instance match:%d\n", svc->instance);
			monitor->hnctl_version = svc->version;
			monitor->hnctl_instance = svc->instance;
			monitor->hnctl.sq_family = AF_QIPCRTR;
			monitor->hnctl.sq_node = svc->node;
			monitor->hnctl.sq_port = svc->port;
		}
	}
	return 0;
}

static void hn_monitor_qmi_del_server(struct qmi_handle *qmi,
				      struct qmi_service *svc)
{
	struct hn_subsys_qmi_monitor *monitor = &g_monitor;
	if (svc->version == QMI_PING_SERVICE_VERSION_V01) {
		pr_debug("server version match:%d\n", svc->version);
		if (svc->instance == QMI_ROOTPD_INSTANCE_ID_V01) {
			pr_debug("server instance match:%d\n", svc->instance);
			monitor->hnctl_version = 0;
			monitor->hnctl_instance = 0;
			monitor->hnctl.sq_family = 0;
			monitor->hnctl.sq_node = 0;
			monitor->hnctl.sq_port = 0;
		}
	}
}

static const struct qmi_ops hn_monitor_qmi_ops = {
	.new_server = hn_monitor_qmi_new_server,
	.del_server = hn_monitor_qmi_del_server,
};

static int hn_monitor_probe(struct platform_device *pdev)
{
	int ret;

	ret = qmi_handle_init(&g_monitor.qmi, QMI_HN_DATA_RESP_MAX_MSG_LEN_V01,
			      &hn_monitor_qmi_ops, NULL);
	if (ret < 0) {
		pr_err("qmi_handle_init failed ret=%d\n", ret);
	} else {
		ret = qmi_add_lookup(&g_monitor.qmi, QMI_PING_SERVICE_ID_V01,
				     QMI_PING_SERVICE_VERSION_V01,
				     QMI_ROOTPD_INSTANCE_ID_V01);
		if (ret < 0) {
			pr_err("qmi_add_lookup failed ret=%d\n", ret);
			qmi_handle_release(&g_monitor.qmi);
		}
	}

	return ret;
}

static int hn_monitor_remove(struct platform_device *pdev)
{
	if (!g_monitor.hnctl_version || !g_monitor.hnctl_instance) {
		qmi_handle_release(&g_monitor.qmi);
	}

	return 0;
}

static struct platform_driver hn_monitor_driver = {
	.probe = hn_monitor_probe,
	.remove = hn_monitor_remove,
	.driver =
		{
			.name = "hn_monitor_client",
		},
};

static int honor_monitor_init(void)
{
	struct platform_device *pdev;
	int ret;
	g_monitor.hn_debug_dir = debugfs_create_dir("hn_monitor", NULL);
	if (IS_ERR(g_monitor.hn_debug_dir)) {
		pr_err("failed to create g_monitor dir\n");
		return PTR_ERR(g_monitor.hn_debug_dir);
	}

	g_monitor.hn_usleep = debugfs_create_file("uSleep", 0644,
						  g_monitor.hn_debug_dir,
						  &g_monitor, &hn_usleep_fops);
	if (IS_ERR(g_monitor.hn_usleep)) {
		ret = PTR_ERR(g_monitor.hn_usleep);
		goto err_remove_debug_dir;
	}

	ret = platform_driver_register(&hn_monitor_driver);
	if (ret) {
		goto err_remove_debug_dir;
	}

	pdev = platform_device_alloc("hn_monitor_client", PLATFORM_DEVID_AUTO);
	if (!pdev) {
		ret = -ENOMEM;
		goto err_unregister_driver;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		goto err_put_device;
	}
	g_monitor.pdev = pdev;

	return 0;

err_put_device:
	platform_device_put(pdev);
err_unregister_driver:
	platform_driver_unregister(&hn_monitor_driver);
err_remove_debug_dir:
	debugfs_remove_recursive(g_monitor.hn_debug_dir);

	return ret;
}

static void honor_monitor_exit(void)
{
	if (g_monitor.pdev) {
		platform_device_unregister(g_monitor.pdev);
		g_monitor.pdev = NULL;
	}
	platform_driver_unregister(&hn_monitor_driver);
	if (g_monitor.hn_debug_dir) {
		debugfs_remove_recursive(g_monitor.hn_debug_dir);
	}
}

module_init(honor_monitor_init);
module_exit(honor_monitor_exit);

MODULE_DESCRIPTION("Honor subsystem data monitor");
MODULE_LICENSE("GPL v2");

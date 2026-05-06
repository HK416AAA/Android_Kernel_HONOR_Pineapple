

#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/skbuff.h>
#include <linux/uaccess.h>
#include <net/netlink.h>

#include "hn_dc_interface.h"
#include "hn_dc_util.h"

#define HN_DC_DIR "hn_dc"
#define DC_DIR "dc"
#define INFO_DIR "info"

static struct proc_dir_entry *g_hn_dc_proc_dir = NULL;

static struct sock *g_dc_nlfd;
static unsigned int g_user_space_pid = 0;

static int proc_info_get(struct seq_file *file, void *data)
{
	struct dc_instance *dc = file->private;

	if (dc == NULL) {
		dc_mloge("dc null");
		return -EFAULT;
	}

	if ((dc->dev[TYPE_MASTER] == NULL) || (dc->dev[TYPE_SLAVE] == NULL)) {
		dc_mloge("dev null");
		return -EFAULT;
	}

#if HN_DC_EARNINGS
	seq_printf(file, "%10s %10s %16s %16s %20s %20s %20s %20s\n", "master",
		   "send", "rx_master_total", "rx_master_drop",
		   "master_latency", "master_miss_fake", "master_miss_real",
		   "high_latency_cnt");

	seq_printf(file, "%10s %10u %16lu %16lu %16lu %16lu %16lu %16lu\n",
		   dc->dev[TYPE_MASTER]->name, dc->sequence,
		   dc->rx_master_total, dc->rx_master_drop,
		   (unsigned long)(ktime_to_ms(dc->master_latency << 19)),
		   dc->master_miss_fake, dc->master_miss_real,
		   dc->high_latency_cnt);

	seq_printf(file, "%10s %10s %16s %16s\n", "slave", "send",
		   "rx_slave_total", "rx_slave_drop");

	seq_printf(file, "%10s %10u %16lu %16lu\n", dc->dev[TYPE_SLAVE]->name,
		   dc->sequence, dc->rx_slave_total, dc->rx_slave_drop);
#else
	seq_printf(file, "%16s    %16s    %10s    %10s    %10s\n", "master",
		   "slave", "send", "receive", "drop");
	seq_printf(file, "%16s    %16s    %10u    %10u    %10u\n",
		   dc->dev[TYPE_MASTER]->name, dc->dev[TYPE_SLAVE]->name,
		   dc->sequence, dc->seq.former_recv_seq,
		   dc->seq.later_recv_seq);
#endif
	return HN_DC_OK;
}

static int proc_info_open(struct inode *inode, struct file *file)
{
	if ((inode == NULL) || (file == NULL)) {
		dc_mloge("inode/file null");
		return -EFAULT;
	}
	return single_open(file, proc_info_get, pde_data(inode));
}

static struct proc_ops g_hn_dc_info_ops = {
	// .owner = THIS_MODULE,
	.proc_open = proc_info_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

void hn_dc_proc_create(void)
{
	g_hn_dc_proc_dir = proc_mkdir(HN_DC_DIR, NULL);
	if (g_hn_dc_proc_dir == NULL) {
		dc_mloge("mkdir failed");
		return;
	}
}

void hn_dc_proc_remove(void)
{
	proc_remove(g_hn_dc_proc_dir);
}

void hn_dc_proc_create_dc(const struct dc_instance *dc)
{
	struct proc_dir_entry *proc_dir = NULL;

	if (dc == NULL) {
		dc_mloge("dc is NULL");
		return;
	}

	proc_dir = proc_mkdir(DC_DIR, g_hn_dc_proc_dir);
	if (proc_dir == NULL) {
		dc_mloge("mkdir failed");
		return;
	}

	proc_create_data(INFO_DIR, S_IRUGO, proc_dir, &g_hn_dc_info_ops,
			 (void *)dc);
}

void hn_dc_proc_delete_dc(void)
{
	remove_proc_entry(DC_DIR, g_hn_dc_proc_dir);
}

static void netlink_msg_proc(struct nlmsghdr *nlh, const char *data)
{
	struct hdc_msg *hdc = (struct hdc_msg *)data;
	struct dc_mngr *dcm = get_dcm();
	int ret;

	if (nlh->nlmsg_type == DC_NETLINK_START_STOP) {
		if (hdc == NULL ||
			nlh->nlmsg_len < sizeof(struct nlmsghdr) + sizeof(struct hdc_msg)) {
			dc_mloge("invalid msg");
			return;
		}

		dc_mlogd("%s %s %d %d", hdc->name_master, hdc->name_slave,
			 hdc->enable, hdc->game_uid);
		if (!hdc->enable) {
			dc_clear();
			return;
		}

		ret = dc_insert(hdc->name_master, hdc->name_slave,
				hdc->game_uid);
		if (ret != HN_DC_SUCC)
			dc_mloge("insert dc failed");
	} else if (nlh->nlmsg_type == DC_NETLINK_STOP_PARAM) {
#if HN_DC_EARNINGS
		if (nlh->nlmsg_len < sizeof(struct nlmsghdr) + sizeof(uint32_t)) {
			dc_mloge("invalid msg");
			return;
		}
		dcm->high_latency = *(uint32_t *)data;
		dc_mlogd("set dc high latency threshold:%d", dcm->high_latency);
#endif
	}
}

static void kernel_netlink_receive(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = NULL;
	struct netlink_msg2knl *msg = NULL;

	if ((skb != NULL) && (skb->len >= NLMSG_HDRLEN)) {
		nlh = nlmsg_hdr(skb);
		g_user_space_pid = nlh->nlmsg_pid;

		/* do not accept no data msg */
		if ((nlh->nlmsg_len > sizeof(struct nlmsghdr)) &&
		    (skb->len >= nlh->nlmsg_len)) {
			msg = (struct netlink_msg2knl *)nlh;
			netlink_msg_proc(nlh, (char *)(msg->data));
		}
	}
}

int hn_dc_notify_event(struct udp_report *udp_sk)
{
	int ret = -1;
	int size = -1;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	void *pdata = NULL;
	gfp_t gfp = GFP_ATOMIC;

	if (!g_user_space_pid || !g_dc_nlfd) {
		dc_mloge("hn_dc_notify_event: cannot notify pid 0 or nlfd 0");
		ret = -1;
		goto end;
	}

	size = sizeof(struct udp_report);
	if (unlikely(!in_interrupt() && !in_atomic())) {
		gfp = GFP_KERNEL;
	}
	skb = nlmsg_new(size, gfp);

	if (!skb) {
		dc_mloge("hn_dc_notify_event: alloc skb fail");
		ret = -1;
		goto end;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, size, 0);
	if (!nlh) {
		dc_mloge("hn_dc_notify_event: notify_event fail");
		kfree_skb(skb);
		skb = NULL;
		ret = -1;
		goto end;
	}

	pdata = nlmsg_data(nlh);

	if (memcpy_s(pdata, size, udp_sk, sizeof(struct udp_report)) != EOK) {
		dc_mloge("hn_dc_notify_event: error EOK %s %d", __func__,
			 __LINE__);
		kfree_skb(skb);
		skb = NULL;
		ret = -1;
		goto end;
	}
	ret = netlink_unicast(g_dc_nlfd, skb, g_user_space_pid, MSG_DONTWAIT);
end:
	return ret;
}

int hn_dc_netlink_init(void)
{
	int ret = HN_DC_SUCC;

	struct netlink_kernel_cfg netlink_ops = {
		.input = kernel_netlink_receive,
	};

	g_dc_nlfd =
		netlink_kernel_create(&init_net, NETLINK_HN_DC, &netlink_ops);
	if (g_dc_nlfd == NULL) {
		dc_mloge("create netlink failed");
		ret = HN_DC_FAIL;
	}
	return ret;
}

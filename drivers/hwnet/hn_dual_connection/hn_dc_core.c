

#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <linux/udp.h>
#include <linux/version.h>
#include <uapi/linux/time.h>
#include "hn_dc_interface.h"
#include "hn_dc_util.h"

static int dhcp_packet(const struct iphdr *iph)
{
	struct udphdr *udph = (void *)iph + (iph->ihl << 2);
	if ((udph != NULL) && (iph->protocol == IPPROTO_UDP) &&
	    ((ntohs(udph->dest) == DHCP_SERVER_PORT) ||
	     (ntohs(udph->dest) == DHCP_CLIENT_PORT) ||
	     (ntohs(udph->dest) == DNS_PORT))) {
		return HN_DC_TRUE;
	}
	return HN_DC_FALSE;
}

/* Game packet: TCP/UDP short packet(from real packet's analysis) */
static inline int packet_valid(const struct iphdr *iph,
			       const struct sk_buff *skb)
{
	if (((iph->protocol == IPPROTO_TCP) || (iph->protocol == IPPROTO_UDP)))
		return HN_DC_SUCC;

	return HN_DC_FAIL;
}

static inline void checksum_update(const struct sk_buff *skb, int csum_offset)
{
	__wsum csum;

	csum = skb_checksum(skb, IP_HDR_OFFSET, skb->len - IP_HDR_OFFSET, 0);
	*(__sum16 *)(skb->data + csum_offset) = csum_fold(csum) ?:
								  CSUM_MANGLED_0;
}

static inline void tcp_checksum_update(const struct sk_buff *skb)
{
	int tcp_csum_offset;

	tcp_csum_offset = IP_HDR_OFFSET + TCP_CHECKSUM_OFFSET; // 34 + 16 = 50
	checksum_update(skb, tcp_csum_offset);
}

static inline void udp_checksum_update(const struct sk_buff *skb)
{
	int udp_csum_offset;

	udp_csum_offset = IP_HDR_OFFSET + UDP_CHECKSUM_OFFSET; // 34 + 6 = 40
	checksum_update(skb, udp_csum_offset);
}

static inline int _inet_ntoa(__be32 ip, char *addr, int addr_len)
{
	char *p = (char *)&ip;

	return snprintf_s(addr, addr_len, MAX_IP_ADDR_LEN - 1, "%u.%u.%u.%u",
			  (((int)p[0]) & 0xff), (((int)p[1]) & 0xff),
			  (((int)p[2]) & 0xff), (((int)p[3]) & 0xff));
}

static int udp_socket_report(struct iphdr *iph, struct dc_mngr *dcm)
{
	struct udphdr *udph = NULL;
	struct udp_tuple *sk_tmp = NULL;
	struct udp_tuple *p_udp_sk = NULL;

	udph = (void *)iph + (iph->ihl << 2);
	if (udph == NULL) {
		return HN_DC_FAIL;
	}

	mutex_lock(&udp_sk_list_mutex);
	list_for_each_entry(p_udp_sk, dcm->udp_sk_list_head, list) {
		if (p_udp_sk->saddr == iph->saddr &&
		    p_udp_sk->daddr == iph->daddr &&
		    p_udp_sk->s_port == udph->source &&
		    p_udp_sk->d_port == udph->dest) {
			sk_tmp = p_udp_sk;
			break;
		}
	}

	if (sk_tmp == NULL) {
		struct udp_report udp_sk;
		char src_ip[MAX_IP_ADDR_LEN] = {0};
		char dst_ip[MAX_IP_ADDR_LEN] = {0};
		gfp_t gfp = GFP_ATOMIC;

		if (unlikely(!in_interrupt() && !in_atomic())) {
			gfp = GFP_KERNEL;
		}
		sk_tmp = kmalloc(sizeof(struct udp_tuple), gfp);

		if (sk_tmp == NULL) {
			mutex_unlock(&udp_sk_list_mutex);
			return HN_DC_FAIL;
		}
		sk_tmp->saddr = iph->saddr;
		sk_tmp->daddr = iph->daddr;
		sk_tmp->s_port = udph->source;
		sk_tmp->d_port = udph->dest;

		list_add(&sk_tmp->list, dcm->udp_sk_list_head);
		mutex_unlock(&udp_sk_list_mutex);

		if (_inet_ntoa(iph->saddr, src_ip, sizeof(src_ip)) < 0) {
			return HN_DC_FAIL;
		}
		if (_inet_ntoa(iph->daddr, dst_ip, sizeof(dst_ip)) < 0) {
			return HN_DC_FAIL;
		}

		if (memcpy_s(udp_sk.src_ip, sizeof(udp_sk.src_ip), src_ip,
			     sizeof(src_ip)) != EOK) {
			return HN_DC_FAIL;
		}
		if (memcpy_s(udp_sk.dst_ip, sizeof(udp_sk.dst_ip), dst_ip,
			     sizeof(dst_ip)) != EOK) {
			return HN_DC_FAIL;
		}
		udp_sk.s_port = ntohs(udph->source);
		udp_sk.d_port = ntohs(udph->dest);
		hn_dc_notify_event(&udp_sk);
	} else {
		mutex_unlock(&udp_sk_list_mutex);
	}

	return HN_DC_SUCC;
}

static int game_packet(const struct sk_buff *skb)
{
	struct iphdr *iph = NULL;
	__be16 protocol;
	int uid = 0;
	struct dc_mngr *dcm = get_dcm();

	/* Unicast */
	if (!is_unicast_ether_addr(skb->data))
		return HN_DC_FAIL;

	/* data: dstMac[6] | srcMac[6] | protoType[2] */
	protocol = *(__be16 *)(&(skb->data[ETH_HDR_OFFSET]));

	if (protocol != htons(ETH_P_IP))
		return HN_DC_FAIL;

	iph = (struct iphdr *)(skb->data + ETH_HDR_OFFSET + LENGTH_OF_TYPE);

	if (dhcp_packet(iph))
		return HN_DC_FAIL;

	if (!packet_valid(iph, skb))
		return HN_DC_FAIL;

	if (iph->saddr == 0x0100007F) { // no loop IP
		return HN_DC_FAIL;
	}

	if (!skb->sk || !sk_fullsock(skb->sk))
		return HN_DC_FAIL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 10)
	uid = sock_i_uid(skb->sk).val;
#else
	uid = sock_i_uid(skb->sk);
#endif

	if (!dcm->game_uid || !uid || dcm->game_uid != uid) {
		return HN_DC_FAIL;
	}

	if (iph->protocol == IPPROTO_UDP) {
		udp_checksum_update(skb);
		return udp_socket_report(iph, dcm);
	} else if (iph->protocol == IPPROTO_TCP) {
		tcp_checksum_update(skb);
	}

	return HN_DC_SUCC;
}

static int send_check(const struct sk_buff *skb)
{
	struct dc_mngr *dcm = get_dcm();

	if (likely(atomic_read(&dcm->dc_enable) == DC_DISABLE))
		return HN_DC_FAIL;

	return game_packet(skb);
}

static struct sk_buff *
insert_dc_header(struct sk_buff *skb, const __be16 dc_proto, const u16 sequence)
{
	struct dc_ethhdr *dc_eth = NULL;

	if (unlikely(skb_cow_head(skb, DC_HLEN) < MINIMUM_SPACE)) {
		dc_mloge("no room for dc header");
		return NULL;
	}

	dc_eth = (struct dc_ethhdr *)skb_push(skb, DC_HLEN);
	if (dc_eth == NULL) {
		dc_mloge("skb_push failed, dc_eth is NULL");
		return NULL;
	}

	// data: dstMac[6] | srcMac[6] | protoType[2]
	if (memmove_s(skb->data, ETH_ALEN * MAC_ADDR_NUM, skb->data + DC_HLEN,
		      ETH_ALEN * MAC_ADDR_NUM) != EOK) {
		dc_mloge("skb memmove failed");
		return NULL;
	}
	skb->mac_header -= DC_HLEN;
	dc_eth->dc_proto = dc_proto;
	dc_eth->sequence = htons(sequence);
	return skb;
}

static void dc_header_rollback(struct sk_buff *skb)
{
	// data: dstMac[6] | srcMac[6] | protoType[2]
	if (memmove_s(skb->data + DC_HLEN, ETH_ALEN * MAC_ADDR_NUM, skb->data,
		      ETH_ALEN * MAC_ADDR_NUM) != EOK) {
		dc_mloge("skb rollback failed");
		return;
	}

	skb_pull(skb, DC_HLEN);
}

static int delete_dc_header(struct sk_buff *skb, u16 *sequence)
{
	struct dc_hdr *dchdr = (struct dc_hdr *)skb->data;

	skb_pull_rcsum(skb, DC_HLEN);
	skb->protocol = dchdr->encap_proto;
	*sequence = ntohs(dchdr->sequence);

	/* data: dstMac[6] | srcMac[6] | protoType[2] */
	if (memmove_s(skb->data - ETH_HLEN, ETH_ALEN * MAC_ADDR_NUM,
		      skb->data - DC_ETH_HLEN,
		      ETH_ALEN * MAC_ADDR_NUM) != EOK) {
		return NET_RX_DROP;
	}

	skb->mac_header += DC_HLEN;
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_reset_mac_len(skb);
	return NET_RX_SUCCESS;
}

static int encap_packet(struct sk_buff *skb, const struct net_device *dev,
			struct dc_ecap_info *encap)
{
	struct dc_mngr *dcm = get_dcm();
	struct dc_instance *dc = &dcm->dc;

	if (insert_dc_header(skb, htons(DC_PROTO), dc->sequence) == NULL) {
		dc_mloge("add header failed");
		return HN_DC_FAIL;
	}

	encap->dev = get_pair_dev(dc, dev);
	if (encap->dev == NULL) {
		dc_header_rollback(skb);
		return HN_DC_FAIL;
	}

	dc->sequence++;
	dev_hold(encap->dev);
	return HN_DC_SUCC;
}

static void send_copy(struct sk_buff *skb, const struct net_device *dev)
{
	int rc;
	struct sk_buff *skb2 = NULL;
	struct dc_ecap_info encap = {0};
	gfp_t gfp = GFP_ATOMIC;

	if (encap_packet(skb, dev, &encap) == HN_DC_FAIL)
		return;

	if (skb) {
		skb->priority = DC_PRI_VO;
	}

	if (unlikely(!in_interrupt() && !in_atomic())) {
		gfp = GFP_KERNEL;
	}
	skb2 = skb_copy(skb, gfp);

	if (skb2 == NULL) {
		dc_header_rollback(skb);
		dev_put(encap.dev);
		return;
	}

	skb2->dev = encap.dev;

	// change_src_mac
	if (memcpy_s(skb2->data + ETH_ALEN, ETH_ALEN, skb2->dev->dev_addr,
		     ETH_ALEN) != EOK) {
		dc_mloge("memcpy skb2 fail");
		dc_header_rollback(skb);
		dev_put(encap.dev);
		kfree_skb(skb2);
		skb2 = NULL;
		return;
	}

	skb2->priority = DC_PRI_VO;
	rc = skb2->dev->netdev_ops->ndo_start_xmit(skb2, skb2->dev);
	if (rc != NETDEV_TX_OK) {
		dc_mloge("tx fail");
		dc_header_rollback(skb);
		dev_put(encap.dev);
		kfree_skb(skb2);
		skb2 = NULL;
		return;
	}
	dev_put(encap.dev);

	return;
}

static void hn_dc_send_dc_copy(struct sk_buff *skb, struct net_device *dev)
{
	if ((skb == NULL) || (dev == NULL)) {
		dc_mloge("skb/dev is NULL");
		return;
	}

	if (send_check(skb) == HN_DC_SUCC)
		send_copy(skb, dev);
}

static int hn_dc_check_dup_packet(struct sk_buff *skb)
{
	struct dc_instance *dc = NULL;
	unsigned short sequence = 0;
	unsigned char seq_high = 0;
	unsigned char seq_low = 0;
	struct dc_mngr *dcm = get_dcm();
#if HN_DC_EARNINGS
	ktime_t time;
	unsigned long master_latency;
#endif

	if (skb == NULL) {
		dc_mloge("skb is NULL");
		return NET_RX_DROP;
	}

	if (skb->protocol != cpu_to_be16(DC_PROTO)) {
		return NET_RX_SUCCESS;
	}

	if (skb->len < DC_HLEN) {
		kfree_skb(skb);
		skb = NULL;
		return NET_RX_DROP;
	}

	if (delete_dc_header(skb, &sequence)) {
		return NET_RX_DROP;
	}

	seq_high = (sequence >> 8) & 0x00FF;
	seq_low = sequence & 0x00FF;

	if (atomic_read(&dcm->dc_enable) == DC_DISABLE) {
		dc_mloge("dc disable");
		return NET_RX_SUCCESS;
	}
	dc = &dcm->dc;

#if HN_DC_EARNINGS
	// HN_DC_SUCC 表示后收到
	if (check_packet_status(dc, sequence) == HN_DC_SUCC) {
		dc->seq.later_recv_seq = sequence;
		if (skb->dev == dc->dev[TYPE_SLAVE]) {
			dc->rx_slave_drop++;
			dc->rx_slave_total++;
		} else if (skb->dev == dc->dev[TYPE_MASTER]) {
			dc->rx_master_drop++;
			dc->rx_master_total++;
			time = ktime_get();
			if (likely(dc->rx_ts[seq_low].seq_high_8bits ==
				   seq_high) &&
			    likely(dc->rx_ts[seq_low].ts)) {
				master_latency =
					(unsigned long)((time >> 19) -
							dc->rx_ts[seq_low].ts);
				dc->rx_ts[seq_low].ts = 0;
				if (unlikely(master_latency > MAX_DIFF_TIME)) {
					dc->master_miss_fake++;
				}
				if (dcm->high_latency &&
				    (master_latency >> 1) > dcm->high_latency) {
					dc->high_latency_cnt++;
				}
				dc->master_latency += master_latency;
			}
			if (likely(dc->master_miss_real)) {
				dc->master_miss_real--;
			}
		}
		kfree_skb(skb);
		skb = NULL;
		return NET_RX_DROP;
	} else { // HN_DC_FAIL 表示先收到
		dc->seq.former_recv_seq = sequence;
		if (skb->dev == dc->dev[TYPE_SLAVE]) {
			dc->master_miss_real++;
			dc->rx_slave_total++;
			time = ktime_get();
			dc->rx_ts[seq_low].ts = (unsigned long)(time >> 19);
			dc->rx_ts[seq_low].seq_high_8bits = seq_high;
		} else if (skb->dev == dc->dev[TYPE_MASTER]) {
			dc->rx_master_total++;
		}
	}
#else
	if (check_packet_status(dc, sequence) == HN_DC_SUCC) {
		kfree_skb(skb);
		skb = NULL;
		dc->seq.later_recv_seq = sequence;
		return NET_RX_DROP;
	} else {
		dc->seq.former_recv_seq = sequence;
	}
#endif

	/* Use master interface to receive */
	if (dc->dev[TYPE_MASTER] != NULL) {
		skb->dev = dc->dev[TYPE_MASTER];
		skb->skb_iif = dc->dev[TYPE_MASTER]->ifindex;
	}

	return NET_RX_SUCCESS;
}

static struct hn_dc_ops g_dc_ops = {
	.dc_send_copy = hn_dc_send_dc_copy,
	.dc_receive = hn_dc_check_dup_packet,
};

static void netdev_down(const struct net_device *dev)
{
	dc_clear_by_dev(dev);
}

static int dc_netdev_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	switch (event) {
	case NETDEV_DOWN:
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGEADDR:
	case NETDEV_CHANGENAME:
		netdev_down(dev);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block g_dc_netdev_notifier = {
	.notifier_call = dc_netdev_event,
};

static int __init hn_dc_init(void)
{
	int ret = 0;

	ret = dcm_init();
	if (ret != HN_DC_SUCC) {
		dc_mloge("dcm init failed");
		return HN_DC_NOK;
	}

	hn_dc_proc_create();
	ret = register_netdevice_notifier(&g_dc_netdev_notifier);
	if (ret) {
		hn_dc_proc_remove();
		return HN_DC_NOK;
	}

	ret = hn_register_dual_connection(&g_dc_ops);
	if (ret) {
		hn_dc_proc_remove();
		unregister_netdevice_notifier(&g_dc_netdev_notifier);
		return HN_DC_NOK;
	}

	ret = hn_dc_netlink_init();
	if (ret != HN_DC_SUCC) {
		dc_mloge("proc create failed");
		hn_dc_proc_remove();
		unregister_netdevice_notifier(&g_dc_netdev_notifier);
		hn_unregister_dual_connection();
		return HN_DC_NOK;
	}

	return HN_DC_OK;
}

static void __exit hn_dc_exit(void)
{
	dcm_exit();
	hn_dc_proc_remove();
	unregister_netdevice_notifier(&g_dc_netdev_notifier);
	hn_unregister_dual_connection();
}

module_init(hn_dc_init);
module_exit(hn_dc_exit);

MODULE_LICENSE("GPL");

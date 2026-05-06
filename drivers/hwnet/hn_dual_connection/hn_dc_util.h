

#ifndef _HN_DC_UTIL_H
#define _HN_DC_UTIL_H

#include <linux/if_ether.h>
#include <securec.h>

#define LOG_TAG "dc_connection"

#define dc_mlogd(format, ...)                                                 \
	do {                                                                  \
		printk(KERN_DEBUG "[" LOG_TAG "] %s: " format "\n", __func__, \
		       ##__VA_ARGS__);                                        \
	} while (0)

#define dc_mloge(format, ...)                                               \
	do {                                                                \
		printk(KERN_ERR "[" LOG_TAG "] %s: " format "\n", __func__, \
		       ##__VA_ARGS__);                                      \
	} while (0)

#define DC_HLEN 4
#define DC_PROTO 0x0889
#define DC_ETH_HLEN 18
#define MAX_IP_ADDR_LEN 16

#define DC_HASH_SIZE 32
#define MAC_HASH_SIZE 64

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DNS_PORT 53

#define MAC_STR_LEN 20
#define DC_DEVNUM 2
#define TYPE_MASTER 0
#define TYPE_SLAVE 1

#define MAC_ADDR_NUM 2
#define LENGTH_OF_TYPE 2
#define DC_DISABLE 0
#define DC_ENABLE 1

#define DC_PRI_VO 0x6

#define IP_HDR_LEN 20
#define TCP_CHECKSUM_OFFSET 16
#define UDP_CHECKSUM_OFFSET 6
#define ETH_HDR_OFFSET (ETH_ALEN * MAC_ADDR_NUM)
#define IP_HDR_OFFSET (ETH_HDR_OFFSET + LENGTH_OF_TYPE + IP_HDR_LEN)

#define MINIMUM_SPACE 0
#define FOUR_BYTES_ALIGNMENT 4

#define HN_DC_FAIL 0
#define HN_DC_SUCC 1

#define HN_DC_FALSE 0
#define HN_DC_TRUE 1

#define HN_DC_OK 0
#define HN_DC_NOK (-1)

/* Game packet is short packet(from real packet's analysis) */
#define HN_DC_VALIDE_GAME_PACKET_LEN 500

/* HiGame1.0 remark the game packet with HIGAME_TOS_MARK
 * and set the skb->accel with SKB_MARK_DSCP_REMARK_ENABLE
 * hn dc uses this mark to test the game packet.
 */
#define SKB_MARK_DSCP_REMARK_ENABLE 0x8
#define HIGAME_TOS_MARK 0xe0

/* bitmap = 32 * 512 * 4 */
#define DC_BITMAP_BLOCK_NUM 4
#define DC_BITS_PER_BLOCK (65536 / 4)
#define DC_BITMAP_THRESHOLD 4096

/* Clear skip-next bitmap */
#define get_clear_bitmap_no(no) (((no) + 2) % DC_BITMAP_BLOCK_NUM)

#define get_bitmap_block_no(seq) ((seq) / DC_BITS_PER_BLOCK)
#define get_bitmap_block_offset(seq) ((seq) % DC_BITS_PER_BLOCK)
#define get_bitmap_line_no(seq) ((seq) / BITS_PER_LONG)
#define get_bitmap_bit_no(seq) ((seq) % BITS_PER_LONG)

#define HN_DC_EARNINGS 1

#if HN_DC_EARNINGS
#define MAX_DIFF_TIME (460 * 2)
#define SEQ_BUF_LEN 256
#endif

/*
 * split bitmap to four pieces, every piece can manage 512 * 32 packets status.
 * clear 4 pieces bitmap round-robin.
 */
struct bitmap {
	/* 0:received none or twice; 1: received once */
	DECLARE_BITMAP(map, DC_BITS_PER_BLOCK);
	/* threshold to clear bitmap */
	unsigned long threshold;
};

/* Mangemant received duplicate packets status */
struct dup_packet {
	struct bitmap block[DC_BITMAP_BLOCK_NUM];
};

struct dc_ethhdr {
	unsigned char dest[ETH_ALEN];
	unsigned char source[ETH_ALEN];
	__be16 dc_proto;
	__be16 sequence;
	__be16 encap_proto;
};

struct dc_hdr {
	__be16 sequence;
	__be16 encap_proto;
};

struct dc_peer_mac {
	struct hlist_node hlist;
	struct rcu_head rcu;
	unsigned char type;
	unsigned char master_mac[ETH_ALEN];
	unsigned char slave_mac[ETH_ALEN];
};

struct dc_ecap_info {
	struct net_device *dev;
	unsigned char type;
};

struct dc_recv_seq {
	unsigned short former_recv_seq;
	unsigned short later_recv_seq;
};
#if HN_DC_EARNINGS
struct dc_pkt_ts {
	unsigned long ts;
	unsigned char seq_high_8bits;
};
#endif

struct dc_instance {
	struct rcu_head rcu;

	/* Concurrency devices */
	struct net_device *dev[DC_DEVNUM];

	/* Sequence for DC header */
	unsigned short sequence;

	/* Manage the seqence of the received packets */
	struct dup_packet status;

	/* Sequence for recv packet */
	struct dc_recv_seq seq;
#if HN_DC_EARNINGS
	unsigned long rx_master_total;
	unsigned long rx_slave_total;
	unsigned long rx_master_drop;
	unsigned long rx_slave_drop;
	unsigned long master_miss_fake;
	unsigned long master_miss_real;
	unsigned long master_latency;
	unsigned long high_latency_cnt;
	struct dc_pkt_ts *rx_ts;
#endif
};

struct dc_mngr {
	atomic_t dc_enable;
	spinlock_t dc_lock;
	struct dc_instance dc;
	int game_uid;
#if HN_DC_EARNINGS
	int high_latency;
#endif
	struct list_head *udp_sk_list_head;
};

struct udp_tuple {
	struct list_head list;
	__be32 saddr;
	__be32 daddr;
	__be16 s_port;
	__be16 d_port;
};

struct udp_report {
	char src_ip[16];
	char dst_ip[16];
	int s_port;
	int d_port;
};

static DEFINE_MUTEX(udp_sk_list_mutex);

int dc_insert(const char *master_dev, const char *slave_dev, int game_uid);
struct net_device *get_pair_dev(struct dc_instance *dc,
				const struct net_device *dev);
int check_packet_status(struct dc_instance *dc, const unsigned short sequence);
struct dc_mngr *get_dcm(void);
void dc_clear_by_dev(const struct net_device *dev);
void dc_clear(void);
int dcm_init(void);
void dcm_exit(void);
#endif

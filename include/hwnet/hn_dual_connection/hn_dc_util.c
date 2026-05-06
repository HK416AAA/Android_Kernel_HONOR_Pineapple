

#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include "hn_dc_interface.h"
#include "hn_dc_util.h"

static struct dc_mngr g_dcm __read_mostly;

LIST_HEAD(udp_sk_list);

struct dc_mngr *get_dcm(void)
{
	return &g_dcm;
}

static struct dc_instance *dc_create(const struct net_device *master_dev,
				     const struct net_device *slave_dev,
				     int game_uid)
{
	struct dc_instance *dc = &g_dcm.dc;
	if (memset_s(dc, sizeof(*dc), 0, sizeof(*dc)) != EOK) {
		dc_mloge("dc dc_create memset failed!");
		return NULL;
	}
	dc_mlogd("set dc");
#if HN_DC_EARNINGS
	dc->rx_ts = kcalloc(SEQ_BUF_LEN, sizeof(struct dc_pkt_ts), GFP_KERNEL);
	if (dc->rx_ts == NULL) {
		dc_mloge("dc rx_ts kcalloc failed!");
		return NULL;
	}
#endif
	dc->dev[TYPE_MASTER] = (struct net_device *)master_dev;
	dc->dev[TYPE_SLAVE] = (struct net_device *)slave_dev;
	g_dcm.game_uid = game_uid;
	atomic_set(&g_dcm.dc_enable, DC_ENABLE);

	return dc;
}

static void dc_rcu_free(struct rcu_head *head)
{
	struct dc_instance *dc = container_of(head, struct dc_instance, rcu);

	dev_put(dc->dev[TYPE_MASTER]);
	dev_put(dc->dev[TYPE_SLAVE]);
	if (memset_s(dc, sizeof(*dc), 0, sizeof(*dc)) != EOK) {
		dc_mloge("dc_rcu_free memset failed!");
	}
}

static void dc_delete(struct dc_instance *dc)
{
	struct dc_mngr *dcm = get_dcm();
	struct udp_tuple *temp = NULL;
	struct udp_tuple *next = NULL;

	atomic_set(&g_dcm.dc_enable, DC_DISABLE);
	hn_dc_proc_delete_dc();

	mutex_lock(&udp_sk_list_mutex);
	list_for_each_entry_safe(temp, next, dcm->udp_sk_list_head, list) {
		list_del(&temp->list);
		kfree(temp);
	}
	mutex_unlock(&udp_sk_list_mutex);
#if HN_DC_EARNINGS
	if (dc->rx_ts) {
		kfree(dc->rx_ts);
		dc->rx_ts = NULL;
	}
#endif
	call_rcu(&dc->rcu, dc_rcu_free);
}

/*
 * Description: when we get netlink msg to start dc connect,
 * then creat dc instance.
 */
int dc_insert(const char *master_dev, const char *slave_dev, int game_uid)
{
	struct dc_instance *dc = NULL;
	struct net_device *master = NULL;
	struct net_device *slave = NULL;
	int flags;

	if ((master_dev == NULL) || (slave_dev == NULL)) {
		dc_mloge("master/slave dev is NULL");
		return HN_DC_FAIL;
	}
	if (atomic_read(&g_dcm.dc_enable) == DC_ENABLE) {
		dc_mloge("dc is already enabled");
		return HN_DC_FAIL;
	}

	master = dev_get_by_name(&init_net, master_dev);
	if (master == NULL) {
		dc_mloge("get dev %s failed", master_dev);
		return -EFAULT;
	}

	flags = master->flags & IFF_UP;
	if (flags == 0) {
		dc_mloge("master dev down");
		dev_put(master);
		return HN_DC_FAIL;
	}

	slave = dev_get_by_name(&init_net, slave_dev);
	if (slave == NULL) {
		dc_mloge("get dev %s failed", slave_dev);
		dev_put(master);
		return -EFAULT;
	}

	flags = slave->flags & IFF_UP;
	if (flags == 0) {
		dc_mloge("slave dev down");
		dev_put(master);
		dev_put(slave);
		return HN_DC_FAIL;
	}

	/* Set DC */
	spin_lock_bh(&g_dcm.dc_lock);
	dc = dc_create(master, slave, game_uid);
	if (dc == NULL) {
		dc_mloge("add dc failed");
		dev_put(master);
		dev_put(slave);
		spin_unlock_bh(&g_dcm.dc_lock);
		return HN_DC_FAIL;
	}
	spin_unlock_bh(&g_dcm.dc_lock);
	hn_dc_proc_create_dc(dc);
	return HN_DC_SUCC;
}

void dc_clear(void)
{
	struct dc_instance *dc = NULL;
	struct dc_mngr *dcm = get_dcm();

	dc_mlogd("clean dc");
	if (atomic_read(&dcm->dc_enable) == DC_DISABLE) {
		dc_mloge("dc reaches 0");
		return;
	}

	dc = &dcm->dc;
	spin_lock_bh(&dcm->dc_lock);
	dc_delete(dc);
	spin_unlock_bh(&dcm->dc_lock);
}

void dc_clear_by_dev(const struct net_device *dev)
{
	struct dc_instance *dc = NULL;
	struct dc_mngr *dcm = NULL;
	dc_mlogd("clean dc");

	if (atomic_read(&g_dcm.dc_enable) == DC_DISABLE) {
		dc_mloge("dc reaches 0");
		return;
	}

	dcm = get_dcm();
	dc = &dcm->dc;

	spin_lock_bh(&g_dcm.dc_lock);
	if ((dc->dev[TYPE_MASTER] == dev) || (dc->dev[TYPE_SLAVE] == dev))
		dc_delete(dc);
	spin_unlock_bh(&g_dcm.dc_lock);
}

struct net_device *get_pair_dev(struct dc_instance *dc,
				const struct net_device *dev)
{
	if ((dc == NULL) || (dev == NULL)) {
		dc_mloge("dc/dev is NULL");
		return NULL;
	}

	if (dc->dev[TYPE_MASTER] == dev)
		return dc->dev[TYPE_SLAVE];

	if (dc->dev[TYPE_SLAVE] == dev)
		return dc->dev[TYPE_MASTER];

	return NULL;
}

static void clear_bitmap(struct bitmap *bitmap)
{
	if (memset_s(bitmap->map, sizeof(bitmap->map), 0,
		     sizeof(bitmap->map)) != EOK) {
		dc_mloge("clear_bitmap memset failed!");
	}
	bitmap->threshold = 0;
}

static int update_bitmap(struct bitmap *bitmap, const long nr)
{
	long line;
	long bit;

	line = get_bitmap_line_no(nr);
	bit = get_bitmap_bit_no(nr);

	if ((line < 0) || (bit < 0)) {
		dc_mloge("line or bit is invalid");
		return HN_DC_FAIL;
	}

	if (test_bit(bit, bitmap->map + line)) {
		return HN_DC_SUCC;
	} else {
		set_bit(bit, bitmap->map + line);
		bitmap->threshold++;
	}
	return HN_DC_FAIL;
}

/*
 * Update seq bitmap status, statistics and clear the proper bitmap.
 * Result HN_DC_SUCC means the current sequence/packet is the LATER copy.
 */
int check_packet_status(struct dc_instance *dc, const unsigned short sequence)
{
	unsigned short block;
	unsigned short offset;
	unsigned short clear_block;
	struct dup_packet *status = NULL;

	if (dc == NULL) {
		dc_mloge("dc is NULL");
		return HN_DC_FAIL;
	}

	status = &dc->status;
	block = get_bitmap_block_no(sequence);
	offset = get_bitmap_block_offset(sequence);

	if (block >= DC_BITMAP_BLOCK_NUM || offset >= DC_BITS_PER_BLOCK) {
		dc_mloge("block or offset is overflow, block: %d, offset: %d",
			 block, offset);
		return HN_DC_FAIL;
	}

	if (update_bitmap(status->block + block, offset) == HN_DC_SUCC)
		return HN_DC_SUCC;

	if (status->block[block].threshold == DC_BITMAP_THRESHOLD) {
		clear_block = get_clear_bitmap_no(block);
		clear_bitmap(status->block + clear_block);
	}

	return HN_DC_FAIL;
}

int dcm_init(void)
{
	if (memset_s(&g_dcm, sizeof(struct dc_mngr), 0,
		     sizeof(struct dc_mngr)) != EOK) {
		dc_mloge("dcm_init memset failed!");
		return HN_DC_FAIL;
	}
	g_dcm.udp_sk_list_head = &udp_sk_list;
	spin_lock_init(&g_dcm.dc_lock);
	return HN_DC_SUCC;
}

void dcm_exit(void)
{
	spin_lock_bh(&g_dcm.dc_lock);
	if (atomic_read(&g_dcm.dc_enable) == DC_ENABLE) {
		dc_mlogd("dc reaches max");
		dev_put(g_dcm.dc.dev[TYPE_MASTER]);
		dev_put(g_dcm.dc.dev[TYPE_SLAVE]);
		atomic_set(&g_dcm.dc_enable, DC_DISABLE);
	}
	spin_unlock_bh(&g_dcm.dc_lock);
	if (memset_s(&g_dcm, sizeof(struct dc_mngr), 0,
		     sizeof(struct dc_mngr)) != EOK) {
		dc_mloge("dcm_exit memset failed!");
	}
}

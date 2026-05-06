

#ifndef __HN_DC_INTERFACE_H__
#define __HN_DC_INTERFACE_H__

#include <linux/proc_fs.h>
#include <uapi/linux/netlink.h>
#include "hn_dc_netlink.h"
#include "hn_dc_util.h"

struct netlink_msg2knl {
	struct nlmsghdr hdr;
	char data[1];
};

void hn_dc_proc_create(void);
void hn_dc_proc_remove(void);
void hn_dc_proc_create_dc(const struct dc_instance *dc);
void hn_dc_proc_delete_dc(void);
int hn_dc_netlink_init(void);
int hn_dc_notify_event(struct udp_report *udp_sk);

#endif



#ifndef __HN_DC_NETLINK_H__
#define __HN_DC_NETLINK_H__

#include <uapi/linux/if.h>

struct hdc_msg {
	char name_master[IFNAMSIZ];
	char name_slave[IFNAMSIZ];
	bool enable;
	int game_uid;
};

enum dc_netlink_msg {
	DC_NETLINK_START_STOP = 0x10,
	DC_NETLINK_STOP_PARAM = 0x11,
};
#endif

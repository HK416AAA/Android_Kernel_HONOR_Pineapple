/*
 * hw_procdatactrl.h
 *
 * This file use to collect kernel state and report it
 *
 * Copyright (c) 2014-2020 Honor Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/socket.h>
#include <linux/net.h>
#include <uapi/linux/in.h>

#ifndef _HW_PROCDATACTRL_H
#define _HW_PROCDATACTRL_H

int pdc_set_pid_prio(int pid, int prio);
int pdc_set_pending_switch(bool state);
int pdc_handle_process_died(int pid);
int pdc_handle_datahook(struct sk_buff *skb, int protocol);
int pdc_reset(void);

#endif
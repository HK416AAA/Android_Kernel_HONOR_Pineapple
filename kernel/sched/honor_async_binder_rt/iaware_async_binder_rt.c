/*
 * iaware_async_binder_rt.c
 *
 * Async binder rt priority implementation
 *
 * Copyright (c) 2022-2022 Honor Device Co., Ltd.
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

#include <linux/sched/honor_async_binder_rt/iaware_async_binder_rt.h>

unsigned int g_sysctl_async_binder_rt_switch = 0;
#ifdef CONFIG_HN_ASYNC_BINDER_THREAD_PRIO_RT
const char async_binder_rt_descriptor_releasebuf[] = {
	'a', '\0', 'n', '\0', 'd', '\0', 'r', '\0', 'o', '\0', 'i', '\0',
	'd', '\0', '.', '\0', 'g', '\0', 'u', '\0', 'i', '\0', '.', '\0',
	'I', '\0', 'T', '\0', 'r', '\0', 'a', '\0', 'n', '\0', 's', '\0',
	'a', '\0', 'c', '\0', 't', '\0', 'i', '\0', 'o', '\0', 'n', '\0',
	'C', '\0', 'o', '\0', 'm', '\0', 'p', '\0', 'o', '\0', 's', '\0',
	'e', '\0', 'r', '\0', 'L', '\0', 'i', '\0', 's', '\0', 't', '\0',
	'e', '\0', 'n', '\0', 'e', '\0', 'r', '\0', '\0'};

const struct async_binder_rt_config_node async_binder_rt_config_list[] = {
	{(char *)async_binder_rt_descriptor_releasebuf,
	 sizeof(async_binder_rt_descriptor_releasebuf)}};
#else
const char async_binder_rt_descriptor_releasebuf[] = {
	'a', '\0', 'n', '\0', 'd', '\0', 'r', '\0', 'o', '\0', 'i', '\0',
	'd', '\0', '.', '\0', 'g', '\0', 'u', '\0', 'i', '\0', '.', '\0',
	'I', '\0', 'T', '\0', 'r', '\0', 'a', '\0', 'n', '\0', 's', '\0',
	'a', '\0', 'c', '\0', 't', '\0', 'i', '\0', 'o', '\0', 'n', '\0',
	'C', '\0', 'o', '\0', 'm', '\0', 'p', '\0', 'o', '\0', 's', '\0',
	'e', '\0', 'r', '\0', 'L', '\0', 'i', '\0', 's', '\0', 't', '\0',
	'e', '\0', 'n', '\0', 'e', '\0', 'r', '\0', '\0'};

const char async_binder_rt_descriptor_surfaceflinger[] = {
	'a',  '\0', 'n',  '\0', 'd',  '\0', 'r',  '\0', 'o',  '\0', 'i',
	'\0', 'd',  '\0', '.',	'\0', 'g',  '\0', 'u',	'\0', 'i',  '\0',
	'.',  '\0', 'I',  '\0', 'W',  '\0', 'i',  '\0', 'n',  '\0', 'd',
	'\0', 'o',  '\0', 'w',	'\0', 'I',  '\0', 'n',	'\0', 'f',  '\0',
	'o',  '\0', 's',  '\0', 'L',  '\0', 'i',  '\0', 's',  '\0', 't',
	'\0', 'e',  '\0', 'n',	'\0', 'e',  '\0', 'r',	'\0'};

const char async_binder_rt_descriptor_startanimation[] = {
	'a',  '\0', 'n',  '\0', 'd',  '\0', 'r',  '\0', 'o',  '\0', 'i',
	'\0', 'd',  '\0', '.',	'\0', 'w',  '\0', 'i',	'\0', 'n',  '\0',
	'd',  '\0', 'o',  '\0', 'w',  '\0', '.',  '\0', 'I',  '\0', 'R',
	'\0', 'e',  '\0', 'm',	'\0', 'o',  '\0', 't',	'\0', 'e',  '\0',
	'T',  '\0', 'r',  '\0', 'a',  '\0', 'n',  '\0', 's',  '\0', 'i',
	'\0', 't',  '\0', 'i',	'\0', 'o',  '\0', 'n',	'\0'};

const char async_binder_rt_descriptor_ontouchup[] = {
	'a',  '\0', 'n',  '\0', 'd',  '\0', 'r',  '\0', 'o',  '\0', 'i',
	'\0', 'd',  '\0', '.',	'\0', 'w',  '\0', 'i',	'\0', 'n',  '\0',
	'd',  '\0', 'o',  '\0', 'w',  '\0', '.',  '\0', 'I',  '\0', 'T',
	'\0', 'r',  '\0', 'a',	'\0', 'n',  '\0', 's',	'\0', 'i',  '\0',
	't',  '\0', 'i',  '\0', 'o',  '\0', 'n',  '\0', 'P',  '\0', 'l',
	'\0', 'a',  '\0', 'y',	'\0', 'e',  '\0', 'r',	'\0'};

const char async_binder_rt_descriptor_animation[] = {
	'a', '\0', 'n', '\0', 'd', '\0', 'r', '\0', 'o', '\0', 'i', '\0',
	'd', '\0', '.', '\0', 'w', '\0', 'i', '\0', 'n', '\0', 'd', '\0',
	'o', '\0', 'w', '\0', '.', '\0', 'I', '\0', 'T', '\0', 'a', '\0',
	's', '\0', 'k', '\0', 'O', '\0', 'r', '\0', 'g', '\0', 'a', '\0',
	'n', '\0', 'i', '\0', 'z', '\0', 'e', '\0', 'r'};

// android.view.IRecentsAnimationRunner
const char descriptor_recents_animation_runner[] = {
	'a', '\0', 'n', '\0', 'd', '\0', 'r', '\0', 'o', '\0', 'i', '\0',
	'd', '\0', '.', '\0', 'v', '\0', 'i', '\0', 'e', '\0', 'w', '\0',
	'.', '\0', 'I', '\0', 'R', '\0', 'e', '\0', 'c', '\0', 'e', '\0',
	'n', '\0', 't', '\0', 's', '\0', 'A', '\0', 'n', '\0', 'i', '\0',
	'm', '\0', 'a', '\0', 't', '\0', 'i', '\0', 'o', '\0', 'n', '\0',
	'R', '\0', 'u', '\0', 'n', '\0', 'n', '\0', 'e', '\0', 'r', '\0'};

const struct async_binder_rt_config_node async_binder_rt_config_list[] = {
	{(char *)async_binder_rt_descriptor_releasebuf,
	 sizeof(async_binder_rt_descriptor_releasebuf)},
	{(char *)async_binder_rt_descriptor_surfaceflinger,
	 sizeof(async_binder_rt_descriptor_surfaceflinger)},
	{(char *)async_binder_rt_descriptor_startanimation,
	 sizeof(async_binder_rt_descriptor_startanimation)},
	{(char *)async_binder_rt_descriptor_ontouchup,
	 sizeof(async_binder_rt_descriptor_ontouchup)},
	{(char *)async_binder_rt_descriptor_animation,
	 sizeof(async_binder_rt_descriptor_animation)},
	{(char *)descriptor_recents_animation_runner,
	 sizeof(descriptor_recents_animation_runner)}};
#endif

const char *get_descriptor_with_index(unsigned int index)
{
	return async_binder_rt_config_list[index].service_name;
}

unsigned int get_descriptor_size_with_index(unsigned int index)
{
	return async_binder_rt_config_list[index].name_length;
}

unsigned int get_descriptor_count(void)
{
	return sizeof(async_binder_rt_config_list) /
	       sizeof(struct async_binder_rt_config_node);
}

#ifndef __HONOR_THP_PM_H
#define __HONOR_THP_PM_H

#include "honor_thp.h"

struct thp_core_data;
struct thp_cmd_node;
struct thp_device;

#if (defined CONFIG_HONOR_THP_PM)
#if (IS_ENABLED(CONFIG_HONOR_THP_QCOM) || IS_ENABLED(CONFIG_HONOR_THP_MTK))
#define CONFIG_LCD_KIT_DRIVER
#include "tp_lcd_kit_core.h"
#endif

void aod_notify_main_panel_power(struct thp_core_data* cd, int status);
void aod_notify_sub_panel_power(struct thp_core_data* cd, int status);
int thp_lcd_notify_register(struct thp_core_data* cd);
int thp_lcd_notify_unregister(struct thp_core_data* cd);
int thp_screen_switch(int screen_status);
int thp_pm_cmd_proc(struct thp_core_data *cd, struct thp_cmd_node *in_cmd,
		    struct thp_cmd_node *out_cmd);
bool need_work_in_suspend_switch(struct thp_core_data *cd);
int thp_pinctrl_select_normal_state(struct thp_core_data *cd);
int thp_pinctrl_select_lowpower_state(struct thp_core_data *cd);
int thp_set_prox_switch_status(struct thp_core_data *cd, bool enable);
int thp_lcdkit_get_project_id(struct thp_core_data *cd, char *buff, int len);
int is_pt_test_mode(struct thp_device *tdev);
bool thp_lcdkit_ready(void);
#else
static inline void aod_notify_main_panel_power(struct thp_core_data* cd,
					       int status) {
	return;
}
static inline void aod_notify_sub_panel_power(struct thp_core_data *cd,
					      int status)
{
	return;
}
static inline int thp_lcd_notify_register(struct thp_core_data *cd)
{
	return 0;
}
static inline int thp_lcd_notify_unregister(struct thp_core_data *cd)
{
	return 0;
}
static inline int thp_screen_switch(int screen_status)
{
	return 0;
}
static inline int thp_pm_cmd_proc(struct thp_core_data *cd,
				  struct thp_cmd_node *in_cmd,
				  struct thp_cmd_node *out_cmd)
{
	return 0;
}
static inline bool need_work_in_suspend_switch(struct thp_core_data *cd)
{
	return false;
}
static inline int thp_pinctrl_select_normal_state(struct thp_core_data *cd)
{
	return 0;
}
static inline int thp_pinctrl_select_lowpower_state(struct thp_core_data *cd)
{
	return 0;
}
static inline int thp_set_prox_switch_status(struct thp_core_data *cd,
					     bool enable)
{
	return 0;
}
static inline int thp_lcdkit_get_project_id(struct thp_core_data *cd,
					    char *buff, int len)
{
	return -EINVAL;
}
static inline int is_pt_test_mode(struct thp_device *tdev)
{
	return 0;
}
static inline bool thp_lcdkit_ready(void)
{
	return true;
}
#endif //(defined CONFIG_HONOR_THP_PM)

#endif //__HONOR_THP_PM_H
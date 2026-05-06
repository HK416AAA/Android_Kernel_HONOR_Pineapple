/*
 * direct_charge_adapter.h
 *
 * adapter operate for direct charge
 *
 * Copyright (c) 2020-2020 Honor Device Co., Ltd.
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

#ifndef _DIRECT_CHARGER_ADAPTER_H_
#define _DIRECT_CHARGER_ADAPTER_H_

#define DC_ADP_DIGEST_LEN 16
#define DC_AF_KEY_LEN (DC_ADP_DIGEST_LEN * 2 + 1)
#define DC_AF_WAIT_CT_TIMEOUT 1000

/*
 * define adapter current threshold at different voltages
 * support up to 6 parameters list on dts
 */
#define DC_ADP_CUR_LEVEL 6

enum direct_charge_adp_cur_info {
	DC_ADP_VOL_MIN,
	DC_ADP_VOL_MAX,
	DC_ADP_CUR_TH,
	DC_ADP_TOTAL,
};

struct direct_charge_adp_cur_para {
	int vol_min;
	int vol_max;
	int cur_th;
};

/* define adapter power curve */
#define DC_ADP_PC_LEVEL 16
#define DC_ADP_PC_PARA_SIZE 2

struct direct_charge_adp_pc_para {
	int volt;
	int cur;
};

/*
* define dc first cc charge time with max power
* support up to 7 parameters list on dts
*/
#define DC_MAX_POWER_TIME_PARA_LEVEL 20
#define DC_ADAPTER_TEMP_LIMIT_PARA_LEVEL 5

enum direct_charge_max_power_time_info {
	DC_ADAPTER_TYPE,
	DC_MAX_POWER_TIME,
	DC_MAX_POWER_LIMIT_CURRENT,
	DC_MAX_POWER_PARA_MAX,
};

struct direct_charge_max_power_time_para {
	int adapter_type;
	int max_power_time;
	int limit_current;
};

struct direct_charge_limit_max_power_para {
	int limit_current;
	int start_time;
	int run_time;
	bool start_timer_flag;
	int max_power_timeout;
};

enum direct_charge_adapter_temp_limit_info {
	DC_ATL_ADAPTER_TYPE,
	DC_ATL_TEMP_SOURCE,
	DC_ATL_LIMIT_TEMP,
	DC_ATL_LIMIT_CURRENT,
	DC_ATL_HYSTERESIS_TEMP,
	DC_ATL_PARA_MAX,
};

struct direct_charge_adapter_temp_limit_para {
	int adapter_type;
	int temp_source;
	int limit_temp;
	int limit_current;
	int temp_hysteresis;
};

enum dc_adapter_temp_limit_type {
	DC_ATL_TYPE_ADAPTERT_TEMP,
	DC_ATL_TYPE_CHARGER_IC_TEMP,
	DC_ATL_TYPE_MAX,
};

struct dc_adapter_config {
	struct direct_charge_adp_cur_para adp_10v2p25a[DC_ADP_CUR_LEVEL];
	struct direct_charge_adp_cur_para adp_10v2p25a_car[DC_ADP_CUR_LEVEL];
	struct direct_charge_adp_cur_para adp_qtr_a_10v2p25a[DC_ADP_CUR_LEVEL];
	struct direct_charge_adp_cur_para adp_qtr_c_20v3a[DC_ADP_CUR_LEVEL];
	struct direct_charge_adp_cur_para adp_10v4a[DC_ADP_CUR_LEVEL];
	struct direct_charge_max_power_time_para
		max_power_time[DC_MAX_POWER_TIME_PARA_LEVEL];
	struct direct_charge_adapter_temp_limit_para
		temp_limit[DC_ADAPTER_TEMP_LIMIT_PARA_LEVEL];

	int vol_err_th;
	int iadap_abnormal_th;
	int init_adapter_vset;
	int max_adapter_vset;
	int max_adapter_iset;
	int max_tadapt;
	u32 use_5a;
	u32 use_8a;
	u32 gain_curr_10v2a;
	u32 gain_curr_10v2p25a;
	u32 adp_antifake_key_index;
	u32 adp_antifake_enable;
	u32 adaptor_detect_by_voltage;
	int adaptor_leakage_current_th;
	u32 reset_adap_volt_enabled;
};

struct dc_adapter_static_info {
	int adp_otp_cnt;
};

struct dc_adapter_info {
	/* data: reset in dc stop */
	u32 prot_type;

	int vadapt;
	int iadapt;
	int tadapt;

	int iadap_abnormal_cnt;
	int max_adapter_cur_th;
	int adapter_temp_limit_cur;

	int adaptor_vset;
	int adaptor_iset;
	int adaptor_iset_real;
	int adaptor_iset_step;

	u32 adapter_type;
	bool adapter_power_curve_flag;
	struct direct_charge_adp_pc_para adp_pwr_curve[DC_ADP_PC_LEVEL];
	struct direct_charge_limit_max_power_para limit_max_pwr;
	int adaptor_vendor_id;
	int adapter_detect_flag;

	int dc_antifake_result;
	u8 dc_af_key[DC_AF_KEY_LEN];

	/* static data: reset in plug out*/
	struct dc_adapter_static_info static_info;

	/* special data: never reset */
	struct completion dc_af_completion;

	/* config */
	struct dc_adapter_config config;
};

int dc_adapter_module_init(void);
int dc_adapter_module_exit(void);

#endif /* _DIRECT_CHARGER_ADAPTER_H_ */

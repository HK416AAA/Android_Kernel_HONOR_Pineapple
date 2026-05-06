#ifndef _HONOR_TOUCH_BOOST_H_
#define _HONOR_TOUCH_BOOST_H_
extern unsigned int min_max_possible_capacity;
extern unsigned int max_possible_capacity;
extern enum sched_boost_policy boost_policy;
extern __read_mostly bool sched_freq_aggr_en;
extern unsigned int sysctl_sched_boost;
extern unsigned int sched_boost_type;
extern int core_ctl_set_boost(bool boost);

enum sched_boost_policy {
	SCHED_BOOST_NONE,
	SCHED_BOOST_ON_BIG,
	SCHED_BOOST_ON_ALL,
};
static inline void touch_enable_frequency_aggregation(bool enable)
{
	sched_freq_aggr_en = enable;
}

static inline bool hmp_capable(void)
{
	return max_possible_capacity != min_max_possible_capacity;
}
#endif

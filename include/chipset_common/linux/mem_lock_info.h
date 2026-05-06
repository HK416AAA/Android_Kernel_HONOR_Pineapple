#ifndef _LINUX_MEMLOCK_H
#define _LINUX_MEMLOCK_H
#define MEMLOCK_STATS_NAME_LEN 16
#define MEMLOCK_STAT_TIME_MIN_US 64
#define MEMLOCK_STAT_TIME_MULTIPLE 2
#define MEMLOCK_STATS_MAX 15
struct memlock_time_stat {
	char name[MEMLOCK_STATS_NAME_LEN];
	unsigned long count_total;
	unsigned long total_time;
	unsigned long counts[MEMLOCK_STATS_MAX];
};
extern struct memlock_time_stat mmaplock_stat;
extern struct memlock_time_stat zonelock_stat;
extern struct memlock_time_stat lrulock_stat;
extern u64 memlock_time_settions[MEMLOCK_STATS_MAX];

static inline void init_memlock_time_stat(struct memlock_time_stat *alloc_stat)
{
	alloc_stat->count_total = 0;
	alloc_stat->total_time = 0;
	memset(&alloc_stat->counts, 0, sizeof(alloc_stat->counts));
}

static inline u32 index_of_memlock_time_stat(u64 delta)
{
	int left = 0;
	int right = MEMLOCK_STATS_MAX - 2;
	int mid;

	if (delta < memlock_time_settions[left])
		return left;

	if (unlikely(delta > memlock_time_settions[right]))
		return MEMLOCK_STATS_MAX - 1;

	while (left <= right) {
		mid = left + (right - left) / 2;
		if (delta < memlock_time_settions[mid])
			right = mid - 1;
		else
			left = mid + 1;
	}

	return left;
}

static inline void count_memlock_time(struct memlock_time_stat *alloc_stat,
				      u64 delta)
{
	alloc_stat->count_total++;
	alloc_stat->total_time += delta;
	alloc_stat->counts[index_of_memlock_time_stat(delta)]++;
}
#endif
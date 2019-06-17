#include "stats.h"

#include <time.h>

// Get current time in seconds
static double gettime() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

// Format time in seconds
static void timefmt(char *buf, double secs) {
	if (secs < 0.001)
		snprintf(buf, 10, "%.3fµs", secs * 1000000.0);
	else if (secs < 1)
		snprintf(buf, 10, "%.3fms", secs * 1000.0);
	else
		snprintf(buf, 10, "%.3fs", secs);
}

void stats_begin(struct stats *stats) {
	stats->start = gettime();
}

void stats_end(struct stats *stats) {
	stats->times[stats->timeidx] = gettime() - stats->start;
	stats->timeidx = (stats->timeidx + 1) % STATS_TIMES;
	if (stats->count < STATS_TIMES)
		stats->count += 1;
}

void stats_print(struct stats *stats, const char *name, FILE *f) {
	char abuf[10];
	timefmt(abuf, stats_get_avg(stats));

	char lbuf[10];
	timefmt(lbuf, stats_get_last(stats));

	fprintf(f, "%s: Avg: %s, last: %s\n", name, abuf, lbuf);
}

double stats_get_last(struct stats *stats) {
	int last = stats->timeidx == 0 ? stats->count - 1 : stats->timeidx - 1;
	return stats->times[last];
}

double stats_get_avg(struct stats *stats) {
	double acc = 0;
	for (int i = 0; i < stats->count; ++i) {
		acc += stats->times[i];
	}

	return acc / stats->count;
}

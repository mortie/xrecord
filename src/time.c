#include "time.h"

#include <time.h>

#include "util.h"

double time_now() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

void time_print(double t, FILE *f) {
	if (t < 0.001)
		fprintf(f, "%.3fµs", t * 1000000.0);
	else if (t < 1)
		fprintf(f, "%.3fms", t * 1000.0);
	else
		fprintf(f, "%.3fs", t);
}

void _time_done(const char *desc, double t) {
	logfmt("Timer done: %s: ", desc);
	time_print(t, logfile);
	fprintf(logfile, "\n");
}

void stats_begin(struct stats *stats) {
	stats->start = time_now();
}

void stats_end(struct stats *stats) {
	stats->times[stats->timeidx] = time_now() - stats->start;
	stats->timeidx = (stats->timeidx + 1) % STATS_TIMES;
	if (stats->count < STATS_TIMES)
		stats->count += 1;
}

void stats_print(struct stats *stats, const char *name, FILE *f) {
	fprintf(f, "%s: Avg: ", name);
	time_print(stats_get_avg(stats), f);
	fprintf(f, ", last: ");
	time_print(stats_get_last(stats), f);
	fprintf(f, "\n");
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

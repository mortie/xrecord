#ifndef TIME_H
#define TIME_H

#include <stdio.h>

double time_now();
void time_print(double t, FILE *f);

void _time_done(const char *desc, double t);

#define time_record(desc) for ( \
		double _time_start = time_now(); \
		_time_start != -1; \
		(_time_done(desc, time_now() - _time_start), _time_start = -1))

#define STATS_TIMES 20
struct stats {
	double start;
	double times[STATS_TIMES];
	int timeidx;
	int count;
};

void stats_begin(struct stats *stats);
void stats_end(struct stats *stats);
void stats_print(struct stats *stats, const char *name, FILE *f);
double stats_get_last(struct stats *stats);
double stats_get_avg(struct stats *stats);

#endif

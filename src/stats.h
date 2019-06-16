#ifndef STATS_H
#define STATS_H

#include <stdio.h>

#define STATS_TIMES 10

struct stats {
	double start;
	double times[STATS_TIMES];
	int timeidx;
	int count;
};

void stats_begin(struct stats *stats);
void stats_end(struct stats *stats);
void stats_print(struct stats *stats, const char *name, FILE *f);

#endif

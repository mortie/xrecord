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

#endif

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

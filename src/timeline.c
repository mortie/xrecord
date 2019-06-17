#include "timeline.h"

#include <pthread.h>

#include "time.h"

static pthread_mutex_t mut;
static FILE *file = NULL;

void timeline_init(FILE *f) {
	file = f;
	pthread_mutex_init(&mut, NULL);
}

void timeline_register(char *name) {
	if (file == NULL) return;
	pthread_mutex_lock(&mut);
	fprintf(file, "%s: REGISTER: %f\n", name, time_now());
	pthread_mutex_unlock(&mut);
}

void timeline_begin(char *name) {
	if (file == NULL) return;
	pthread_mutex_lock(&mut);
	fprintf(file, "%s: BEGIN: %f\n", name, time_now());
	pthread_mutex_unlock(&mut);
}

void timeline_end(char *name) {
	if (file == NULL) return;
	pthread_mutex_lock(&mut);
	fprintf(file, "%s: END: %f\n", name, time_now());
	pthread_mutex_unlock(&mut);
}

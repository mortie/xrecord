#include "ringbuf.h"

#include <stdlib.h>
#include <string.h>

struct ringbuf *ringbuf_create(size_t size, size_t nmemb) {
	struct ringbuf *rb = malloc(sizeof(struct ringbuf) + size * nmemb);
	pthread_mutex_init(&rb->mut, NULL);
	pthread_cond_init(&rb->cond_space, NULL);
	pthread_cond_init(&rb->cond_data, NULL);
	rb->size = size;
	rb->nmemb = nmemb;
	rb->ri = 0;
	rb->wi = 0;
	rb->used = 0;
	return rb;
}

void ringbuf_destroy(struct ringbuf *rb) {
	pthread_mutex_destroy(&rb->mut);
	pthread_cond_destroy(&rb->cond_space);
	pthread_cond_destroy(&rb->cond_data);
	free(rb);
}

void ringbuf_put(struct ringbuf *rb, int idx, void *data) {
	memcpy(rb->data + rb->size * idx, data, rb->size);
}

void *ringbuf_get(struct ringbuf *rb, int idx) {
	return rb->data + rb->size * idx;
}

void *ringbuf_write_start(struct ringbuf *rb) {
	pthread_mutex_lock(&rb->mut);

	// Wait for space to be available if necessary
	while (rb->used == rb->nmemb)
		pthread_cond_wait(&rb->cond_space, &rb->mut);

	pthread_mutex_unlock(&rb->mut);
	return rb->data + rb->size * rb->wi;
}

void ringbuf_write_end(struct ringbuf *rb) {
	pthread_mutex_lock(&rb->mut);
	rb->wi = (rb->wi + 1) % rb->nmemb;
	rb->used += 1;
	pthread_cond_signal(&rb->cond_data);
	pthread_mutex_unlock(&rb->mut);
}

void ringbuf_write(struct ringbuf *rb, void *data) {
	memcpy(ringbuf_write_start(rb), data, rb->size);
	ringbuf_write_end(rb);
}

void *ringbuf_read_start(struct ringbuf *rb) {
	pthread_mutex_lock(&rb->mut);

	// Wait for data to be available if necessary
	while (rb->used == 0)
		pthread_cond_wait(&rb->cond_data, &rb->mut);

	pthread_mutex_unlock(&rb->mut);
	return rb->data + rb->size * rb->ri;
}

void ringbuf_read_end(struct ringbuf *rb) {
	pthread_mutex_lock(&rb->mut);
	rb->ri = (rb->ri + 1) % rb->nmemb;
	rb->used -= 1;
	pthread_cond_signal(&rb->cond_space);
	pthread_mutex_unlock(&rb->mut);
}

void ringbuf_read(struct ringbuf *rb, void *data) {
	memcpy(data, ringbuf_read_start(rb), rb->size);
	ringbuf_read_end(rb);
}

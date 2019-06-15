#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stdint.h>

struct ringbuf {
	pthread_mutex_t mut;
	pthread_cond_t cond_space;
	pthread_cond_t cond_data;
	size_t size;
	int nmemb;
	int ri;
	int wi;
	int used;
	unsigned char data[];
};

struct ringbuf *ringbuf_create(size_t size, size_t nmbemb);
void ringbuf_destroy(struct ringbuf *rb);

void *ringbuf_write_start(struct ringbuf *rb);
void ringbuf_write_end(struct ringbuf *rb);
void ringbuf_write(struct ringbuf *cb, void *data);

void *ringbuf_read_start(struct ringbuf *rb);
void ringbuf_read_end(struct ringbuf *rb);
void ringbuf_read(struct ringbuf *cb, void *data);

#endif

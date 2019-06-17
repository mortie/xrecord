#ifndef IMGSRC_H
#define IMGSRC_H

#include "rect.h"

#include <stdint.h>
#include <libavcodec/avcodec.h>

struct membuf {
	void *data;
};

struct imgsrc {
	// Prepare video capture
	void (*init)(struct imgsrc *src, struct rect rect);

	// Free all memory allocated by imgsrc_create_*.
	void (*free)(struct imgsrc *src);

	// Set up a memory buffer (hopefully for shared memory)
	struct membuf *(*alloc_membuf)(struct imgsrc *src);

	// Get a video frame.
	// Fills membuf->data.
	void (*get_frame)(struct imgsrc *src, struct membuf *membuf);

	// Variables initialized on creation (i.e before init)
	struct rect screensize;
	enum AVPixelFormat pixfmt;

	// Initialized in init
	struct rect rect;
	int bpl;
};

// Allocate imgsrcs.
extern struct imgsrc *imgsrc_create_x11();

#endif

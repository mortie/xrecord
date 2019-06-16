#ifndef IMGSRC_H
#define IMGSRC_H

#include "rect.h"

#include <stdint.h>
#include <libavcodec/avcodec.h>

struct imgbuf {
	void *data;
	int bpl;
};

struct imgsrc {
	// Prepare video capture
	void (*init)(struct imgsrc *src, struct rect rect);

	// Free all memory allocated by imgsrc_create_*.
	void (*free)(struct imgsrc *src);

	// Get a video frame.
	// This will never be freed, but the buffer will never be referenced
	// again the next time get_frame is called. The buffer should probably be
	// re-used between calls.
	void *(*get_frame)(struct imgsrc *src);

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

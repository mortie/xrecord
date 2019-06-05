#ifndef IMGSRC_H
#define IMGSRC_H

#include "rect.h"

#include <stdint.h>
#include <libavcodec/avcodec.h>

struct imgbuf {
	enum AVPixelFormat fmt;
	void *data;
	int bpl;
};

struct imgsrc {
	// Free all memory allocated by imgsrc_create_*.
	void (*free)(struct imgsrc *src);

	// Get a video frame.
	// This will never be freed, but the buffer will never be referenced
	// again the next time get_frame is called. The buffer should probably be
	// re-used between calls.
	struct imgbuf (*get_frame)(struct imgsrc *src);

	struct rect rect;
};

// Allocate imgsrcs.
extern struct imgsrc *imgsrc_create_x11(char *rectstr);

#endif

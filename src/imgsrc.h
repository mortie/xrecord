#ifndef IMGSRC_H
#define IMGSRC_H

#include "rect.h"

#include <stdint.h>

struct imgsrc {
	void (*free)(void *src);
	void (*get_frame)(void *src, uint8_t *data);
	struct rect rect;
};

extern struct imgsrc *imgsrc_create_x11(char *rectstr);

#endif

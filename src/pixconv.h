#ifndef PIXCONV_H
#define PIXCONV_H

#include <libavcodec/avcodec.h>

#include "rect.h"

struct pixconv {
	struct rect inrect;
	enum AVPixelFormat infmt;
	struct rect outrect;
	enum AVPixelFormat outfmt;
};

struct pixconv *pixconv_create(
		struct rect inrect, enum AVPixelFormat infmt,
		struct rect outrect, enum AVPixelFormat outfmt);

void pixconv_free(struct pixconv *conv);

int pixconv_convert(
		struct pixconv *conv,
		uint8_t **inplanes, const int *instrides,
		uint8_t **outplanes, const int *outstrides);

#endif

#ifndef VENC_H
#define VENC_H

#include <libavcodec/avcodec.h>

struct encconf {
	enum AVCodecID id;
	int fps;
	int width;
	int height;
};

int open_encoder(
		const AVCodec **codec, AVCodecContext **ctx,
		const char *name, struct encconf *conf);

#endif

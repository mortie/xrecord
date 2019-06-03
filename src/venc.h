#ifndef VENC_H
#define VENC_H

#include <libavcodec/avcodec.h>

struct enc_conf {
	enum AVCodecID id;
	int fps;
	int width;
	int height;
};

int find_encoder(
		const AVCodec **codec, AVCodecContext **ctx,
		const char *name, struct enc_conf *conf);

#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>

#include "imgsrc.h"
#include "rect.h"
#include "venc.h"

int main(int argc, char **argv) {
	const AVCodec *codec;
	AVCodecContext *ctx;
	struct enc_conf conf = {
		.id = AV_CODEC_ID_H264,
		.fps = 30,
		.width = 1920,
		.height = 1080,
	};

	if (find_encoder(&codec, &ctx, NULL, &conf) < 0) {
		fprintf(stderr, "Failed to find video encoder.\n");
		return EXIT_FAILURE;
	}

	printf("Using codec: %s\n", codec->long_name);
	return 0;

	struct imgsrc *src = imgsrc_create_x11(NULL);
	uint8_t *data = malloc(src->rect.w * src->rect.h * 3);

	while (true) {
		src->get_frame(src, data);
		printf("got frame\n");
	}

	src->free(src);

	return EXIT_SUCCESS;
}

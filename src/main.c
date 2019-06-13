#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "imgsrc.h"
#include "rect.h"
#include "venc.h"
#include "pixconv.h"

struct config {
	struct rect inrect;
	struct rect outrect;
};

int main(int argc, char **argv) {
	struct config conf = {
		.inrect = {
			.x = 0,
			.y = 0,
			.w = 1920,
			.h = 1080,
		},
		.outrect = {
			.w = 1920,
			.h = 1080,
		},
	};

	// Create image source
	struct imgsrc *src = imgsrc_create_x11();

	// TODO: make conf.inrect depend on src->screensize

	const AVCodec *codec;
	AVCodecContext *ctx;
	struct enc_conf encconf = {
		.id = AV_CODEC_ID_H264,
		.fps = 30,
		.width = conf.inrect.w,
		.height = conf.inrect.h,
	};

	if (find_encoder(&codec, &ctx, NULL, &encconf) < 0) {
		fprintf(stderr, "Failed to find video encoder.\n");
		return EXIT_FAILURE;
	}

	printf("Using codec: %s\n", codec->long_name);

	// Find encoder pixel format
	enum AVPixelFormat encfmt;
	if (ctx->hw_frames_ctx) {
		AVHWFramesContext *fctx = (AVHWFramesContext *)ctx->hw_frames_ctx->data;
		encfmt = fctx->sw_format;
	} else {
		encfmt = ctx->pix_fmt;
	}

	struct pixconv *conv = pixconv_create(
			conf.inrect, src->pixfmt,
			conf.outrect, encfmt);

	// Create the frame we'll send to the encoder
	AVFrame *frame = av_frame_alloc();
	if (frame == NULL) {
		fprintf(stderr, "av_frame_alloc :(\n");
		return EXIT_FAILURE;
	}
	frame->format = encfmt;
	frame->width = conf.outrect.w;
	frame->height = conf.outrect.h;
	if (av_frame_get_buffer(frame, 32) < 0) {
		fprintf(stderr, "av_frame_get_buffer :(\n");
		return EXIT_FAILURE;
	}

	// Create hardware frame if we have a hardare encoder
	AVFrame *hwframe = NULL;
	if (ctx->hw_frames_ctx) {
		hwframe = av_frame_alloc();
		if (hwframe == NULL) {
			fprintf(stderr, "av_frame_alloc :(\n");
			return EXIT_FAILURE;
		}

		if (av_hwframe_get_buffer(ctx->hw_frames_ctx, hwframe, 0) < 0) {
			fprintf(stderr, "av_hwframe_get_buffer :(\n");
			return EXIT_FAILURE;
		}
	}

	AVPacket *pkt = av_packet_alloc();
	if (!pkt) {
		fprintf(stderr, "av_packet_alloc :(\n");
		return EXIT_FAILURE;
	}

	FILE *out = fopen("output.h264", "w");

	src->init(src, conf.inrect);
	while (true) {
		// Get the next round's fram
		struct imgbuf buf = src->get_frame(src);

		av_frame_make_writable(frame);
		pixconv_convert(conv,
				(uint8_t  *[]) { buf.data }, (const int[]) { buf.bpl },
				frame->data, frame->linesize);

		// Decide what frame to use
		AVFrame *f;
		if (hwframe) {
			f = hwframe;
			if (av_hwframe_transfer_data(hwframe, frame, 0) < 0) {
				fprintf(stderr, "av_hwframe_transfer_data :(\n");
				return EXIT_FAILURE;
			}
		} else {
			f = frame;
		}

		// Send frame to encoder
		if (avcodec_send_frame(ctx, f) < 0) {
			fprintf(stderr, "avcodec_send_frame :(\n");
			return EXIT_FAILURE;
		}

		// Receive frame from encoder
		while (1) {
			int ret = avcodec_receive_packet(ctx, pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			} else if (ret < 0) {
				fprintf(stderr, "Error during encoding\n");
				exit(1);
			}

			fwrite(pkt->data, 1, pkt->size, out);
			av_packet_unref(pkt);
		}

		printf("Encoded frame.\n");

		frame->pts += 1;
	}

	src->free(src);

	return EXIT_SUCCESS;
}

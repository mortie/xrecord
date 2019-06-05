#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "imgsrc.h"
#include "rect.h"
#include "venc.h"

int main(int argc, char **argv) {

	const AVCodec *codec;
	AVCodecContext *ctx;
	struct enc_conf conf = {
		.id = AV_CODEC_ID_H264,
		.fps = 30,
		.width = 3860,
		.height = 2160,
	};

	if (find_encoder(&codec, &ctx, NULL, &conf) < 0) {
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

	struct imgsrc *src = imgsrc_create_x11(NULL);

	// The easiest way to get the source pixel format is to
	// just get an image first
	struct imgbuf buf = src->get_frame(src);
	fprintf(stderr, "got image\n");

	// Create the libswscale image scale/conversion context
	struct SwsContext *swsctx = sws_getContext(
			src->rect.w, src->rect.h, buf.fmt,
			src->rect.w, src->rect.h, encfmt,
			0, NULL, NULL, NULL);
	if (swsctx == NULL) {
		fprintf(stderr, "sws_getContxt :(\n");
		return EXIT_FAILURE;
	}

	// Create the frame we'll send to the encoder
	AVFrame *frame = av_frame_alloc();
	if (frame == NULL) {
		fprintf(stderr, "av_frame_alloc :(\n");
		return EXIT_FAILURE;
	}
	frame->format = encfmt;
	frame->width = src->rect.w;
	frame->height = src->rect.h;
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

	while (true) {
		printf("convert frame...\n");
		av_frame_make_writable(frame);
		sws_scale(swsctx,
				(const uint8_t * const[]) { buf.data }, (int[]) { buf.bpl }, 0, src->rect.h,
				frame->data, frame->linesize);
		printf("done.\n");

		// Decide what frame to use
		printf("encode frame...\n");
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
		printf("done.\n");

		frame->pts += 1;

		// Get the next round's fram
		printf("get frame...\n");
		buf = src->get_frame(src);
		printf("done.\n");
	}

	src->free(src);

	return EXIT_SUCCESS;
}

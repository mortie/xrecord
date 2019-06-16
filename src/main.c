#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <libavcodec/avcodec.h>

#include "ringbuf.h"
#include "rect.h"
#include "util.h"
#include "stats.h"
#include "imgsrc.h"
#include "pixconv.h"
#include "venc.h"

static struct config {
	struct rect inrect;
	struct rect outrect;
	const char *outfile;
} conf;

static struct stats cap_stats;
static struct stats conv_stats;
static struct stats enc_stats;
static struct stats total_stats;

/*
 * Capturer
 */

struct capctx {
	struct imgsrc *imgsrc;
	struct ringbuf *outq;
};

static void *cap_thread(void *arg) {
	struct capctx *ctx = (struct capctx *)arg;

	while (1) {
		char *data = ringbuf_write_start(ctx->outq);
		stats_begin(&cap_stats);

		memcpy(
				data,
				ctx->imgsrc->get_frame(ctx->imgsrc),
				ctx->imgsrc->bpl * ctx->imgsrc->rect.h);

		ringbuf_write_end(ctx->outq);
		stats_end(&cap_stats);
	}

	return NULL;
}

/*
 * Converter
 */

struct convctx {
	struct pixconv *conv;
	int bpl;
	struct ringbuf *inq;
	struct ringbuf *outq;
};

static void *conv_thread(void *arg) {
	struct convctx *ctx = (struct convctx *)arg;

	// Prepare avframes
	for (int i = 0; i < ctx->inq->nmemb; ++i) {
		AVFrame *f = av_frame_alloc();
		f->format = ctx->conv->outfmt;
		f->width = ctx->conv->outrect.w;
		f->height = ctx->conv->outrect.h;
		if (av_frame_get_buffer(f, 32) < 0)
			panic("Failed to get AV frame buffer.");
		ringbuf_put(ctx->outq, i, &f);
	}

	while (1) {
		void *data = ringbuf_read_start(ctx->inq);
		AVFrame **frame = ringbuf_write_start(ctx->outq);
		stats_begin(&conv_stats);

		pixconv_convert(ctx->conv,
				(uint8_t  *[]) { data }, (const int[]) { ctx->bpl },
				(*frame)->data, (*frame)->linesize);

		ringbuf_write_end(ctx->outq);
		ringbuf_read_end(ctx->inq);
		stats_end(&conv_stats);
	}

	return NULL;
}

/*
 * Encoder
 */

struct encctx {
	const AVCodec *codec;
	AVCodecContext *avctx;
	enum AVPixelFormat fmt;
	FILE *file;
	struct ringbuf *inq;
};

static void *enc_thread(void *arg) {
	struct encctx *ctx = (struct encctx *)arg;

	// Create hardware frame if we have a hardare encoder
	AVFrame *hwframe = NULL;
	if (ctx->avctx->hw_frames_ctx) {
		hwframe = av_frame_alloc();
		if (hwframe == NULL)
			panic("Failed to allocate hardware frame.");

		if (av_hwframe_get_buffer(ctx->avctx->hw_frames_ctx, hwframe, 0) < 0)
			panic("Failed to get hardware buffer.");
	}

	AVPacket *pkt = av_packet_alloc();
	if (!pkt)
		panic("Failed to allocate AVPacket.");

	while (1) {
		stats_begin(&total_stats);
		AVFrame **avf = ringbuf_read_start(ctx->inq);
		stats_begin(&enc_stats);

		AVFrame *f;
		if (hwframe) {
			f = hwframe;
			if (av_hwframe_transfer_data(hwframe, *avf, 0) < 0)
				panic("Failed to transfer data to hardware frame.");
		} else {
			f = *avf;
		}

		// Send frame to encoder
		if (avcodec_send_frame(ctx->avctx, f) < 0)
			panic("Failed to send frame to codec.");

		// Receive frame from encoder
		while (1) {
			int ret = avcodec_receive_packet(ctx->avctx, pkt);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0)
				panic("Encoding error.");

			fwrite(pkt->data, 1, pkt->size, ctx->file);
			av_packet_unref(pkt);
		}

		(*avf)->pts += 1;

		ringbuf_read_end(ctx->inq);
		stats_end(&enc_stats);
		stats_end(&total_stats);
	}

	return NULL;
}

int main(int argc, char **argv) {

	// Create image source
	struct imgsrc *imgsrc = imgsrc_create_x11();

	// TODO: get this from argv
	conf.inrect.x = 0;
	conf.inrect.y = 0;
	conf.inrect.w = imgsrc->screensize.w;
	conf.inrect.h = imgsrc->screensize.h;
	conf.outrect.w = imgsrc->screensize.w;
	conf.outrect.h = imgsrc->screensize.h;
	conf.outfile = "output.h264";

	/*
	 * Set up capturer
	 */

	imgsrc->init(imgsrc, conf.inrect);
	struct capctx capctx = {
		.imgsrc = imgsrc,
		.outq = ringbuf_create(imgsrc->bpl * imgsrc->rect.h * 4, 2),
	};
	pthread_t cap_th;
	pthread_create(&cap_th, NULL, cap_thread, &capctx);

	/*
	 * Set up encoder
	 */

	struct encctx encctx;
	encctx.file = fopen(conf.outfile, "w");
	if (encctx.file == NULL) ppanic("%s", conf.outfile);
	encctx.inq = ringbuf_create(sizeof(AVFrame *), 2);

	struct encconf encconf = {
		.id = AV_CODEC_ID_H264,
		.fps = 30,
		.width = imgsrc->rect.w,
		.height = imgsrc->rect.h,
	};

	if (find_encoder(&encctx.codec, &encctx.avctx, NULL, &encconf) < 0)
		panic("Failed to find video encoder.");

	enum AVPixelFormat encfmt;
	if (encctx.avctx->hw_frames_ctx) {
		AVHWFramesContext *fctx = (AVHWFramesContext *)encctx.avctx->hw_frames_ctx->data;
		encfmt = fctx->sw_format;
	} else {
		encfmt = encctx.avctx->pix_fmt;
	}

	pthread_t enc_th;
	pthread_create(&enc_th, NULL, enc_thread, &encctx);

	/*
	 * Set up converter
	 */

	struct pixconv *conv = pixconv_create(
			imgsrc->rect, imgsrc->pixfmt,
			conf.outrect, encfmt);
	struct convctx convctx = {
		.conv = conv,
		.bpl = imgsrc->bpl,
		.inq = capctx.outq,
		.outq = encctx.inq,
	};
	pthread_t conv_th;
	pthread_create(&conv_th, NULL, conv_thread, &convctx);

	while (1) {
		fprintf(stderr, "\n");
		stats_print(&cap_stats, "Capture", stderr);
		stats_print(&conv_stats, "Convert", stderr);
		stats_print(&enc_stats, "Encode", stderr);
		stats_print(&total_stats, "Total", stderr);
		sleep(3);
	}

	pthread_join(cap_th, NULL);
	pthread_join(enc_th, NULL);
	pthread_join(conv_th, NULL);

	return EXIT_SUCCESS;
}

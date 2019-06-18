#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <libavcodec/avcodec.h>

#include "ringbuf.h"
#include "rect.h"
#include "util.h"
#include "time.h"
#include "timeline.h"
#include "imgsrc.h"
#include "pixconv.h"
#include "venc.h"

#define NUM_BUFFERS 4

struct config {
	struct rect inrect;
	struct rect outrect;
	const char *outfile;
	const char *timelinefile;
	double fps;
};

/*
 * Capturer
 */

struct capctx {
	struct imgsrc *imgsrc;
	struct ringbuf *outq;
	double fps;
};

static void *cap_thread(void *arg) {
	struct capctx *ctx = (struct capctx *)arg;

	// Prepare mem bufs
	for (int i = 0; i < ctx->outq->nmemb; ++i) {
		struct membuf *buf = ctx->imgsrc->alloc_membuf(ctx->imgsrc);
		ringbuf_put(ctx->outq, i, &buf);
	}

	double acc = 0;
	double prev = time_now();
	double target = (double)1 / ctx->fps;

	while (1) {
		struct membuf **membuf = ringbuf_write_start(ctx->outq);

		timeline_begin("cap");
		ctx->imgsrc->get_frame(ctx->imgsrc, *membuf);
		ringbuf_write_end(ctx->outq);
		timeline_end("cap");

		double now = time_now();
		acc += target - (now - prev);
		if (acc > 0) {
			usleep(acc * 1000000ll);
			acc -= time_now() - now;
		} else if (acc < -2) {
			logln("Can't keep up! Skipping %.3fms.", -(acc * 1000.0));
			acc = 0;
		}
		prev = time_now();
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
	for (int i = 0; i < ctx->outq->nmemb; ++i) {
		AVFrame *f = av_frame_alloc();
		f->format = ctx->conv->outfmt;
		f->width = ctx->conv->outrect.w;
		f->height = ctx->conv->outrect.h;
		if (av_frame_get_buffer(f, 32) < 0)
			panic("Failed to get AV frame buffer.");
		ringbuf_put(ctx->outq, i, &f);
	}

	while (1) {
		struct membuf **membuf = ringbuf_read_start(ctx->inq);
		AVFrame **frame = ringbuf_write_start(ctx->outq);

		timeline_begin("conv");
		int ret = pixconv_convert(ctx->conv,
				(uint8_t  *[]) { (*membuf)->data }, (const int[]) { ctx->bpl },
				(*frame)->data, (*frame)->linesize);
		if (ret < 0)
			panic("Pixel conversion failed.");

		ringbuf_write_end(ctx->outq);
		ringbuf_read_end(ctx->inq);
		timeline_end("conv");
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

	int pts = 0;

	double nextsec = time_now() + 1;
	int framecount = 0;
	while (1) {
		if (time_now() >= nextsec) {
			logln("FPS: %i", framecount);
			framecount = 0;
			nextsec += 1;
		}
		framecount += 1;

		AVFrame **avf = ringbuf_read_start(ctx->inq);

		timeline_begin("enc");
		(*avf)->pts = pts++;

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

		ringbuf_read_end(ctx->inq);
		timeline_end("enc");
	}

	return NULL;
}

static void parse_args(int argc, char **argv, struct config *conf) {
	struct option long_opts[] = {
		{ "timeline", required_argument, 0, 't' },
		{ "rect",     required_argument, 0, 'r' },
		{ "size",     required_argument, 0, 's' },
		{ "help",     no_argument,       0, 'h' },
		{ 0 },
	};

	int c;
	int option_ind;
	bool size_changed = false;
	while (1) {
		c = getopt_long(argc, argv, "t:i:r:h", long_opts, &option_ind);

		if (c == -1)
			break;

		switch (c) {
		case 't':
			conf->timelinefile = optarg;
			break;

		case 'r':
			rect_parse(&conf->inrect, optarg);
			break;

		case 's':
			rect_parse(&conf->outrect, optarg);
			size_changed = true;
			break;

		case 'h':
			printf("Usage: %s [options] <outfile>\n", argv[0]);
			exit(EXIT_SUCCESS);

		case '?':
			break;

		default:
			logln("hi %s", optarg);
		}
	}

	if (!size_changed) {
		conf->outrect.w = conf->inrect.w;
		conf->outrect.h = conf->inrect.h;
	}

	if (argv[optind] == NULL) {
		printf("Usage: %s [options] <outfile>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	conf->outfile = argv[optind];
	logln("Using input rectangle %ix%i+%i+%i",
			conf->inrect.w, conf->inrect.h, conf->inrect.x, conf->inrect.y);
	logln("Writing %ix%i to %s.", conf->outrect.w, conf->outrect.h, conf->outfile);
}

int main(int argc, char **argv) {

	// Create image source
	struct imgsrc *imgsrc = imgsrc_create_x11();

	// TODO: get this from argv
	struct config conf;
	conf.inrect.x = 0;
	conf.inrect.y = 0;
	conf.inrect.w = imgsrc->screensize.w;
	conf.inrect.h = imgsrc->screensize.h;
	conf.outrect.w = imgsrc->screensize.w;
	conf.outrect.h = imgsrc->screensize.h;
	conf.outfile = NULL;
	conf.timelinefile = NULL;
	conf.fps = 30;

	parse_args(argc, argv, &conf);

	if (conf.timelinefile) {
		FILE *f = fopen(conf.timelinefile, "w");
		if (f == NULL) {
			logperror("%s", conf.timelinefile);
		} else {
			timeline_init(f);
		}

		timeline_register("cap");
		timeline_register("conv");
		timeline_register("enc");
	}

	/*
	 * Set up capturer
	 */

	imgsrc->init(imgsrc, conf.inrect);
	struct capctx capctx = {
		.imgsrc = imgsrc,
		.outq = ringbuf_create(sizeof(void *), NUM_BUFFERS),
		.fps = conf.fps,
	};

	/*
	 * Set up encoder
	 */

	struct encctx encctx;
	encctx.file = fopen(conf.outfile, "w");
	if (encctx.file == NULL) ppanic("%s", conf.outfile);
	encctx.inq = ringbuf_create(sizeof(AVFrame *), NUM_BUFFERS);

	struct encconf encconf = {
		.id = AV_CODEC_ID_H264,
		.fps = conf.fps,
		.width = conf.outrect.w,
		.height = conf.outrect.h,
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

	/*
	 * Set up converter
	 */

	struct pixconv *conv = pixconv_create(
			imgsrc->rect, imgsrc->pixfmt,
			conf.outrect, encfmt);
	if (conv == NULL)
		panic("Failed to create pixconv.");

	struct convctx convctx = {
		.conv = conv,
		.bpl = imgsrc->bpl,
		.inq = capctx.outq,
		.outq = encctx.inq,
	};

	/*
	 * Create threads
	 */

	pthread_t cap_th;
	pthread_create(&cap_th, NULL, cap_thread, &capctx);

	pthread_t conv_th;
	pthread_create(&conv_th, NULL, conv_thread, &convctx);

	pthread_t enc_th;
	pthread_create(&enc_th, NULL, enc_thread, &encctx);

	/*
	 * Wait
	 */

	pthread_join(cap_th, NULL);
	pthread_join(conv_th, NULL);
	pthread_join(enc_th, NULL);

	return EXIT_SUCCESS;
}

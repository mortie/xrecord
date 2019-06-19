#include "venc.h"

#include <libavutil/hwcontext.h>
#include <fnmatch.h>
#include <stdbool.h>

#include "util.h"

static void setconf(AVCodecContext *ctx, enum AVPixelFormat fmt, struct encconf *conf) {
	ctx->time_base = (AVRational) { 1, conf->fps };
	ctx->pix_fmt = fmt;
	ctx->width = conf->width;
	ctx->height = conf->height;
}

static int set_hwframe_ctx(
		AVCodecContext *ctx, AVBufferRef *hw_device_ctx,
		enum AVPixelFormat fmt, struct encconf *conf) {
	AVBufferRef *hw_frames_ref;
	AVHWFramesContext *frames_ctx = NULL;
	int ret = 0;

	if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx))) {
		fprintf(stderr, "Failed to create VAAPI frame context.\n");
		return -1;
	}
	frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
	frames_ctx->format = AV_PIX_FMT_VAAPI;
	frames_ctx->sw_format = fmt;
	frames_ctx->width = conf->width;
	frames_ctx->height = conf->height;
	frames_ctx->initial_pool_size = 20;
	if ((ret = av_hwframe_ctx_init(hw_frames_ref)) < 0) {
		fprintf(stderr, "Failed to initialize VAAPI frame context."
				"Error code: %s\n",av_err2str(ret));
		av_buffer_unref(&hw_frames_ref);
		return ret;
	}
	ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
	if (!ctx->hw_frames_ctx)
		ret = AVERROR(ENOMEM);

	av_buffer_unref(&hw_frames_ref);
	return ret;
}

static bool find_codec(const AVCodec **codec, enum AVCodecID id, char *pattern) {
	void *iter = NULL;
	while ((*codec = av_codec_iterate(&iter)) != NULL) {
		if ((*codec)->id != id)
			continue;

		int match = fnmatch(pattern, (*codec)->name, 0);
		if (match == FNM_NOMATCH) {
			continue;
		} else if (match == 0) {
			return true;
		} else {
			logln("Invalid pattern: %s", pattern);
			return false;
		}

	}

	return false;
}

static int try_nvenc(
		const AVCodec **codec, AVCodecContext **ctx,
		struct encconf *conf) {

	if (!find_codec(codec, conf->id, "nvenc_*")) {
		logln("Found no nvenc codec.");
		return -1;
	}

	*ctx = avcodec_alloc_context3(*codec);
	setconf(*ctx, (*codec)->pix_fmts[0], conf);
	return avcodec_open2(*ctx, *codec, NULL);
}

static int try_vaapi(
		const AVCodec **codec, AVCodecContext **ctx,
		struct encconf *conf) {

	if (!find_codec(codec, conf->id, "*_vaapi")) {
		logln("Found no vaapi codec.");
		return -1;
	}

	*ctx = avcodec_alloc_context3(*codec);
	setconf(*ctx, AV_PIX_FMT_VAAPI, conf);

	// Create hardware device
	static AVBufferRef *hw_device_ctx = NULL;
	int ret = av_hwdevice_ctx_create(
			&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI,
			NULL, NULL, 0);
	if (ret < 0) {
		logln("Failed to create a VAAPI device. Error code: %s", av_err2str(ret));
		avcodec_free_context(ctx);
		return ret;
	}

	ret = set_hwframe_ctx(*ctx, hw_device_ctx, AV_PIX_FMT_NV12, conf);
	if (ret < 0) {
		logln("Failed to set hwframe context.");
		avcodec_free_context(ctx);
		return ret;
	}

	ret = avcodec_open2(*ctx, *codec, NULL);
	if (ret < 0) {
		avcodec_free_context(ctx);
		return -1;
	}

	return ret;
}

int open_encoder(
		const AVCodec **codec, AVCodecContext **ctx,
		const char *name, struct encconf *conf) {

	if (name != NULL) {
		*codec = avcodec_find_encoder_by_name(name);
		if (*codec == NULL) {
			fprintf(stderr, "Encoder '%s' doesn't exist.\n", name);
			return -1;
		}

		*ctx = avcodec_alloc_context3(*codec);
		setconf(*ctx, (*codec)->pix_fmts[0], conf);
		int ret = avcodec_open2(*ctx, *codec, NULL);

		if (ret < 0)
			avcodec_free_context(ctx);

		return ret;
	}

	// Try out nvenc
	fprintf(stderr, "Trying out nvenc...\n");
	if (try_nvenc(codec, ctx, conf) >= 0)
		return 0;

	// Try out vaapi
	fprintf(stderr, "Trying out vaapi...\n");
	if (try_vaapi(codec, ctx, conf) >= 0)
		return 0;

	// Give up, just find a sw encoder
	fprintf(stderr, "Found no hardware encoder, just using the default software encoder.\n");
	*codec = avcodec_find_encoder(conf->id);
	*ctx = avcodec_alloc_context3(*codec);
	setconf(*ctx, (*codec)->pix_fmts[0], conf);

	return avcodec_open2(*ctx, *codec, NULL);
}

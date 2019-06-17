#include "pixconv.h"

#include <stdbool.h>

// 2.0 support seems to be limited (fuck nvidia):
// https://en.wikipedia.org/wiki/OpenCL#OpenCL_2.0_support
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/opencl.h>
#include <libavutil/imgutils.h>

#include "assets.h"
#include "clerr.h"
#include "util.h"

#define CHECKERR(err) do { \
	if (err < 0) \
		panic("CL error: %s (%i)", clGetErrorString(err), err); \
} while (0)

struct pixconv_cl {
	struct pixconv conv;
	cl_program program;
	cl_kernel kernel;
	cl_context context;
	cl_command_queue queue;
	cl_device_id device;
};

struct pixconv_rgb32_nv12 {
	struct pixconv_cl cl;
	cl_mem input_image;
	cl_mem output_y_image;
	cl_mem output_uv_image;
	cl_event events[2];
};

struct pixconv_rgb32_yuv420 {
	struct pixconv_cl cl;
	cl_mem input_image;
	cl_mem output_y_image;
	cl_mem output_u_image;
	cl_mem output_v_image;
	cl_event events[3];
};

static bool is_rgb32_nv12(enum AVPixelFormat in, enum AVPixelFormat out) {
	return in == AV_PIX_FMT_BGRA && out == AV_PIX_FMT_NV12;
}

static bool is_rgb32_yuv420(enum AVPixelFormat in, enum AVPixelFormat out) {
	return in == AV_PIX_FMT_BGRA && out == AV_PIX_FMT_YUV420P;
}

static void rgbdesc(enum AVPixelFormat fmt, int *r, int *g, int *b) {
	if (fmt == AV_PIX_FMT_BGRA) {
		*b = 0; *g = 1; *r = 2;
	} else {
		panic("Unsupported pixel format: %s", av_get_pix_fmt_name(fmt));
	}
}

static int setup_cl(struct pixconv_cl *cl, const char *kname) {
	int err;

	cl_platform_id platform_ids[10];
	cl_uint num_platforms;
	err = clGetPlatformIDs(10, platform_ids, &num_platforms);
	CHECKERR(err);

	for (unsigned int i = 0; i < num_platforms; ++i) {
		char platname[128];
		err = clGetPlatformInfo(
				platform_ids[i],  CL_PLATFORM_NAME,
				sizeof(platname), platname, NULL);
		CHECKERR(err);

		char vendname[128];
		err = clGetPlatformInfo(
				platform_ids[i],  CL_PLATFORM_VENDOR,
				sizeof(vendname), vendname, NULL);
		CHECKERR(err);

		logln("Platform %i: %s (%s)", i, platname, vendname);

		cl_device_id device_ids[10];
		cl_uint num_devices;
		err = clGetDeviceIDs(
				platform_ids[i], CL_DEVICE_TYPE_ALL, 10,
				device_ids, &num_devices);
		CHECKERR(err);

		for (unsigned int j = 0; j < num_devices; ++j) {
			char devname[128];
			err = clGetDeviceInfo(
					device_ids[j], CL_DEVICE_NAME,
					sizeof(devname), devname, NULL);
			CHECKERR(err);

			cl_bool image_support;
			err = clGetDeviceInfo(
					device_ids[j], CL_DEVICE_IMAGE_SUPPORT,
					sizeof(image_support), &image_support, NULL);
			CHECKERR(err);

			if (image_support) {
				logln("  Device %i: %s", j, devname);
				cl->device = device_ids[j];
			} else {
				logln("  Device %i: %s - No image support, ignoring.", j, devname);
			}
		}
	}

	char devname[128];
	err = clGetDeviceInfo(
			cl->device, CL_DEVICE_NAME,
			sizeof(devname), devname, NULL);
	CHECKERR(err);
	logln("Using %s.", devname);

	cl->context = clCreateContext(0, 1, &cl->device, NULL, NULL, &err);
	CHECKERR(err);

	cl->queue = clCreateCommandQueue(cl->context, cl->device, 0, &err);
	CHECKERR(err);

	cl->program = clCreateProgramWithSource(
			cl->context, 1,
			(const char *[]) { (char *)ASSETS_CONVERT_IMAGE_CL },
			(size_t[]) { ASSETS_CONVERT_IMAGE_CL_LEN },
			&err);
	CHECKERR(err);

	err = clBuildProgram(cl->program, 0, NULL, NULL, NULL, NULL);
	if (err) {
		size_t len;
		err = clGetProgramBuildInfo(
				cl->program, cl->device, CL_PROGRAM_BUILD_LOG,
				0, NULL, &len);
		CHECKERR(err);

		char *logstr = malloc(len);
		err = clGetProgramBuildInfo(
				cl->program, cl->device, CL_PROGRAM_BUILD_LOG,
				len, logstr, &len);
		CHECKERR(err);

		fprintf(stderr, "Failed to compile:\n%s", logstr);
		free(logstr);
		return -1;
	}

	cl->kernel = clCreateKernel(cl->program, kname, &err);
	CHECKERR(err);

	return 0;
}

struct pixconv *pixconv_create(
		struct rect inrect, enum AVPixelFormat infmt,
		struct rect outrect, enum AVPixelFormat outfmt) {
	assume(
			is_rgb32_nv12(infmt, outfmt) ||
			is_rgb32_yuv420(infmt, outfmt));

	struct pixconv_cl *cl;
	int err;
	if (is_rgb32_nv12(infmt, outfmt)) {
		struct pixconv_rgb32_nv12 *rgb32_nv12 = malloc(sizeof(*rgb32_nv12));
		cl = (struct pixconv_cl *)rgb32_nv12;

		int ret = setup_cl(cl, "convert_rgb32_nv12");

		if (ret < 0) {
			logln("Creating kernel failed.");
			free(cl);
			return NULL;
		}

		float scale_x = (float)inrect.w / (float)outrect.w;
		err = clSetKernelArg(cl->kernel, 0, sizeof(scale_x), &scale_x);
		CHECKERR(err);
		float scale_y = (float)inrect.h / (float)outrect.h;
		err = clSetKernelArg(cl->kernel, 1, sizeof(scale_y), &scale_y);
		CHECKERR(err);

		// Set up RGB positions
		int r, g, b;
		rgbdesc(infmt, &r, &g, &b);
		err = clSetKernelArg(cl->kernel, 2, sizeof(r), &r);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 3, sizeof(g), &g);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 4, sizeof(b), &b);
		CHECKERR(err);

		// Set up input image
		cl_image_format input_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_RGBA,
		};
		cl_image_desc input_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = inrect.w,
			.image_height = inrect.h,
		};
		rgb32_nv12->input_image = clCreateImage(
				cl->context, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY,
				&input_format, &input_desc, NULL, &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 5,
				sizeof(rgb32_nv12->input_image), &rgb32_nv12->input_image);
		CHECKERR(err);

		// Set up output Y image
		cl_image_format output_y_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_y_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = outrect.w,
			.image_height = outrect.h,
		};
		rgb32_nv12->output_y_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY,
				&output_y_format, &output_y_desc, NULL, &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 6,
				sizeof(rgb32_nv12->output_y_image), &rgb32_nv12->output_y_image);
		CHECKERR(err);

		// Set up output UV image
		cl_image_format output_uv_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_RG,
		};
		cl_image_desc output_uv_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = outrect.w / 2,
			.image_height = outrect.h / 2,
		};
		rgb32_nv12->output_uv_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY,
				&output_uv_format, &output_uv_desc, NULL, &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 7,
				sizeof(rgb32_nv12->output_uv_image), &rgb32_nv12->output_uv_image);
		CHECKERR(err);

		// Set up events
		for (int i = 0; i < 2; ++i) {
			rgb32_nv12->events[i] = clCreateUserEvent(cl->context, &err);
			CHECKERR(err);
		}

	} else if (is_rgb32_yuv420(infmt, outfmt)) {
		struct pixconv_rgb32_yuv420 *rgb32_yuv420 = malloc(sizeof(*rgb32_yuv420));
		cl = (struct pixconv_cl *)rgb32_yuv420;

		int ret = setup_cl(cl, "convert_rgb32_yuv420");

		if (ret < 0) {
			logln("Creating kernel failed.");
			free(cl);
			return NULL;
		}

		float scale_x = (float)inrect.w / (float)outrect.w;
		err = clSetKernelArg(cl->kernel, 0, sizeof(scale_x), &scale_x);
		CHECKERR(err);
		float scale_y = (float)inrect.h / (float)outrect.h;
		err = clSetKernelArg(cl->kernel, 1, sizeof(scale_y), &scale_y);
		CHECKERR(err);

		// Set up RGB positions
		int r, g, b;
		rgbdesc(infmt, &r, &g, &b);
		err = clSetKernelArg(cl->kernel, 2, sizeof(r), &r);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 3, sizeof(g), &g);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 4, sizeof(b), &b);
		CHECKERR(err);

		// Set up input image
		cl_image_format input_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_RGBA,
		};
		cl_image_desc input_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = inrect.w,
			.image_height = inrect.h,
		};
		rgb32_yuv420->input_image = clCreateImage(
				cl->context, CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY,
				&input_format, &input_desc, NULL, &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 5,
				sizeof(rgb32_yuv420->input_image), &rgb32_yuv420->input_image);
		CHECKERR(err);

		// Set up output Y image
		cl_image_format output_y_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_y_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = outrect.w,
			.image_height = outrect.h,
		};
		rgb32_yuv420->output_y_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY,
				&output_y_format, &output_y_desc, NULL, &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 6,
				sizeof(rgb32_yuv420->output_y_image), &rgb32_yuv420->output_y_image);
		CHECKERR(err);

		// Set up output U image
		cl_image_format output_u_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_u_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = outrect.w / 2,
			.image_height = outrect.h / 2,
		};
		rgb32_yuv420->output_u_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY,
				&output_u_format, &output_u_desc, NULL, &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 7,
				sizeof(rgb32_yuv420->output_u_image), &rgb32_yuv420->output_u_image);
		CHECKERR(err);

		// Set up output V image
		cl_image_format output_v_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_v_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = outrect.w / 2,
			.image_height = outrect.h / 2,
		};
		rgb32_yuv420->output_v_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY,
				&output_v_format, &output_v_desc, NULL, &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 8,
				sizeof(rgb32_yuv420->output_v_image), &rgb32_yuv420->output_v_image);
		CHECKERR(err);

		// Set up events
		for (int i = 0; i < 3; ++i) {
			rgb32_yuv420->events[i] = clCreateUserEvent(cl->context, &err);
			CHECKERR(err);
		}

	} else {
		assume_unreached();
	}

	memcpy(&cl->conv.inrect, &inrect, sizeof(inrect));
	cl->conv.infmt = infmt;
	memcpy(&cl->conv.outrect, &outrect, sizeof(outrect));
	cl->conv.outfmt = outfmt;

	return (struct pixconv *)cl;
}

void pixconv_free(struct pixconv *conv) {
	free(conv);
}

int pixconv_convert(
		struct pixconv *conv,
		uint8_t **inplanes, const int *instrides,
		uint8_t **outplanes, const int *outstrides) {
	int err;

	struct pixconv_cl *cl = (struct pixconv_cl *)conv;

	if (is_rgb32_nv12(conv->infmt, conv->outfmt)) {
		struct pixconv_rgb32_nv12 *rgb32_nv12 = (struct pixconv_rgb32_nv12 *)cl;

		// Write input
		err = clEnqueueWriteImage (
				cl->queue, rgb32_nv12->input_image, CL_TRUE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->inrect.w, conv->inrect.h, 1 },
				instrides[0], 0, inplanes[0],
				0, NULL, NULL);
		CHECKERR(err);

		// Run kernel
		err = clEnqueueNDRangeKernel(
				cl->queue, cl->kernel, 2, NULL,
				(const size_t[]) { conv->outrect.w, conv->outrect.h, 0 }, NULL,
				0, NULL, NULL);
		CHECKERR(err);

		// Read Y
		err = clEnqueueReadImage(
				cl->queue, rgb32_nv12->output_y_image, CL_FALSE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w, conv->outrect.h, 1 },
				outstrides[0], 0, outplanes[0],
				0, NULL, &rgb32_nv12->events[0]);
		CHECKERR(err);

		// Read UV
		err = clEnqueueReadImage(
				cl->queue, rgb32_nv12->output_uv_image, CL_FALSE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w / 2, conv->outrect.h / 2, 1 },
				outstrides[1], 0, outplanes[1],
				0, NULL, &rgb32_nv12->events[1]);
		CHECKERR(err);

		// Wait for reads to complete
		err = clWaitForEvents(2, rgb32_nv12->events);
		CHECKERR(err);

		return 0;
	} else if (is_rgb32_yuv420(conv->infmt, conv->outfmt)) {
		struct pixconv_rgb32_yuv420 *rgb32_yuv420 = (struct pixconv_rgb32_yuv420 *)cl;

		// Write input
		err = clEnqueueWriteImage (
				cl->queue, rgb32_yuv420->input_image, CL_TRUE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->inrect.w, conv->inrect.h, 1 },
				instrides[0], 0, inplanes[0],
				0, NULL, NULL);
		CHECKERR(err);

		// Run kernel
		CHECKERR(err);
		err = clEnqueueNDRangeKernel(
				cl->queue, cl->kernel, 2, NULL,
				(const size_t[]) { conv->outrect.w, conv->outrect.h, 0 }, NULL,
				0, NULL, NULL);
		CHECKERR(err);

		// Read Y
		err = clEnqueueReadImage(
				cl->queue, rgb32_yuv420->output_y_image, CL_FALSE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w, conv->outrect.h, 1 },
				outstrides[0], 0, outplanes[0],
				0, NULL, &rgb32_yuv420->events[0]);
		CHECKERR(err);

		// Read U
		err = clEnqueueReadImage(
				cl->queue, rgb32_yuv420->output_u_image, CL_FALSE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w / 2, conv->outrect.h / 2, 1 },
				outstrides[1], 0, outplanes[1],
				0, NULL, &rgb32_yuv420->events[1]);
		CHECKERR(err);

		// Read V
		err = clEnqueueReadImage(
				cl->queue, rgb32_yuv420->output_v_image, CL_FALSE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w / 2, conv->outrect.h / 2, 1 },
				outstrides[2], 0, outplanes[2],
				0, NULL, &rgb32_yuv420->events[2]);
		CHECKERR(err);

		err = clWaitForEvents(3, rgb32_yuv420->events);
		CHECKERR(err);

		return 0;
	} else {
		assume_unreached();
	}
}

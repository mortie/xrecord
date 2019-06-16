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

static bool is_rgb32_nv12(enum AVPixelFormat in, enum AVPixelFormat out) {
	return in == AV_PIX_FMT_BGRA && out == AV_PIX_FMT_NV12;
}

static bool is_rgb32_yuv420(enum AVPixelFormat in, enum AVPixelFormat out) {
	return in == AV_PIX_FMT_BGRA && out == AV_PIX_FMT_YUV420P;
}

static void rgbdesc(enum AVPixelFormat fmt, int *r, int *g, int *b) {
	if (fmt == AV_PIX_FMT_BGRA) {
		*b = 0; *g = 8; *r = 16;
	} else {
		panic("Unsupported pixel format: %s", av_get_pix_fmt_name(fmt));
	}
}

struct pixconv *pixconv_create(
		struct rect inrect, enum AVPixelFormat infmt,
		struct rect outrect, enum AVPixelFormat outfmt) {
	struct pixconv_cl *cl = malloc(sizeof(*cl));

	int err;
	cl_uint num_devices;
	err = clGetDeviceIDs(
			NULL, CL_DEVICE_TYPE_GPU, 1,
			&cl->device, &num_devices);
	CHECKERR(err);

	cl->context = clCreateContext(0, 1, &cl->device, NULL, NULL, &err);
	CHECKERR(err);

	if (is_rgb32_nv12(infmt, outfmt)) {
		cl->queue = clCreateCommandQueue(cl->context, cl->device, 0, &err);
		CHECKERR(err);

		cl->program = clCreateProgramWithSource(
				cl->context, 1,
				(const char *[]) { (char *)ASSETS_CONVERT_RGB32_NV12_CL },
				(size_t[]) { ASSETS_CONVERT_RGB32_NV12_CL_LEN },
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
		}

		cl->kernel = clCreateKernel(cl->program, "convert_rgb32_nv12", &err);
		CHECKERR(err);
	} else if (is_rgb32_yuv420(infmt, outfmt)) {
		cl->queue = clCreateCommandQueue(cl->context, cl->device, 0, &err);
		CHECKERR(err);

		cl->program = clCreateProgramWithSource(
				cl->context, 1,
				(const char *[]) { (char *)ASSETS_CONVERT_RGB32_YUV420_CL },
				(size_t[]) { ASSETS_CONVERT_RGB32_YUV420_CL_LEN },
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
		}

		cl->kernel = clCreateKernel(cl->program, "convert_rgb32_yuv420", &err);
		CHECKERR(err);
	} else {
		panic("Unsupported pixel formats: %s, %s.",
				av_get_pix_fmt_name(infmt),
				av_get_pix_fmt_name(outfmt));
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
		uint8_t **in_planes, const int *in_strides,
		uint8_t **out_planes, const int *out_strides) {
	int err;

	struct pixconv_cl *cl = (struct pixconv_cl *)conv;

	if (is_rgb32_nv12(conv->infmt, conv->outfmt)) {
		int r, g, b;
		rgbdesc(conv->infmt, &r, &g, &b);
		err = clSetKernelArg(cl->kernel, 0, sizeof(r), &r);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 1, sizeof(g), &g);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 2, sizeof(b), &b);
		CHECKERR(err);

		// Set up input image
		cl_image_format input_format = {
			.image_channel_data_type = CL_UNSIGNED_INT32,
			.image_channel_order = CL_R,
		};
		cl_image_desc input_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = conv->inrect.w,
			.image_height = conv->inrect.h,
			.image_row_pitch = in_strides[0],
		};
		cl_mem input_image = clCreateImage(
				cl->context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
				&input_format, &input_desc, in_planes[0], &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 3, sizeof(input_image), &input_image);
		CHECKERR(err);

		// Set up output Y image
		cl_image_format output_y_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_y_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = conv->outrect.w,
			.image_height = conv->outrect.h,
			.image_row_pitch = out_strides[0],
		};
		cl_mem output_y_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
				&output_y_format, &output_y_desc, out_planes[0], &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 4, sizeof(output_y_image), &output_y_image);
		CHECKERR(err);

		// Set up output UV image
		cl_image_format output_uv_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_RG,
		};
		cl_image_desc output_uv_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = conv->outrect.w / 2,
			.image_height = conv->outrect.h / 2,
			.image_row_pitch = out_strides[1],
		};
		cl_mem output_uv_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
				&output_uv_format, &output_uv_desc, out_planes[1], &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 5, sizeof(output_uv_image), &output_uv_image);
		CHECKERR(err);

		// Run kernel
		CHECKERR(err);
		err = clEnqueueNDRangeKernel(
				cl->queue, cl->kernel, 2, NULL,
				(const size_t[]) { conv->inrect.w, conv->inrect.h, 0 }, NULL,
				0, NULL, NULL);
		CHECKERR(err);

		// Read Y
		err = clEnqueueReadImage(
				cl->queue, output_y_image, CL_TRUE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w, conv->outrect.h, 1 },
				out_strides[0], 0, out_planes[0],
				0, NULL, NULL);
		CHECKERR(err);

		// Read UV
		err = clEnqueueReadImage(
				cl->queue, output_uv_image, CL_TRUE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w / 2, conv->outrect.h / 2, 1 },
				out_strides[1], 0, out_planes[1],
				0, NULL, NULL);
		CHECKERR(err);

		return 0;
	} else if (is_rgb32_yuv420(conv->infmt, conv->outfmt)) {
		int r, g, b;
		rgbdesc(conv->infmt, &r, &g, &b);
		err = clSetKernelArg(cl->kernel, 0, sizeof(r), &r);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 1, sizeof(g), &g);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 2, sizeof(b), &b);
		CHECKERR(err);

		// Set up input image
		cl_image_format input_format = {
			.image_channel_data_type = CL_UNSIGNED_INT32,
			.image_channel_order = CL_R,
		};
		cl_image_desc input_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = conv->inrect.w,
			.image_height = conv->inrect.h,
			.image_row_pitch = in_strides[0],
		};
		cl_mem input_image = clCreateImage(
				cl->context, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR,
				&input_format, &input_desc, in_planes[0], &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 3, sizeof(input_image), &input_image);
		CHECKERR(err);

		// Set up output Y image
		cl_image_format output_y_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_y_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = conv->outrect.w,
			.image_height = conv->outrect.h,
			.image_row_pitch = out_strides[0],
		};
		cl_mem output_y_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
				&output_y_format, &output_y_desc, out_planes[0], &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 4, sizeof(output_y_image), &output_y_image);
		CHECKERR(err);

		// Set up output U image
		cl_image_format output_u_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_u_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = conv->outrect.w / 2,
			.image_height = conv->outrect.h / 2,
			.image_row_pitch = out_strides[1],
		};
		cl_mem output_u_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
				&output_u_format, &output_u_desc, out_planes[1], &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 5, sizeof(output_u_image), &output_u_image);
		CHECKERR(err);

		// Set up output V image
		cl_image_format output_v_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_v_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = conv->outrect.w / 2,
			.image_height = conv->outrect.h / 2,
			.image_row_pitch = out_strides[1],
		};
		cl_mem output_v_image = clCreateImage(
				cl->context, CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
				&output_v_format, &output_v_desc, out_planes[2], &err);
		CHECKERR(err);
		err = clSetKernelArg(cl->kernel, 6, sizeof(output_v_image), &output_v_image);
		CHECKERR(err);

		// Run kernel
		CHECKERR(err);
		err = clEnqueueNDRangeKernel(
				cl->queue, cl->kernel, 2, NULL,
				(const size_t[]) { conv->inrect.w, conv->inrect.h, 0 }, NULL,
				0, NULL, NULL);
		CHECKERR(err);

		// Read Y
		err = clEnqueueReadImage(
				cl->queue, output_y_image, CL_TRUE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w, conv->outrect.h, 1 },
				out_strides[0], 0, out_planes[0],
				0, NULL, NULL);
		CHECKERR(err);

		// Read U
		err = clEnqueueReadImage(
				cl->queue, output_u_image, CL_TRUE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w / 2, conv->outrect.h / 2, 1 },
				out_strides[1], 0, out_planes[1],
				0, NULL, NULL);
		CHECKERR(err);

		// Read V
		err = clEnqueueReadImage(
				cl->queue, output_v_image, CL_TRUE,
				(const size_t[]) { 0, 0, 0 },
				(const size_t[]) { conv->outrect.w / 2, conv->outrect.h / 2, 1 },
				out_strides[2], 0, out_planes[2],
				0, NULL, NULL);
		CHECKERR(err);

		return 0;
	} else {
		logln("Unsupported pixel formats: %s, %s",
				av_get_pix_fmt_name(conv->infmt),
				av_get_pix_fmt_name(conv->outfmt));
		return -1;
	}
}

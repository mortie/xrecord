#include "pixconv.h"

#include <stdbool.h>

// 2.0 support seems to be limited (fuck nvidia):
// https://en.wikipedia.org/wiki/OpenCL#OpenCL_2.0_support
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/opencl.h>

#include "assets.h"

#define CHECKERR(err) do { \
	if (err) { \
		fprintf(stderr, "%s:%i: error %i\n", __FILE__, __LINE__, err); \
		exit(1); \
	} else { \
		fprintf(stderr, "%s:%i: OK %i\n", __FILE__, __LINE__, err); \
	} \
} while (0)

struct pixconv_cl {
	struct pixconv conv;
	cl_program program;
	cl_kernel kernel;
};

struct conv_rgb32_nv12 {
	struct pixconv_cl cl;

	cl_mem input_image;
	cl_mem input_buffer;
	cl_mem output_y_image;
	cl_mem output_y_buffer;
	cl_mem output_uv_image;
	cl_mem output_uv_buffer;
};

static int is_rgb32_nv12(enum AVPixelFormat in, enum AVPixelFormat out) {
	return in == AV_PIX_FMT_BGRA && out == AV_PIX_FMT_NV12;
}

static void rgbdesc(enum AVPixelFormat fmt, int *r, int *g, int *b) {
	if (fmt == AV_PIX_FMT_BGRA) {
		*r = 1; *g = 2; *b = 3;
	} else {
		fprintf(stderr, "Unsupported pixel format.\n");
	}
}

struct pixconv *pixconv_create(
		struct rect inrect, enum AVPixelFormat infmt,
		struct rect outrect, enum AVPixelFormat outfmt) {
	struct pixconv_cl *cl;

	int err;
	cl_uint num_devices;
	cl_device_id device;
	err = clGetDeviceIDs(
			NULL, CL_DEVICE_TYPE_GPU, 1,
			&device, &num_devices);
	CHECKERR(err);

	cl_context context = clCreateContext(0, 1, &device, NULL, NULL, &err);
	CHECKERR(err);

	cl_command_queue queue_gpu = clCreateCommandQueue(context, device, 0, &err);
	CHECKERR(err);

	if (is_rgb32_nv12(infmt, outfmt)) {
		struct conv_rgb32_nv12 *rgb32_nv12 = malloc(sizeof(*rgb32_nv12));
		cl = (struct pixconv_cl *)rgb32_nv12;

		// Set up input image/buffer
		// Single-color uint32 image, because it interprets colors its own way
		cl_image_format input_format = {
			.image_channel_data_type = CL_UNSIGNED_INT32,
			.image_channel_order = CL_R,
		};
		cl_image_desc input_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = inrect.w,
			.image_height = inrect.h,
		};
		rgb32_nv12->input_image = clCreateImage(
				context, CL_MEM_READ_ONLY, &input_format,
				&input_desc, NULL, &err);
		CHECKERR(err);
		rgb32_nv12->input_buffer = clCreateBuffer(
				context, CL_MEM_READ_ONLY,
				inrect.w * inrect.h * 4, NULL, &err);
		CHECKERR(err);

		// Set up output Y image/buffr
		cl_image_format output_y_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_R,
		};
		cl_image_desc output_y_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = outrect.w / 2,
			.image_height = outrect.h / 2,
		};
		rgb32_nv12->output_y_image = clCreateImage(
				context, CL_MEM_WRITE_ONLY, &output_y_format,
				&output_y_desc, NULL, &err);
		CHECKERR(err);
		rgb32_nv12->output_y_buffer = clCreateBuffer(
				context, CL_MEM_READ_ONLY,
				(outrect.w / 2) * (outrect.h / 2) * 1, NULL, &err);
		CHECKERR(err);

		// Set up output UV image/buffer
		cl_image_format output_uv_format = {
			.image_channel_data_type = CL_UNSIGNED_INT8,
			.image_channel_order = CL_RG,
		};
		cl_image_desc output_uv_desc = {
			.image_type = CL_MEM_OBJECT_IMAGE2D,
			.image_width = outrect.w / 2,
			.image_height = outrect.h / 2,
		};
		rgb32_nv12->input_image = clCreateImage(
				context, CL_MEM_WRITE_ONLY, &output_uv_format,
				&output_uv_desc, NULL, &err);
		CHECKERR(err);
		rgb32_nv12->output_uv_buffer = clCreateBuffer(
				context, CL_MEM_READ_ONLY,
				(outrect.w / 2) * (outrect.h / 2) * 2, NULL, &err);
		CHECKERR(err);

		rgb32_nv12->cl.program = clCreateProgramWithSource(
				context, 1,
				(const char *[]) { (char *)ASSETS_CONVERT_RGB32_NV12_CL },
				(size_t[]) { ASSETS_CONVERT_RGB32_NV12_CL_LEN },
				&err);
		CHECKERR(err);

		err = clBuildProgram(rgb32_nv12->cl.program, 0, NULL, NULL, NULL, NULL);
		if (err) {
			size_t len;
			err = clGetProgramBuildInfo(
					rgb32_nv12->cl.program, device, CL_PROGRAM_BUILD_LOG,
					0, NULL, &len);
			CHECKERR(err);

			char *logstr = malloc(len);
			err = clGetProgramBuildInfo(
					rgb32_nv12->cl.program, device, CL_PROGRAM_BUILD_LOG,
					len, logstr, &len);
			CHECKERR(err);

			fprintf(stderr, "Failed to compile:\n%s", logstr);
		}

		rgb32_nv12->cl.kernel = clCreateKernel(
				rgb32_nv12->cl.program, "convert_rgb32_nv12", &err);
		CHECKERR(err);
	} else {
		fprintf(stderr, "Unsupported pixel formats.\n");
		return NULL;
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
		uint8_t const * const *in_planes, const int *in_strides,
		uint8_t **out_planes, const int *out_strides) {
	int err;

	if (is_rgb32_nv12(conv->infmt, conv->outfmt)) {
		struct conv_rgb32_nv12 *rgb32_nv12 = (struct conv_rgb32_nv12 *)conv;
		int r, g, b;
		err = clSetKernelArg(rgb32_nv12->cl.kernel, 0, sizeof(r), &r);
		CHECKERR(err);
		err = clSetKernelArg(rgb32_nv12->cl.kernel, 1, sizeof(g), &g);
		CHECKERR(err);
		err = clSetKernelArg(rgb32_nv12->cl.kernel, 2, sizeof(b), &b);
		CHECKERR(err);
		err = clSetKernelArg(
				rgb32_nv12->cl.kernel, 3,
				sizeof(rgb32_nv12->input_image), rgb32_nv12->input_image);
		CHECKERR(err);
		err = clSetKernelArg(
				rgb32_nv12->cl.kernel, 4,
				sizeof(rgb32_nv12->output_y_image), rgb32_nv12->output_y_image);
		CHECKERR(err);
		err = clSetKernelArg(
				rgb32_nv12->cl.kernel, 5,
				sizeof(rgb32_nv12->output_uv_image), rgb32_nv12->output_uv_image);
		CHECKERR(err);

		return 0;
	} else {
		fprintf(stderr, "Unsupported pixel formats.");
		return -1;
	}
}

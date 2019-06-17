__constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

kernel void convert_rgb32_nv12(
		float scale_x, float scale_y,
		int in_r, int in_g, int in_b,
		read_only image2d_t input,
		write_only image2d_t output_y,
		write_only image2d_t output_uv) {

	int outx = get_global_id(0);
	int outy = get_global_id(1);
	int inx = outx * scale_x;
	int iny = outy * scale_y;

	float pix_r, pix_g, pix_b;
	uint pix_y, pix_u, pix_v;

	uint4 pixvec = read_imageui(input, sampler, (int2)(inx, iny));
	uint pix = pixvec.x;

	pix_r = (float)((pix & (0xff << in_r)) >> in_r);
	pix_g = (float)((pix & (0xff << in_g)) >> in_g);
	pix_b = (float)((pix & (0xff << in_b)) >> in_b);

	pix_y = clamp( (0.257f * pix_r) + (0.504f * pix_g) + (0.098f * pix_b) + 16,  0.0f, 255.0f);
	pix_u = clamp(-(0.148f * pix_r) - (0.291f * pix_g) + (0.439f * pix_b) + 128, 0.0f, 255.0f);
	pix_v = clamp( (0.439f * pix_r) - (0.368f * pix_g) - (0.071f * pix_b) + 128, 0.0f, 255.0f);

	write_imageui(output_y, (int2)(outx, outy), (uint4)(pix_y, 0, 0, 0));
	write_imageui(output_uv, (int2)(outx / 2, outy / 2), (uint4)(pix_u, pix_v, 0, 255));
}

kernel void convert_rgb32_yuv420(
		float scale_x, float scale_y,
		int in_r, int in_g, int in_b,
		read_only image2d_t input,
		write_only image2d_t output_y,
		write_only image2d_t output_u,
		write_only image2d_t output_v) {

	int outx = get_global_id(0);
	int outy = get_global_id(1);
	int inx = outx * scale_x;
	int iny = outy * scale_y;

	float pix_r, pix_g, pix_b;
	uint pix_y, pix_u, pix_v;

	uint4 pixvec = read_imageui(input, sampler, (int2)(inx, iny));
	uint pix = pixvec.x;

	pix_r = (float)((pix & (0xff << in_r)) >> in_r);
	pix_g = (float)((pix & (0xff << in_g)) >> in_g);
	pix_b = (float)((pix & (0xff << in_b)) >> in_b);

	pix_y = clamp( (0.257f * pix_r) + (0.504f * pix_g) + (0.098f * pix_b) + 16,  0.0f, 255.0f);
	pix_u = clamp(-(0.148f * pix_r) - (0.291f * pix_g) + (0.439f * pix_b) + 128, 0.0f, 255.0f);
	pix_v = clamp( (0.439f * pix_r) - (0.368f * pix_g) - (0.071f * pix_b) + 128, 0.0f, 255.0f);

	write_imageui(output_y, (int2)(outx, outy), (uint4)(pix_y, 0, 0, 0));
	write_imageui(output_u, (int2)(outx / 2, outy / 2), (uint4)(pix_u, 0, 0, 255));
	write_imageui(output_v, (int2)(outx / 2, outy / 2), (uint4)(pix_v, 0, 0, 255));
}

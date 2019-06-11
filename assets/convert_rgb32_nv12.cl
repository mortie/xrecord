kernel void convert_rgb32_nv12(
		int in_r, int in_g, int in_b,
		read_only image2d_t input,
		write_only image2d_t output_y,
		write_only image2d_t output_uv) {
	int x = get_global_id(0);
	int y = get_global_id(1);

	int in_width = get_image_width(input);
	uint4 in_pix = read_imageui(
			input, 0, (int2)(in_width - 1 - x, y));

	int out_width = get_image_width(output_y);
}

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pixman.h>

#include "buffer.h"
#include "output-layout.h"
#include "render.h"

#include "wlr-screencopy-unstable-v1-protocol.h"

static pixman_format_code_t get_pixman_format(enum wl_shm_format wl_fmt) {
	switch (wl_fmt) {
#if GRIM_LITTLE_ENDIAN
	case WL_SHM_FORMAT_RGB332:
		return PIXMAN_r3g3b2;
	case WL_SHM_FORMAT_BGR233:
		return PIXMAN_b2g3r3;
	case WL_SHM_FORMAT_ARGB4444:
		return PIXMAN_a4r4g4b4;
	case WL_SHM_FORMAT_XRGB4444:
		return PIXMAN_x4r4g4b4;
	case WL_SHM_FORMAT_ABGR4444:
		return PIXMAN_a4b4g4r4;
	case WL_SHM_FORMAT_XBGR4444:
		return PIXMAN_x4b4g4r4;
	case WL_SHM_FORMAT_ARGB1555:
		return PIXMAN_a1r5g5b5;
	case WL_SHM_FORMAT_XRGB1555:
		return PIXMAN_x1r5g5b5;
	case WL_SHM_FORMAT_ABGR1555:
		return PIXMAN_a1b5g5r5;
	case WL_SHM_FORMAT_XBGR1555:
		return PIXMAN_x1b5g5r5;
	case WL_SHM_FORMAT_RGB565:
		return PIXMAN_r5g6b5;
	case WL_SHM_FORMAT_BGR565:
		return PIXMAN_b5g6r5;
	case WL_SHM_FORMAT_RGB888:
		return PIXMAN_r8g8b8;
	case WL_SHM_FORMAT_BGR888:
		return PIXMAN_b8g8r8;
	case WL_SHM_FORMAT_ARGB8888:
		return PIXMAN_a8r8g8b8;
	case WL_SHM_FORMAT_XRGB8888:
		return PIXMAN_x8r8g8b8;
	case WL_SHM_FORMAT_ABGR8888:
		return PIXMAN_a8b8g8r8;
	case WL_SHM_FORMAT_XBGR8888:
		return PIXMAN_x8b8g8r8;
	case WL_SHM_FORMAT_BGRA8888:
		return PIXMAN_b8g8r8a8;
	case WL_SHM_FORMAT_BGRX8888:
		return PIXMAN_b8g8r8x8;
	case WL_SHM_FORMAT_RGBA8888:
		return PIXMAN_r8g8b8a8;
	case WL_SHM_FORMAT_RGBX8888:
		return PIXMAN_r8g8b8x8;
	case WL_SHM_FORMAT_ARGB2101010:
		return PIXMAN_a2r10g10b10;
	case WL_SHM_FORMAT_ABGR2101010:
		return PIXMAN_a2b10g10r10;
	case WL_SHM_FORMAT_XRGB2101010:
		return PIXMAN_x2r10g10b10;
	case WL_SHM_FORMAT_XBGR2101010:
		return PIXMAN_x2b10g10r10;
#else
	case WL_SHM_FORMAT_ARGB8888:
		return PIXMAN_b8g8r8a8;
	case WL_SHM_FORMAT_XRGB8888:
		return PIXMAN_b8g8r8x8;
	case WL_SHM_FORMAT_ABGR8888:
		return PIXMAN_r8g8b8a8;
	case WL_SHM_FORMAT_XBGR8888:
		return PIXMAN_r8g8b8x8;
	case WL_SHM_FORMAT_BGRA8888:
		return PIXMAN_a8r8g8b8;
	case WL_SHM_FORMAT_BGRX8888:
		return PIXMAN_x8r8g8b8;
	case WL_SHM_FORMAT_RGBA8888:
		return PIXMAN_a8b8g8r8;
	case WL_SHM_FORMAT_RGBX8888:
		return PIXMAN_x8b8g8r8;
#endif
	default:
		return 0;
	}
}

bool is_format_supported(enum wl_shm_format fmt) {
	return get_pixman_format(fmt) != 0;
}

uint32_t get_format_min_stride(enum wl_shm_format fmt, uint32_t width) {
	uint32_t bits_per_pixel = PIXMAN_FORMAT_BPP(get_pixman_format(fmt));
	return ((width * bits_per_pixel + 0x1f) >> 5) * sizeof(uint32_t);
}

static void compute_composite_region(const struct pixman_f_transform *out2com,
		int output_width, int output_height, struct grim_box *dest,
		bool *grid_aligned) {
	struct pixman_transform o2c_fixedpt;
	pixman_transform_from_pixman_f_transform(&o2c_fixedpt, out2com);

	pixman_fixed_t w = pixman_int_to_fixed(output_width);
	pixman_fixed_t h = pixman_int_to_fixed(output_height);
	struct pixman_vector corners[4] = {
		{{0, 0, pixman_fixed_1}},
		{{w, 0, pixman_fixed_1}},
		{{0, h, pixman_fixed_1}},
		{{w, h, pixman_fixed_1}},
	};

	pixman_fixed_t x_min = INT32_MAX, x_max = INT32_MIN,
		y_min = INT32_MAX, y_max = INT32_MIN;
	for (int i = 0; i < 4; i++) {
		pixman_transform_point(&o2c_fixedpt, &corners[i]);
		x_min = corners[i].vector[0] < x_min ? corners[i].vector[0] : x_min;
		x_max = corners[i].vector[0] > x_max ? corners[i].vector[0] : x_max;
		y_min = corners[i].vector[1] < y_min ? corners[i].vector[1] : y_min;
		y_max = corners[i].vector[1] > y_max ? corners[i].vector[1] : y_max;
	}

	*grid_aligned = pixman_fixed_frac(x_min) == 0 &&
		pixman_fixed_frac(x_max) == 0 &&
		pixman_fixed_frac(y_min) == 0 &&
		pixman_fixed_frac(y_max) == 0;

	int32_t x1 = pixman_fixed_to_int(pixman_fixed_floor(x_min));
	int32_t x2 = pixman_fixed_to_int(pixman_fixed_ceil(x_max));
	int32_t y1 = pixman_fixed_to_int(pixman_fixed_floor(y_min));
	int32_t y2 = pixman_fixed_to_int(pixman_fixed_ceil(y_max));
	*dest = (struct grim_box) {
		.x = x1,
		.y = y1,
		.width = x2 - x1,
		.height = y2 - y1
	};
}

pixman_image_t *render(struct grim_state *state, struct grim_box *geometry,
		double scale) {
	int common_width = geometry->width * scale;
	int common_height = geometry->height * scale;
	pixman_image_t *common_image = pixman_image_create_bits(PIXMAN_a8r8g8b8,
		common_width, common_height, NULL, 0);
	if (!common_image) {
		fprintf(stderr, "failed to create image with size: %d x %d\n",
			common_width, common_height);
		return NULL;
	}

	struct grim_capture *capture;
	wl_list_for_each(capture, &state->captures, link) {
		struct grim_buffer *buffer = capture->buffer;
		if (buffer == NULL) {
			continue;
		}

		pixman_format_code_t pixman_fmt = get_pixman_format(buffer->format);
		if (!pixman_fmt) {
			fprintf(stderr, "unsupported format %d = 0x%08x\n",
				buffer->format, buffer->format);
			return NULL;
		}

		int32_t output_x = capture->logical_geometry.x - geometry->x;
		int32_t output_y = capture->logical_geometry.y - geometry->y;
		int32_t output_width = capture->logical_geometry.width;
		int32_t output_height = capture->logical_geometry.height;

		int32_t raw_output_width = buffer->width;
		int32_t raw_output_height = buffer->height;
		apply_output_transform(capture->transform, &raw_output_width, &raw_output_height);

		int output_flipped_x = get_output_flipped(capture->transform);
		int output_flipped_y = capture->screencopy_frame_flags &
			ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT ? -1 : 1;

		pixman_image_t *output_image = pixman_image_create_bits(
			pixman_fmt, buffer->width, buffer->height,
			buffer->data, buffer->stride);
		if (!output_image) {
			fprintf(stderr, "Failed to create image\n");
			return NULL;
		}

		// The transformation `out2com` will send a pixel in the output_image
		// to one in the common_image
		struct pixman_f_transform out2com;
		pixman_f_transform_init_identity(&out2com);
		pixman_f_transform_translate(&out2com, NULL,
			-(double)buffer->width / 2,
			-(double)buffer->height / 2);
		pixman_f_transform_scale(&out2com, NULL,
			(double)output_width / raw_output_width,
			(double)output_height * output_flipped_y / raw_output_height);
		pixman_f_transform_rotate(&out2com, NULL,
			round(cos(get_output_rotation(capture->transform))),
			round(sin(get_output_rotation(capture->transform))));
		pixman_f_transform_scale(&out2com, NULL, output_flipped_x, 1);
		pixman_f_transform_translate(&out2com, NULL,
			(double)output_width / 2,
			(double)output_height / 2);
		pixman_f_transform_translate(&out2com, NULL, output_x, output_y);
		pixman_f_transform_scale(&out2com, NULL, scale, scale);

		struct grim_box composite_dest;
		bool grid_aligned;
		compute_composite_region(&out2com, buffer->width,
			buffer->height, &composite_dest, &grid_aligned);

		pixman_f_transform_translate(&out2com, NULL,
			-composite_dest.x, -composite_dest.y);

		struct pixman_f_transform com2out;
		pixman_f_transform_invert(&com2out, &out2com);
		struct pixman_transform c2o_fixedpt;
		pixman_transform_from_pixman_f_transform(&c2o_fixedpt, &com2out);
		pixman_image_set_transform(output_image, &c2o_fixedpt);

		double x_scale = fmax(fabs(out2com.m[0][0]), fabs(out2com.m[0][1]));
		double y_scale = fmax(fabs(out2com.m[1][0]), fabs(out2com.m[1][1]));
		if (x_scale >= 0.75 && y_scale >= 0.75) {
			// Bilinear scaling is relatively fast and gives decent
			// results for upscaling and light downscaling
			pixman_image_set_filter(output_image,
				PIXMAN_FILTER_BILINEAR, NULL, 0);
		} else {
			// When downscaling, convolve the output_image so that each
			// pixel in the common_image collects colors from a region
			// of size roughly 1/x_scale*1/y_scale in the output_image
			int n_values = 0;
			pixman_fixed_t *conv = pixman_filter_create_separable_convolution(
				&n_values,
				pixman_double_to_fixed(fmax(1., 1. / x_scale)),
				pixman_double_to_fixed(fmax(1., 1. / y_scale)),
				PIXMAN_KERNEL_IMPULSE, PIXMAN_KERNEL_IMPULSE,
				PIXMAN_KERNEL_LANCZOS2, PIXMAN_KERNEL_LANCZOS2,
				2, 2);
			pixman_image_set_filter(output_image,
				PIXMAN_FILTER_SEPARABLE_CONVOLUTION, conv, n_values);
			free(conv);
		}

		bool overlapping = false;
		struct grim_capture *other_capture;
		wl_list_for_each(other_capture, &state->captures, link) {
			if (capture != other_capture && intersect_box(&capture->logical_geometry,
					&other_capture->logical_geometry)) {
				overlapping = true;
			}
		}
		/* OP_SRC copies the image instead of blending it, and is much
		 * faster, but this a) is incorrect in the weird case where
		 * logical outputs overlap and are partially transparent b)
		 * can draw the edge between two outputs incorrectly if that
		 * edge is not exactly grid aligned in the common image */
		pixman_op_t op = (grid_aligned && !overlapping) ? PIXMAN_OP_SRC : PIXMAN_OP_OVER;
		pixman_image_composite32(op, output_image, NULL, common_image,
			0, 0, 0, 0, composite_dest.x, composite_dest.y,
			composite_dest.width, composite_dest.height);

		pixman_image_unref(output_image);
	}

	return common_image;
}

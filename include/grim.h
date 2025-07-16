#ifndef _GRIM_H
#define _GRIM_H

#include <wayland-client.h>

#include "box.h"

enum grim_filetype {
	GRIM_FILETYPE_PNG,
	GRIM_FILETYPE_PPM,
	GRIM_FILETYPE_JPEG,
};

struct grim_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct zxdg_output_manager_v1 *xdg_output_manager;
	struct ext_output_image_capture_source_manager_v1 *ext_output_image_capture_source_manager;
	struct ext_foreign_toplevel_image_capture_source_manager_v1 *ext_foreign_toplevel_image_capture_source_manager;
	struct ext_image_copy_capture_manager_v1 *ext_image_copy_capture_manager;
	struct zwlr_screencopy_manager_v1 *screencopy_manager;
	struct ext_foreign_toplevel_list_v1 *foreign_toplevel_list;

	struct wl_list outputs;
	struct wl_list toplevels;

	struct wl_list captures;
	size_t n_done;
};

struct grim_buffer;

struct grim_output {
	struct grim_state *state;
	struct wl_output *wl_output;
	struct zxdg_output_v1 *xdg_output;
	struct wl_list link;

	int32_t fallback_x, fallback_y; // legacy position from wl_output.geometry
	uint32_t mode_width, mode_height; // current mode size
	enum wl_output_transform transform;
	int32_t scale;

	struct grim_box logical_geometry;
	double logical_scale; // guessed from the logical size
	char *name;
};

struct grim_capture {
	struct grim_state *state;
	struct grim_output *output;
	struct wl_list link;

	enum wl_output_transform transform;
	struct grim_box logical_geometry;

	struct grim_buffer *buffer;

	struct ext_image_copy_capture_session_v1 *ext_image_copy_capture_session;
	struct ext_image_copy_capture_frame_v1 *ext_image_copy_capture_frame;
	uint32_t buffer_width, buffer_height;
	enum wl_shm_format shm_format;
	bool has_shm_format;

	struct zwlr_screencopy_frame_v1 *screencopy_frame;
	uint32_t screencopy_frame_flags; // enum zwlr_screencopy_frame_v1_flags
};

struct grim_toplevel {
	struct ext_foreign_toplevel_handle_v1 *handle;
	struct wl_list link;

	char *identifier;
};

#endif

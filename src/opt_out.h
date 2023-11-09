/* clinfo output options */
#ifndef OPT_OUT_H
#define OPT_OUT_H

#include <string.h>

#include "ext.h"

enum output_modes {
	CLINFO_HUMAN = 1, /* more human readable */
	CLINFO_RAW = 2, /* property-by-property */
	CLINFO_BOTH = CLINFO_HUMAN | CLINFO_RAW
};

/* Specify how we should handle conditional properties. */
enum cond_prop_modes {
	COND_PROP_CHECK = 0, /* default: check, skip if invalid */
	COND_PROP_TRY = 1, /* try, don't print an error if invalid */
	COND_PROP_SHOW = 2 /* try, print an error if invalid */
};

/* Output options */
struct opt_out {
	enum output_modes mode;
	enum cond_prop_modes cond;

/* Specify that we should only print information about specific devices */
/* TODO proper memory management */
#define MAX_SELECTED_DEVICES 256
	cl_uint2 selected_devices[MAX_SELECTED_DEVICES];
	size_t num_selected_devices;

/* Specify that we should only print information about a specific property */
/* TODO proper memory management */
#define MAX_SELECTED_PROPS 256
	const char *selected_props[MAX_SELECTED_PROPS];
	size_t num_selected_props;

/* Specify if we should only be listing the platform and devices;
 * can be done in both human and raw mode, and only the platform
 * and device names (and number) will be shown
 * TODO check if terminal supports UTF-8 and use Unicode line-drawing
 * for the tree in list mode
 */
	cl_bool brief;
	cl_bool detailed; // !brief
	cl_bool offline;
	cl_bool null_platform;

/* JSON output for RAW */
	cl_bool json;

/* clGetDeviceInfo returns CL_INVALID_VALUE both for unknown properties
 * and when the destination variable is too small. Set the following to CL_TRUE
 * to check which one is the case
 */
	cl_bool check_size;
};

static inline cl_bool is_selected_platform(const struct opt_out *output, cl_uint p) {
	if (output->num_selected_devices == 0) return CL_TRUE;

	for (cl_uint i = 0; i < output->num_selected_devices; ++i) {
		if (p == output->selected_devices[i].s[0]) return CL_TRUE;
	}
	return CL_FALSE;
}

static inline cl_bool is_selected_device(const struct opt_out *output, cl_uint p, cl_uint d) {
	if (output->num_selected_devices == 0) return CL_TRUE;

	for (cl_uint i = 0; i < output->num_selected_devices; ++i) {
		const cl_uint2 cmp = output->selected_devices[i];
		if (p == cmp.s[0] && d == cmp.s[1]) return CL_TRUE;
	}
	return CL_FALSE;
}

static inline cl_bool is_selected_prop(const struct opt_out *output, const char *prop) {
	if (output->num_selected_props == 0) return CL_TRUE;

	for (cl_uint i = 0; i < output->num_selected_props; ++i) {
		if (strstr(prop, output->selected_props[i])) return CL_TRUE;
	}
	return CL_FALSE;
}
static inline cl_bool is_requested_prop(const struct opt_out *output, const char *prop) {
	// NOTE the difference compared to the above: here we are checking if a specific property
	// was *requested*, so if none was explicitly requested we return false here.
	if (output->num_selected_props == 0) return CL_FALSE;

	for (cl_uint i = 0; i < output->num_selected_props; ++i) {
		if (strstr(prop, output->selected_props[i])) return CL_TRUE;
	}
	return CL_FALSE;
}
#endif

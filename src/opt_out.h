/* clinfo output options */
#ifndef OPT_OUT_H
#define OPT_OUT_H

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
/* Specify if we should only be listing the platform and devices;
 * can be done in both human and raw mode, and only the platform
 * and device names (and number) will be shown
 * TODO check if terminal supports UTF-8 and use Unicode line-drawing
 * for the tree in list mode
 */
	cl_bool brief;
	cl_bool detailed; // !brief
	cl_bool offline;
/* clGetDeviceInfo returns CL_INVALID_VALUE both for unknown properties
 * and when the destination variable is too small. Set the following to CL_TRUE
 * to check which one is the case
 */
	cl_bool check_size;
};

#endif

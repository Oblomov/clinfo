/* OpenCL error handling */

#include <stdio.h>

#include "ext.h"
#include "fmtmacros.h"

cl_int error;

int
check_ocl_error(cl_int err, const char *what, const char *func, int line)
{
	if (err != CL_SUCCESS) {
		fflush(stdout);
		fflush(stderr);
		fprintf(stderr, "%s:%u: %s : error %d\n",
			func, line, what, err);
		fflush(stderr);
	}
	return err != CL_SUCCESS;
}

const char *current_function;
size_t current_line;
const char *current_param;

int
report_ocl_error(char *dstbuf, size_t sz, cl_int err, const char *fmt)
{
	static char full_fmt[1024];
	if (err != CL_SUCCESS) {
		snprintf(full_fmt, 1024, "<%s:%" PRIuS ": %s : error %d>",
			current_function, current_line, fmt, err);
		snprintf(dstbuf, sz, full_fmt, current_param);
	}
	return err != CL_SUCCESS;
}

int
report_ocl_error_old(char *where, size_t sz, cl_int err, const char *what, const char *func, int line)
{
	if (err != CL_SUCCESS) {
		snprintf(where, sz, "<%s:%d: %s : error %d>",
			func, line, what, err);
	}
	return err != CL_SUCCESS;
}

#define CHECK_ERROR(what) if (check_ocl_error(error, what, __func__, __LINE__)) exit(1)

#define REPORT_ERROR(what) report_ocl_error_old(strbuf, bufsz, error, what, __func__, __LINE__)
#define REPORT_ERROR2(what) report_ocl_error(strbuf, bufsz, error, what)


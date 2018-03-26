/* OpenCL error handling */

#ifndef ERROR_H
#define ERROR_H

#include <stdio.h>

#include "ext.h"
#include "info_loc.h"
#include "fmtmacros.h"

cl_int
check_ocl_error(cl_int err, const char *what, const char *func, int line)
{
	if (err != CL_SUCCESS) {
		fflush(stdout);
		fflush(stderr);
		fprintf(stderr, "%s:%u: %s : error %d\n",
			func, line, what, err);
		fflush(stderr);
	}
	return err;
}

cl_int
report_ocl_error_basic(char *dstbuf, size_t sz, cl_int err, const char *what, const char *func, int line)
{
	if (err != CL_SUCCESS) {
		snprintf(dstbuf, sz, "<%s:%d: %s : error %d>",
			func, line, what, err);
	}
	return err;
}


cl_int
report_ocl_error_loc(char *dstbuf, size_t sz, cl_int err, const char *fmt,
	const struct info_loc *loc)
{
	static char full_fmt[1024];
	if (err != CL_SUCCESS) {
		snprintf(full_fmt, 1024, "<%s:%" PRIuS ": %s : error %d>",
			loc->function, loc->line, fmt, err);
		snprintf(dstbuf, sz, full_fmt, loc->sname);
	}
	return err != CL_SUCCESS;
}

void
report_size_mismatch(char *dstbuf, size_t sz, size_t req, size_t ours,
	const struct info_loc *loc)
{
	snprintf(dstbuf, sz, "<%s:%" PRIuS ": %s : size mismatch "
		"(requested %" PRIuS ", we offer %" PRIuS ")>",
		loc->function, loc->line, loc->sname,
		req, ours);
}

#define CHECK_ERROR(error, what) if (check_ocl_error(error, what, __func__, __LINE__)) exit(1)

#define REPORT_ERROR(error, what) report_ocl_error_basic(strbuf, bufsz, error, what, __func__, __LINE__)
#define REPORT_ERROR_LOC(error, loc, what) report_ocl_error_loc(strbuf, bufsz, error, what, loc)
#define REPORT_SIZE_MISMATCH(loc, req, ours) report_size_mismatch(strbuf, bufsz, req, ours, loc)

#endif

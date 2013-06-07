/* OpenCL error handling */

#include <stdio.h>

cl_int error;

void
check_ocl_error(cl_int err, const char *what, const char *func, int line)
{
	if (err != CL_SUCCESS) {
		fprintf(stderr, "%s:%u: %s : error %d\n",
			func, line, what, err);
		exit(1);
	}
}

#define CHECK_ERROR(what) check_ocl_error(error, what, __func__, __LINE__)


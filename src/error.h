/* OpenCL error handling */

#include <stdio.h>

cl_int error;

int
check_ocl_error(cl_int err, const char *what, const char *func, int line)
{
	if (err != CL_SUCCESS) {
		fprintf(stderr, "%s:%u: %s : error %d\n",
			func, line, what, err);
	}
	return err != CL_SUCCESS;
}

#define CHECK_ERROR(what) if (check_ocl_error(error, what, __func__, __LINE__)) exit(1)


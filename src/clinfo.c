/* Collect all available information on all available devices
 * on all available OpenCL platforms present in the system
 */

#include <stdio.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

void
check_ocl_error(cl_int err, const char *what, const char *func, int line)
{
	if (err != CL_SUCCESS) {
		fprintf(stderr, "%s:%s: %s : error %d\n",
			func, line, what, err);
		exit(1);
	}
}

cl_int error;
#define CHECK_ERROR(what) check_ocl_error(error, what, __func__, __LINE__)


#define ALLOC(var, num, what) do { \
	var = malloc(num*sizeof(*var)); \
	if (!var) { \
		fprintf(stderr, "%s:%s: %s : Out of memory\n", \
			__func__, __LINE__, what); \
		exit(1); \
	} \
} while (0);

#define REALLOC(var, num, what) do { \
	var = realloc(var, num*sizeof(*var)); \
	if (!var) { \
		fprintf(stderr, "%s:%s: %s : Out of memory\n", \
			__func__, __LINE__, what); \
		exit(1); \
	} \
} while (0);


char *buffer;
size_t bufsz, nusz;

#define SHOW_STRING(cmd, id, param, str) do { \
	error = cmd(id, param, 0, NULL, &nusz); \
	CHECK_ERROR("get " #param " size"); \
	if (nusz > bufsz) { \
		REALLOC(buffer, nusz, #param); \
		bufsz = nusz; \
	} \
	error = cmd(id, param, bufsz, buffer, 0); \
	CHECK_ERROR("get " #param); \
	printf("  %-46s: %s\n", str, buffer); \
} while (0);

void
printPlatformInfo(cl_platform_id pid)
{
#define PARAM(param, str) \
	SHOW_STRING(clGetPlatformInfo, pid, CL_PLATFORM_##param, "Platform " str)

	PARAM(NAME, "Name");
	PARAM(VENDOR, "Vendor");
	PARAM(VERSION, "Version");
	PARAM(PROFILE, "Profile");
	PARAM(EXTENSIONS, "Extensions");
#undef PARAM
}

void
printDeviceInfo(cl_device_id *dev)
{
}

int main(void)
{
	cl_uint num_entries;
	cl_platform_id *platforms;
	cl_device_id *devices;
	uint i;

	error = clGetPlatformIDs(0, NULL, &num_entries);
	CHECK_ERROR("number of platforms");

	printf("%-48s: %u\n", "Number of platforms", num_entries);
	if (!num_entries)
		return 0;

	ALLOC(platforms, num_entries, "platform IDs");
	error = clGetPlatformIDs(num_entries, platforms, NULL);
	CHECK_ERROR("platform IDs");

	for (i = 0; i < num_entries; ++i)
		printPlatformInfo(platforms[i]);

}

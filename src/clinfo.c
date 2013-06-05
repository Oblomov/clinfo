/* Collect all available information on all available devices
 * on all available OpenCL platforms present in the system
 */

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include <string.h>

#include "cl_error.h"
#include "cl_mem.h"

cl_uint num_platforms;
cl_platform_id *platform;
char **platform_name;
cl_uint *num_devs;
cl_uint num_devs_all;
cl_device_id *all_devices;
cl_device_id *device;


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

/* Print platform info and prepare arrays for device info */
void
printPlatformInfo(cl_uint p)
{
	cl_platform_id pid = platform[p];

#define PARAM(param, str) \
	SHOW_STRING(clGetPlatformInfo, pid, CL_PLATFORM_##param, "Platform " str)

	puts("");
	PARAM(NAME, "Name");

	/* Store name for future reference */
	size_t len = strlen(buffer);
	platform_name[p] = malloc(len + 1);
	/* memcpy instead of strncpy since we already have the len
	 * and memcpy is possibly more optimized */
	memcpy(platform_name[p], buffer, len);
	platform_name[p][len] = '\0';

	PARAM(VENDOR, "Vendor");
	PARAM(VERSION, "Version");
	PARAM(PROFILE, "Profile");
	PARAM(EXTENSIONS, "Extensions");
#undef PARAM

	error = clGetDeviceIDs(pid, CL_DEVICE_TYPE_ALL, 0, NULL, num_devs + p);
	CHECK_ERROR("number of devices");
	num_devs_all += num_devs[p];
}

void
printDeviceInfo(cl_uint d)
{
	cl_device_id dev = device[d];
#define STR_PARAM(param, str) \
	SHOW_STRING(clGetDeviceInfo, dev, CL_DEVICE_##param, "Device " str)
	STR_PARAM(NAME, "Name");
}

int main(void)
{
	cl_uint p, d;

	error = clGetPlatformIDs(0, NULL, &num_platforms);
	CHECK_ERROR("number of platforms");

	printf("%-48s: %u\n", "Number of platforms", num_platforms);
	if (!num_platforms)
		return 0;

	ALLOC(platform, num_platforms, "platform IDs");
	error = clGetPlatformIDs(num_platforms, platform, NULL);
	CHECK_ERROR("platform IDs");

	ALLOC(platform_name, num_platforms, "platform names");
	ALLOC(num_devs, num_platforms, "platform devices");

	for (p = 0; p < num_platforms; ++p)
		printPlatformInfo(p);

	ALLOC(all_devices, num_devs_all, "device IDs");

	for (p = 0, device = all_devices;
	     p < num_platforms;
	     device += num_devs[p++]) {
		error = clGetDeviceIDs(platform[p], CL_DEVICE_TYPE_ALL, num_devs[p], device, NULL);
		printf("\n  %-46s: %s\n", "Platform Name", platform_name[p]);
		printf("%-48s: %u\n", "Number of devices", num_devs[p]);
		for (d = 0; d < num_devs[p]; ++d) {
			printDeviceInfo(d);
		}
	}
}

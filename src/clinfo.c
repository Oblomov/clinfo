/* Collect all available information on all available devices
 * on all available OpenCL platforms present in the system
 */

#include <time.h>
#include <string.h>

/* Load STDC format macros (PRI*), or define them
 * for those crappy, non-standard compilers
 */
#include "fmtmacros.h"

// Support for the horrible MS C compiler
#ifdef _MSC_VER
#include "ms_support.h"
#endif

#include "ext.h"
#include "error.h"
#include "memory.h"
#include "strbuf.h"

cl_uint num_platforms;
cl_platform_id *platform;
char **platform_name;
cl_uint *num_devs;
cl_uint num_devs_all;
cl_device_id *all_devices;
cl_device_id *device;

enum output_modes {
	CLINFO_HUMAN = 1, /* more human readable */
	CLINFO_RAW = 2, /* property-by-property */
	CLINFO_BOTH = CLINFO_HUMAN | CLINFO_RAW
};

enum output_modes output_mode = CLINFO_HUMAN;

static const char unk[] = "Unknown";
static const char none[] = "None";
static const char na[] = "n/a"; // not available
static const char fpsupp[] = "Floating-point support";

static const char* bool_str[] = { "No", "Yes" };
static const char* endian_str[] = { "Big-Endian", "Little-Endian" };
static const char* device_type_str[] = { unk, "Default", "CPU", "GPU", "Accelerator", "Custom" };
static const char* device_type_raw_str[] = { unk,
	"CL_DEVICE_TYPE_DEFAULT", "CL_DEVICE_TYPE_CPU", "CL_DEVICE_TYPE_GPU",
	"CL_DEVICE_TYPE_ACCELERATOR", "CL_DEVICE_TYPE_CUSTOM"
};
static const size_t device_type_str_count = sizeof(device_type_str)/sizeof(*device_type_str);
static const char* local_mem_type_str[] = { none, "Local", "Global" };
static const char* cache_type_str[] = { none, "Read-Only", "Read/Write" };

static const char* sources[] = {
	"#define GWO(type) global type* restrict\n",
	"#define GRO(type) global const type* restrict\n",
	"#define BODY int i = get_global_id(0); out[i] = in1[i] + in2[i]\n",
	"#define _KRN(T, N) void kernel sum##N(GWO(T##N) out, GRO(T##N) in1, GRO(T##N) in2) { BODY; }\n",
	"#define KRN(N) _KRN(float, N)\n",
	"KRN()\n/* KRN(2)\nKRN(4)\nKRN(8)\nKRN(16) */\n",
};

static const char empty_str[] = "";
static const char comma_str[] = ", ";
static const char vbar_str[] = " | ";

/* preferred workgroup size multiple for each kernel
 * have not found a platform where the WG multiple changes,
 * but keep this flexible (this can grow up to 5)
 */
#define NUM_KERNELS 1
size_t wgm[NUM_KERNELS];

#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(*ar))

#define INDENT "  "
#define I0_STR "%-48s  "
#define I1_STR "  %-46s  "
#define I2_STR "    %-44s  "

#define STR_PRINT(name, str) \
	printf(I1_STR "%s\n", name, skip_leading_ws(str))

#define SHOW_STRING(cmd, param, name, ...) do { \
	GET_STRING(cmd, param, #param, __VA_ARGS__); \
	STR_PRINT(name, strbuf); \
} while (0)

int had_error = 0;

int
platform_info_str(cl_platform_id pid, cl_platform_info param, const char* pname)
{
	error = clGetPlatformInfo(pid, param, 0, NULL, &nusz);
	if (nusz > bufsz) {
		REALLOC(strbuf, nusz, current_param);
		bufsz = nusz;
	}
	had_error = REPORT_ERROR2("get %s size");
	if (!had_error) {
		error = clGetPlatformInfo(pid, param, bufsz, strbuf, 0);
		had_error = REPORT_ERROR2("get %s");
	}
	printf(I1_STR "%s\n", pname, skip_leading_ws(strbuf));
	return had_error;
}

struct platform_info_checks {
	int has_khr_icd;
};

struct platform_info_traits {
	cl_platform_info param; // CL_PLATFORM_*
	const char *sname; // "CL_PLATFORM_*"
	const char *pname; // "Platform *"
	/* pointer to function that checks if the parameter should be checked */
	int (*check_func)(const struct platform_info_checks *);
};

int khr_icd_p(const struct platform_info_checks *chk)
{
	return chk->has_khr_icd;
}

#define PINFO_COND(symbol, name, funcptr) { symbol, #symbol, "Platform " name, &funcptr }
#define PINFO(symbol, name) { symbol, #symbol, "Platform " name, NULL }
struct platform_info_traits pinfo_traits[] = {
	PINFO(CL_PLATFORM_NAME, "Name"),
	PINFO(CL_PLATFORM_VENDOR, "Vendor"),
	PINFO(CL_PLATFORM_VERSION, "Version"),
	PINFO(CL_PLATFORM_PROFILE, "Profile"),
	PINFO(CL_PLATFORM_EXTENSIONS, "Extensions"),
	PINFO_COND(CL_PLATFORM_ICD_SUFFIX_KHR, "Extensions function suffix", khr_icd_p)
};

/* Print platform info and prepare arrays for device info */
void
printPlatformInfo(cl_uint p)
{
	cl_platform_id pid = platform[p];
	size_t len = 0;

	struct platform_info_checks pinfo_checks = { 0 };

	current_function = __func__;

	for (current_line = 0; current_line < ARRAY_SIZE(pinfo_traits); ++current_line) {
		const struct platform_info_traits *traits = pinfo_traits + current_line;
		current_param = traits->sname;

		if (traits->check_func && !traits->check_func(&pinfo_checks))
			continue;

		had_error = platform_info_str(pid, traits->param,
			output_mode == CLINFO_HUMAN ?
			traits->pname : traits->sname);

		if (had_error)
			continue;

		/* post-processing */

		switch (traits->param) {
		case CL_PLATFORM_NAME:
			/* Store name for future reference */
			len = strlen(strbuf);
			platform_name[p] = malloc(len + 1);
			/* memcpy instead of strncpy since we already have the len
			 * and memcpy is possibly more optimized */
			memcpy(platform_name[p], strbuf, len);
			platform_name[p][len] = '\0';
			break;
		case CL_PLATFORM_EXTENSIONS:
			pinfo_checks.has_khr_icd = !!strstr(strbuf, "cl_khr_icd");
			break;
		default:
			/* do nothing */
			break;
		}

	}

	error = clGetDeviceIDs(pid, CL_DEVICE_TYPE_ALL, 0, NULL, num_devs + p);
	if (error != CL_DEVICE_NOT_FOUND)
		CHECK_ERROR("number of devices");
	num_devs_all += num_devs[p];
}

#define GET_PARAM(param, var) do { \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, sizeof(var), &var, 0); \
	had_error = REPORT_ERROR("get " #param); \
} while (0)

#define GET_PARAM_PTR(param, var, num) do { \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, num*sizeof(*var), var, 0); \
	had_error = REPORT_ERROR("get " #param); \
} while (0)

#define GET_PARAM_ARRAY(param, var, num) do { \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, 0, NULL, &num); \
	had_error = REPORT_ERROR("get number of " #param); \
	if (!had_error) { \
		REALLOC(var, num/sizeof(*var), #param); \
		error = clGetDeviceInfo(dev, CL_DEVICE_##param, num, var, NULL); \
		had_error = REPORT_ERROR("get " #param); \
	} \
} while (0)

int
getWGsizes(cl_platform_id pid, cl_device_id dev)
{
	int ret = 0;

#define RR_ERROR(what) do { \
	had_error = REPORT_ERROR(what); \
	if (had_error) { \
		ret = error; \
		goto out; \
	} \
} while(0)


	cl_context_properties ctxpft[] = {
		CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
		0, 0 };
	cl_uint cursor = 0;
	cl_context ctx = 0;
	cl_program prg = 0;
	cl_kernel krn = 0;

	ctx = clCreateContext(ctxpft, 1, &dev, NULL, NULL, &error);
	RR_ERROR("create context");
	prg = clCreateProgramWithSource(ctx, ARRAY_SIZE(sources), sources, NULL, &error);
	RR_ERROR("create program");
	error = clBuildProgram(prg, 1, &dev, NULL, NULL, NULL);
	had_error = REPORT_ERROR("build program");
	if (had_error)
		ret = error;

	/* for a program build failure, dump the log to stderr before bailing */
	if (error == CL_BUILD_PROGRAM_FAILURE) {
		GET_STRING(clGetProgramBuildInfo, CL_PROGRAM_BUILD_LOG, "CL_PROGRAM_BUILD_LOG", prg, dev);
		if (error == CL_SUCCESS) {
			fputs("=== CL_PROGRAM_BUILD_LOG ===\n", stderr);
			fputs(strbuf, stderr);
		}
	}
	if (had_error)
		goto out;

	for (cursor = 0; cursor < NUM_KERNELS; ++cursor) {
		snprintf(strbuf, bufsz, "sum%u", 1<<cursor);
		if (cursor == 0)
			strbuf[3] = 0; // scalar kernel is called 'sum'
		krn = clCreateKernel(prg, strbuf, &error);
		RR_ERROR("create kernel");
		error = clGetKernelWorkGroupInfo(krn, dev, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,
			sizeof(*wgm), wgm + cursor, NULL);
		RR_ERROR("get kernel info");
		clReleaseKernel(krn);
		krn = 0;
	}

out:
	if (krn)
		clReleaseKernel(krn);
	if (prg)
		clReleaseProgram(prg);
	if (ctx)
		clReleaseContext(ctx);
	return ret;
}

/* parse a CL_DEVICE_VERSION info to determine the OpenCL version.
 * Returns an unsigned integer in the from major*10 + minor
 */
cl_uint
getOpenCLVersion(const char *version)
{
	cl_uint ret = 10;
	long parse = 0;
	const char *from = version;
	char *next = NULL;
	parse = strtol(from, &next, 10);

	if (next != from) {
		ret = parse*10;
		// skip the dot TODO should we actually check for the dot?
		from = ++next;
		parse = strtol(from, &next, 10);
		if (next != from)
			ret += parse;
	}
	return ret;
}

/*
 * Device properties/extensions used in traits checks, and relevant functions
 */

struct device_info_checks {
	cl_device_type devtype;
	char has_half[12];
	char has_double[24];
	char has_nv[29];
	char has_amd[30];
	char has_svm_ext[11];
	char has_fission[22];
	char has_atomic_counters[26];
	char has_image2d_buffer[27];
	char has_intel_local_thread[30];
	char has_altera_dev_temp[29];
	char has_spir[12];
	char has_qcom_ext_host_ptr[21];
	cl_uint dev_version;
};

void identify_device_extensions(const char *extensions, struct device_info_checks *chk)
{
#define _HAS_EXT(ext) (strstr(extensions, ext))
#define HAS_EXT(ext) _HAS_EXT(#ext)
#define CPY_EXT(what, ext) do { \
	strncpy(chk->has_##what, has, sizeof(ext)); \
	chk->has_##what[sizeof(ext)-1] = '\0'; \
} while (0)
#define CHECK_EXT(what, ext) do { \
	has = _HAS_EXT(#ext); \
	if (has) CPY_EXT(what, #ext); \
} while(0)

	char *has;
	CHECK_EXT(half, cl_khr_fp16);
	CHECK_EXT(double, cl_khr_fp64);
	CHECK_EXT(spir, cl_khr_spir);
	if (dev_has_double(&chk))
		CHECK_EXT(double, cl_amd_fp64);
	if (dev_has_double(&chk))
		CHECK_EXT(double, cl_APPLE_fp64_basic_ops);
		CHECK_EXT(nv, cl_nv_device_attribute_query);
		CHECK_EXT(amd, cl_amd_device_attribute_query);
		CHECK_EXT(svm_ext, cl_amd_svm);
		CHECK_EXT(fission, cl_ext_device_fission);
		CHECK_EXT(atomic_counters, cl_ext_atomic_counters_64);
	if (dev_has_atomic_counters(&chk))
		CHECK_EXT(atomic_counters, cl_ext_atomic_counters_32);
		CHECK_EXT(image2d_buffer, cl_khr_image2d_from_buffer);
		CHECK_EXT(intel_local_thread, cl_intel_exec_by_local_thread);
		CHECK_EXT(altera_dev_temp, cl_altera_device_temperature);
		CHECK_EXT(qcom_ext_host_ptr, cl_qcom_ext_host_ptr);
}

#define DEFINE_EXT_CHECK(ext) int dev_has_##ext(const struct device_info_checks *chk) \
{ \
	return !!(chk->has_##ext[0]); \
}

DEFINE_EXT_CHECK(half)
DEFINE_EXT_CHECK(double)
DEFINE_EXT_CHECK(nv)
DEFINE_EXT_CHECK(amd)
DEFINE_EXT_CHECK(svm_ext)
DEFINE_EXT_CHECK(fission)
DEFINE_EXT_CHECK(atomic_counters)
DEFINE_EXT_CHECK(image2d_buffer)
DEFINE_EXT_CHECK(intel_local_thread)
DEFINE_EXT_CHECK(altera_dev_temp)
DEFINE_EXT_CHECK(spir)
DEFINE_EXT_CHECK(qcom_ext_host_ptr)

// device supports 1.2
int dev_is_12(const struct device_info_checks *chk)
{
	return !!(chk->dev_version >= 12);
}

// device supports 2.0
int dev_is_20(const struct device_info_checks *chk)
{
	return !!(chk->dev_version >= 20);
}

int dev_is_gpu(const struct device_info_checks *chk)
{
	return !!(chk->devtype & CL_DEVICE_TYPE_GPU);
}

int dev_is_gpu_amd(const struct device_info_checks *chk)
{
	return dev_is_gpu(chk) && dev_has_amd(chk);
}

int dev_has_svm(const struct device_info_checks *chk)
{
	return dev_is_20(chk) || dev_has_svm_ext(chk);
}

/*
 * Device info print functions
 */

const char *cur_sfx = empty_str;

#define _GET_VAL \
	error = clGetDeviceInfo(dev, param, sizeof(val), &val, 0); \
	had_error = REPORT_ERROR2("get %s");

#define GET_VAL do { \
	_GET_VAL \
} while (0)

#define _FMT_VAL(fmt) \
	if (had_error) \
		printf(I1_STR "%s\n", pname, strbuf); \
	else \
		printf(I1_STR fmt "%s\n", pname, val, cur_sfx);

#define FMT_VAL(fmt) do { \
	_FMT_VAL(fmt) \
} while (0)

#define SHOW_VAL(fmt) do { \
	_GET_VAL \
	_FMT_VAL(fmt) \
} while (0)

#define DEFINE_DEVINFO_SHOW(how, type, fmt) \
int device_info_##how(cl_device_id dev, cl_device_info param, const char *pname, \
	const struct device_info_checks *chk) \
{ \
	type val; \
	SHOW_VAL(fmt); \
}

/* Get string-type info without showing it */
int device_info_str_get(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	error = clGetDeviceInfo(dev, param, 0, NULL, &nusz);
	if (nusz > bufsz) {
		REALLOC(strbuf, nusz, current_param);
		bufsz = nusz;
	}
	had_error = REPORT_ERROR2("get %s size");
	if (!had_error) {
		error = clGetDeviceInfo(dev, param, bufsz, strbuf, 0);
		had_error = REPORT_ERROR2("get %s");
	}
	return had_error;
}

int device_info_str(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	had_error = device_info_str_get(dev, param, pname, chk);
	printf(I1_STR "%s\n", pname, skip_leading_ws(strbuf));
	return had_error;
}

DEFINE_DEVINFO_SHOW(int, cl_uint, "%u")
DEFINE_DEVINFO_SHOW(hex, cl_uint, "0x%x")
DEFINE_DEVINFO_SHOW(long, cl_ulong, "%lu")
DEFINE_DEVINFO_SHOW(sz, size_t, "%" PRIuS)

int device_info_bool(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_bool val;
	GET_VAL;
	if (had_error)
		printf(I1_STR "%s\n", pname, strbuf);
	else
		printf(I1_STR "%s%s\n", pname, bool_str[val], cur_sfx);
}

int device_info_devtype(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_device_type val;
	GET_VAL;
	if (!had_error) {
		/* iterate over device type strings, appending their textual form
		 * to strbuf. We use plain strcpy since we know that it's at least
		 * 1024 so we are safe.
		 * TODO: check for extra bits/no bits
		 */
		size_t szval = 0;
		cl_uint i = device_type_str_count;
		const char *sep = (output_mode == CLINFO_HUMAN ? comma_str : vbar_str);
		size_t sepsz = (output_mode == CLINFO_HUMAN ? 2 : 3);
		const char * const *devstr = (output_mode == CLINFO_HUMAN ?
			device_type_str : device_type_raw_str);
		for (; i > 0; --i) {
			/* assemble CL_DEVICE_TYPE_* from index i */
			cl_device_type cur = (cl_device_type)(1) << (i-1);
			if (val & cur) {
				/* match: add separator if not first match */
				if (szval > 0) {
					strcpy(strbuf + szval, sep);
					szval += sepsz;
				}
				strcpy(strbuf + szval, devstr[i]);
				szval += strlen(devstr[i]);
			}
		}
		strbuf[szval] = '\0';
	}
	printf(I1_STR "%s\n", pname, strbuf);
	/* we abuse global strbuf to pass the device type over to the caller */
	if (!had_error)
		memcpy(strbuf, &val, sizeof(val));
	return had_error;
}

/* stringify a cl_device_topology_amd */
int devtopo_str(const cl_device_topology_amd *devtopo)
{
	switch (devtopo->raw.type) {
	case 0:
		snprintf(strbuf, bufsz, "(%s)", na);
		break;
	case CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD:
		snprintf(strbuf, bufsz, "PCI-E, %02x:%02x.%u",
			(cl_uchar)(devtopo->pcie.bus),
			devtopo->pcie.device, devtopo->pcie.function);
		break;
	default:
		snprintf(strbuf, bufsz, "<unknown (%u): %u %u %u %u %u>",
			devtopo->raw.type,
			devtopo->raw.data[0], devtopo->raw.data[1],
			devtopo->raw.data[2],
			devtopo->raw.data[3], devtopo->raw.data[4]);
	}
}

int device_info_devtopo_amd(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_device_topology_amd val;
	GET_VAL;
	/* TODO how to do this in CLINFO_RAW mode */
	if (!had_error) {
		devtopo_str(&val);
	}
	printf(I1_STR "%s\n", pname, strbuf);
}

/* we assemble a cl_device_topology_amd struct from the NVIDIA info */
int device_info_devtopo_nv(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_device_topology_amd devtopo;
	cl_uint val;

	devtopo.raw.type = CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD;

	GET_VAL; /* CL_DEVICE_PCI_BUS_ID_NV */

	if (!had_error) {
		devtopo.pcie.bus = val & 0xff;

		param = CL_DEVICE_PCI_SLOT_ID_NV;
		current_param = "CL_DEVICE_PCI_SLOT_ID_NV";

		GET_VAL;

		if (!had_error) {
			devtopo.pcie.device = val >> 3;
			devtopo.pcie.function = val & 7;
			devtopo_str(&devtopo);
		}
	}

	printf(I1_STR "%s\n", pname, strbuf);
}

/* NVIDIA Compute Capability */
int device_info_cc_nv(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_uint major, val;
	GET_VAL; /* MAJOR */
	if (!had_error) {
		major = val;
		param = CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV;
		current_param = "CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV";
		GET_VAL;
		if (!had_error)
			snprintf(strbuf, bufsz, "%u.%u", major, val);
	}

	printf(I1_STR "%s\n", pname, strbuf);
}

/*
 * Device info traits
 */

struct device_info_traits {
	enum output_modes output_mode;
	cl_device_info param; // CL_DEVICE_*
	const char *sname; // "CL_DEVICE_*"
	const char *pname; // "Device *"
	const char *sfx; // suffix for the output in non-raw mode
	/* pointer to function that shows the parameter */
	int (*show_func)(cl_device_id dev, cl_device_info param, const char *pname, const struct device_info_checks *);
	/* pointer to function that checks if the parameter should be checked */
	int (*check_func)(const struct device_info_checks *);
};

#define DINFO_SFX(symbol, name, sfx, typ) symbol, #symbol, name, sfx, device_info_##typ
#define DINFO(symbol, name, typ) symbol, #symbol, name, NULL, device_info_##typ

struct device_info_traits dinfo_traits[] = {
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NAME, "Device Name", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_VENDOR, "Device Vendor", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_VENDOR_ID, "Device Vendor ID", hex), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_VERSION, "Device Version", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DRIVER_VERSION, "Driver Version", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_OPENCL_C_VERSION, "Device OpenCL C Version", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_EXTENSIONS, "Device Extensions", str_get), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TYPE, "Device Type", devtype), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PROFILE, "Device Profile", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_BOARD_NAME_AMD, "Device Board Name (AMD)", str), dev_has_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TOPOLOGY_AMD, "Device Topology (AMD)", devtopo_amd), dev_has_amd },

	/* Device Topology (NV) is multipart, so different for HUMAN and RAW */
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PCI_BUS_ID_NV, "Device Topology (NV)", devtopo_nv), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PCI_BUS_ID_NV, "Device PCI bus (NV)", int), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PCI_SLOT_ID_NV, "Device PCI slot (NV)", int), dev_has_nv },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_COMPUTE_UNITS, "Max compute units", int), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SIMD_PER_COMPUTE_UNIT_AMD, "SIMD per compute unit (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SIMD_WIDTH_AMD, "SIMD width (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SIMD_INSTRUCTION_WIDTH_AMD, "SIMD instruction width (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_MAX_CLOCK_FREQUENCY, "Max clock frequency", "MHz", int), NULL },

	/* Device Compute Capability (NV) is multipart, so different for HUMAN and RAW */
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV, "NVIDIA Compute Capability", cc_nv), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV, "NVIDIA Compute Capability Major", int), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV, "NVIDIA Compute Capability Minor", int), dev_has_nv },

	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_CORE_TEMPERATURE_ALTERA, "Core Temperature (Altera)", " C", int), dev_has_altera_dev_temp },

};

void
printDeviceInfo(cl_uint d)
{
	cl_device_id dev = device[d];
	cl_device_type devtype;
	cl_device_local_mem_type lmemtype;
	cl_device_mem_cache_type cachetype;
	cl_device_exec_capabilities execap;
	cl_device_fp_config fpconfig;
	cl_platform_id pid;

	cl_command_queue_properties queueprop;

	cl_device_partition_property *partprop = NULL;
	size_t numpartprop = 0;
	cl_device_affinity_domain partdom;

	cl_device_partition_property_ext *partprop_ext = NULL;
	size_t numpartprop_ext = 0;
	cl_device_partition_property_ext *partdom_ext = NULL;
	size_t numpartdom_ext = 0;

	cl_uint uintval, uintval2;
	cl_uint cursor;
	cl_ulong ulongval;
	double doubleval;
	cl_bool boolval;
	size_t szval;
	size_t *szvals = NULL;
	cl_uint szels = 3;
	size_t len;

	char *extensions = NULL;

	struct device_info_checks chk;
	memset(&chk, 0, sizeof(chk));
	chk.dev_version = 10;

#define KB UINT64_C(1024)
#define MB (KB*KB)
#define GB (MB*KB)
#define TB (GB*KB)
#define MEM_SIZE(val) ( \
	val >= TB ? val/TB : \
	val >= GB ? val/GB : \
	val >= MB ? val/MB : \
	val/KB )
#define MEM_PFX(val) ( \
	val >= TB ? "TiB" : \
	val >= GB ? "GiB" : \
	val >= MB ? "MiB" : \
	 "KiB" )

#define STR_PARAM(param, str) \
	SHOW_STRING(clGetDeviceInfo, CL_DEVICE_##param, "Device " str, dev)
#define INT_PARAM(param, name, sfx) do { \
	GET_PARAM(param, uintval); \
	if (had_error) { \
		printf(I1_STR "%s\n", name, strbuf); \
	} else { \
		printf(I1_STR "%u" sfx "\n", name, uintval); \
	} \
} while (0)
#define HEX_PARAM(param, name) do { \
	GET_PARAM(param, uintval); \
	if (had_error) { \
		printf(I1_STR "%s\n", name, strbuf); \
	} else { \
		printf(I1_STR "0x%x\n", name, uintval); \
	} \
} while (0)
#define LONG_PARAM(param, name, sfx) do { \
	GET_PARAM(param, ulongval); \
	if (had_error) { \
		printf(I1_STR "%s\n", name, strbuf); \
	} else { \
		printf(I1_STR "%u" sfx "\n", name, ulongval); \
	} \
} while (0)
#define SZ_PARAM(param, name, sfx) do { \
	GET_PARAM(param, szval); \
	if (had_error) { \
		printf(I1_STR "%s\n", name, strbuf); \
	} else { \
		printf(I1_STR "%" PRIuS sfx "\n", name, szval); \
	} \
} while (0)
#define MEM_PARAM_STR(var, fmt, name) do { \
	doubleval = (double)var; \
	if (var > KB) { \
		snprintf(strbuf, bufsz, " (%.4lg%s)", \
			MEM_SIZE(doubleval), \
			MEM_PFX(doubleval)); \
		strbuf[bufsz-1] = '\0'; \
	} else strbuf[0] = '\0'; \
	printf(I1_STR fmt "%s\n", name, var, strbuf); \
} while (0)
#define MEM_PARAM(param, name) do { \
	GET_PARAM(param, ulongval); \
	if (had_error) { \
		printf(I1_STR "%s\n", name, strbuf); \
	} else { \
		MEM_PARAM_STR(ulongval, "%" PRIu64, name); \
	} \
} while (0)
#define BOOL_PARAM(param, name) do { \
	GET_PARAM(param, boolval); \
	if (had_error) { \
		printf(I1_STR "%s\n", name, strbuf); \
	} else { \
		STR_PRINT(name, bool_str[boolval]); \
	} \
} while (0)

	current_function = __func__;

	/* pointer to the traits for CL_DEVICE_EXTENSIONS */
	const struct device_info_traits *extensions_traits = NULL;

	for (current_line = 0; current_line < ARRAY_SIZE(dinfo_traits); ++current_line) {
		const struct device_info_traits *traits = dinfo_traits + current_line;
		current_param = traits->sname;

		/* skip if it's not for this output mode */
		if (!(output_mode & traits->output_mode))
			continue;

		if (traits->check_func && !traits->check_func(&chk))
			continue;

		cur_sfx = traits->sfx ? traits->sfx : empty_str;

		had_error = traits->show_func(dev, traits->param,
			output_mode == CLINFO_HUMAN ?
			traits->pname : traits->sname,
			&chk);

		if (traits->param == CL_DEVICE_EXTENSIONS) {
			/* make a backup of the extensions string, regardless of
			 * errors */
			extensions_traits = traits;
			len = strlen(strbuf);
			ALLOC(extensions, len+1, "extensions");
			memcpy(extensions, strbuf, len);
			extensions[len] = '\0';
		}

		if (had_error)
			continue;

		switch (traits->param) {
		case CL_DEVICE_VERSION:
			/* compute numeric value for OpenCL version */
			chk.dev_version = getOpenCLVersion(strbuf + 7);
			break;
		case CL_DEVICE_EXTENSIONS:
			identify_device_extensions(extensions, &chk);
			break;
		case CL_DEVICE_TYPE:
			/* strbuf was abused to give us the dev type */
			memcpy(&(chk.devtype), strbuf, sizeof(chk.devtype));
			break;
		default:
			/* do nothing */
			break;
		}
	}

	/* device fission, two different ways: core in 1.2, extension previously
	 * platforms that suppot both might expose different properties (e.g., partition
	 * by name is not considered in OpenCL 1.2, but an option with the extension
	 */
	szval = 0;
	if (dev_is_12(&chk)) {
		strncpy(strbuf + szval, "core, ", chk.has_fission[0] ? 6 : 4);
		szval += (chk.has_fission[0] ? 6 : 4);
	}
	if (dev_has_fission(&chk)) {
		strncpy(strbuf + szval, chk.has_fission, bufsz - (szval + 1));
		szval += strlen(chk.has_fission);
	}
	strbuf[szval] = 0;

	printf(I1_STR "(%s)\n", "Device Partition",
		szval ? strbuf : na);
	if (dev_is_12(&chk)) {
		INT_PARAM(PARTITION_MAX_SUB_DEVICES, INDENT "Max number of sub-devices",);
		GET_PARAM_ARRAY(PARTITION_PROPERTIES, partprop, szval);
		numpartprop = szval/sizeof(*partprop);
		printf(I2_STR, "Supported partition types");
		for (cursor = 0; cursor < numpartprop ; ++cursor) {
			switch(partprop[cursor]) {
			case 0:
				printf("none"); break;
			case CL_DEVICE_PARTITION_EQUALLY:
				printf("equally"); break;
			case CL_DEVICE_PARTITION_BY_COUNTS:
				printf("by counts"); break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN:
				printf("by affinity domain"); break;
			case CL_DEVICE_PARTITION_BY_NAMES_INTEL:
				printf("by name (Intel extension)"); break;
			default:
				printf("by <unknown> (0x%" PRIXPTR ")", partprop[cursor]); break;
			}
			if (cursor < numpartprop - 1)
				printf(", ");
		}
		if (numpartprop == 0) {
			printf("none specified"); // different from none
		}
		puts("");
		GET_PARAM(PARTITION_AFFINITY_DOMAIN, partdom);
		if (partdom) {
			cl_bool comma = CL_FALSE;
			printf(I2_STR, "Supported affinity domains");
#define CHECK_AFFINITY_FLAG(flag, text) do { \
	if (partdom & CL_DEVICE_AFFINITY_DOMAIN_##flag) { \
		printf("%s%s", (comma ? ", ": ""), text); comma = CL_TRUE; \
	} \
} while (0)
#define CHECK_AFFINITY_CACHE(level) \
	CHECK_AFFINITY_FLAG(level##_CACHE, #level " cache")

			CHECK_AFFINITY_FLAG(NUMA, "NUMA");
			CHECK_AFFINITY_CACHE(L1);
			CHECK_AFFINITY_CACHE(L2);
			CHECK_AFFINITY_CACHE(L3);
			CHECK_AFFINITY_CACHE(L4);
			CHECK_AFFINITY_FLAG(NEXT_PARTITIONABLE, "next partitionable");
			puts("");
		}
	}
	if (dev_has_fission(&chk)) {
		GET_PARAM_ARRAY(PARTITION_TYPES_EXT, partprop_ext, szval);
		numpartprop_ext = szval/sizeof(*partprop_ext);
		printf(I2_STR, "Supported partition types (ext)");
		for (cursor = 0; cursor < numpartprop_ext ; ++cursor) {
			switch(partprop_ext[cursor]) {
			case 0:
				printf("none"); break;
			case CL_DEVICE_PARTITION_EQUALLY_EXT:
				printf("equally"); break;
			case CL_DEVICE_PARTITION_BY_COUNTS_EXT:
				printf("by counts"); break;
			case CL_DEVICE_PARTITION_BY_NAMES_EXT:
				printf("by names"); break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT:
				printf("by affinity domain"); break;
			default:
				printf("by <unknown> (0x%" PRIX64 ")", partprop_ext[cursor]); break;
			}
			if (cursor < numpartprop_ext - 1)
				printf(", ");
		}
		puts("");
		GET_PARAM_ARRAY(AFFINITY_DOMAINS_EXT, partdom_ext, szval);
		numpartdom_ext = szval/sizeof(*partdom_ext);
		if (numpartdom_ext) {
			printf(I2_STR, "Supported affinity domains (ext)");
#define CASE_CACHE(level) \
	case CL_AFFINITY_DOMAIN_##level##_CACHE_EXT: \
		printf(#level " cache")
			for (cursor = 0; cursor < numpartdom_ext ; ++cursor) {
				switch(partdom_ext[cursor]) {
				CASE_CACHE(L1); break;
				CASE_CACHE(L2); break;
				CASE_CACHE(L3); break;
				CASE_CACHE(L4); break;
				case CL_AFFINITY_DOMAIN_NUMA_EXT:
					printf("NUMA"); break;
				case CL_AFFINITY_DOMAIN_NEXT_FISSIONABLE_EXT:
					printf("next fissionable"); break;
				default:
					printf("<unknown> (0x%" PRIX64 ")", partdom_ext[cursor]);
					break;
				}
				if (cursor < numpartdom_ext - 1)
					printf(", ");
			}
			puts("");
		}
	}

	// workgroup sizes
	INT_PARAM(MAX_WORK_ITEM_DIMENSIONS, "Max work item dimensions",);
	if (uintval > szels)
		szels = uintval;
	REALLOC(szvals, szels, "work item sizes");
	GET_PARAM_PTR(MAX_WORK_ITEM_SIZES, szvals, uintval);
	for (cursor = 0; cursor < uintval; ++cursor) {
		snprintf(strbuf, bufsz, "Max work item size[%u]", cursor);
		strbuf[bufsz-1] = '\0'; // this is probably never needed, but better safe than sorry
		printf(I2_STR "%" PRIuS "\n", strbuf , szvals[cursor]);
	}
	SZ_PARAM(MAX_WORK_GROUP_SIZE, "Max work group size",);

	GET_PARAM(PLATFORM, pid);
	if (!getWGsizes(pid, dev))
		printf(I1_STR "%" PRIuS "\n", "Preferred work group size multiple", wgm[0]);
	else
		printf(I1_STR "%s\n", "Preferred work group size multiple", strbuf);

	if (dev_has_nv(&chk)) {
		INT_PARAM(WARP_SIZE_NV, "Warp size (NVIDIA)",);
	}
	if (dev_is_gpu_amd(&chk)) {
		INT_PARAM(WAVEFRONT_WIDTH_AMD, "Wavefront width (AMD)",);
	}

	// preferred/native vector widths
	printf(I1_STR, "Preferred / native vector sizes");
#define _PRINT_VEC(UCtype, type, optional, ext) do { \
	GET_PARAM(PREFERRED_VECTOR_WIDTH_##UCtype, uintval); \
	if (!had_error) \
		GET_PARAM(NATIVE_VECTOR_WIDTH_##UCtype, uintval2); \
	if (had_error) { \
		printf("\n" I2_STR "%s", #type, strbuf); \
	} else { \
		printf("\n" I2_STR "%8u / %-8u", #type, uintval, uintval2); \
		if (optional) \
		printf(" (%s)", *ext ? ext : na); \
	} \
} while (0)
#define PRINT_VEC(UCtype, type) _PRINT_VEC(UCtype, type, 0, "")
#define PRINT_VEC_OPT(UCtype, type, ext) _PRINT_VEC(UCtype, type, 1, ext)

	PRINT_VEC(CHAR, char);
	PRINT_VEC(SHORT, short);
	PRINT_VEC(INT, int);
	PRINT_VEC(LONG, long); // this is actually optional in EMBED profiles
	PRINT_VEC_OPT(HALF, half, chk.has_half);
	PRINT_VEC(FLOAT, float);
	PRINT_VEC_OPT(DOUBLE, double, chk.has_double);
	puts("");

	// FP configurations
#define SHOW_FP_FLAG(str, flag) \
	printf(I2_STR "%s\n", str, bool_str[!!(fpconfig & CL_FP_##flag)])
#define SHOW_FP_SUPPORT(type) do { \
	GET_PARAM(type##_FP_CONFIG, fpconfig); \
	if (had_error) { \
		printf(I2_STR "%s\n", "Error", strbuf); \
	} else { \
		SHOW_FP_FLAG("Denormals", DENORM); \
		SHOW_FP_FLAG("Infinity and NANs", INF_NAN); \
		SHOW_FP_FLAG("Round to nearest", ROUND_TO_NEAREST); \
		SHOW_FP_FLAG("Round to zero", ROUND_TO_ZERO); \
		SHOW_FP_FLAG("Round to infinity", ROUND_TO_INF); \
		SHOW_FP_FLAG("IEEE754-2008 fused multiply-add", FMA); \
		SHOW_FP_FLAG("Correctly-rounded divide and sqrt operations", CORRECTLY_ROUNDED_DIVIDE_SQRT); \
		SHOW_FP_FLAG("Support is emulated in software", SOFT_FLOAT); \
	} \
} while (0)

#define FPSUPP_STR(str, opt) \
	"  %-17s%-29s " opt "\n", #str "-precision", fpsupp
	printf(FPSUPP_STR(Half, " (%s)"),
		chk.has_half[0] ? chk.has_half : na);
	if (dev_has_half(&chk))
		SHOW_FP_SUPPORT(HALF);
	printf(FPSUPP_STR(Single, " (core)"));
	SHOW_FP_SUPPORT(SINGLE);
	printf(FPSUPP_STR(Double, " (%s)"),
		chk.has_double[0] ? chk.has_double : na);
	if (dev_has_double(&chk))
		SHOW_FP_SUPPORT(DOUBLE);

	// arch bits and endianness
	GET_PARAM(ADDRESS_BITS, uintval);
	GET_PARAM(ENDIAN_LITTLE, boolval);
	printf(I1_STR "%u, %s\n", "Address bits", uintval, endian_str[boolval]);

	// memory size and alignment

	// global
	MEM_PARAM(GLOBAL_MEM_SIZE, "Global memory size");
	if (dev_is_gpu_amd(&chk)) {
		// FIXME seek better documentation about this. what does it mean?
		GET_PARAM_ARRAY(GLOBAL_FREE_MEMORY_AMD, szvals, szval);
		szels = szval/sizeof(*szvals);
		for (cursor = 0; cursor < szels; ++cursor) {
			MEM_PARAM_STR(szvals[cursor], "%" PRIuS, "Free global memory (AMD)");
		}

		INT_PARAM(GLOBAL_MEM_CHANNELS_AMD, "Global memory channels (AMD)",);
		INT_PARAM(GLOBAL_MEM_CHANNEL_BANKS_AMD, "Global memory banks per channel (AMD)",);
		INT_PARAM(GLOBAL_MEM_CHANNEL_BANK_WIDTH_AMD, "Global memory bank width (AMD)", " bytes");
	}

	BOOL_PARAM(ERROR_CORRECTION_SUPPORT, "Error Correction support");
	MEM_PARAM(MAX_MEM_ALLOC_SIZE, "Max memory allocation");

	BOOL_PARAM(HOST_UNIFIED_MEMORY, "Unified memory for Host and Device");
	if (dev_has_nv(&chk)) {
		BOOL_PARAM(INTEGRATED_MEMORY_NV, "NVIDIA integrated memory");
	}

	// SVM
	if (dev_has_svm(&chk)) {
		cl_device_svm_capabilities svm_cap;
		GET_PARAM(SVM_CAPABILITIES, svm_cap);
		if (!had_error) {
			szval = 0;
			strbuf[szval++] = '(';
			if (dev_is_20(&chk)) {
				strncpy(strbuf + szval, "core, ", chk.has_svm_ext[0] ? 6 : 4);
				szval += (chk.has_svm_ext[0] ? 6 : 4);
			}
			if (dev_has_svm_ext(&chk)) {
				strncpy(strbuf + szval, chk.has_svm_ext, bufsz - (szval + 2));
				szval += strlen(chk.has_svm_ext);
			}
			strbuf[szval++] = ')';
			strbuf[szval++] = 0;
		}
		printf(I1_STR "%s\n", "Shared Virtual Memory (SVM) capabilities", strbuf);
		if (!had_error) {
			STR_PRINT(INDENT "Coarse-grained buffer sharing", bool_str[!!(svm_cap & CL_DEVICE_SVM_COARSE_GRAIN_BUFFER)]);
			STR_PRINT(INDENT "Fine-grained buffer sharing", bool_str[!!(svm_cap & CL_DEVICE_SVM_FINE_GRAIN_BUFFER)]);
			STR_PRINT(INDENT "Fine-grained system sharing", bool_str[!!(svm_cap & CL_DEVICE_SVM_FINE_GRAIN_SYSTEM)]);
			STR_PRINT(INDENT "Atomics", bool_str[!!(svm_cap & CL_DEVICE_SVM_ATOMICS)]);
		}
	}

	INT_PARAM(MIN_DATA_TYPE_ALIGN_SIZE, "Minimum alignment for any data type", " bytes");
	GET_PARAM(MEM_BASE_ADDR_ALIGN, uintval);
	printf(I1_STR "%u bits (%u bytes)\n",
		"Alignment of base address", uintval, uintval/8);

	// atomics alignment
	if (dev_is_20(&chk)) {
		printf(I1_STR "\n", "Preferred alignment for atomics");
		INT_PARAM(PREFERRED_PLATFORM_ATOMIC_ALIGNMENT, INDENT "SVM", "");
		INT_PARAM(PREFERRED_GLOBAL_ATOMIC_ALIGNMENT, INDENT "Global", "");
		INT_PARAM(PREFERRED_LOCAL_ATOMIC_ALIGNMENT, INDENT "Local", "");

	}

	if (dev_has_qcom_ext_host_ptr(&chk)) {
		SZ_PARAM(PAGE_SIZE_QCOM, "Page size (QUALCOMM)", " bytes");
		SZ_PARAM(EXT_MEM_PADDING_IN_BYTES_QCOM, "Externa memory padding (QUALCOMM)", " bytes");
	}

	// global variables
	if (dev_is_20(&chk)) { // TODO some 1.2 devices respond to this too ...
		MEM_PARAM(MAX_GLOBAL_VARIABLE_SIZE, "Max size for global variable");
		MEM_PARAM(GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE, "Preferred total size of global vars");
	}

	// cache
	GET_PARAM(GLOBAL_MEM_CACHE_TYPE, cachetype);
	STR_PRINT("Global Memory cache type", cache_type_str[cachetype]);
	if (cachetype != CL_NONE) {
		MEM_PARAM(GLOBAL_MEM_CACHE_SIZE, "Global Memory cache size");
		INT_PARAM(GLOBAL_MEM_CACHELINE_SIZE, "Global Memory cache line", " bytes");
	}

	// images
	BOOL_PARAM(IMAGE_SUPPORT, "Image support");
	if (boolval) {
		INT_PARAM(MAX_SAMPLERS, INDENT "Max number of samplers per kernel",);
		if (dev_is_12(&chk)) {
			SZ_PARAM(IMAGE_MAX_BUFFER_SIZE, INDENT "Max 1D image size", " pixels");
			SZ_PARAM(IMAGE_MAX_ARRAY_SIZE, INDENT "Max 1D or 2D image array size", " images");
		}
		if (dev_has_image2d_buffer(&chk)) {
			SZ_PARAM(IMAGE_BASE_ADDRESS_ALIGNMENT, INDENT "Base address alignment for 2D image buffers",);
			SZ_PARAM(IMAGE_PITCH_ALIGNMENT, INDENT "Pitch alignment for 2D image buffers",);
		}
		GET_PARAM_PTR(IMAGE2D_MAX_HEIGHT, szvals, 1);
		GET_PARAM_PTR(IMAGE2D_MAX_WIDTH, (szvals+1), 1);
		printf(I2_STR "%" PRIuS "x%" PRIuS " pixels\n", "Max 2D image size",
			szvals[0], szvals[1]);
		GET_PARAM_PTR(IMAGE3D_MAX_HEIGHT, szvals, 1);
		GET_PARAM_PTR(IMAGE3D_MAX_WIDTH, (szvals+1), 1);
		GET_PARAM_PTR(IMAGE3D_MAX_DEPTH, (szvals+2), 1);
		printf(I2_STR "%" PRIuS "x%" PRIuS "x%" PRIuS " pixels\n", "Max 3D image size",
			szvals[0], szvals[1], szvals[2]);
		INT_PARAM(MAX_READ_IMAGE_ARGS, INDENT "Max number of read image args",);
		INT_PARAM(MAX_WRITE_IMAGE_ARGS, INDENT "Max number of write image args",);
		if (dev_is_20(&chk)) {
			INT_PARAM(MAX_READ_WRITE_IMAGE_ARGS, INDENT "Max number of read/write image args",);
		}
	}

	// pipes
	if (dev_is_20(&chk)) {
		INT_PARAM(MAX_PIPE_ARGS, "Max number of pipe args", "");
		INT_PARAM(PIPE_MAX_ACTIVE_RESERVATIONS, "Max active pipe reservations", "");
		GET_PARAM(PIPE_MAX_PACKET_SIZE, uintval);
		if (had_error)
			printf(I1_STR "%s\n", "Max pipe packet size", strbuf); \
		else
			MEM_PARAM_STR(uintval, "%u", "Max pipe packet size");
	}


	// local
	GET_PARAM(LOCAL_MEM_TYPE, lmemtype);
	STR_PRINT("Local memory type", local_mem_type_str[lmemtype]);
	if (lmemtype != CL_NONE)
		MEM_PARAM(LOCAL_MEM_SIZE, "Local memory size");
	if (dev_is_gpu_amd(&chk)) {
		MEM_PARAM(LOCAL_MEM_SIZE_PER_COMPUTE_UNIT_AMD, "Local memory size per CU (AMD)");
		INT_PARAM(LOCAL_MEM_BANKS_AMD, "Local memory banks (AMD)",);
	}

	// nv: registers/CU
	if (dev_has_nv(&chk)) {
		INT_PARAM(REGISTERS_PER_BLOCK_NV, "NVIDIA registers per CU",);
	}


	// constant
	MEM_PARAM(MAX_CONSTANT_BUFFER_SIZE, "Max constant buffer size");
	INT_PARAM(MAX_CONSTANT_ARGS, "Max number of constant args",);

	MEM_PARAM(MAX_PARAMETER_SIZE, "Max size of kernel argument");
	if (dev_has_atomic_counters(&chk))
		INT_PARAM(MAX_ATOMIC_COUNTERS_EXT, "Max number of atomic counters",);

	// queue and kernel capabilities

	GET_PARAM(QUEUE_PROPERTIES, queueprop);
	printf(I1_STR "%s\n",
		(dev_is_20(&chk) ? "Queue properties (on host)" : "Queue properties"),
		had_error ? strbuf : "");
	if (!had_error) {
		STR_PRINT(INDENT "Out-of-order execution", bool_str[!!(queueprop & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)]);
		STR_PRINT(INDENT "Profiling", bool_str[!!(queueprop & CL_QUEUE_PROFILING_ENABLE)]);
	}
	if (dev_has_intel_local_thread(&chk)) {
		printf(I1_STR "%s\n", INDENT "Intel local thread execution", bool_str[1]);
	}

	// queues on device
	if (dev_is_20(&chk)) {
		GET_PARAM(QUEUE_ON_DEVICE_PROPERTIES, queueprop);
		printf(I1_STR "%s\n", "Queue properties (on device)",
			had_error ? strbuf : "");
		if (!had_error) {
			STR_PRINT(INDENT "Out-of-order execution", bool_str[!!(queueprop & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)]);
			STR_PRINT(INDENT "Profiling", bool_str[!!(queueprop & CL_QUEUE_PROFILING_ENABLE)]);
		}

		GET_PARAM(QUEUE_ON_DEVICE_PREFERRED_SIZE, uintval);
		if (had_error)
			printf(I2_STR "%s\n", "Preferred size", strbuf); \
		else
			MEM_PARAM_STR(uintval, "%u", INDENT "Preferred size");
		GET_PARAM(QUEUE_ON_DEVICE_MAX_SIZE, uintval);
		if (had_error)
			printf(I2_STR "%s\n", "Max size", strbuf); \
		else
			MEM_PARAM_STR(uintval, "%u", INDENT "Max size");

		INT_PARAM(MAX_ON_DEVICE_QUEUES, "Max queues on device", "");
		INT_PARAM(MAX_ON_DEVICE_EVENTS, "Max events on device", "");
	}


	SZ_PARAM(PROFILING_TIMER_RESOLUTION, "Profiling timer resolution", "ns");
	if (dev_has_amd(&chk)) {
		time_t time;
		char *nl;
		GET_PARAM(PROFILING_TIMER_OFFSET_AMD, ulongval);
		time = ulongval/UINT64_C(1000000000);
		strncpy(strbuf, ctime(&time), bufsz);
		nl = strstr(strbuf, "\n");
		if (nl) *nl = '\0'; // kill the newline generated by ctime

		printf(I1_STR "%" PRIu64 "ns (%s)\n", "Profiling timer offset since Epoch (AMD)",
			ulongval, strbuf);
	}

	printf(I1_STR "\n", "Execution capabilities");
	GET_PARAM(EXECUTION_CAPABILITIES, execap);
	STR_PRINT(INDENT "Run OpenCL kernels", bool_str[!!(execap & CL_EXEC_KERNEL)]);
	STR_PRINT(INDENT "Run native kernels", bool_str[!!(execap & CL_EXEC_NATIVE_KERNEL)]);
	if (dev_has_nv(&chk)) {
		BOOL_PARAM(KERNEL_EXEC_TIMEOUT_NV, INDENT "NVIDIA kernel execution timeout");
		BOOL_PARAM(GPU_OVERLAP_NV, "NVIDIA concurrent copy and kernel execution");
		INT_PARAM(ATTRIBUTE_ASYNC_ENGINE_COUNT_NV, INDENT "Number of copy engines",);
	}
	if (dev_has_spir(&chk)) {
		SHOW_STRING(clGetDeviceInfo, CL_DEVICE_SPIR_VERSIONS, INDENT "SPIR versions", dev);
	}

	if (dev_is_12(&chk)) {
		BOOL_PARAM(PREFERRED_INTEROP_USER_SYNC, "Prefer user sync for interops");
		MEM_PARAM(PRINTF_BUFFER_SIZE, "printf() buffer size");
		STR_PARAM(BUILT_IN_KERNELS, "Built-in kernels");
	}

	// misc. availability
	BOOL_PARAM(AVAILABLE, "Device Available");
	BOOL_PARAM(COMPILER_AVAILABLE, "Compiler Available");
	if (dev_is_12(&chk))
		BOOL_PARAM(LINKER_AVAILABLE, "Linker Available");

	// and finally the extensions
	printf(I1_STR "%s\n", (output_mode == CLINFO_HUMAN ?
			extensions_traits->pname :
			extensions_traits->sname), extensions);
	free(extensions);
	extensions = NULL;
}

int main(int argc, char *argv[])
{
	cl_uint p, d;

	/* if there's a 'raw' in the program name, switch to raw output mode */
	if (strstr(argv[0], "raw"))
		output_mode = CLINFO_RAW;

	ALLOC(strbuf, 1024, "general string buffer");
	bufsz = 1024;

	error = clGetPlatformIDs(0, NULL, &num_platforms);
	CHECK_ERROR("number of platforms");

	printf(I0_STR "%u\n", "Number of platforms", num_platforms);
	if (!num_platforms)
		return 0;

	ALLOC(platform, num_platforms, "platform IDs");
	error = clGetPlatformIDs(num_platforms, platform, NULL);
	CHECK_ERROR("platform IDs");

	ALLOC(platform_name, num_platforms, "platform names");
	ALLOC(num_devs, num_platforms, "platform devices");

	for (p = 0; p < num_platforms; ++p) {
		printPlatformInfo(p);
		puts("");
	}

	if (num_devs_all > 0)
		ALLOC(all_devices, num_devs_all, "device IDs");

	for (p = 0, device = all_devices;
	     p < num_platforms;
	     device += num_devs[p++]) {
		printf(I1_STR "%s\n", "Platform Name", platform_name[p]);
		printf(I0_STR "%u\n", "Number of devices", num_devs[p]);

		if (num_devs[p] > 0) {
			error = clGetDeviceIDs(platform[p], CL_DEVICE_TYPE_ALL, num_devs[p], device, NULL);
			CHECK_ERROR("device IDs");
		}
		for (d = 0; d < num_devs[p]; ++d) {
			printDeviceInfo(d);
			if (d < num_devs[p] - 1)
				puts("");
			fflush(stdout);
			fflush(stderr);
		}
		if (p < num_platforms - 1)
			puts("");
	}
}

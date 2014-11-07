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

#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(*ar))

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
static const char core[] = "core"; // not available
static const char fpsupp[] = "Floating-point support";

static const char* bool_str[] = { "No", "Yes" };
static const char* bool_raw_str[] = { "CL_FALSE", "CL_TRUE" };

static const char* endian_str[] = { "Big-Endian", "Little-Endian" };

static const char* device_type_str[] = { unk, "Default", "CPU", "GPU", "Accelerator", "Custom" };
static const char* device_type_raw_str[] = { unk,
	"CL_DEVICE_TYPE_DEFAULT", "CL_DEVICE_TYPE_CPU", "CL_DEVICE_TYPE_GPU",
	"CL_DEVICE_TYPE_ACCELERATOR", "CL_DEVICE_TYPE_CUSTOM"
};
static const size_t device_type_str_count = ARRAY_SIZE(device_type_str);

static const char* partition_type_str[] = {
	"none specified", "none", "equally", "by counts", "by affinity domain", "by names (Intel)"
};
static const char* partition_type_raw_str[] = {
	"NONE SPECIFIED",
	"NONE",
	"CL_DEVICE_PARTITION_EQUALLY_EXT",
	"CL_DEVICE_PARTITION_BY_COUNTS_EXT",
	"CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT",
	"CL_DEVICE_PARTITION_BY_NAMES_INTEL_EXT"
};

static const char* affinity_domain_str[] = {
	"NUMA", "L4 cache", "L3 cache", "L2 cache", "L1 cache", "next partitionalbe"
};

static const char* affinity_domain_raw_str[] = {
	"CL_DEVICE_AFFINITY_DOMAIN_NUMA",
	"CL_DEVICE_AFFINITY_DOMAIN_L4_CACHE",
	"CL_DEVICE_AFFINITY_DOMAIN_L3_CACHE",
	"CL_DEVICE_AFFINITY_DOMAIN_L2_CACHE",
	"CL_DEVICE_AFFINITY_DOMAIN_L1_CACHE",
	"CL_DEVICE_AFFINITY_DOMAIN_NEXT_PARTITIONABLE"
};

static const char* affinity_domain_raw_ext_str[] = {
	"CL_AFFINITY_DOMAIN_NUMA_EXT",
	"CL_AFFINITY_DOMAIN_L4_CACHE_EXT",
	"CL_AFFINITY_DOMAIN_L3_CACHE_EXT",
	"CL_AFFINITY_DOMAIN_L2_CACHE_EXT",
	"CL_AFFINITY_DOMAIN_L1_CACHE_EXT",
	"CL_AFFINITY_DOMAIN_NEXT_FISSIONABLE_EXT"
};

const size_t affinity_domain_count = ARRAY_SIZE(affinity_domain_str);

static const char* fp_conf_str[] = {
	"Denormals", "Infinity and NANs", "Round to nearest", "Round to zero",
	"Round to infinity", "IEEE754-2008 fused multiply-add",
	"Support is emulated in software",
	"Correctly-rounded divide and sqrt operations"
};

static const char* fp_conf_raw_str[] = {
	"CL_FP_DENORM",
	"CL_FP_INF_NAN",
	"CL_FP_ROUND_TO_NEAREST",
	"CL_FP_ROUND_TO_ZERO",
	"CL_FP_ROUND_TO_INF",
	"CL_FP_FMA",
	"CL_FP_SOFT_FLOAT",
	"CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT"
};

const size_t fp_conf_count = ARRAY_SIZE(fp_conf_str);

static const char* svm_cap_str[] = {
	"Coarse-grained buffer sharing",
	"Fine-grained buffer sharing",
	"Fine-grained system sharing",
	"Atomics"
};

static const char* svm_cap_raw_str[] = {
	"CL_DEVICE_SVM_COARSE_GRAIN_BUFFER",
	"CL_DEVICE_SVM_FINE_GRAIN_BUFFER",
	"CL_DEVICE_SVM_FINE_GRAIN_SYSTEM",
	"CL_DEVICE_SVM_ATOMICS",
};

const size_t svm_cap_count = ARRAY_SIZE(svm_cap_str);

static const char* memsfx[] = {
	"B", "KiB", "MiB", "GiB", "TiB"
};

const size_t memsfx_count = ARRAY_SIZE(memsfx);

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

/* preferred workgroup size multiple for each kernel
 * have not found a platform where the WG multiple changes,
 * but keep this flexible (this can grow up to 5)
 */
#define NUM_KERNELS 1
size_t wgm[NUM_KERNELS];

#define INDENT "  "
#define I0_STR "%-48s  "
#define I1_STR "  %-46s  "
#define I2_STR "    %-44s  "

static const char empty_str[] = "";
static const char spc_str[] = " ";
static const char times_str[] = "x";
static const char comma_str[] = ", ";
static const char vbar_str[] = " | ";


#define STR_PRINT(name, str) \
	printf(I1_STR "%s\n", name, skip_leading_ws(str))

#define SHOW_STRING(cmd, param, name, ...) do { \
	GET_STRING(cmd, param, #param, __VA_ARGS__); \
	STR_PRINT(name, strbuf); \
} while (0)


int had_error = 0;
const char *cur_sfx = empty_str;

/* print strbuf, prefixed by pname, skipping leading whitespace if skip is nonzero,
 * affixing cur_sfx */
static inline
void show_strbuf(const char *pname, int skip)
{
	if (skip)
		printf(I1_STR "%s%s\n", pname, skip_leading_ws(strbuf), cur_sfx);
	else
		printf(I1_STR "%s%s\n", pname, strbuf, cur_sfx);
}

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
	show_strbuf(pname, 1);
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

int dev_has_partition(const struct device_info_checks *chk)
{
	return dev_is_12(chk) || dev_has_fission(chk);
}

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
	CHECK_EXT(spir, cl_khr_spir);
	CHECK_EXT(double, cl_khr_fp64);
	if (!dev_has_double(chk))
		CHECK_EXT(double, cl_amd_fp64);
	if (!dev_has_double(chk))
		CHECK_EXT(double, cl_APPLE_fp64_basic_ops);
	CHECK_EXT(nv, cl_nv_device_attribute_query);
	CHECK_EXT(amd, cl_amd_device_attribute_query);
	CHECK_EXT(svm_ext, cl_amd_svm);
	CHECK_EXT(fission, cl_ext_device_fission);
	CHECK_EXT(atomic_counters, cl_ext_atomic_counters_64);
	if (dev_has_atomic_counters(chk))
		CHECK_EXT(atomic_counters, cl_ext_atomic_counters_32);
	CHECK_EXT(image2d_buffer, cl_khr_image2d_from_buffer);
	CHECK_EXT(intel_local_thread, cl_intel_exec_by_local_thread);
	CHECK_EXT(altera_dev_temp, cl_altera_device_temperature);
	CHECK_EXT(qcom_ext_host_ptr, cl_qcom_ext_host_ptr);
}



/*
 * Device info print functions
 */

#define _GET_VAL \
	error = clGetDeviceInfo(dev, param, sizeof(val), &val, 0); \
	had_error = REPORT_ERROR2("get %s");

#define _GET_VAL_ARRAY \
	error = clGetDeviceInfo(dev, param, 0, NULL, &szval); \
	had_error = REPORT_ERROR2("get number of %s"); \
	numval = szval/sizeof(val); \
	if (!had_error) { \
		REALLOC(val, numval, current_param); \
		error = clGetDeviceInfo(dev, param, szval, val, NULL); \
		had_error = REPORT_ERROR("get %s"); \
	}

#define GET_VAL do { \
	_GET_VAL \
} while (0)

#define GET_VAL_ARRAY do { \
	_GET_VAL_ARRAY \
} while (0)

#define _FMT_VAL(fmt) \
	if (had_error) \
		show_strbuf(pname, 0); \
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
	return had_error; \
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
	show_strbuf(pname, 1);
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
	const char * const * str = (output_mode == CLINFO_HUMAN ?
		bool_str : bool_raw_str);
	GET_VAL;
	if (had_error)
		show_strbuf(pname, 0);
	else
		printf(I1_STR "%s%s\n", pname, str[val], cur_sfx);
	return had_error;
}

size_t strbuf_mem(cl_ulong val, size_t szval)
{
	double dbl = val;
	int sfx = 0;
	while (dbl > 1024 && sfx < memsfx_count) {
		dbl /= 1024;
		++sfx;
	}
	return sprintf(strbuf + szval, " (%.4lg%s)",
		dbl, memsfx[sfx]);
}

int device_info_mem(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_ulong val;
	size_t szval = 0;
	GET_VAL;
	if (!had_error) {
		szval += sprintf(strbuf, "%" PRIu64, val);
		if (output_mode == CLINFO_HUMAN && val > 1024)
			szval += strbuf_mem(val, szval);
	}
	show_strbuf(pname, 0);
	return had_error;
}

int device_info_free_mem_amd(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	size_t *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL;
	if (!had_error) {
		size_t cursor = 0;
		szval = 0;
		for (cursor = 0; cursor < numval; ++cursor) {
			if (szval > 0) {
				strbuf[szval] = ' ';
				++szval;
			}
			szval += sprintf(strbuf, "%" PRIuS, val[cursor]);
			if (output_mode == CLINFO_HUMAN)
				szval += strbuf_mem(val[cursor], szval);
		}
	}
	show_strbuf(pname, 0);
	return had_error;
}


int device_info_szptr(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	size_t *val = NULL;
	size_t szval, numval;
	GET_VAL_ARRAY;
	if (!had_error) {
		const char *sep = (output_mode == CLINFO_HUMAN ? times_str : spc_str);
		size_t sepsz = 1;
		int counter = 0;
		szval = 0;
		for (counter = 0; counter < numval; ++counter) {
			if (szval > 0) {
				strcpy(strbuf + szval, sep);
				szval += sepsz;
			}
			szval += snprintf(strbuf + szval, bufsz - szval - 1, "%zu", val[counter]);
			if (szval >= bufsz) {
				trunc_strbuf();
				break;
			}
		}
	}
	show_strbuf(pname, 0);
	free(val);
	return had_error;
}

int device_info_wg(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_platform_id val;
	{
		/* shadow */
		cl_device_info param = CL_DEVICE_PLATFORM;
		current_param = "CL_DEVICE_PLATFORM";
		GET_VAL;
	}
	current_param = "CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE";
	if (!had_error)
		had_error = getWGsizes(val, dev);
	if (!had_error) {
		sprintf(strbuf, "%" PRIuS, wgm[0]);
	}
	show_strbuf(pname, 0);
	return had_error;
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
	show_strbuf(pname, 0);
	/* we abuse global strbuf to pass the device type over to the caller */
	if (!had_error)
		memcpy(strbuf, &val, sizeof(val));
	return had_error;
}

/* stringify a cl_device_topology_amd */
void devtopo_str(const cl_device_topology_amd *devtopo)
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
	show_strbuf(pname, 0);
	return had_error;
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

	show_strbuf(pname, 0);
	return had_error;
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

	show_strbuf(pname, 0);
	return had_error;
}

/* Device Parition, CLINFO_HUMAN header */
int device_info_partition_header(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	int is_12 = dev_is_12(chk);
	int has_fission = dev_has_fission(chk);
	size_t szval = snprintf(strbuf, bufsz, "(%s%s%s)",
		(is_12 ? core : empty_str),
		(is_12 && has_fission ? comma_str : empty_str),
		chk->has_fission);
	if (szval >= bufsz)
		trunc_strbuf();

	show_strbuf(pname, 0);
	had_error = CL_SUCCESS;
	return had_error;
}

/* Device partition properties */
int device_info_partition_types(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	size_t numval, szval, cursor, slen;
	cl_device_partition_property *val = NULL;

	const char *sep = (output_mode == CLINFO_HUMAN ? comma_str : vbar_str);
	size_t sepsz = (output_mode == CLINFO_HUMAN ? 2 : 3);
	const char * const *ptstr = (output_mode == CLINFO_HUMAN ?
		partition_type_str : partition_type_raw_str);

	GET_VAL_ARRAY;

	szval = 0;
	if (!had_error) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			if (szval > 0) {
				strcpy(strbuf + szval, sep);
				szval += sepsz;
			}

			switch (val[cursor]) {
			case 0: str_idx = 1; break;
			case CL_DEVICE_PARTITION_EQUALLY: str_idx = 2; break;
			case CL_DEVICE_PARTITION_BY_COUNTS: str_idx = 3; break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN: str_idx = 4; break;
			case CL_DEVICE_PARTITION_BY_NAMES_INTEL: str_idx = 5; break;
			default:
				szval += sprintf(strbuf + szval, "by <unknown> (0x%" PRIXPTR ")", val[cursor]);
				break;
			}
			if (str_idx > 0) {
				/* string length, minus _EXT */
				slen = strlen(ptstr[str_idx]);
				if (output_mode == CLINFO_RAW && str_idx > 1)
					slen -= 4;
				strncpy(strbuf + szval, ptstr[str_idx], slen);
				szval += slen;
			}
		}
		if (szval == 0) {
			slen = strlen(ptstr[0]);
			strcpy(strbuf, ptstr[0]);
			szval += slen;
		}
		strbuf[szval] = '\0';
	}

	show_strbuf(pname, 0);

	free(val);
	return had_error;
}

int device_info_partition_types_ext(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	size_t numval, szval, cursor, slen;
	cl_device_partition_property_ext *val = NULL;

	const char *sep = (output_mode == CLINFO_HUMAN ? comma_str : vbar_str);
	size_t sepsz = (output_mode == CLINFO_HUMAN ? 2 : 3);
	const char * const *ptstr = (output_mode == CLINFO_HUMAN ?
		partition_type_str : partition_type_raw_str);

	GET_VAL_ARRAY;

	szval = 0;
	if (!had_error) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			if (szval > 0) {
				strcpy(strbuf + szval, sep);
				szval += sepsz;
			}

			switch (val[cursor]) {
			case 0: str_idx = 1; break;
			case CL_DEVICE_PARTITION_EQUALLY_EXT: str_idx = 2; break;
			case CL_DEVICE_PARTITION_BY_COUNTS_EXT: str_idx = 3; break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT: str_idx = 4; break;
			case CL_DEVICE_PARTITION_BY_NAMES_EXT: str_idx = 5; break;
			default:
				szval += sprintf(strbuf + szval, "by <unknown> (0x%" PRIXPTR ")", val[cursor]);
				break;
			}
			if (str_idx > 0) {
				/* string length */
				slen = strlen(ptstr[str_idx]);
				strncpy(strbuf + szval, ptstr[str_idx], slen);
				szval += slen;
			}
		}
		if (szval == 0) {
			slen = strlen(ptstr[0]);
			strcpy(strbuf, ptstr[0]);
			szval += slen;
		}
		strbuf[szval] = '\0';
	}

	show_strbuf(pname, 0);

	free(val);
	return had_error;
}


/* Device partition affinity domains */
int device_info_partition_affinities(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_device_affinity_domain val;
	GET_VAL;
	if (!had_error && val) {
		/* iterate over affinity domain strings appending their textual form
		 * to strbuf
		 * TODO: check for extra bits/no bits
		 */
		size_t szval = 0;
		cl_uint i = 0;
		const char *sep = (output_mode == CLINFO_HUMAN ? comma_str : vbar_str);
		size_t sepsz = (output_mode == CLINFO_HUMAN ? 2 : 3);
		const char * const *affstr = (output_mode == CLINFO_HUMAN ?
			affinity_domain_str : affinity_domain_raw_str);
		for (i = 0; i < affinity_domain_count; ++i) {
			cl_device_affinity_domain cur = (cl_device_affinity_domain)(1) << i;
			if (val & cur) {
				/* match: add separator if not first match */
				if (szval > 0) {
					strcpy(strbuf + szval, sep);
					szval += sepsz;
				}
				strcpy(strbuf + szval, affstr[i]);
				szval += strlen(affstr[i]);
			}
		}
	}
	if (val || had_error)
		show_strbuf(pname, 0);
	return had_error;
}

int device_info_partition_affinities_ext(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	size_t numval, szval, cursor, slen;
	cl_device_partition_property_ext *val = NULL;

	const char *sep = (output_mode == CLINFO_HUMAN ? comma_str : vbar_str);
	size_t sepsz = (output_mode == CLINFO_HUMAN ? 2 : 3);
	const char * const *ptstr = (output_mode == CLINFO_HUMAN ?
		affinity_domain_str : affinity_domain_raw_ext_str);

	GET_VAL_ARRAY;

	szval = 0;
	if (!had_error) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			if (szval > 0) {
				strcpy(strbuf + szval, sep);
				szval += sepsz;
			}

			switch (val[cursor]) {
			case CL_AFFINITY_DOMAIN_NUMA_EXT: str_idx = 0; break;
			case CL_AFFINITY_DOMAIN_L4_CACHE_EXT: str_idx = 1; break;
			case CL_AFFINITY_DOMAIN_L3_CACHE_EXT: str_idx = 2; break;
			case CL_AFFINITY_DOMAIN_L2_CACHE_EXT: str_idx = 3; break;
			case CL_AFFINITY_DOMAIN_L1_CACHE_EXT: str_idx = 4; break;
			case CL_AFFINITY_DOMAIN_NEXT_FISSIONABLE_EXT: str_idx = 5; break;
			default:
				szval += sprintf(strbuf + szval, "<unknown> (0x%" PRIX64 ")", val[cursor]);
				break;
			}
			if (str_idx >= 0) {
				/* string length */
				slen = strlen(ptstr[str_idx]);
				strncpy(strbuf + szval, ptstr[str_idx], slen);
				szval += slen;
			}
		}
		strbuf[szval] = '\0';
	}

	show_strbuf(pname, 0);

	free(val);
	return had_error;
}

/* Preferred / native vector widths */
int device_info_vecwidth(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_uint preferred, val;
	GET_VAL;
	if (!had_error) {
		preferred = val;

		/* we get called with PREFERRED, NATIVE is at +0x30 offset, except for HALF,
		 * which is at +0x08 */
		param += (param == CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF ? 0x08 : 0x30);
		/* TODO update current_param */
		GET_VAL;

		if (!had_error) {
			size_t szval = 0;
			const char *ext = (param == CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF ?
				chk->has_half : (param == CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE ?
				chk->has_double : NULL));
			szval = sprintf(strbuf, "%8u / %-8u", preferred, val);
			if (ext)
				sprintf(strbuf + szval, " (%s)", *ext ? ext : na);
		}
	}
	show_strbuf(pname, 0);
	return had_error;
}

/* Floating-point configurations */
int device_info_fpconf(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_device_fp_config val;
	int get_it = (
		(param == CL_DEVICE_SINGLE_FP_CONFIG) ||
		(param == CL_DEVICE_HALF_FP_CONFIG && dev_has_half(chk)) ||
		(param == CL_DEVICE_DOUBLE_FP_CONFIG && dev_has_double(chk)));
	if (get_it)
		GET_VAL;
	else
		had_error = CL_SUCCESS;

	if (!had_error) {
		size_t szval = 0;
		cl_uint i = 0;
		const char *sep = vbar_str;
		const char * const *fpstr = (output_mode == CLINFO_HUMAN ?
			fp_conf_str : fp_conf_raw_str);
		if (output_mode == CLINFO_HUMAN) {
			const char *why;
			switch (param) {
			case CL_DEVICE_HALF_FP_CONFIG:
				why = get_it ? chk->has_half : na;
				break;
			case CL_DEVICE_SINGLE_FP_CONFIG:
				why = core;
				break;
			case CL_DEVICE_DOUBLE_FP_CONFIG:
				why = get_it ? chk->has_double : na;
			}
			/* show 'why' it's being shown */
			szval += sprintf(strbuf, "(%s)", why);
		}
		if (get_it) {
			for (i = 0; i < fp_conf_count; ++i) {
				cl_device_fp_config cur = (cl_device_fp_config)(1) << i;
				if (output_mode == CLINFO_HUMAN) {
					szval += sprintf(strbuf + szval, "\n" I2_STR "%s",
						fpstr[i], bool_str[!!(val & cur)]);
				} else if (val & cur) {
					if (szval > 0)
						szval += sprintf(strbuf + szval, sep);
					szval += sprintf(strbuf + szval, fpstr[i]);
				}
			}
		}
	}

	/* only print this for HUMAN output or if we actually got the value */
	if (output_mode == CLINFO_HUMAN || get_it)
		show_strbuf(pname, 0);
	return had_error;
}

/* Arch bits and endianness (HUMAN) */
int device_info_arch(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_uint bits;
	{
		cl_uint val;
		GET_VAL;
		if (!had_error)
			bits = val;
	}
	if (!had_error) {
		cl_bool val;
		param = CL_DEVICE_ENDIAN_LITTLE;
		current_param = "CL_DEVICE_ENDIAN_LITTLE";
		GET_VAL;
		if (!had_error)
			sprintf(strbuf, "%u, %s", bits, endian_str[val]);
	}
	show_strbuf(pname, 0);
	return had_error;
}

/* SVM capabilities */
int device_info_svm_cap(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_device_svm_capabilities val;
	int is_20 = dev_is_20(chk);
	int has_svm_ext = dev_has_svm_ext(chk);

	GET_VAL;

	if (!had_error) {
		size_t szval = 0;
		cl_uint i = 0;
		const char *sep = vbar_str;
		const char * const *scstr = (output_mode == CLINFO_HUMAN ?
			svm_cap_str : svm_cap_raw_str);
		if (output_mode == CLINFO_HUMAN) {
			/* show 'why' it's being shown */
			szval += sprintf(strbuf, "(%s%s%s)",
				(is_20 ? core : empty_str),
				(is_20 && has_svm_ext ? comma_str : empty_str),
				chk->has_svm_ext);
		}
		for (i = 0; i < svm_cap_count; ++i) {
			cl_device_svm_capabilities cur = (cl_device_svm_capabilities)(1) << i;
			if (output_mode == CLINFO_HUMAN) {
				szval += sprintf(strbuf + szval, "\n" I2_STR "%s",
					scstr[i], bool_str[!!(val & cur)]);
			} else if (val & cur) {
				if (szval > 0)
					szval += sprintf(strbuf + szval, sep);
				szval += sprintf(strbuf + szval, scstr[i]);
			}
		}
	}

	show_strbuf(pname, 0);
	return had_error;
}

/*
 * Device info traits
 */

/* A CL_FALSE param means "just print pname" */

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

	/* Device partition support: summary is only presented in HUMAN case */
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PARTITION_MAX_SUB_DEVICES, "Device Partition", partition_header), dev_has_partition },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PARTITION_MAX_SUB_DEVICES, INDENT "Max number of sub-devices", int), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PARTITION_PROPERTIES, INDENT "Supported partition types", partition_types), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PARTITION_AFFINITY_DOMAIN, INDENT "Supported affinity domains", partition_affinities), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PARTITION_TYPES_EXT, INDENT "Supported partition types (ext)", partition_types_ext), dev_has_fission },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AFFINITY_DOMAINS_EXT, INDENT "Supported affinity domains (ext)", partition_affinities_ext), dev_has_fission },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, "Max work item dimensions", int), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_WORK_ITEM_SIZES, "Max work item sizes", szptr), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_WORK_GROUP_SIZE, "Max work group size", sz), NULL },
	{ CLINFO_BOTH, DINFO(CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, "Preferred work group size multiple", wg), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_WARP_SIZE_NV, "Warp size (NV)", int), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_WAVEFRONT_WIDTH_AMD, "Wavefront width (AMD)", int), dev_is_gpu_amd },

	/* Preferred/native vector widths: header is only presented in HUMAN case, that also pairs
	 * PREFERRED and NATIVE in a single line */
#define DINFO_VECWIDTH(Type, type) \
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PREFERRED_VECTOR_WIDTH_##Type, INDENT #type, vecwidth), NULL }, \
	{ CLINFO_RAW, DINFO(CL_DEVICE_PREFERRED_VECTOR_WIDTH_##Type, INDENT #type, int), NULL }, \
	{ CLINFO_RAW, DINFO(CL_DEVICE_NATIVE_VECTOR_WIDTH_##Type, INDENT #type, int), NULL }

	{ CLINFO_HUMAN, DINFO(CL_FALSE, "Preferred / native vector sizes", str), NULL },
	DINFO_VECWIDTH(CHAR, char),
	DINFO_VECWIDTH(SHORT, short),
	DINFO_VECWIDTH(INT, int),
	DINFO_VECWIDTH(LONG, long),
	DINFO_VECWIDTH(HALF, half),
	DINFO_VECWIDTH(FLOAT, float),
	DINFO_VECWIDTH(DOUBLE, double),

	/* Floating point configurations */
#define DINFO_FPCONF(Type, type, cond) \
	{ CLINFO_BOTH, DINFO(CL_DEVICE_##Type##_FP_CONFIG, #type "-precision Floating-point support", fpconf), NULL }

	DINFO_FPCONF(HALF, Half, dev_has_half),
	DINFO_FPCONF(SINGLE, Single, NULL),
	DINFO_FPCONF(DOUBLE, Double, dev_has_double),

	/* Address bits and endianness are written together for HUMAN, separate for RAW */
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_ADDRESS_BITS, "Address bits", arch), NULL },
	{ CLINFO_RAW, DINFO(CL_DEVICE_ADDRESS_BITS, "Address bits", int), NULL },
	{ CLINFO_RAW, DINFO(CL_DEVICE_ENDIAN_LITTLE, "Little Endian", bool), NULL },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_SIZE, "Global memory size", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_FREE_MEMORY_AMD, "Global free memory (AMD)", free_mem_amd), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_CHANNELS_AMD, "Global memory channels (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_CHANNEL_BANKS_AMD, "Global memory banks per channel (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_CHANNEL_BANK_WIDTH_AMD, "Global memory bank width (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ERROR_CORRECTION_SUPPORT, "Error Correction support", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_MEM_ALLOC_SIZE, "Max memory allocation", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_HOST_UNIFIED_MEMORY, "Unified memory for Host and Device", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_INTEGRATED_MEMORY_NV, "Integrated memory (NV)", bool), dev_has_nv },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_SVM_CAPABILITIES, "Shared Virtual Memory (SVM) capabilities", svm_cap), dev_has_svm },
};

void
printDeviceInfo(cl_uint d)
{
	cl_device_id dev = device[d];
	cl_device_local_mem_type lmemtype;
	cl_device_mem_cache_type cachetype;
	cl_device_exec_capabilities execap;

	cl_command_queue_properties queueprop;

	cl_uint uintval;
	cl_ulong ulongval;
	double doubleval;
	cl_bool boolval;
	size_t szval;
	size_t *szvals = calloc(3, sizeof(size_t));
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
		const char *pname = (output_mode == CLINFO_HUMAN ?
			traits->pname : traits->sname);

		current_param = traits->sname;

		/* skip if it's not for this output mode */
		if (!(output_mode & traits->output_mode))
			continue;

		if (traits->check_func && !traits->check_func(&chk))
			continue;

		cur_sfx = traits->sfx ? traits->sfx : empty_str;

		/* Handle headers */
		if (traits->param == CL_FALSE) {
			strbuf[0] = '\0';
			show_strbuf(pname, 0);
			had_error = CL_FALSE;
			continue;
		}

		had_error = traits->show_func(dev, traits->param,
			pname, &chk);

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
	return 0;
}

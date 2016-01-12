/* Collect all available information on all available devices
 * on all available OpenCL platforms present in the system
 */

#include <time.h>
#include <string.h>
#include <dlfcn.h>

#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void*)0)
#endif

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
#define UNUSED __attribute__((unused))

struct platform_data {
	char *pname; /* CL_PLATFORM_NAME */
	char *sname; /* CL_PLATFORM_ICD_SUFFIX_KHR or surrogate */
	cl_uint ndevs; /* number of devices */
	cl_bool has_amd_offline; /* has cl_amd_offline_devices extension */
};

struct platform_info_checks {
	int has_khr_icd;
	cl_uint plat_version;
};

cl_uint num_platforms;
cl_platform_id *platform;
/* highest version exposed by any platform: if the OpenCL library (the ICD loader)
 * has a lower version, problems may arise (such as API calls causing segfaults
 * or any other unexpected behavior
 */
cl_uint max_plat_version;
/* auto-detected OpenCL version support for the ICD loader */
cl_uint icdl_ocl_version_found = 10;
/* OpenCL version support declared by the ICD loader */
cl_uint icdl_ocl_version;

struct platform_data *pdata;
/* maximum length of a platform's sname */
size_t platform_sname_maxlen;
/* maximum number of devices */
cl_uint maxdevs;
/* line prefix, used to identify the platform/device for each
 * device property in RAW output mode */
char *line_pfx;
int line_pfx_len;

cl_uint num_devs_all;

cl_device_id *all_devices;

enum output_modes {
	CLINFO_HUMAN = 1, /* more human readable */
	CLINFO_RAW = 2, /* property-by-property */
	CLINFO_BOTH = CLINFO_HUMAN | CLINFO_RAW
};

enum output_modes output_mode = CLINFO_HUMAN;

/* Specify if we should only be listing the platform and devices;
 * can be done in both human and raw mode, and only the platform
 * and device names (and number) will be shown
 * TODO check if terminal supports UTF-8 and use Unicode line-drawing
 * for the tree in list mode
 */
cl_bool list_only = CL_FALSE;

static const char unk[] = "Unknown";
static const char none[] = "None";
static const char none_raw[] = "CL_NONE";
static const char na[] = "n/a"; // not available
static const char core[] = "core"; // not available

static const char bytes_str[] = " bytes";
static const char pixels_str[] = " pixels";
static const char images_str[] = " images";

static const char* bool_str[] = { "No", "Yes" };
static const char* bool_raw_str[] = { "CL_FALSE", "CL_TRUE" };

static const char* endian_str[] = { "Big-Endian", "Little-Endian" };

static const cl_device_type devtype[] = { 0,
	CL_DEVICE_TYPE_DEFAULT, CL_DEVICE_TYPE_CPU, CL_DEVICE_TYPE_GPU,
	CL_DEVICE_TYPE_ACCELERATOR, CL_DEVICE_TYPE_CUSTOM, CL_DEVICE_TYPE_ALL };

const size_t devtype_count = ARRAY_SIZE(devtype);

static const char* device_type_str[] = { unk, "Default", "CPU", "GPU", "Accelerator", "Custom", "All" };
static const char* device_type_raw_str[] = { unk,
	"CL_DEVICE_TYPE_DEFAULT", "CL_DEVICE_TYPE_CPU", "CL_DEVICE_TYPE_GPU",
	"CL_DEVICE_TYPE_ACCELERATOR", "CL_DEVICE_TYPE_CUSTOM", "CL_DEVICE_TYPE_ALL"
};

static const char* partition_type_str[] = {
	"none specified", none, "equally", "by counts", "by affinity domain", "by names (Intel)"
};
static const char* partition_type_raw_str[] = {
	"NONE SPECIFIED",
	none_raw,
	"CL_DEVICE_PARTITION_EQUALLY_EXT",
	"CL_DEVICE_PARTITION_BY_COUNTS_EXT",
	"CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT",
	"CL_DEVICE_PARTITION_BY_NAMES_INTEL_EXT"
};

static const char numa[] = "NUMA";
static const char l1cache[] = "L1 cache";
static const char l2cache[] = "L2 cache";
static const char l3cache[] = "L3 cache";
static const char l4cache[] = "L4 cache";

static const char* affinity_domain_str[] = {
	numa, l4cache, l3cache, l2cache, l1cache, "next partitionable"
};

static const char* affinity_domain_ext_str[] = {
	numa, l4cache, l3cache, l2cache, l1cache, "next fissionable"
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

static const char* lmem_type_str[] = { none, "Local", "Global" };
static const char* lmem_type_raw_str[] = { none_raw, "CL_LOCAL", "CL_GLOBAL" };
static const char* cache_type_str[] = { none, "Read-Only", "Read/Write" };
static const char* cache_type_raw_str[] = { none_raw, "CL_READ_ONLY_CACHE", "CL_READ_WRITE_CACHE" };

static const char* queue_prop_str[] = { "Out-of-order execution", "Profiling" };
static const char* queue_prop_raw_str[] = {
	"CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE",
	"CL_QUEUE_PROFILING_ENABLE"
};

const size_t queue_prop_count = ARRAY_SIZE(queue_prop_str);

static const char* execap_str[] = { "Run OpenCL kernels", "Run native kernels" };
static const char* execap_raw_str[] = {
	"CL_EXEC_KERNEL",
	"CL_EXEC_NATIVE_KERNEL"
};

const size_t execap_count = ARRAY_SIZE(execap_str);

static const char* sources[] = {
	"#define GWO(type) global type* restrict\n",
	"#define GRO(type) global const type* restrict\n",
	"#define BODY int i = get_global_id(0); out[i] = in1[i] + in2[i]\n",
	"#define _KRN(T, N) void kernel sum##N(GWO(T##N) out, GRO(T##N) in1, GRO(T##N) in2) { BODY; }\n",
	"#define KRN(N) _KRN(float, N)\n",
	"KRN()\n/* KRN(2)\nKRN(4)\nKRN(8)\nKRN(16) */\n",
};

const char *no_plat(void)
{
	return output_mode == CLINFO_HUMAN ?
		"No platform" :
		"CL_INVALID_PLATFORM";
}

const char *no_dev(void)
{
	return output_mode == CLINFO_HUMAN ?
		"No devices found in platform" :
		"CL_DEVICE_NOT_FOUND";
}

const char *no_dev_avail(void)
{
	return output_mode == CLINFO_HUMAN ?
		"No devices available in platform" :
		"CL_DEVICE_NOT_AVAILABLE";
}


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

int had_error = 0;
const char *cur_sfx = empty_str;

/* parse a CL_DEVICE_VERSION or CL_PLATFORM_VERSION info to determine the OpenCL version.
 * Returns an unsigned integer in the form major*10 + minor
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


/* print strbuf, prefixed by pname, skipping leading whitespace if skip is nonzero,
 * affixing cur_sfx */
static inline
void show_strbuf(const char *pname, int skip)
{
	printf("%s" I1_STR "%s%s\n",
		line_pfx, pname,
		(skip ? skip_leading_ws(strbuf) : strbuf),
		had_error ? empty_str : cur_sfx);
}

int
platform_info_str(cl_platform_id pid, cl_platform_info param, const char* pname, const struct platform_info_checks * chk UNUSED)
{
	error = clGetPlatformInfo(pid, param, 0, NULL, &nusz);
	if (nusz > bufsz) {
		REALLOC(strbuf, nusz, current_param);
		bufsz = nusz;
	}
	had_error = REPORT_ERROR2("get %s size");
	if (!had_error) {
		error = clGetPlatformInfo(pid, param, bufsz, strbuf, NULL);
		had_error = REPORT_ERROR2("get %s");
	}
	/* when only listing, do not print anything we're just gathering
	 * information
	 */
	if (!list_only)
		show_strbuf(pname, 1);
	return had_error;
}

int
platform_info_ulong(cl_platform_id pid, cl_platform_info param, const char* pname, const struct platform_info_checks * chk UNUSED)
{
	cl_ulong val = 0;

	error = clGetPlatformInfo(pid, param, sizeof(val), &val, NULL);
	had_error = REPORT_ERROR2("get %s");
	/* when only listing, do not print anything we're just gathering
	 * information
	 */
	if (!list_only) {
		if (had_error)
			show_strbuf(pname, 0);
		else
			printf("%s" I1_STR "%" PRIu64 "%s\n", line_pfx, pname, val, cur_sfx);
	}
	return had_error;
}

struct platform_info_traits {
	cl_platform_info param; // CL_PLATFORM_*
	const char *sname; // "CL_PLATFORM_*"
	const char *pname; // "Platform *"
	const char *sfx; // suffix for the output in non-raw mode
	/* pointer to function that shows the parameter */
	int (*show_func)(cl_platform_id pid, cl_platform_info param, const char *pname, const struct platform_info_checks *);
	/* pointer to function that checks if the parameter should be checked */
	int (*check_func)(const struct platform_info_checks *);
};

int khr_icd_p(const struct platform_info_checks *chk)
{
	return chk->has_khr_icd;
}

int plat_is_21(const struct platform_info_checks *chk)
{
	return !(chk->plat_version < 21);
}

#define PINFO_COND(symbol, name, sfx, typ, funcptr) { symbol, #symbol, "Platform " name, sfx, &platform_info_##typ, &funcptr }
#define PINFO(symbol, name, sfx, typ) { symbol, #symbol, "Platform " name, sfx, &platform_info_##typ, NULL }
struct platform_info_traits pinfo_traits[] = {
	PINFO(CL_PLATFORM_NAME, "Name", NULL, str),
	PINFO(CL_PLATFORM_VENDOR, "Vendor", NULL, str),
	PINFO(CL_PLATFORM_VERSION, "Version", NULL, str),
	PINFO(CL_PLATFORM_PROFILE, "Profile", NULL, str),
	PINFO(CL_PLATFORM_EXTENSIONS, "Extensions", NULL, str),
	PINFO_COND(CL_PLATFORM_HOST_TIMER_RESOLUTION, "Host timer resolution", "ns", ulong, plat_is_21),
	PINFO_COND(CL_PLATFORM_ICD_SUFFIX_KHR, "Extensions function suffix", NULL, str, khr_icd_p)
};

/* Print platform info and prepare arrays for device info */
void
printPlatformInfo(cl_uint p)
{
	cl_platform_id pid = platform[p];
	size_t len = 0;

	struct platform_info_checks pinfo_checks = { 0, 10 };

	current_function = __func__;

	for (current_line = 0; current_line < ARRAY_SIZE(pinfo_traits); ++current_line) {
		const struct platform_info_traits *traits = pinfo_traits + current_line;
		const char *pname = (output_mode == CLINFO_HUMAN ?
			traits->pname : traits->sname);

		current_param = traits->sname;

		if (traits->check_func && !traits->check_func(&pinfo_checks))
			continue;

		cur_sfx = (output_mode == CLINFO_HUMAN && traits->sfx) ? traits->sfx : empty_str;

		had_error = traits->show_func(pid, traits->param,
			pname, &pinfo_checks);

		if (had_error)
			continue;

		/* post-processing */

		switch (traits->param) {
		case CL_PLATFORM_NAME:
			/* Store name for future reference */
			len = strlen(strbuf);
			ALLOC(pdata[p].pname, len+1, "platform name copy");
			/* memcpy instead of strncpy since we already have the len
			 * and memcpy is possibly more optimized */
			memcpy(pdata[p].pname, strbuf, len);
			pdata[p].pname[len] = '\0';
			break;
		case CL_PLATFORM_VERSION:
			/* compute numeric value for OpenCL version */
			pinfo_checks.plat_version = getOpenCLVersion(strbuf + 7);
			break;
		case CL_PLATFORM_EXTENSIONS:
			pinfo_checks.has_khr_icd = !!strstr(strbuf, "cl_khr_icd");
			pdata[p].has_amd_offline = !!strstr(strbuf, "cl_amd_offline_devices");
			break;
		case CL_PLATFORM_ICD_SUFFIX_KHR:
			/* Store ICD suffix for future reference */
			len = strlen(strbuf);
			ALLOC(pdata[p].sname, len+1, "platform ICD suffix copy");
			/* memcpy instead of strncpy since we already have the len
			 * and memcpy is possibly more optimized */
			memcpy(pdata[p].sname, strbuf, len);
			pdata[p].sname[len] = '\0';
		default:
			/* do nothing */
			break;
		}

	}

	if (pinfo_checks.plat_version > max_plat_version)
		max_plat_version = pinfo_checks.plat_version;

	/* if no CL_PLATFORM_ICD_SUFFIX_KHR, use P### as short/symbolic name */
	if (!pdata[p].sname) {
#define SNAME_MAX 32
		ALLOC(pdata[p].sname, SNAME_MAX, "platform symbolic name");
		snprintf(pdata[p].sname, SNAME_MAX, "P%u", p);
	}

	len = strlen(pdata[p].sname);
	if (len > platform_sname_maxlen)
		platform_sname_maxlen = len;

	error = clGetDeviceIDs(pid, CL_DEVICE_TYPE_ALL, 0, NULL, &(pdata[p].ndevs));
	if (error == CL_DEVICE_NOT_FOUND)
		pdata[p].ndevs = 0;
	else
		CHECK_ERROR("number of devices");

	num_devs_all += pdata[p].ndevs;

	if (pdata[p].ndevs > maxdevs)
		maxdevs = pdata[p].ndevs;
}

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
	cl_context ctx = NULL;
	cl_program prg = NULL;
	cl_kernel krn = NULL;

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
		krn = NULL;
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

/*
 * Device properties/extensions used in traits checks, and relevant functions
 */

struct device_info_checks {
	cl_device_type devtype;
	cl_device_mem_cache_type cachetype;
	cl_device_local_mem_type lmemtype;
	cl_bool image_support;
	char has_half[12];
	char has_double[24];
	char has_nv[29];
	char has_amd[30];
	char has_svm_ext[11];
	char has_fission[22];
	char has_atomic_counters[26];
	char has_image2d_buffer[27];
	char has_intel_local_thread[30];
	char has_intel_AME[36];
	char has_altera_dev_temp[29];
	char has_spir[12];
	char has_qcom_ext_host_ptr[21];
	char has_simultaneous_sharing[30];
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
DEFINE_EXT_CHECK(intel_AME)
DEFINE_EXT_CHECK(altera_dev_temp)
DEFINE_EXT_CHECK(spir)
DEFINE_EXT_CHECK(qcom_ext_host_ptr)
DEFINE_EXT_CHECK(simultaneous_sharing)

/* In the version checks we negate the opposite conditions
 * instead of double-negating the actual condition
 */

// device supports 1.2
int dev_is_12(const struct device_info_checks *chk)
{
	return !(chk->dev_version < 12);
}

// device supports 2.0
int dev_is_20(const struct device_info_checks *chk)
{
	return !(chk->dev_version < 20);
}

// device supports 2.1
int dev_is_21(const struct device_info_checks *chk)
{
	return !(chk->dev_version < 21);
}

// device does not support 2.0
int dev_not_20(const struct device_info_checks *chk)
{
	return !(chk->dev_version >= 20);
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

int dev_has_cache(const struct device_info_checks *chk)
{
	return chk->cachetype != CL_NONE;
}

int dev_has_lmem(const struct device_info_checks *chk)
{
	return chk->lmemtype != CL_NONE;
}

int dev_has_images(const struct device_info_checks *chk)
{
	return chk->image_support;
}

int dev_has_images_12(const struct device_info_checks *chk)
{
	return dev_has_images(chk) && dev_is_12(chk);
}

int dev_has_images_20(const struct device_info_checks *chk)
{
	return dev_has_images(chk) && dev_is_20(chk);
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
	CHECK_EXT(intel_AME, cl_intel_advanced_motion_estimation);
	CHECK_EXT(altera_dev_temp, cl_altera_device_temperature);
	CHECK_EXT(qcom_ext_host_ptr, cl_qcom_ext_host_ptr);
	CHECK_EXT(simultaneous_sharing, cl_intel_simultaneous_sharing);
}



/*
 * Device info print functions
 */

#define _GET_VAL \
	error = clGetDeviceInfo(dev, param, sizeof(val), &val, NULL); \
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
		printf("%s" I1_STR fmt "%s\n", line_pfx, pname, val, cur_sfx);

#define FMT_VAL(fmt) do { \
	_FMT_VAL(fmt) \
} while (0)

#define SHOW_VAL(fmt) do { \
	_GET_VAL \
	_FMT_VAL(fmt) \
} while (0)

#define DEFINE_DEVINFO_SHOW(how, type, fmt) \
int device_info_##how(cl_device_id dev, cl_device_info param, const char *pname, \
	const struct device_info_checks *chk UNUSED) \
{ \
	type val = 0; \
	SHOW_VAL(fmt); \
	return had_error; \
}

/* Get string-type info without showing it */
int device_info_str_get(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	current_param = pname;
	error = clGetDeviceInfo(dev, param, 0, NULL, &nusz);
	if (nusz > bufsz) {
		REALLOC(strbuf, nusz, current_param);
		bufsz = nusz;
	}
	had_error = REPORT_ERROR2("get %s size");
	if (!had_error) {
		error = clGetDeviceInfo(dev, param, bufsz, strbuf, NULL);
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
DEFINE_DEVINFO_SHOW(long, cl_ulong, "%" PRIu64)
DEFINE_DEVINFO_SHOW(sz, size_t, "%" PRIuS)

int device_info_bool(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_bool val = 0;
	const char * const * str = (output_mode == CLINFO_HUMAN ?
		bool_str : bool_raw_str);
	GET_VAL;
	if (had_error)
		show_strbuf(pname, 0);
	else {
		printf("%s" I1_STR "%s%s\n", line_pfx, pname, str[val], cur_sfx);
		/* abuse strbuf to pass the bool value up to the caller,
		 * this is used e.g. by CL_DEVICE_IMAGE_SUPPORT
		 */
		memcpy(strbuf, &val, sizeof(val));
	}
	return had_error;
}

int device_info_bits(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_uint val;
	GET_VAL;
	if (!had_error)
		sprintf(strbuf, "%u bits (%u bytes)", val, val/8);
	show_strbuf(pname, 0);
	return had_error;
}


size_t strbuf_mem(cl_ulong val, size_t szval)
{
	double dbl = val;
	size_t sfx = 0;
	while (dbl > 1024 && sfx < memsfx_count) {
		dbl /= 1024;
		++sfx;
	}
	return sprintf(strbuf + szval, " (%.4lg%s)",
		dbl, memsfx[sfx]);
}

int device_info_mem(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_ulong val = 0;
	size_t szval = 0;
	GET_VAL;
	if (!had_error) {
		szval += sprintf(strbuf, "%" PRIu64, val);
		if (output_mode == CLINFO_HUMAN && val > 1024)
			strbuf_mem(val, szval);
	}
	show_strbuf(pname, 0);
	return had_error;
}

int device_info_mem_int(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_uint val = 0;
	size_t szval = 0;
	GET_VAL;
	if (!had_error) {
		szval += sprintf(strbuf, "%u", val);
		if (output_mode == CLINFO_HUMAN && val > 1024)
			strbuf_mem(val, szval);
	}
	show_strbuf(pname, 0);
	return had_error;
}

int device_info_free_mem_amd(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	size_t *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY;
	if (!had_error) {
		size_t cursor = 0;
		szval = 0;
		for (cursor = 0; cursor < numval; ++cursor) {
			if (szval > 0) {
				strbuf[szval] = ' ';
				++szval;
			}
			szval += sprintf(strbuf + szval, "%" PRIuS, val[cursor]);
			if (output_mode == CLINFO_HUMAN)
				szval += strbuf_mem(val[cursor]*UINT64_C(1024), szval);
		}
	}
	show_strbuf(pname, 0);
	free(val);
	return had_error;
}

int device_info_time_offset(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_ulong val = 0;
	GET_VAL;
	if (!had_error) {
		size_t szval = 0;
		time_t time = val/UINT64_C(1000000000);
		szval += snprintf(strbuf, bufsz, "%" PRIu64 "ns (", val);
		szval += bufcpy(szval, ctime(&time));
		/* overwrite ctime's newline with the closing parenthesis */
		if (szval < bufsz)
			strbuf[szval - 1] = ')';
	}
	show_strbuf(pname, 0);
	return had_error;
}

int device_info_szptr(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	size_t *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY;
	if (!had_error) {
		size_t counter = 0;
		set_separator(output_mode == CLINFO_HUMAN ? times_str : spc_str);
		szval = 0;
		for (counter = 0; counter < numval; ++counter) {
			add_separator(&szval);
			szval += snprintf(strbuf + szval, bufsz - szval - 1, "%" PRIuS, val[counter]);
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

int device_info_wg(cl_device_id dev, cl_device_info param UNUSED, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_platform_id val = NULL;
	{
		/* shadow */
		cl_device_info param = CL_DEVICE_PLATFORM;
		current_param = "CL_DEVICE_PLATFORM";
		GET_VAL;
	}
	current_param = pname;
	if (!had_error)
		had_error = getWGsizes(val, dev);
	if (!had_error) {
		sprintf(strbuf, "%" PRIuS, wgm[0]);
	}
	show_strbuf(pname, 0);
	return had_error;
}

int device_info_img_sz_2d(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	size_t width = 0, height = 0, val = 0;
	GET_VAL; /* HEIGHT */
	if (!had_error) {
		height = val;
		param = CL_DEVICE_IMAGE2D_MAX_WIDTH;
		current_param = "CL_DEVICE_IMAGE2D_MAX_WIDTH";
		GET_VAL;
		if (!had_error) {
			width = val;
			sprintf(strbuf, "%" PRIuS "x%" PRIuS, width, height);
		}
	}
	show_strbuf(pname, 0);
	return had_error;
}

int device_info_img_sz_3d(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	size_t width = 0, height = 0, depth = 0, val = 0;
	GET_VAL; /* HEIGHT */
	if (!had_error) {
		height = val;
		param = CL_DEVICE_IMAGE3D_MAX_WIDTH;
		current_param = "CL_DEVICE_IMAGE3D_MAX_WIDTH";
		GET_VAL;
		if (!had_error) {
			width = val;
			param = CL_DEVICE_IMAGE3D_MAX_DEPTH;
			current_param = "CL_DEVICE_IMAGE3D_MAX_DEPTH";
			GET_VAL;
			if (!had_error) {
				depth = val;
				sprintf(strbuf, "%" PRIuS "x%" PRIuS "x%" PRIuS,
					width, height, depth);
			}
		}
	}
	show_strbuf(pname, 0);
	return had_error;
}


int device_info_devtype(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_device_type val = 0;
	GET_VAL;
	if (!had_error) {
		/* iterate over device type strings, appending their textual form
		 * to strbuf.
		 * TODO: check for extra bits/no bits
		 */
		cl_uint i = devtype_count - 1; /* skip CL_DEVICE_TYPE_ALL */
		const char * const *devstr = (output_mode == CLINFO_HUMAN ?
			device_type_str : device_type_raw_str);
		size_t szval = 0;
		strbuf[szval] = '\0';
		set_separator(output_mode == CLINFO_HUMAN ? comma_str : vbar_str);
		for (; i > 0; --i) {
			/* assemble CL_DEVICE_TYPE_* from index i */
			cl_device_type cur = (cl_device_type)(1) << (i-1);
			if (val & cur) {
				/* match: add separator if not first match */
				add_separator(&szval);
				szval += bufcpy(szval, devstr[i]);
			}
		}
	}
	show_strbuf(pname, 0);
	/* we abuse global strbuf to pass the device type over to the caller */
	if (!had_error)
		memcpy(strbuf, &val, sizeof(val));
	return had_error;
}

int device_info_cachetype(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_device_mem_cache_type val = 0;
	GET_VAL;
	if (!had_error) {
		const char * const *ar = (output_mode == CLINFO_HUMAN ?
			cache_type_str : cache_type_raw_str);
		bufcpy(0, ar[val]);
	}
	show_strbuf(pname, 0);
	/* we abuse global strbuf to pass the cache type over to the caller */
	if (!had_error)
		memcpy(strbuf, &val, sizeof(val));
	return had_error;
}

int device_info_lmemtype(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_device_local_mem_type val = 0;
	GET_VAL;
	if (!had_error) {
		const char * const *ar = (output_mode == CLINFO_HUMAN ?
			lmem_type_str : lmem_type_raw_str);
		bufcpy(0, ar[val]);
	}
	show_strbuf(pname, 0);
	/* we abuse global strbuf to pass the lmem type over to the caller */
	if (!had_error)
		memcpy(strbuf, &val, sizeof(val));
	return had_error;
}

/* stringify a cl_device_topology_amd */
void devtopo_str(const cl_device_topology_amd *devtopo)
{
	switch (devtopo->raw.type) {
	case 0:
		if (output_mode == CLINFO_HUMAN)
			sprintf(strbuf, "(%s)", na);
		else
			sprintf(strbuf, none_raw);
		break;
	case CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD:
		sprintf(strbuf, "PCI-E, %02x:%02x.%u",
			(cl_uchar)(devtopo->pcie.bus),
			devtopo->pcie.device, devtopo->pcie.function);
		break;
	default:
		sprintf(strbuf, "<unknown (%u): %u %u %u %u %u>",
			devtopo->raw.type,
			devtopo->raw.data[0], devtopo->raw.data[1],
			devtopo->raw.data[2],
			devtopo->raw.data[3], devtopo->raw.data[4]);
	}
}

int device_info_devtopo_amd(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
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
	const struct device_info_checks *chk UNUSED)
{
	cl_device_topology_amd devtopo;
	cl_uint val = 0;

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
	const struct device_info_checks *chk UNUSED)
{
	cl_uint major = 0, val = 0;
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

/* AMD GFXIP */
int device_info_gfxip_amd(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_uint major = 0, val = 0;
	GET_VAL; /* MAJOR */
	if (!had_error) {
		major = val;
		param = CL_DEVICE_GFXIP_MINOR_AMD;
		current_param = "CL_DEVICE_GFXIP_MINOR_AMD";
		GET_VAL;
		if (!had_error)
			snprintf(strbuf, bufsz, "%u.%u", major, val);
	}

	show_strbuf(pname, 0);
	return had_error;
}


/* Device Partition, CLINFO_HUMAN header */
int device_info_partition_header(cl_device_id dev UNUSED, cl_device_info param UNUSED,
	const char *pname, const struct device_info_checks *chk)
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
	const struct device_info_checks *chk UNUSED)
{
	size_t numval = 0, szval = 0, cursor = 0, slen = 0;
	cl_device_partition_property *val = NULL;
	const char * const *ptstr = (output_mode == CLINFO_HUMAN ?
		partition_type_str : partition_type_raw_str);

	set_separator(output_mode == CLINFO_HUMAN ? comma_str : vbar_str);

	GET_VAL_ARRAY;

	szval = 0;
	if (!had_error) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			add_separator(&szval);

			switch (val[cursor]) {
			case 0: str_idx = 1; break;
			case CL_DEVICE_PARTITION_EQUALLY: str_idx = 2; break;
			case CL_DEVICE_PARTITION_BY_COUNTS: str_idx = 3; break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN: str_idx = 4; break;
			case CL_DEVICE_PARTITION_BY_NAMES_INTEL: str_idx = 5; break;
			default:
				szval += snprintf(strbuf + szval, bufsz - szval - 1, "by <unknown> (0x%" PRIXPTR ")", val[cursor]);
				break;
			}
			if (str_idx > 0) {
				/* string length, minus _EXT */
				slen = strlen(ptstr[str_idx]);
				if (output_mode == CLINFO_RAW && str_idx > 1)
					slen -= 4;
				szval += bufcpy_len(szval, ptstr[str_idx], slen);
			}
			if (szval >= bufsz) {
				trunc_strbuf();
				break;
			}
		}
		if (szval == 0) {
			bufcpy(szval, ptstr[0]);
		} else if (szval < bufsz)
			strbuf[szval] = '\0';
	}

	show_strbuf(pname, 0);

	free(val);
	return had_error;
}

int device_info_partition_types_ext(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	size_t numval = 0, szval = 0, cursor = 0, slen = 0;
	cl_device_partition_property_ext *val = NULL;
	const char * const *ptstr = (output_mode == CLINFO_HUMAN ?
		partition_type_str : partition_type_raw_str);

	set_separator(output_mode == CLINFO_HUMAN ? comma_str : vbar_str);

	GET_VAL_ARRAY;

	szval = 0;
	if (!had_error) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			add_separator(&szval);

			switch (val[cursor]) {
			case 0: str_idx = 1; break;
			case CL_DEVICE_PARTITION_EQUALLY_EXT: str_idx = 2; break;
			case CL_DEVICE_PARTITION_BY_COUNTS_EXT: str_idx = 3; break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT: str_idx = 4; break;
			case CL_DEVICE_PARTITION_BY_NAMES_EXT: str_idx = 5; break;
			default:
				szval += snprintf(strbuf + szval, bufsz - szval - 1, "by <unknown> (0x%" PRIX64 ")", val[cursor]);
				break;
			}
			if (str_idx > 0) {
				/* string length */
				slen = strlen(ptstr[str_idx]);
				strncpy(strbuf + szval, ptstr[str_idx], slen);
				szval += slen;
			}
			if (szval >= bufsz) {
				trunc_strbuf();
				break;
			}
		}
		if (szval == 0) {
			slen = strlen(ptstr[0]);
			memcpy(strbuf, ptstr[0], slen);
			szval += slen;
		}
		if (szval < bufsz)
			strbuf[szval] = '\0';
	}

	show_strbuf(pname, 0);

	free(val);
	return had_error;
}


/* Device partition affinity domains */
int device_info_partition_affinities(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
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
		const char * const *affstr = (output_mode == CLINFO_HUMAN ?
			affinity_domain_str : affinity_domain_raw_str);
		set_separator(output_mode == CLINFO_HUMAN ? comma_str : vbar_str);
		for (i = 0; i < affinity_domain_count; ++i) {
			cl_device_affinity_domain cur = (cl_device_affinity_domain)(1) << i;
			if (val & cur) {
				/* match: add separator if not first match */
				add_separator(&szval);
				szval += bufcpy(szval, affstr[i]);
			}
			if (szval >= bufsz)
				break;
		}
	}
	if (val || had_error)
		show_strbuf(pname, 0);
	return had_error;
}

int device_info_partition_affinities_ext(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	size_t numval = 0, szval = 0, cursor = 0, slen = 0;
	cl_device_partition_property_ext *val = NULL;
	const char * const *ptstr = (output_mode == CLINFO_HUMAN ?
		affinity_domain_ext_str : affinity_domain_raw_ext_str);

	set_separator(output_mode == CLINFO_HUMAN ? comma_str : vbar_str);

	GET_VAL_ARRAY;

	szval = 0;
	if (!had_error) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			add_separator(&szval);

			switch (val[cursor]) {
			case CL_AFFINITY_DOMAIN_NUMA_EXT: str_idx = 0; break;
			case CL_AFFINITY_DOMAIN_L4_CACHE_EXT: str_idx = 1; break;
			case CL_AFFINITY_DOMAIN_L3_CACHE_EXT: str_idx = 2; break;
			case CL_AFFINITY_DOMAIN_L2_CACHE_EXT: str_idx = 3; break;
			case CL_AFFINITY_DOMAIN_L1_CACHE_EXT: str_idx = 4; break;
			case CL_AFFINITY_DOMAIN_NEXT_FISSIONABLE_EXT: str_idx = 5; break;
			default:
				szval += snprintf(strbuf + szval, bufsz - szval - 1, "<unknown> (0x%" PRIX64 ")", val[cursor]);
				break;
			}
			if (str_idx >= 0) {
				/* string length */
				const char *str = ptstr[str_idx];
				slen = strlen(str);
				strncpy(strbuf + szval, str, slen);
				szval += slen;
			}
			if (szval >= bufsz) {
				trunc_strbuf();
				break;
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
	cl_uint preferred = 0, val = 0;
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
	cl_device_fp_config val = 0;
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
		const char * const *fpstr = (output_mode == CLINFO_HUMAN ?
			fp_conf_str : fp_conf_raw_str);
		set_separator(vbar_str);
		if (output_mode == CLINFO_HUMAN) {
			const char *why = na;
			switch (param) {
			case CL_DEVICE_HALF_FP_CONFIG:
				if (get_it)
					why = chk->has_half;
				break;
			case CL_DEVICE_SINGLE_FP_CONFIG:
				why = core;
				break;
			case CL_DEVICE_DOUBLE_FP_CONFIG:
				if (get_it)
					why = chk->has_double;
				break;
			default:
				/* "this can't happen" (unless OpenCL starts supporting _other_ floating-point formats, maybe) */
				fprintf(stderr, "unsupported floating-point configuration parameter %s\n", pname);

			}
			/* show 'why' it's being shown */
			szval += sprintf(strbuf, "(%s)", why);
		}
		if (get_it) {
			for (i = 0; i < fp_conf_count; ++i) {
				cl_device_fp_config cur = (cl_device_fp_config)(1) << i;
				if (output_mode == CLINFO_HUMAN) {
					szval += sprintf(strbuf + szval, "\n%s" I2_STR "%s",
						line_pfx, fpstr[i], bool_str[!!(val & cur)]);
				} else if (val & cur) {
					add_separator(&szval);
					szval += bufcpy(szval, fpstr[i]);
				}
			}
		}
	}

	/* only print this for HUMAN output or if we actually got the value */
	if (output_mode == CLINFO_HUMAN || get_it)
		show_strbuf(pname, 0);
	return had_error;
}

/* Queue properties */
int device_info_qprop(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk)
{
	cl_command_queue_properties val = 0;
	GET_VAL;
	if (!had_error) {
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *qpstr = (output_mode == CLINFO_HUMAN ?
			queue_prop_str : queue_prop_raw_str);
		set_separator(vbar_str);
		for (i = 0; i < queue_prop_count; ++i) {
			cl_command_queue_properties cur = (cl_command_queue_properties)(1) << i;
			if (output_mode == CLINFO_HUMAN) {
				szval += sprintf(strbuf + szval, "\n%s" I2_STR "%s",
					line_pfx, qpstr[i], bool_str[!!(val & cur)]);
			} else if (val & cur) {
				add_separator(&szval);
				szval += bufcpy(szval, qpstr[i]);
			}
		}
		if (output_mode == CLINFO_HUMAN && param == CL_DEVICE_QUEUE_PROPERTIES &&
			dev_has_intel_local_thread(chk))
			sprintf(strbuf + szval, "\n%s" I2_STR "%s",
				line_pfx, "Local thread execution (Intel)", bool_str[CL_TRUE]);
	}
	show_strbuf(pname, 0);
	return had_error;
}

/* Execution capbilities */
int device_info_execap(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_device_exec_capabilities val = 0;
	GET_VAL;
	if (!had_error) {
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *qpstr = (output_mode == CLINFO_HUMAN ?
			execap_str : execap_raw_str);
		set_separator(vbar_str);
		for (i = 0; i < execap_count; ++i) {
			cl_device_exec_capabilities cur = (cl_device_exec_capabilities)(1) << i;
			if (output_mode == CLINFO_HUMAN) {
				szval += sprintf(strbuf + szval, "\n%s" I2_STR "%s",
					line_pfx, qpstr[i], bool_str[!!(val & cur)]);
			} else if (val & cur) {
				add_separator(&szval);
				szval += bufcpy(szval, qpstr[i]);
			}
		}
	}
	show_strbuf(pname, 0);
	return had_error;
}

/* Arch bits and endianness (HUMAN) */
int device_info_arch(cl_device_id dev, cl_device_info param, const char *pname,
	const struct device_info_checks *chk UNUSED)
{
	cl_uint bits = 0;
	{
		cl_uint val = 0;
		GET_VAL;
		if (!had_error)
			bits = val;
	}
	if (!had_error) {
		cl_bool val = 0;
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
	cl_device_svm_capabilities val = 0;
	int is_20 = dev_is_20(chk);
	int has_svm_ext = dev_has_svm_ext(chk);

	GET_VAL;

	if (!had_error) {
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *scstr = (output_mode == CLINFO_HUMAN ?
			svm_cap_str : svm_cap_raw_str);
		set_separator(vbar_str);
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
				szval += sprintf(strbuf + szval, "\n%s" I2_STR "%s",
					line_pfx, scstr[i], bool_str[!!(val & cur)]);
			} else if (val & cur) {
				add_separator(&szval);
				szval += bufcpy(szval, scstr[i]);
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
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV, "Compute Capability (NV)", cc_nv), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV, INDENT "Compute Capability Major (NV)", int), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV, INDENT "Compute Capability Minor (NV)", int), dev_has_nv },

	/* GFXIP (AMD) is multipart, so different for HUMAN and RAW */
	/* TODO: find a better human-friendly name than GFXIP; v3 of the cl_amd_device_attribute_query
	 * extension specification calls it “core engine GFXIP”, which honestly is not better than
	 * our name choice. */
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_GFXIP_MAJOR_AMD, "Graphics IP (AMD)", gfxip_amd), dev_is_gpu_amd },
	{ CLINFO_RAW, DINFO(CL_DEVICE_GFXIP_MAJOR_AMD, INDENT "Graphics IP MAJOR (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_RAW, DINFO(CL_DEVICE_GFXIP_MINOR_AMD, INDENT "Graphics IP MINOR (AMD)", int), dev_is_gpu_amd },

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
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_NUM_SUB_GROUPS, "Max sub-groups per work group", int), dev_is_21 },

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

	/* Global memory */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_SIZE, "Global memory size", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_FREE_MEMORY_AMD, "Global free memory (AMD)", free_mem_amd), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_CHANNELS_AMD, "Global memory channels (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_CHANNEL_BANKS_AMD, "Global memory banks per channel (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_GLOBAL_MEM_CHANNEL_BANK_WIDTH_AMD, "Global memory bank width (AMD)", bytes_str, int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ERROR_CORRECTION_SUPPORT, "Error Correction support", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_MEM_ALLOC_SIZE, "Max memory allocation", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_HOST_UNIFIED_MEMORY, "Unified memory for Host and Device", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_INTEGRATED_MEMORY_NV, "Integrated memory (NV)", bool), dev_has_nv },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_SVM_CAPABILITIES, "Shared Virtual Memory (SVM) capabilities", svm_cap), dev_has_svm },

	/* Alignment */
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE, "Minimum alignment for any data type", bytes_str, int), NULL },
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_MEM_BASE_ADDR_ALIGN, "Alignment of base address", bits), NULL },
	{ CLINFO_RAW, DINFO(CL_DEVICE_MEM_BASE_ADDR_ALIGN, "Alignment of base address", int), NULL },

	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_PAGE_SIZE_QCOM, "Page size (QCOM)", bytes_str, sz), dev_has_qcom_ext_host_ptr },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_EXT_MEM_PADDING_IN_BYTES_QCOM, "Externa memory padding (QCOM)", bytes_str, sz), dev_has_qcom_ext_host_ptr },

	/* Atomics alignment, with HUMAN-only header */
	{ CLINFO_HUMAN, DINFO(CL_FALSE, "Preferred alignment for atomics", str), dev_is_20 },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT, INDENT "SVM", bytes_str, int), dev_is_20 },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT, INDENT "Global", bytes_str, int), dev_is_20 },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT, INDENT "Local", bytes_str, int), dev_is_20 },

	/* Global variables. TODO some 1.2 devices respond to this too */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE, "Max size for global variable", mem), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE, "Preferred total size of global vars", mem), dev_is_20 },

	/* Global memory cache */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_CACHE_TYPE, "Global Memory cache type", cachetype), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, "Global Memory cache size", sz), dev_has_cache },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE, "Global Memory cache line", " bytes", int), dev_has_cache },

	/* Image support */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_IMAGE_SUPPORT, "Image support", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_SAMPLERS, INDENT "Max number of samplers per kernel", int), dev_has_images },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_IMAGE_MAX_BUFFER_SIZE, INDENT "Max size for 1D images from buffer", pixels_str, sz), dev_has_images_12 },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_IMAGE_MAX_ARRAY_SIZE, INDENT "Max 1D or 2D image array size", images_str, sz), dev_has_images_12 },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT, INDENT "Base address alignment for 2D image buffers", bytes_str, sz), dev_has_image2d_buffer },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_IMAGE_PITCH_ALIGNMENT, INDENT "Pitch alignment for 2D image buffers", bytes_str, sz), dev_has_image2d_buffer },

	/* Image dimensions are split for RAW, combined for HUMAN */
	{ CLINFO_HUMAN, DINFO_SFX(CL_DEVICE_IMAGE2D_MAX_HEIGHT, INDENT "Max 2D image size",  pixels_str, img_sz_2d), dev_has_images },
	{ CLINFO_RAW, DINFO(CL_DEVICE_IMAGE2D_MAX_HEIGHT, INDENT "Max 2D image height",  sz), dev_has_images },
	{ CLINFO_RAW, DINFO(CL_DEVICE_IMAGE2D_MAX_WIDTH, INDENT "Max 2D image width",  sz), dev_has_images },
	{ CLINFO_HUMAN, DINFO_SFX(CL_DEVICE_IMAGE3D_MAX_HEIGHT, INDENT "Max 3D image size",  pixels_str, img_sz_3d), dev_has_images },
	{ CLINFO_RAW, DINFO(CL_DEVICE_IMAGE3D_MAX_HEIGHT, INDENT "Max 3D image height",  sz), dev_has_images },
	{ CLINFO_RAW, DINFO(CL_DEVICE_IMAGE3D_MAX_WIDTH, INDENT "Max 3D image width",  sz), dev_has_images },
	{ CLINFO_RAW, DINFO(CL_DEVICE_IMAGE3D_MAX_DEPTH, INDENT "Max 3D image depth",  sz), dev_has_images },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_READ_IMAGE_ARGS, INDENT "Max number of read image args", int), dev_has_images },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_WRITE_IMAGE_ARGS, INDENT "Max number of write image args", int), dev_has_images },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS, INDENT "Max number of read/write image args", int), dev_has_images_20 },

	/* Pipes */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_PIPE_ARGS, "Max number of pipe args", int), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS, "Max active pipe reservations", int), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PIPE_MAX_PACKET_SIZE, "Max pipe packet size", mem_int), dev_is_20 },

	/* Local memory */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LOCAL_MEM_TYPE, "Local memory type", lmemtype), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LOCAL_MEM_SIZE, "Local memory size", mem), dev_has_lmem },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LOCAL_MEM_SIZE_PER_COMPUTE_UNIT_AMD, "Local memory syze per CU (AMD)", mem), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LOCAL_MEM_BANKS_AMD, "Local memory banks (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_REGISTERS_PER_BLOCK_NV, "Registers per block (NV)", int), dev_has_nv },

	/* Constant memory */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, "Max constant buffer size", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_CONSTANT_ARGS, "Max number of constant args", int), NULL },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_PARAMETER_SIZE, "Max size of kernel argument", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ATOMIC_COUNTERS_EXT, "Max number of atomic counters", sz), dev_has_atomic_counters },

	/* Queue properties */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_PROPERTIES, "Queue properties", qprop), dev_not_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_PROPERTIES, "Queue properties (on host)", qprop), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES, "Queue properties (on device)", qprop), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE, INDENT "Preferred size", mem), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE, INDENT "Max size", mem), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ON_DEVICE_QUEUES, "Max queues on device", int), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ON_DEVICE_EVENTS, "Max events on device", int), dev_is_20 },

	/* Interop */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PREFERRED_INTEROP_USER_SYNC, "Prefer user sync for interop", bool), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_SIMULTANEOUS_INTEROPS_INTEL, "Number of simulataneous interops (Intel)", int), dev_has_simultaneous_sharing },
	/* TODO: this needs defines for the possible values of the context interops,
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SIMULTANEOUS_INTEROPS_INTEL, "Simulataneous interops", interop_list), dev_has_simultaneous_sharing },
	 */

	/* Profiling resolution */
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_PROFILING_TIMER_RESOLUTION, "Profiling timer resolution", "ns", long), NULL },
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PROFILING_TIMER_OFFSET_AMD, "Profiling timer offset since Epoch (AMD)", time_offset), dev_has_amd },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PROFILING_TIMER_OFFSET_AMD, "Profiling timer offset since Epoch (AMD)", long), dev_has_amd },

	/* Kernel execution capabilities */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_EXECUTION_CAPABILITIES, "Execution capabilities", execap), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SUB_GROUP_INDEPENDENT_FORWARD_PROGRESS, INDENT "Sub-group independent forward progress", bool), dev_is_21 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_THREAD_TRACE_SUPPORTED_AMD, INDENT "Thread trace supported (AMD)", bool), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_KERNEL_EXEC_TIMEOUT_NV, INDENT "Kernel execution timeout (NV)", bool), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GPU_OVERLAP_NV, "Concurrent copy and kernel execution (NV)", bool), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT_NV, INDENT "Number of async copy engines", int), dev_has_nv },
	/* TODO FIXME Current drivers don't seem to respond to this, should probably be queried based on driver version,
	 * or maybe it depends on some other device property?
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVAILABLE_ASYNC_QUEUES_AMD, INDENT "Number of async queues (AMD)", int), dev_is_gpu_amd },
	 */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_IL_VERSION, INDENT "IL version", str), dev_is_21, },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SPIR_VERSIONS, INDENT "SPIR versions", str), dev_has_spir },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PRINTF_BUFFER_SIZE, "printf() buffer size", mem), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_BUILT_IN_KERNELS, "Built-in kernels", str), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ME_VERSION_INTEL, "Motion Estimation accelerator version	(Intel)", int), dev_has_intel_AME },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVAILABLE, "Device Available", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_COMPILER_AVAILABLE, "Compiler Available", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LINKER_AVAILABLE, "Linker Available", bool), dev_is_12 },
};

/* Process all the device info in the traits, except if param_whitelist is not NULL,
 * in which case only those in the whitelist will be processed.
 * If present, the whitelist should be sorted in the order of appearance of the parameters
 * in the traits table, and terminated by the value CL_FALSE
 */

void
printDeviceInfo(const cl_device_id *device, cl_uint d,
	const cl_device_info *param_whitelist) /* list of device info to process, or NULL */
{
	cl_device_id dev = device[d];

	char *extensions = NULL;

	/* pointer to the traits for CL_DEVICE_EXTENSIONS */
	const struct device_info_traits *extensions_traits = NULL;

	struct device_info_checks chk;
	memset(&chk, 0, sizeof(chk));
	chk.dev_version = 10;

	current_function = __func__;

	for (current_line = 0; current_line < ARRAY_SIZE(dinfo_traits); ++current_line) {

		const struct device_info_traits *traits = dinfo_traits + current_line;
		const char *pname = (output_mode == CLINFO_HUMAN ?
			traits->pname : traits->sname);

		current_param = traits->sname;

		/* Whitelist check: finish if done traversing the list,
		 * skip current param if it's not the right one
		 */
		if (param_whitelist) {
			if (*param_whitelist == CL_FALSE)
				break;
			if (traits->param != *param_whitelist)
				continue;
			++param_whitelist;
		}

		/* skip if it's not for this output mode */
		if (!(output_mode & traits->output_mode))
			continue;

		if (traits->check_func && !traits->check_func(&chk))
			continue;

		cur_sfx = (output_mode == CLINFO_HUMAN && traits->sfx) ? traits->sfx : empty_str;

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
			size_t len = strlen(strbuf);
			extensions_traits = traits;
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
		case CL_DEVICE_GLOBAL_MEM_CACHE_TYPE:
			/* strbuf was abused to give us the cache type */
			memcpy(&(chk.cachetype), strbuf, sizeof(chk.cachetype));
			break;
		case CL_DEVICE_LOCAL_MEM_TYPE:
			/* strbuf was abused to give us the lmem type */
			memcpy(&(chk.lmemtype), strbuf, sizeof(chk.lmemtype));
			break;
		case CL_DEVICE_IMAGE_SUPPORT:
			/* strbuf was abused to give us boolean value */
			memcpy(&(chk.image_support), strbuf, sizeof(chk.image_support));
			break;
		default:
			/* do nothing */
			break;
		}
	}

	// and finally the extensions, if we retrieved them
	if (extensions)
		printf("%s" I1_STR "%s\n", line_pfx, (output_mode == CLINFO_HUMAN ?
				extensions_traits->pname :
				extensions_traits->sname), extensions);
	free(extensions);
	extensions = NULL;
}

/* list of allowed properties for AMD offline devices */
/* everything else seems to be set to 0, and all the other string properties
 * actually segfault the driver */

static const cl_device_info amd_offline_info_whitelist[] = {
	CL_DEVICE_NAME,
	/* These are present, but all the same, so just skip them:
	CL_DEVICE_VENDOR,
	CL_DEVICE_VENDOR_ID,
	CL_DEVICE_VERSION,
	CL_DRIVER_VERSION,
	CL_DEVICE_OPENCL_C_VERSION,
	*/
	CL_DEVICE_EXTENSIONS,
	CL_DEVICE_TYPE,
	CL_DEVICE_MAX_WORK_GROUP_SIZE,
	CL_DEVICE_AVAILABLE
};

/* process offline devices from the cl_amd_offline_devices extension */
int processOfflineDevicesAMD(cl_uint p)
{
	int ret = 0;

	cl_platform_id pid = platform[p];
	cl_device_id *device = NULL;
	cl_int num_devs, d;

	cl_context_properties ctxpft[] = {
		CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
		CL_CONTEXT_OFFLINE_DEVICES_AMD, (cl_context_properties)CL_TRUE,
		0
	};

	cl_context ctx = NULL;

	if (!list_only)
		printf("%s" I0_STR, line_pfx,
			(output_mode == CLINFO_HUMAN ?
			 "Number of offline devices (AMD)" : "#OFFDEVICES"));

	ctx = clCreateContextFromType(ctxpft, CL_DEVICE_TYPE_ALL, NULL, NULL, &error);
	RR_ERROR("create context");

	error = clGetContextInfo(ctx, CL_CONTEXT_NUM_DEVICES, sizeof(num_devs), &num_devs, NULL);
	RR_ERROR("get num devs");

	ALLOC(device, num_devs, "offline devices");

	error = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, num_devs*sizeof(*device), device, NULL);
	RR_ERROR("get devs");

	if (!list_only)
		printf("%d\n", num_devs);

	for (d = 0; d < num_devs; ++d) {
		if (list_only) {
			/*
			if (output_mode == CLINFO_HUMAN)
				puts(" |");
			*/
			if (d == num_devs - 1 && output_mode != CLINFO_RAW)
				line_pfx[1] = '`';
			had_error = device_info_str_get(device[d], CL_DEVICE_NAME, "CL_DEVICE_NAME", NULL);
			printf("%s%u: %s\n", line_pfx, d, strbuf);
		} else {
			if (line_pfx_len > 0) {
				sprintf(strbuf, "[%s/%u]", pdata[p].sname, -d);
				sprintf(line_pfx, "%*s", -line_pfx_len, strbuf);
			}
			printDeviceInfo(device, d, amd_offline_info_whitelist);
			if (d < num_devs - 1)
				puts("");
		}
		fflush(stdout);
		fflush(stderr);
	}

	had_error = CL_FALSE;
out:
	free(device);
	if (ctx)
		clReleaseContext(ctx);
	return ret;

}

void listPlatformsAndDevices(cl_bool show_offline)
{
	cl_uint p, d;
	cl_device_id *device;

	if (output_mode == CLINFO_RAW)
		sprintf(strbuf, "%u", num_platforms);
	else
		sprintf(strbuf, " +-- %sDevice #", (show_offline ? "Offline" : ""));

	line_pfx_len = strlen(strbuf) + 1;
	REALLOC(line_pfx, line_pfx_len, "line prefix");

	for (p = 0, device = all_devices; p < num_platforms; device += pdata[p++].ndevs) {
		printf("%s%u: %s\n",
			(output_mode == CLINFO_HUMAN ? "Platform #" : ""),
			p, pdata[p].pname);
		if (output_mode == CLINFO_RAW)
			sprintf(line_pfx, "%u:", p);
		else
			sprintf(line_pfx, " +-- Device #");

		if (pdata[p].ndevs > 0) {
			error = clGetDeviceIDs(platform[p], CL_DEVICE_TYPE_ALL, pdata[p].ndevs, device, NULL);
			CHECK_ERROR("device IDs");
			for (d = 0; d < pdata[p].ndevs; ++d) {
				/*
				if (output_mode == CLINFO_HUMAN)
					puts(" |");
				*/
				cl_bool last_device = (d == pdata[p].ndevs - 1 && output_mode != CLINFO_RAW &&
					(!show_offline || !pdata[p].has_amd_offline));
				if (last_device)
					line_pfx[1] = '`';
				had_error = device_info_str_get(device[d], CL_DEVICE_NAME, "CL_DEVICE_NAME", NULL);
				printf("%s%u: %s\n", line_pfx, d, strbuf);
				fflush(stdout);
				fflush(stderr);
			}
		}

		if (show_offline && pdata[p].has_amd_offline) {
			if (output_mode == CLINFO_RAW)
				sprintf(line_pfx, "%u*", p);
			else
				sprintf(line_pfx, " +-- Offline Device #");
			had_error = processOfflineDevicesAMD(p);
			if (had_error)
				puts(strbuf);
		}
	}
}

void showDevices(cl_bool show_offline)
{
	cl_uint p, d;
	cl_device_id *device;

	/* TODO consider enabling this for both output modes */
	if (output_mode == CLINFO_RAW) {
		sprintf(strbuf, "%u", maxdevs);
		line_pfx_len = platform_sname_maxlen + strlen(strbuf) + 4;
		REALLOC(line_pfx, line_pfx_len, "line prefix");
	}

	for (p = 0, device = all_devices; p < num_platforms; device += pdata[p++].ndevs) {
		if (line_pfx_len > 0) {
			sprintf(strbuf, "[%s/*]", pdata[p].sname);
			sprintf(line_pfx, "%*s", -line_pfx_len, strbuf);
		}
		printf("%s" I1_STR "%s\n",
			line_pfx,
			(output_mode == CLINFO_HUMAN ?
			 pinfo_traits[0].pname : pinfo_traits[0].sname),
			pdata[p].pname);
		printf("%s" I0_STR "%u\n",
			line_pfx,
			(output_mode == CLINFO_HUMAN ?
			 "Number of devices" : "#DEVICES"),
			pdata[p].ndevs);

		if (pdata[p].ndevs > 0) {
			error = clGetDeviceIDs(platform[p], CL_DEVICE_TYPE_ALL, pdata[p].ndevs, device, NULL);
			CHECK_ERROR("device IDs");
		}
		for (d = 0; d < pdata[p].ndevs; ++d) {
			if (line_pfx_len > 0) {
				sprintf(strbuf, "[%s/%u]", pdata[p].sname, d);
				sprintf(line_pfx, "%*s", -line_pfx_len, strbuf);
			}
			printDeviceInfo(device, d, NULL);
			if (d < pdata[p].ndevs - 1)
				puts("");
			fflush(stdout);
			fflush(stderr);
		}
		if (show_offline && pdata[p].has_amd_offline) {
			puts("");
			had_error = processOfflineDevicesAMD(p);
			if (had_error)
				puts(strbuf);
		}
		puts("");
	}
}

/* check the behavior of clGetPlatformInfo() when given a NULL platform ID */
void checkNullGetPlatformName(void)
{
	current_param = "CL_PLATFORM_NAME";

	error = clGetPlatformInfo(NULL, CL_PLATFORM_NAME, bufsz, strbuf, NULL);
	if (error == CL_INVALID_PLATFORM) {
		bufcpy(0, no_plat());
	} else {
		current_line = __LINE__+1;
		had_error = REPORT_ERROR2("get %s");
	}
	printf(I1_STR "%s\n",
		"clGetPlatformInfo(NULL, CL_PLATFORM_NAME, ...)", strbuf);
}

/* check the behavior of clGetDeviceIDs() when given a NULL platform ID;
 * return the index of the default platform in our array of platform IDs,
 * or num_platforms (which is an invalid platform index) in case of errors
 * or no platform or device found.
 */
cl_uint checkNullGetDevices(void)
{
	cl_uint i = 0; /* generic iterator */
	cl_device_id dev = NULL; /* sample device */
	cl_platform_id plat = NULL; /* detected platform */

	cl_uint found = 0; /* number of platforms found */
	cl_uint pidx = num_platforms; /* index of the platform found */
	cl_uint numdevs = 0;

	current_function = __func__;
	current_param = "device IDs";

	error = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_ALL, 0, NULL, &numdevs);
	/* TODO we should check other CL_DEVICE_TYPE_* combinations, since a smart
	 * implementation might give you a different default platform for GPUs
	 * and for CPUs.
	 * Of course the “no devices” case would then need to be handled differently.
	 * The logic might be maintained similarly, provided we also gather
	 * the number of devices of each type for each platform, although it's
	 * obviously more likely to have multiple platforms with no devices
	 * of a given type.
	 */

	switch (error) {
	case CL_INVALID_PLATFORM:
		bufcpy(0, no_plat());
		break;
	case CL_DEVICE_NOT_FOUND:
		 /* No devices were found, see if there are platforms with
		  * no devices, and if there's only one, assume this is the
		  * one being used as default by the ICD loader */
		for (i = 0; i < num_platforms; ++i) {
			if (pdata[i].ndevs == 0) {
				++found;
				if (found > 1)
					break;
				else {
					plat = platform[i];
					pidx = i;
				}
			}
		}

		switch (found) {
		case 0:
			bufcpy(0, (output_mode == CLINFO_HUMAN ?
				"<error: 0 devices, no matching platform!>" :
				"CL_DEVICE_NOT_FOUND | CL_INVALID_PLATFORM"));
			break;
		case 1:
			bufcpy(0, (output_mode == CLINFO_HUMAN ?
				pdata[pidx].pname :
				pdata[pidx].sname));
			break;
		default: /* found > 1 */
			bufcpy(0, (output_mode == CLINFO_HUMAN ?
				"<error: 0 devices, multiple matching platforms!>" :
				"CL_DEVICE_NOT_FOUND | ????"));
			break;
		}
		break;
	default:
		current_line = __LINE__+1;
		had_error = REPORT_ERROR2("get number of %s");
		if (had_error)
			break;

		/* Determine platform by looking at the CL_DEVICE_PLATFORM of
		 * one of the devices */
		error = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_ALL, 1, &dev, NULL);
		current_line = __LINE__+1;
		had_error = REPORT_ERROR2("get %s");
		if (had_error)
			break;

		current_param = "CL_DEVICE_PLATFORM";
		error = clGetDeviceInfo(dev, CL_DEVICE_PLATFORM,
			sizeof(plat), &plat, NULL);
		current_line = __LINE__+1;
		had_error = REPORT_ERROR2("get %s");
		if (had_error)
			break;

		for (i = 0; i < num_platforms; ++i) {
			if (platform[i] == plat) {
				pidx = i;
				sprintf(strbuf, "%s [%s]",
					(output_mode == CLINFO_HUMAN ? "Success" : "CL_SUCCESS"),
					pdata[i].sname);
				break;
			}
		}
		if (i == num_platforms) {
			sprintf(strbuf, "<error: platform 0x%p not found>", plat);
		}
	}
	printf(I1_STR "%s\n",
		"clGetDeviceIDs(NULL, CL_DEVICE_TYPE_ALL, ...)", strbuf);
	return pidx;
}

void checkNullCtx(cl_uint pidx, const cl_device_id *dev, const char *which)
{
	cl_context ctx = clCreateContext(NULL, 1, dev, NULL, NULL, &error);

	current_function = __func__;
	current_param = which;
	current_line = __LINE__+2;

	had_error = REPORT_ERROR2("create context with device from %s platform");
	if (!had_error)
		sprintf(strbuf, "%s [%s]",
			(output_mode == CLINFO_HUMAN ? "Success" : "CL_SUCCESS"),
			pdata[pidx].sname);
	if (ctx) {
		clReleaseContext(ctx);
		ctx = NULL;
	}
}

/* check behavior of clCreateContextFromType() with NULL cl_context_properties */
void checkNullCtxFromType(void)
{
	size_t t; /* type iterator */
	size_t i; /* generic iterator */
	char def[1024];
	cl_context ctx = NULL;

	size_t ndevs = 8;
	size_t szval = 0;
	size_t cursz = ndevs*sizeof(cl_device_id);
	cl_platform_id plat = NULL;
	cl_device_id *devs = NULL;

	const char *platname_prop = (output_mode == CLINFO_HUMAN ?
		pinfo_traits[0].pname :
		pinfo_traits[0].sname);

	const char *devname_prop = (output_mode == CLINFO_HUMAN ?
		dinfo_traits[0].pname :
		dinfo_traits[0].sname);

	ALLOC(devs, ndevs, "context devices");

	current_function = __func__;
	for (t = 2; t < devtype_count; ++t) { /* we skip 0 and _DEFAULT */
		current_param = device_type_raw_str[t];

		sprintf(strbuf, "clCreateContextFromType(NULL, %s)", current_param);
		sprintf(def, I1_STR, strbuf);

		current_line = __LINE__+1;
		ctx = clCreateContextFromType(NULL, devtype[t], NULL, NULL, &error);

		switch (error) {
		case CL_INVALID_PLATFORM:
			bufcpy(0, no_plat()); break;
		case CL_DEVICE_NOT_FOUND:
		case CL_INVALID_DEVICE_TYPE: /* e.g. _CUSTOM device on 1.1 platform */
			bufcpy(0, no_dev()); break;
		case CL_DEVICE_NOT_AVAILABLE:
			bufcpy(0, no_dev_avail()); break;
		default:
			had_error = REPORT_ERROR2("create context from type %s");
			if (had_error)
				break;

			/* get the devices */
			current_param = "CL_CONTEXT_DEVICES";
			current_line = __LINE__+2;

			error = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, 0, NULL, &szval);
			had_error = REPORT_ERROR2("get %s size");
			if (had_error)
				break;
			if (szval > cursz) {
				REALLOC(devs, szval, "context devices");
				cursz = szval;
			}

			current_line = __LINE__+1;
			error = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, cursz, devs, NULL);
			had_error = REPORT_ERROR2("get %s");
			if (had_error)
				break;
			ndevs = szval/sizeof(cl_device_id);
			if (ndevs < 1) {
				bufcpy(0, "<error: context created with no devices>");
			}

			/* get the platform from the first device */
			current_param = "CL_DEVICE_PLATFORM";
			current_line = __LINE__+1;
			error = clGetDeviceInfo(*devs, CL_DEVICE_PLATFORM, sizeof(plat), &plat, NULL);
			had_error = REPORT_ERROR2("get %s");
			if (had_error)
				break;

			szval = 0;
			for (i = 0; i < num_platforms; ++i) {
				if (platform[i] == plat)
					break;
			}
			if (i == num_platforms) {
				sprintf(strbuf, "<error: platform 0x%p not found>", plat);
				break;
			} else {
				szval += sprintf(strbuf, "%s (%" PRIuS ")",
					(output_mode == CLINFO_HUMAN ? "Success" : "CL_SUCCESS"),
					ndevs);
				szval += snprintf(strbuf + szval, bufsz - szval, "\n" I2_STR "%s",
					platname_prop, pdata[i].pname);
			}
			for (i = 0; i < ndevs; ++i) {
				size_t szname = 0;
				/* for each device, show the device name */
				/* TODO some other unique ID too, e.g. PCI address, if available? */

				szval += snprintf(strbuf + szval, bufsz - szval, "\n" I2_STR, devname_prop);
				if (szval >= bufsz) {
					trunc_strbuf();
					break;
				}

				current_param = "CL_DEVICE_NAME";
				current_line = __LINE__+1;
				error = clGetDeviceInfo(devs[i], CL_DEVICE_NAME, bufsz - szval, strbuf + szval, &szname);
				had_error = REPORT_ERROR2("get %s");
				if (had_error)
					break;
				szval += szname - 1;


			}
			if (i != ndevs)
				break; /* had an error earlier, bail */

		}

		if (ctx) {
			clReleaseContext(ctx);
			ctx = NULL;
		}
		printf("%s%s\n", def, strbuf);
	}
	free(devs);
}

/* check the behavior of NULL platform in clGetDeviceIDs (see checkNullGetDevices)
 * and in clCreateContext() */
void checkNullBehavior(void)
{
	cl_device_id *dev = NULL;
	cl_uint p = 0;
	cl_uint pidx;

	printf("NULL platform behavior\n");

	checkNullGetPlatformName();

	pidx = checkNullGetDevices();

	/* If there's a default platform, and it has devices, try
	 * creating a context with its first device and see if it works */

	if (pidx == num_platforms) {
		bufcpy(0, no_plat());
	} else if (pdata[pidx].ndevs == 0) {
		bufcpy(0, no_dev());
	} else {
		p = 0;
		dev = all_devices;
		while (p < num_platforms && p != pidx) {
			dev += pdata[p++].ndevs;
		}
		if (p < num_platforms) {
			checkNullCtx(pidx, dev, "default");
		} else {
			/* this shouldn't happen, but still ... */
			bufcpy(0, "<error: overflow in default platform scan>");
		}
	}
	printf(I1_STR "%s\n", "clCreateContext(NULL, ...) [default]", strbuf);

	/* Look for a device from a non-default platform, if there are any */
	if (pidx == num_platforms || num_platforms > 1) {
		p = 0;
		dev = all_devices;
		while (p < num_platforms && (p == pidx || pdata[p].ndevs == 0)) {
			dev += pdata[p++].ndevs;
		}
		if (p < num_platforms) {
			checkNullCtx(p, dev, "non-default");
		} else {
			bufcpy(0, "<error: no devices in non-default plaforms>");
		}
		printf(I1_STR "%s\n", "clCreateContext(NULL, ...) [other]", strbuf);
	}

	checkNullCtxFromType();

}


/* Get properties of the ocl-icd loader, if available */
/* All properties are currently char[] */
typedef enum {
	CL_ICDL_OCL_VERSION=1,
	CL_ICDL_VERSION=2,
	CL_ICDL_NAME=3,
	CL_ICDL_VENDOR=4,
} cl_icdl_info;

/* Function pointer to the ICD loader info function */
cl_int (*clGetICDLoaderInfoOCLICD)(cl_icdl_info, size_t, void*, size_t*);

/* We want to auto-detect the OpenCL version supported by the ICD loader.
 * To do this, we will progressively find symbols introduced in new APIs,
 * until a NULL symbol is found.
 */

struct icd_loader_test {
	cl_uint version;
	const char *symbol;
} icd_loader_tests[] = {
	{ 11, "clCreateSubBuffer" },
	{ 12, "clCreateImage" },
	{ 20, "clSVMAlloc" },
	{ 21, "clGetHostTimer" },
	{ 0, NULL }
};

int
icdl_info_str(cl_icdl_info param, const char* pname)
{
	error = clGetICDLoaderInfoOCLICD(param, 0, NULL, &nusz);
	if (nusz > bufsz) {
		REALLOC(strbuf, nusz, current_param);
		bufsz = nusz;
	}
	had_error = REPORT_ERROR2("get %s size");
	if (!had_error) {
		error = clGetICDLoaderInfoOCLICD(param, bufsz, strbuf, NULL);
		had_error = REPORT_ERROR2("get %s");
	}
	show_strbuf(pname, 1);
	return had_error;
}

struct icdl_info_traits {
	cl_icdl_info param; // CL_ICDL_*
	const char *sname; // "CL_ICDL_*"
	const char *pname; // "ICD loader *"
};

static const char * const oclicdl_pfx = "OCLICD";

#define LINFO(symbol, name) { symbol, #symbol, "ICD loader " name }
struct icdl_info_traits linfo_traits[] = {
	LINFO(CL_ICDL_NAME, "Name"),
	LINFO(CL_ICDL_VENDOR, "Vendor"),
	LINFO(CL_ICDL_VERSION, "Version"),
	LINFO(CL_ICDL_OCL_VERSION, "Profile")
};

/* GCC < 4.6 does not support the diagnostic push _inside_ the function,
 * so we have to put it outside
 */
#if defined __GNUC__ && ((__GNUC__*10 + __GNUC_MINOR__) < 46)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

void oclIcdProps(void)
{
	/* First of all, we try to auto-detect the supported ICD loader version */
	int i = 0;

	do {
		struct icd_loader_test check = icd_loader_tests[i];
		if (check.symbol == NULL)
			break;
		if (dlsym(RTLD_DEFAULT, check.symbol) == NULL)
			break;
		icdl_ocl_version_found = check.version;
		++i;
	} while (1);


	/* We find the clGetICDLoaderInfoOCLICD extension address, and use it to query
	 * the ICD loader properties. It should be noted however that
	 * clGetExtensionFunctionAddress is marked deprecated as of OpenCL 1.2, so
	 * to use it and compile cleanly we need disable the relevant warning.
	 * It should be noted that in this specific case we cannot replace the
	 * call to clGetExtensionFunctionAddress with a call to the superseding function
	 * clGetExtensionFunctionAddressForPlatform because the extension is in the
	 * loader itself, not in a specific platform.
	 */

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined __GNUC__ && ((__GNUC__*10 + __GNUC_MINOR__) >= 46)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

	clGetICDLoaderInfoOCLICD = clGetExtensionFunctionAddress("clGetICDLoaderInfoOCLICD");

#ifdef _MSC_VER
#pragma warning(pop)
#elif defined __GNUC__ && ((__GNUC__*10 + __GNUC_MINOR__) >= 46)
#pragma GCC diagnostic pop
#endif

	if (clGetICDLoaderInfoOCLICD != NULL) {
		/* TODO think of a sensible header in CLINFO_RAW */
		if (output_mode != CLINFO_RAW)
			puts("\nICD loader properties");
		current_function = __func__;

		if (output_mode == CLINFO_RAW) {
			line_pfx_len = strlen(oclicdl_pfx) + 5;
			REALLOC(line_pfx, line_pfx_len, "line prefix OCL ICD");
			sprintf(strbuf, "[%s/*]", oclicdl_pfx);
			sprintf(line_pfx, "%*s", -line_pfx_len, strbuf);
		}

		for (current_line = 0; current_line < ARRAY_SIZE(linfo_traits); ++current_line) {
			const struct icdl_info_traits *traits = linfo_traits + current_line;
			current_param = traits->sname;

			had_error = icdl_info_str(traits->param,
				output_mode == CLINFO_HUMAN ?
				traits->pname : traits->sname);

			if (!had_error && traits->param == CL_ICDL_OCL_VERSION) {
				icdl_ocl_version = getOpenCLVersion(strbuf + 7);
			}
		}
	}

	if (output_mode == CLINFO_HUMAN) {
		if (icdl_ocl_version &&
			icdl_ocl_version != icdl_ocl_version_found) {
			printf(	"\tNOTE:\tyour OpenCL library declares to support OpenCL %u.%u,\n"
				"\t\tbut it seems to support up to OpenCL %u.%u %s.\n",
				icdl_ocl_version / 10, icdl_ocl_version % 10,
				icdl_ocl_version_found / 10, icdl_ocl_version_found % 10,
				icdl_ocl_version_found < icdl_ocl_version  ?
				"only" : "too");
		}
		if (icdl_ocl_version_found < max_plat_version) {
			printf(	"\tNOTE:\tyour OpenCL library only supports OpenCL %u.%u,\n"
				"\t\tbut some installed platforms support OpenCL %u.%u.\n"
				"\t\tPrograms using %u.%u features may crash\n"
				"\t\tor behave unexepectedly\n",
				icdl_ocl_version_found / 10, icdl_ocl_version_found % 10,
				max_plat_version / 10, max_plat_version % 10,
				max_plat_version / 10, max_plat_version % 10);
		}
	}
}

#if defined __GNUC__ && ((__GNUC__*10 + __GNUC_MINOR__) < 46)
#pragma GCC diagnostic warning "-Wdeprecated-declarations"
#endif

void version(void)
{
	puts("clinfo version 2.1.16.01.12");
}

void usage(void)
{
	version();
	puts("Display properties of all available OpenCL platforms and devices");
	puts("Usage: clinfo [options ...]\n");
	puts("Options:");
	puts("\t--human\t\thuman-friendly output (default)");
	puts("\t--raw\t\traw output");
	puts("\t--offline\talso show offline devices");
	puts("\t--list, -l\tonly list the platforms and devices by name");
	puts("\t-h, -?\t\tshow usage");
	puts("\t--version, -v\tshow version\n");
	puts("Defaults to raw mode if invoked with");
	puts("a name that contains the string \"raw\"");
}

int main(int argc, char *argv[])
{
	cl_uint p;
	int a = 0;

	cl_bool show_offline = CL_FALSE;

	/* if there's a 'raw' in the program name, switch to raw output mode */
	if (strstr(argv[0], "raw"))
		output_mode = CLINFO_RAW;

	/* process command-line arguments */
	while (++a < argc) {
		if (!strcmp(argv[a], "--raw"))
			output_mode = CLINFO_RAW;
		else if (!strcmp(argv[a], "--human"))
			output_mode = CLINFO_HUMAN;
		else if (!strcmp(argv[a], "--offline"))
			show_offline = CL_TRUE;
		else if (!strcmp(argv[a], "-l") || !strcmp(argv[a], "--list"))
			list_only = CL_TRUE;
		else if (!strcmp(argv[a], "-?") || !strcmp(argv[a], "-h")) {
			usage();
			return 0;
		} else if (!strcmp(argv[a], "--version") || !strcmp(argv[a], "-v")) {
			version();
			return 0;
		} else {
			fprintf(stderr, "ignoring unknown command-line parameter %s\n", argv[a]);
		}
	}


	ALLOC(strbuf, 1024, "general string buffer");
	bufsz = 1024;

	error = clGetPlatformIDs(0, NULL, &num_platforms);
	if (error != CL_PLATFORM_NOT_FOUND_KHR)
		CHECK_ERROR("number of platforms");

	if (!list_only)
		printf(I0_STR "%u\n",
			(output_mode == CLINFO_HUMAN ?
			 "Number of platforms" : "#PLATFORMS"),
			num_platforms);
	if (!num_platforms)
		return 0;

	ALLOC(platform, num_platforms, "platform IDs");
	error = clGetPlatformIDs(num_platforms, platform, NULL);
	CHECK_ERROR("platform IDs");

	ALLOC(pdata, num_platforms, "platform data");
	ALLOC(line_pfx, 1, "line prefix");

	for (p = 0; p < num_platforms; ++p) {
		printPlatformInfo(p);
		if (!list_only)
			puts("");
	}

	if (num_devs_all > 0) {
		ALLOC(all_devices, num_devs_all, "device IDs");
	}

	if (list_only) {
		listPlatformsAndDevices(show_offline);
	} else {
		showDevices(show_offline);
		if (output_mode != CLINFO_RAW)
			checkNullBehavior();
		oclIcdProps();
	}

	return 0;
}

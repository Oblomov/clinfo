/* Collect all available information on all available devices
 * on all available OpenCL platforms present in the system
 */

#include <time.h>
#include <string.h>

/* We will want to check for symbols in the OpenCL library.
 * On Windows, we must get the module handle for it, on Unix-like
 * systems we can just use RTLD_DEFAULT
 */
#ifdef _MSC_VER
# include <windows.h>
# define dlsym GetProcAddress
# define DL_MODULE GetModuleHandle("OpenCL")
#else
# include <dlfcn.h>
# define DL_MODULE ((void*)0) /* This would be RTLD_DEFAULT */
#endif

/* Load STDC format macros (PRI*), or define them
 * for those crappy, non-standard compilers
 */
#include "fmtmacros.h"

// More support for the horrible MS C compiler
#ifdef _MSC_VER
#include "ms_support.h"
#endif

#include "error.h"
#include "memory.h"
#include "strbuf.h"

#include "ext.h"
#include "ctx_prop.h"
#include "info_loc.h"
#include "info_ret.h"
#include "opt_out.h"

#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(*ar))

#ifndef UNUSED
#define UNUSED(x) x __attribute__((unused))
#endif

struct platform_data {
	char *pname; /* CL_PLATFORM_NAME */
	char *sname; /* CL_PLATFORM_ICD_SUFFIX_KHR or surrogate */
	cl_uint ndevs; /* number of devices */
	cl_bool has_amd_offline; /* has cl_amd_offline_devices extension */
};

struct platform_info_checks {
	cl_uint plat_version;
	cl_bool has_khr_icd;
	cl_bool has_amd_object_metadata;
};

struct platform_list {
	/* Number of platforms in the system */
	cl_uint num_platforms;
	/* Total number of devices across all platforms */
	cl_uint ndevs_total;
	/* Number of devices allocated in all_devs array */
	cl_uint alloc_devs;
	/* Highest OpenCL version supported by any platform.
	 * If the OpenCL library / ICD loader only supports
	 * a lower version, problems may arise (such as
	 * API calls causing segfaults or any other unexpected
	 * behavior
	 */
	cl_uint max_plat_version;
	/* Largest number of devices on any platform */
	cl_uint max_devs;
	/* Length of the longest platform sname */
	cl_int max_sname_len;
	/* Array of platform IDs */
	cl_platform_id *platform;
	/* Array of device IDs (across all platforms) */
	cl_device_id *all_devs;
	/* Array of offsets in all_devs where the devices
	 * of each platform begin */
	cl_uint *dev_offset;
	/* Array of clinfo-specific platform data */
	struct platform_data *pdata;
	/* Arrau of clinfo-specifici platform checks */
	struct platform_info_checks *platform_checks;
};

void
init_plist(struct platform_list *plist)
{
	plist->num_platforms = 0;
	plist->ndevs_total = 0;
	plist->alloc_devs = 0;
	plist->max_plat_version = 0;
	plist->platform = NULL;
	plist->all_devs = NULL;
	plist->dev_offset = NULL;
	plist->pdata = NULL;
	plist->platform_checks = NULL;
}

void plist_devs_reserve(struct platform_list *plist, cl_uint amount)
{
	if (amount > plist->alloc_devs) {
		REALLOC(plist->all_devs, amount, "all devices");
		plist->alloc_devs = amount;
	}
}


void
alloc_plist(struct platform_list *plist)
{
	ALLOC(plist->platform, plist->num_platforms, "platform IDs");
	ALLOC(plist->dev_offset, plist->num_platforms, "platform device list offset");
	/* The actual sizing for this will change as we gather platform info,
	 * but assume at least one device per platform
	 */
	plist_devs_reserve(plist, plist->num_platforms);
	ALLOC(plist->pdata, plist->num_platforms, "platform data");
	ALLOC(plist->platform_checks, plist->num_platforms, "platform checks data");
}
void
free_plist(struct platform_list *plist)
{
	free(plist->platform);
	free(plist->all_devs);
	free(plist->dev_offset);
	free(plist->pdata);
	free(plist->platform_checks);
	init_plist(plist);
}

const cl_device_id *
get_platform_devs(const struct platform_list *plist, cl_uint p)
{
	return plist->all_devs + plist->dev_offset[p];
}

cl_device_id
get_platform_dev(const struct platform_list *plist, cl_uint p, cl_uint d)
{
	return get_platform_devs(plist, p)[d];
}

/* Data for the OpenCL library / ICD loader */
struct icdl_data {
	/* auto-detected OpenCL version support for the ICD loader */
	cl_uint detected_version;
	/* OpenCL version support declared by the ICD loader */
	cl_uint reported_version;
};

/* line prefix, used to identify the platform/device for each
 * device property in RAW output mode */
char *line_pfx;
int line_pfx_len;

#define CHECK_SIZE(ret, loc, val, cmd, ...) do { \
	/* check if the issue is with param size */ \
	if (output->check_size && ret->err == CL_INVALID_VALUE) { \
		size_t _actual_sz; \
		if (cmd(__VA_ARGS__, 0, NULL, &_actual_sz) == CL_SUCCESS) { \
			REPORT_SIZE_MISMATCH(&(ret->err_str), loc, _actual_sz, sizeof(val)); \
		} \
	} \
} while (0)

static const char unk[] = "Unknown";
static const char none[] = "None";
static const char none_raw[] = "CL_NONE";
static const char na[] = "n/a"; // not available
static const char na_wrap[] = "(n/a)"; // not available
static const char core[] = "core";

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
/* number of actual device types, without ALL */
const size_t actual_devtype_count = ARRAY_SIZE(devtype) - 1;

static const char* device_type_str[] = { unk, "Default", "CPU", "GPU", "Accelerator", "Custom", "All" };
static const char* device_type_raw_str[] = { unk,
	"CL_DEVICE_TYPE_DEFAULT", "CL_DEVICE_TYPE_CPU", "CL_DEVICE_TYPE_GPU",
	"CL_DEVICE_TYPE_ACCELERATOR", "CL_DEVICE_TYPE_CUSTOM", "CL_DEVICE_TYPE_ALL"
};

static const char* partition_type_str[] = {
	none, "equally", "by counts", "by affinity domain", "by names (Intel)"
};
static const char* partition_type_raw_str[] = {
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

static const char *terminate_capability_str[] = {
	"Context"
};

static const char *terminate_capability_raw_str[] = {
	"CL_DEVICE_TERMINATE_CAPABILITY_CONTEXT_KHR"
};

const size_t terminate_capability_count = ARRAY_SIZE(terminate_capability_str);

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

/* SI suffixes for memory sizes. Note that in OpenCL most of them are
 * passed via a cl_ulong, which at most can mode 16 EiB, but hey,
 * let's be forward-thinking ;-)
 */
static const char* memsfx[] = {
	"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"
};

const size_t memsfx_end = ARRAY_SIZE(memsfx) + 1;

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

const char *num_devs_header(const struct opt_out *output, cl_bool these_are_offline)
{
	return output->mode == CLINFO_HUMAN ?
		(these_are_offline ? "Number of offine devices (AMD)" : "Number of devices") :
		(these_are_offline ? "#OFFDEVICES" : "#DEVICES");
}

const char *not_specified(const struct opt_out *output)
{
	return output->mode == CLINFO_HUMAN ?
		na_wrap : "";
}

const char *no_plat(const struct opt_out *output)
{
	return output->mode == CLINFO_HUMAN ?
		"No platform" :
		"CL_INVALID_PLATFORM";
}

const char *invalid_dev_type(const struct opt_out *output)
{
	return output->mode == CLINFO_HUMAN ?
		"Invalid device type for platform" :
		"CL_INVALID_DEVICE_TYPE";
}

const char *invalid_dev_value(const struct opt_out *output)
{
	return output->mode == CLINFO_HUMAN ?
		"Invalid device type value for platform" :
		"CL_INVALID_VALUE";
}

const char *no_dev_found(const struct opt_out *output)
{
	return output->mode == CLINFO_HUMAN ?
		"No devices found in platform" :
		"CL_DEVICE_NOT_FOUND";
}

const char *no_dev_avail(const struct opt_out *output)
{
	return output->mode == CLINFO_HUMAN ?
		"No devices available in platform" :
		"CL_DEVICE_NOT_AVAILABLE";
}

/* OpenCL context interop names */

typedef struct cl_interop_name {
	cl_uint from;
	cl_uint to;
	/* 5 because that's the largest we know of,
	 * 2 because it's HUMAN, RAW */
	const char *value[5][2];
} cl_interop_name;

static const cl_interop_name cl_interop_names[] = {
	{ /* cl_khr_gl_sharing */
		 CL_GL_CONTEXT_KHR,
		 CL_CGL_SHAREGROUP_KHR,
		 {
			{ "GL", "CL_GL_CONTEXT_KHR" },
			{ "EGL", "CL_EGL_DISPALY_KHR" },
			{ "GLX", "CL_GLX_DISPLAY_KHR" },
			{ "WGL", "CL_WGL_HDC_KHR" },
			{ "CGL", "CL_CGL_SHAREGROUP_KHR" }
		}
	},
	{ /* cl_khr_dx9_media_sharing */
		CL_CONTEXT_ADAPTER_D3D9_KHR,
		CL_CONTEXT_ADAPTER_DXVA_KHR,
		{
			{ "D3D9 (KHR)", "CL_CONTEXT_ADAPTER_D3D9_KHR" },
			{ "D3D9Ex (KHR)", "CL_CONTEXT_ADAPTER_D3D9EX_KHR" },
			{ "DXVA (KHR)", "CL_CONTEXT_ADAPTER_DXVA_KHR" }
		}
	},
	{ /* cl_khr_d3d10_sharing */
		CL_CONTEXT_D3D10_DEVICE_KHR,
		CL_CONTEXT_D3D10_DEVICE_KHR,
		{
			{ "D3D10", "CL_CONTEXT_D3D10_DEVICE_KHR" }
		}
	},
	{ /* cl_khr_d3d11_sharing */
		CL_CONTEXT_D3D11_DEVICE_KHR,
		CL_CONTEXT_D3D11_DEVICE_KHR,
		{
			{ "D3D11", "CL_CONTEXT_D3D11_DEVICE_KHR" }
		}
	},
	/* cl_intel_dx9_media_sharing is split in two because the allowed values are not consecutive */
	{ /* cl_intel_dx9_media_sharing part 1 */
		CL_CONTEXT_D3D9_DEVICE_INTEL,
		CL_CONTEXT_D3D9_DEVICE_INTEL,
		{
			{ "D3D9 (INTEL)", "CL_CONTEXT_D3D9_DEVICE_INTEL" }
		}
	},
	{ /* cl_intel_dx9_media_sharing part 2 */
		CL_CONTEXT_D3D9EX_DEVICE_INTEL,
		CL_CONTEXT_DXVA_DEVICE_INTEL,
		{
			{ "D3D9Ex (INTEL)", "CL_CONTEXT_D3D9EX_DEVICE_INTEL" },
			{ "DXVA (INTEL)", "CL_CONTEXT_DXVA_DEVICE_INTEL" }
		}
	},
	{ /* cl_intel_va_api_media_sharing */
		CL_CONTEXT_VA_API_DISPLAY_INTEL,
		CL_CONTEXT_VA_API_DISPLAY_INTEL,
		{
			{ "VA-API", "CL_CONTEXT_VA_API_DISPLAY_INTEL" }
		}
	}
};

const size_t num_known_interops = ARRAY_SIZE(cl_interop_names);


#define INDENT "  "
#define I0_STR "%-48s  "
#define I1_STR "  %-46s  "
#define I2_STR "    %-44s  "

static const char empty_str[] = "";
static const char spc_str[] = " ";
static const char times_str[] = "x";
static const char comma_str[] = ", ";
static const char vbar_str[] = " | ";

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

#define SPLIT_CL_VERSION(ver) ((ver)/10), ((ver)%10)

/* print strbuf, prefixed by pname, skipping leading whitespace if skip is nonzero,
 * affixing cur_sfx */
static inline
void show_strbuf(const struct _strbuf *strbuf, const char *pname, int skip, cl_int err)
{
	printf("%s" I1_STR "%s%s\n",
		line_pfx, pname,
		(skip ? skip_leading_ws(strbuf->buf) : strbuf->buf),
		err ? empty_str : cur_sfx);
}

void
platform_info_str(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out* UNUSED(output))
{
	GET_STRING_LOC(ret, loc, clGetPlatformInfo, loc->plat, loc->param.plat);
}

void
platform_info_ulong(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	ret->err = REPORT_ERROR_LOC(ret,
		clGetPlatformInfo(loc->plat, loc->param.plat, sizeof(ret->value.u64), &ret->value.u64, NULL),
		loc, "get %s");
	CHECK_SIZE(ret, loc, ret->value.u64, clGetPlatformInfo, loc->plat, loc->param.plat);
	strbuf_printf(&ret->str, "%" PRIu64, ret->value.u64);
}

void
platform_info_sz(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	ret->err = REPORT_ERROR_LOC(ret,
		clGetPlatformInfo(loc->plat, loc->param.plat, sizeof(ret->value.s), &ret->value.s, NULL),
		loc, "get %s");
	CHECK_SIZE(ret, loc, ret->value.s, clGetPlatformInfo, loc->plat, loc->param.plat);
	strbuf_printf(&ret->str, "%" PRIuS, ret->value.s);
}

struct platform_info_traits {
	cl_platform_info param; // CL_PLATFORM_*
	const char *sname; // "CL_PLATFORM_*"
	const char *pname; // "Platform *"
	const char *sfx; // suffix for the output in non-raw mode
	/* pointer to function that retrieves the parameter */
	void (*show_func)(struct platform_info_ret *,
		const struct info_loc *, const struct platform_info_checks *,
		const struct opt_out *);
	/* pointer to function that checks if the parameter should be retrieved */
	cl_bool (*check_func)(const struct platform_info_checks *);
};

cl_bool khr_icd_p(const struct platform_info_checks *chk)
{
	return chk->has_khr_icd;
}

cl_bool plat_is_12(const struct platform_info_checks *chk)
{
	return !(chk->plat_version < 12);
}

cl_bool plat_is_20(const struct platform_info_checks *chk)
{
	return !(chk->plat_version < 20);
}

cl_bool plat_is_21(const struct platform_info_checks *chk)
{
	return !(chk->plat_version < 21);
}

cl_bool plat_has_amd_object_metadata(const struct platform_info_checks *chk)
{
	return chk->has_amd_object_metadata;
}


#define PINFO_COND(symbol, name, sfx, typ, funcptr) { symbol, #symbol, "Platform " name, sfx, &platform_info_##typ, &funcptr }
#define PINFO(symbol, name, sfx, typ) { symbol, #symbol, "Platform " name, sfx, &platform_info_##typ, NULL }
struct platform_info_traits pinfo_traits[] = {
	PINFO(CL_PLATFORM_NAME, "Name", NULL, str),
	PINFO(CL_PLATFORM_VENDOR, "Vendor", NULL, str),
	PINFO(CL_PLATFORM_VERSION, "Version", NULL, str),
	PINFO(CL_PLATFORM_PROFILE, "Profile", NULL, str),
	PINFO(CL_PLATFORM_EXTENSIONS, "Extensions", NULL, str),
	PINFO_COND(CL_PLATFORM_MAX_KEYS_AMD, "Max metadata object keys (AMD)", NULL, sz, plat_has_amd_object_metadata),
	PINFO_COND(CL_PLATFORM_HOST_TIMER_RESOLUTION, "Host timer resolution", "ns", ulong, plat_is_21),
	PINFO_COND(CL_PLATFORM_ICD_SUFFIX_KHR, "Extensions function suffix", NULL, str, khr_icd_p)
};

/* Collect (and optionally show) infomation on a specific platform,
 * initializing relevant arrays and optionally showing the collected
 * information
 */
void
gatherPlatformInfo(struct platform_list *plist, cl_uint p, const struct opt_out *output)
{
	cl_int len = 0;

	struct platform_data *pdata = plist->pdata + p;
	struct platform_info_checks *pinfo_checks = plist->platform_checks + p;
	struct platform_info_ret ret;
	struct info_loc loc;

	pinfo_checks->plat_version = 10;

	INIT_RET(ret, "platform");
	reset_loc(&loc, __func__);
	loc.plat = plist->platform[p];

	for (loc.line = 0; loc.line < ARRAY_SIZE(pinfo_traits); ++loc.line) {
		const struct platform_info_traits *traits = pinfo_traits + loc.line;

		/* checked is true if there was no condition to check for, or if the
		 * condition was satisfied
		 */
		int checked = !(traits->check_func && !traits->check_func(pinfo_checks));

		if (output->cond == COND_PROP_CHECK && !checked)
			continue;

		loc.sname = traits->sname;
		loc.pname = (output->mode == CLINFO_HUMAN ?
			traits->pname : traits->sname);
		loc.param.plat = traits->param;

		cur_sfx = (output->mode == CLINFO_HUMAN && traits->sfx) ? traits->sfx : empty_str;

		ret.str.buf[0] = '\0';
		ret.err_str.buf[0] = '\0';
		traits->show_func(&ret, &loc, pinfo_checks, output);

		/* The property is skipped if this was a conditional property,
		 * unsatisfied, there was an error retrieving it and cond_prop_mode is not
		 * COND_PROP_SHOW.
		 */
		if (ret.err && !checked && output->cond != COND_PROP_SHOW)
			continue;

		/* when only listing, do not print anything, we're just gathering
		 * information */
		if (output->detailed) {
			show_strbuf(RET_BUF(ret), loc.pname, 0, ret.err);
		}

		if (ret.err)
			continue;

		/* post-processing */

		switch (traits->param) {
		case CL_PLATFORM_NAME:
			/* Store name for future reference */
			len = strlen(ret.str.buf);
			ALLOC(pdata->pname, len+1, "platform name copy");
			/* memcpy instead of strncpy since we already have the len
			 * and memcpy is possibly more optimized */
			memcpy(pdata->pname, ret.str.buf, len);
			pdata->pname[len] = '\0';
			break;
		case CL_PLATFORM_VERSION:
			/* compute numeric value for OpenCL version */
			pinfo_checks->plat_version = getOpenCLVersion(ret.str.buf + 7);
			break;
		case CL_PLATFORM_EXTENSIONS:
			pinfo_checks->has_khr_icd = !!strstr(ret.str.buf, "cl_khr_icd");
			pinfo_checks->has_amd_object_metadata = !!strstr(ret.str.buf, "cl_amd_object_metadata");
			pdata->has_amd_offline = !!strstr(ret.str.buf, "cl_amd_offline_devices");
			break;
		case CL_PLATFORM_ICD_SUFFIX_KHR:
			/* Store ICD suffix for future reference */
			len = strlen(ret.str.buf);
			ALLOC(pdata->sname, len+1, "platform ICD suffix copy");
			/* memcpy instead of strncpy since we already have the len
			 * and memcpy is possibly more optimized */
			memcpy(pdata->sname, ret.str.buf, len);
			pdata->sname[len] = '\0';
		default:
			/* do nothing */
			break;
		}

	}

	if (pinfo_checks->plat_version > plist->max_plat_version)
		plist->max_plat_version = pinfo_checks->plat_version;

	/* if no CL_PLATFORM_ICD_SUFFIX_KHR, use P### as short/symbolic name */
	if (!pdata->sname) {
#define SNAME_MAX 32
		ALLOC(pdata->sname, SNAME_MAX, "platform symbolic name");
		snprintf(pdata->sname, SNAME_MAX, "P%" PRIu32 "", p);
	}

	len = (cl_int)strlen(pdata->sname);
	if (len > plist->max_sname_len)
		plist->max_sname_len = len;

	ret.err = clGetDeviceIDs(loc.plat, CL_DEVICE_TYPE_ALL, 0, NULL, &pdata->ndevs);
	if (ret.err == CL_DEVICE_NOT_FOUND)
		pdata->ndevs = 0;
	else
		CHECK_ERROR(ret.err, "number of devices");
	plist->ndevs_total += pdata->ndevs;
	plist->dev_offset[p] = p ? plist->dev_offset[p-1] + (pdata-1)->ndevs : 0;
	plist_devs_reserve(plist, plist->ndevs_total);

	if (pdata->ndevs > 0) {
		ret.err = clGetDeviceIDs(loc.plat, CL_DEVICE_TYPE_ALL,
			pdata->ndevs,
			plist->all_devs + plist->dev_offset[p], NULL);
	}

	if (pdata->ndevs > plist->max_devs)
		plist->max_devs = pdata->ndevs;

	UNINIT_RET(ret);
}

/*
 * Device properties/extensions used in traits checks, and relevant functions
 */

struct device_info_checks {
	const struct platform_info_checks *pinfo_checks;
	cl_device_type devtype;
	cl_device_mem_cache_type cachetype;
	cl_device_local_mem_type lmemtype;
	cl_bool image_support;
	cl_bool compiler_available;
	char has_half[12];
	char has_double[24];
	char has_nv[29];
	char has_amd[30];
	char has_amd_svm[11];
	char has_arm_svm[29];
	char has_arm_core_id[15];
	char has_fission[22];
	char has_atomic_counters[26];
	char has_image2d_buffer[27];
	char has_il_program[18];
	char has_intel_local_thread[30];
	char has_intel_AME[36];
	char has_intel_AVC_ME[43];
	char has_intel_planar_yuv[20];
	char has_intel_required_subgroup_size[32];
	char has_altera_dev_temp[29];
	char has_p2p[23];
	char has_spir[12];
	char has_qcom_ext_host_ptr[21];
	char has_simultaneous_sharing[30];
	char has_subgroup_named_barrier[30];
	char has_terminate_context[25];
	cl_uint dev_version;
};

#define DEFINE_EXT_CHECK(ext) cl_bool dev_has_##ext(const struct device_info_checks *chk) \
{ \
	return !!(chk->has_##ext[0]); \
}

DEFINE_EXT_CHECK(half)
DEFINE_EXT_CHECK(double)
DEFINE_EXT_CHECK(nv)
DEFINE_EXT_CHECK(amd)
DEFINE_EXT_CHECK(amd_svm)
DEFINE_EXT_CHECK(arm_svm)
DEFINE_EXT_CHECK(arm_core_id)
DEFINE_EXT_CHECK(fission)
DEFINE_EXT_CHECK(atomic_counters)
DEFINE_EXT_CHECK(image2d_buffer)
DEFINE_EXT_CHECK(il_program)
DEFINE_EXT_CHECK(intel_local_thread)
DEFINE_EXT_CHECK(intel_AME)
DEFINE_EXT_CHECK(intel_AVC_ME)
DEFINE_EXT_CHECK(intel_planar_yuv)
DEFINE_EXT_CHECK(intel_required_subgroup_size)
DEFINE_EXT_CHECK(altera_dev_temp)
DEFINE_EXT_CHECK(p2p)
DEFINE_EXT_CHECK(spir)
DEFINE_EXT_CHECK(qcom_ext_host_ptr)
DEFINE_EXT_CHECK(simultaneous_sharing)
DEFINE_EXT_CHECK(subgroup_named_barrier)
DEFINE_EXT_CHECK(terminate_context)

/* In the version checks we negate the opposite conditions
 * instead of double-negating the actual condition
 */

// device supports 1.1
cl_bool dev_is_11(const struct device_info_checks *chk)
{
	return !(chk->dev_version < 11);
}


// device supports 1.2
cl_bool dev_is_12(const struct device_info_checks *chk)
{
	return !(chk->dev_version < 12);
}

// device supports 2.0
cl_bool dev_is_20(const struct device_info_checks *chk)
{
	return !(chk->dev_version < 20);
}

// device supports 2.1
cl_bool dev_is_21(const struct device_info_checks *chk)
{
	return !(chk->dev_version < 21);
}

// device does not support 2.0
cl_bool dev_not_20(const struct device_info_checks *chk)
{
	return !(chk->dev_version >= 20);
}


cl_bool dev_is_gpu(const struct device_info_checks *chk)
{
	return !!(chk->devtype & CL_DEVICE_TYPE_GPU);
}

cl_bool dev_is_gpu_amd(const struct device_info_checks *chk)
{
	return dev_is_gpu(chk) && dev_has_amd(chk);
}

/* Device supports cl_amd_device_attribute_query v4 */
cl_bool dev_has_amd_v4(const struct device_info_checks *chk)
{
	/* We don't actually have a criterion to check if the device
	 * supports a specific version of an extension, so for the time
	 * being rely on them being GPU devices with cl_amd_device_attribute_query
	 * and the platform supporting OpenCL 2.0 or later
	 * TODO FIXME tune criteria
	 */
	return dev_is_gpu(chk) && dev_has_amd(chk) && plat_is_20(chk->pinfo_checks);
}

/* Device supports cl_arm_core_id v2 */
cl_bool dev_has_arm_core_id_v2(const struct device_info_checks *chk)
{
	/* We don't actually have a criterion to check if the device
	 * supports a specific version of an extension, so for the time
	 * being rely on them having cl_arm_core_id and the platform
	 * supporting OpenCL 1.2 or later
	 * TODO FIXME tune criteria
	 */
	return dev_has_arm_core_id(chk) && plat_is_12(chk->pinfo_checks);
}

cl_bool dev_has_svm(const struct device_info_checks *chk)
{
	return dev_is_20(chk) || dev_has_amd_svm(chk);
}

cl_bool dev_has_partition(const struct device_info_checks *chk)
{
	return dev_is_12(chk) || dev_has_fission(chk);
}

cl_bool dev_has_cache(const struct device_info_checks *chk)
{
	return chk->cachetype != CL_NONE;
}

cl_bool dev_has_lmem(const struct device_info_checks *chk)
{
	return chk->lmemtype != CL_NONE;
}

cl_bool dev_has_il(const struct device_info_checks *chk)
{
	return dev_is_21(chk) || dev_has_il_program(chk);
}

cl_bool dev_has_images(const struct device_info_checks *chk)
{
	return chk->image_support;
}

cl_bool dev_has_images_12(const struct device_info_checks *chk)
{
	return dev_has_images(chk) && dev_is_12(chk);
}

cl_bool dev_has_images_20(const struct device_info_checks *chk)
{
	return dev_has_images(chk) && dev_is_20(chk);
}

cl_bool dev_has_compiler(const struct device_info_checks *chk)
{
	return chk->compiler_available;
}

cl_bool dev_has_compiler_11(const struct device_info_checks *chk)
{
	return dev_is_11(chk) && dev_has_compiler(chk);
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
	CHECK_EXT(amd_svm, cl_amd_svm);
	CHECK_EXT(arm_svm, cl_arm_shared_virtual_memory);
	CHECK_EXT(arm_core_id, cl_arm_core_id);
	CHECK_EXT(fission, cl_ext_device_fission);
	CHECK_EXT(atomic_counters, cl_ext_atomic_counters_64);
	if (dev_has_atomic_counters(chk))
		CHECK_EXT(atomic_counters, cl_ext_atomic_counters_32);
	CHECK_EXT(image2d_buffer, cl_khr_image2d_from_buffer);
	CHECK_EXT(il_program, cl_khr_il_program);
	CHECK_EXT(intel_local_thread, cl_intel_exec_by_local_thread);
	CHECK_EXT(intel_AME, cl_intel_advanced_motion_estimation);
	CHECK_EXT(intel_AVC_ME, cl_intel_device_side_avc_motion_estimation);
	CHECK_EXT(intel_planar_yuv, cl_intel_planar_yuv);
	CHECK_EXT(intel_required_subgroup_size, cl_intel_required_subgroup_size);
	CHECK_EXT(altera_dev_temp, cl_altera_device_temperature);
	CHECK_EXT(p2p, cl_amd_copy_buffer_p2p);
	CHECK_EXT(qcom_ext_host_ptr, cl_qcom_ext_host_ptr);
	CHECK_EXT(simultaneous_sharing, cl_intel_simultaneous_sharing);
	CHECK_EXT(subgroup_named_barrier, cl_khr_subgroup_named_barrier);
	CHECK_EXT(terminate_context, cl_khr_terminate_context);
}


/*
 * Device info print functions
 */

#define _GET_VAL(ret, loc, val) \
	ret->err = REPORT_ERROR_LOC(ret, \
		clGetDeviceInfo((loc)->dev, (loc)->param.dev, sizeof(val), &(val), NULL), \
		loc, "get %s"); \
	CHECK_SIZE(ret, loc, val, clGetDeviceInfo, (loc)->dev, (loc)->param.dev);

#define _GET_VAL_ARRAY(ret, loc) \
	ret->err = REPORT_ERROR_LOC(ret, \
		clGetDeviceInfo(loc->dev, loc->param.dev, 0, NULL, &szval), \
		loc, "get number of %s"); \
	numval = szval/sizeof(*val); \
	if (!ret->err) { \
		REALLOC(val, numval, loc->sname); \
		ret->err = REPORT_ERROR_LOC(ret, \
			clGetDeviceInfo(loc->dev, loc->param.dev, szval, val, NULL), \
			loc, "get %s"); \
		if (ret->err) { free(val); val = NULL; } \
	}

#define GET_VAL(ret, loc, field) do { \
	_GET_VAL(ret, (loc), ret->value.field) \
} while (0)

#define GET_VAL_ARRAY(ret, loc) do { \
	_GET_VAL_ARRAY(ret, (loc)) \
} while (0)

#define DEFINE_DEVINFO_FETCH(type, field) \
type \
device_fetch_##type(struct device_info_ret *ret, \
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk), \
	const struct opt_out *output) \
{ \
	GET_VAL(ret, loc, field); \
	return ret->value.field; \
}

DEFINE_DEVINFO_FETCH(size_t, s)
DEFINE_DEVINFO_FETCH(cl_bool, b)
DEFINE_DEVINFO_FETCH(cl_uint, u32)
DEFINE_DEVINFO_FETCH(cl_ulong, u64)
DEFINE_DEVINFO_FETCH(cl_device_type, devtype)
DEFINE_DEVINFO_FETCH(cl_device_mem_cache_type, cachetype)
DEFINE_DEVINFO_FETCH(cl_device_local_mem_type, lmemtype)
DEFINE_DEVINFO_FETCH(cl_device_topology_amd, devtopo)
DEFINE_DEVINFO_FETCH(cl_device_affinity_domain, affinity_domain)
DEFINE_DEVINFO_FETCH(cl_device_fp_config, fpconfig)
DEFINE_DEVINFO_FETCH(cl_command_queue_properties, qprop)
DEFINE_DEVINFO_FETCH(cl_device_exec_capabilities, execap)
DEFINE_DEVINFO_FETCH(cl_device_svm_capabilities, svmcap)
DEFINE_DEVINFO_FETCH(cl_device_terminate_capability_khr, termcap)

#define DEV_FETCH_LOC(type, var, loc) \
	type var = device_fetch_##type(ret, loc, chk, output)
#define DEV_FETCH(type, var) DEV_FETCH_LOC(type, var, loc)

#define FMT_VAL(ret, fmt, val) if (!ret->err) strbuf_printf(&ret->str, fmt, val)

#define DEFINE_DEVINFO_SHOW(how, type, field, fmt) \
void \
device_info_##how(struct device_info_ret *ret, \
	const struct info_loc *loc, const struct device_info_checks* chk, \
	const struct opt_out *output) \
{ \
	DEV_FETCH(type, val); \
	if (!ret->err) FMT_VAL(ret, fmt, val); \
}

DEFINE_DEVINFO_SHOW(int, cl_uint, u32, "%" PRIu32)
DEFINE_DEVINFO_SHOW(hex, cl_uint, u32, "%#" PRIx32)
DEFINE_DEVINFO_SHOW(long, cl_ulong, u64, "%" PRIu64)
DEFINE_DEVINFO_SHOW(sz, size_t, s, "%" PRIuS)

void
device_info_str(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out* UNUSED(output))
{
	GET_STRING_LOC(ret, loc, clGetDeviceInfo, loc->dev, loc->param.dev);
}

void
device_info_bool(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_bool, val);
	if (!ret->err) {
		const char * const * str = (output->mode == CLINFO_HUMAN ?
			bool_str : bool_raw_str);
		strbuf_printf(&ret->str, "%s", str[val]);
	}
}

void
device_info_bits(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_uint, val);
	if (!ret->err)
		strbuf_printf(&ret->str, "%" PRIu32 " bits (%" PRIu32 " bytes)", val, val/8);
}


size_t strbuf_mem(struct _strbuf *str, cl_ulong val, size_t szval)
{
	double dbl = (double)val;
	size_t sfx = 0;
	while (dbl > 1024 && sfx < memsfx_end) {
		dbl /= 1024;
		++sfx;
	}
	return sprintf(str->buf + szval, " (%.4lg%s)",
		dbl, memsfx[sfx]);
}

void
device_info_mem(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_ulong, val);
	if (!ret->err) {
		size_t szval = strbuf_printf(&ret->str, "%" PRIu64, val);
		if (output->mode == CLINFO_HUMAN && val > 1024)
			strbuf_mem(&ret->str, val, szval);
	}
}

void
device_info_mem_int(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_uint, val);
	if (!ret->err) {
		size_t szval = strbuf_printf(&ret->str, "%" PRIu32, val);
		if (output->mode == CLINFO_HUMAN && val > 1024)
			strbuf_mem(&ret->str, val, szval);
	}
}

void
device_info_mem_sz(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(size_t, val);
	if (!ret->err) {
		size_t szval = strbuf_printf(&ret->str, "%" PRIuS, val);
		if (output->mode == CLINFO_HUMAN && val > 1024)
			strbuf_mem(&ret->str, val, szval);
	}
}

void
device_info_free_mem_amd(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	size_t *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		size_t cursor = 0;
		szval = 0;
		for (cursor = 0; cursor < numval; ++cursor) {
			if (szval > 0) {
				ret->str.buf[szval] = ' ';
				++szval;
			}
			szval += sprintf(ret->str.buf + szval, "%" PRIuS, val[cursor]);
			if (output->mode == CLINFO_HUMAN)
				szval += strbuf_mem(&ret->str, val[cursor]*UINT64_C(1024), szval);
		}
		// TODO: ret->value.??? = val;
	}
	free(val);
}

void
device_info_time_offset(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_ulong, val);
	if (!ret->err) {
		size_t szval = 0;
		time_t time = val/UINT64_C(1000000000);
		szval += strbuf_printf(&ret->str, "%" PRIu64 "ns (", val);
		szval += bufcpy(&ret->str, szval, ctime(&time));
		/* overwrite ctime's newline with the closing parenthesis */
		if (szval < ret->str.sz)
			ret->str.buf[szval - 1] = ')';
	}
}

void
device_info_szptr_sep(struct device_info_ret *ret, const char *human_sep,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	size_t *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		size_t counter = 0;
		set_separator(output->mode == CLINFO_HUMAN ? human_sep : spc_str);
		szval = 0;
		for (counter = 0; counter < numval; ++counter) {
			add_separator(&ret->str, &szval);
			szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "%" PRIuS, val[counter]);
			if (szval >= ret->str.sz) {
				trunc_strbuf(&ret->str);
				break;
			}
		}
		// TODO: ret->value.??? = val;
	}
	free(val);
}

void
device_info_szptr_times(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* chk,
	const struct opt_out *output)
{
	device_info_szptr_sep(ret, times_str, loc, chk, output);
}

void
device_info_szptr_comma(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* chk,
	const struct opt_out *output)
{
	device_info_szptr_sep(ret, comma_str, loc, chk, output);
}

void
getWGsizes(struct device_info_ret *ret, const struct info_loc *loc, size_t *wgm, size_t wgm_sz,
	const struct opt_out* UNUSED(output))
{
	cl_int log_err;

	cl_context_properties ctxpft[] = {
		CL_CONTEXT_PLATFORM, (cl_context_properties)loc->plat,
		0, 0 };
	cl_uint cursor = 0;
	cl_context ctx = NULL;
	cl_program prg = NULL;
	cl_kernel krn = NULL;

	ret->err = CL_SUCCESS;

	ctx = clCreateContext(ctxpft, 1, &loc->dev, NULL, NULL, &ret->err);
	if (REPORT_ERROR(&ret->err_str, ret->err, "create context")) goto out;
	prg = clCreateProgramWithSource(ctx, ARRAY_SIZE(sources), sources, NULL, &ret->err);
	if (REPORT_ERROR(&ret->err_str, ret->err, "create program")) goto out;
	ret->err = clBuildProgram(prg, 1, &loc->dev, NULL, NULL, NULL);
	log_err = REPORT_ERROR(&ret->err_str, ret->err, "build program");

	/* for a program build failure, dump the log to stderr before bailing */
	if (log_err == CL_BUILD_PROGRAM_FAILURE) {
		struct _strbuf logbuf;
		init_strbuf(&logbuf);
		GET_STRING(&logbuf, ret->err,
			clGetProgramBuildInfo, CL_PROGRAM_BUILD_LOG, "CL_PROGRAM_BUILD_LOG", prg, loc->dev);
		if (ret->err == CL_SUCCESS) {
			fflush(stdout);
			fflush(stderr);
			fputs("=== CL_PROGRAM_BUILD_LOG ===\n", stderr);
			fputs(logbuf.buf, stderr);
			fflush(stderr);
		}
		free_strbuf(&logbuf);
	}
	if (ret->err)
		goto out;

	for (cursor = 0; cursor < wgm_sz; ++cursor) {
		strbuf_printf(&ret->str, "sum%u", 1<<cursor);
		if (cursor == 0)
			ret->str.buf[3] = 0; // scalar kernel is called 'sum'
		krn = clCreateKernel(prg, ret->str.buf, &ret->err);
		if (REPORT_ERROR(&ret->err_str, ret->err, "create kernel")) goto out;
		ret->err = clGetKernelWorkGroupInfo(krn, loc->dev, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,
			sizeof(*wgm), wgm + cursor, NULL);
		if (REPORT_ERROR(&ret->err_str, ret->err, "get kernel info")) goto out;
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
}


void
device_info_wg(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	/* preferred workgroup size multiple for each kernel
	 * have not found a platform where the WG multiple changes,
	 * but keep this flexible (this can grow up to 5)
	 */
#define NUM_KERNELS 1
	size_t wgm[NUM_KERNELS] = {0};

	getWGsizes(ret, loc, wgm, NUM_KERNELS, output);
	if (!ret->err) {
		strbuf_printf(&ret->str, "%" PRIuS, wgm[0]);
	}
	ret->value.s = wgm[0];
}

void
device_info_img_sz_2d(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	struct info_loc loc2 = *loc;
	size_t width = 0, height = 0;
	_GET_VAL(ret, loc, height); /* HEIGHT */
	if (!ret->err) {
		RESET_LOC_PARAM(loc2, dev, CL_DEVICE_IMAGE2D_MAX_WIDTH);
		_GET_VAL(ret, &loc2, width);
		if (!ret->err) {
			strbuf_printf(&ret->str, "%" PRIuS "x%" PRIuS, width, height);
		}
	}
	ret->value.u32v.s[0] = width;
	ret->value.u32v.s[1] = height;
}

void
device_info_img_sz_intel_planar_yuv(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	struct info_loc loc2 = *loc;
	size_t width = 0, height = 0;
	_GET_VAL(ret, loc, height); /* HEIGHT */
	if (!ret->err) {
		RESET_LOC_PARAM(loc2, dev, CL_DEVICE_PLANAR_YUV_MAX_WIDTH_INTEL);
		_GET_VAL(ret, &loc2, width);
		if (!ret->err) {
			strbuf_printf(&ret->str, "%" PRIuS "x%" PRIuS, width, height);
		}
	}
	ret->value.u32v.s[0] = width;
	ret->value.u32v.s[1] = height;
}


void
device_info_img_sz_3d(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	struct info_loc loc2 = *loc;
	size_t width = 0, height = 0, depth = 0;
	_GET_VAL(ret, loc, height); /* HEIGHT */
	if (!ret->err) {
		RESET_LOC_PARAM(loc2, dev, CL_DEVICE_IMAGE3D_MAX_WIDTH);
		_GET_VAL(ret, &loc2, width);
		if (!ret->err) {
			RESET_LOC_PARAM(loc2, dev, CL_DEVICE_IMAGE3D_MAX_DEPTH);
			_GET_VAL(ret, &loc2, depth);
			if (!ret->err) {
				strbuf_printf(&ret->str, "%" PRIuS "x%" PRIuS "x%" PRIuS,
					width, height, depth);
			}
		}
	}
	ret->value.u32v.s[0] = width;
	ret->value.u32v.s[1] = height;
	ret->value.u32v.s[2] = depth;
}


void
device_info_devtype(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_device_type, val);
	if (!ret->err) {
		/* iterate over device type strings, appending their textual form
		 * to ret->str */
		cl_uint i = (cl_uint)actual_devtype_count;
		const char * const *devstr = (output->mode == CLINFO_HUMAN ?
			device_type_str : device_type_raw_str);
		size_t szval = 0;
		set_separator(output->mode == CLINFO_HUMAN ? comma_str : vbar_str);
		for (; i > 0; --i) {
			/* assemble CL_DEVICE_TYPE_* from index i */
			cl_device_type cur = (cl_device_type)(1) << (i-1);
			if (val & cur) {
				/* match: add separator if not first match */
				add_separator(&ret->str, &szval);
				szval += bufcpy(&ret->str, szval, devstr[i]);
			}
		}
		/* check for extra bits */
		if (szval < ret->str.sz) {
			cl_device_type known_mask = ((cl_device_type)(1) << actual_devtype_count) - 1;
			cl_device_type extra = val & ~known_mask;
			if (extra) {
				add_separator(&ret->str, &szval);
				szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "%#" PRIx64, extra);
			}
		}
	}
}

void
device_info_cachetype(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_device_mem_cache_type, val);
	if (!ret->err) {
		const char * const *ar = (output->mode == CLINFO_HUMAN ?
			cache_type_str : cache_type_raw_str);
		bufcpy(&ret->str, 0, ar[val]);
	}
}

void
device_info_lmemtype(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_device_local_mem_type, val);
	if (!ret->err) {
		const char * const *ar = (output->mode == CLINFO_HUMAN ?
			lmem_type_str : lmem_type_raw_str);
		bufcpy(&ret->str, 0, ar[val]);
	}
	ret->value.lmemtype = val;
}

/* cl_arm_core_id */
void
device_info_core_ids(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_ulong, val);

	if (!ret->err) {
		/* The value is a bitfield where each set bit corresponds to a code ID
		 * value that can be returned by the device-side function. We print them
		 * here as ranges, such as 0-4, 8-12 */
		set_separator(empty_str);
		size_t szval = 0;
		int range_start = -1;
		int cur_bit = 0;
#define CORE_ID_END 64
		do {
			/* Find the start of the range */
			while ((cur_bit < CORE_ID_END) && !(val >> cur_bit))
				++cur_bit;
			range_start = cur_bit++;

			/* Find the end of the range */
			while ((cur_bit < CORE_ID_END) && (val >> cur_bit))
				++cur_bit;

			/* print the range [range_start, cur_bit[ */
			if (range_start >= 0) {
				szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1,
					"%s%d", sep, range_start);
				if (cur_bit - range_start > 1)
					szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1,
						"-%d", cur_bit - 1);
				set_separator(comma_str);
			}
		} while (cur_bit < CORE_ID_END);
	}
	ret->value.u64 = val;
}

/* stringify a cl_device_topology_amd */
void devtopo_str(struct device_info_ret *ret, const cl_device_topology_amd *devtopo)
{
	switch (devtopo->raw.type) {
	case 0:
		/* leave empty */
		break;
	case CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD:
		strbuf_printf(&ret->str, "PCI-E, %02x:%02x.%u",
			(cl_uchar)(devtopo->pcie.bus),
			devtopo->pcie.device, devtopo->pcie.function);
		break;
	default:
		strbuf_printf(&ret->str, "<unknown (%u): %u %u %u %u %u>",
			devtopo->raw.type,
			devtopo->raw.data[0], devtopo->raw.data[1],
			devtopo->raw.data[2],
			devtopo->raw.data[3], devtopo->raw.data[4]);
	}
}

void
device_info_devtopo_amd(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_device_topology_amd, val);
	/* TODO how to do this in CLINFO_RAW mode */
	if (!ret->err) {
		devtopo_str(ret, &val);
	}
}

/* we assemble a cl_device_topology_amd struct from the NVIDIA info */
void
device_info_devtopo_nv(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	struct info_loc loc2 = *loc;
	cl_device_topology_amd devtopo;
	DEV_FETCH(cl_uint, val); /* CL_DEVICE_PCI_BUS_ID_NV */
	if (!ret->err) {
		devtopo.raw.type = CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD;
		devtopo.pcie.bus = val & 0xff;
		RESET_LOC_PARAM(loc2, dev, CL_DEVICE_PCI_SLOT_ID_NV);
		_GET_VAL(ret, &loc2, val);

		if (!ret->err) {
			devtopo.pcie.device = (val >> 3) & 0xff;
			devtopo.pcie.function = val & 7;
			devtopo_str(ret, &devtopo);
		}
		ret->value.devtopo = devtopo;
	}
}

/* NVIDIA Compute Capability */
void
device_info_cc_nv(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	struct info_loc loc2 = *loc;
	cl_uint major = 0, minor = 0;
	_GET_VAL(ret, loc, major); /* MAJOR */
	if (!ret->err) {
		RESET_LOC_PARAM(loc2, dev, CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV);
		_GET_VAL(ret, &loc2, minor);
		if (!ret->err) {
			strbuf_printf(&ret->str, "%" PRIu32 ".%" PRIu32 "", major, minor);
		}
	}
	ret->value.u32v.s[0] = major;
	ret->value.u32v.s[1] = minor;
}

/* AMD GFXIP */
void
device_info_gfxip_amd(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	struct info_loc loc2 = *loc;
	cl_uint major = 0, minor = 0;
	_GET_VAL(ret, loc, major); /* MAJOR */
	if (!ret->err) {
		RESET_LOC_PARAM(loc2, dev, CL_DEVICE_GFXIP_MINOR_AMD);
		_GET_VAL(ret, &loc2, minor);
		if (!ret->err) {
			strbuf_printf(&ret->str, "%" PRIu32 ".%" PRIu32 "", major, minor);
		}
	}
	ret->value.u32v.s[0] = major;
	ret->value.u32v.s[1] = minor;
}


/* Device Partition, CLINFO_HUMAN header */
void
device_info_partition_header(struct device_info_ret *ret,
	const struct info_loc *UNUSED(loc), const struct device_info_checks *chk,
	const struct opt_out* UNUSED(output))
{
	cl_bool is_12 = dev_is_12(chk);
	cl_bool has_fission = dev_has_fission(chk);
	size_t szval = strbuf_printf(&ret->str, "(%s%s%s%s)",
		(is_12 ? core : empty_str),
		(is_12 && has_fission ? comma_str : empty_str),
		chk->has_fission,
		(!(is_12 || has_fission) ? na : empty_str));

	ret->err = CL_SUCCESS;

	if (szval >= ret->str.sz)
		trunc_strbuf(&ret->str);
}

/* Device partition properties */
void
device_info_partition_types(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	size_t numval = 0, szval = 0, cursor = 0, slen = 0;
	cl_device_partition_property *val = NULL;
	const char * const *ptstr = (output->mode == CLINFO_HUMAN ?
		partition_type_str : partition_type_raw_str);

	set_separator(output->mode == CLINFO_HUMAN ? comma_str : vbar_str);

	GET_VAL_ARRAY(ret, loc);

	szval = 0;
	if (!ret->err) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			add_separator(&ret->str, &szval);

			switch (val[cursor]) {
			case 0: str_idx = 0; break;
			case CL_DEVICE_PARTITION_EQUALLY: str_idx = 1; break;
			case CL_DEVICE_PARTITION_BY_COUNTS: str_idx = 2; break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN: str_idx = 3; break;
			case CL_DEVICE_PARTITION_BY_NAMES_INTEL: str_idx = 4; break;
			default:
				szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "by <unknown> (%#" PRIxPTR ")", val[cursor]);
				break;
			}
			if (str_idx >= 0) {
				/* string length, minus _EXT */
				slen = strlen(ptstr[str_idx]);
				if (output->mode == CLINFO_RAW && str_idx > 0)
					slen -= 4;
				szval += bufcpy_len(&ret->str, szval, ptstr[str_idx], slen);
			}
			if (szval >= ret->str.sz) {
				trunc_strbuf(&ret->str);
				break;
			}
		}
		// TODO ret->value.??? = val
	}
	free(val);
}

void
device_info_partition_types_ext(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	size_t numval = 0, szval = 0, cursor = 0, slen = 0;
	cl_device_partition_property_ext *val = NULL;
	const char * const *ptstr = (output->mode == CLINFO_HUMAN ?
		partition_type_str : partition_type_raw_str);

	set_separator(output->mode == CLINFO_HUMAN ? comma_str : vbar_str);

	GET_VAL_ARRAY(ret, loc);

	szval = 0;
	if (!ret->err) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			add_separator(&ret->str, &szval);

			switch (val[cursor]) {
			case 0: str_idx = 0; break;
			case CL_DEVICE_PARTITION_EQUALLY_EXT: str_idx = 1; break;
			case CL_DEVICE_PARTITION_BY_COUNTS_EXT: str_idx = 2; break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT: str_idx = 3; break;
			case CL_DEVICE_PARTITION_BY_NAMES_EXT: str_idx = 4; break;
			default:
				szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "by <unknown> (%#" PRIx64 ")", val[cursor]);
				break;
			}
			if (str_idx >= 0) {
				/* string length */
				slen = strlen(ptstr[str_idx]);
				strncpy(ret->str.buf + szval, ptstr[str_idx], slen);
				szval += slen;
			}
			if (szval >= ret->str.sz) {
				trunc_strbuf(&ret->str);
				break;
			}
		}
		if (szval < ret->str.sz)
			ret->str.buf[szval] = '\0';
		// TODO ret->value.??? = val
	}
	free(val);
}


/* Device partition affinity domains */
void
device_info_partition_affinities(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_device_affinity_domain, val);
	if (!ret->err && val) {
		/* iterate over affinity domain strings appending their textual form
		 * to ret->str */
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *affstr = (output->mode == CLINFO_HUMAN ?
			affinity_domain_str : affinity_domain_raw_str);
		set_separator(output->mode == CLINFO_HUMAN ? comma_str : vbar_str);
		for (i = 0; i < affinity_domain_count; ++i) {
			cl_device_affinity_domain cur = (cl_device_affinity_domain)(1) << i;
			if (val & cur) {
				/* match: add separator if not first match */
				add_separator(&ret->str, &szval);
				szval += bufcpy(&ret->str, szval, affstr[i]);
			}
			if (szval >= ret->str.sz)
				break;
		}
		/* check for extra bits */
		if (szval < ret->str.sz) {
			cl_device_affinity_domain known_mask = ((cl_device_affinity_domain)(1) << affinity_domain_count) - 1;
			cl_device_affinity_domain extra = val & ~known_mask;
			if (extra) {
				add_separator(&ret->str, &szval);
				szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "%#" PRIx64, extra);
			}
		}
	}
}

void
device_info_partition_affinities_ext(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	size_t numval = 0, szval = 0, cursor = 0, slen = 0;
	cl_device_partition_property_ext *val = NULL;
	const char * const *ptstr = (output->mode == CLINFO_HUMAN ?
		affinity_domain_ext_str : affinity_domain_raw_ext_str);

	set_separator(output->mode == CLINFO_HUMAN ? comma_str : vbar_str);

	GET_VAL_ARRAY(ret, loc);

	szval = 0;
	if (!ret->err) {
		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			add_separator(&ret->str, &szval);

			switch (val[cursor]) {
			case CL_AFFINITY_DOMAIN_NUMA_EXT: str_idx = 0; break;
			case CL_AFFINITY_DOMAIN_L4_CACHE_EXT: str_idx = 1; break;
			case CL_AFFINITY_DOMAIN_L3_CACHE_EXT: str_idx = 2; break;
			case CL_AFFINITY_DOMAIN_L2_CACHE_EXT: str_idx = 3; break;
			case CL_AFFINITY_DOMAIN_L1_CACHE_EXT: str_idx = 4; break;
			case CL_AFFINITY_DOMAIN_NEXT_FISSIONABLE_EXT: str_idx = 5; break;
			default:
				szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "<unknown> (%#" PRIx64 ")", val[cursor]);
				break;
			}
			if (str_idx >= 0) {
				/* string length */
				const char *str = ptstr[str_idx];
				slen = strlen(str);
				strncpy(ret->str.buf + szval, str, slen);
				szval += slen;
			}
			if (szval >= ret->str.sz) {
				trunc_strbuf(&ret->str);
				break;
			}
		}
		ret->str.buf[szval] = '\0';
		// TODO: ret->value.??? = val
	}
	free(val);
}

/* Preferred / native vector widths */
void
device_info_vecwidth(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out *output)
{
	struct info_loc loc2 = *loc;
	cl_uint preferred = 0, native = 0;
	_GET_VAL(ret, loc, preferred);
	if (!ret->err) {
		/* we get called with PREFERRED, NATIVE is at +0x30 offset, except for HALF,
		 * which is at +0x08 */
		loc2.param.dev +=
			(loc2.param.dev == CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF ? 0x08 : 0x30);
		/* TODO update loc2.sname */
		_GET_VAL(ret, &loc2, native);

		if (!ret->err) {
			size_t szval = 0;
			const char *ext = (loc2.param.dev == CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF ?
				chk->has_half : (loc2.param.dev == CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE ?
				chk->has_double : NULL));
			szval = strbuf_printf(&ret->str, "%8u / %-8u", preferred, native);
			if (ext)
				sprintf(ret->str.buf + szval, " (%s)", *ext ? ext : na);
		}
	}
	ret->value.u32v.s[0] = preferred;
	ret->value.u32v.s[1] = native;
}

/* Floating-point configurations */
void
device_info_fpconf(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out *output)
{
	/* When in HUMAN output, we are called unconditionally,
	 * so we have to do some manual checks ourselves */
	const cl_bool get_it = (output->mode != CLINFO_HUMAN) ||
		(loc->param.dev == CL_DEVICE_SINGLE_FP_CONFIG) ||
		(loc->param.dev == CL_DEVICE_HALF_FP_CONFIG && dev_has_half(chk)) ||
		(loc->param.dev == CL_DEVICE_DOUBLE_FP_CONFIG && dev_has_double(chk));

	DEV_FETCH(cl_device_fp_config, val);
	/* Sanitize! */
	if (ret->err && !get_it) {
		ret->err = CL_SUCCESS;
		val = 0;
	}


	if (!ret->err) {
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *fpstr = (output->mode == CLINFO_HUMAN ?
			fp_conf_str : fp_conf_raw_str);
		set_separator(vbar_str);
		if (output->mode == CLINFO_HUMAN) {
			const char *why = na;
			switch (loc->param.dev) {
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
				fprintf(stderr, "unsupported floating-point configuration parameter %s\n", loc->pname);
			}
			/* show 'why' it's being shown */
			szval += strbuf_printf(&ret->str, "(%s)", why);
		}
		if (get_it) {
			size_t num_flags = fp_conf_count;
			/* The last flag, CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT is only considered
			 * in the single-precision case. half and double don't consider it,
			 * so we skip it altogether */
			if (loc->param.dev != CL_DEVICE_SINGLE_FP_CONFIG)
				num_flags -= 1;

			for (i = 0; i < num_flags; ++i) {
				cl_device_fp_config cur = (cl_device_fp_config)(1) << i;
				if (output->mode == CLINFO_HUMAN) {
					szval += sprintf(ret->str.buf + szval, "\n%s" I2_STR "%s",
						line_pfx, fpstr[i], bool_str[!!(val & cur)]);
				} else if (val & cur) {
					add_separator(&ret->str, &szval);
					szval += bufcpy(&ret->str, szval, fpstr[i]);
				}
			}
		}
	}
}

/* Queue properties */
void
device_info_qprop(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out *output)
{
	DEV_FETCH(cl_command_queue_properties, val);
	if (!ret->err) {
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *qpstr = (output->mode == CLINFO_HUMAN ?
			queue_prop_str : queue_prop_raw_str);
		set_separator(vbar_str);
		for (i = 0; i < queue_prop_count; ++i) {
			cl_command_queue_properties cur = (cl_command_queue_properties)(1) << i;
			if (output->mode == CLINFO_HUMAN) {
				szval += sprintf(ret->str.buf + szval, "\n%s" I2_STR "%s",
					line_pfx, qpstr[i], bool_str[!!(val & cur)]);
			} else if (val & cur) {
				add_separator(&ret->str, &szval);
				szval += bufcpy(&ret->str, szval, qpstr[i]);
			}
		}
		if (output->mode == CLINFO_HUMAN && loc->param.dev == CL_DEVICE_QUEUE_PROPERTIES &&
			dev_has_intel_local_thread(chk))
			sprintf(ret->str.buf + szval, "\n%s" I2_STR "%s",
				line_pfx, "Local thread execution (Intel)", bool_str[CL_TRUE]);
	}
}

/* Execution capbilities */
void
device_info_execap(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_device_exec_capabilities, val);
	if (!ret->err) {
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *qpstr = (output->mode == CLINFO_HUMAN ?
			execap_str : execap_raw_str);
		set_separator(vbar_str);
		for (i = 0; i < execap_count; ++i) {
			cl_device_exec_capabilities cur = (cl_device_exec_capabilities)(1) << i;
			if (output->mode == CLINFO_HUMAN) {
				szval += sprintf(ret->str.buf + szval, "\n%s" I2_STR "%s",
					line_pfx, qpstr[i], bool_str[!!(val & cur)]);
			} else if (val & cur) {
				add_separator(&ret->str, &szval);
				szval += bufcpy(&ret->str, szval, qpstr[i]);
			}
		}
	}
}

/* Arch bits and endianness (HUMAN) */
void
device_info_arch(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_uint, bits);
	struct info_loc loc2 = *loc;
	RESET_LOC_PARAM(loc2, dev, CL_DEVICE_ENDIAN_LITTLE);

	if (!ret->err) {
		DEV_FETCH_LOC(cl_bool, val, &loc2);
		if (!ret->err) {
			strbuf_printf(&ret->str, "%" PRIu32 ", %s", bits, endian_str[val]);
		}
	}
}

/* SVM capabilities */
void
device_info_svm_cap(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out *output)
{
	const cl_bool is_20 = dev_is_20(chk);
	const cl_bool checking_core = (loc->param.dev == CL_DEVICE_SVM_CAPABILITIES);
	const cl_bool has_amd_svm = (checking_core && dev_has_amd_svm(chk));
	DEV_FETCH(cl_device_svm_capabilities, val);

	if (!ret->err) {
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *scstr = (output->mode == CLINFO_HUMAN ?
			svm_cap_str : svm_cap_raw_str);
		set_separator(vbar_str);
		if (output->mode == CLINFO_HUMAN && checking_core) {
			/* show 'why' it's being shown */
			szval += strbuf_printf(&ret->str, "(%s%s%s)",
				(is_20 ? core : empty_str),
				(is_20 && has_amd_svm ? comma_str : empty_str),
				chk->has_amd_svm);
		}
		for (i = 0; i < svm_cap_count; ++i) {
			cl_device_svm_capabilities cur = (cl_device_svm_capabilities)(1) << i;
			if (output->mode == CLINFO_HUMAN) {
				szval += sprintf(ret->str.buf + szval, "\n%s" I2_STR "%s",
					line_pfx, scstr[i], bool_str[!!(val & cur)]);
			} else if (val & cur) {
				add_separator(&ret->str, &szval);
				szval += bufcpy(&ret->str, szval, scstr[i]);
			}
		}
	}
}

/* Device terminate capability */
void
device_info_terminate_capability(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_device_terminate_capability_khr, val);
	if (!ret->err && val) {
		/* iterate over terminate capability strings appending their textual form
		 * to ret->str */
		size_t szval = 0;
		cl_uint i = 0;
		const char * const *capstr = (output->mode == CLINFO_HUMAN ?
			terminate_capability_str : terminate_capability_raw_str);
		set_separator(output->mode == CLINFO_HUMAN ? comma_str : vbar_str);
		for (i = 0; i < terminate_capability_count; ++i) {
			cl_device_terminate_capability_khr cur = (cl_device_terminate_capability_khr)(1) << i;
			if (val & cur) {
				/* match: add separator if not first match */
				add_separator(&ret->str, &szval);
				szval += bufcpy(&ret->str, szval, capstr[i]);
			}
			if (szval >= ret->str.sz)
				break;
		}
		/* check for extra bits */
		if (szval < ret->str.sz) {
			cl_device_terminate_capability_khr known_mask = ((cl_device_terminate_capability_khr)(1) << terminate_capability_count) - 1;
			cl_device_terminate_capability_khr extra = val & ~known_mask;
			if (extra) {
				add_separator(&ret->str, &szval);
				szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "%#" PRIx64, extra);
			}
		}
	}
}

void
device_info_p2p_dev_list(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out* UNUSED(output))
{
	cl_device_id *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		size_t cursor = 0;
		szval = 0;
		for (cursor= 0; cursor < numval; ++cursor) {
			if (szval > 0) {
				ret->str.buf[szval] = ' ';
				++szval;
			}
			szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "%p", (void*)val[cursor]);
		}
		// TODO: ret->value.??? = val;
	}
	free(val);
}

void
device_info_interop_list(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_uint *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		size_t cursor = 0;
		const cl_interop_name *interop_name_end = cl_interop_names + num_known_interops;
		cl_uint human_raw = output->mode - CLINFO_HUMAN;
		const char *groupsep = (output->mode == CLINFO_HUMAN ? comma_str : vbar_str);
		cl_bool first = CL_TRUE;
		szval = 0;
		for (cursor = 0; cursor < numval; ++cursor) {
			cl_uint current = val[cursor];
			if (!current && cursor < numval - 1) {
				/* A null value is used as group terminator, but we only print it
				 * if it's not the final one
				 */
				szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "%s", groupsep);
				first = CL_TRUE;
			}
			if (current) {
				cl_bool found = CL_FALSE;
				const cl_interop_name *n = cl_interop_names;

				if (!first) {
					ret->str.buf[szval] = ' ';
					++szval;
				}

				while (n < interop_name_end) {
					if (current >= n->from && current <= n->to) {
						found = CL_TRUE;
						break;
					}
					++n;
				}
				if (found) {
					cl_uint i = current - n->from;
					szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "%s", n->value[i][human_raw]);
				} else {
					szval += snprintf(ret->str.buf + szval, ret->str.sz - szval - 1, "%#" PRIx32, val[cursor]);
				}
				first = CL_FALSE;
			}
			if (szval >= ret->str.sz) {
				trunc_strbuf(&ret->str);
				break;
			}
		}
		// TODO: ret->value.??? = val;
	}
	free(val);
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
	/* pointer to function that retrieves the parameter */
	void (*show_func)(struct device_info_ret *,
		const struct info_loc *, const struct device_info_checks *,
		const struct opt_out *);
	/* pointer to function that checks if the parameter should be retrieved */
	cl_bool (*check_func)(const struct device_info_checks *);
};

#define DINFO_SFX(symbol, name, sfx, typ) symbol, #symbol, name, sfx, device_info_##typ
#define DINFO(symbol, name, typ) symbol, #symbol, name, NULL, device_info_##typ

struct device_info_traits dinfo_traits[] = {
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NAME, "Device Name", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_VENDOR, "Device Vendor", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_VENDOR_ID, "Device Vendor ID", hex), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_VERSION, "Device Version", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DRIVER_VERSION, "Driver Version", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_OPENCL_C_VERSION, "Device OpenCL C Version", str), dev_is_11 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_EXTENSIONS, "Device Extensions", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TYPE, "Device Type", devtype), NULL },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_BOARD_NAME_AMD, "Device Board Name (AMD)", str), dev_has_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TOPOLOGY_AMD, "Device Topology (AMD)", devtopo_amd), dev_has_amd },

	/* Device Topology (NV) is multipart, so different for HUMAN and RAW */
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PCI_BUS_ID_NV, "Device Topology (NV)", devtopo_nv), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PCI_BUS_ID_NV, "Device PCI bus (NV)", int), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PCI_SLOT_ID_NV, "Device PCI slot (NV)", int), dev_has_nv },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_PROFILE, "Device Profile", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVAILABLE, "Device Available", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_COMPILER_AVAILABLE, "Compiler Available", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LINKER_AVAILABLE, "Linker Available", bool), dev_is_12 },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_COMPUTE_UNITS, "Max compute units", int), NULL },
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_COMPUTE_UNITS_BITFIELD, "Available core IDs", core_ids), dev_has_arm_core_id_v2 },
	{ CLINFO_RAW, DINFO(CL_DEVICE_COMPUTE_UNITS_BITFIELD, "Available core IDs", long), dev_has_arm_core_id_v2 },
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
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_WORK_ITEM_SIZES, "Max work item sizes", szptr_times), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_WORK_GROUP_SIZE, "Max work group size", sz), NULL },

	/* cl_amd_device_attribute_query v4 */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_AMD, "Preferred work group size (AMD)", sz), dev_has_amd_v4 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_WORK_GROUP_SIZE_AMD, "Max work group size (AMD)", sz), dev_has_amd_v4 },

	{ CLINFO_BOTH, DINFO(CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, "Preferred work group size multiple", wg), dev_has_compiler_11 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_WARP_SIZE_NV, "Warp size (NV)", int), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_WAVEFRONT_WIDTH_AMD, "Wavefront width (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_NUM_SUB_GROUPS, "Max sub-groups per work group", int), dev_is_21 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_NAMED_BARRIER_COUNT_KHR, "Max named sub-group barriers", int), dev_has_subgroup_named_barrier },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SUB_GROUP_SIZES_INTEL, "Sub-group sizes (Intel)", szptr_comma), dev_has_intel_required_subgroup_size },

	/* Preferred/native vector widths: header is only presented in HUMAN case, that also pairs
	 * PREFERRED and NATIVE in a single line */
#define DINFO_VECWIDTH(Type, type) \
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PREFERRED_VECTOR_WIDTH_##Type, INDENT #type, vecwidth), NULL }, \
	{ CLINFO_RAW, DINFO(CL_DEVICE_PREFERRED_VECTOR_WIDTH_##Type, INDENT #type, int), NULL }, \
	{ CLINFO_RAW, DINFO(CL_DEVICE_NATIVE_VECTOR_WIDTH_##Type, INDENT #type, int), dev_is_11 }

	{ CLINFO_HUMAN, DINFO(CL_FALSE, "Preferred / native vector sizes", str), NULL },
	DINFO_VECWIDTH(CHAR, char),
	DINFO_VECWIDTH(SHORT, short),
	DINFO_VECWIDTH(INT, int),
	DINFO_VECWIDTH(LONG, long),
	DINFO_VECWIDTH(HALF, half), /* this should be excluded for 1.0 */
	DINFO_VECWIDTH(FLOAT, float),
	DINFO_VECWIDTH(DOUBLE, double),

	/* Floating point configurations */
#define DINFO_FPCONF(Type, type, cond) \
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_##Type##_FP_CONFIG, #type "-precision Floating-point support", fpconf), NULL }, \
	{ CLINFO_RAW, DINFO(CL_DEVICE_##Type##_FP_CONFIG, #type "-precision Floating-point support", fpconf), cond }

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
	{ CLINFO_BOTH, DINFO(CL_DEVICE_HOST_UNIFIED_MEMORY, "Unified memory for Host and Device", bool), dev_is_11 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_INTEGRATED_MEMORY_NV, "Integrated memory (NV)", bool), dev_has_nv },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_SVM_CAPABILITIES, "Shared Virtual Memory (SVM) capabilities", svm_cap), dev_has_svm },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SVM_CAPABILITIES_ARM, "Shared Virtual Memory (SVM) capabilities (ARM)", svm_cap), dev_has_arm_svm },

	/* Alignment */
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE, "Minimum alignment for any data type", bytes_str, int), NULL },
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_MEM_BASE_ADDR_ALIGN, "Alignment of base address", bits), NULL },
	{ CLINFO_RAW, DINFO(CL_DEVICE_MEM_BASE_ADDR_ALIGN, "Alignment of base address", int), NULL },

	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_PAGE_SIZE_QCOM, "Page size (QCOM)", bytes_str, sz), dev_has_qcom_ext_host_ptr },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_EXT_MEM_PADDING_IN_BYTES_QCOM, "External memory padding (QCOM)", bytes_str, sz), dev_has_qcom_ext_host_ptr },

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
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GLOBAL_MEM_CACHE_SIZE, "Global Memory cache size", mem), dev_has_cache },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE, "Global Memory cache line size", " bytes", int), dev_has_cache },

	/* Image support */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_IMAGE_SUPPORT, "Image support", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_SAMPLERS, INDENT "Max number of samplers per kernel", int), dev_has_images },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_IMAGE_MAX_BUFFER_SIZE, INDENT "Max size for 1D images from buffer", pixels_str, sz), dev_has_images_12 },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_IMAGE_MAX_ARRAY_SIZE, INDENT "Max 1D or 2D image array size", images_str, sz), dev_has_images_12 },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT, INDENT "Base address alignment for 2D image buffers", bytes_str, sz), dev_has_image2d_buffer },
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_IMAGE_PITCH_ALIGNMENT, INDENT "Pitch alignment for 2D image buffers", pixels_str, sz), dev_has_image2d_buffer },

	/* Image dimensions are split for RAW, combined for HUMAN */
	{ CLINFO_HUMAN, DINFO_SFX(CL_DEVICE_IMAGE2D_MAX_HEIGHT, INDENT "Max 2D image size",  pixels_str, img_sz_2d), dev_has_images },
	{ CLINFO_RAW, DINFO(CL_DEVICE_IMAGE2D_MAX_HEIGHT, INDENT "Max 2D image height",  sz), dev_has_images },
	{ CLINFO_RAW, DINFO(CL_DEVICE_IMAGE2D_MAX_WIDTH, INDENT "Max 2D image width",  sz), dev_has_images },
	{ CLINFO_HUMAN, DINFO_SFX(CL_DEVICE_PLANAR_YUV_MAX_HEIGHT_INTEL, INDENT "Max planar YUV image size",  pixels_str, img_sz_2d), dev_has_intel_planar_yuv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PLANAR_YUV_MAX_HEIGHT_INTEL, INDENT "Max planar YUV image height",  sz), dev_has_intel_planar_yuv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PLANAR_YUV_MAX_WIDTH_INTEL, INDENT "Max planar YUV image width",  sz), dev_has_intel_planar_yuv },
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
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_CONSTANT_ARGS, "Max number of constant args", int), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, "Max constant buffer size", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PREFERRED_CONSTANT_BUFFER_SIZE_AMD, "Preferred constant buffer size (AMD)", mem_sz), dev_has_amd_v4 },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_PARAMETER_SIZE, "Max size of kernel argument", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ATOMIC_COUNTERS_EXT, "Max number of atomic counters", sz), dev_has_atomic_counters },

	/* Queue properties */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_PROPERTIES, "Queue properties", qprop), dev_not_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_HOST_PROPERTIES, "Queue properties (on host)", qprop), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES, "Queue properties (on device)", qprop), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE, INDENT "Preferred size", mem), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE, INDENT "Max size", mem), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ON_DEVICE_QUEUES, "Max queues on device", int), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ON_DEVICE_EVENTS, "Max events on device", int), dev_is_20 },

	/* Terminate context */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TERMINATE_CAPABILITY_KHR_1x, "Terminate capability (1.2 define)", terminate_capability), dev_has_terminate_context },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TERMINATE_CAPABILITY_KHR_2x, "Terminate capability (2.x define)", terminate_capability), dev_has_terminate_context },

	/* Interop */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PREFERRED_INTEROP_USER_SYNC, "Prefer user sync for interop", bool), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_SIMULTANEOUS_INTEROPS_INTEL, "Number of simultaneous interops (Intel)", int), dev_has_simultaneous_sharing },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SIMULTANEOUS_INTEROPS_INTEL, "Simultaneous interops", interop_list), dev_has_simultaneous_sharing },

	/* P2P buffer copy */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_P2P_DEVICES_AMD, "Number of P2P devices (AMD)", int), dev_has_p2p },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_P2P_DEVICES_AMD, "P2P devices (AMD)", p2p_dev_list), dev_has_p2p },

	/* Profiling resolution */
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_PROFILING_TIMER_RESOLUTION, "Profiling timer resolution", "ns", sz), NULL },
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PROFILING_TIMER_OFFSET_AMD, "Profiling timer offset since Epoch (AMD)", time_offset), dev_has_amd },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PROFILING_TIMER_OFFSET_AMD, "Profiling timer offset since Epoch (AMD)", long), dev_has_amd },

	/* Kernel execution capabilities */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_EXECUTION_CAPABILITIES, "Execution capabilities", execap), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SUB_GROUP_INDEPENDENT_FORWARD_PROGRESS, INDENT "Sub-group independent forward progress", bool), dev_is_21 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_THREAD_TRACE_SUPPORTED_AMD, INDENT "Thread trace supported (AMD)", bool), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_KERNEL_EXEC_TIMEOUT_NV, INDENT "Kernel execution timeout (NV)", bool), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GPU_OVERLAP_NV, "Concurrent copy and kernel execution (NV)", bool), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT_NV, INDENT "Number of async copy engines", int), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVAILABLE_ASYNC_QUEUES_AMD, INDENT "Number of async queues (AMD)", int), dev_has_amd_v4 },
	/* TODO FIXME undocumented, experimental */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_REAL_TIME_COMPUTE_QUEUES_AMD, INDENT "Max real-time compute queues (AMD)", int), dev_has_amd_v4 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_REAL_TIME_COMPUTE_UNITS_AMD, INDENT "Max real-time compute units (AMD)", int), dev_has_amd_v4 },

	/* TODO: this should tell if it's being done due to the device being 2.1 or due to it having the extension */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_IL_VERSION, INDENT "IL version", str), dev_has_il },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SPIR_VERSIONS, INDENT "SPIR versions", str), dev_has_spir },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PRINTF_BUFFER_SIZE, "printf() buffer size", mem_sz), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_BUILT_IN_KERNELS, "Built-in kernels", str), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ME_VERSION_INTEL, "Motion Estimation accelerator version (Intel)", int), dev_has_intel_AME },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVC_ME_VERSION_INTEL, INDENT "Device-side AVC Motion Estimation version", int), dev_has_intel_AVC_ME },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVC_ME_SUPPORTS_TEXTURE_SAMPLER_USE_INTEL, INDENT INDENT "Supports texture sampler use", bool), dev_has_intel_AVC_ME },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVC_ME_SUPPORTS_PREEMPTION_INTEL, INDENT INDENT "Supports preemption", bool), dev_has_intel_AVC_ME },
};

/* Process all the device info in the traits, except if param_whitelist is not NULL,
 * in which case only those in the whitelist will be processed.
 * If present, the whitelist should be sorted in the order of appearance of the parameters
 * in the traits table, and terminated by the value CL_FALSE
 */

void
printDeviceInfo(cl_device_id dev, const struct platform_list *plist, cl_uint p,
	const cl_device_info *param_whitelist, /* list of device info to process, or NULL */
	const struct opt_out *output)
{
	char *extensions = NULL;

	/* pointer to the traits for CL_DEVICE_EXTENSIONS */
	const struct device_info_traits *extensions_traits = NULL;

	struct device_info_checks chk;
	struct device_info_ret ret;
	struct info_loc loc;

	memset(&chk, 0, sizeof(chk));
	chk.pinfo_checks = plist->platform_checks + p;
	chk.dev_version = 10;

	INIT_RET(ret, "device");

	reset_loc(&loc, __func__);
	loc.plat = plist->platform[p];
	loc.dev = dev;

	for (loc.line = 0; loc.line < ARRAY_SIZE(dinfo_traits); ++loc.line) {

		const struct device_info_traits *traits = dinfo_traits + loc.line;

		/* checked is true if there was no condition to check for, or if the
		 * condition was satisfied
		 */
		int checked = !(traits->check_func && !traits->check_func(&chk));

		loc.sname = traits->sname;
		loc.pname = (output->mode == CLINFO_HUMAN ?
			traits->pname : traits->sname);
		loc.param.dev = traits->param;

		/* Whitelist check: finish if done traversing the list,
		 * skip current param if it's not the right one
		 */
		if ((output->cond == COND_PROP_CHECK || output->brief) && param_whitelist) {
			if (*param_whitelist == CL_FALSE)
				break;
			if (traits->param != *param_whitelist)
				continue;
			++param_whitelist;
		}

		/* skip if it's not for this output mode */
		if (!(output->mode & traits->output_mode))
			continue;

		if (output->cond == COND_PROP_CHECK && !checked)
			continue;

		cur_sfx = (output->mode == CLINFO_HUMAN && traits->sfx) ? traits->sfx : empty_str;

		ret.str.buf[0] = '\0';
		ret.err_str.buf[0] = '\0';

		/* Handle headers */
		if (traits->param == CL_FALSE) {
			ret.err = CL_SUCCESS;
			show_strbuf(&ret.str, loc.pname, 0, ret.err);
			continue;
		}

		traits->show_func(&ret, &loc, &chk, output);

		if (traits->param == CL_DEVICE_EXTENSIONS) {
			/* make a backup of the extensions string, regardless of
			 * errors */
			const char *msg = RET_BUF(ret)->buf;
			size_t len = strlen(msg);
			extensions_traits = traits;
			ALLOC(extensions, len+1, "extensions");
			memcpy(extensions, msg, len);
			extensions[len] = '\0';
		} else {
			if (ret.err) {
				/* if there was an error retrieving the property,
				 * skip if it wasn't expected to work and we
				 * weren't asked to show everything regardless of
				 * error */
				if (!checked && output->cond != COND_PROP_SHOW)
					continue;

			} else {
				/* on success, but empty result, show (n/a) */
				if (ret.str.buf[0] == '\0')
					bufcpy(&ret.str, 0, not_specified(output));
			}
			if (output->brief)
				printf("%s%s\n", line_pfx, RET_BUF(ret)->buf);
			else
				show_strbuf(RET_BUF(ret), loc.pname, 0, ret.err);
		}

		if (ret.err)
			continue;

		switch (traits->param) {
		case CL_DEVICE_VERSION:
			/* compute numeric value for OpenCL version */
			chk.dev_version = getOpenCLVersion(ret.str.buf + 7);
			break;
		case CL_DEVICE_EXTENSIONS:
			identify_device_extensions(extensions, &chk);
			break;
		case CL_DEVICE_TYPE:
			chk.devtype = ret.value.devtype;
			break;
		case CL_DEVICE_GLOBAL_MEM_CACHE_TYPE:
			chk.cachetype = ret.value.cachetype;
			break;
		case CL_DEVICE_LOCAL_MEM_TYPE:
			chk.lmemtype = ret.value.lmemtype;
			break;
		case CL_DEVICE_IMAGE_SUPPORT:
			chk.image_support = ret.value.b;
			break;
		case CL_DEVICE_COMPILER_AVAILABLE:
			chk.compiler_available = ret.value.b;
			break;
		default:
			/* do nothing */
			break;
		}
	}

	// and finally the extensions, if we retrieved them
	if (extensions)
		printf("%s" I1_STR "%s\n", line_pfx, (output->mode == CLINFO_HUMAN ?
				extensions_traits->pname :
				extensions_traits->sname), extensions);
	free(extensions);
	extensions = NULL;
	UNINIT_RET(ret);
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
	CL_DEVICE_GFXIP_MAJOR_AMD,
	CL_DEVICE_GFXIP_MINOR_AMD,
	CL_DEVICE_MAX_WORK_GROUP_SIZE,
	CL_FALSE
};

static const cl_device_info list_info_whitelist[] = {
	CL_DEVICE_NAME,
	CL_FALSE
};

/* return a list of offline devices from the AMD extension */
cl_device_id *
fetchOfflineDevicesAMD(const struct platform_list *plist, cl_uint p,
	/* the number of devices will be returned in ret->value.u32,
	 * the associated context in ret->base.ctx;
	 */
	struct device_info_ret *ret)
{
	cl_platform_id pid = plist->platform[p];
	cl_device_id *device = NULL;
	cl_uint num_devs = 0;
	cl_context ctx = NULL;

	cl_context_properties ctxpft[] = {
		CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
		CL_CONTEXT_OFFLINE_DEVICES_AMD, (cl_context_properties)CL_TRUE,
		0
	};

	ctx = clCreateContextFromType(ctxpft, CL_DEVICE_TYPE_ALL,
		NULL, NULL, &ret->err);
	REPORT_ERROR(&ret->err_str, ret->err, "create context");

	if (!ret->err) {
		ret->err = REPORT_ERROR(&ret->err_str,
			clGetContextInfo(ctx, CL_CONTEXT_NUM_DEVICES,
				sizeof(num_devs), &num_devs, NULL),
			"get num devs");
	}

	if (!ret->err) {
		ALLOC(device, num_devs, "offline devices");

		ret->err = REPORT_ERROR(&ret->err_str,
			clGetContextInfo(ctx, CL_CONTEXT_DEVICES,
				num_devs*sizeof(*device), device, NULL),
			"get devs");
	}

	if (ret->err) {
		if (ctx) clReleaseContext(ctx);
		free(device);
		device = NULL;
	} else {
		ret->value.u32 = num_devs;
		ret->base.ctx = ctx;
	}
	return device;
}

void printPlatformName(const struct platform_list *plist, cl_uint p, struct _strbuf *str,
	const struct opt_out *output)
{
	const struct platform_data *pdata = plist->pdata + p;
	const char *brief_prefix = (output->mode == CLINFO_HUMAN ? "Platform #" : "");
	const char *title = (output->mode == CLINFO_HUMAN  ? pinfo_traits[0].pname :
		pinfo_traits[0].sname);
	const int prefix_width = -line_pfx_len*(!output->brief);
	if (output->brief) {
		strbuf_printf(str, "%s%" PRIu32 ": ", brief_prefix, p);
	} else if (output->mode == CLINFO_RAW) {
		strbuf_printf(str, "[%s/*]", pdata->sname);
	}
	sprintf(line_pfx, "%*s", prefix_width, str->buf);

	if (output->brief)
		printf("%s%s\n", line_pfx, pdata->pname);
	else
		printf("%s" I1_STR "%s\n", line_pfx, title, pdata->pname);
}

void printPlatformDevices(const struct platform_list *plist, cl_uint p,
	const cl_device_id *device, cl_uint ndevs,
	struct _strbuf *str, const struct opt_out *output, cl_bool these_are_offline)
{
	const struct platform_data *pdata = plist->pdata + p;
	const cl_device_info *param_whitelist = output->brief ? list_info_whitelist :
		these_are_offline ? amd_offline_info_whitelist : NULL;
	cl_uint d;

	if (output->detailed)
		printf("%s" I0_STR "%" PRIu32 "\n",
			line_pfx,
			num_devs_header(output, these_are_offline),
			ndevs);

	for (d = 0; d < ndevs; ++d) {
		const cl_device_id dev = device[d];
		if (output->brief) {
			const cl_bool last_device = (d == ndevs - 1 &&
				output->mode != CLINFO_RAW &&
				(!output->offline ||
				 !pdata->has_amd_offline ||
				 these_are_offline));
			if (output->mode == CLINFO_RAW)
				sprintf(line_pfx, "%" PRIu32 "%c%" PRIu32 ": ",
					p,
					these_are_offline ? '*' : '.',
					d);
			else
				sprintf(line_pfx, " +-- %sDevice #%" PRIu32 ": ",
					these_are_offline ? "Offline " : "",
					d);
			if (last_device)
				line_pfx[1] = '`';
		} else if (line_pfx_len > 0) {
			cl_int sd = (these_are_offline ? -1 : 1)*(cl_int)d;
			strbuf_printf(str, "[%s/%" PRId32 "]", pdata->sname, sd);
			sprintf(line_pfx, "%*s", -line_pfx_len, str->buf);
		}
		printDeviceInfo(dev, plist, p, param_whitelist, output);
		if (output->detailed && d < pdata[p].ndevs - 1)
			puts("");
		fflush(stdout);
		fflush(stderr);
	}
}


void showDevices(const struct platform_list *plist, const struct opt_out *output)
{
	const cl_uint num_platforms = plist->num_platforms;
	const cl_uint maxdevs = plist->max_devs;
	const struct platform_data *pdata = plist->pdata;

	cl_uint p;
	struct _strbuf str;
	init_strbuf(&str);
	realloc_strbuf(&str, 1024, "show devices");

	if (output->mode == CLINFO_RAW) {
		if (output->brief)
			strbuf_printf(&str, "%" PRIu32 ".%" PRIu32 ": ", num_platforms, maxdevs);
		else
			strbuf_printf(&str, "[%*s/%" PRIu32 "] ",
				plist->max_sname_len, "", maxdevs);
	} else {
		if (output->brief)
			strbuf_printf(&str, " +-- %sDevice #%" PRIu32 ": ",
				(output->offline ? "Offline " : ""), maxdevs);
		else
			str.buf[0] = '\0'; /* reset */
		/* TODO we have no prefix in HUMAN detailed output mode,
		 * consider adding one
		 */
	}

	if (str.buf[0]) {
		line_pfx_len = (int)(strlen(str.buf) + 1);
		REALLOC(line_pfx, line_pfx_len, "line prefix");
		str.buf[0] = '\0'; /* reset */
	}

	for (p = 0; p < num_platforms; ++p) {
		printPlatformName(plist, p, &str, output);

		printPlatformDevices(plist, p,
			get_platform_devs(plist, p), pdata[p].ndevs,
			&str, output, CL_FALSE);

		if (output->offline && pdata[p].has_amd_offline) {
			struct device_info_ret ret;
			cl_device_id *devs = NULL;

			INIT_RET(ret, "offline device");
			if (output->detailed)
				puts("");

			devs = fetchOfflineDevicesAMD(plist, p, &ret);
			if (ret.err) {
				puts(ret.err_str.buf);
			} else {
				printPlatformDevices(plist, p, devs, ret.value.u32,
					&str, output, CL_TRUE);
				clReleaseContext(ret.base.ctx);
				free(devs);
			}
			UNINIT_RET(ret);
		}
		if (output->detailed)
			puts("");
	}
	free_strbuf(&str);
}

/* check the behavior of clGetPlatformInfo() when given a NULL platform ID */
void checkNullGetPlatformName(const struct opt_out *output)
{
	struct device_info_ret ret;
	struct info_loc loc;

	INIT_RET(ret, "null ctx");
	reset_loc(&loc, __func__);
	RESET_LOC_PARAM(loc, plat, CL_PLATFORM_NAME);

	ret.err = clGetPlatformInfo(NULL, CL_PLATFORM_NAME, ret.str.sz, ret.str.buf, NULL);
	if (ret.err == CL_INVALID_PLATFORM) {
		bufcpy(&ret.err_str, 0, no_plat(output));
	} else {
		loc.line = __LINE__ + 1;
		REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s");
	}
	printf(I1_STR "%s\n",
		"clGetPlatformInfo(NULL, CL_PLATFORM_NAME, ...)", RET_BUF(ret)->buf);
	UNINIT_RET(ret);
}

/* check the behavior of clGetDeviceIDs() when given a NULL platform ID;
 * return the index of the default platform in our array of platform IDs,
 * or num_platforms (which is an invalid platform index) in case of errors
 * or no platform or device found.
 */
cl_uint checkNullGetDevices(const struct platform_list *plist, const struct opt_out *output)
{
	const cl_uint num_platforms = plist->num_platforms;
	const struct platform_data *pdata = plist->pdata;
	const cl_platform_id *platform = plist->platform;

	struct device_info_ret ret;
	struct info_loc loc;

	cl_uint i = 0; /* generic iterator */
	cl_device_id dev = NULL; /* sample device */
	cl_platform_id plat = NULL; /* detected platform */

	cl_uint found = 0; /* number of platforms found */
	cl_uint pidx = num_platforms; /* index of the platform found */
	cl_uint numdevs = 0;

	INIT_RET(ret, "null get devices");

	reset_loc(&loc, __func__);
	loc.sname = "device IDs";

	ret.err = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_ALL, 0, NULL, &numdevs);
	/* TODO we should check other CL_DEVICE_TYPE_* combinations, since a smart
	 * implementation might give you a different default platform for GPUs
	 * and for CPUs.
	 * Of course the “no devices” case would then need to be handled differently.
	 * The logic might be maintained similarly, provided we also gather
	 * the number of devices of each type for each platform, although it's
	 * obviously more likely to have multiple platforms with no devices
	 * of a given type.
	 */

	switch (ret.err) {
	case CL_INVALID_PLATFORM:
		bufcpy(&ret.err_str, 0, no_plat(output));
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
			bufcpy(&ret.err_str, 0, (output->mode == CLINFO_HUMAN ?
				"<error: 0 devices, no matching platform!>" :
				"CL_DEVICE_NOT_FOUND | CL_INVALID_PLATFORM"));
			break;
		case 1:
			bufcpy(&ret.str, 0, (output->mode == CLINFO_HUMAN ?
				pdata[pidx].pname :
				pdata[pidx].sname));
			break;
		default: /* found > 1 */
			bufcpy(&ret.err_str, 0, (output->mode == CLINFO_HUMAN ?
				"<error: 0 devices, multiple matching platforms!>" :
				"CL_DEVICE_NOT_FOUND | ????"));
			break;
		}
		break;
	default:
		loc.line = __LINE__+1;
		if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get number of %s")) break;

		/* Determine platform by looking at the CL_DEVICE_PLATFORM of
		 * one of the devices */
		ret.err = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_ALL, 1, &dev, NULL);
		loc.line = __LINE__+1;
		if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s")) break;

		RESET_LOC_PARAM(loc, dev, CL_DEVICE_PLATFORM);
		ret.err = clGetDeviceInfo(dev, CL_DEVICE_PLATFORM,
			sizeof(plat), &plat, NULL);
		loc.line = __LINE__+1;
		if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s")) break;

		for (i = 0; i < num_platforms; ++i) {
			if (platform[i] == plat) {
				pidx = i;
				strbuf_printf(&ret.str, "%s [%s]",
					(output->mode == CLINFO_HUMAN ? "Success" : "CL_SUCCESS"),
					pdata[i].sname);
				break;
			}
		}
		if (i == num_platforms) {
			ret.err = CL_INVALID_PLATFORM;
			strbuf_printf(&ret.err_str, "<error: platform %p not found>", (void*)plat);
		}
	}
	printf(I1_STR "%s\n",
		"clGetDeviceIDs(NULL, CL_DEVICE_TYPE_ALL, ...)", RET_BUF(ret)->buf);

	UNINIT_RET(ret);
	return pidx;
}

void checkNullCtx(struct device_info_ret *ret,
	const struct platform_list *plist, cl_uint pidx, const char *which,
	const struct opt_out *output)
{
	const cl_device_id *dev = plist->all_devs + plist->dev_offset[pidx];
	struct info_loc loc;
	cl_context ctx = clCreateContext(NULL, 1, dev, NULL, NULL, &ret->err);

	reset_loc(&loc, __func__);
	loc.sname = which;
	loc.line = __LINE__+2;

	if (!REPORT_ERROR_LOC(ret, ret->err, &loc, "create context with device from %s platform"))
		strbuf_printf(&ret->str, "%s [%s]",
			(output->mode == CLINFO_HUMAN ? "Success" : "CL_SUCCESS"),
			plist->pdata[pidx].sname);
	if (ctx) {
		clReleaseContext(ctx);
		ctx = NULL;
	}
}

/* check behavior of clCreateContextFromType() with NULL cl_context_properties */
void checkNullCtxFromType(const struct platform_list *plist, const struct opt_out *output)
{
	const cl_uint num_platforms = plist->num_platforms;
	const struct platform_data *pdata = plist->pdata;
	const cl_platform_id *platform = plist->platform;

	size_t t; /* type iterator */
	size_t i; /* generic iterator */
	char def[1024];
	cl_context ctx = NULL;

	size_t ndevs = 8;
	size_t szval = 0;
	size_t cursz = ndevs*sizeof(cl_device_id);
	cl_platform_id plat = NULL;
	cl_device_id *devs = NULL;

	struct device_info_ret ret;
	struct info_loc loc;

	const char *platname_prop = (output->mode == CLINFO_HUMAN ?
		pinfo_traits[0].pname :
		pinfo_traits[0].sname);

	const char *devname_prop = (output->mode == CLINFO_HUMAN ?
		dinfo_traits[0].pname :
		dinfo_traits[0].sname);

	reset_loc(&loc, __func__);
	INIT_RET(ret, "null ctx from type");

	ALLOC(devs, ndevs, "context devices");

	for (t = 1; t < devtype_count; ++t) { /* we skip 0 */
		loc.sname = device_type_raw_str[t];

		strbuf_printf(&ret.str, "clCreateContextFromType(NULL, %s)", loc.sname);
		sprintf(def, I1_STR, ret.str.buf);

		loc.line = __LINE__+1;
		ctx = clCreateContextFromType(NULL, devtype[t], NULL, NULL, &ret.err);

		switch (ret.err) {
		case CL_INVALID_PLATFORM:
			bufcpy(&ret.err_str, 0, no_plat(output)); break;
		case CL_DEVICE_NOT_FOUND:
			bufcpy(&ret.err_str, 0, no_dev_found(output)); break;
		case CL_INVALID_DEVICE_TYPE: /* e.g. _CUSTOM device on 1.1 platform */
			bufcpy(&ret.err_str, 0, invalid_dev_type(output)); break;
		case CL_INVALID_VALUE: /* This is what apple returns for the case above */
			bufcpy(&ret.err_str, 0, invalid_dev_type(output)); break;
		case CL_DEVICE_NOT_AVAILABLE:
			bufcpy(&ret.err_str, 0, no_dev_avail(output)); break;
		default:
			if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "create context from type %s")) break;

			/* get the devices */
			loc.sname = "CL_CONTEXT_DEVICES";
			loc.line = __LINE__+2;

			ret.err = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, 0, NULL, &szval);
			if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s size")) break;
			if (szval > cursz) {
				REALLOC(devs, szval, "context devices");
				cursz = szval;
			}

			loc.line = __LINE__+1;
			ret.err = clGetContextInfo(ctx, CL_CONTEXT_DEVICES, cursz, devs, NULL);
			if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s")) break;
			ndevs = szval/sizeof(cl_device_id);
			if (ndevs < 1) {
				ret.err = CL_DEVICE_NOT_FOUND;
				bufcpy(&ret.err_str, 0, "<error: context created with no devices>");
			}

			/* get the platform from the first device */
			RESET_LOC_PARAM(loc, dev, CL_DEVICE_PLATFORM);
			loc.line = __LINE__+1;
			ret.err = clGetDeviceInfo(*devs, CL_DEVICE_PLATFORM, sizeof(plat), &plat, NULL);
			if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s")) break;
			loc.plat = plat;

			szval = 0;
			for (i = 0; i < num_platforms; ++i) {
				if (platform[i] == plat)
					break;
			}
			if (i == num_platforms) {
				ret.err = CL_INVALID_PLATFORM;
				strbuf_printf(&ret.err_str, "<error: platform %p not found>", (void*)plat);
				break;
			} else {
				szval += strbuf_printf(&ret.str, "%s (%" PRIuS ")",
					(output->mode == CLINFO_HUMAN ? "Success" : "CL_SUCCESS"),
					ndevs);
				szval += snprintf(ret.str.buf + szval, ret.str.sz - szval, "\n" I2_STR "%s",
					platname_prop, pdata[i].pname);
			}
			for (i = 0; i < ndevs; ++i) {
				size_t szname = 0;
				/* for each device, show the device name */
				/* TODO some other unique ID too, e.g. PCI address, if available? */

				szval += snprintf(ret.str.buf + szval, ret.str.sz - szval, "\n" I2_STR, devname_prop);
				if (szval >= ret.str.sz) {
					trunc_strbuf(&ret.str);
					break;
				}

				RESET_LOC_PARAM(loc, dev, CL_DEVICE_NAME);
				loc.dev = devs[i];
				loc.line = __LINE__+1;
				ret.err = clGetDeviceInfo(devs[i], CL_DEVICE_NAME, ret.str.sz - szval, ret.str.buf + szval, &szname);
				if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s")) break;
				szval += szname - 1;
			}
			if (i != ndevs)
				break; /* had an error earlier, bail */

		}

		if (ctx) {
			clReleaseContext(ctx);
			ctx = NULL;
		}
		printf("%s%s\n", def, RET_BUF(ret)->buf);
	}
	free(devs);
	UNINIT_RET(ret);
}

/* check the behavior of NULL platform in clGetDeviceIDs (see checkNullGetDevices)
 * and in clCreateContext() */
void checkNullBehavior(const struct platform_list *plist, const struct opt_out *output)
{
	const cl_uint num_platforms = plist->num_platforms;
	const struct platform_data *pdata = plist->pdata;

	cl_uint p = 0;
	struct device_info_ret ret;

	INIT_RET(ret, "null behavior");

	printf("NULL platform behavior\n");

	checkNullGetPlatformName(output);

	p = checkNullGetDevices(plist, output);

	/* If there's a default platform, and it has devices, try
	 * creating a context with its first device and see if it works */

	if (p == num_platforms) {
		ret.err = CL_INVALID_PLATFORM;
		bufcpy(&ret.err_str, 0, no_plat(output));
	} else if (pdata[p].ndevs == 0) {
		ret.err = CL_DEVICE_NOT_FOUND;
		bufcpy(&ret.err_str, 0, no_dev_found(output));
	} else {
		if (p < num_platforms) {
			checkNullCtx(&ret, plist, p, "default", output);
		} else {
			/* this shouldn't happen, but still ... */
			ret.err = CL_OUT_OF_HOST_MEMORY;
			bufcpy(&ret.err_str, 0, "<error: overflow in default platform scan>");
		}
	}
	printf(I1_STR "%s\n", "clCreateContext(NULL, ...) [default]", RET_BUF(ret)->buf);

	/* Look for a device from a non-default platform, if there are any */
	if (p == num_platforms || num_platforms > 1) {
		cl_uint p2 = 0;
		while (p2 < num_platforms && (p2 == p || pdata[p2].ndevs == 0)) {
			p2++;
		}
		if (p2 < num_platforms) {
			checkNullCtx(&ret, plist, p2, "non-default", output);
		} else {
			ret.err = CL_DEVICE_NOT_FOUND;
			bufcpy(&ret.str, 0, "<error: no devices in non-default plaforms>");
		}
		printf(I1_STR "%s\n", "clCreateContext(NULL, ...) [other]", RET_BUF(ret)->buf);
	}

	checkNullCtxFromType(plist, output);

	UNINIT_RET(ret);
}


/* Get properties of the ocl-icd loader, if available */
/* All properties are currently char[] */

/* Function pointer to the ICD loader info function */

typedef cl_int (*icdl_info_fn_ptr)(cl_icdl_info, size_t, void*, size_t*);
icdl_info_fn_ptr clGetICDLoaderInfoOCLICD;

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
	{ 22, "clSetProgramSpecializationConstant" },
	{ 0, NULL }
};

void
icdl_info_str(struct icdl_info_ret *ret, const struct info_loc *loc)
{
	GET_STRING_LOC(ret, loc, clGetICDLoaderInfoOCLICD, loc->param.icdl);
	return;
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

/* The ICD loader info function must be retrieved via clGetExtensionFunctionAddress,
 * which returns a void pointer.
 * ISO C forbids assignments between function pointers and void pointers,
 * but POSIX allows it. To compile without warnings even in -pedantic mode,
 * we take advantage of the fact that we _can_ do the conversion via
 * pointers-to-pointers. This is supported on most compilers, except
 * for some rather old GCC versions whose strict aliasing rules are
 * too strict. Disable strict aliasing warnings for these compilers.
 */
#if defined __GNUC__ && ((__GNUC__*10 + __GNUC_MINOR__) < 46)
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

struct icdl_data oclIcdProps(const struct platform_list *plist, const struct opt_out *output)
{
	const cl_uint max_plat_version = plist->max_plat_version;

	struct icdl_data icdl;

	/* Counter that'll be used to walk the icd_loader_tests */
	int i = 0;

	/* We find the clGetICDLoaderInfoOCLICD extension address, which will be used
	 * to query the ICD loader properties.
	 * It should be noted that in this specific case we cannot replace the
	 * call to clGetExtensionFunctionAddress with a call to the superseding function
	 * clGetExtensionFunctionAddressForPlatform because the extension is in the
	 * loader itself, not in a specific platform.
	 */
	void *ptrHack = clGetExtensionFunctionAddress("clGetICDLoaderInfoOCLICD");
	clGetICDLoaderInfoOCLICD = *(icdl_info_fn_ptr*)(&ptrHack);

	/* Initialize icdl_data ret versions */
	icdl.detected_version = 10;
	icdl.reported_version = 0;

	/* Step #1: try to auto-detect the supported ICD loader version */
	do {
		struct icd_loader_test check = icd_loader_tests[i];
		if (check.symbol == NULL)
			break;
		if (dlsym(DL_MODULE, check.symbol) == NULL)
			break;
		icdl.detected_version = check.version;
		++i;
	} while (1);

	/* Step #2: query properties from extension, if available */
	if (clGetICDLoaderInfoOCLICD != NULL) {
		struct info_loc loc;
		struct icdl_info_ret ret;
		reset_loc(&loc, __func__);
		INIT_RET(ret, "ICD loader");

		/* TODO think of a sensible header in CLINFO_RAW */
		if (output->mode != CLINFO_RAW)
			puts("\nICD loader properties");

		if (output->mode == CLINFO_RAW) {
			line_pfx_len = (int)(strlen(oclicdl_pfx) + 5);
			REALLOC(line_pfx, line_pfx_len, "line prefix OCL ICD");
			strbuf_printf(&ret.str, "[%s/*]", oclicdl_pfx);
			sprintf(line_pfx, "%*s", -line_pfx_len, ret.str.buf);
		}

		for (loc.line = 0; loc.line < ARRAY_SIZE(linfo_traits); ++loc.line) {
			const struct icdl_info_traits *traits = linfo_traits + loc.line;
			loc.sname = traits->sname;
			loc.pname = (output->mode == CLINFO_HUMAN ?
				traits->pname : traits->sname);
			loc.param.icdl = traits->param;

			ret.str.buf[0] = '\0';
			ret.err_str.buf[0] = '\0';
			icdl_info_str(&ret, &loc);
			show_strbuf(RET_BUF(ret), loc.pname, 1, ret.err);

			if (!ret.err && traits->param == CL_ICDL_OCL_VERSION) {
				icdl.reported_version = getOpenCLVersion(ret.str.buf + 7);
			}
		}
		UNINIT_RET(ret);
	}

	/* Step #3: show it */
	if (output->mode == CLINFO_HUMAN) {
		if (icdl.reported_version &&
			icdl.reported_version != icdl.detected_version) {
			printf(	"\tNOTE:\tyour OpenCL library declares to support OpenCL %" PRIu32 ".%" PRIu32 ",\n"
				"\t\tbut it seems to support up to OpenCL %" PRIu32 ".%" PRIu32 " %s.\n",
				SPLIT_CL_VERSION(icdl.reported_version),
				SPLIT_CL_VERSION(icdl.detected_version),
				icdl.detected_version < icdl.reported_version  ?
				"only" : "too");
		}
		if (icdl.detected_version < max_plat_version) {
			printf(	"\tNOTE:\tyour OpenCL library only supports OpenCL %" PRIu32 ".%" PRIu32 ",\n"
				"\t\tbut some installed platforms support OpenCL %" PRIu32 ".%" PRIu32 ".\n"
				"\t\tPrograms using %" PRIu32 ".%" PRIu32 " features may crash\n"
				"\t\tor behave unexpectedly\n",
				SPLIT_CL_VERSION(icdl.detected_version),
				SPLIT_CL_VERSION(max_plat_version),
				SPLIT_CL_VERSION(max_plat_version));
		}
	}
	return icdl;
}

#if defined __GNUC__ && ((__GNUC__*10 + __GNUC_MINOR__) < 46)
#pragma GCC diagnostic warning "-Wstrict-aliasing"
#endif

void version(void)
{
	puts("clinfo version 2.2.18.04.06");
}

void usage(void)
{
	version();
	puts("Display properties of all available OpenCL platforms and devices");
	puts("Usage: clinfo [options ...]\n");
	puts("Options:");
	puts("\t--all-props, -a\t\ttry all properties, only show valid ones");
	puts("\t--always-all-props, -A\t\tshow all properties, even if invalid");
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
	cl_int err;
	int a = 0;

	struct opt_out output;

	struct platform_list plist;
	init_plist(&plist);

	output.mode = CLINFO_HUMAN;
	output.cond = COND_PROP_CHECK;
	output.brief = CL_FALSE;
	output.offline = CL_FALSE;
	output.check_size = CL_FALSE;

	/* if there's a 'raw' in the program name, switch to raw output mode */
	if (strstr(argv[0], "raw"))
		output.mode = CLINFO_RAW;

	/* process command-line arguments */
	while (++a < argc) {
		if (!strcmp(argv[a], "-a") || !strcmp(argv[a], "--all-props"))
			output.cond = COND_PROP_TRY;
		else if (!strcmp(argv[a], "-A") || !strcmp(argv[a], "--always-all-props"))
			output.cond = COND_PROP_SHOW;
		else if (!strcmp(argv[a], "--raw"))
			output.mode = CLINFO_RAW;
		else if (!strcmp(argv[a], "--human"))
			output.mode = CLINFO_HUMAN;
		else if (!strcmp(argv[a], "--offline"))
			output.offline = CL_TRUE;
		else if (!strcmp(argv[a], "-l") || !strcmp(argv[a], "--list"))
			output.brief = CL_TRUE;
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
	output.detailed = !output.brief;

	err = clGetPlatformIDs(0, NULL, &plist.num_platforms);
	if (err != CL_PLATFORM_NOT_FOUND_KHR)
		CHECK_ERROR(err, "number of platforms");

	if (!output.brief)
		printf(I0_STR "%" PRIu32 "\n",
			(output.mode == CLINFO_HUMAN ?
			 "Number of platforms" : "#PLATFORMS"),
			plist.num_platforms);
	if (!plist.num_platforms)
		return 0;

	alloc_plist(&plist);
	err = clGetPlatformIDs(plist.num_platforms, plist.platform, NULL);
	CHECK_ERROR(err, "platform IDs");

	ALLOC(line_pfx, 1, "line prefix");

	for (p = 0; p < plist.num_platforms; ++p) {
		gatherPlatformInfo(&plist, p, &output);
		if (output.detailed)
			puts("");
	}
	showDevices(&plist, &output);
	if (output.detailed) {
		if (output.mode != CLINFO_RAW)
			checkNullBehavior(&plist, &output);
		oclIcdProps(&plist, &output);
	}

	free_plist(&plist);
	return 0;
}

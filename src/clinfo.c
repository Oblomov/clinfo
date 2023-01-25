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
#ifdef RTLD_DEFAULT
# define DL_MODULE RTLD_DEFAULT
#else
# define DL_MODULE ((void*)0) /* This would be RTLD_DEFAULT */
#endif
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
	cl_bool has_extended_versioning;
	cl_bool has_external_memory;
	cl_bool has_semaphore;
	cl_bool has_external_semaphore;
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
	size_t max_sname_len;
	/* Array of platform IDs */
	cl_platform_id *platform;
	/* Array of device IDs (across all platforms) */
	cl_device_id *all_devs;
	/* Array of offsets in all_devs where the devices
	 * of each platform begin */
	cl_uint *dev_offset;
	/* Array of clinfo-specific platform data */
	struct platform_data *pdata;
	/* Array of clinfo-specific platform checks */
	struct platform_info_checks *platform_checks;
};

void
init_plist(struct platform_list *plist)
{
	plist->num_platforms = 0;
	plist->ndevs_total = 0;
	plist->alloc_devs = 0;
	plist->max_plat_version = 0;
	plist->max_devs = 0;
	plist->max_sname_len = 0;
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


cl_uint
alloc_plist(struct platform_list *plist, const struct opt_out *output)
{
	cl_uint num_platforms = plist->num_platforms;
	if (output->null_platform)
		num_platforms += 1;
	ALLOC(plist->platform, num_platforms, "platform IDs");
	ALLOC(plist->dev_offset, num_platforms, "platform device list offset");
	/* The actual sizing for this will change as we gather platform info,
	 * but assume at least one device per platform
	 */
	plist_devs_reserve(plist, num_platforms);
	ALLOC(plist->pdata, num_platforms, "platform data");
	ALLOC(plist->platform_checks, num_platforms, "platform checks data");
	return num_platforms;
}
void
free_plist(struct platform_list *plist)
{
	free(plist->platform);
	free(plist->all_devs);
	free(plist->dev_offset);
	for (cl_uint p = 0 ; p < plist->num_platforms; ++p) {
		free(plist->pdata[p].sname);
		free(plist->pdata[p].pname);
	}
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
static const char* bool_json_str[] = { "false", "true" };

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

static const char* atomic_cap_str[] = {
	"relaxed", "acquire/release", "sequentially-consistent",
	"work-item scope", "work-group scope", "device scope", "all-devices scope"
};
static const char* atomic_cap_raw_str[] = {
	"CL_DEVICE_ATOMIC_ORDER_RELAXED",
	"CL_DEVICE_ATOMIC_ORDER_ACQ_REL",
	"CL_DEVICE_ATOMIC_ORDER_SEQ_CST",
	"CL_DEVICE_ATOMIC_SCOPE_WORK_ITEM",
	"CL_DEVICE_ATOMIC_SCOPE_WORK_GROUP",
	"CL_DEVICE_ATOMIC_SCOPE_DEVICE",
	"CL_DEVICE_ATOMIC_SCOPE_ALL_DEVICES"
};
const size_t atomic_cap_count = ARRAY_SIZE(atomic_cap_str);

static const char *device_enqueue_cap_str[] = {
	"supported", "replaceable default queue"
};

static const char *device_enqueue_cap_raw_str[] = {
	"CL_DEVICE_QUEUE_SUPPORTED",
	"CL_DEVICE_QUEUE_REPLACEABLE_DEFAULT"
};
const size_t device_enqueue_cap_count = ARRAY_SIZE(atomic_cap_str);

static const char *command_buffer_str[] = {
	"kernel printf", "device side enqueue", "simultaneous use", "out of order",
};

static const char *command_buffer_raw_str[] = {
	"CL_COMMAND_BUFFER_CAPABILITY_KERNEL_PRINTF_KHR",
	"CL_COMMAND_BUFFER_CAPABILITY_DEVICE_SIDE_ENQUEUE_KHR",
	"CL_COMMAND_BUFFER_CAPABILITY_SIMULTANEOUS_USE_KHR",
	"CL_COMMAND_BUFFER_CAPABILITY_OUT_OF_ORDER_KHR",
};

const size_t command_buffer_count = ARRAY_SIZE(command_buffer_str);

static const char *mutable_dispatch_str[] = {
	"Global Offset",
	"Local Offset",
	"Local Size",
	"Arguments",
	"Exec Info",
};

static const char *mutable_dispatch_raw_str[] = {
	"CL_MUTABLE_DISPATCH_GLOBAL_OFFSET_KHR",
	"CL_MUTABLE_DISPATCH_GLOBAL_SIZE_KHR",
	"CL_MUTABLE_DISPATCH_LOCAL_SIZE_KHR",
	"CL_MUTABLE_DISPATCH_ARGUMENTS_KHR",
	"CL_MUTABLE_DISPATCH_EXEC_INFO_KHR",
};

const size_t mutable_dispatch_count = ARRAY_SIZE(mutable_dispatch_str);

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

static const char *terminate_capability_arm_str[] = {
	"Controlled Success",
	"Controlled Failurure",
	"Query"
};

static const char * terminate_capability_arm_raw_str[] = {
	"CL_DEVICE_CONTROLLED_TERMINATION_SUCCESS_ARM",
	"CL_DEVICE_CONTROLLED_TERMINATION_FAILURE_ARM",
	"CL_DEVICE_CONTROLLED_TERMINATION_QUERY_ARM"
};

const size_t terminate_capability_arm_count = ARRAY_SIZE(terminate_capability_arm_str);

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

static const char * intel_usm_cap_str[] = {
	"USM access",
	"USM atomic access",
	"USM concurrent access",
	"USM concurrent atomic access"
};

static const char * intel_usm_cap_raw_str[] = {
	"CL_UNIFIED_SHARED_MEMORY_ACCESS_INTEL",
	"CL_UNIFIED_SHARED_MEMORY_ATOMIC_ACCESS_INTEL",
	"CL_UNIFIED_SHARED_MEMORY_CONCURRENT_ACCESS_INTEL",
	"CL_UNIFIED_SHARED_MEMORY_CONCURRENT_ATOMIC_ACCESS_INTEL",
};

const size_t intel_usm_cap_count = ARRAY_SIZE(intel_usm_cap_str);

static const char* arm_scheduling_controls_str[] = {
	"Kernel batching",
	"Work-group batch size",
	"Work-group batch size modifier",
	"Deferred flush",
	"Register allocation",
	"Warp throttling",
	"Compute unit batch queue size",
};

static const char* arm_scheduling_controls_raw_str[] = {
	"CL_DEVICE_SCHEDULING_KERNEL_BATCHING_ARM",
	"CL_DEVICE_SCHEDULING_WORKGROUP_BATCH_SIZE_ARM",
	"CL_DEVICE_SCHEDULING_WORKGROUP_BATCH_SIZE_MODIFIER_ARM",
	"CL_DEVICE_SCHEDULING_DEFERRED_FLUSH_ARM",
	"CL_DEVICE_SCHEDULING_REGISTER_ALLOCATION_ARM",
	"CL_DEVICE_SCHEDULING_WARP_THROTTLING_ARM",
	"CL_DEVICE_SCHEDULING_COMPUTE_UNIT_BATCH_QUEUE_SIZE_ARM",
};

const size_t arm_scheduling_controls_count = ARRAY_SIZE(arm_scheduling_controls_str);

static const char* ext_mem_handle_str[] = {
	"Opaque FD",
	"Opaqe Win32",
	"Opaque Win32 KMT",
	"D3D11 Texture",
	"D3D11 Texture KMT",
	"D3D12 Heap",
	"D3D12 Resource",
	"DMA buffer"
};

static const char* ext_mem_handle_raw_str[] = {
	"CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_FD_KHR",
	"CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR",
	"CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KMT_KHR",
	"CL_EXTERNAL_MEMORY_HANDLE_D3D11_TEXTURE_KHR",
	"CL_EXTERNAL_MEMORY_HANDLE_D3D11_TEXTURE_KMT_KHR",
	"CL_EXTERNAL_MEMORY_HANDLE_D3D12_HEAP_KHR",
	"CL_EXTERNAL_MEMORY_HANDLE_D3D12_RESOURCE_KHR",
	"CL_EXTERNAL_MEMORY_HANDLE_DMA_BUF_KHR",
};

const size_t ext_mem_handle_count = ARRAY_SIZE(ext_mem_handle_str);
const size_t ext_mem_handle_offset = 0x2060;

static const char* semaphore_type_str[] = {
	"Binary"
};
static const char* semaphore_type_raw_str[] = {
	"CL_SEMAPHORE_TYPE_BINARY_KHR"
};
const size_t semaphore_type_count = ARRAY_SIZE(semaphore_type_str);
const size_t semaphore_type_offset = 1;

static const char* semaphore_handle_str[] = {
	"Opaque FD",
	"Opaque Win32",
	"Opaque Win32 KMT",
	"Sync FD",
	"D3D12 Fence"
};
static const char* semaphore_handle_raw_str[] = {
	"CL_SEMAPHORE_HANDLE_OPAQUE_FD_KHR",
	"CL_SEMAPHORE_HANDLE_OPAQUE_WIN32_KHR",
	"CL_SEMAPHORE_HANDLE_OPAQUE_WIN32_KMT_KHR",
	"CL_SEMAPHORE_HANDLE_SYNC_FD_KHR",
	"CL_SEMAPHORE_HANDLE_D3D12_FENCE_KHR",
};
const size_t semaphore_handle_count = ARRAY_SIZE(semaphore_handle_str);
const size_t semaphore_handle_offset = 0x2055;

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

static const char* intel_queue_cap_str[] = {
	"create single-queue events",
	"create cross-queue events",
	"single-queue wait list",
	"cross-queue wait list",
	"unknown (bit 4)",
	"unknown (bit 5)",
	"unknown (bit 6)",
	"unknown (bit 7)",
	"transfer buffer",
	"transfer buffer rect",
	"map buffer",
	"fill buffer",
	"transfer image",
	"map image",
	"fill image",
	"transfer buffer to image",
	"transfer image to buffer",
	"unknown (bit 17)",
	"unknown (bit 18)",
	"unknown (bit 19)",
	"unknown (bit 20)",
	"unknown (bit 21)",
	"unknown (bit 22)",
	"unknown (bit 23)",
	"marker enqueue",
	"barrier enqueue",
	"kernel enqueue",
	"unknown (bit 27)",
	"unknown (bit 28)",
	"no sync operations",
};

static const char* intel_queue_cap_raw_str[] = {
	"CL_QUEUE_CAPABILITY_CREATE_SINGLE_QUEUE_EVENTS_INTEL",
	"CL_QUEUE_CAPABILITY_CREATE_CROSS_QUEUE_EVENTS_INTEL",
	"CL_QUEUE_CAPABILITY_SINGLE_QUEUE_EVENT_WAIT_LIST_INTEL",
	"CL_QUEUE_CAPABILITY_CROSS_QUEUE_EVENT_WAIT_LIST_INTEL",
	"CL_QUEUE_CAPABILITY_UNKNOWN_4",
	"CL_QUEUE_CAPABILITY_UNKNOWN_5",
	"CL_QUEUE_CAPABILITY_UNKNOWN_6",
	"CL_QUEUE_CAPABILITY_UNKNOWN_7",
	"CL_QUEUE_CAPABILITY_TRANSFER_BUFFER_INTEL",
	"CL_QUEUE_CAPABILITY_TRANSFER_BUFFER_RECT_INTEL",
	"CL_QUEUE_CAPABILITY_MAP_BUFFER_INTEL",
	"CL_QUEUE_CAPABILITY_FILL_BUFFER_INTEL",
	"CL_QUEUE_CAPABILITY_TRANSFER_IMAGE_INTEL",
	"CL_QUEUE_CAPABILITY_MAP_IMAGE_INTEL",
	"CL_QUEUE_CAPABILITY_FILL_IMAGE_INTEL",
	"CL_QUEUE_CAPABILITY_TRANSFER_BUFFER_IMAGE_INTEL",
	"CL_QUEUE_CAPABILITY_TRANSFER_IMAGE_BUFFER_INTEL",
	"CL_QUEUE_CAPABILITY_UNKNOWN_17",
	"CL_QUEUE_CAPABILITY_UNKNOWN_18",
	"CL_QUEUE_CAPABILITY_UNKNOWN_19",
	"CL_QUEUE_CAPABILITY_UNKNOWN_20",
	"CL_QUEUE_CAPABILITY_UNKNOWN_21",
	"CL_QUEUE_CAPABILITY_UNKNOWN_22",
	"CL_QUEUE_CAPABILITY_UNKNOWN_23",
	"CL_QUEUE_CAPABILITY_MARKER_INTEL",
	"CL_QUEUE_CAPABILITY_BARRIER_INTEL",
	"CL_QUEUE_CAPABILITY_KERNEL_INTEL",
	"CL_QUEUE_CAPABILITY_UNKNOWN_27",
	"CL_QUEUE_CAPABILITY_UNKNOWN_28",
	"CL_QUEUE_NO_SYNC_OPERATIONS_INTEL",
};

const size_t intel_queue_cap_count = ARRAY_SIZE(intel_queue_cap_str);

static const char* execap_str[] = { "Run OpenCL kernels", "Run native kernels" };
static const char* execap_raw_str[] = {
	"CL_EXEC_KERNEL",
	"CL_EXEC_NATIVE_KERNEL"
};

const size_t execap_count = ARRAY_SIZE(execap_str);

static const char* intel_features_str[] = { "DP4A", "DPAS" };
static const char* intel_features_raw_str[] = { "CL_DEVICE_FEATURE_FLAG_DP4A_INTEL", "CL_DEVICE_FEATURE_FLAG_DPAS_INTEL" };

const size_t intel_features_count = ARRAY_SIZE(intel_features_str);

static const char* sources[] = {
	"#define GWO(type) global type* restrict\n",
	"#define GRO(type) global const type* restrict\n",
	"#define BODY int i = get_global_id(0); out[i] = in1[i] + in2[i]\n",
	"#define _KRN(T, N) kernel void sum##N(GWO(T##N) out, GRO(T##N) in1, GRO(T##N) in2) { BODY; }\n",
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

/* New line and a full padding */
static const char full_padding[] = "\n"
INDENT INDENT INDENT INDENT INDENT
INDENT INDENT INDENT INDENT INDENT
INDENT INDENT INDENT INDENT INDENT
INDENT INDENT INDENT INDENT INDENT
INDENT INDENT INDENT INDENT INDENT;

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

/* OpenCL 3.0 introduced “proper” versioning, in the form of a major.minor.patch struct
 * packed into a single cl_uint (type aliased to cl_version)
 */
struct unpacked_cl_version {
	cl_uint major;
	cl_uint minor;
	cl_uint patch;
};

struct unpacked_cl_version unpack_cl_version(cl_uint version)
{
	struct unpacked_cl_version ret;
	ret.major = (version >> 22);
	ret.minor = (version >> 12) & ((1<<10)-1);
	ret.patch =  version & ((1<<12)-1);
	return ret;
}

void strbuf_version(const char *what, struct _strbuf *str, const char *before, cl_uint version, const char *after)
{
	struct unpacked_cl_version u = unpack_cl_version(version);
	strbuf_append(what, str, "%s%" PRIu32 ".%" PRIu32 ".%" PRIu32 "%s",
				before, u.major, u.minor, u.patch, after);
}

void set_common_separator(const struct opt_out *output)
{
	set_separator(output->json || output->mode == CLINFO_HUMAN ? comma_str : vbar_str);
}

void strbuf_name_version(const char *what, struct _strbuf *str, const cl_name_version *ext, size_t num_exts,
	const struct opt_out *output)
{
	realloc_strbuf(str, num_exts*(CL_NAME_VERSION_MAX_NAME_SIZE + 256), "extension versions");
	set_separator(output->mode == CLINFO_HUMAN ? full_padding : output->json ? comma_str : spc_str);
	if (output->json) {
		strbuf_append_str(what, str, "{");
	}
	for (size_t i = 0; i < num_exts; ++i) {
		const cl_name_version  *e = ext + i;
		if (i > 0) strbuf_append_str(what, str, sep);
		if (output->json || output->mode == CLINFO_HUMAN) {
			struct unpacked_cl_version u = unpack_cl_version(e->version);
			strbuf_append(what, str,
				output->json ?
				"\"%s\" : { \"raw\" : %" PRIu32 ", \"version\" : \"%d.%d.%d\" }" :
				"%-65s%#8" PRIx32 " (%d.%d.%d)",
				e->name, e->version, u.major, u.minor, u.patch);
		} else {
			strbuf_append(what, str, "%s:%#" PRIx32, e->name, e->version);
		}
	}
	if (output->json)
		strbuf_append_str(what, str, " }");
}


void strbuf_named_uint(const char *what, struct _strbuf *str, const cl_uint *ext, size_t num_exts, const struct opt_out *output,
	const char* const* human_str, const char* const* raw_str, const size_t count, const size_t offset)
{
	const char *quote = output->json ? "\"" : "";
	const char * const * name_str = output->mode == CLINFO_HUMAN ? human_str : raw_str;
	set_common_separator(output);
	if (output->json)
		strbuf_append_str_len(what, str, "[ ", 2);

	for (size_t cursor = 0; cursor < num_exts; ++cursor) {
		/* add separator for values past the first */
		if (cursor > 0) strbuf_append_str(what, str, sep);

		cl_uint val = ext[cursor];
		cl_bool known = (val >= offset && val < offset + count);
		if (known) 
			strbuf_append(what, str, "%s%s%s", quote, name_str[val - offset], quote);
		else
			strbuf_append(what, str, "%s%#" PRIx32 "%s", quote, val, quote);
	}
	if (output->json)
		strbuf_append_str_len(what, str, " ]", 2);
}

void strbuf_ext_mem(const char *what, struct _strbuf *str, const cl_external_memory_handle_type_khr *ext, size_t num_exts,
	const struct opt_out *output)
{
	strbuf_named_uint(what, str, ext, num_exts, output,
		ext_mem_handle_str, ext_mem_handle_raw_str, ext_mem_handle_count, ext_mem_handle_offset);
}

void strbuf_semaphore_type(const char *what, struct _strbuf *str, const cl_semaphore_type_khr *ext, size_t num_exts,
	const struct opt_out *output)
{
	strbuf_named_uint(what, str, ext, num_exts, output,
		semaphore_type_str, semaphore_type_raw_str, semaphore_type_count, semaphore_type_offset);
}

void strbuf_ext_semaphore_handle(const char *what, struct _strbuf *str, const cl_external_semaphore_handle_type_khr *ext, size_t num_exts,
	const struct opt_out *output)
{
	strbuf_named_uint(what, str, ext, num_exts, output,
		semaphore_handle_str, semaphore_handle_raw_str, semaphore_handle_count, semaphore_handle_offset);
}


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

/* print a JSON string version of NULL-terminated string str, escaping \ and " and wrapping it all in "
 */
static inline
void json_stringify(const char *str)
{
	putchar('"');
	while (*str) {
		if (*str == '\\' || *str == '"')
			putchar('\\');
		putchar(*str);
		++str;
	}
	putchar('"');
}

/* print JSON version of strbuf, prefixed by pname, skipping leading whitespace if skip is nonzero,
 * quoting and escaping as string if is_string is nonzero
 */
static inline
void json_strbuf(const struct _strbuf *strbuf, const char *pname, cl_uint n, cl_bool is_string)
{
	printf("%s\"%s\" : ", (n > 0 ? comma_str : spc_str), pname);
	if (is_string)
		json_stringify(strbuf->buf);
	else
		fputs(strbuf->buf, stdout);
}

void
platform_info_str(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out* UNUSED(output))
{
	GET_STRING_LOC(ret, loc, clGetPlatformInfo, loc->plat, loc->param.plat);
	ret->needs_escaping = CL_TRUE;
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
	strbuf_append(loc->pname, &ret->str, "%" PRIu64, ret->value.u64);
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
	strbuf_append(loc->pname, &ret->str, "%" PRIuS, ret->value.s);
}

void
platform_info_version(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	ret->err = REPORT_ERROR_LOC(ret,
		clGetPlatformInfo(loc->plat, loc->param.plat, sizeof(ret->value.u32), &ret->value.u32, NULL),
		loc, "get %s");
	CHECK_SIZE(ret, loc, ret->value.u32, clGetPlatformInfo, loc->plat, loc->param.plat);
	if (!ret->err) {
		strbuf_append(loc->pname, &ret->str,
			output->json ? "{ \"raw\" : %" PRIu32 ", \"version\" :" : "%#" PRIx32,
			ret->value.u32);
		if (output->json || output->mode == CLINFO_HUMAN) {
			strbuf_version(loc->pname, &ret->str,
				output->json ? " \"" : " (",
				ret->value.u32,
				output->json ? "\" }" : ")");
		}
	}
}

void
platform_info_ext_version(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_name_version *ext = NULL;
	size_t nusz = 0;
	ret->err = REPORT_ERROR_LOC(ret,
		clGetPlatformInfo(loc->plat, loc->param.plat, 0, NULL, &nusz),
		loc, "get %s size");
	if (!ret->err) {
		REALLOC(ext, nusz, loc->sname);
		ret->err = REPORT_ERROR_LOC(ret,
			clGetPlatformInfo(loc->plat, loc->param.plat, nusz, ext, NULL),
			loc, "get %s");
	}
	if (!ret->err) {
		size_t num_exts = nusz / sizeof(*ext);
		strbuf_name_version(loc->pname, &ret->str, ext, num_exts, output);
	}
	free(ext);
}

void
platform_info_ext_mem(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_external_memory_handle_type_khr *ext = NULL;
	size_t nusz = 0;
	ret->err = REPORT_ERROR_LOC(ret,
		clGetPlatformInfo(loc->plat, loc->param.plat, 0, NULL, &nusz),
		loc, "get %s size");
	if (!ret->err) {
		REALLOC(ext, nusz, loc->sname);
		ret->err = REPORT_ERROR_LOC(ret,
			clGetPlatformInfo(loc->plat, loc->param.plat, nusz, ext, NULL),
			loc, "get %s");
	}
	if (!ret->err) {
		size_t num_exts = nusz / sizeof(*ext);
		strbuf_ext_mem(loc->pname, &ret->str, ext, num_exts, output);
	}
	free(ext);
}

void
platform_info_semaphore_types(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_semaphore_type_khr *ext = NULL;
	size_t nusz = 0;
	ret->err = REPORT_ERROR_LOC(ret,
		clGetPlatformInfo(loc->plat, loc->param.plat, 0, NULL, &nusz),
		loc, "get %s size");
	if (!ret->err) {
		REALLOC(ext, nusz, loc->sname);
		ret->err = REPORT_ERROR_LOC(ret,
			clGetPlatformInfo(loc->plat, loc->param.plat, nusz, ext, NULL),
			loc, "get %s");
	}
	if (!ret->err) {
		size_t num_exts = nusz / sizeof(*ext);
		strbuf_semaphore_type(loc->pname, &ret->str, ext, num_exts, output);
	}
	free(ext);
}

void
platform_info_ext_semaphore_handles(struct platform_info_ret *ret,
	const struct info_loc *loc, const struct platform_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_external_semaphore_handle_type_khr *ext = NULL;
	size_t nusz = 0;
	ret->err = REPORT_ERROR_LOC(ret,
		clGetPlatformInfo(loc->plat, loc->param.plat, 0, NULL, &nusz),
		loc, "get %s size");
	if (!ret->err) {
		REALLOC(ext, nusz, loc->sname);
		ret->err = REPORT_ERROR_LOC(ret,
			clGetPlatformInfo(loc->plat, loc->param.plat, nusz, ext, NULL),
			loc, "get %s");
	}
	if (!ret->err) {
		size_t num_exts = nusz / sizeof(*ext);
		strbuf_ext_semaphore_handle(loc->pname, &ret->str, ext, num_exts, output);
	}
	free(ext);
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

cl_bool plat_is_30(const struct platform_info_checks *chk)
{
	return !(chk->plat_version < 30);
}

cl_bool plat_has_amd_object_metadata(const struct platform_info_checks *chk)
{
	return chk->has_amd_object_metadata;
}

cl_bool plat_has_ext_ver(const struct platform_info_checks *chk)
{
	return plat_is_30(chk) || chk->has_extended_versioning;
}

cl_bool plat_has_ext_mem(const struct platform_info_checks *chk)
{
	return chk->has_external_memory;
}

cl_bool plat_has_semaphore(const struct platform_info_checks *chk)
{
	return chk->has_semaphore;
}

cl_bool plat_has_external_semaphore(const struct platform_info_checks *chk)
{
	return chk->has_external_semaphore;
}

#define PINFO_COND(symbol, name, sfx, typ, funcptr) { symbol, #symbol, "Platform " name, sfx, &platform_info_##typ, &funcptr }
#define PINFO(symbol, name, sfx, typ) { symbol, #symbol, "Platform " name, sfx, &platform_info_##typ, NULL }
struct platform_info_traits pinfo_traits[] = {
	PINFO(CL_PLATFORM_NAME, "Name", NULL, str),
	PINFO(CL_PLATFORM_VENDOR, "Vendor", NULL, str),
	PINFO(CL_PLATFORM_VERSION, "Version", NULL, str),
	PINFO(CL_PLATFORM_PROFILE, "Profile", NULL, str),
	PINFO(CL_PLATFORM_EXTENSIONS, "Extensions", NULL, str),
	PINFO_COND(CL_PLATFORM_EXTENSIONS_WITH_VERSION, "Extensions with Version", NULL, ext_version, plat_has_ext_ver),
	PINFO_COND(CL_PLATFORM_NUMERIC_VERSION, "Numeric Version", NULL, version, plat_has_ext_ver),
	PINFO_COND(CL_PLATFORM_ICD_SUFFIX_KHR, "Extensions function suffix", NULL, str, khr_icd_p),
	PINFO_COND(CL_PLATFORM_MAX_KEYS_AMD, "Max metadata object keys (AMD)", NULL, sz, plat_has_amd_object_metadata),
	PINFO_COND(CL_PLATFORM_HOST_TIMER_RESOLUTION, "Host timer resolution", "ns", ulong, plat_is_21),
	PINFO_COND(CL_PLATFORM_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR, "External memory handle types", NULL, ext_mem, plat_has_ext_mem),
	PINFO_COND(CL_PLATFORM_SEMAPHORE_TYPES_KHR, "Semaphore types", NULL, semaphore_types, plat_has_semaphore),
	PINFO_COND(CL_PLATFORM_SEMAPHORE_IMPORT_HANDLE_TYPES_KHR, "External semaphore import types", NULL, ext_semaphore_handles, plat_has_external_semaphore),
	PINFO_COND(CL_PLATFORM_SEMAPHORE_EXPORT_HANDLE_TYPES_KHR, "External semaphore export types", NULL, ext_semaphore_handles, plat_has_external_semaphore),

};

/* Collect (and optionally show) information on a specific platform,
 * initializing relevant arrays and optionally showing the collected
 * information
 */
void
gatherPlatformInfo(struct platform_list *plist, cl_uint p, const struct opt_out *output)
{
	size_t len = 0;
	cl_uint n = 0; /* number of platform properties shown, for JSON */

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
		cl_bool requested;

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

		reset_strbuf(&ret.str);
		reset_strbuf(&ret.err_str);
		ret.needs_escaping = CL_FALSE;
		traits->show_func(&ret, &loc, pinfo_checks, output);

		/* The property is skipped if this was a conditional property,
		 * unsatisfied, there was an error retrieving it and cond_prop_mode is not
		 * COND_PROP_SHOW.
		 */
		if (ret.err && !checked && output->cond != COND_PROP_SHOW)
			continue;

		/* The property gets printed if we are not just listing,
		 * or if the user requested a property and this one matches.
		 * Otherwise, we're just gathering information */
		requested = (output->prop && strstr(loc.sname, output->prop) != NULL);
		if (output->detailed || requested) {
			if (output->json) {
				json_strbuf(RET_BUF(ret), loc.pname, n++, ret.err || ret.needs_escaping);
			} else {
				show_strbuf(RET_BUF(ret), loc.pname, CL_FALSE, ret.err);
			}
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
			/* We print the platform name here in the JSON + brief case */
			if (output->json && output->brief) json_stringify(pdata->pname);
			break;
		case CL_PLATFORM_VERSION:
			/* compute numeric value for OpenCL version */
			pinfo_checks->plat_version = getOpenCLVersion(ret.str.buf + 7);
			break;
		case CL_PLATFORM_EXTENSIONS:
			pinfo_checks->has_khr_icd = !!strstr(ret.str.buf, "cl_khr_icd");
			pinfo_checks->has_amd_object_metadata = !!strstr(ret.str.buf, "cl_amd_object_metadata");
			pinfo_checks->has_external_memory = !!strstr(ret.str.buf, "cl_khr_external_memory");
			pinfo_checks->has_semaphore = !!strstr(ret.str.buf, "cl_khr_semaphore");
			pinfo_checks->has_external_semaphore = !!strstr(ret.str.buf, "cl_khr_external_semaphore");
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
		ALLOC(pdata->sname, SNAME_MAX+1, "platform symbolic name");
		snprintf(pdata->sname, SNAME_MAX, "P%" PRIu32 "", p);
	}

	len = strlen(pdata->sname);
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
 * TODO add version control for 3.0+ platforms
 */

struct device_info_checks {
	const struct platform_info_checks *pinfo_checks;
	cl_device_type devtype;
	cl_device_mem_cache_type cachetype;
	cl_device_local_mem_type lmemtype;
	cl_bool image_support;
	cl_bool compiler_available;
	cl_bool arm_register_alloc_support;
	cl_bool arm_warp_count_support;
	char has_half[12];
	char has_double[24];
	char has_nv[29];
	char has_amd[30];
	char has_intel[32];
	char has_amd_svm[11];
	char has_arm_svm[29];
	char has_intel_usm[31];
	char has_external_memory[23];
	char has_semaphore[17];
	char has_external_semaphore[26];
	char has_arm_core_id[15];
	char has_arm_job_slots[26];
	char has_arm_scheduling_controls[27];
	char has_fission[22];
	char has_atomic_counters[26];
	char has_image2d_buffer[27];
	char has_il_program[18];
	char has_intel_queue_families[32];
	char has_intel_local_thread[30];
	char has_intel_AME[36];
	char has_intel_AVC_ME[43];
	char has_intel_planar_yuv[20];
	char has_intel_required_subgroup_size[32];
	char has_altera_dev_temp[29];
	char has_p2p[23];
	char has_pci_bus_info[20];
	char has_spir[12];
	char has_qcom_ext_host_ptr[21];
	char has_simultaneous_sharing[30];
	char has_subgroup_named_barrier[30];
	char has_command_buffer[25];
	char has_mutable_dispatch[27];
	char has_terminate_context[25];
	char has_terminate_arm[37];
	char has_extended_versioning[27];
	char has_cxx_for_opencl[22];
	char has_device_uuid[19];
	cl_uint dev_version;
	cl_uint p2p_num_devs;
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
DEFINE_EXT_CHECK(intel_usm)
DEFINE_EXT_CHECK(external_memory)
DEFINE_EXT_CHECK(semaphore)
DEFINE_EXT_CHECK(external_semaphore)
DEFINE_EXT_CHECK(arm_core_id)
DEFINE_EXT_CHECK(arm_job_slots)
DEFINE_EXT_CHECK(arm_scheduling_controls)
DEFINE_EXT_CHECK(fission)
DEFINE_EXT_CHECK(atomic_counters)
DEFINE_EXT_CHECK(il_program)
DEFINE_EXT_CHECK(intel)
DEFINE_EXT_CHECK(intel_queue_families)
DEFINE_EXT_CHECK(intel_local_thread)
DEFINE_EXT_CHECK(intel_AME)
DEFINE_EXT_CHECK(intel_AVC_ME)
DEFINE_EXT_CHECK(intel_planar_yuv)
DEFINE_EXT_CHECK(intel_required_subgroup_size)
DEFINE_EXT_CHECK(altera_dev_temp)
DEFINE_EXT_CHECK(p2p)
DEFINE_EXT_CHECK(pci_bus_info)
DEFINE_EXT_CHECK(spir)
DEFINE_EXT_CHECK(qcom_ext_host_ptr)
DEFINE_EXT_CHECK(simultaneous_sharing)
DEFINE_EXT_CHECK(subgroup_named_barrier)
DEFINE_EXT_CHECK(command_buffer)
DEFINE_EXT_CHECK(mutable_dispatch)
DEFINE_EXT_CHECK(terminate_context)
DEFINE_EXT_CHECK(terminate_arm)
DEFINE_EXT_CHECK(extended_versioning)
DEFINE_EXT_CHECK(cxx_for_opencl)
DEFINE_EXT_CHECK(device_uuid)

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

// device supports 3.0
cl_bool dev_is_30(const struct device_info_checks *chk)
{
	return !(chk->dev_version < 30);
}

// device has extended versioning: 3.0 or has_extended_versioning
cl_bool dev_has_ext_ver(const struct device_info_checks *chk)
{
	return dev_is_30(chk) || dev_has_extended_versioning(chk);
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

/* Device supports cl_intel_device_attribute_query and is a GPU */
cl_bool dev_is_gpu_intel(const struct device_info_checks *chk)
{
	return dev_is_gpu(chk) && dev_has_intel(chk);
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

/* Device supports register allocation queries */
cl_bool dev_has_arm_register_alloc(const struct device_info_checks *chk)
{
	return dev_has_arm_scheduling_controls(chk) && chk->arm_register_alloc_support;
}

/* Device supports warp  */
cl_bool dev_has_arm_warp_count_support(const struct device_info_checks *chk)
{
	return dev_has_arm_scheduling_controls(chk) && chk->arm_warp_count_support;
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

cl_bool dev_has_image2d_buffer(const struct device_info_checks *chk)
{
	return dev_has_images_20(chk) || !!(chk->has_image2d_buffer[0]);
}

cl_bool dev_has_compiler(const struct device_info_checks *chk)
{
	return chk->compiler_available;
}

cl_bool dev_has_compiler_11(const struct device_info_checks *chk)
{
	return dev_is_11(chk) && dev_has_compiler(chk);
}

cl_bool dev_has_p2p_devs(const struct device_info_checks *chk)
{
	return dev_has_p2p(chk) && chk->p2p_num_devs > 0;
}


void identify_device_extensions(const char *extensions, struct device_info_checks *chk)
{
#define _HAS_EXT(ext) (strstr(extensions, ext))
#define CPY_EXT(what, ext) do { \
	strncpy(chk->has_##what, has+1, sizeof(ext)); \
	chk->has_##what[sizeof(ext)-1] = '\0'; \
} while (0)
#define CHECK_EXT(what, ext) do { \
	has = _HAS_EXT(" " #ext " "); \
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
	CHECK_EXT(intel, cl_intel_device_attribute_query);
	CHECK_EXT(amd_svm, cl_amd_svm);
	CHECK_EXT(arm_svm, cl_arm_shared_virtual_memory);
	CHECK_EXT(intel_usm, cl_intel_unified_shared_memory);
	CHECK_EXT(external_memory, cl_khr_external_memory);
	CHECK_EXT(semaphore, cl_khr_semaphore);
	CHECK_EXT(external_semaphore, cl_khr_external_semaphore);
	CHECK_EXT(arm_core_id, cl_arm_core_id);
	CHECK_EXT(arm_job_slots, cl_arm_job_slot_selection);
	CHECK_EXT(arm_scheduling_controls, cl_arm_scheduling_controls);
	CHECK_EXT(fission, cl_ext_device_fission);
	CHECK_EXT(atomic_counters, cl_ext_atomic_counters_64);
	if (dev_has_atomic_counters(chk))
		CHECK_EXT(atomic_counters, cl_ext_atomic_counters_32);
	CHECK_EXT(image2d_buffer, cl_khr_image2d_from_buffer);
	CHECK_EXT(il_program, cl_khr_il_program);
	CHECK_EXT(intel_queue_families, cl_intel_command_queue_families);
	CHECK_EXT(intel_local_thread, cl_intel_exec_by_local_thread);
	CHECK_EXT(intel_AME, cl_intel_advanced_motion_estimation);
	CHECK_EXT(intel_AVC_ME, cl_intel_device_side_avc_motion_estimation);
	CHECK_EXT(intel_planar_yuv, cl_intel_planar_yuv);
	CHECK_EXT(intel_required_subgroup_size, cl_intel_required_subgroup_size);
	CHECK_EXT(altera_dev_temp, cl_altera_device_temperature);
	CHECK_EXT(p2p, cl_amd_copy_buffer_p2p);
	CHECK_EXT(pci_bus_info, cl_khr_pci_bus_info);
	CHECK_EXT(qcom_ext_host_ptr, cl_qcom_ext_host_ptr);
	CHECK_EXT(simultaneous_sharing, cl_intel_simultaneous_sharing);
	CHECK_EXT(subgroup_named_barrier, cl_khr_subgroup_named_barrier);
	CHECK_EXT(command_buffer, cl_khr_command_buffer);
	CHECK_EXT(mutable_dispatch, cl_khr_mutable_dispatch);
	CHECK_EXT(terminate_context, cl_khr_terminate_context);
	CHECK_EXT(terminate_arm, cl_arm_controlled_kernel_termination);
	CHECK_EXT(extended_versioning, cl_khr_extended_versioning);
	CHECK_EXT(cxx_for_opencl, cl_ext_cxx_for_opencl);
	CHECK_EXT(device_uuid, cl_khr_device_uuid);
}


/*
 * Device info print functions
 */

#define _GET_VAL(ret, loc, val) \
	ret->err = REPORT_ERROR_LOC(ret, \
		clGetDeviceInfo((loc)->dev, (loc)->param.dev, sizeof(val), &(val), NULL), \
		loc, "get %s"); \
	CHECK_SIZE(ret, loc, val, clGetDeviceInfo, (loc)->dev, (loc)->param.dev);

#define _GET_VAL_VALUES(ret, loc) \
	REALLOC(val, numval, loc->sname); \
	ret->err = REPORT_ERROR_LOC(ret, \
		clGetDeviceInfo(loc->dev, loc->param.dev, szval, val, NULL), \
		loc, "get %s"); \
	if (ret->err) { free(val); val = NULL; } \

#define _GET_VAL_ARRAY(ret, loc) \
	ret->err = REPORT_ERROR_LOC(ret, \
		clGetDeviceInfo(loc->dev, loc->param.dev, 0, NULL, &szval), \
		loc, "get number of %s"); \
	numval = szval/sizeof(*val); \
	if (!ret->err && numval > 0) { \
		_GET_VAL_VALUES(ret, loc) \
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
DEFINE_DEVINFO_FETCH(cl_version, u32)
DEFINE_DEVINFO_FETCH(cl_ulong, u64)
DEFINE_DEVINFO_FETCH(cl_bitfield, u64)
DEFINE_DEVINFO_FETCH(cl_device_type, devtype)
DEFINE_DEVINFO_FETCH(cl_device_mem_cache_type, cachetype)
DEFINE_DEVINFO_FETCH(cl_device_local_mem_type, lmemtype)
DEFINE_DEVINFO_FETCH(cl_device_topology_amd, devtopo_amd)
DEFINE_DEVINFO_FETCH(cl_device_pci_bus_info_khr, devtopo_khr)
DEFINE_DEVINFO_FETCH(cl_device_affinity_domain, affinity_domain)
DEFINE_DEVINFO_FETCH(cl_device_fp_config, fpconfig)
DEFINE_DEVINFO_FETCH(cl_command_queue_properties, qprop)
DEFINE_DEVINFO_FETCH(cl_device_exec_capabilities, execap)
DEFINE_DEVINFO_FETCH(cl_device_svm_capabilities, svmcap)
DEFINE_DEVINFO_FETCH(cl_device_terminate_capability_khr, termcap)

#define DEV_FETCH_LOC(type, var, loc) \
	type var = device_fetch_##type(ret, loc, chk, output)
#define DEV_FETCH(type, var) DEV_FETCH_LOC(type, var, loc)

#define FMT_VAL(loc, ret, fmt, val) if (!ret->err) strbuf_append(loc->pname, &ret->str, fmt, val)

#define DEFINE_DEVINFO_SHOW(how, type, field, fmt) \
void \
device_info_##how(struct device_info_ret *ret, \
	const struct info_loc *loc, const struct device_info_checks* chk, \
	const struct opt_out *output) \
{ \
	DEV_FETCH(type, val); \
	if (!ret->err) FMT_VAL(loc, ret, fmt, val); \
}

DEFINE_DEVINFO_SHOW(int, cl_uint, u32, "%" PRIu32)
DEFINE_DEVINFO_SHOW(hex, cl_uint, u32, output->json ? "%" PRIu32 : "%#" PRIx32)
DEFINE_DEVINFO_SHOW(long, cl_ulong, u64, "%" PRIu64)
DEFINE_DEVINFO_SHOW(sz, size_t, s, "%" PRIuS)

void
device_info_str(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out* UNUSED(output))
{
	GET_STRING_LOC(ret, loc, clGetDeviceInfo, loc->dev, loc->param.dev);
	ret->needs_escaping = CL_TRUE;
}

void
device_info_bool(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_bool, val);
	if (!ret->err) {
		const char * const * str = (output->mode == CLINFO_HUMAN ?
			bool_str : output->json ? bool_json_str : bool_raw_str);
		strbuf_append(loc->pname, &ret->str, "%s", str[val]);
	}
}

void
device_info_bits(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	DEV_FETCH(cl_uint, val);
	if (!ret->err)
		strbuf_append(loc->pname, &ret->str, "%" PRIu32 " bits (%" PRIu32 " bytes)", val, val/8);
}

void
device_info_version(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, u32);
	if (!ret->err) {
		strbuf_append(loc->pname, &ret->str,
			output->json ? "{ \" raw \" : %" PRIu32 ", \"version\" :" : "%#" PRIx32,
			ret->value.u32);
		if (output->json || output->mode == CLINFO_HUMAN) {
			strbuf_version(loc->pname, &ret->str,
				output->json ? " \"" : " (",
				ret->value.u32,
				output->json ? "\" }" : ")");
		}
	}
}

void
device_info_ext_version(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_name_version *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		strbuf_name_version(loc->pname, &ret->str, val, numval, output);
	}
	free(val);
}

void
device_info_ext_mem(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_external_memory_handle_type_khr *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		strbuf_ext_mem(loc->pname, &ret->str, val, numval, output);
	}
	free(val);
}

void
device_info_semaphore_types(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_semaphore_type_khr *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		strbuf_semaphore_type(loc->pname, &ret->str, val, numval, output);
	}
	free(val);
}

void
device_info_ext_semaphore_handles(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_external_semaphore_handle_type_khr *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		strbuf_ext_semaphore_handle(loc->pname, &ret->str, val, numval, output);
	}
	free(val);
}

void strbuf_mem(const char *what, struct _strbuf *str, cl_ulong val)
{
	double dbl = (double)val;
	size_t sfx = 0;
	while (dbl > 1024 && sfx < memsfx_end) {
		dbl /= 1024;
		++sfx;
	}
	strbuf_append(what, str, " (%.4lg%s)", dbl, memsfx[sfx]);
}

void
device_info_mem(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, u64);
	if (!ret->err) {
		strbuf_append(loc->pname, &ret->str, "%" PRIu64, ret->value.u64);
		if (output->mode == CLINFO_HUMAN && ret->value.u64 > 1024)
			strbuf_mem(loc->pname, &ret->str, ret->value.u64);
	}
}

void
device_info_mem_int(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, u32);
	if (!ret->err) {
		strbuf_append(loc->pname, &ret->str, "%" PRIu32, ret->value.u32);
		if (output->mode == CLINFO_HUMAN && ret->value.u32 > 1024)
			strbuf_mem(loc->pname, &ret->str, ret->value.u32);
	}
}

void
device_info_mem_sz(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, s);
	if (!ret->err) {
		strbuf_append(loc->pname, &ret->str, "%" PRIuS, ret->value.s);
		if (output->mode == CLINFO_HUMAN && ret->value.s > 1024)
			strbuf_mem(loc->pname, &ret->str, ret->value.s);
	}
}

void
device_info_free_mem_amd(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	// Apparently, with the introduction of ROCm, CL_DEVICE_GLOBAL_FREE_MEMORY_AMD
	// returns 1 or 2 values depending on how it's called: if it's called with a
	// szval < 2*sizeof(size_t), it will only return 1 value, otherwise it will return 2.
	// At least now these are documented in the ROCm source code: the first value
	// is the total amount of free memory, and the second is the size of the largest
	// free block. So let's just manually ask for both values
	GET_VAL(ret, loc, u64v2);
	if (!ret->err) {
		size_t cursor = 0;
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " [", 2);
		for (cursor = 0; cursor < 2; ++cursor) {
			cl_ulong v = ret->value.u64v2.s[cursor];
			if (cursor > 0)
				strbuf_append_str(loc->pname, &ret->str,
					output->json ? comma_str : spc_str);
			strbuf_append(loc->pname, &ret->str, "%" PRIuS, v);
			if (output->mode == CLINFO_HUMAN)
				strbuf_mem(loc->pname, &ret->str, v*UINT64_C(1024));
		}
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " ]", 2);
	}
}

void
device_info_time_offset(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, u64);
	if (!ret->err) {
		time_t time = ret->value.u64/UINT64_C(1000000000);
		strbuf_append(loc->pname, &ret->str, "%" PRIu64 "ns (", ret->value.u64);
		strbuf_append_str(loc->pname, &ret->str, ctime(&time));
		/* overwrite ctime's newline with the closing parenthesis */
		ret->str.buf[ret->str.end - 1] = ')';
	}
}

void
device_info_intptr(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_int *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		size_t counter = 0;
		set_separator(output->mode == CLINFO_HUMAN ? comma_str : output->json ? comma_str : spc_str);
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " [", 2);
		for (counter = 0; counter < numval; ++counter) {
			if (counter > 0) strbuf_append_str(loc->pname, &ret->str, sep);
			strbuf_append(loc->pname, &ret->str, "%" PRId32, val[counter]);
		}
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " ]", 2);
		// TODO: ret->value.??? = val;
	}
	free(val);
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
		set_separator(output->mode == CLINFO_HUMAN ? human_sep : output->json ? comma_str : spc_str);
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " [", 2);
		for (counter = 0; counter < numval; ++counter) {
			if (counter > 0) strbuf_append_str(loc->pname, &ret->str, sep);
			strbuf_append(loc->pname, &ret->str, "%" PRIuS, val[counter]);
		}
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " ]", 2);
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
		init_strbuf(&logbuf, "program build log");
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
		strbuf_append(__func__, &ret->str, "sum%u", 1<<cursor);
		if (cursor == 0)
			ret->str.buf[3] = 0; // scalar kernel is called 'sum'
		krn = clCreateKernel(prg, ret->str.buf, &ret->err);
		reset_strbuf(&ret->str);
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
		strbuf_append("get WG sizes", &ret->str, "%" PRIuS, wgm[0]);
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
			strbuf_append("image size 2D", &ret->str, "%" PRIuS "x%" PRIuS, width, height);
		}
	}
	ret->value.u64v.s[0] = width;
	ret->value.u64v.s[1] = height;
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
			 strbuf_append("image size planar YUV", &ret->str, "%" PRIuS "x%" PRIuS, width, height);
		}
	}
	ret->value.u64v.s[0] = width;
	ret->value.u64v.s[1] = height;
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
				strbuf_append("image size 3D", &ret->str,
					"%" PRIuS "x%" PRIuS "x%" PRIuS,
					width, height, depth);
			}
		}
	}
	ret->value.u64v.s[0] = width;
	ret->value.u64v.s[1] = height;
	ret->value.u64v.s[2] = depth;
}

void strbuf_bitfield(const char *what, struct _strbuf *str,
	cl_bitfield bits, const char *bits_name,
	const char * const *bit_str, size_t bit_str_count,
	const struct opt_out *output)
{
	const char *quote = output->json ? "\"" : "";
	/* number of matches so far, for separator placement */
	cl_uint count = 0;
	/* iterator */
	cl_uint i = 0;
	/* leftovers bits */
	cl_bitfield known_mask, extra;

	set_common_separator(output);

	if (output->json)
		strbuf_append(what, str,
			"{ \"raw\" : %" PRIu64 ", \"%s\" : [ ",
			bits, bits_name);

	if (bits) {
		for (i = 0; i < bit_str_count; ++i) {
			if (bits & (1UL << i)) {
				strbuf_append(what, str, "%s%s%s%s",
					(count > 0 ? sep : ""),
					quote, bit_str[i], quote);
				++count;
			}
		}

		/* check for extra bits */
		known_mask = ((cl_bitfield)(1) << bit_str_count) - 1;
		extra = bits & ~known_mask;
		if (extra) {
			strbuf_append(what, str, "%s%s%#" PRIx64 "%s",
				(count > 0 ? sep : ""), quote, extra, quote);
		}
	}

	if (output->json)
		strbuf_append_str(what, str, " ] }");
}


void
device_info_bitfield(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output,
	const cl_bitfield bits,
	const size_t bit_str_count, /* number of entries in bit_str */
	const char * const * bit_str, /* array of strings describing the bits */
	const char * bits_name) /* JSON name for this bitfield */
{
	strbuf_bitfield(loc->pname, &ret->str, bits, bits_name, bit_str, bit_str_count, output);
}


/* This could use device_info_bitfield, but we prefer to go through fields in reverse,
 * so we just dup the code
 */
void
device_info_devtype(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, devtype);
	if (!ret->err) {
		const char *quote = output->json ? "\"" : "";
		const char * const *devstr = (output->mode == CLINFO_HUMAN ?
			device_type_str : device_type_raw_str);
		cl_uint i = (cl_uint)actual_devtype_count;
		/* number of matches so far, for separator placement */
		cl_uint count = 0;
		/* leftovers bits */
		cl_device_type known_mask, extra;

		set_common_separator(output);

		if (output->json)
			strbuf_append(loc->pname, &ret->str,
				"{ \"raw\" : %" PRIu64 ", \"type\" : [ ",
				ret->value.devtype);

		/* iterate over device type strings, appending their textual form
		 * to ret->str */
		for (; i > 0; --i) {
			/* assemble CL_DEVICE_TYPE_* from index i */
			cl_device_type cur = (cl_device_type)(1) << (i-1);
			if (ret->value.devtype & cur) {
				/* match: add separator if not first match */
				strbuf_append(loc->pname, &ret->str, "%s%s%s%s",
					(count > 0 ? sep : ""),
					quote, devstr[i], quote);
				++count;
			}
		}

		/* check for extra bits */
		known_mask = ((cl_device_type)(1) << actual_devtype_count) - 1;
		extra = ret->value.devtype & ~known_mask;
		if (extra) {
			strbuf_append(loc->pname, &ret->str, "%s%s%#" PRIx64 "%s",
				(count > 0 ? sep : ""), quote, extra, quote);
		}

		if (output->json)
			strbuf_append_str(loc->pname, &ret->str, " ] }");
	}
}

void
device_info_cachetype(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, cachetype);
	if (!ret->err) {
		const char * const *ar = (output->mode == CLINFO_HUMAN ?
			cache_type_str : cache_type_raw_str);
		strbuf_append_str(loc->pname, &ret->str, ar[ret->value.cachetype]);
		ret->needs_escaping = CL_TRUE;
	}
}

void
device_info_lmemtype(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, lmemtype);
	if (!ret->err) {
		const char * const *ar = (output->mode == CLINFO_HUMAN ?
			lmem_type_str : lmem_type_raw_str);
		strbuf_append_str(loc->pname, &ret->str, ar[ret->value.lmemtype]);
		ret->needs_escaping = CL_TRUE;
	}
}

void
device_info_atomic_caps(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, bits);
	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.bits,
			atomic_cap_count, (output->mode == CLINFO_HUMAN ?
				atomic_cap_str : atomic_cap_raw_str),
			"capabilities");
	}
}

void
device_info_device_enqueue_caps(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, bits);
	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.bits,
			device_enqueue_cap_count, (output->mode == CLINFO_HUMAN ?
				device_enqueue_cap_str : device_enqueue_cap_raw_str),
			"capabilities");
	}
}

/* cl_arm_core_id */
void
device_info_core_ids(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_ulong val;
	GET_VAL(ret, loc, u64);
	val = ret->value.u64;

	if (!ret->err) {
		const char *quote = output->json ? "\"" : "";
		/* The value is a bitfield where each set bit corresponds to a core ID
		 * value that can be returned by the device-side function. We print them
		 * here as ranges, such as 0-4, 8-12 */
		int range_start = -1;
		int cur_bit = 0;

		if (output->json)
			strbuf_append(loc->pname, &ret->str,
				"{ \"raw\" : %" PRIu64 ", \"core_ids\" : [ ",
				ret->value.u64);

		set_separator(empty_str);
#define CORE_ID_END 64
		do {
			/* Find the start of the range */
			while ((cur_bit < CORE_ID_END) && !((val >> cur_bit) & 1))
				++cur_bit;
			range_start = cur_bit++;

			/* Find the end of the range */
			while ((cur_bit < CORE_ID_END) && ((val >> cur_bit) & 1))
				++cur_bit;

			/* print the range [range_start, cur_bit[ */
			if (range_start >= 0 && range_start < CORE_ID_END) {
				strbuf_append(loc->pname, &ret->str, "%s%s%d", sep, quote, range_start);
				if (cur_bit - range_start > 1)
					strbuf_append(loc->pname, &ret->str, "-%d", cur_bit - 1);
				set_separator(comma_str);
				if (output->json)
					strbuf_append_str(loc->pname, &ret->str, quote);
			}
		} while (cur_bit < CORE_ID_END);

		if (output->json)
			strbuf_append_str(loc->pname, &ret->str, " ] }");
	}
}

/* cl_arm_job_slot_selection */
void
device_info_job_slots(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_uint val;
	GET_VAL(ret, loc, u32);
	val = ret->value.u32;

	if (!ret->err) {
		const char *quote = output->json ? "\"" : "";
		/* The value is a bitfield where each set bit corresponds to an available job slot.
		 * We print them here as ranges, such as 0-4, 8-12 */
		int range_start = -1;
		int cur_bit = 0;

		if (output->json)
			strbuf_append(loc->pname, &ret->str,
				"{ \"raw\" : %" PRIu32 ", \"slots\" : [ ",
				ret->value.u32);

		set_separator(empty_str);
#define JOB_SLOT_END 32
		do {
			/* Find the start of the range */
			while ((cur_bit < JOB_SLOT_END) && !((val >> cur_bit) & 1))
				++cur_bit;
			range_start = cur_bit++;

			/* Find the end of the range */
			while ((cur_bit < JOB_SLOT_END) && ((val >> cur_bit) & 1))
				++cur_bit;

			/* print the range [range_start, cur_bit[ */
			if (range_start >= 0 && range_start < JOB_SLOT_END) {
				strbuf_append(loc->pname, &ret->str, "%s%s%d", sep, quote, range_start);
				if (cur_bit - range_start > 1)
					strbuf_append(loc->pname, &ret->str, "-%d", cur_bit - 1);
				set_separator(comma_str);
				if (output->json)
					strbuf_append_str(loc->pname, &ret->str, quote);
			}
		} while (cur_bit < JOB_SLOT_END);

		if (output->json)
			strbuf_append_str(loc->pname, &ret->str, " ] }");
	}
}

void devtopo_pci_str(struct device_info_ret *ret, const cl_device_pci_bus_info_khr *devtopo)
{
	strbuf_append("devtopo", &ret->str, "PCI-E, %04x:%02x:%02x.%u",
		devtopo->pci_domain,
		devtopo->pci_bus,
		devtopo->pci_device, devtopo->pci_function);
	ret->value.devtopo_khr = *devtopo;
}

void
device_info_devtopo_khr(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, devtopo_khr);
	/* TODO how to do this in CLINFO_RAW mode */
	if (!ret->err) {
		devtopo_pci_str(ret, &ret->value.devtopo_khr);
		/* TODO JSONify */
		ret->needs_escaping = CL_TRUE;
	}
}


/* stringify a cl_device_topology_amd */
void devtopo_amd_str(struct device_info_ret *ret, const cl_device_topology_amd *devtopo)
{
	cl_device_pci_bus_info_khr devtopo_info;

	switch (devtopo->raw.type) {
	case 0:
		/* leave empty */
		break;
	case CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD:
		devtopo_info.pci_domain = 0;
		devtopo_info.pci_bus = devtopo->pcie.bus;
		devtopo_info.pci_device = devtopo->pcie.device;
		devtopo_info.pci_function = devtopo->pcie.function;
		devtopo_pci_str(ret, &devtopo_info);
		break;
	default:
		strbuf_append("devtopo", &ret->str, "<unknown (%u): %u %u %u %u %u>",
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
	GET_VAL(ret, loc, devtopo_amd);
	/* TODO how to do this in CLINFO_RAW mode */
	if (!ret->err) {
		devtopo_amd_str(ret, &ret->value.devtopo_amd);
		/* TODO JSONify */
		ret->needs_escaping = CL_TRUE;
	}
}

/* we assemble a clinfo_device_topology_pci struct from the NVIDIA info */
void
device_info_devtopo_nv(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	struct info_loc loc2 = *loc;
	cl_device_pci_bus_info_khr devtopo;
	DEV_FETCH(cl_uint, val); /* CL_DEVICE_PCI_BUS_ID_NV */
	if (!ret->err) {
		devtopo.pci_bus = val & 0xff;
		RESET_LOC_PARAM(loc2, dev, CL_DEVICE_PCI_SLOT_ID_NV);
		_GET_VAL(ret, &loc2, val);

		if (!ret->err) {
			cl_int safe_err;
			devtopo.pci_device = (val >> 3) & 0xff;
			devtopo.pci_function = val & 7;

			/* CL_DEVICE_PCI_DOMAIN_ID_NV is not supported in older drivers,
			 * but we have no way to check other than querying, and recovering
			 * in the CL_INVALID_VALUE case */
			RESET_LOC_PARAM(loc2, dev, CL_DEVICE_PCI_DOMAIN_ID_NV);
			safe_err = clGetDeviceInfo(loc2.dev, CL_DEVICE_PCI_DOMAIN_ID_NV,
				sizeof(val), &val, NULL);
			if (safe_err == CL_SUCCESS) {
				devtopo.pci_domain = val;
			} else if (safe_err == CL_INVALID_VALUE) {
				devtopo.pci_domain = 0;
			} else {
				REPORT_ERROR_LOC(ret, safe_err, &loc2, "get CL_DEVICE_PCI_DOMAIN_ID_NV");
			}
			if (!ret->err)
				devtopo_pci_str(ret, &devtopo);
		}
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
			strbuf_append("NV CC", &ret->str, "%" PRIu32 ".%" PRIu32 "", major, minor);
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
			strbuf_append("AMD GFXIP", &ret->str, "%" PRIu32 ".%" PRIu32 "", major, minor);
		}
	}
	ret->value.u32v.s[0] = major;
	ret->value.u32v.s[1] = minor;
}

/* Intel feature capabilities */
void
device_info_intel_features(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, bits);
	device_info_bitfield(ret, loc, chk, output, ret->value.bits, intel_features_count,
		(output->mode == CLINFO_HUMAN ? intel_features_str : intel_features_raw_str), 
		"features_intel");
}



/* Device Partition, CLINFO_HUMAN header */
void
device_info_partition_header(struct device_info_ret *ret,
	const struct info_loc *UNUSED(loc), const struct device_info_checks *chk,
	const struct opt_out* UNUSED(output))
{
	cl_bool is_12 = dev_is_12(chk);
	cl_bool has_fission = dev_has_fission(chk);
	strbuf_append("dev partition", &ret->str, "(%s%s%s%s)",
		(is_12 ? core : empty_str),
		(is_12 && has_fission ? comma_str : empty_str),
		chk->has_fission,
		(!(is_12 || has_fission) ? na : empty_str));

	ret->err = CL_SUCCESS;
}

/* Device partition properties */
void
device_info_partition_types(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	size_t numval = 0, szval = 0, cursor = 0;
	cl_device_partition_property *val = NULL;
	const char * const *ptstr = (output->mode == CLINFO_HUMAN ?
		partition_type_str : partition_type_raw_str);

	GET_VAL_ARRAY(ret, loc);

	if (!ret->err) {
		const char *quote = output->json ? "\"" : "";
		set_common_separator(output);
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, "[ ", 2);

		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			if (cursor > 0) strbuf_append_str(loc->pname, &ret->str, sep);

			switch (val[cursor]) {
			case 0: str_idx = 0; break;
			case CL_DEVICE_PARTITION_EQUALLY: str_idx = 1; break;
			case CL_DEVICE_PARTITION_BY_COUNTS: str_idx = 2; break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN: str_idx = 3; break;
			case CL_DEVICE_PARTITION_BY_NAMES_INTEL: str_idx = 4; break;
			default:
				strbuf_append(loc->pname, &ret->str,
					"%sby <unknown> (%#" PRIxPTR ")%s",
					quote, val[cursor], quote);
				break;
			}
			if (str_idx >= 0) {
				/* string length, minus _EXT */
				size_t slen = strlen(ptstr[str_idx]);
				if (output->mode == CLINFO_RAW && str_idx > 0)
					slen -= 4;
				strbuf_append_str(loc->pname, &ret->str, quote);
				strbuf_append_str_len(loc->pname, &ret->str, ptstr[str_idx], slen);
				strbuf_append_str(loc->pname, &ret->str, quote);
			}
		}
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " ]", 2);
		// TODO ret->value.??? = val
	}
	free(val);
}

void
device_info_partition_types_ext(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	size_t numval = 0, szval = 0, cursor = 0;
	cl_device_partition_property_ext *val = NULL;
	const char * const *ptstr = (output->mode == CLINFO_HUMAN ?
		partition_type_str : partition_type_raw_str);

	GET_VAL_ARRAY(ret, loc);

	if (!ret->err) {
		const char *quote = output->json ? "\"" : "";
		set_common_separator(output);
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, "[ ", 1);

		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			if (cursor > 0) strbuf_append_str(loc->pname, &ret->str, sep);

			switch (val[cursor]) {
			case 0: str_idx = 0; break;
			case CL_DEVICE_PARTITION_EQUALLY_EXT: str_idx = 1; break;
			case CL_DEVICE_PARTITION_BY_COUNTS_EXT: str_idx = 2; break;
			case CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT: str_idx = 3; break;
			case CL_DEVICE_PARTITION_BY_NAMES_EXT: str_idx = 4; break;
			default:
				strbuf_append(loc->pname, &ret->str,
					"%sby <unknown> (%#" PRIx64 ")%s",
					quote, val[cursor], quote);
				break;
			}
			if (str_idx >= 0) {
				strbuf_append(loc->pname, &ret->str, "%s%s%s",
					quote, ptstr[str_idx], quote);
			}
		}
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " ]", 2);
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
	GET_VAL(ret, loc, affinity_domain);

	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.affinity_domain,
			affinity_domain_count, (output->mode == CLINFO_HUMAN ?
				affinity_domain_str : affinity_domain_raw_str),
			"domain");
	}
}

void
device_info_partition_affinities_ext(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	size_t numval = 0, szval = 0, cursor = 0;
	cl_device_partition_property_ext *val = NULL;
	const char * const *ptstr = (output->mode == CLINFO_HUMAN ?
		affinity_domain_ext_str : affinity_domain_raw_ext_str);

	GET_VAL_ARRAY(ret, loc);

	if (!ret->err) {
		const char *quote = output->json ? "\"" : "";
		set_common_separator(output);
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, "[ ", 2);

		for (cursor = 0; cursor < numval; ++cursor) {
			int str_idx = -1;

			/* add separator for values past the first */
			if (cursor > 0) strbuf_append_str(loc->pname, &ret->str, sep);

			switch (val[cursor]) {
			case CL_AFFINITY_DOMAIN_NUMA_EXT: str_idx = 0; break;
			case CL_AFFINITY_DOMAIN_L4_CACHE_EXT: str_idx = 1; break;
			case CL_AFFINITY_DOMAIN_L3_CACHE_EXT: str_idx = 2; break;
			case CL_AFFINITY_DOMAIN_L2_CACHE_EXT: str_idx = 3; break;
			case CL_AFFINITY_DOMAIN_L1_CACHE_EXT: str_idx = 4; break;
			case CL_AFFINITY_DOMAIN_NEXT_FISSIONABLE_EXT: str_idx = 5; break;
			default:
				strbuf_append(loc->pname, &ret->str,
					"%s<unknown> (%#" PRIx64 ")%s",
					quote, val[cursor], quote);
				break;
			}
			if (str_idx >= 0) {
				strbuf_append(loc->pname, &ret->str, "%s%s%s",
					quote, ptstr[str_idx], quote);
			}
		}
		if (output->json)
			strbuf_append_str_len(loc->pname, &ret->str, " ]", 2);
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
			const char *ext = (loc2.param.dev == CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF ?
				chk->has_half : (loc2.param.dev == CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE ?
				chk->has_double : NULL));
			strbuf_append(loc->pname, &ret->str, "%8u / %-8u", preferred, native);
			if (ext)
				strbuf_append(loc->pname, &ret->str, " (%s)", *ext ? ext : na);
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

	GET_VAL(ret, loc, fpconfig);
	/* Sanitize! */
	if (ret->err && !get_it) {
		ret->err = CL_SUCCESS;
		ret->value.fpconfig = 0;
	}

	if (output->json)
		strbuf_append(loc->pname, &ret->str,
			"{ \"raw\" : %" PRIu64 ", \"config\" : [ ",
			ret->value.fpconfig);

	if (!ret->err) {
		cl_uint i = 0;
		cl_uint count = 0;
		const char * const *fpstr = (output->mode == CLINFO_HUMAN ?
			fp_conf_str : fp_conf_raw_str);
		set_common_separator(output);
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
			strbuf_append(loc->pname, &ret->str, "(%s)", why);
		}
		if (get_it) {
			const char *quote = output->json ? "\"" : "";
			size_t num_flags = fp_conf_count;
			/* The last flag, CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT is only considered
			 * in the single-precision case. half and double don't consider it,
			 * so we skip it altogether */
			if (loc->param.dev != CL_DEVICE_SINGLE_FP_CONFIG)
				num_flags -= 1;

			for (i = 0; i < num_flags; ++i) {
				cl_device_fp_config cur = (cl_device_fp_config)(1) << i;
				cl_bool present = !!(ret->value.fpconfig & cur);
				if (output->mode == CLINFO_HUMAN) {
					strbuf_append(loc->pname, &ret->str, "\n%s" I2_STR "%s",
						line_pfx, fpstr[i], bool_str[present]);
				} else if (present) {
					strbuf_append(loc->pname, &ret->str, "%s%s%s%s",
						(count > 0 ? sep : ""), quote, fpstr[i], quote);
					++count;
				}
			}
		}
	}
	if (output->json)
		strbuf_append_str(loc->pname, &ret->str, " ] }");
}

/* Queue properties */
void
device_info_qprop(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out *output)
{
	GET_VAL(ret, loc, qprop);
	if (!ret->err) {
		const char * const *qpstr = (output->mode == CLINFO_HUMAN ?
			queue_prop_str : queue_prop_raw_str);

		if (output->mode != CLINFO_HUMAN) {
			device_info_bitfield(ret, loc, chk, output, ret->value.qprop,
				queue_prop_count, qpstr, "queue_prop");
		} else { /* output->mode == CLINFO_HUMAN */
			for (cl_uint i = 0; i < queue_prop_count; ++i) {
				cl_command_queue_properties cur = (cl_command_queue_properties)(1) << i;
				cl_bool present =!!(ret->value.qprop & cur);
				strbuf_append(loc->pname, &ret->str, "\n%s" I2_STR "%s",
					line_pfx, qpstr[i], bool_str[present]);
			}
			/* TODO FIXME extra bits? */
			if (loc->param.dev == CL_DEVICE_QUEUE_PROPERTIES && dev_has_intel_local_thread(chk))
				strbuf_append(loc->pname, &ret->str, "\n%s" I2_STR "%s",
					line_pfx, "Local thread execution (Intel)", bool_str[CL_TRUE]);
		}
	}
}

void
device_info_command_buffer_caps(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out *output)
{
	GET_VAL(ret, loc, cmdbufcap);
	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.cmdbufcap,
			command_buffer_count,
			(output->mode == CLINFO_RAW ? command_buffer_raw_str : command_buffer_str),
			"capabilities");
	}
}

void
device_info_mutable_dispatch_caps(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out *output)
{
	GET_VAL(ret, loc, cmdbufcap);
	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.cmdbufcap,
			mutable_dispatch_count,
			(output->mode == CLINFO_RAW ? mutable_dispatch_raw_str : mutable_dispatch_str),
			"capabilities");
	}
}

void
device_info_intel_usm_cap(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out *output)
{
	GET_VAL(ret, loc, svmcap);
	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.svmcap,
			intel_usm_cap_count,
			(output->mode == CLINFO_RAW ? intel_usm_cap_raw_str : intel_usm_cap_str),
			"capabilities");
	}
}

/* Device queue family properties */
void
strbuf_intel_queue_family(const char *what, struct _strbuf *str, const cl_queue_family_properties_intel *fams, size_t num_fams,
	const struct opt_out *output)
{
	realloc_strbuf(str, num_fams*(CL_QUEUE_FAMILY_MAX_NAME_SIZE_INTEL + 512), "queue families");
	if (output->json) {
		strbuf_append_str(what, str, "{");
	}
	for (size_t i = 0; i < num_fams; ++i) {
		const cl_queue_family_properties_intel  *fam = fams + i;
		set_separator(output->mode == CLINFO_HUMAN ? full_padding : output->json ? comma_str : spc_str);
		if (i > 0) strbuf_append_str(what, str, sep);
		if (output->json || output->mode == CLINFO_HUMAN) {
			strbuf_append(what, str,
				output->json ?
				"\"%s\" : { \"count\" : %u" :
				"%-65s(%u)",
				fam->name, fam->count);
		} else {
			strbuf_append(what, str, "%s:%u:", fam->name, fam->count);
		}

		if (output->json)
			strbuf_append(what, str, ", \"proprerties\" : ");
		else if (output->mode == CLINFO_HUMAN)
			strbuf_append(what, str, "\n%115s", "Queue properties" INDENT);
		strbuf_bitfield(what, str, fam->properties, "properties",
			output->mode == CLINFO_RAW ? queue_prop_raw_str : queue_prop_str,
			queue_prop_count, output);

		if (output->json)
			strbuf_append(what, str, ", \"capabilities\" : ");
		else if (output->mode == CLINFO_HUMAN)
			strbuf_append(what, str, "\n%115s", "Capabilities" INDENT);
		else strbuf_append(what, str, ":");
		strbuf_bitfield(what, str, fam->properties, "capabilities",
			output->mode == CLINFO_RAW ? intel_queue_cap_raw_str : intel_queue_cap_str,
			intel_queue_cap_count, output);
		if (output->json)
			strbuf_append(what, str, "}");
	}
	if (output->json)
		strbuf_append_str(what, str, " }");
}

void
device_info_qfamily_prop(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_queue_family_properties_intel *val = NULL;
	size_t szval = 0, numval = 0;
	GET_VAL_ARRAY(ret, loc);
	if (!ret->err) {
		strbuf_intel_queue_family(loc->pname, &ret->str, val, numval, output);
		// TODO: ret->value.??? = val;
	}
	free(val);
}


/* Execution capabilities */
void
device_info_execap(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, execap);
	if (!ret->err) {
		const char * const *qpstr = (output->mode == CLINFO_HUMAN ?
			execap_str : execap_raw_str);

		if (output->mode != CLINFO_HUMAN) {
			device_info_bitfield(ret, loc, chk, output, ret->value.execap,
				execap_count, qpstr, "type");
		} else { /* output->mode == CLINFO_HUMAN */
			for (cl_uint i = 0; i < execap_count; ++i) {
				cl_device_exec_capabilities cur = (cl_device_exec_capabilities)(1) << i;
				cl_bool present =!!(ret->value.execap & cur);
				strbuf_append(loc->pname, &ret->str, "\n%s" I2_STR "%s",
					line_pfx, qpstr[i], bool_str[present]);
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
	struct info_loc loc2 = *loc;
	DEV_FETCH(cl_uint, bits);
	RESET_LOC_PARAM(loc2, dev, CL_DEVICE_ENDIAN_LITTLE);
	if (!ret->err) {
		DEV_FETCH_LOC(cl_bool, val, &loc2);
		if (!ret->err) {
			strbuf_append(loc->pname, &ret->str, "%" PRIu32 ", %s", bits, endian_str[val]);
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
	GET_VAL(ret, loc, svmcap);

	if (!ret->err) {
		const char * const *scstr = (output->mode == CLINFO_HUMAN ?
			svm_cap_str : svm_cap_raw_str);

		if (output->mode != CLINFO_HUMAN) {
			device_info_bitfield(ret, loc, chk, output, ret->value.svmcap,
				svm_cap_count, scstr, "capabilities");
		} else { /* output->mode == CLINFO_HUMAN */
			if (checking_core) {
				/* show 'why' it's being shown */
				strbuf_append(loc->pname, &ret->str, "(%s%s%s)",
					(is_20 ? core : empty_str),
					(is_20 && has_amd_svm ? comma_str : empty_str),
					chk->has_amd_svm);
			}
			for (cl_uint i = 0; i < svm_cap_count; ++i) {
				cl_device_svm_capabilities cur = (cl_device_svm_capabilities)(1) << i;
				cl_bool present = !!(ret->value.svmcap & cur);
				strbuf_append(loc->pname, &ret->str, "\n%s" I2_STR "%s",
					line_pfx, scstr[i], bool_str[present]);
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
	GET_VAL(ret, loc, termcap);

	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.termcap,
			terminate_capability_count, (output->mode == CLINFO_HUMAN ?
				terminate_capability_str : terminate_capability_raw_str),
			"terminate");
	}
}

/* Device terminate capability */
void
device_info_terminate_arm(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, termcap);

	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.termcap,
			terminate_capability_arm_count, (output->mode == CLINFO_HUMAN ?
				terminate_capability_arm_str : terminate_capability_arm_raw_str),
			"terminate");
	}
}


/* ARM scheduling controls */
void
device_info_arm_scheduling_controls(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	GET_VAL(ret, loc, sched_controls);

	if (!ret->err) {
		device_info_bitfield(ret, loc, chk, output, ret->value.sched_controls,
			arm_scheduling_controls_count, (output->mode == CLINFO_HUMAN ?
				arm_scheduling_controls_str : arm_scheduling_controls_raw_str),
			"scheduling controls");
	}
}

void
device_info_p2p_dev_list(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks *chk,
	const struct opt_out* UNUSED(output))
{
	// Contrary to most array values in OpenCL, the AMD platform does not support querying
	// CL_DEVICE_P2P_DEVICES_AMD with a NULL ptr to get the number of results.
	// The user is assumed to have queried for the CL_DEVICE_NUM_P2P_DEVICES_AMD first,
	// and to have allocated the return array beforehand.
	cl_device_id *val = NULL;
	size_t numval = chk->p2p_num_devs, szval = numval*sizeof(*val);
	_GET_VAL_VALUES(ret, loc);
	if (!ret->err) {
		size_t cursor = 0;
		strbuf_append_str_len(loc->pname, &ret->str, "[ ", 2);
		set_common_separator(output);
		for (cursor = 0; cursor < numval; ++cursor) {
			strbuf_append(loc->pname, &ret->str, "%s%p",
				(cursor > 0 ? sep : ""), (void*)val[cursor]);
		}
		strbuf_append_str_len(loc->pname, &ret->str, " ]", 2);
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
				strbuf_append_str(loc->pname, &ret->str, groupsep);
				first = CL_TRUE;
			}
			if (current) {
				cl_bool found = CL_FALSE;
				const cl_interop_name *n = cl_interop_names;

				if (!first) {
					strbuf_append_str(loc->pname, &ret->str, " ");
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
					strbuf_append(loc->pname, &ret->str, "%s", n->value[i][human_raw]);
				} else {
					strbuf_append(loc->pname, &ret->str, "%#" PRIx32, val[cursor]);
				}
				first = CL_FALSE;
			}
		}
		// TODO: ret->value.??? = val;
	}
	// TODO JSONify
	ret->needs_escaping = CL_TRUE;
	free(val);
}

void device_info_uuid(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_uchar uuid[CL_UUID_SIZE_KHR];
	_GET_VAL(ret, loc, uuid);
	if (!ret->err) {
		strbuf_append(loc->pname, &ret->str,
			"%02x%02x%02x%02x-"
			"%02x%02x-"
			"%02x%02x-"
			"%02x%02x-"
			"%02x%02x%02x%02x%02x%02x",
			uuid[0],  uuid[1],  uuid[2],  uuid[3],  uuid[4],
			uuid[5],  uuid[6],
			uuid[7],  uuid[8],
			uuid[9],  uuid[10],
			uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
	}
	ret->needs_escaping = CL_TRUE;
}

void device_info_luid(struct device_info_ret *ret,
	const struct info_loc *loc, const struct device_info_checks* UNUSED(chk),
	const struct opt_out *output)
{
	cl_uchar uuid[CL_LUID_SIZE_KHR];
	_GET_VAL(ret, loc, uuid);
	if (!ret->err) {
		/* TODO not sure this is the correct representation for LUIDs? */
		strbuf_append(loc->pname, &ret->str, "%02x%02x-%02x%02x%02x%02x%02x%02x",
			uuid[0], uuid[1],
			uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7]);
	}
	ret->needs_escaping = CL_TRUE;
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

	/* This has to be made before calling NUMERIC_VERSION , since to know if it's supported
	 * we need to know about the extensions */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_EXTENSIONS, "Device Extensions", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_EXTENSIONS_WITH_VERSION, "Device Extensions with Version", ext_version), dev_has_ext_ver },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_UUID_KHR, "Device UUID", uuid), dev_has_device_uuid },
	{ CLINFO_BOTH, DINFO(CL_DRIVER_UUID_KHR, "Driver UUID", uuid), dev_has_device_uuid },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LUID_VALID_KHR, "Valid Device LUID", bool), dev_has_device_uuid },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LUID_KHR, "Device LUID", luid), dev_has_device_uuid },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NODE_MASK_KHR, "Device Node Mask", hex), dev_has_device_uuid },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUMERIC_VERSION, "Device Numeric Version", version), dev_has_ext_ver },
	{ CLINFO_BOTH, DINFO(CL_DRIVER_VERSION, "Driver Version", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_OPENCL_C_VERSION, "Device OpenCL C Version", str), dev_is_11 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_OPENCL_C_NUMERIC_VERSION_KHR, "Device OpenCL C Numeric Version", version), dev_has_extended_versioning },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_OPENCL_C_ALL_VERSIONS, "Device OpenCL C all versions", ext_version), dev_is_30 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_OPENCL_C_FEATURES, "Device OpenCL C features", ext_version), dev_is_30 },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_CXX_FOR_OPENCL_NUMERIC_VERSION_EXT, "Device C++ for OpenCL Numeric Version", version), dev_has_cxx_for_opencl },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_LATEST_CONFORMANCE_VERSION_PASSED, "Latest conformance test passed", str), dev_is_30 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TYPE, "Device Type", devtype), NULL },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_BOARD_NAME_AMD, "Device Board Name (AMD)", str), dev_has_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PCIE_ID_AMD, "Device PCI-e ID (AMD)", hex), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TOPOLOGY_AMD, "Device Topology (AMD)", devtopo_amd), dev_has_amd },

	/* Device Topology (NV) is multipart, so different for HUMAN and RAW */
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PCI_BUS_ID_NV, "Device Topology (NV)", devtopo_nv), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PCI_BUS_ID_NV, "Device PCI bus (NV)", int), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PCI_SLOT_ID_NV, "Device PCI slot (NV)", int), dev_has_nv },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PCI_DOMAIN_ID_NV, "Device PCI domain (NV)", int), dev_has_nv },

	/* Device Topology / PCI bus info (KHR) */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PCI_BUS_INFO_KHR, "Device PCI bus info (KHR)", devtopo_khr), dev_has_pci_bus_info },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_PROFILE, "Device Profile", str), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVAILABLE, "Device Available", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_COMPILER_AVAILABLE, "Compiler Available", bool), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LINKER_AVAILABLE, "Linker Available", bool), dev_is_12 },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_COMPUTE_UNITS, "Max compute units", int), NULL },
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_COMPUTE_UNITS_BITFIELD_ARM, "Available core IDs (ARM)", core_ids), dev_has_arm_core_id_v2 },
	{ CLINFO_RAW, DINFO(CL_DEVICE_COMPUTE_UNITS_BITFIELD_ARM, "Available core IDs (ARM)", long), dev_has_arm_core_id_v2 },
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_JOB_SLOTS_ARM, "Available job slots (ARM)", job_slots), dev_has_arm_job_slots },
	{ CLINFO_RAW, DINFO(CL_DEVICE_JOB_SLOTS_ARM, "Available job slots (ARM)", int), dev_has_arm_job_slots },
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

	/* Device IP version (Intel) */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_IP_VERSION_INTEL, "Device IP (Intel)", version), dev_is_gpu_intel },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ID_INTEL, "Device ID (Intel)", int), dev_is_gpu_intel },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_SLICES_INTEL, "Slices (Intel)", int), dev_is_gpu_intel },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_SUB_SLICES_PER_SLICE_INTEL, "Sub-slices per slice (Intel)", int), dev_is_gpu_intel },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_EUS_PER_SUB_SLICE_INTEL, "EUs per sub-slice (Intel)", int), dev_is_gpu_intel },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_THREADS_PER_EU_INTEL, "Threads per EU (Intel)", int), dev_is_gpu_intel },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_FEATURE_CAPABILITIES_INTEL, "Feature capabilities (Intel)", intel_features), dev_is_gpu_intel },

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

	{ CLINFO_BOTH, DINFO(CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, "Preferred work group size multiple (device)", sz), dev_is_30 },
	{ CLINFO_BOTH, DINFO(CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, "Preferred work group size multiple (kernel)", wg), dev_has_compiler_11 },
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

	/* External memory */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR, "External memory handle types", ext_mem), dev_has_external_memory },

	/* Semaphores */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SEMAPHORE_TYPES_KHR, "Semaphore types", semaphore_types), dev_has_semaphore },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SEMAPHORE_IMPORT_HANDLE_TYPES_KHR, "External semaphore import types", ext_semaphore_handles), dev_has_external_semaphore },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SEMAPHORE_EXPORT_HANDLE_TYPES_KHR, "External semaphore export types", ext_semaphore_handles), dev_has_external_semaphore },

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

	{ CLINFO_HUMAN, DINFO_SFX(CL_FALSE, "Unified Shared Memory (USM)", "(cl_intel_unified_shared_memory)", str), dev_has_intel_usm },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_HOST_MEM_CAPABILITIES_INTEL, "Host USM capabilities (Intel)", intel_usm_cap), dev_has_intel_usm },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_DEVICE_MEM_CAPABILITIES_INTEL, "Device USM capabilities (Intel)", intel_usm_cap), dev_has_intel_usm },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SINGLE_DEVICE_SHARED_MEM_CAPABILITIES_INTEL, "Single-Device USM caps (Intel)", intel_usm_cap), dev_has_intel_usm },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_CROSS_DEVICE_SHARED_MEM_CAPABILITIES_INTEL, "Cross-Device USM caps (Intel)", intel_usm_cap), dev_has_intel_usm },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SHARED_SYSTEM_MEM_CAPABILITIES_INTEL, "Shared System USM caps (Intel)", intel_usm_cap), dev_has_intel_usm },

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

	/* 3.0+ Atomic memory and fence capabilities */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ATOMIC_MEMORY_CAPABILITIES, "Atomic memory capabilities", atomic_caps), dev_is_30 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ATOMIC_FENCE_CAPABILITIES, "Atomic fence capabilities", atomic_caps), dev_is_30 },

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
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PIPE_SUPPORT, "Pipe support", bool), dev_is_30 },
	/* TODO FIXME: the above should be true if dev is [2.0, 3.0[, and the next properties should be nested */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_PIPE_ARGS, "Max number of pipe args", int), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS, "Max active pipe reservations", int), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PIPE_MAX_PACKET_SIZE, "Max pipe packet size", mem_int), dev_is_20 },

	/* Local memory */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LOCAL_MEM_TYPE, "Local memory type", lmemtype), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LOCAL_MEM_SIZE, "Local memory size", mem), dev_has_lmem },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LOCAL_MEM_SIZE_PER_COMPUTE_UNIT_AMD, "Local memory size per CU (AMD)", mem), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_LOCAL_MEM_BANKS_AMD, "Local memory banks (AMD)", int), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_REGISTERS_PER_BLOCK_NV, "Registers per block (NV)", int), dev_has_nv },

	/* Constant memory */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_CONSTANT_ARGS, "Max number of constant args", int), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE, "Max constant buffer size", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PREFERRED_CONSTANT_BUFFER_SIZE_AMD, "Preferred constant buffer size (AMD)", mem_sz), dev_has_amd_v4 },

	/* Generic address space support */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GENERIC_ADDRESS_SPACE_SUPPORT, "Generic address space support", bool), dev_is_30},

	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_PARAMETER_SIZE, "Max size of kernel argument", mem), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ATOMIC_COUNTERS_EXT, "Max number of atomic counters", sz), dev_has_atomic_counters },

	/* Queue properties */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_PROPERTIES, "Queue properties", qprop), dev_not_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_HOST_PROPERTIES, "Queue properties (on host)", qprop), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_DEVICE_ENQUEUE_CAPABILITIES, "Device enqueue capabilities", device_enqueue_caps), dev_is_30 },
	/* TODO FIXME: the above should be true if dev is [2.0, 3.0[, and the next properties should be nested */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES, "Queue properties (on device)", qprop), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE, INDENT "Preferred size", mem), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE, INDENT "Max size", mem), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ON_DEVICE_QUEUES, "Max queues on device", int), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_ON_DEVICE_EVENTS, "Max events on device", int), dev_is_20 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_QUEUE_FAMILY_PROPERTIES_INTEL, "Device queue families", qfamily_prop), dev_has_intel_queue_families },

	/* Command buffers */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_COMMAND_BUFFER_CAPABILITIES_KHR, "Command buffer capabilities", command_buffer_caps), dev_has_command_buffer },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_COMMAND_BUFFER_REQUIRED_QUEUE_PROPERTIES_KHR, INDENT "Required queue properties for command buffer", qprop), dev_has_command_buffer },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MUTABLE_DISPATCH_CAPABILITIES_KHR, "Mutable dispatch capabilities", mutable_dispatch_caps), dev_has_mutable_dispatch },

	/* Terminate context */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TERMINATE_CAPABILITY_KHR_1x, "Terminate capability (1.2 define)", terminate_capability), dev_has_terminate_context },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_TERMINATE_CAPABILITY_KHR, "Terminate capability (2.x and later)", terminate_capability), dev_has_terminate_context },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_CONTROLLED_TERMINATION_CAPABILITIES_ARM, "Controlled termination caps. (ARM)", terminate_arm), dev_has_terminate_arm },

	/* Interop */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PREFERRED_INTEROP_USER_SYNC, "Prefer user sync for interop", bool), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_SIMULTANEOUS_INTEROPS_INTEL, "Number of simultaneous interops (Intel)", int), dev_has_simultaneous_sharing },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SIMULTANEOUS_INTEROPS_INTEL, "Simultaneous interops", interop_list), dev_has_simultaneous_sharing },

	/* P2P buffer copy */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NUM_P2P_DEVICES_AMD, "Number of P2P devices (AMD)", int), dev_has_p2p },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_P2P_DEVICES_AMD, "P2P devices (AMD)", p2p_dev_list), dev_has_p2p_devs },

	/* Profiling resolution */
	{ CLINFO_BOTH, DINFO_SFX(CL_DEVICE_PROFILING_TIMER_RESOLUTION, "Profiling timer resolution", "ns", sz), NULL },
	{ CLINFO_HUMAN, DINFO(CL_DEVICE_PROFILING_TIMER_OFFSET_AMD, "Profiling timer offset since Epoch (AMD)", time_offset), dev_has_amd },
	{ CLINFO_RAW, DINFO(CL_DEVICE_PROFILING_TIMER_OFFSET_AMD, "Profiling timer offset since Epoch (AMD)", long), dev_has_amd },

	/* Kernel execution capabilities */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_EXECUTION_CAPABILITIES, "Execution capabilities", execap), NULL },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_NON_UNIFORM_WORK_GROUP_SUPPORT, INDENT "Non-uniform work-groups",  bool), dev_is_30 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_WORK_GROUP_COLLECTIVE_FUNCTIONS_SUPPORT, INDENT "Work-group collective functions",  bool), dev_is_30 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SUB_GROUP_INDEPENDENT_FORWARD_PROGRESS, INDENT "Sub-group independent forward progress", bool), dev_is_21 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_THREAD_TRACE_SUPPORTED_AMD, INDENT "Thread trace supported (AMD)", bool), dev_is_gpu_amd },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_KERNEL_EXEC_TIMEOUT_NV, INDENT "Kernel execution timeout (NV)", bool), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_GPU_OVERLAP_NV, INDENT "Concurrent copy and kernel execution (NV)", bool), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT_NV, INDENT INDENT "Number of async copy engines", int), dev_has_nv },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_AVAILABLE_ASYNC_QUEUES_AMD, INDENT "Number of async queues (AMD)", int), dev_has_amd_v4 },
	/* TODO FIXME undocumented, experimental */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_REAL_TIME_COMPUTE_QUEUES_AMD, INDENT "Max real-time compute queues (AMD)", int), dev_has_amd_v4 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_REAL_TIME_COMPUTE_UNITS_AMD, INDENT "Max real-time compute units (AMD)", int), dev_has_amd_v4 },

	{ CLINFO_BOTH, DINFO(CL_DEVICE_SCHEDULING_CONTROLS_CAPABILITIES_ARM, INDENT "Scheduling controls (ARM)", arm_scheduling_controls), dev_has_arm_scheduling_controls },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SUPPORTED_REGISTER_ALLOCATIONS_ARM, INDENT "Supported reg allocs (ARM)", intptr), dev_has_arm_register_alloc },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_MAX_WARP_COUNT_ARM, INDENT "Max warps/CU (ARM)", int), dev_has_arm_warp_count_support },

	/* TODO: this should tell if it's being done due to the device being 2.1 or due to it having the extension */
	{ CLINFO_BOTH, DINFO(CL_DEVICE_IL_VERSION, INDENT "IL version", str), dev_has_il },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_ILS_WITH_VERSION, INDENT "ILs with version", ext_version), dev_has_ext_ver },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_SPIR_VERSIONS, INDENT "SPIR versions", str), dev_has_spir },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_PRINTF_BUFFER_SIZE, "printf() buffer size", mem_sz), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_BUILT_IN_KERNELS, "Built-in kernels", str), dev_is_12 },
	{ CLINFO_BOTH, DINFO(CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION, "Built-in kernels with version", ext_version), dev_has_ext_ver },
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
	size_t ext_len = 0;
	char *versioned_extensions = NULL;

	/* pointers to the traits for CL_DEVICE_EXTENSIONS and CL_DEVICE_EXTENSIONS_WITH_VERSION */
	const struct device_info_traits *extensions_traits = NULL;
	const struct device_info_traits *versioned_extensions_traits = NULL;

	struct device_info_checks chk;
	struct device_info_ret ret;
	struct info_loc loc;

	cl_uint n = 0; /* number of device properties shown, for JSON */

	memset(&chk, 0, sizeof(chk));
	chk.pinfo_checks = plist->platform_checks + p;
	chk.dev_version = 10;

	INIT_RET(ret, "device");

	reset_loc(&loc, __func__);
	loc.plat = plist->platform[p];
	loc.dev = dev;

	for (loc.line = 0; loc.line < ARRAY_SIZE(dinfo_traits); ++loc.line) {

		const struct device_info_traits *traits = dinfo_traits + loc.line;
		cl_bool requested;

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

		reset_strbuf(&ret.str);
		reset_strbuf(&ret.err_str);
		ret.needs_escaping = CL_FALSE;

		/* Handle headers */
		if (traits->param == CL_FALSE) {
			ret.err = CL_SUCCESS;
			show_strbuf(&ret.str, loc.pname, 0, ret.err);
			continue;
		}

		traits->show_func(&ret, &loc, &chk, output);

		/* Do not print this property if the user requested one and this does not match */
		requested = !(output->prop && strstr(loc.sname, output->prop) == NULL);
		if (traits->param == CL_DEVICE_EXTENSIONS) {
			/* make a backup of the extensions string, regardless of
			 * errors and requested, because we need the information
			 * to fetch further information */
			const char *msg = RET_BUF(ret)->buf;
			ext_len = strlen(msg);
			extensions_traits = traits;
			/* pad with spaces: this will make it easier to check for extension presence
			 * without erroneously matching substrings by simply padding the extension name
			 * with spaces.
			 */
			ALLOC(extensions, ext_len+3, "extensions");
			memcpy(extensions + 1, msg, ext_len);
			extensions[0] = ' ';
			extensions[ext_len+1] = ' ';
			extensions[ext_len+2] = '\0';
		} else if (traits->param == CL_DEVICE_EXTENSIONS_WITH_VERSION) {
			if (ret.err && !checked && output->cond != COND_PROP_SHOW)
				continue;
			/* This will be displayed at the end, after we display the output of CL_DEVICE_EXTENSIONS */
			const char *msg = RET_BUF(ret)->buf;
			const size_t len = RET_BUF(ret)->sz;
			if (!requested)
				continue;
			versioned_extensions_traits = traits;
			ALLOC(versioned_extensions, len, "versioned extensions");
			memcpy(versioned_extensions, msg, len);
		} else if (requested) {
			if (ret.err) {
				/* if there was an error retrieving the property,
				 * skip if it wasn't expected to work and we
				 * weren't asked to show everything regardless of
				 * error */
				if (!checked && output->cond != COND_PROP_SHOW)
					continue;

			} else {
				/* on success, but empty result, show (n/a) */
				if (ret.str.buf[0] == '\0') {
					reset_strbuf(&ret.str);
					strbuf_append_str(loc.pname, &ret.str, not_specified(output));
				}
			}
			if (output->brief && output->json)
				json_stringify(RET_BUF(ret)->buf);
			else if (output->brief)
				printf("%s%s\n", line_pfx, RET_BUF(ret)->buf);
			else if (output->json)
				json_strbuf(RET_BUF(ret), loc.pname, n++, ret.err || ret.needs_escaping);
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
			if (!requested) {
				free(extensions);
				extensions = NULL;
			}
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
		case CL_DEVICE_NUM_P2P_DEVICES_AMD:
			chk.p2p_num_devs = ret.value.u32;
			break;
		case CL_DEVICE_SCHEDULING_CONTROLS_CAPABILITIES_ARM:
			chk.arm_register_alloc_support = !!(ret.value.sched_controls & CL_DEVICE_SCHEDULING_REGISTER_ALLOCATION_ARM);
			// TODO warp count support should check for extension version >= 0.4
			chk.arm_warp_count_support = !!(ret.value.sched_controls);
			break;
		default:
			/* do nothing */
			break;
		}
	}

	// and finally the extensions, if we retrieved them
	if (extensions) {
		// undo the padding
		extensions[ext_len + 1] = '\0';
		if (output->json) {
			printf("%s\"%s\" : ", (n > 0 ? comma_str : spc_str),
				(output->mode == CLINFO_HUMAN ?
				 extensions_traits->pname : extensions_traits->sname));
			json_stringify(extensions + 1);
			++n;
		} else
			printf("%s" I1_STR "%s\n", line_pfx, (output->mode == CLINFO_HUMAN ?
					extensions_traits->pname : extensions_traits->sname),
				extensions + 1);
	}
	if (versioned_extensions) {
		if (output->json) {
			printf("%s\"%s\" : ", (n > 0 ? comma_str : spc_str),
				(output->mode == CLINFO_HUMAN ?
				 versioned_extensions_traits->pname : versioned_extensions_traits->sname));
			fputs(versioned_extensions, stdout);
			++n;
		} else {
			printf("%s" I1_STR "%s\n", line_pfx, (output->mode == CLINFO_HUMAN ?
					versioned_extensions_traits->pname :
					versioned_extensions_traits->sname),
				versioned_extensions);
		}
	}
	free(extensions);
	free(versioned_extensions);
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
		strbuf_append(__func__, str, "%s%" PRIu32 ": ", brief_prefix, p);
	} else if (output->mode == CLINFO_RAW) {
		strbuf_append(__func__, str, "[%s/*]", pdata->sname);
	}
	sprintf(line_pfx, "%*s", prefix_width, str->buf);
	reset_strbuf(str);

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

	if (output->json)
		printf("%s\"%s\" : [", (these_are_offline ? comma_str : spc_str),
			(these_are_offline ? "offline" : "online"));
	else if (output->detailed)
		printf("%s" I0_STR "%" PRIu32 "\n",
			line_pfx,
			num_devs_header(output, these_are_offline),
			ndevs);

	for (d = 0; d < ndevs; ++d) {
		const cl_device_id dev = device[d];
		if (output->selected && output->device != d) continue;
		if (output->brief) {
			const cl_bool last_device = (d == ndevs - 1 &&
				output->mode != CLINFO_RAW &&
				(!output->offline ||
				 !pdata->has_amd_offline ||
				 these_are_offline));
			if (output->json) { /* nothing to do */ }
			else if (output->mode == CLINFO_RAW)
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
			strbuf_append(__func__, str, "[%s/%" PRId32 "]", pdata->sname, sd);
			sprintf(line_pfx, "%*s", -line_pfx_len, str->buf);
			reset_strbuf(str);
		}

		if (output->json)
			printf("%s%s",	(d > 0 ? comma_str : spc_str),
				(output->brief ? "" : "{"));

		printDeviceInfo(dev, plist, p, param_whitelist, output);

		if (output->json) {
			if (!output->brief) printf(" }");
		} else if (output->detailed && d < pdata[p].ndevs - 1)
			puts("");


		fflush(stdout);
		fflush(stderr);
	}
	if (output->json)
		fputs(" ]", stdout);
}


void showDevices(const struct platform_list *plist, const struct opt_out *output)
{
	const cl_uint num_platforms = plist->num_platforms + (output->null_platform ? 1 : 0);
	const cl_uint maxdevs = plist->max_devs;
	const struct platform_data *pdata = plist->pdata;

	cl_uint p;
	struct _strbuf str;
	init_strbuf(&str, __func__);

	if (output->mode == CLINFO_RAW) {
		if (output->brief)
			strbuf_append(__func__, &str, "%" PRIu32 ".%" PRIu32 ": ", num_platforms, maxdevs);
		else
			strbuf_append(__func__, &str, "[%*s/%" PRIu32 "] ",
				plist->max_sname_len, "", maxdevs);
	} else {
		if (output->brief)
			strbuf_append(__func__, &str, " +-- %sDevice #%" PRIu32 ": ",
				(output->offline ? "Offline " : ""), maxdevs);
		/* TODO we have no prefix in HUMAN detailed output mode,
		 * consider adding one
		 */
	}

	if (str.buf[0]) {
		line_pfx_len = (int)(strlen(str.buf) + 1);
		REALLOC(line_pfx, line_pfx_len, "line prefix");
		reset_strbuf(&str);
	}

	for (p = 0; p < num_platforms; ++p) {
		/* skip non-selected platforms altogether */
		if (output->selected && output->platform != p) continue;

		/* Open the JSON devices list for this platform */
		if (output->json)
			printf("%s{", p > 0 ? comma_str : spc_str);
		/* skip platform header if only printing specfic properties, */
		else if (!output->prop)
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

		/* Close JSON object for this platform */
		if (output->json)
			fputs(" }", stdout);
		else if (output->detailed)
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
		strbuf_append(__func__, &ret.err_str, no_plat(output));
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
		strbuf_append_str(__func__, &ret.err_str, no_plat(output));
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
			strbuf_append_str(__func__, &ret.err_str, (output->mode == CLINFO_HUMAN ?
				"<error: 0 devices, no matching platform!>" :
				"CL_DEVICE_NOT_FOUND | CL_INVALID_PLATFORM"));
			break;
		case 1:
			strbuf_append(__func__, &ret.err_str, "%s%s%s%s",
				no_dev_found(output),
				(output->mode == CLINFO_HUMAN ? " [" : " | "),
				(output->mode == CLINFO_HUMAN ? pdata[pidx].pname : pdata[pidx].sname),
				(output->mode == CLINFO_HUMAN ? "?]" : "?"));
			break;
		default: /* found > 1 */
			strbuf_append_str(__func__, &ret.err_str, (output->mode == CLINFO_HUMAN ?
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
				strbuf_append(__func__, &ret.str, "%s [%s]",
					(output->mode == CLINFO_HUMAN ? "Success" : "CL_SUCCESS"),
					pdata[i].sname);
				break;
			}
		}
		if (i == num_platforms) {
			ret.err = CL_INVALID_PLATFORM;
			strbuf_append(__func__, &ret.err_str, "<error: platform %p not found>", (void*)plat);
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
		strbuf_append(__func__, &ret->str, "%s [%s]",
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

		strbuf_append(__func__, &ret.str, "clCreateContextFromType(NULL, %s)", loc.sname);
		sprintf(def, I1_STR, ret.str.buf);
		reset_strbuf(&ret.str);

		loc.line = __LINE__+1;
		ctx = clCreateContextFromType(NULL, devtype[t], NULL, NULL, &ret.err);

		switch (ret.err) {
		case CL_INVALID_PLATFORM:
			strbuf_append_str(__func__, &ret.err_str, no_plat(output)); break;
		case CL_DEVICE_NOT_FOUND:
			strbuf_append_str(__func__, &ret.err_str, no_dev_found(output)); break;
		case CL_INVALID_DEVICE_TYPE: /* e.g. _CUSTOM device on 1.1 platform */
			strbuf_append_str(__func__, &ret.err_str, invalid_dev_type(output)); break;
		case CL_INVALID_VALUE: /* This is what apple returns for the case above */
			strbuf_append_str(__func__, &ret.err_str, invalid_dev_type(output)); break;
		case CL_DEVICE_NOT_AVAILABLE:
			strbuf_append_str(__func__, &ret.err_str, no_dev_avail(output)); break;
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
				strbuf_append_str(__func__, &ret.err_str, "<error: context created with no devices>");
			}

			/* get the platform from the first device */
			RESET_LOC_PARAM(loc, dev, CL_DEVICE_PLATFORM);
			loc.line = __LINE__+1;
			ret.err = clGetDeviceInfo(*devs, CL_DEVICE_PLATFORM, sizeof(plat), &plat, NULL);
			if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s")) break;
			loc.plat = plat;

			for (i = 0; i < num_platforms; ++i) {
				if (platform[i] == plat)
					break;
			}
			if (i == num_platforms) {
				ret.err = CL_INVALID_PLATFORM;
				strbuf_append(__func__, &ret.err_str, "<error: platform %p not found>", (void*)plat);
				break;
			} else {
				strbuf_append(__func__, &ret.str, "%s (%" PRIuS ")",
					(output->mode == CLINFO_HUMAN ? "Success" : "CL_SUCCESS"),
					ndevs);
				strbuf_append(__func__, &ret.str, "\n" I2_STR "%s",
					platname_prop, pdata[i].pname);
			}
			for (i = 0; i < ndevs; ++i) {
				size_t szname = 0;
				/* for each device, show the device name */
				/* TODO some other unique ID too, e.g. PCI address, if available? */

				strbuf_append(__func__, &ret.str, "\n" I2_STR, devname_prop);

				RESET_LOC_PARAM(loc, dev, CL_DEVICE_NAME);
				loc.dev = devs[i];
				loc.line = __LINE__+1;
				ret.err = clGetDeviceInfo(devs[i], CL_DEVICE_NAME, ret.str.sz - ret.str.end, ret.str.buf + ret.str.end, &szname);
				if (REPORT_ERROR_LOC(&ret, ret.err, &loc, "get %s")) break;
				ret.str.end += szname - 1;
			}
			if (i != ndevs)
				break; /* had an error earlier, bail */
		}

		if (ctx) {
			clReleaseContext(ctx);
			ctx = NULL;
		}
		printf("%s%s\n", def, RET_BUF(ret)->buf);
		reset_strbuf(&ret.str);
		reset_strbuf(&ret.err_str);
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
		strbuf_append(__func__, &ret.err_str, no_plat(output));
	} else if (pdata[p].ndevs == 0) {
		ret.err = CL_DEVICE_NOT_FOUND;
		strbuf_append(__func__, &ret.err_str, no_dev_found(output));
	} else {
		if (p < num_platforms) {
			checkNullCtx(&ret, plist, p, "default", output);
		} else {
			/* this shouldn't happen, but still ... */
			ret.err = CL_OUT_OF_HOST_MEMORY;
			strbuf_append_str(__func__, &ret.err_str, "<error: overflow in default platform scan>");
		}
	}
	printf(I1_STR "%s\n", "clCreateContext(NULL, ...) [default]", RET_BUF(ret)->buf);

	/* Look for a device from a non-default platform, if there are any */
	if (p == num_platforms || num_platforms > 1) {
		cl_uint p2 = 0;
		reset_strbuf(&ret.str);
		reset_strbuf(&ret.err_str);
		while (p2 < num_platforms && (p2 == p || pdata[p2].ndevs == 0)) {
			p2++;
		}
		if (p2 < num_platforms) {
			checkNullCtx(&ret, plist, p2, "non-default", output);
		} else {
			ret.err = CL_DEVICE_NOT_FOUND;
			strbuf_append(__func__, &ret.err_str, "<error: no devices in non-default plaforms>");
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
	{ 30, "clSetContextDestructorCallback" },
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

	/* clinfo may lag behind the OpenCL standard or loader version,
	 * and we don't want to give a warning if we can't tell if the loader
	 * correctly supports a version unknown to us
	 */
	cl_uint clinfo_highest_known_version = 0;

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
		clinfo_highest_known_version = icdl.detected_version = check.version;
		++i;
	} while (1);

	/* Step #2: query properties from extension, if available */
	if (clGetICDLoaderInfoOCLICD != NULL) {
		cl_uint n = 0; /* number of ICD loader properties shown, for JSON */
		struct info_loc loc;
		struct icdl_info_ret ret;
		reset_loc(&loc, __func__);
		INIT_RET(ret, "ICD loader");

		/* TODO think of a sensible header in CLINFO_RAW */
		if (output->mode != CLINFO_RAW)
			puts("\nICD loader properties");

		if (output->json) {
			fputs(", \"icd_loader\" : {", stdout);
		} else if (output->mode == CLINFO_RAW) {
			line_pfx_len = (int)(strlen(oclicdl_pfx) + 5);
			REALLOC(line_pfx, line_pfx_len, "line prefix OCL ICD");
			strbuf_append(loc.pname, &ret.str, "[%s/*]", oclicdl_pfx);
			sprintf(line_pfx, "%*s", -line_pfx_len, ret.str.buf);
			reset_strbuf(&ret.str);
		}

		for (loc.line = 0; loc.line < ARRAY_SIZE(linfo_traits); ++loc.line) {
			const struct icdl_info_traits *traits = linfo_traits + loc.line;
			cl_bool requested;
			loc.sname = traits->sname;
			loc.pname = (output->mode == CLINFO_HUMAN ?
				traits->pname : traits->sname);
			loc.param.icdl = traits->param;

			reset_strbuf(&ret.str);
			reset_strbuf(&ret.err_str);
			icdl_info_str(&ret, &loc);

			/* Do not print this property if the user requested one and this does not match */
			requested = !(output->prop && strstr(loc.sname, output->prop) == NULL);
			if (requested) {
				if (output->json)
					json_strbuf(RET_BUF(ret), loc.pname, n++, CL_TRUE);
				else
					show_strbuf(RET_BUF(ret), loc.pname, 1, ret.err);
			}

			if (!ret.err && traits->param == CL_ICDL_OCL_VERSION) {
				icdl.reported_version = getOpenCLVersion(ret.str.buf + 7);
			}
		}

		if (output->json)
			printf("%s\"_detected_version\" : \"%" PRIu32 ".%" PRIu32 "\" }",
				(n > 0 ? comma_str : spc_str),
				SPLIT_CL_VERSION(icdl.detected_version));
		UNINIT_RET(ret);
	}

	/* Step #3: show it */
	if (output->mode == CLINFO_HUMAN) {
		// for the loader vs platform max version check we use the version we detected
		// if the reported version is known to us, and the reported version if it's higher
		// than the standard versions we know about
		cl_uint max_version_check = icdl.reported_version > clinfo_highest_known_version ?
			icdl.reported_version : icdl.detected_version;

		if (icdl.reported_version &&
			icdl.reported_version <= clinfo_highest_known_version &&
			icdl.reported_version != icdl.detected_version) {
			printf(	"\tNOTE:\tyour OpenCL library declares to support OpenCL %" PRIu32 ".%" PRIu32 ",\n"
				"\t\tbut it seems to support up to OpenCL %" PRIu32 ".%" PRIu32 " %s.\n",
				SPLIT_CL_VERSION(icdl.reported_version),
				SPLIT_CL_VERSION(icdl.detected_version),
				icdl.detected_version < icdl.reported_version  ?
				"only" : "too");
		}

		if (max_version_check < max_plat_version) {
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
	puts("clinfo version 3.0.23.01.25");
}

void parse_device_spec(const char *str, struct opt_out *output)
{
	int p, d, n;
	if (!str) {
		fprintf(stderr, "please specify a device in the form P:D where P is the platform number and D the device number\n");
		exit(1);
	}
	n = sscanf(str, "%d:%d", &p, &d);
	if (n != 2 || p < 0 || d < 0) {
		fprintf(stderr, "invalid device specification '%s'\n", str);
		exit(1);
	}
	output->platform = p;
	output->device = d;
}

void free_output(struct opt_out *output)
{
	free((char*)output->prop);
	output->prop = NULL;
}

void parse_prop(const char *input, struct opt_out *output)
{
	/* We normalize the property name by upcasing it and replacing the minus sign (-)
	 * with the underscore (_). If any other character is found, we consider it an error
	 */

	size_t len = strlen(input);
	char *normalized;
	ALLOC(normalized, len+1, "normalized property name");
	for (size_t i = 0; i < len; ++i)
	{
		char c = input[i];
		if ( (c == '_') || ( c >= 'A' && c <= 'Z'))
			normalized[i] = c;
		else if (c >= 'a' && c <= 'z')
			normalized[i] = 'A' + (c - 'a');
		else if (c == '-')
			normalized[i] = '_';
		else {
			fprintf(stderr, "invalid property name substring '%s'\n", input);
			exit(1);
		}
	}

	if (output->prop) {
		fprintf(stderr, "WARNING: only one property name substring supported, discarding %s in favor of %s\n",
			output->prop, normalized);
		free_output(output);
	}
	output->prop = normalized;
}

void usage(void)
{
	version();
	puts("Display properties of all available OpenCL platforms and devices");
	puts("Usage: clinfo [options ...]\n");
	puts("Options:");
	puts("\t--all-props, -a\t\ttry all properties, only show valid ones");
	puts("\t--always-all-props, -At\tshow all properties, even if invalid");
	puts("\t--human\t\thuman-friendly output (default)");
	puts("\t--raw\t\traw output");
	puts("\t--offline\talso show offline devices");
	puts("\t--null-platform\talso show the NULL platform devices");
	puts("\t--list, -l\tonly list the platforms and devices by name");
	puts("\t--prop prop-name\tonly list properties matching the given name");
	puts("\t--device p:d,");
	puts("\t-d p:d\t\tonly show information about device number d from platform number p");
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

	output.platform = CL_UINT_MAX;
	output.device = CL_UINT_MAX;
	output.prop = NULL;
	output.mode = CLINFO_HUMAN;
	output.cond = COND_PROP_CHECK;
	output.brief = CL_FALSE;
	output.offline = CL_FALSE;
	output.null_platform = CL_FALSE;
	output.json = CL_FALSE;
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
		else if (!strcmp(argv[a], "--null-platform"))
			output.null_platform = CL_TRUE;
		else if (!strcmp(argv[a], "--json"))
			output.json = CL_TRUE;
		else if (!strcmp(argv[a], "-l") || !strcmp(argv[a], "--list"))
			output.brief = CL_TRUE;
		else if (!strcmp(argv[a], "-d") || !strcmp(argv[a], "--device")) {
			++a;
			parse_device_spec(argv[a], &output);
		} else if (!strncmp(argv[a], "-d", 2)) {
			parse_device_spec(argv[a] + 2, &output);
		} else if (!strcmp(argv[a], "--prop")) {
			++a;
			parse_prop(argv[a], &output);
		} else if (!strcmp(argv[a], "-?") || !strcmp(argv[a], "-h")) {
			usage();
			free_output(&output);
			return 0;
		} else if (!strcmp(argv[a], "--version") || !strcmp(argv[a], "-v")) {
			version();
			free_output(&output);
			return 0;
		} else {
			fprintf(stderr, "ignoring unknown command-line parameter %s\n", argv[a]);
		}
	}
	/* If a property was specified, we only print in RAW mode.
	 * Likewise, JSON format assumes RAW
	 */
	if (output.prop || output.json)
		output.mode = CLINFO_RAW;
	output.selected = (output.device != CL_UINT_MAX);
	output.detailed = !output.brief && !output.selected && !output.prop;

	err = clGetPlatformIDs(0, NULL, &plist.num_platforms);
	if (err != CL_PLATFORM_NOT_FOUND_KHR)
		CHECK_ERROR(err, "number of platforms");

	if (output.detailed && !output.json)
		printf(I0_STR "%" PRIu32 "\n",
			(output.mode == CLINFO_HUMAN ?
			 "Number of platforms" : "#PLATFORMS"),
			plist.num_platforms);

	cl_uint alloced_platforms = 0;
	if (plist.num_platforms) {
		alloced_platforms = alloc_plist(&plist, &output);
		err = clGetPlatformIDs(plist.num_platforms, plist.platform, NULL);
		CHECK_ERROR(err, "platform IDs");
	}

	ALLOC(line_pfx, 1, "line prefix");

	/* Open the JSON object and the JSON platforms list */
	if (output.json)
		fputs("{ \"platforms\" : [", stdout);

	for (p = 0; p < alloced_platforms; ++p) {
		// skip non-selected platforms altogether
		if (output.selected && output.platform != p) continue;

		/* Open a JSON object for this platform */
		if (output.json)
			printf("%s%s", (p > 0 ? comma_str : spc_str),
				(output.brief ? "" : "{"));

		gatherPlatformInfo(&plist, p, &output);

		/* Close JSON object for this platform */
		if (output.json && !output.brief)
			fputs(" }", stdout);
		else if (output.detailed)
			puts("");
	}

	/* Close JSON platforms list, open JSON devices list */
	if (alloced_platforms) {
		if (output.json)
			fputs(" ], \"devices\" : [", stdout);

		showDevices(&plist, &output);
	}

	/* Close JSON devices list */
	if (output.json)
		fputs(" ]", stdout);

	if (output.prop || (output.detailed && !output.selected)) {
		if (output.mode != CLINFO_RAW && plist.num_platforms)
			checkNullBehavior(&plist, &output);
		oclIcdProps(&plist, &output);
	}

	/* Close the JSON object */
	if (output.json)
		fputs(" }", stdout);


	free_plist(&plist);
	free(line_pfx);
	free_output(&output);
	return 0;
}

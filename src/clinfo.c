/* Collect all available information on all available devices
 * on all available OpenCL platforms present in the system
 */

/* cl_ulong is always a 64bit integer, so in a few places
   we want to use its shadow type uint64_t, and print the
   values using PRIu64
 */
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <time.h>
#include <string.h>

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

static const char unk[] = "Unknown";
static const char none[] = "None";
static const char na[] = "n/a"; // not available
static const char fpsupp[] = "Floating-point support";

static const char* bool_str[] = { "No", "Yes" };
static const char* endian_str[] = { "Big-Endian", "Little-Endian" };
static const char* device_type_str[] = { unk, "Default", "CPU", "GPU", "Accelerator", "Custom" };
static const char* local_mem_type_str[] = { none, "Local", "Global" };
static const char* cache_type_str[] = { none, "Read-Only", "Read/Write" };

static const char* sources[] = {
	"#define GWO(type) global write_only type* restrict\n",
	"#define GRO(type) global read_only type* restrict\n",
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

#define ARRAY_SIZE(ar) (sizeof(ar)/sizeof(*ar))

#define INDENT "  "
#define I0_STR "%-48s  "
#define I1_STR "  %-46s  "
#define I2_STR "    %-44s  "

#define STR_PRINT(name, str) \
	printf(I1_STR "%s\n", name, str)

#define SHOW_STRING(cmd, param, name, ...) do { \
	GET_STRING(cmd, param, __VA_ARGS__); \
	STR_PRINT(name, strbuf); \
} while (0)

/* Print platform info and prepare arrays for device info */
void
printPlatformInfo(cl_uint p)
{
	cl_platform_id pid = platform[p];

#define PARAM(param, str) \
	SHOW_STRING(clGetPlatformInfo, CL_PLATFORM_##param, "Platform " str, pid)

	PARAM(NAME, "Name");

	/* Store name for future reference */
	size_t len = strlen(strbuf);
	platform_name[p] = malloc(len + 1);
	/* memcpy instead of strncpy since we already have the len
	 * and memcpy is possibly more optimized */
	memcpy(platform_name[p], strbuf, len);
	platform_name[p][len] = '\0';

	PARAM(VENDOR, "Vendor");
	PARAM(VERSION, "Version");
	PARAM(PROFILE, "Profile");
	PARAM(EXTENSIONS, "Extensions");
#undef PARAM

	error = clGetDeviceIDs(pid, CL_DEVICE_TYPE_ALL, 0, NULL, num_devs + p);
	if (error != CL_DEVICE_NOT_FOUND)
		CHECK_ERROR("number of devices");
	num_devs_all += num_devs[p];
}

#define GET_PARAM(param, var) do { \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, sizeof(var), &var, 0); \
	CHECK_ERROR("get " #param); \
} while (0)

#define GET_PARAM_PTR(param, var, num) do { \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, num*sizeof(*var), var, 0); \
	CHECK_ERROR("get " #param); \
} while (0)

#define GET_PARAM_ARRAY(param, var, num) do { \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, 0, NULL, &num); \
	CHECK_ERROR("get number of " #param); \
	REALLOC(var, num/sizeof(*var), #param); \
	error = clGetDeviceInfo(dev, CL_DEVICE_##param, num, var, NULL); \
	CHECK_ERROR("get " #param); \
} while (0)

int
getWGsizes(cl_platform_id pid, cl_device_id dev)
{
	int ret = 0;
// make CHECK_ERROR return instead of exit
#define exit(val) do { ret = val; goto out; } while(0)

	cl_context_properties ctxpft[] = {
		CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
		0, 0 };
	cl_uint cursor = 0;
	cl_context ctx = 0;
	cl_program prg = 0;
	cl_kernel krn = 0;

	ctx = clCreateContext(ctxpft, 1, &dev, NULL, NULL, &error);
	CHECK_ERROR("create context");
	prg = clCreateProgramWithSource(ctx, ARRAY_SIZE(sources), sources, NULL, &error);
	CHECK_ERROR("create program");
	error = clBuildProgram(prg, 1, &dev, NULL, NULL, NULL);
#if 0
	if (error != CL_SUCCESS) {
		GET_STRING(clGetProgramBuildInfo, CL_PROGRAM_BUILD_LOG, prg, dev);
		fputs(strbuf, stderr);
		exit(1);
	}
#else
	CHECK_ERROR("build program");
#endif

	for (cursor = 0; cursor < NUM_KERNELS; ++cursor) {
		sprintf(strbuf, "sum%u", 1<<cursor);
		if (cursor == 0)
			strbuf[3] = 0; // scalar kernel is called 'sum'
		krn = clCreateKernel(prg, strbuf, &error);
		CHECK_ERROR("create kernel");
		error = clGetKernelWorkGroupInfo(krn, dev, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE,
			sizeof(*wgm), wgm + cursor, NULL);
		CHECK_ERROR("get kernel info");
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
#undef exit
}

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

	char* extensions;

	// these will hold the string from which we detected extension support
	char has_half[12] = {0};
	char has_double[12] = {0};
	char has_nv[29] = {0};
	char has_amd[30] = {0};
	char has_fission[22] = {0};
	char has_atomic_counters[26] = {0};
	char has_image2d_buffer[27] = {0};
	char has_intel_local_thread[30] = {0};
	char has_altera_dev_temp[29] = {0};

	// device supports OpenCL 1.2
	cl_bool is_12 = CL_FALSE;
	cl_bool is_gpu = CL_FALSE;

#define KB UINT64_C(1024)
#define MB (KB*KB)
#define GB (MB*KB)
#define TB (GB*KB)
#define MEM_SIZE(val) ( \
	val > TB ? val/TB : \
	val > GB ? val/GB : \
	val > MB ? val/MB : \
	val/KB )
#define MEM_PFX(val) ( \
	val > TB ? "TB" : \
	val > GB ? "GB" : \
	val > MB ? "MB" : \
	 "KB" )

#define STR_PARAM(param, str) \
	SHOW_STRING(clGetDeviceInfo, CL_DEVICE_##param, "Device " str, dev)
#define INT_PARAM(param, name, sfx) do { \
	GET_PARAM(param, uintval); \
	printf(I1_STR "%u" sfx "\n", name, uintval); \
} while (0)
#define LONG_PARAM(param, name, sfx) do { \
	GET_PARAM(param, ulongval); \
	printf(I1_STR "%u" sfx "\n", name, ulongval); \
} while (0)
#define SZ_PARAM(param, name, sfx) do { \
	GET_PARAM(param, szval); \
	printf(I1_STR "%zu" sfx "\n", name, szval); \
} while (0)
#define MEM_PARAM_STR(var, fmt, name) do { \
	doubleval = var; \
	if (var > KB) { \
		snprintf(strbuf, bufsz, " (%6.4lg%s)", \
			MEM_SIZE(doubleval), \
			MEM_PFX(doubleval)); \
		strbuf[bufsz-1] = '\0'; \
	} else strbuf[0] = '\0'; \
	printf(I1_STR fmt "%s\n", name, var, strbuf); \
} while (0)
#define MEM_PARAM(param, name) do { \
	GET_PARAM(param, ulongval); \
	MEM_PARAM_STR(ulongval, "%" PRIu64, name); \
} while (0)
#define BOOL_PARAM(param, name) do { \
	GET_PARAM(param, boolval); \
	STR_PRINT(name, bool_str[boolval]); \
} while (0)

	// device name
	STR_PARAM(NAME, "Name");
	STR_PARAM(VENDOR, "Vendor");
	STR_PARAM(VERSION, "Version");
	is_12 = !!(strstr(strbuf, "OpenCL 1.2"));
	SHOW_STRING(clGetDeviceInfo, CL_DRIVER_VERSION, "Driver Version", dev);
	STR_PARAM(OPENCL_C_VERSION, "OpenCL C Version");

	// we get the extensions information here, but only print it at the end
	GET_STRING(clGetDeviceInfo, CL_DEVICE_EXTENSIONS, dev);
	size_t len = strlen(strbuf);
	ALLOC(extensions, len+1, "extensions");
	memcpy(extensions, strbuf, len);
	extensions[len] = '\0';

#define _HAS_EXT(ext) (strstr(extensions, ext))
#define HAS_EXT(ext) _HAS_EXT(#ext)
#define CPY_EXT(what, ext) do { \
	strncpy(has_##what, has, sizeof(ext)); \
	has_##what[sizeof(ext)-1] = '\0'; \
} while (0)
#define CHECK_EXT(what, ext) do { \
	has = _HAS_EXT(#ext); \
	if (has) CPY_EXT(what, #ext); \
} while(0)

	{
		char *has;
		CHECK_EXT(half, cl_khr_fp16);
		CHECK_EXT(double, cl_khr_fp64);
		if (!*has_double)
			CHECK_EXT(double, cl_amd_fp64);
		CHECK_EXT(nv, cl_nv_device_attribute_query);
		CHECK_EXT(amd, cl_amd_device_attribute_query);
		CHECK_EXT(fission, cl_ext_device_fission);
		CHECK_EXT(atomic_counters, cl_ext_atomic_counters_64);
		if (!*has_atomic_counters)
			CHECK_EXT(atomic_counters, cl_ext_atomic_counters_32);
		CHECK_EXT(image2d_buffer, cl_khr_image2d_from_buffer);
		CHECK_EXT(intel_local_thread, cl_intel_exec_by_local_thread);
		CHECK_EXT(altera_dev_temp, cl_altera_device_temperature);
	}


	// device type
	GET_PARAM(TYPE, devtype);
	// FIXME this can be a combination of flags
	STR_PRINT("Device Type", device_type_str[ffs(devtype)]);
	is_gpu = !!(devtype & CL_DEVICE_TYPE_GPU);
	STR_PARAM(PROFILE, "Profile");
	if (*has_amd) {
		STR_PARAM(BOARD_NAME_AMD, "Board Name (AMD)");

		cl_device_topology_amd devtopo;
		GET_PARAM(TOPOLOGY_AMD, devtopo);

		switch (devtopo.raw.type) {
		case 0:
			snprintf(strbuf, bufsz, "(%s)", na);
			break;
		case CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD:
			snprintf(strbuf, bufsz, "PCI-E, %02x:%02x.%u",
				devtopo.pcie.bus, devtopo.pcie.device, devtopo.pcie.function);
			break;
		default:
			snprintf(strbuf, bufsz, "<unknown (%u): %u %u %u %u %u>", devtopo.raw.type,
				devtopo.raw.data[0], devtopo.raw.data[1], devtopo.raw.data[2],
				devtopo.raw.data[3], devtopo.raw.data[4]);
		}
		STR_PRINT("Device Topology (AMD)", strbuf);
	}

	// compute units and clock
	INT_PARAM(MAX_COMPUTE_UNITS, "Max compute units",);
	if (*has_amd && is_gpu) {
		// these are GPU-only
		INT_PARAM(SIMD_PER_COMPUTE_UNIT_AMD, "SIMD per compute units (AMD)",);
		INT_PARAM(SIMD_WIDTH_AMD, "SIMD width (AMD)",);
		INT_PARAM(SIMD_INSTRUCTION_WIDTH_AMD, "SIMD instruction width (AMD)",);
	}
	INT_PARAM(MAX_CLOCK_FREQUENCY, "Max clock frequency", "MHz");
	if (*has_nv) {
		GET_PARAM(COMPUTE_CAPABILITY_MAJOR_NV, uintval);
		GET_PARAM(COMPUTE_CAPABILITY_MINOR_NV, uintval2);
		printf(I1_STR "%u.%u\n",
			"NVIDIA Compute Capability",
			uintval, uintval2);
	}
	if (*has_altera_dev_temp)
		INT_PARAM(CORE_TEMPERATURE_ALTERA, "Core temperature (Altera)", " C");

	/* device fission, two different ways: core in 1.2, extension previously
	 * platforms that suppot both might expose different properties (e.g., partition
	 * by name is not considered in OpenCL 1.2, but an option with the extension
	 */
	szval = 0;
	if (is_12) {
		strncpy(strbuf + szval, "core, ", *has_fission ? 6 : 4);
		szval += (*has_fission ? 6 : 4);
	}
	if (*has_fission) {
		strcpy(strbuf + szval, has_fission);
		szval += strlen(has_fission);
	}
	strbuf[szval] = 0;

	printf(I1_STR "(%s)\n", "Device Partition",
		szval ? strbuf : na);
	if (is_12) {
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
		puts("");
		GET_PARAM(PARTITION_AFFINITY_DOMAIN, partdom);
		if (partdom) {
			printf(I2_STR, "Supported affinity domains");
			cl_bool comma = CL_FALSE;
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
	if (*has_fission) {
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
		printf(I2_STR "%zu\n", strbuf , szvals[cursor]);
	}
	SZ_PARAM(MAX_WORK_GROUP_SIZE, "Max work group size",);

	GET_PARAM(PLATFORM, pid);
	if (!getWGsizes(pid, dev))
		printf(I1_STR "%zu\n", "Preferred work group size multiple", wgm[0]);
	else
		printf(I1_STR "%s\n", "Preferred work group size multiple", "<detection failed>");

	if (*has_nv) {
		INT_PARAM(WARP_SIZE_NV, "Warp size (NVIDIA)",);
	}
	if (*has_amd && is_gpu) {
		INT_PARAM(WAVEFRONT_WIDTH_AMD, "Wavefront width (AMD)",);
	}

	// preferred/native vector widths
	printf(I1_STR, "Preferred / native vector sizes");
#define _PRINT_VEC(UCtype, type, optional, ext) do { \
	GET_PARAM(PREFERRED_VECTOR_WIDTH_##UCtype, uintval); \
	GET_PARAM(NATIVE_VECTOR_WIDTH_##UCtype, uintval2); \
	printf("\n" I2_STR "%8u / %-8u", #type, uintval, uintval2); \
	if (optional) \
		printf(" (%s)", *ext ? ext : na); \
} while (0)
#define PRINT_VEC(UCtype, type) _PRINT_VEC(UCtype, type, 0, "")
#define PRINT_VEC_OPT(UCtype, type, ext) _PRINT_VEC(UCtype, type, 1, ext)

	PRINT_VEC(CHAR, char);
	PRINT_VEC(SHORT, short);
	PRINT_VEC(INT, int);
	PRINT_VEC(LONG, long); // this is actually optional in EMBED profiles
	PRINT_VEC_OPT(HALF, half, has_half);
	PRINT_VEC(FLOAT, float);
	PRINT_VEC_OPT(DOUBLE, double, has_double);
	puts("");

	// FP configurations
#define SHOW_FP_FLAG(str, flag) \
	printf(I2_STR "%s\n", str, bool_str[!!(fpconfig & CL_FP_##flag)])
#define SHOW_FP_SUPPORT(type) do { \
	GET_PARAM(type##_FP_CONFIG, fpconfig); \
	SHOW_FP_FLAG("Denormals", DENORM); \
	SHOW_FP_FLAG("Infinity and NANs", INF_NAN); \
	SHOW_FP_FLAG("Round to nearest", ROUND_TO_NEAREST); \
	SHOW_FP_FLAG("Round to zero", ROUND_TO_ZERO); \
	SHOW_FP_FLAG("Round to infinity", ROUND_TO_INF); \
	SHOW_FP_FLAG("IEEE754-2008 fused multiply-add", FMA); \
	SHOW_FP_FLAG("Correctly-rounded divide and sqrt operations", CORRECTLY_ROUNDED_DIVIDE_SQRT); \
	SHOW_FP_FLAG("Support is emulated in software", SOFT_FLOAT); \
} while (0)

#define FPSUPP_STR(str, opt) \
	"  %-17s%-29s " opt "\n", #str "-precision", fpsupp
	printf(FPSUPP_STR(Half, " (%s)"),
		*has_half ? has_half : na);
	if (*has_half)
		SHOW_FP_SUPPORT(HALF);
	printf(FPSUPP_STR(Single, ""));
	SHOW_FP_SUPPORT(SINGLE);
	printf(FPSUPP_STR(Double, " (%s)"),
		*has_double ? has_double : na);
	if (*has_double)
		SHOW_FP_SUPPORT(DOUBLE);

	// arch bits and endianness
	GET_PARAM(ADDRESS_BITS, uintval);
	GET_PARAM(ENDIAN_LITTLE, boolval);
	printf(I1_STR "%u, %s\n", "Address bits", uintval, endian_str[boolval]);

	// memory size and alignment

	// global
	MEM_PARAM(GLOBAL_MEM_SIZE, "Global memory size");
	if (*has_amd && is_gpu) {
		// FIXME seek better documentation about this. what does it mean?
		GET_PARAM_ARRAY(GLOBAL_FREE_MEMORY_AMD, szvals, szval);
		szels = szval/sizeof(*szvals);
		for (cursor = 0; cursor < szels; ++cursor) {
			MEM_PARAM_STR(szvals[cursor], "%zu", "Free global memory (AMD)");
		}

		INT_PARAM(GLOBAL_MEM_CHANNELS_AMD, "Global memory channels (AMD)",);
		INT_PARAM(GLOBAL_MEM_CHANNEL_BANKS_AMD, "Global memory banks per channel (AMD)",);
		INT_PARAM(GLOBAL_MEM_CHANNEL_BANK_WIDTH_AMD, "Global memory bank width (AMD)", " bytes");
	}

	BOOL_PARAM(ERROR_CORRECTION_SUPPORT, "Error Correction support");
	MEM_PARAM(MAX_MEM_ALLOC_SIZE, "Max memory allocation");
	BOOL_PARAM(HOST_UNIFIED_MEMORY, "Unified memory for Host and Device");
	if (*has_nv) {
		BOOL_PARAM(INTEGRATED_MEMORY_NV, "NVIDIA integrated memory");
		BOOL_PARAM(GPU_OVERLAP_NV, "NVIDIA concurrent copy and kernel execution");
	}
	INT_PARAM(MIN_DATA_TYPE_ALIGN_SIZE, "Minimum alignment for any data type", " bytes");
	GET_PARAM(MEM_BASE_ADDR_ALIGN, uintval);
	printf(I1_STR "%u bits (%u bytes)\n",
		"Alignment of base address", uintval, uintval/8);

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
		if (is_12) {
			SZ_PARAM(IMAGE_MAX_BUFFER_SIZE, INDENT "Max 1D image size", " pixels");
			SZ_PARAM(IMAGE_MAX_ARRAY_SIZE, INDENT "Max 1D or 2D image array size", " images");
		}
		if (*has_image2d_buffer) {
			SZ_PARAM(IMAGE_BASE_ADDRESS_ALIGNMENT, INDENT "Base address alignment for 2D image buffers",);
			SZ_PARAM(IMAGE_PITCH_ALIGNMENT, INDENT "Pitch alignment for 2D image buffers",);
		}
		GET_PARAM_PTR(IMAGE2D_MAX_HEIGHT, szvals, 1);
		GET_PARAM_PTR(IMAGE2D_MAX_WIDTH, (szvals+1), 1);
		printf(I2_STR "%zux%zu pixels\n", "Max 2D image size",
			szvals[0], szvals[1]);
		GET_PARAM_PTR(IMAGE3D_MAX_HEIGHT, szvals, 1);
		GET_PARAM_PTR(IMAGE3D_MAX_WIDTH, (szvals+1), 1);
		GET_PARAM_PTR(IMAGE3D_MAX_DEPTH, (szvals+2), 1);
		printf(I2_STR "%zux%zux%zu pixels\n", "Max 3D image size",
			szvals[0], szvals[1], szvals[2]);
		INT_PARAM(MAX_READ_IMAGE_ARGS, INDENT "Max number of read image args",);
		INT_PARAM(MAX_WRITE_IMAGE_ARGS, INDENT "Max number of write image args",);
	}

	// local
	GET_PARAM(LOCAL_MEM_TYPE, lmemtype);
	STR_PRINT("Local memory type", local_mem_type_str[lmemtype]);
	if (lmemtype != CL_NONE)
		MEM_PARAM(LOCAL_MEM_SIZE, "Local memory size");
	if (*has_amd && is_gpu) {
		MEM_PARAM(LOCAL_MEM_SIZE_PER_COMPUTE_UNIT_AMD, "Local memory size per CU (AMD)");
		INT_PARAM(LOCAL_MEM_BANKS_AMD, "Local memory banks (AMD)",);
	}


	// constant
	MEM_PARAM(MAX_CONSTANT_BUFFER_SIZE, "Max constant buffer size");
	INT_PARAM(MAX_CONSTANT_ARGS, "Max number of constant args",);

	// nv: registers/CU
	if (*has_nv) {
		INT_PARAM(REGISTERS_PER_BLOCK_NV, "NVIDIA registers per CU",);
	}

	MEM_PARAM(MAX_PARAMETER_SIZE, "Max size of kernel argument");
	if (*has_atomic_counters)
		INT_PARAM(MAX_ATOMIC_COUNTERS_EXT, "Max number of atomic counters",);

	// queue and kernel capabilities
	printf(I1_STR "\n", "Queue properties");
	GET_PARAM(QUEUE_PROPERTIES, queueprop);
	STR_PRINT(INDENT "Out-of-order execution", bool_str[!!(queueprop & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE)]);
	STR_PRINT(INDENT "Profiling", bool_str[!!(queueprop & CL_QUEUE_PROFILING_ENABLE)]);
	if (*has_intel_local_thread) {
		printf(I1_STR "%s\n", INDENT "Intel local thread execution", bool_str[1]);
	}
	SZ_PARAM(PROFILING_TIMER_RESOLUTION, "Profiling timer resolution", "ns");
	if (*has_amd) {
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
	if (*has_nv) {
		BOOL_PARAM(KERNEL_EXEC_TIMEOUT_NV, INDENT "NVIDIA kernel execution timeout");
	}

	if (is_12) {
		BOOL_PARAM(PREFERRED_INTEROP_USER_SYNC, "Prefer user sync for interops");
		MEM_PARAM(PRINTF_BUFFER_SIZE, "printf() buffer size");
		STR_PARAM(BUILT_IN_KERNELS, "Built-in kernels");
	}

	// misc. availability
	BOOL_PARAM(AVAILABLE, "Device Available");
	BOOL_PARAM(COMPILER_AVAILABLE, "Compiler Available");
	if (is_12)
		BOOL_PARAM(LINKER_AVAILABLE, "Linker Available");

	// and finally the extensions
	printf(I1_STR "%s\n", "Device Extensions", extensions); \
}

int main(void)
{
	cl_uint p, d;

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
		}
		if (p < num_platforms - 1)
			puts("");
	}
}

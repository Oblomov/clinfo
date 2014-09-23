/* Include OpenCL headers and extensions, and define vendor-specific OpenCL
 * extensions, since they may not be defined in the official headers.
 */

#ifdef __APPLE__
#include <OpenCL/cl_ext.h>
#else
#include <CL/cl_ext.h>
#endif

#ifdef __APPLE__
/* Apple's header lacks a lot */
#define CL_PLATFORM_ICD_SUFFIX_KHR                  0x0920

#define CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV       0x4000
#define CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV       0x4001

#define CL_DEVICE_PARTITION_EQUALLY_EXT             0x4050
#define CL_DEVICE_PARTITION_BY_COUNTS_EXT           0x4051
#define CL_DEVICE_PARTITION_BY_NAMES_EXT            0x4052
#define CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT  0x4053
#define CL_DEVICE_PARTITION_TYPES_EXT               0x4055
#define CL_DEVICE_AFFINITY_DOMAINS_EXT              0x4056

#define CL_AFFINITY_DOMAIN_L1_CACHE_EXT             0x1
#define CL_AFFINITY_DOMAIN_L2_CACHE_EXT             0x2
#define CL_AFFINITY_DOMAIN_L3_CACHE_EXT             0x3
#define CL_AFFINITY_DOMAIN_L4_CACHE_EXT             0x4
#define CL_AFFINITY_DOMAIN_NUMA_EXT                 0x10
#define CL_AFFINITY_DOMAIN_NEXT_FISSIONABLE_EXT     0x100

#define CL_DEVICE_REGISTERS_PER_BLOCK_NV            0x4002
#define CL_DEVICE_WARP_SIZE_NV                      0x4003
#define CL_DEVICE_GPU_OVERLAP_NV                    0x4004
#define CL_DEVICE_KERNEL_EXEC_TIMEOUT_NV            0x4005
#define CL_DEVICE_INTEGRATED_MEMORY_NV              0x4006

typedef cl_ulong  cl_device_partition_property_ext;
#endif /* __APPLE__ */

/* cl_nv_device_attribute_query extension */
/* these clGetDeviceInfo param_names are not included in the official
 * headers */
#define CL_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT_NV   0x4007
#define CL_DEVICE_PCI_BUS_ID_NV                     0x4008
#define CL_DEVICE_PCI_SLOT_ID_NV                    0x4009

/* cl_amd_device_attribute_query */

#define CL_DEVICE_PROFILING_TIMER_OFFSET_AMD        0x4036
#define CL_DEVICE_TOPOLOGY_AMD                      0x4037
#define CL_DEVICE_BOARD_NAME_AMD                    0x4038
#define CL_DEVICE_GLOBAL_FREE_MEMORY_AMD            0x4039
#define CL_DEVICE_SIMD_PER_COMPUTE_UNIT_AMD         0x4040
#define CL_DEVICE_SIMD_WIDTH_AMD                    0x4041
#define CL_DEVICE_SIMD_INSTRUCTION_WIDTH_AMD        0x4042
#define CL_DEVICE_WAVEFRONT_WIDTH_AMD               0x4043
#define CL_DEVICE_GLOBAL_MEM_CHANNELS_AMD           0x4044
#define CL_DEVICE_GLOBAL_MEM_CHANNEL_BANKS_AMD      0x4045
#define CL_DEVICE_GLOBAL_MEM_CHANNEL_BANK_WIDTH_AMD 0x4046
#define CL_DEVICE_LOCAL_MEM_SIZE_PER_COMPUTE_UNIT_AMD   0x4047
#define CL_DEVICE_LOCAL_MEM_BANKS_AMD               0x4048

#ifndef CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD
#define CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD            1

typedef union
{
    struct { cl_uint type; cl_uint data[5]; } raw;
    struct { cl_uint type; cl_char unused[17]; cl_char bus; cl_char device; cl_char function; } pcie;
} cl_device_topology_amd;
#endif

/* cl_ext_atomic_counters_{32,64} */
#define CL_DEVICE_MAX_ATOMIC_COUNTERS_EXT           0x4032

/* cl_intel_device_partition_by_names */
#define CL_DEVICE_PARTITION_BY_NAMES_INTEL          0x4052

/* cl_qcom_ext_host_ptr */
#define CL_DEVICE_EXT_MEM_PADDING_IN_BYTES_QCOM     0x40A0
#define CL_DEVICE_PAGE_SIZE_QCOM                    0x40A1

/* cl_khr_spir */
#define CL_DEVICE_SPIR_VERSIONS                     0x40E0

/* cl_altera_device_temperature */
#define CL_DEVICE_CORE_TEMPERATURE_ALTERA           0x40F3

/* Include OpenCL headers and extensions, and define vendor-specific OpenCL
 * extensions, since they may not be defined in the official headers.
 */

#ifdef __APPLE__
#include <OpenCL/cl_ext.h>
#else
#include <CL/cl_ext.h>
#endif

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

/* cl_altera_device_temperature */
#define CL_DEVICE_CORE_TEMPERATURE_ALTERA           0x40F3

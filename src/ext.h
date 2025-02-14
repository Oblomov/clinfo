/* Include OpenCL header, and define OpenCL extensions, since what is and is not
 * available in the official headers is very system-dependent */

#ifndef EXT_H
#define EXT_H

/* Khronos now provides unified headers for all OpenCL versions, and
 * it should be included after defining a target OpenCL version
 * (otherwise, the maximum version will simply be used, but a message
 * will be printed).
 *
 * TODO: until 3.0 gets finalized, we only target 2.2 because the 3.0
 * defines etc are still changing, so users may have an older version
 * of the 3.0 headers lying around, which may prevent clinfo from being
 * compilable.
 */
#define CL_TARGET_OPENCL_VERSION 220

/* We will use the deprecated clGetExtensionFunctionAddress,
 * so let the headers know that we don't care about it being deprecated.
 * The standard CL_USE_DEPRECATED_OPENCL_1_1_APIS define apparently
 * doesn't work for macOS, so we'll just tell the compiler to not
 * warn about deprecated functions.
 * A more correct solution would be to suppress the warning only around the
 * clGetExtensionFunctionAddress call, but honestly I just cleaned up that
 * piece of code. And I'm actually wondering if it even makes sense to
 * build that part of the code on macOS: does anybody actually use
 * ocl-icd as OpenCL dispatcher on macOS?
 */

#ifdef __APPLE__
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <OpenCL/opencl.h>
#else
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS
#include <CL/cl.h>
#endif

/* Very old headers will be missing these defines */
#ifndef CL_VERSION_1_1
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_HALF           0x1034
#define CL_DEVICE_HOST_UNIFIED_MEMORY                   0x1035
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_CHAR              0x1036
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_SHORT             0x1037
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_INT               0x1038
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_LONG              0x1039
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_FLOAT             0x103A
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_DOUBLE            0x103B
#define CL_DEVICE_NATIVE_VECTOR_WIDTH_HALF              0x103C
#define CL_DEVICE_OPENCL_C_VERSION                      0x103D

#define CL_FP_SOFT_FLOAT                                (1 << 6)

#define CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE    0x11B3
#endif

#ifndef CL_VERSION_1_2
#define CL_DEVICE_TYPE_CUSTOM                           (1 << 4)

#define CL_DEVICE_LINKER_AVAILABLE                      0x103E
#define CL_DEVICE_BUILT_IN_KERNELS                      0x103F
#define CL_DEVICE_IMAGE_MAX_BUFFER_SIZE                 0x1040
#define CL_DEVICE_IMAGE_MAX_ARRAY_SIZE                  0x1041
#define CL_DEVICE_PARTITION_MAX_SUB_DEVICES             0x1043
#define CL_DEVICE_PARTITION_PROPERTIES                  0x1044
#define CL_DEVICE_PARTITION_AFFINITY_DOMAIN             0x1045
#define CL_DEVICE_PARTITION_TYPE                        0x1046
#define CL_DEVICE_PREFERRED_INTEROP_USER_SYNC           0x1048
#define CL_DEVICE_PRINTF_BUFFER_SIZE                    0x1049
#define CL_DEVICE_IMAGE_PITCH_ALIGNMENT                 0x104A
#define CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT          0x104B

#define CL_FP_CORRECTLY_ROUNDED_DIVIDE_SQRT             (1 << 7)

/* cl_device_partition_property */
#define CL_DEVICE_PARTITION_EQUALLY                     0x1086
#define CL_DEVICE_PARTITION_BY_COUNTS                   0x1087
#define CL_DEVICE_PARTITION_BY_COUNTS_LIST_END          0x0
#define CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN          0x1088

/* cl_device_affinity_domain */
#define CL_DEVICE_AFFINITY_DOMAIN_NUMA                  (1 << 0)
#define CL_DEVICE_AFFINITY_DOMAIN_L4_CACHE              (1 << 1)
#define CL_DEVICE_AFFINITY_DOMAIN_L3_CACHE              (1 << 2)
#define CL_DEVICE_AFFINITY_DOMAIN_L2_CACHE              (1 << 3)
#define CL_DEVICE_AFFINITY_DOMAIN_L1_CACHE              (1 << 4)
#define CL_DEVICE_AFFINITY_DOMAIN_NEXT_PARTITIONABLE    (1 << 5)

#endif

/* These two defines were introduced in the 1.2 headers
 * on 2012-11-30, so earlier versions don't have them
 * (e.g. Debian wheezy)
 */

#ifndef CL_DEVICE_IMAGE_PITCH_ALIGNMENT
#define CL_DEVICE_IMAGE_PITCH_ALIGNMENT                 0x104A
#define CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT          0x104B
#endif

/* 2.0 headers are not very common for the time being, so
 * let's copy the defines for the new CL_DEVICE_* properties
 * here.
 */
#ifndef CL_VERSION_2_0
#define CL_DEVICE_MAX_READ_WRITE_IMAGE_ARGS             0x104C
#define CL_DEVICE_MAX_GLOBAL_VARIABLE_SIZE              0x104D
#define CL_DEVICE_QUEUE_ON_HOST_PROPERTIES              0x102A
#define CL_DEVICE_QUEUE_ON_DEVICE_PROPERTIES            0x104E
#define CL_DEVICE_QUEUE_ON_DEVICE_PREFERRED_SIZE        0x104F
#define CL_DEVICE_QUEUE_ON_DEVICE_MAX_SIZE              0x1050
#define CL_DEVICE_MAX_ON_DEVICE_QUEUES                  0x1051
#define CL_DEVICE_MAX_ON_DEVICE_EVENTS                  0x1052
#define CL_DEVICE_SVM_CAPABILITIES                      0x1053
#define CL_DEVICE_GLOBAL_VARIABLE_PREFERRED_TOTAL_SIZE  0x1054
#define CL_DEVICE_MAX_PIPE_ARGS                         0x1055
#define CL_DEVICE_PIPE_MAX_ACTIVE_RESERVATIONS          0x1056
#define CL_DEVICE_PIPE_MAX_PACKET_SIZE                  0x1057
#define CL_DEVICE_PREFERRED_PLATFORM_ATOMIC_ALIGNMENT   0x1058
#define CL_DEVICE_PREFERRED_GLOBAL_ATOMIC_ALIGNMENT     0x1059
#define CL_DEVICE_PREFERRED_LOCAL_ATOMIC_ALIGNMENT      0x105A

#define CL_DEVICE_SVM_COARSE_GRAIN_BUFFER           (1 << 0)
#define CL_DEVICE_SVM_FINE_GRAIN_BUFFER             (1 << 1)
#define CL_DEVICE_SVM_FINE_GRAIN_SYSTEM             (1 << 2)
#define CL_DEVICE_SVM_ATOMICS                       (1 << 3)

typedef cl_bitfield         cl_device_svm_capabilities;
#endif

#ifndef CL_VERSION_2_1
#define CL_PLATFORM_HOST_TIMER_RESOLUTION		0x0905
#define CL_DEVICE_IL_VERSION				0x105B
#define CL_DEVICE_MAX_NUM_SUB_GROUPS			0x105C
#define CL_DEVICE_SUB_GROUP_INDEPENDENT_FORWARD_PROGRESS 0x105D
#endif

#ifndef CL_VERSION_3_0
#define CL_PLATFORM_NUMERIC_VERSION			0x0906
#define CL_PLATFORM_EXTENSIONS_WITH_VERSION		0x0907
#define CL_DEVICE_NUMERIC_VERSION			0x105E
#define CL_DEVICE_EXTENSIONS_WITH_VERSION		0x1060
#define CL_DEVICE_ILS_WITH_VERSION			0x1061
#define CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION		0x1062
#define CL_DEVICE_ATOMIC_MEMORY_CAPABILITIES		0x1063
#define CL_DEVICE_ATOMIC_FENCE_CAPABILITIES		0x1064
#define CL_DEVICE_NON_UNIFORM_WORK_GROUP_SUPPORT	0x1065
#define CL_DEVICE_OPENCL_C_ALL_VERSIONS			0x1066
#define CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_MULTIPLE	0x1067
#define CL_DEVICE_WORK_GROUP_COLLECTIVE_FUNCTIONS_SUPPORT 0x1068
#define CL_DEVICE_GENERIC_ADDRESS_SPACE_SUPPORT		0x1069
#define CL_DEVICE_OPENCL_C_FEATURES			0x106F
#define CL_DEVICE_DEVICE_ENQUEUE_CAPABILITIES		0x1070
#define CL_DEVICE_PIPE_SUPPORT				0x1071
#define CL_DEVICE_LATEST_CONFORMANCE_VERSION_PASSED	0x1072


typedef cl_bitfield	cl_device_atomic_capabilities;
typedef cl_bitfield	cl_device_device_enqueue_capabilities;
typedef cl_uint		cl_version;

#define CL_NAME_VERSION_MAX_NAME_SIZE 64

typedef struct _cl_name_version {
    cl_version              version;
    char                    name[CL_NAME_VERSION_MAX_NAME_SIZE];
} cl_name_version;

/* cl_device_atomic_capabilities */
#define CL_DEVICE_ATOMIC_ORDER_RELAXED		(1 << 0)
#define CL_DEVICE_ATOMIC_ORDER_ACQ_REL		(1 << 1)
#define CL_DEVICE_ATOMIC_ORDER_SEQ_CST		(1 << 2)
#define CL_DEVICE_ATOMIC_SCOPE_WORK_ITEM	(1 << 3)
#define CL_DEVICE_ATOMIC_SCOPE_WORK_GROUP	(1 << 4)
#define CL_DEVICE_ATOMIC_SCOPE_DEVICE		(1 << 5)
#define CL_DEVICE_ATOMIC_SCOPE_ALL_DEVICES	(1 << 6)

/* cl_device_device_enqueue_capabilities */
#define CL_DEVICE_QUEUE_SUPPORTED               (1 << 0)
#define CL_DEVICE_QUEUE_REPLACEABLE_DEFAULT     (1 << 1)

#endif

/*
 * Extensions
 */

/* cl_khr_extended_versioning */
// the _KHR fields are the same as the unsuffixed from OpenCL 3
#define CL_PLATFORM_NUMERIC_VERSION_KHR			CL_PLATFORM_NUMERIC_VERSION
#define CL_PLATFORM_EXTENSIONS_WITH_VERSION_KHR		CL_PLATFORM_EXTENSIONS_WITH_VERSION
#define CL_DEVICE_NUMERIC_VERSION_KHR			CL_DEVICE_NUMERIC_VERSION
#define CL_DEVICE_OPENCL_C_NUMERIC_VERSION_KHR		0x105F
#define CL_DEVICE_EXTENSIONS_WITH_VERSION_KHR		CL_DEVICE_EXTENSIONS_WITH_VERSION
#define CL_DEVICE_ILS_WITH_VERSION_KHR			CL_DEVICE_ILS_WITH_VERSION
#define CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION_KHR	CL_DEVICE_BUILT_IN_KERNELS_WITH_VERSION

/* cl_khr_image2d_from_buffer */
// the _KHR fields are the same as the unsuffixed from OpenCL 2
#define CL_DEVICE_IMAGE_PITCH_ALIGNMENT_KHR		CL_DEVICE_IMAGE_PITCH_ALIGNMENT
#define CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT_KHR	CL_DEVICE_IMAGE_BASE_ADDRESS_ALIGNMENT


/* cl_khr_icd */
#define CL_PLATFORM_ICD_SUFFIX_KHR			0x0920
#define CL_PLATFORM_NOT_FOUND_KHR			-1001

/* cl_khr_kernel_clock */
#define CL_DEVICE_KERNEL_CLOCK_CAPABILITIES_KHR             0x1076
typedef cl_bitfield         cl_device_kernel_clock_capabilities_khr;
#define CL_DEVICE_KERNEL_CLOCK_SCOPE_DEVICE_KHR             (1 << 0)
#define CL_DEVICE_KERNEL_CLOCK_SCOPE_WORK_GROUP_KHR         (1 << 1)
#define CL_DEVICE_KERNEL_CLOCK_SCOPE_SUB_GROUP_KHR          (1 << 2)

/* cl_amd_object_metadata */
#define CL_PLATFORM_MAX_KEYS_AMD			0x403C

/* cl_khr_device_uuid extension */

#define CL_UUID_SIZE_KHR 16
#define CL_LUID_SIZE_KHR 8

#define CL_DEVICE_UUID_KHR          0x106A
#define CL_DRIVER_UUID_KHR          0x106B
#define CL_DEVICE_LUID_VALID_KHR    0x106C
#define CL_DEVICE_LUID_KHR          0x106D
#define CL_DEVICE_NODE_MASK_KHR     0x106E

/* cl_khr_fp64 */
#define CL_DEVICE_DOUBLE_FP_CONFIG			0x1032

/* cl_khr_fp16 */
#define CL_DEVICE_HALF_FP_CONFIG			0x1033

/* cl_khr_il_program */
#define CL_DEVICE_IL_VERSION_KHR			0x105B

/* cl_khr_command_buffer */
#define CL_DEVICE_COMMAND_BUFFER_CAPABILITIES_KHR	0x12A9
#define CL_DEVICE_COMMAND_BUFFER_REQUIRED_QUEUE_PROPERTIES_KHR	0x12AA
typedef cl_bitfield         cl_device_command_buffer_capabilities_khr;

/* cl_khr_command_buffer_multi_device */
#define CL_PLATFORM_COMMAND_BUFFER_CAPABILITIES_KHR         0x0908
typedef cl_bitfield         cl_platform_command_buffer_capabilities_khr;
#define CL_COMMAND_BUFFER_PLATFORM_UNIVERSAL_SYNC_KHR       (1 << 0)
#define CL_COMMAND_BUFFER_PLATFORM_REMAP_QUEUES_KHR         (1 << 1)
#define CL_COMMAND_BUFFER_PLATFORM_AUTOMATIC_REMAP_KHR      (1 << 2)

#define CL_DEVICE_COMMAND_BUFFER_NUM_SYNC_DEVICES_KHR       0x12AB
#define CL_DEVICE_COMMAND_BUFFER_SYNC_DEVICES_KHR           0x12AC

/* cl_khr_command_buffer_mutable_dispatch */
#define CL_DEVICE_MUTABLE_DISPATCH_CAPABILITIES_KHR	0x12B0
typedef cl_bitfield         cl_mutable_dispatch_fields_khr;

/* cl_khr_terminate_context */
#define CL_DEVICE_TERMINATE_CAPABILITY_KHR_1x		0x200F
#define CL_DEVICE_TERMINATE_CAPABILITY_KHR		0x2031

/* TODO: I cannot find official definitions for these,
 * so I'm currently extrapolating them from the specification
 */
typedef cl_bitfield cl_device_terminate_capability_khr;
#define CL_DEVICE_TERMINATE_CAPABILITY_CONTEXT_KHR	(1<<0)

/* cl_khr_subgroup_named_barrier */
#define CL_DEVICE_MAX_NAMED_BARRIER_COUNT_KHR		0x2035

/* cl_khr_semaphore */
#define CL_PLATFORM_SEMAPHORE_TYPES_KHR			0x2036
#define CL_DEVICE_SEMAPHORE_TYPES_KHR			0x204C
typedef cl_uint cl_semaphore_type_khr;

/* cl_khr_external_semaphore */
#define CL_PLATFORM_SEMAPHORE_IMPORT_HANDLE_TYPES_KHR	0x2037
#define CL_PLATFORM_SEMAPHORE_EXPORT_HANDLE_TYPES_KHR	0x2038
#define CL_DEVICE_SEMAPHORE_IMPORT_HANDLE_TYPES_KHR	0x204D
#define CL_DEVICE_SEMAPHORE_EXPORT_HANDLE_TYPES_KHR	0x204E
typedef cl_uint cl_external_semaphore_handle_type_khr;

/* cl_khr_external_memory */
#define CL_PLATFORM_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR	0x2044
#define CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR	0x204F
// introduced in 1.0.1
#define CL_DEVICE_EXTERNAL_MEMORY_IMPORT_ASSUME_LINEAR_IMAGES_HANDLE_TYPES_KHR 0x2052
typedef cl_uint cl_external_memory_handle_type_khr;


/* cl_khr_pci_bus_info */
typedef struct _cl_device_pci_bus_info_khr {
	cl_uint	pci_domain;
	cl_uint	pci_bus;
	cl_uint	pci_device;
	cl_uint	pci_function;
} cl_device_pci_bus_info_khr;

#define CL_DEVICE_PCI_BUS_INFO_KHR			0x410F

/* cl_nv_device_attribute_query */
#define CL_DEVICE_COMPUTE_CAPABILITY_MAJOR_NV		0x4000
#define CL_DEVICE_COMPUTE_CAPABILITY_MINOR_NV		0x4001
#define CL_DEVICE_REGISTERS_PER_BLOCK_NV		0x4002
#define CL_DEVICE_WARP_SIZE_NV				0x4003
#define CL_DEVICE_GPU_OVERLAP_NV			0x4004
#define CL_DEVICE_KERNEL_EXEC_TIMEOUT_NV		0x4005
#define CL_DEVICE_INTEGRATED_MEMORY_NV			0x4006
#define CL_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT_NV	0x4007
#define CL_DEVICE_PCI_BUS_ID_NV				0x4008
#define CL_DEVICE_PCI_SLOT_ID_NV			0x4009
#define CL_DEVICE_PCI_DOMAIN_ID_NV			0x400A

/* cl_ext_atomic_counters_{32,64} */
#define CL_DEVICE_MAX_ATOMIC_COUNTERS_EXT		0x4032

/* cl_ext_float_atomics */
typedef cl_bitfield         cl_device_fp_atomic_capabilities_ext;
/* cl_device_fp_atomic_capabilities_ext */
#define CL_DEVICE_GLOBAL_FP_ATOMIC_LOAD_STORE_EXT           (1 << 0)
#define CL_DEVICE_GLOBAL_FP_ATOMIC_ADD_EXT                  (1 << 1)
#define CL_DEVICE_GLOBAL_FP_ATOMIC_MIN_MAX_EXT              (1 << 2)
#define CL_DEVICE_LOCAL_FP_ATOMIC_LOAD_STORE_EXT            (1 << 16)
#define CL_DEVICE_LOCAL_FP_ATOMIC_ADD_EXT                   (1 << 17)
#define CL_DEVICE_LOCAL_FP_ATOMIC_MIN_MAX_EXT               (1 << 18)

/* cl_device_info */
#define CL_DEVICE_SINGLE_FP_ATOMIC_CAPABILITIES_EXT         0x4231
#define CL_DEVICE_DOUBLE_FP_ATOMIC_CAPABILITIES_EXT         0x4232
#define CL_DEVICE_HALF_FP_ATOMIC_CAPABILITIES_EXT           0x4233

/* cl_amd_device_attribute_query */
#define CL_DEVICE_PROFILING_TIMER_OFFSET_AMD		0x4036
#define CL_DEVICE_TOPOLOGY_AMD				0x4037
#define CL_DEVICE_BOARD_NAME_AMD			0x4038
#define CL_DEVICE_GLOBAL_FREE_MEMORY_AMD		0x4039
#define CL_DEVICE_SIMD_PER_COMPUTE_UNIT_AMD		0x4040
#define CL_DEVICE_SIMD_WIDTH_AMD			0x4041
#define CL_DEVICE_SIMD_INSTRUCTION_WIDTH_AMD		0x4042
#define CL_DEVICE_WAVEFRONT_WIDTH_AMD			0x4043
#define CL_DEVICE_GLOBAL_MEM_CHANNELS_AMD		0x4044
#define CL_DEVICE_GLOBAL_MEM_CHANNEL_BANKS_AMD		0x4045
#define CL_DEVICE_GLOBAL_MEM_CHANNEL_BANK_WIDTH_AMD	0x4046
#define CL_DEVICE_LOCAL_MEM_SIZE_PER_COMPUTE_UNIT_AMD	0x4047
#define CL_DEVICE_LOCAL_MEM_BANKS_AMD			0x4048
#define CL_DEVICE_THREAD_TRACE_SUPPORTED_AMD		0x4049
#define CL_DEVICE_GFXIP_MAJOR_AMD			0x404A
#define CL_DEVICE_GFXIP_MINOR_AMD			0x404B
#define CL_DEVICE_AVAILABLE_ASYNC_QUEUES_AMD		0x404C
/* These two are undocumented */
#define CL_DEVICE_MAX_REAL_TIME_COMPUTE_QUEUES_AMD	0x404D
#define CL_DEVICE_MAX_REAL_TIME_COMPUTE_UNITS_AMD	0x404E
/* These were added in v4 of the extension, but have values lower than
 * than the older ones, and spanning around the cl_ext_atomic_counters_*
 * define
 */
#define CL_DEVICE_PREFERRED_WORK_GROUP_SIZE_AMD         0x4030
#define CL_DEVICE_MAX_WORK_GROUP_SIZE_AMD               0x4031
#define CL_DEVICE_PREFERRED_CONSTANT_BUFFER_SIZE_AMD    0x4033
#define CL_DEVICE_PCIE_ID_AMD                           0x4034

#ifndef CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD
#define CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD		1

typedef union
{
	struct { cl_uint type; cl_uint data[5]; } raw;
	struct { cl_uint type; cl_char unused[17]; cl_char bus; cl_char device; cl_char function; } pcie;
} cl_device_topology_amd;
#endif

/* cl_amd_offline_devices */
#define CL_CONTEXT_OFFLINE_DEVICES_AMD			0x403F

/* cl_amd_copy_buffer_p2p */
#define CL_DEVICE_NUM_P2P_DEVICES_AMD			0x4088
#define CL_DEVICE_P2P_DEVICES_AMD			0x4089

/* cl_ext_cxx_for_opencl */
#define CL_DEVICE_CXX_FOR_OPENCL_NUMERIC_VERSION_EXT	0x4230

/* cl_ext_device_fission */
#define cl_ext_device_fission				1

typedef cl_ulong  cl_device_partition_property_ext;

#define CL_DEVICE_PARTITION_EQUALLY_EXT			0x4050
#define CL_DEVICE_PARTITION_BY_COUNTS_EXT		0x4051
#define CL_DEVICE_PARTITION_BY_NAMES_EXT		0x4052
#define CL_DEVICE_PARTITION_BY_NAMES_INTEL		0x4052 /* cl_intel_device_partition_by_names */
#define CL_DEVICE_PARTITION_BY_AFFINITY_DOMAIN_EXT	0x4053

#define CL_DEVICE_PARENT_DEVICE_EXT			0x4054
#define CL_DEVICE_PARTITION_TYPES_EXT			0x4055
#define CL_DEVICE_AFFINITY_DOMAINS_EXT			0x4056
#define CL_DEVICE_REFERENCE_COUNT_EXT			0x4057
#define CL_DEVICE_PARTITION_STYLE_EXT			0x4058

#define CL_AFFINITY_DOMAIN_L1_CACHE_EXT			0x1
#define CL_AFFINITY_DOMAIN_L2_CACHE_EXT			0x2
#define CL_AFFINITY_DOMAIN_L3_CACHE_EXT			0x3
#define CL_AFFINITY_DOMAIN_L4_CACHE_EXT			0x4
#define CL_AFFINITY_DOMAIN_NUMA_EXT			0x10
#define CL_AFFINITY_DOMAIN_NEXT_FISSIONABLE_EXT		0x100

/* cl_intel_advanced_motion_estimation */
#define CL_DEVICE_ME_VERSION_INTEL			0x407E

/* cl_intel_device_side_avc_motion_estimation */
#define CL_DEVICE_AVC_ME_VERSION_INTEL				0x410B
#define CL_DEVICE_AVC_ME_SUPPORTS_TEXTURE_SAMPLER_USE_INTEL	0x410C
#define CL_DEVICE_AVC_ME_SUPPORTS_PREEMPTION_INTEL		0x410D

/* cl_intel_planar_yuv */
#define CL_DEVICE_PLANAR_YUV_MAX_WIDTH_INTEL		0x417E
#define CL_DEVICE_PLANAR_YUV_MAX_HEIGHT_INTEL		0x417F

/* cl_intel_unified_shared_memory */
#define CL_DEVICE_HOST_MEM_CAPABILITIES_INTEL			0x4190
#define CL_DEVICE_DEVICE_MEM_CAPABILITIES_INTEL			0x4191
#define CL_DEVICE_SINGLE_DEVICE_SHARED_MEM_CAPABILITIES_INTEL	0x4192
#define CL_DEVICE_CROSS_DEVICE_SHARED_MEM_CAPABILITIES_INTEL	0x4193
#define CL_DEVICE_SHARED_SYSTEM_MEM_CAPABILITIES_INTEL		0x4194

/* cl_qcom_ext_host_ptr */
#define CL_DEVICE_EXT_MEM_PADDING_IN_BYTES_QCOM		0x40A0
#define CL_DEVICE_PAGE_SIZE_QCOM			0x40A1

/* cl_arm_shared_virtual_memory */
#define CL_DEVICE_SVM_CAPABILITIES_ARM			0x40B6
#define CL_DEVICE_SVM_COARSE_GRAIN_BUFFER_ARM		CL_DEVICE_SVM_COARSE_GRAIN_BUFFER
#define CL_DEVICE_SVM_FINE_GRAIN_BUFFER_ARM		CL_DEVICE_SVM_FINE_GRAIN_BUFFER
#define CL_DEVICE_SVM_FINE_GRAIN_SYSTEM_ARM		CL_DEVICE_SVM_FINE_GRAIN_SYSTEM
#define CL_DEVICE_SVM_ATOMICS_ARM			CL_DEVICE_SVM_ATOMICS

/* cl_arm_core_id */
#define CL_DEVICE_COMPUTE_UNITS_BITFIELD_ARM		0x40BF

/* cl_arm_controlled_kernel_termination */
#define CL_DEVICE_CONTROLLED_TERMINATION_CAPABILITIES_ARM	0x41EE

typedef cl_bitfield cl_device_controlled_termination_capabilities_arm;
#define CL_DEVICE_CONTROLLED_TERMINATION_SUCCESS_ARM	(1 << 0)
#define CL_DEVICE_CONTROLLED_TERMINATION_FAILURE_ARM	(1 << 1)
#define CL_DEVICE_CONTROLLED_TERMINATION_QUERY_ARM	(1 << 2)

/* cl_khr_spir */
#define CL_DEVICE_SPIR_VERSIONS				0x40E0

/* cl_altera_device_temperature */
#define CL_DEVICE_CORE_TEMPERATURE_ALTERA		0x40F3

/* cl_intel_simultaneous_sharing */
#define CL_DEVICE_SIMULTANEOUS_INTEROPS_INTEL		0x4104
#define CL_DEVICE_NUM_SIMULTANEOUS_INTEROPS_INTEL	0x4105

/* cl_intel_required_subgroup_size */
#define CL_DEVICE_SUB_GROUP_SIZES_INTEL			0x4108

/* cl_intel_command_queue_families */
#define CL_DEVICE_QUEUE_FAMILY_PROPERTIES_INTEL		0x418B

typedef cl_bitfield         cl_command_queue_capabilities_intel;

#define CL_QUEUE_FAMILY_MAX_NAME_SIZE_INTEL                 64
typedef struct _cl_queue_family_properties_intel {
    cl_command_queue_properties properties;
    cl_command_queue_capabilities_intel capabilities;
    cl_uint count;
    char name[CL_QUEUE_FAMILY_MAX_NAME_SIZE_INTEL];
} cl_queue_family_properties_intel;

/* cl_arm_job_slot_selection */
#define CL_DEVICE_JOB_SLOTS_ARM				0x41E0

/* cl_arm_scheduling_controls */

typedef cl_bitfield cl_device_scheduling_controls_capabilities_arm;

#define CL_DEVICE_SCHEDULING_CONTROLS_CAPABILITIES_ARM	0x41E4

#define CL_DEVICE_SCHEDULING_KERNEL_BATCHING_ARM		(1 << 0)
#define CL_DEVICE_SCHEDULING_WORKGROUP_BATCH_SIZE_ARM		(1 << 1)
#define CL_DEVICE_SCHEDULING_WORKGROUP_BATCH_SIZE_MODIFIER_ARM	(1 << 2)
#define CL_DEVICE_SCHEDULING_DEFERRED_FLUSH_ARM			(1 << 3)
#define CL_DEVICE_SCHEDULING_REGISTER_ALLOCATION_ARM		(1 << 4)
#define CL_DEVICE_SCHEDULING_WARP_THROTTLING_ARM		(1 << 5)
#define CL_DEVICE_SCHEDULING_COMPUTE_UNIT_BATCH_QUEUE_SIZE_ARM	(1 << 6)
#define CL_DEVICE_SCHEDULING_COMPUTE_UNIT_LIMIT_ARM		(1 << 7)

#define CL_DEVICE_MAX_WARP_COUNT_ARM			0x41EA
#define CL_DEVICE_SUPPORTED_REGISTER_ALLOCATIONS_ARM	0x41EB

/* cl_intel_device_attribute_query */

typedef cl_bitfield cl_device_feature_capabilities_intel;

#define CL_DEVICE_FEATURE_FLAG_DP4A_INTEL	(1 << 0)
#define CL_DEVICE_FEATURE_FLAG_DPAS_INTEL	(1 << 1)

#define CL_DEVICE_IP_VERSION_INTEL			0x4250
#define CL_DEVICE_ID_INTEL				0x4251
#define CL_DEVICE_NUM_SLICES_INTEL			0x4252
#define CL_DEVICE_NUM_SUB_SLICES_PER_SLICE_INTEL	0x4253
#define CL_DEVICE_NUM_EUS_PER_SUB_SLICE_INTEL		0x4254
#define CL_DEVICE_NUM_THREADS_PER_EU_INTEL		0x4255
#define CL_DEVICE_FEATURE_CAPABILITIES_INTEL		0x4256

/* clGeICDLoaderInfoOCLICD */
typedef enum {
	CL_ICDL_OCL_VERSION=1,
	CL_ICDL_VERSION=2,
	CL_ICDL_NAME=3,
	CL_ICDL_VENDOR=4,
} cl_icdl_info;

#endif

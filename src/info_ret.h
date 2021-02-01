#ifndef INFO_RET_H
#define INFO_RET_H

#include "ext.h"
#include "strbuf.h"

/* The cl_device_topology_amd structure is missing domain infromation, but the NV queries expose them,
 * so we define our own structure that supersets both, and convert to this before printing */

struct clinfo_device_topology_pci {
	cl_uint domain;
	cl_uchar bus;
	cl_uchar device;
	cl_uchar function;
};

/* Return type of the functions that gather platform info */
struct platform_info_ret
{
	cl_int err;
	/* string representation of the value (if any) */
	struct _strbuf str;
	/* error representation of the value (if any) */
	struct _strbuf err_str;
	/* actual value, when not a string */
	union {
		size_t s;
		cl_uint u32;
		cl_ulong u64;
	} value;
	/* Does this ret need escaping as JSON? */
	cl_bool needs_escaping;
};

/* Return type of the functions that print device info */
struct device_info_ret {
	cl_int err;
	/* string representation of the value (if any) */
	struct _strbuf str;
	/* error representation of the value (if any) */
	struct _strbuf err_str;
	/* actual value, when not a string */
	union {
		size_t s;
		cl_long i64;
		cl_ulong u64;
		cl_ulong2 u64v2;
		cl_ulong4 u64v;
		cl_int i32;
		cl_uint u32;
		cl_uint4 u32v;
		cl_bitfield bits;
		cl_bool b;
		cl_device_type devtype;
		cl_device_mem_cache_type cachetype;
		cl_device_local_mem_type lmemtype;
		cl_device_topology_amd devtopo_amd;
		cl_device_affinity_domain affinity_domain;
		cl_device_fp_config fpconfig;
		cl_command_queue_properties qprop;
		cl_device_exec_capabilities execap;
		cl_device_svm_capabilities svmcap;
		cl_device_terminate_capability_khr termcap;
		struct clinfo_device_topology_pci devtopo_pci;
	} value;
	/* pointer base for array data or other auxiliary information */
	union {
		void *ptr; // TODO
		cl_context ctx; // associated context
	} base;
	/* Does this ret need escaping as JSON? */
	cl_bool needs_escaping;
};

/* Return type of the functions that gather ICD loader info */
struct icdl_info_ret
{
	cl_int err;
	/* string representation of the value (if any) */
	struct _strbuf str;
	/* error representation of the value (if any) */
	struct _strbuf err_str;
};

#define RET_BUF(ret) (ret.err ? &ret.err_str : &ret.str)
#define RET_BUF_PTR(ret) (ret->err ? &ret->err_str : &ret->str)
#define INIT_RET(ret, msg) do { \
	init_strbuf(&ret.str, msg " info string values"); \
	init_strbuf(&ret.err_str, msg " info error values"); \
} while (0)

#define UNINIT_RET(ret) do { \
	free_strbuf(&ret.str); \
	free_strbuf(&ret.err_str); \
} while (0)


#endif

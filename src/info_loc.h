#ifndef INFO_LOC_H
#define INFO_LOC_H

#include "ext.h"

struct info_loc {
	const char *function;
	const char *sname; // parameter symbolic name
	const char *pname; // parameter printable name
	size_t line;
	cl_platform_id plat;
	cl_device_id dev;
	union {
		cl_platform_info plat;
		cl_device_info dev;
		cl_icdl_info icdl;
	} param;
};

static inline void reset_loc(struct info_loc *loc, const char *func)
{
	loc->function = func;
	loc->sname = loc->pname = NULL;
	loc->line = 0;
	loc->plat = NULL;
	loc->dev = NULL;
	loc->param.plat = 0;
}

#define RESET_LOC_PARAM(_loc, _dev, _param) do { \
	_loc.param._dev = _param; \
	_loc.sname = #_param; \
} while (0)

#endif

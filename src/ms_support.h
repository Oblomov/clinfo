/* Missing functions and other misc stuff to support
 * the horrible MS C compiler
 *
 * TODO could be improved by version-checking for C99 support
 */

#ifndef MS_SUPPORT
#define MS_SUPPORT

// disable warning about unsafe strncpy vs strncpy_s usage
#pragma warning(disable : 4996)
// disable warning about constant conditional expressions
#pragma warning(disable : 4127)
// disable warning about non-constant aggregate initializer
#pragma warning(disable : 4204)

// disable warning about global shadowing
#pragma warning(disable : 4459)
// disable warning about parameter shadowing
#pragma warning(disable : 4457)

// Suppress warning about unused parameters. The macro definition
// _should_ work, but it doesn't on VS2012 (cl 17), may be a version thing
#define UNUSED(x) x __pragma(warning(suppress: 4100))
// TODO FIXME remove full-blown warning removal where not needed
#pragma warning(disable: 4100)

// No inline in MS C
#define inline __inline

// No snprintf in MS C, copy over implementation taken from
// stackoverflow

#include <stdarg.h>
#include <stdio.h>

inline int c99_vsnprintf(char* str, size_t size, const char* format, va_list ap)
{
    int count = -1;

    if (size != 0)
        count = _vsnprintf_s(str, size, _TRUNCATE, format, ap);
    if (count == -1)
        count = _vscprintf(format, ap);

    return count;
}

inline int c99_snprintf(char* str, size_t size, const char* format, ...)
{
    int count;
    va_list ap;

    va_start(ap, format);
    count = c99_vsnprintf(str, size, format, ap);
    va_end(ap);

    return count;
}

#define snprintf c99_snprintf

// And no __func__ either

#define __func__ __FUNCTION__

#endif

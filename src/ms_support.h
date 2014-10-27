/* Missing functions and other misc stuff to support
 * the horrible MS C compiler
 *
 * TODO could be improved by version-checking for C99 support
 */

// also disable strncpy vs strncpy_s warning
#pragma warning(disable : 4996)

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

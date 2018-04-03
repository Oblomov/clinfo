/* cl_ulong is always a 64bit integer, so in a few places
   we want to use its shadow type uint64_t, and print the
   values using PRIu64. We'll similarly define one for
   size_t, to make support for non-standard/older compiler
   easier.
 */

#ifndef FMT_MACROS_H
#define FMT_MACROS_H

#ifdef _WIN32
/* TODO FIXME WIN64 support */
# include <stdint.h>
# include <stddef.h> // size_t
# define PRIu32 "I32u"
# define PRId32 "I32d"
# define PRIx32 "I32x"
# define PRIX32 "I32X"
# define PRIu64 "I64u"
# define PRIx64 "I64x"
# define PRIX64 "I64X"
# define PRIuS "Iu"
#if INTPTR_MAX <= INT32_MAX
# define PRIXPTR PRIX32
# define PRIxPTR PRIx32
#else
# define PRIXPTR PRIX64
# define PRIxPTR PRIx64
#endif
#else
# define __STDC_FORMAT_MACROS
# include <inttypes.h>
#endif

// size_t print spec
#ifndef PRIuS
# define PRIuS "zu"
#endif

#endif

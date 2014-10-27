/* cl_ulong is always a 64bit integer, so in a few places
   we want to use its shadow type uint64_t, and print the
   values using PRIu64. We'll similarly define one for
   size_t, to make support for non-standard/older compiler
   easier.
 */

#ifdef _MSC_VER
#  include <stdint.h>
#  include <stddef.h> // size_t
#  define PRIu64 "I64u"
#  define PRIX64 "I64x"
#  define PRIXPTR "I64x" // TODO FIXME
#  define PRIuS "I32u" // TODO FIXME
#else
# define __STDC_FORMAT_MACROS
# include <inttypes.h>
#endif

// size_t print spec
#ifndef PRIuS
# define PRIuS "zu"
#endif


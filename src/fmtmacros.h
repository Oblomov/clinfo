/* cl_ulong is always a 64bit integer, so in a few places
   we want to use its shadow type uint64_t, and print the
   values using PRIu64. We'll similarly define one for
   size_t, to make support for non-standard/older compiler
   easier.
 */

#ifndef _FMT_MACROS_H
#define _FMT_MACROS_H

#ifdef _WIN32
#  include <stdint.h>
#  include <stddef.h> // size_t
#  define PRIu64 "I64u"
#  define PRIX64 "I64x"
#  define PRIXPTR "p"
#  define PRIuS "Iu"
#else
# define __STDC_FORMAT_MACROS
# include <inttypes.h>
#endif

// size_t print spec
#ifndef PRIuS
# define PRIuS "zu"
#endif

#endif

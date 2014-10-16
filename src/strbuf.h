/* multi-purpose string strbuf, will be initialized to be
 * at least 1024 bytes long.
 */

#include <ctype.h>

char *strbuf;
size_t bufsz, nusz;

#define GET_STRING(cmd, param, param_str, ...) do { \
	error = cmd(__VA_ARGS__, param, 0, NULL, &nusz); \
	if (nusz > bufsz) { \
		REALLOC(strbuf, nusz, #param); \
		bufsz = nusz; \
	} \
	if (REPORT_ERROR("get " param_str " size")) break; \
	error = cmd(__VA_ARGS__, param, bufsz, strbuf, 0); \
	REPORT_ERROR("get " param_str); \
} while (0)

/* Skip leading whitespace in a string */
static inline const char* skip_leading_ws(const char *str)
{
	const char *ret = str;
	while (isspace(*ret)) ++ret;
	return ret;
}


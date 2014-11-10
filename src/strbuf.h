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

/* replace last 3 chars in strbuf with ... */
static const char ellip[] = "...";

static void trunc_strbuf(void)
{
	memcpy(strbuf + bufsz - 4, ellip, 4);
}

/* copy a string to strbuf, at the given offset,
 * returning the amount of bytes written (excluding the
 * closing NULL byte)
 */
static inline size_t bufcpy(size_t offset, const char *str)
{
	size_t len = strlen(str);
	size_t maxlen = bufsz - offset - 1;
	char *dst = strbuf + offset;
	int trunc = 0;
	if (bufsz < offset) {
		fprintf(stderr, "bufcpy overflow copying %s at offset %zu/%zu (%s)\n",
			str, offset, bufsz, strbuf);
		maxlen = 0;
		trunc = 1;
	}
	if (len > maxlen) {
		len = maxlen;
		trunc = 1;
		/* TODO enlarge strbuf instead, if maxlen > 0 */
	}
	memcpy(dst, str, len);
	offset += len;
	if (trunc)
		trunc_strbuf();
	else
		strbuf[offset] = '\0';
	return len;
}

/* multi-purpose string strbuf, will be initialized to be
 * at least 1024 bytes long.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include "fmtmacros.h"

char *strbuf;
size_t bufsz, nusz;

#define GET_STRING(cmd, param, param_str, ...) do { \
	error = cmd(__VA_ARGS__, param, 0, NULL, &nusz); \
	if (nusz > bufsz) { \
		REALLOC(strbuf, nusz, #param); \
		bufsz = nusz; \
	} \
	if (REPORT_ERROR("get " param_str " size")) break; \
	error = cmd(__VA_ARGS__, param, bufsz, strbuf, NULL); \
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
static inline size_t bufcpy_len(size_t offset, const char *str, size_t len)
{
	size_t maxlen = bufsz - offset - 1;
	char *dst = strbuf + offset;
	int trunc = 0;
	if (bufsz < offset) {
		fprintf(stderr, "bufcpy overflow copying %s at offset %" PRIuS "/%" PRIuS " (%s)\n",
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

/* As above, auto-compute string length */
static inline size_t bufcpy(size_t offset, const char *str)
{
	return bufcpy_len(offset, str, strlen(str));
}


/* Separators: we want to be able to prepend separators as needed to strbuf,
 * which we do only if halfway through the buffer. The callers should first
 * call a 'set_separator' and then use add_separator(&offset) to add it, where szval
 * is an offset inside the buffer, which will be incremented as needed
 */

const char *sep;
size_t sepsz;

void set_separator(const char* _sep)
{
	sep = _sep;
	sepsz = strlen(sep);
}

/* Note that no overflow check is done: it is assumed that strbuf will have enough room */
void add_separator(size_t *offset)
{
	if (*offset)
		*offset += bufcpy_len(*offset, sep, sepsz);
}

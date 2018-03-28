/* multi-purpose string _strbuf, will be initialized to be
 * at least 1024 bytes long.
 */

#ifndef STRBUF_H
#define STRBUF_H

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "memory.h"
#include "fmtmacros.h"

struct _strbuf
{
	char *buf;
	size_t sz;
};

static inline void init_strbuf(struct _strbuf *str)
{
	str->buf = NULL;
	str->sz = 0;
}

static inline void free_strbuf(struct _strbuf *str)
{
	free(str->buf);
	init_strbuf(str);
}

#define strbuf_printf(str, ...) snprintf((str)->buf, (str)->sz, __VA_ARGS__)

static inline void realloc_strbuf(struct _strbuf *str, size_t nusz, const char* what)
{
	if (nusz > str->sz) {
		REALLOC(str->buf, nusz, what);
		str->sz = nusz;
	}
}

#define GET_STRING(str, err, cmd, param, param_str, ...) do { \
	size_t nusz; \
	err = cmd(__VA_ARGS__, param, 0, NULL, &nusz); \
	if (REPORT_ERROR(str, err, "get " param_str " size")) break; \
	realloc_strbuf(str, nusz, #param); \
	err = cmd(__VA_ARGS__, param, (str)->sz, (str)->buf, NULL); \
	REPORT_ERROR(str, err, "get " param_str); \
} while (0)

#define GET_STRING_LOC(ret, loc, cmd, ...) do { \
	size_t nusz; \
	ret->err = REPORT_ERROR_LOC(ret, \
		cmd(__VA_ARGS__, 0, NULL, &nusz), \
		loc, "get %s size"); \
	if (!ret->err) { \
		realloc_strbuf(&ret->str, nusz, loc->sname); \
		ret->err = REPORT_ERROR_LOC(ret, \
			cmd(__VA_ARGS__, ret->str.sz, ret->str.buf, NULL), \
			loc, "get %s"); \
	} \
} while (0)

/* Skip leading whitespace in a string */
static inline const char* skip_leading_ws(const char *str)
{
	const char *ret = str;
	while (isspace(*ret)) ++ret;
	return ret;
}

/* replace last 3 chars in _strbuf with ... */
static const char ellip[] = "...";

static inline void trunc_strbuf(struct _strbuf *str)
{
	memcpy(str->buf + str->sz - 4, ellip, 4);
}

/* copy a string to _strbuf, at the given offset,
 * returning the amount of bytes written (excluding the
 * closing NULL byte)
 */
static inline size_t bufcpy_len(struct _strbuf *str,
	size_t offset, const char *src, size_t len)
{
	size_t maxlen = str->sz - offset - 1;
	char *dst = str->buf + offset;
	int trunc = 0;
	if (str->sz < offset) {
		fprintf(stderr, "bufcpy overflow copying %s at offset %" PRIuS "/%" PRIuS " (%s)\n",
			src, offset, str->sz, str->buf);
		maxlen = 0;
		trunc = 1;
	}
	if (len > maxlen) {
		len = maxlen;
		trunc = 1;
		/* TODO enlarge str->buf instead, if maxlen > 0 */
	}
	memcpy(dst, src, len);
	offset += len;
	if (trunc)
		trunc_strbuf(str);
	else
		str->buf[offset] = '\0';
	return len;
}

/* As above, auto-compute string length */
static inline size_t bufcpy(struct _strbuf *str, size_t offset, const char *src)
{
	return bufcpy_len(str, offset, src, strlen(src));
}


/* Separators: we want to be able to prepend separators as needed to _strbuf,
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

/* Note that no overflow check is done: it is assumed that _strbuf will have enough room */
void add_separator(struct _strbuf *str, size_t *offset)
{
	if (*offset)
		*offset += bufcpy_len(str, *offset, sep, sepsz);
}

#endif

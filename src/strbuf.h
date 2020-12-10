/* multi-purpose string _strbuf, will be initialized to be
 * at least 1024 bytes long.
 */

#ifndef STRBUF_H
#define STRBUF_H

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "memory.h"
#include "fmtmacros.h"

struct _strbuf
{
	char *buf;
	size_t sz; /* allocated size */
	size_t end; /* offset to terminating null byte */
};

static inline void realloc_strbuf(struct _strbuf *str, size_t nusz, const char* what)
{
	if (nusz > str->sz) {
		REALLOC(str->buf, nusz, what);
		str->sz = nusz;
	}
}

static inline void reset_strbuf(struct _strbuf *str)
{
	str->end = 0;
	if (str->buf) str->buf[0] = '\0';
}

static inline void init_strbuf(struct _strbuf *str, const char *what)
{
	str->sz = 0;
	str->buf = NULL;
	realloc_strbuf(str, 1024, what);
	reset_strbuf(str);
}

static inline void free_strbuf(struct _strbuf *str)
{
	free(str->buf);
	str->buf = NULL;
	reset_strbuf(str);
}

static inline void strbuf_append(const char *what, struct _strbuf *str, const char *fmt, ...)
{
	va_list ap;
	size_t room = str->sz - str->end - 1;
	size_t written = 0;

	/* write if we have room */
	va_start(ap, fmt);
	written = vsnprintf(str->buf + str->end, room, fmt, ap);
	va_end(ap);

	/* if we would have written more, we need to expand the storage */
	if (written >= room) {
		realloc_strbuf(str, str->end + written + 1, what);
		room = str->sz - str->end;

		/* and re-write */
		va_start(ap, fmt);
		written = vsnprintf(str->buf + str->end, room, fmt, ap);
		va_end(ap);
	}
	str->end += written;
}

static inline void strbuf_append_str_len(const char *what, struct _strbuf *str,
	const char *to_append, /* string to append */
	size_t len) /* length of string to append */
{
	size_t room = str->sz - str->end - 1;

	if (len >= room) {
		realloc_strbuf(str, str->end + len + 1, what);
	}
	/* copy up to the terminating NULL */
	memcpy(str->buf + str->end, to_append, len);
	str->end += len;
	/* ensure we have a NULL in last position, since len may have been used
	 * to override the original string length */
	str->buf[str->end] = '\0';
}

static inline void strbuf_append_str(const char *what, struct _strbuf *str, const char *to_append)
{
	strbuf_append_str_len(what, str, to_append, strlen(to_append));
}

#define GET_STRING(str, err, cmd, param, param_str, ...) do { \
	size_t nusz; \
	err = cmd(__VA_ARGS__, param, 0, NULL, &nusz); \
	if (REPORT_ERROR(str, err, "get " param_str " size")) break; \
	realloc_strbuf(str, nusz, #param); \
	err = cmd(__VA_ARGS__, param, (str)->sz, (str)->buf, NULL); \
	if (REPORT_ERROR(str, err, "get " param_str)) break; \
	(str)->end = nusz; \
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
	if (!ret->err) { \
		ret->str.end = nusz; \
	} \
} while (0)

/* Skip leading whitespace in a string */
static inline const char* skip_leading_ws(const char *str)
{
	const char *ret = str;
	while (isspace((unsigned char) *ret)) ++ret;
	return ret;
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

#endif

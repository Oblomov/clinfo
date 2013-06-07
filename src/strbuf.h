/* multi-purpose string strbuf, will be initialized to be
 * at least 1024 bytes long.
 */
char *strbuf;
size_t bufsz, nusz;

#define GET_STRING(cmd, param, ...) do { \
	error = cmd(__VA_ARGS__, param, 0, NULL, &nusz); \
	CHECK_ERROR("get " #param " size"); \
	if (nusz > bufsz) { \
		REALLOC(strbuf, nusz, #param); \
		bufsz = nusz; \
	} \
	error = cmd(__VA_ARGS__, param, bufsz, strbuf, 0); \
	CHECK_ERROR("get " #param); \
} while (0)



/* multi-purpose string strbuf, will be initialized to be
 * at least 1024 bytes long.
 */
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



/* Memory handling */

#include <stdlib.h>

#define CHECK_MEM(var, what) do { \
	if (!var) { \
		fprintf(stderr, "%s:%d: %s : Out of memory\n", \
			__func__, __LINE__, what); \
		exit(1); \
	} \
} while (0)

#define ALLOC(var, num, what) do { \
	var = calloc(num, sizeof(*var)); \
	CHECK_MEM(var, what); \
} while (0)

#define REALLOC(var, num, what) do { \
	var = realloc(var, (num)*sizeof(*var)); \
	CHECK_MEM(var, what); \
} while (0)

# Headers

HDR =	src/error.h \
	src/ext.h \
	src/fmtmacros.h \
	src/memory.h \
	src/ms_support.h \
	src/strbuf.h

VPATH = src

CFLAGS += -std=c99 -g -Wall -Wextra -pedantic

SPARSE ?= sparse
SPARSEFLAGS=-Wsparse-all -Wno-decl

# BSD make does not define RM
RM ?= rm -f

# Common library includes
# TODO ideally we would want this to be on "not Darwin",
# rather than shared, but I haven't found a way to achieve this
# using features supported by both GNU and BSD make
LDLIBS = -lOpenCL

# OS-specific library includes
LDLIBS_Darwin = -framework OpenCL
LDLIBS_Linux = -ldl

LDLIBS += $(LDLIBS_${OS})

clinfo: clinfo.o

clinfo.o: clinfo.c $(HDR)

clean:
	$(RM) clinfo.o clinfo

sparse: clinfo.c
	$(SPARSE) $(CPPFLAGS) $(CFLAGS) $(SPARSEFLAGS) $^

.PHONY: clean sparse

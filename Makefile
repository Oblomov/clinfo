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
LDLIBS = -lOpenCL -ldl

# OS-specific library includes
LDLIBS_Darwin = -framework OpenCL
LDLIBS_Darwin_exclude = -lOpenCL

LDLIBS += $(LDLIBS_${OS})

# Remove -lOpenCL if OS is Darwin
LDLIBS := $(LDLIBS:$(LDLIBS_${OS}_exclude)=)

clinfo: clinfo.o

clinfo.o: clinfo.c $(HDR)

clean:
	$(RM) clinfo.o clinfo

sparse: clinfo.c
	$(SPARSE) $(CPPFLAGS) $(CFLAGS) $(SPARSEFLAGS) $^

.PHONY: clean sparse

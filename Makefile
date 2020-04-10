# An interesting trick to run a shell command:
# GNU Make uses $(shell cmd), whereas
# BSD make use $(var:sh), where ${var} holds the command
OS.exec = uname -s
OS ?= $(shell $(OS.exec))$(OS.exec:sh)
OS := $(OS)

# Headers

PROG = clinfo
MAN = man1/$(PROG).1

HDR =	src/error.h \
	src/ext.h \
	src/ctx_prop.h \
	src/fmtmacros.h \
	src/memory.h \
	src/ms_support.h \
	src/info_loc.h \
	src/info_ret.h \
	src/opt_out.h \
	src/strbuf.h

VPATH = src

# Make it easier to find the OpenCL headers on systems
# that don't ship them by default; the user can just clone
# them on a parallel directory from the official repository
CPPFLAGS += -I../OpenCL-Headers

CFLAGS ?= -g -pedantic -Werror
CFLAGS += -std=c99 -Wall -Wextra

SPARSE ?= sparse
SPARSEFLAGS=-Wsparse-all -Wno-decl

# BSD make does not define RM
RM ?= rm -f

# Installation paths and modes
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
BINMODE ?= 555
MANDIR ?= $(PREFIX)/man
MANMODE ?= 444

ANDROID_VENDOR_PATH = ${ANDROID_ROOT}/vendor/lib64

LDFLAGS_Android += -Wl,-rpath-link=${ANDROID_VENDOR_PATH} -L${ANDROID_VENDOR_PATH}

LDFLAGS += $(LDFLAGS_$(OS))

# Common library includes
LDLIBS__common = -lOpenCL -ldl

# OS-specific library includes
LDLIBS_Darwin = -framework OpenCL
LDLIBS_Darwin_exclude = -lOpenCL

LDLIBS += $(LDLIBS_${OS}) $(LDLIBS__common:$(LDLIBS_${OS}_exclude)=)

#
# Standard targets
#

$(PROG): $(PROG).o

$(PROG).o: $(PROG).c $(HDR)

clean:
	$(RM) $(PROG).o $(PROG)

$(BINDIR):
	install -d $@

$(MANDIR)/man1:
	install -d $@

$(BINDIR)/$(PROG): $(PROG) $(BINDIR)
	install -p -m $(BINMODE) $(PROG) $@

$(MANDIR)/$(MAN): $(MAN) $(MANDIR)/man1
	install -p -m $(MANMODE) $(MAN) $@

install: $(BINDIR)/$(PROG) $(MANDIR)/$(MAN)


sparse: $(PROG).c
	$(SPARSE) $(CPPFLAGS) $(CFLAGS) $(SPARSEFLAGS) $^


.PHONY: clean sparse install

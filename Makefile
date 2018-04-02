# An interesting trick run a shell command:
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

# Common library includes
LDLIBS = -lOpenCL -ldl

# OS-specific library includes
LDLIBS_Darwin = -framework OpenCL
LDLIBS_Darwin_exclude = -lOpenCL

LDLIBS += $(LDLIBS_${OS})

# Remove -lOpenCL if OS is Darwin
LDLIBS := $(LDLIBS:$(LDLIBS_${OS}_exclude)=)


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

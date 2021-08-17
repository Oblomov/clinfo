# An interesting trick to run a shell command:
# GNU Make uses $(shell cmd), whereas
# BSD make use $(var:sh), where ${var} holds the command
# We can run a shell command on both by storing the value of the command
# in a variable var and then using $(shell $(var))$(var:sh).

# To detect the operating system it's generally sufficient to run `uname - s`,
# but this way Android is detected as Linux. Android can be detected by `uname -o`,
# but not all `uname` implementation even have the `-o` flag.
# So we first detect the kernel, and then if it's Linux we use the -o detection
# to find if this is Android, otherwise falling back to whatever the kernel was.

OS.exec = t="$$(uname -s)" ; [ Linux = "$$t" ] && uname -o || printf "%s\n" "$$t"
OS ?= $(shell $(OS.exec))$(OS.exec:sh)
# Force expansion
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

ANDROID_VENDOR_PATH ?= ${ANDROID_ROOT}/vendor/lib64

LDFLAGS_Android += -Wl,-rpath-link=${ANDROID_VENDOR_PATH} -L${ANDROID_VENDOR_PATH}

LDFLAGS += $(LDFLAGS_$(OS))

# Common library includes
LDLIBS__common = -lOpenCL -ldl

# OS-specific library includes
LDLIBS_Darwin = -framework OpenCL
LDLIBS_Darwin_exclude = -lOpenCL

LDLIBS += $(LDLIBS_${OS}) $(LDLIBS__common:$(LDLIBS_${OS}_exclude)=)

# The main target is the executable, which is normally called clinfo.
# However, on Android, due to the lack of support for RPATH, clinfo
# needs an approprite LD_LIBRARY_PATH, so we map `clinfo` to a shell script
# that sets LD_LIBRARY_PATH and invokes the real program, which is now called
# clinfo.real.
#
# Of course on Android we need to buid both, but not on other OSes


EXT.Android = .real
EXENAME = $(PROG)$(EXT.${OS})

TARGETS.Android = $(PROG)
TARGETS = $(EXENAME) $(TARGETS.${OS})

all: $(TARGETS)

#
# Targets to actually build the stuff
#
# Many versions of make define a LINK.c as a synthetic rule to link
# C object files. In case it's not defined already, propose our own:
LINK.c ?= $(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS)

# Recipe for the actual executable, either clinfo (non-Android)
# or clinfo.real (on Androd)
$(EXENAME): $(PROG).o
	$(LINK.c) -o $@ $< $(LDLIBS)

$(PROG).o: $(PROG).c $(HDR)

# For Android: create a wrapping shell script to run
# clinfo with the appropriate LD_LIBRARY_PATH.
$(OS:Android=)$(PROG):
	@echo '#!/bin/sh' > $@
	@echo 'LD_LIBRARY_PATH="${ANDROID_VENDOR_PATH}:${ANDROID_VENDOR_PATH}/egl:$$LD_LIBRARY_PATH" ./$(EXENAME) "$$@"' >> $@
	chmod +x $@

clean:
	$(RM) $(PROG).o $(TARGETS)

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(MANDIR)/man1

	install -p -m $(BINMODE) $(PROG) $(DESTDIR)$(BINDIR)/$(PROG)
	install -p -m $(MANMODE) $(MAN) $(DESTDIR)$(MANDIR)/man1

sparse: $(PROG).c
	$(SPARSE) $(CPPFLAGS) $(CFLAGS) $(SPARSEFLAGS) $^


.PHONY: clean sparse install

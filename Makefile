SRCDIR=src

VPATH=$(SRCDIR)

HDR=$(wildcard $(SRCDIR)/*.h)

PLATFORM=$(shell uname -s)

ifeq ($(PLATFORM),Darwin)
  LDLIBS=-framework OpenCL
else
  LDLIBS=-lOpenCL
endif

ifeq ($(PLATFORM),Linux)
  LDLIBS += -ldl
endif

CFLAGS+=-std=c99 -g -Wall -Wextra -pedantic

SPARSE ?= sparse
SPARSEFLAGS=-Wsparse-all -Wno-decl

clinfo:

clinfo.o: clinfo.c $(HDR)

clean:
	$(RM) clinfo.o clinfo

sparse: clinfo.c
	$(SPARSE) $(CPPFLAGS) $(CFLAGS) $(SPARSEFLAGS) $^

.PHONY: clean sparse

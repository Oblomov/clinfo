SRCDIR=src

VPATH=$(SRCDIR)

HDR=$(wildcard $(SRCDIR)/*.h)

PLATFORM=$(shell uname -s)

ifeq ($(PLATFORM),Darwin)
  LDLIBS=-framework OpenCL
else
  LDLIBS=-lOpenCL
endif

CFLAGS+=-std=c99 -g -Wall -Wextra

clinfo:

clinfo.o: clinfo.c $(HDR)

clean:
	$(RM) clinfo.o clinfo

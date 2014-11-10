SRCDIR=src

VPATH=$(SRCDIR)

HDR=$(wildcard $(SRCDIR)/*.h)

PLATFORM=$(shell uname -s)

ifeq ($(PLATFORM),Darwin)
  LDLIBS=-framework OpenCL
else
  LDLIBS=-lOpenCL
endif

CFLAGS+=-g -Wall

clinfo:

clinfo.o: clinfo.c $(HDR)

clean:
	$(RM) clinfo.o clinfo

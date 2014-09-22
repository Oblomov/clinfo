SRCDIR=src

VPATH=$(SRCDIR)

HDR=$(wildcard $(SRCDIR)/*.h)

LDLIBS=-lOpenCL

clinfo:

clinfo.o: clinfo.c $(HDR)

clean:
	$(RM) clinfo.o clinfo

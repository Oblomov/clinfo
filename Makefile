SRCDIR=src

VPATH=$(SRCDIR)

HDR=$(wildcard $(SRCDIR)/*.h)

LDLIBS=-lOpenCL

clinfo:

clinfo.o: clinfo.c $(HDR)

osx:
	$(MAKE) clinfo LDLIBS=-Wl,-framework,OpenCL

clean:
	$(RM) clinfo.o clinfo

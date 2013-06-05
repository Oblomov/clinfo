LDLIBS=-lOpenCL

vpath %.c src/

clinfo: clinfo.c

clean:
	$(RM) clinfo

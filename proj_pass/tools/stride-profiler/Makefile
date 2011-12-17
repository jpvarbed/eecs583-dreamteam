# Don't do -D__inline__= as this bones sys/stat.h
CFLAGS=-D_GNU_SOURCE -D_XOPEN_SOURCE=600 -c -Wall -Wno-deprecated -fpermissive -fexceptions -pedantic -Wno-long-long -g -O0 -I.

stride_hooks.o: stride_hooks.cxx
	g++ $(CFLAGS) -o $@ $<

all:  stride_hooks.o

clean:
	rm -rf *.o

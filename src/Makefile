CC = gcc -ggdb -Wall
CFLAGS = `pkg-config --cflags gstreamer-0.10 gstreamer-base-0.10 libsoup-2.4 librtmp`
LIBS = `pkg-config --libs gstreamer-0.10 gstreamer-base-0.10 libsoup-2.4 librtmp`

all: libgstjtvsrc.so

libgstjtvsrc.so: gstjtvsrc.o
	$(CC) -o $@ -shared -fPIC $< $(LIBS)

gstjtvsrc.o: gstjtvsrc.c gstjtvsrc.h Makefile
	$(CC) -c $< $(CFLAGS)

clean:
	rm -rf *~ *.so *.o

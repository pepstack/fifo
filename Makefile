# Makefile for fifo apps
# 2020-01-01
#   350137278@qq.com
#
#  https://blog.csdn.net/shaoxiaohu1/article/details/46943417
#
# show all predefinitions of gcc:
#
#  $ gcc -E -dM - </dev/null
###########################################################

PREFIX=.

CC=gcc

# build for release
CFLAGS=-D_GNU_SOURCE -DNDEBUG -O2

APPINCLUDE=-I$(PREFIX)/src

# fifo apps
FIFOCLIENT=fifoclient
FIFOSERVER=fifoserver

all: $(FIFOCLIENT) $(FIFOSERVER)


fifo.o: $(PREFIX)/src/fifo.c
	$(CC) $(CFLAGS) -c $(PREFIX)/src/fifo.c -o $@


# fifoclient
$(FIFOCLIENT): fifo.o $(PREFIX)/examples/fifoclient.c
	$(CC) $(CFLAGS) $(PREFIX)/examples/fifoclient.c $(APPINCLUDE) -o $@ \
	fifo.o \
	-lpthread -lrt -lm

# fifoserver
$(FIFOSERVER): fifo.o $(PREFIX)/examples/fifoserver.c
	$(CC) $(CFLAGS) $(PREFIX)/examples/fifoserver.c $(APPINCLUDE) -o $@ \
	fifo.o \
	-lpthread -lrt -lm

clean:
	-rm -f $(FIFOCLIENT)
	-rm -f $(FIFOSERVER)
	-rm -f fifo.o
	-rm -f ./msvc/fifo-win32/fifo-win32.VC.db
	-rm -f ./msvc/fifo-win32/fifo-win32.VC.VC.opendb
	-rm -f ./msvc/fifo-win32/fifo-win32.sdf
	-rm -rf ./msvc/fifo-win32/build
	-rm -rf ./msvc/fifo-win32/target
	-rm -rf ./msvc/fifo-win32/Debug

.PHONY: all clean

GNUTLS_CFLAGS = `pkg-config --cflags gnutls`
GNUTLS_LIBS   = `pkg-config --libs gnutls`

FUSE_CFLAGS = `pkg-config --cflags fuse`
FUSE_LIBS   = `pkg-config --libs fuse`

CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -O0 -g \
	-Wall -Werror -pedantic \
	$(GNUTLS_CFLAGS) $(FUSE_CFLAGS)

CC = gcc

COMMON = common.o struct_helpers.o tools.o
SRVOBJS = server.o operations.o paths.o $(COMMON)
CLIOBJS = client.o $(COMMON)
FSOBJS  = newfs.o clientops.o $(COMMON)

OBJS = $(SRVOBJS) $(CLIOBJS) $(FSOBJS)

all: server client newfs

struct_helpers.h: structs.h make_struct_helpers.py
	./make_struct_helpers.py

struct_helpers.c: struct_helpers.h

server:	$(SRVOBJS)
	$(CC) $(CFLAGS) $(GNUTLS_LIBS) $^ -o $@

client: $(CLIOBJS)
	$(CC) $(CFLAGS) $(GNUTLS_LIBS) $^ -o $@

newfs:  $(FSOBJS)
	$(CC) $(CFLAGS) $(GNUTLS_LIBS) $(FUSE_LIBS) $^ -o $@

-include $(OBJS:.o=.d)

%.d: %.c
	$(CC) -MM $(CPPFLAGS)-D_FILE_OFFSET_BITS=64 $< > $@

clean:
	rm -f $(OBJS) \
	rm -f server client struct_helpers.* *.d

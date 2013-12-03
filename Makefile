GNUTLS_CFLAGS = `pkg-config --cflags gnutls`
GNUTLS_LIBS = `pkg-config --libs gnutls`

CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -O0 -g -Wall -Werror -pedantic $(GNUTLS_CFLAGS)
CC = gcc

COMMON = common.o struct_helpers.o tools.o
SRVOBJS = server.o operations.o paths.o $(COMMON)
CLIOBJS = client.o $(COMMON)

OBJS = $(SRVOBJS) $(CLIOBJS)

all: server client

struct_helpers.h: structs.h make_struct_helpers.py
	./make_struct_helpers.py

struct_helpers.c: struct_helpers.h

server:	$(SRVOBJS)
	$(CC) $(CFLAGS) $(GNUTLS_LIBS) $^ -o $@

client: $(CLIOBJS)
	$(CC) $(CFLAGS) $(GNUTLS_LIBS) $^ -o $@

-include $(OBJS:.o=.d)

%.d: %.c
	$(CC) -MM $(CPPFLAGS) $< > $@

clean:
	rm -f $(OBJS) \
	rm -f server client struct_helpers.* *.d

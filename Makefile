CFLAGS = -std=c99 -D_POSIX_C_SOURCE -O0 -g -Wall -pedantic
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
	$(CC) $(CFLAGS) $^ -o $@

client: $(CLIOBJS)
	$(CC) $(CFLAGS) $^ -o $@

-include $(OBJS:.o=.d)

%.d: %.c
	$(CC) -MM $(CPPFLAGS) $< > $@

clean:
	rm -f $(OBJS) \
	rm -f server client struct_helpers.* *.d

CFLAGS = -std=c99 -D_POSIX_C_SOURCE -O0 -g -Wall -pedantic
CC = gcc

COMMON = common.o struct_helpers.o tools.o
SRVOBJS = server.o binsrv.o $(COMMON)
CLIOBJS = client.o $(COMMON)

OBJS = $(SRVOBJS) $(CLIOBJS)

all: server client

server:	$(SRVOBJS)
	$(CC) $(CFLAGS) $^ -o $@

client: $(CLIOBJS)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm server client $(OBJS)

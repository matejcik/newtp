CFLAGS = -std=c99 -D_POSIX_C_SOURCE -O0 -g -Wall -pedantic
CC = gcc

SRVOBJS = server.o binsrv.o tools.o
CLIOBJS = client.o tools.o

OBJS = $(SRVOBJS) $(CLIOBJS)

all: server client

server:	$(SRVOBJS)
	$(CC) $(CFLAGS) $^ -o $@

client: $(CLIOBJS)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm server client $(OBJS)

CFLAGS = -std=c99 -D_POSIX_C_SOURCE -O0 -g -Wall -pedantic
CC = gcc

OBJS = server.o work.o

all: server

server:	$(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm server $(OBJS)

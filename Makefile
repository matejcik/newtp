CFLAGS = -std=c99 -D_POSIX_C_SOURCE -O0 -g -Wall -pedantic
CC = gcc

COMMON = common.o struct_helpers.o tools.o
SRVOBJS = server.o binsrv.o $(COMMON)
CLIOBJS = client.o $(COMMON)

OBJS = $(SRVOBJS) $(CLIOBJS)

all: server client

struct_helpers.h: structs.h make_struct_helpers.py
	./make_struct_helpers.py

server:	$(SRVOBJS)
	$(CC) $(CFLAGS) $^ -o $@

client: $(CLIOBJS)
	$(CC) $(CFLAGS) $^ -o $@

-include $(subst .o,.d,$(OBJS))

%.d: %.c
	$(CC) -M $(CPPFLAGS) $< > $@.$$$$;                  \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

clean:
	rm -f $(OBJS) \
	rm -f server client struct_helpers.c *.d

#!/usr/bin/python

FILENAME = 'struct_helpers'
h_label = FILENAME.upper() + '__H__'
h_header = """\
#ifndef %s
#define %s

#include "structs.h"

""" % (h_label,h_label)
h_footer = """\
#endif /* %s */
""" % h_label

c_header = """\
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "structs.h"
#include "log.h"
#include "%s.h"

""" % FILENAME
c_footer = ""

def h_send_struct (s):
	return "int send_%s (int sock, struct %s *);\n" % (s,s)

def h_recv_struct (s):
	return "int recv_%s (int sock, struct %s *);\n" % (s,s)

def h_send_struct_params (s, p):
	return "int send_%s_p (int sock, %s);\n" % (s, ', '.join(map(lambda x: x[0], p)))

cf_recv_head = """\
int recv_%s (int sock, struct %s * s)
{
	int ret;
	int len = %s;
	char buf[len];
	int pos = 0;

	assert(s);

	if ((ret = recv_full(sock, buf, len)) <= 0) return ret;
""";
cf_recv_foot = """\
	return ret;
}
""";

cf_sendp_head = """\
int send_%s_p (int sock, %s)
{
	int len = %s;
	char buf[len];
	int pos = 0;
""";
cf_sendp_foot = """\
	return send_full(sock, buf, len);
}
""";

cf_send = """\
int send_%s (int sock, struct %s * s)
{
	return send_%s_p (sock, %s);
}
""";

def c_functions (s, p):
	length = ' + '.join(map(lambda a: "sizeof(%s)" % a[0], p))
	res = []
	# recv
	recv = [cf_recv_head % (s, s, length)]
	for tp, name in p:
		recv.append("	memcpy(&s->%s, buf + pos, sizeof(%s));" % (name, tp))
		recv.append("	pos += sizeof(%s);" % tp)
		if tp == 'uint16_t':
			recv.append("	s->%s = ntohs(s->%s);" % (name,name))
		elif tp == 'uint32_t':
			recv.append("	s->%s = ntohl(s->%s);" % (name,name))
	recv.append(cf_recv_foot)
	res.append('\n'.join(recv))

	# send
	sendp = [cf_sendp_head % (s, ', '.join(map(lambda a: "%s %s" % (a[0],a[1]), p)), length)]
	for tp, name in p:
		if tp == 'uint16_t':
			sendp.append("	%s = htons(%s);" % (name,name))
		elif tp == 'uint32_t':
			sendp.append("	%s = htonl(%s);" % (name,name))
		sendp.append("	memcpy(buf + pos, &%s, sizeof(%s));" % (name, tp))
		sendp.append("	pos += sizeof(%s);" % tp)
	sendp.append(cf_sendp_foot)
	res.append('\n'.join(sendp))

	send = cf_send % (s, s, s, ', '.join(map(lambda a: "s->" + a[1], p)))
	res.append(send)

	return '\n'.join(res)

import re

start = re.compile('^struct ([a-z_]+) {$')
item = re.compile('([a-z0-9_]+)\s+([a-z_]+);(?:\s*/\*.*\*/)?')
end = re.compile('^};$')

h = open(FILENAME + '.h', 'w')
h.write(h_header)

c = open(FILENAME + '.c', 'w')
c.write(c_header)

struct = None
items = []

src = open('structs.h', 'r')
for line in src:
	line = line.strip()
	if not struct:
		match = start.match(line)
		if match:
			struct = match.group(1)
			h.write(h_recv_struct(struct))
			h.write(h_send_struct(struct))
			items = []
	else:
		match = item.match(line)
		if match:
			items.append((match.group(1), match.group(2)))
		else:
			match = end.match(line)
			if match:
				h.write(h_send_struct_params(struct,items))
				h.write("\n")
				c.write(c_functions(struct,items))
				struct = None
			else:
				print "not matched:",line


h.write(h_footer)
h.close()

c.write(c_footer)
c.close()

src.close()

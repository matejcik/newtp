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

def h_defs (s, f, l):
	return '#define FORMAT_%s "%s"\n#define SIZEOF_%s (%s)\n' % (s,f,s,l)

def h_send_struct (s):
	return "int send_%s (int sock, struct %s *);\n" % (s,s)

def h_recv_struct (s):
	return "int recv_%s (int sock, struct %s *);\n" % (s,s)

def h_send_struct_params (s, p):
	return "int send_%s_p (int sock, %s);\n" % (s, ', '.join(map(lambda x: x[0], p)))

def cf_recv (s, f):
	return """\
int recv_%s (int sock, struct %s * s)
{
	int ret;
	char buf[SIZEOF_%s];

	assert(s);

	if ((ret = recv_full(sock, buf, SIZEOF_%s)) <= 0) return ret;
	assert(ret == SIZEOF_%s);
	assert(unpack(buf, FORMAT_%s, %s) == SIZEOF_%s);
	return ret;
}
""" % (s, s, s, s, s, s, f, s)

def cf_sendp (s, p, f):
	return """\
int send_%s_p (int sock, %s)
{
	char buf[SIZEOF_%s];
	assert(pack(buf, FORMAT_%s, %s) == SIZEOF_%s);
	return send_full(sock, buf, SIZEOF_%s);
}
""" % (s,p,s,s,f,s,s);

def cf_send (s, f):
	return """\
int send_%s (int sock, struct %s * s)
{
	return send_%s_p (sock, %s);
}
""" % (s,s,s,f);

def genthings (p):
	length = ' + '.join(map(lambda a: "sizeof(%s)" % a[0], p))
	res = []
	fmt = []
	plist = []
	previous = ''
	for tp, name in p:
		if tp == 'uint8_t': fmt.append('c')
		elif tp == 'uint16_t': fmt.append('s')
		elif tp == 'uint32_t': fmt.append('i')
		elif tp == 'uint64_t': fmt.append('l')
		elif tp == 'char *':
			if previous == 'uint16_t':
				fmt.append('B')
			else:
				fmt.append('Z')
		else:
			print "unknown type:",tp

		res.append(name)
		plist.append("%s const %s" % (tp, name))
		previous = tp
	
	return (length,res,''.join(fmt),', '.join(plist))

import re

start = re.compile('^struct ([a-z_]+) {$')
item = re.compile('([a-z0-9_]+(?: ?\*)?)\s+([a-z_]+);(?:\s*/\*.*\*/)?')
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
			items = []
	else:
		match = item.match(line)
		if match:
			items.append((match.group(1), match.group(2)))
		else:
			match = end.match(line)
			if match:
				length, res, fmt, plist = genthings(items)
				h.write(h_defs(struct, fmt, length))
				h.write(h_recv_struct(struct))
				h.write(h_send_struct(struct))
				h.write(h_send_struct_params(struct,items))
				h.write("\n")

				recvs = ', '.join(map(lambda a: "&s->%s" % a, res))
				c.write(cf_recv(struct, recvs))
				c.write("\n")
				sends = ', '.join(map(lambda a: "s->%s" % a, res))
				c.write(cf_send(struct, sends))
				c.write("\n")
				c.write(cf_sendp(struct, plist, ', '.join(res)))
				
				struct = None
			else:
				print "not matched:",line


h.write(h_footer)
h.close()

c.write(c_footer)
c.close()

src.close()

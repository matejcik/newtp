#!/usr/bin/python3

FILENAME = 'struct_helpers'
h_label = FILENAME.upper() + '__H__'
h_header = """\
#ifndef {0}
#define {0}

#include "structs.h"

""".format(h_label)
h_footer = """\
#endif /* {0} */
""".format(h_label)

c_header = """\
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "structs.h"
#include "log.h"
#include "{0}.h"

""".(FILENAME)
c_footer = ""

def h_defs (name, format_str, len_list, fields):
    return """\
#define FORMAT_{name} "{format_str}"
#define SIZEOF_{name}(s) ({len_list})
int pack_{name} (char * const buf, struct {name} const * s);
int pack_{name}_p (char * const buf, {fields});
int unpack_{name} (char const * const buf, struct {name} * s);
""".format(locals())

def cf_functions (name, struct_fields):
    return """\
int pack_{name} (char * buf, struct {name} * s)
{
    int size;

    assert(s);
    assert(buf);
    size = pack(buf, FORMAT_{name}, {struct_fields});
    assert(size == SIZEOF_{name}(s));
    return size;
}

int pack_{name}_p (char * constbuf);

int unpack_{name} (char const * const buf, struct {name} * s)
{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, FORMAT_{name}, {fields});
    assert(size == SIZEOF_{name}(s));
    return size;
}
""".format(locals())

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

def make_format (p):
    fmt = []
    for tp, name in p:
        if tp == 'uint8_t': fmt.append('c')
        elif tp == 'uint16_t': fmt.append('s')
        elif tp == 'uint32_t': fmt.append('i')
        elif tp == 'uint64_t': fmt.append('l')
        elif tp == 'char *':
            fmt.append('B')
        elif tp.startswith('char['):
            n = tp[5:6]
            fmt.append(n)
            fmt.append('B')
        else:
            print "unknown type:",tp

    return ''.join(fmt)

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

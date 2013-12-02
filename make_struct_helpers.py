#!/usr/bin/env python3

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

""".format(FILENAME)

c_footer = ""

def h_defs (name, format_str, fields):
    def len_item(x):
        tp, name = x
        if tp == "char *":
            return "(s)->{}_len".format(name)
        else:
            return "sizeof({})".format(tp)
    len_list = ' + '.join(map(len_item, fields))
    field_types = ', '.join(map(lambda x: x[0] + " const", fields))

    return """\
#define FORMAT_{name} "{format_str}"
#define SIZEOF_{name}(s) ({len_list})
int pack_{name} (char * const buf, struct {name} const * s);
int pack_{name}_p (char * const buf, {field_types});
int unpack_{name} (char const * const buf, struct {name} * s);
""".format(**locals())

def c_functions (name, fields):
    field_refs = ', '.join(map(lambda x: "s->" + x[1], fields))
    field_prefs = ', '.join(map(lambda x: "&s->" + x[1], fields))
    field_list = ', '.join(map(lambda x: x[0] + " const " + x[1], fields))
    field_names = ', '.join(map(lambda x: x[1], fields))
    return """\
int pack_{name} (char * const buf, struct {name} const * s)
{{
    assert(s);
    return pack_{name}_p(buf, {field_refs});
}}

int pack_{name}_p (char * const buf, {field_list})
{{
    int PACK_size;

    assert(buf);
    PACK_size = pack(buf, FORMAT_{name}, {field_names});
    /* assert(size == SIZEOF_{name}(s)); */
    return PACK_size;
}}

int unpack_{name} (char const * const buf, struct {name} * s)
{{
    int size;

    assert(s);
    assert(buf);
    size = unpack(buf, FORMAT_{name}, {field_prefs});
    assert(size == SIZEOF_{name}(s));
    return size;
}}
""".format(**locals())

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
            print("unknown type:",tp)

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
                format_str = make_format(items)
                h.write(h_defs(struct, format_str, items))
                h.write("\n")

                c.write(c_functions(struct, items))
                c.write("\n")

                struct = None

            else:
                print("not matched:",line)


h.write(h_footer)
h.close()

c.write(c_footer)
c.close()

src.close()

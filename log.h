#ifndef LOG__H__
#define LOG__H__

#include <stdio.h>
#include <unistd.h>

#define log__base_p(p, s, ...) fprintf(stderr, "child %d: " p " " s "\n", getpid(), __VA_ARGS__)
#define log__base(p, s) fprintf(stderr, "child %d: " p " " s "\n", getpid())

#define err(s) log__base("ERR", s)
#define errp(s, ...) log__base_p("ERR", s, __VA_ARGS__)

#define warn(s) log__base("WARN", s)
#define warnp(s, ...) log__base_p("WARN", s, __VA_ARGS__)

#define log(s) log__base("", s)
#define logp(s, ...) log__base_p("", s, __VA_ARGS__)

#endif

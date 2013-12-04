#ifndef LOG__H__
#define LOG__H__

#include <stdio.h>
#include <unistd.h>

#define ERR_STR  "** ERR: "
#define WARN_STR "* WARN: "
#define INFO_STR "(info): "

#define log__base_p(p, s, ...) fprintf(stderr, "child %d: " p s "\n", getpid(), __VA_ARGS__)
#define log__base(p, s) fprintf(stderr, "child %d: " p s "\n", getpid())

#define err(s) log__base(ERR_STR, s)
#define errp(s, ...) log__base_p(ERR_STR, s, __VA_ARGS__)

#define warn(s) log__base(WARN_STR, s)
#define warnp(s, ...) log__base_p(WARN_STR, s, __VA_ARGS__)

#define log(s) log__base(INFO_STR, s)
#define logp(s, ...) log__base_p(INFO_STR, s, __VA_ARGS__)

#endif

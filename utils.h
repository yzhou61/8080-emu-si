#include <stdio.h>
#include <unistd.h>

#define ABORT(msg)                  \
    do {                            \
        printf msg;                 \
        _exit(1);                   \
    } while (0)

#define TRACE(...) //fprintf(stderr, __VA_ARGS__)

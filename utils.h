#include <stdio.h>

#define ABORT(msg)                  \
    do {                            \
        printf msg;                 \
        exit(1);                    \
    } while (0)

#define TRACE(...) //fprintf(stderr, __VA_ARGS__)

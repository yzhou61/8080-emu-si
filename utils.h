#include <stdio.h>
#include <unistd.h>

#define ABORT(msg)                  \
    do {                            \
        printf msg;                 \
        fflush(stdout);             \
        _exit(1);                   \
    } while (0)

#define TRACE(...) printf(__VA_ARGS__)

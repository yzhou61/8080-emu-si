#include <stdio.h>
#include <stdlib.h>

#define TRACE() fprintf(stderr, "%s at %ld\n", __FUNCTION__, state->pc)

struct options_t {
    const char *bin_name;
};

struct state_t {
    long pc;
};

static void usage()
{
    fprintf(stderr, "Usage:\n");
}

static int parse_options(int argc, char **argv, struct options_t *options)
{
    if (argc < 2) {
        return 1;
    }

    options->bin_name = argv[1];
    return 0;
}

static long init_program(const char *name, unsigned char **buffer)
{
    FILE *bin;
    long size = -1;
    int succeeded = 0;

    bin = fopen(name, "rb");
    if (NULL == bin) {
        perror("fopen() failed\n");
        goto Failed;
    }

    if (0 != fseek(bin, 0, SEEK_END)) {
        fprintf(stderr, "fseek() failed\n");
        goto Failed;
    }

    size = ftell(bin);
    if (0 > size) {
        fprintf(stderr, "ftell() failed\n");
        goto Failed;
    }

    *buffer = malloc(size);
    if (NULL == *buffer) {
        fprintf(stderr, "malloc() failed\n");
        goto Failed;
    }

    rewind(bin);

    if (size != fread(*buffer, 1, size, bin)) {
        fprintf(stderr, "didn't read enough data\n");
        goto Failed;
    }

    succeeded = 1;

Failed:
    if (NULL != *buffer) {
        if (!succeeded) {
            free(*buffer);
        }
    }

    if (NULL != bin) {
        fclose(bin);
    }

    return succeeded ? size : -1;
}

static void deinit_program(unsigned char *program)
{
    free(program);
}

static void NOP(struct state_t *state)
{
    TRACE();
    ++(state->pc);
}

static void JMP(unsigned char *program, struct state_t *state)
{
    int pc;

    TRACE();
    pc = program[state->pc + 2] * 256 + program[state->pc + 1];
    fprintf(stderr, "JMP to %d\n", pc);
    state->pc = pc;
}

static int execute_one(unsigned char *program, struct state_t *state)
{
    switch(program[state->pc]) {
        case 0x00:
            NOP(state);
            return 0;
        case 0xC3:
            JMP(program, state);
            return 0;
        default:
            fprintf(stderr, "Unrecognized instruction %2x at %ld\n", program[state->pc], state->pc);
            return -1;
    }
}

static int execute(unsigned char *program, long program_size, struct state_t *state)
{
    while (state->pc < program_size) {
        int res = execute_one(program, state);

        if (res < 0) {
            return -1;
        }
    }

    return 0;
}

static void init_state(struct state_t *state)
{
    state->pc = 0;
}

static void deinit_state(struct state_t *state)
{
}

int main(int argc, char **argv)
{
    struct options_t options;
    unsigned char *program = NULL;
    long program_size;
    int res;
    struct state_t state;

    if (parse_options(argc, argv, &options) != 0) {
        fprintf(stderr, "Invalid options.\n");
        usage();
        exit(1);
    }

    program_size = init_program(options.bin_name, &program);
    if (program_size < 0) {
        fprintf(stderr, "init_program() failed\n");
        exit(1);
    }

    init_state(&state);

    res = execute(program, program_size, &state);

    deinit_state(&state);

    deinit_program(program);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>

#define MEMSIZE (1 << 16)

#define TRACE() fprintf(stderr, "==== %s(0x%02x) at 0x%04lx ", __FUNCTION__, state->program[state->pc], state->pc)

#define TRACE_STATE() fprintf(stderr, " (pc:0x%04lx,sp:0x%04x,b:0x%02x)\n", state->pc, state->sp, state->b)

struct options_t {
    const char *bin_name;
};

struct state_t {
    unsigned long pc;
    unsigned char *program;
    unsigned long program_size;
    unsigned int sp;
    unsigned int b;
    unsigned char *mem;
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

static int read_8b(struct state_t *state)
{
    int res;

    res = state->program[state->pc + 1];

    return res;
}

static int read_16b(struct state_t *state)
{
    int res;

    res = state->program[state->pc + 2] * 256 + state->program[state->pc + 1];

    return res;
}

static void NOP(struct state_t *state)
{
    TRACE();
    ++(state->pc);
}

static void JMP(struct state_t *state)
{
    TRACE();
    state->pc = read_16b(state);
}

static void LXI(struct state_t *state, int *reg)
{
    TRACE();
    *reg = read_16b(state);
    state->pc += 3;
}

static void MVI(struct state_t *state, int *reg)
{
    TRACE();
    *reg = read_8b(state);
    state->pc += 2;
}

static void CAL(struct state_t *state)
{
    int pc;

    TRACE();
    pc = read_16b(state);

}

static int execute_one(struct state_t *state)
{
    switch(state->program[state->pc]) {
        case 0x00:
            NOP(state);
            break;
        case 0x06:
            MVI(state, &(state->b));
            break;
        case 0x31:
            LXI(state, &(state->sp));
            break;
        case 0xC3:
            JMP(state);
            break;
        case 0xCD:
            CAL(state);
            break;
        default:
            fprintf(stderr, "Unrecognized instruction 0x%02x at 0x%04lx\n", state->program[state->pc], state->pc);
            return -1;
    }

    TRACE_STATE();
    return 0;
}

static int execute(struct state_t *state)
{
    while (state->pc < state->program_size) {
        int res = execute_one(state);

        if (res < 0) {
            return -1;
        }
    }

    return 0;
}

static void init_state(struct state_t *state, unsigned char *program, long program_size)
{
    state->pc = 0;
    state->program = program;
    state->program_size = program_size;
    state->sp = 0;
    state->mem = calloc(1, MEMSIZE);
}

static void deinit_state(struct state_t *state)
{
    free(state->program);
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

    init_state(&state, program, program_size);

    res = execute(&state);

    deinit_state(&state);
    return 0;
}

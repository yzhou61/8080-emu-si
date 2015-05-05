#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * Memory map:
  ROM
  $0000-$07ff:    invaders.h
  $0800-$0fff:    invaders.g
  $1000-$17ff:    invaders.f
  $1800-$1fff:    invaders.e

  RAM
  $2000-$23ff:    work RAM
  $2400-$3fff:    video RAM

  $4000-:     RAM mirror
*/

#define MEM_SIZE (0x4000)
#define ROM_START (0x0000)
#define ROM_SIZE (0x2000)
#define RAM_START ROM_SIZE
#define STACK_BOTTOM (0x2400)

#define FS (0x80)
#define FZ (0x40)
#define F0 (0)
#define FA (0x10)
//#define F0 (0)
#define FP (0x04)
#define F1 (1)
#define FC (0x01)

#define TRACE(name) fprintf(stderr, "0x%04lx\t%4s\t", state->pc, #name)
//#define TRACE(name) fprintf(stderr, "0x%04lx\t%4s(0x%02x)\t\t", state->pc, #name, state->program[state->pc])

#define TRACE_STATE() fprintf(stderr, \
                              "\t\t\t\tpc:0x%04lx,sp:0x%04x,a:0x%02x,b:0x%02x,c:0x%02x,d:0x%02x,e:0x%02x,f:"BYTETOBINARYPATTERN",h:0x%02x,l:0x%02x)\n", \
                              state->pc, state->sp, state->a, *(state->b), *(state->c), *(state->d), *(state->e), BYTETOBINARY(state->f), *(state->h), *(state->l)); \
                      print_stack(state)

#define BYTETOBINARYPATTERN "%d%d%d%d%d%d%d%d"
#define BYTETOBINARY(byte)  \
  (byte & 0x80 ? 1 : 0), \
  (byte & 0x40 ? 1 : 0), \
  (byte & 0x20 ? 1 : 0), \
  (byte & 0x10 ? 1 : 0), \
  (byte & 0x08 ? 1 : 0), \
  (byte & 0x04 ? 1 : 0), \
  (byte & 0x02 ? 1 : 0), \
  (byte & 0x01 ? 1 : 0)

struct options_t {
    const char *bin_name;
};

struct state_t {
    unsigned long pc;
    unsigned char *program;
    unsigned long program_size;
    unsigned int sp;
    unsigned int bc;
    unsigned int de;
    unsigned int hl;
    unsigned char a;
    unsigned char *b;
    unsigned char *c;
    unsigned char *d;
    unsigned char *e;
    unsigned char f;
    unsigned char *h;
    unsigned char *l;
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

static long init_program(const char *name, unsigned char *buffer)
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
    if (size > ROM_SIZE) {
        fprintf(stderr, "program size too big\n");
        goto Failed;
    }

    rewind(bin);

    if (size != fread(buffer, 1, size, bin)) {
        fprintf(stderr, "didn't read enough data\n");
        goto Failed;
    }

    succeeded = 1;

Failed:
    if (NULL != bin) {
        fclose(bin);
    }

    return succeeded ? size : -1;
}

static unsigned char read_8b(struct state_t *state)
{
    unsigned char res;

    res = state->program[state->pc + 1];

    state->pc += 2;

    fprintf(stderr, "0x%04x\t", res);

    return res;
}

static unsigned int read_16b(struct state_t *state)
{
    unsigned int res;

    res = state->program[state->pc + 2] * 256 + state->program[state->pc + 1];

    state->pc += 3;

    fprintf(stderr, "0x%04x\t", res);

    return res;
}

static void print_stack(struct state_t *state)
{
    int i;

    fprintf(stderr, "\t\t\t\t");

    for (i = STACK_BOTTOM; i >= state->sp; --i) {
        fprintf(stderr, "0x%02x ", state->mem[i]);
    }

    fprintf(stderr, "\n");
}

static unsigned char parity(unsigned char b)
{
    b = ((b & 0xAA) >> 1) + (b & 0x55);
    b = ((b & 0xCC) >> 2) + (b & 0x33);
    b = ((b & 0xF0) >> 4) + (b & 0x0F);
    return b;
}

static void NOP(struct state_t *state)
{
    ++(state->pc);
}

static void JMP(struct state_t *state)
{
    state->pc = read_16b(state);
}

static void JNZ(struct state_t *state)
{
    if ((state->f & FZ) == 0) {
        state->pc = read_16b(state);
    } else {
        state->pc += 3;
    }
}

static void LXI(struct state_t *state, unsigned int *reg)
{
    *reg = read_16b(state);
}

static void MVI(struct state_t *state, unsigned char *reg)
{
    *reg = read_8b(state);
}

static void CAL(struct state_t *state)
{
    unsigned int pc;

    pc = read_16b(state);
    state->mem[--state->sp] = (state->pc >> 8) & 0x00FF;
    state->mem[--state->sp] = state->pc & 0x00FF;

    state->pc = pc;
}

static void RET(struct state_t *state)
{
    unsigned int pc = 0;

    pc = state->mem[state->sp++];
    pc |= state->mem[state->sp++] << 8;

    state->pc = pc;
}

static void LDAX(struct state_t *state, unsigned int reg)
{
    state->a = state->mem[reg];
    ++(state->pc);
}

static void MOVM(struct state_t *state, unsigned char src)
{
    state->mem[state->hl] = src;
    ++(state->pc);
}

static void INX(struct state_t *state, unsigned int *reg)
{
    ++(*reg);
    ++(state->pc);
}

static void DCR(struct state_t *state, unsigned char *reg)
{
    unsigned char value = *reg;

    --value;

    if (0 == value) {
        state->f |= FZ;
    }

    if (value < 0x80) {
        state->f |= FS;
    }

    if ((parity(value) & 0x01) == 0) {
        state->f |= FP;
    }

    if (value == 0xFF) {
        state->f |= FC;
    }

    *reg = value;

    ++(state->pc);
}

static int execute_one(struct state_t *state)
{
    switch(state->program[state->pc]) {
        case 0x00:
            TRACE(NOP);
            NOP(state);
            break;
        case 0x05:
            TRACE(DCR);
            fprintf(stderr, "B\t");
            DCR(state, state->b);
            break;
        case 0x06:
            TRACE(MVI);
            fprintf(stderr, "B\t");
            MVI(state, state->b);
            break;
        case 0x11:
            TRACE(LXI);
            fprintf(stderr, "DE\t");
            LXI(state, &(state->de));
            break;
        case 0x13:
            TRACE(INX);
            fprintf(stderr, "DE\t");
            INX(state, &(state->de));
            break;
        case 0x1A:
            TRACE(LDAX);
            fprintf(stderr, "DE\t");
            LDAX(state, state->de);
            break;
        case 0x21:
            TRACE(LXI);
            fprintf(stderr, "HL\t");
            LXI(state, &(state->hl));
            break;
        case 0x23:
            TRACE(INX);
            fprintf(stderr, "HL\t");
            INX(state, &(state->hl));
            break;
        case 0x31:
            TRACE(LXI);
            fprintf(stderr, "SP\t");
            LXI(state, &(state->sp));
            break;
        case 0x77:
            TRACE(MOVM);
            fprintf(stderr, "A\t");
            MOVM(state, state->a);
            break;
        case 0xC2:
            TRACE(JNZ);
            JNZ(state);
            break;
        case 0xC3:
            TRACE(JMP);
            JMP(state);
            break;
        case 0xC9:
            TRACE(RET);
            RET(state);
            break;
        case 0xCD:
            TRACE(CAL);
            CAL(state);
            break;
        default:
            fprintf(stderr, "Unrecognized instruction 0x%02x at 0x%04lx\n", state->program[state->pc], state->pc);
            return -1;
    }

    fprintf(stderr, "\n");

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

static int init_state(struct state_t *state, const char *filename)
{
    state->pc = 0;
    state->sp = 0x2400;
    state->a = 0;
    state->f = 0;
    state->bc = 0;
    state->de = 0;
    state->hl = 0;
    state->b = ((unsigned char *)&(state->bc)) + 1;
    state->c = (unsigned char *)&(state->bc);
    state->d = ((unsigned char *)&(state->de)) + 1;
    state->e = (unsigned char *)&(state->de);
    state->h = ((unsigned char *)&(state->hl)) + 1;
    state->l = (unsigned char *)&(state->hl);
    posix_memalign((void **)&(state->mem), 0x1000, MEM_SIZE);
    memset(state->mem, 0xdd, MEM_SIZE);

    state->program = state->mem;
    state->program_size = init_program(filename, state->mem);
    if (0 >= state->program_size) {
        fprintf(stderr, "loading program failed\n");
        return -1;
    }

    if (0 != mprotect(state->program, ROM_SIZE, PROT_READ)) {
        perror("mprotect() failed");
        fprintf(stderr, "%p\n", state->program);
        return -1;
    }

    return 0;
}

static void deinit_state(struct state_t *state)
{
    free(state->mem);
}

int main(int argc, char **argv)
{
    struct options_t options;
    unsigned char *program = NULL;
    int res;
    struct state_t state;

    if (parse_options(argc, argv, &options) != 0) {
        fprintf(stderr, "Invalid options.\n");
        usage();
        exit(1);
    }

    if (0 > init_state(&state, options.bin_name)) {
        fprintf(stderr, "init_state() failed\n");
        exit(1);
    }

    res = execute(&state);

    deinit_state(&state);
    return 0;
}

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

#define FS  (0x80)
#define FZ  (0x40)
#define F00 (0x20)
#define FA  (0x10)
#define F01 (0x08)
#define FP  (0x04)
#define F1  (0x02)
#define FC  (0x01)

#define TRACE(...) fprintf(stderr, __VA_ARGS__)

#define TRACE_INS(name) TRACE("0x%04lx\t%4s\t", state->pc, #name)

#define TRACE_STATE() TRACE("\t\t\t\tpc\tsp\ta\tb\tc\td\te\tSZ0A0P1C\th\tl\tINTR\n"); \
                      TRACE("\t\t\t\t0x%04lx\t0x%04x\t0x%02x\t0x%02x\t0x%02x\t0x%02x\t0x%02x\t"BYTETOBINARYPATTERN"\t0x%02x\t0x%02x\t%d\n", \
                              state->pc, state->sp, state->a, *(state->b), *(state->c), *(state->d), *(state->e), BYTETOBINARY(state->f), *(state->h), *(state->l), state->intr); \
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
    unsigned char intr;
};

static int stop = 0;

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

    ++(state->pc);

    res = state->program[state->pc];

    TRACE("0x%02x\t", res);

    return res;
}

static unsigned int read_16b(struct state_t *state)
{
    unsigned int res;

    res = (state->program[state->pc + 2] << 8) + state->program[state->pc + 1];

    state->pc += 2;

    TRACE("0x%04x\t", res);

    return res;
}

static void print_stack(struct state_t *state)
{
    int i;

    TRACE("\t\t\t\t");

    for (i = STACK_BOTTOM; i >= state->sp; --i) {
        TRACE("0x%02x ", state->mem[i]);
    }

    TRACE("\n");
}

static unsigned char parity(unsigned char b)
{
    b = ((b & 0xAA) >> 1) + (b & 0x55);
    b = ((b & 0xCC) >> 2) + (b & 0x33);
    b = ((b & 0xF0) >> 4) + (b & 0x0F);
    return (b & 0x01) == 0 ? 1 : 0;
}

static void set_ZSP(struct state_t *state, unsigned char res)
{
    if (0 == res) {
        state->f |=  FZ;
    } else {
        state->f &= ~FZ;
    }

    if ((res & 0x80) != 0) {
        state->f |=  FS;
    } else {
        state->f &= ~FS;
    }

    if (parity(res)) {
        state->f |=  FP;
    } else {
        state->f &= ~FP;
    }
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
    unsigned int pc = read_16b(state);

    if ((state->f & FZ) == 0) {
        state->pc = pc;
    } else {
        ++(state->pc);
    }
}

static void LXI(struct state_t *state, unsigned int *reg)
{
    *reg = read_16b(state);
    ++(state->pc);
}

static void MVI(struct state_t *state, unsigned char *reg)
{
    *reg = read_8b(state);
    ++(state->pc);
}

static void CAL(struct state_t *state)
{
    unsigned int pc;

    pc = read_16b(state);
    ++(state->pc);
    state->mem[--state->sp] = ((state->pc >> 8) & 0x00FF);
    state->mem[--state->sp] = (state->pc & 0x00FF);

    state->pc = pc;
}

static void RET(struct state_t *state)
{
    unsigned int pc = 0;

    pc = state->mem[state->sp++];
    pc |= (state->mem[state->sp++] << 8);

    state->pc = pc;
}

static void LDA(struct state_t *state)
{
    unsigned int mem = read_16b(state);

    state->a = state->mem[mem];

    ++(state->pc);
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

static void MOVXM(struct state_t *state, unsigned char *dst)
{
    *dst = state->mem[state->hl];
    ++(state->pc);
}

static void MOV(struct state_t *state, unsigned char *dst, unsigned char src)
{
    *dst = src;
    ++(state->pc);
}

static void MVIM(struct state_t *state)
{
    unsigned char i = read_8b(state);
    state->mem[state->hl] = i;
    ++(state->pc);
}

static void INX(struct state_t *state, unsigned int *reg)
{
    ++(*reg);
    ++(state->pc);
}

static void DCR(struct state_t *state, unsigned char *reg)
{
    unsigned int ac = ((*reg) & 0x08);

    --(*reg);

    set_ZSP(state, *reg);

    if (((*reg) & 0x08) != ac) {
        state->f |=  FA;
    } else {
        state->f &= ~FA;
    }

    ++(state->pc);
}

static void CPI(struct state_t *state)
{
    unsigned char i = read_8b(state);

    if (state->a < i) {
        state->f |=  FC;
    } else {
        state->f &= ~FC;
    }

    state->a -= i;

    set_ZSP(state, state->a);

    ++(state->pc);
}

static void PUSH(struct state_t *state, unsigned int reg)
{
    state->mem[--state->sp] = ((reg >> 8) & 0x00FF);
    state->mem[--state->sp] = (reg & 0x00FF);

    ++(state->pc);
}

static void PUSHPSW(struct state_t *state)
{
    state->mem[--state->sp] = state->a;
    state->mem[--state->sp] = state->f;

    ++(state->pc);
}

static void POP(struct state_t *state, unsigned int *reg)
{
    *reg = 0;
    *reg = state->mem[state->sp++];
    *reg |= state->mem[state->sp++] << 8;

    ++(state->pc);
}

static void POPPSW(struct state_t *state)
{
    state->f = state->mem[state->sp++];
    state->a = state->mem[state->sp++];

    ++(state->pc);
}

static void DAD(struct state_t *state, unsigned int reg)
{
    unsigned int res;

    res = reg + state->hl;

    if (res > 0xFFFF) {
        state->f |=  FC;
    } else {
        state->f &= ~FC;
    }

    state->hl = (res & 0xFFFF);

    ++(state->pc);
}

static void XCHG(struct state_t *state)
{
    unsigned int tmp;

    tmp = state->hl;
    state->hl = state->de;
    state->de = tmp;

    ++(state->pc);
}

// TODO
static void OUT(struct state_t *state)
{
    read_8b(state);

    ++(state->pc);
}

static void RRC(struct state_t *state)
{
    unsigned int c = (state->a & 0x01);

    state->a >>= 1;

    state->a |= c << 7;

    state->f &= ~FC;
    state->f |= (FC * c);

    ++(state->pc);
}

static unsigned char and(struct state_t *state, unsigned char a, unsigned char b)
{
    unsigned char res = a & b;

    set_ZSP(state, res);

    state->f &= ~FC;
    state->f &= ~FA;

    return res;
}

static void ANI(struct state_t *state)
{
    unsigned char b = read_8b(state);

    state->a = and(state, state->a, b);

    ++(state->pc);
}

static void ANA(struct state_t *state, unsigned char reg)
{
    state->a = and(state, state->a, reg);

    ++(state->pc);
}

static void ADI(struct state_t *state)
{
    unsigned char b = read_8b(state);
    unsigned int ac = (state->a & 0x08);
    unsigned int sum;

    sum = state->a + b;
    state->a = (sum & 0xFF);

    set_ZSP(state, state->a);

    if (sum > 0xFF) {
        state->f |=  FC;
    } else {
        state->f &= ~FC;
    }

    if ((sum & 0x08) != ac) {
        state->f |=  FA;
    } else {
        state->f &= ~FA;
    }

    ++(state->pc);
}

static void STA(struct state_t *state)
{
    unsigned int mem = read_16b(state);
    state->mem[mem] = state->a;

    ++(state->pc);
}

static void XRA(struct state_t *state, unsigned char reg)
{
    state->a ^= reg;

    set_ZSP(state, state->a);

    state->f &= ~FC;
    state->f &= ~FA;

    ++(state->pc);
}

// TODO
static void EI(struct state_t *state)
{
    state->intr = 1;

    ++(state->pc);
}

static int execute_one(struct state_t *state)
{
    switch(state->program[state->pc]) {
        case 0x00:
            TRACE_INS("NOP");
            NOP(state);
            break;
        case 0x01:
            TRACE_INS(LXI);
            TRACE("BC\t");
            LXI(state, &(state->bc));
            break;
        case 0x03:
            TRACE_INS(INX);
            TRACE("BC\t");
            INX(state, &(state->bc));
            break;
        case 0x05:
            TRACE_INS(DCR);
            TRACE("B\t");
            DCR(state, state->b);
            break;
        case 0x06:
            TRACE_INS(MVI);
            TRACE("B\t");
            MVI(state, state->b);
            break;
        case 0x09:
            TRACE_INS(DAD);
            TRACE("BC\t");
            DAD(state, state->bc);
            break;
        case 0x0A:
            TRACE_INS(LDAX);
            TRACE("BC\t");
            LDAX(state, state->bc);
            break;
        case 0x0D:
            TRACE_INS(DCR);
            TRACE("C\t");
            DCR(state, state->c);
            break;
        case 0x0E:
            TRACE_INS(MVI);
            TRACE("C\t");
            MVI(state, state->c);
            break;
        case 0x0F:
            TRACE_INS(RRC);
            RRC(state);
            break;
        case 0x11:
            TRACE_INS(LXI);
            TRACE("DE\t");
            LXI(state, &(state->de));
            break;
        case 0x13:
            TRACE_INS(INX);
            TRACE("DE\t");
            INX(state, &(state->de));
            break;
        case 0x15:
            TRACE_INS(DCR);
            TRACE("D\t");
            DCR(state, state->d);
            break;
        case 0x16:
            TRACE_INS(MVI);
            TRACE("D\t");
            MVI(state, state->d);
            break;
        case 0x19:
            TRACE_INS(DAD);
            TRACE("DE\t");
            DAD(state, state->de);
            break;
        case 0x1A:
            TRACE_INS(LDAX);
            TRACE("DE\t");
            LDAX(state, state->de);
            break;
        case 0x1D:
            TRACE_INS(DCR);
            TRACE("E\t");
            DCR(state, state->e);
            break;
        case 0x1E:
            TRACE_INS(MVI);
            TRACE("E\t");
            MVI(state, state->e);
            break;
        case 0x21:
            TRACE_INS(LXI);
            TRACE("HL\t");
            LXI(state, &(state->hl));
            break;
        case 0x23:
            TRACE_INS(INX);
            TRACE("HL\t");
            INX(state, &(state->hl));
            break;
        case 0x26:
            TRACE_INS(MVI);
            TRACE("H\t");
            MVI(state, state->h);
            break;
        case 0x29:
            TRACE_INS(DAD);
            TRACE("HL\t");
            DAD(state, state->hl);
            break;
        case 0x2E:
            TRACE_INS(MVI);
            TRACE("L\t");
            MVI(state, state->l);
            break;
        case 0x31:
            TRACE_INS(LXI);
            TRACE("SP\t");
            LXI(state, &(state->sp));
            break;
        case 0x32:
            TRACE_INS(STA);
            STA(state);
            break;
        case 0x36:
            TRACE_INS(MVIM);
            MVIM(state);
            break;
        case 0x3A:
            TRACE_INS(LDA);
            LDA(state);
            break;
        case 0x3E:
            TRACE_INS(MVI);
            TRACE("A\t");
            MVI(state, &(state->a));
            break;
        case 0x56:
            TRACE_INS(MOVXM);
            TRACE("D\t");
            MOVXM(state, state->d);
            break;
        case 0x5E:
            TRACE_INS(MOVXM);
            TRACE("E\t");
            MOVXM(state, state->e);
            break;
        case 0x66:
            TRACE_INS(MOVXM);
            TRACE("H\t");
            MOVXM(state, state->h);
            break;
        case 0x6F:
            TRACE_INS(MOV);
            TRACE("L\tA\t");
            MOV(state, state->l, state->a);
            break;
        case 0x77:
            TRACE_INS(MOVM);
            TRACE("A\t");
            MOVM(state, state->a);
            break;
        case 0x7A:
            TRACE_INS(MOV);
            TRACE("A\tD\t");
            MOV(state, &(state->a), *state->d);
            break;
        case 0x7B:
            TRACE_INS(MOV);
            TRACE("A\tE\t");
            MOV(state, &(state->a), *state->e);
            break;
        case 0x7C:
            TRACE_INS(MOV);
            TRACE("A\tH\t");
            MOV(state, &(state->a), *state->h);
            break;
        case 0x7E:
            TRACE_INS(MOVXM);
            TRACE("A\t");
            MOVXM(state, &(state->a));
            break;
        case 0xA7:
            TRACE_INS(ANA);
            TRACE("A\t");
            ANA(state, state->a);
            break;
        case 0xAF:
            TRACE_INS(XRA);
            TRACE("A\t");
            XRA(state, state->a);
            break;
        case 0xC1:
            TRACE_INS(POP);
            TRACE("BC\t");
            POP(state, &(state->bc));
            break;
        case 0xC2:
            TRACE_INS(JNZ);
            JNZ(state);
            break;
        case 0xC3:
            TRACE_INS(JMP);
            JMP(state);
            break;
        case 0xC5:
            TRACE_INS(PUSH);
            TRACE("B\tC\t");
            PUSH(state, state->bc);
            break;
        case 0xC6:
            TRACE_INS(ADI);
            ADI(state);
            break;
        case 0xC9:
            TRACE_INS(RET);
            RET(state);
            break;
        case 0xCD:
            TRACE_INS(CAL);
            CAL(state);
            break;
        case 0xD1:
            TRACE_INS(POP);
            TRACE("DE\t");
            POP(state, &(state->de));
            break;
        case 0xD3:
            TRACE_INS(*OUT);
            OUT(state);
            break;
        case 0xD5:
            TRACE_INS(PUSH);
            TRACE("D\tE\t");
            PUSH(state, state->de);
            break;
        case 0xE1:
            TRACE_INS(POP);
            TRACE("H\tL\t");
            POP(state, &(state->hl));
            break;
        case 0xE5:
            TRACE_INS(PUSH);
            TRACE("H\tL\t");
            PUSH(state, state->hl);
            break;
        case 0xE6:
            TRACE_INS(ANI);
            ANI(state);
            break;
        case 0xEB:
            TRACE_INS(XCHG);
            TRACE("HL\tDE\t");
            XCHG(state);
            break;
        case 0xF1:
            TRACE_INS(POPPSW);
            POPPSW(state);
            break;
        case 0xF5:
            TRACE_INS(PUSHPSW);
            PUSHPSW(state);
            break;
        case 0xFB:
            TRACE_INS(EI);
            EI(state);
            break;
        case 0xFE:
            TRACE_INS(CPI);
            CPI(state);
            break;
        default:
            fprintf(stderr, "Unrecognized instruction 0x%02x at 0x%04lx\n", state->program[state->pc], state->pc);
            return -1;
    }

    TRACE("\n");

    TRACE_STATE();
    return 0;
}

static int execute(struct state_t *state)
{
    while (stop <= 0 && state->pc < state->program_size) {
        int res = execute_one(state);

        if (res < 0) {
            return -1;
        }
    }

    return 0;
}

static int init_state(struct state_t *state, const char *filename)
{
    state->intr = 0;
    state->pc = 0;
    state->sp = 0x2400;
    state->a = 0;
    state->f = F1;
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

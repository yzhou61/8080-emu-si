#include "8080e.h"
#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define MEM_SIZE (0x4000)
// TODO Allow some buffer space after the mem, to avoid segfaults
#define MIRROR_SIZE (0x1000)
#define STACK_BOTTOM (0x2400)
#define ROM_SIZE (0x2000)

#define FS  (0x80)
#define FZ  (0x40)
#define F00 (0x20)
#define FA  (0x10)
#define F01 (0x08)
#define FP  (0x04)
#define F1  (0x02)
#define FC  (0x01)

#define MEM_LOC(loc) state->mem[((loc) & 0x4000) == 0 ? (loc) : ((loc) - 0x2000)]

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

#define TRACE_INS(name) TRACE("%llu:\t0x%04lx\t%4s\t", ++count, state->pc, #name)

#define TRACE_STATE() TRACE("\t\t\t\tpc\tsp\ta\tb\tc\td\te\tSZ0A0P1C\th\tl\tINTR\tSHIFT\tSHIFT OFF\n"); \
                      TRACE("\t\t\t\t0x%04lx\t0x%04x\t0x%02x\t0x%02x\t0x%02x\t0x%02x\t0x%02x\t"BYTETOBINARYPATTERN"\t0x%02x\t0x%02x\t%d\t0x%04x\t0x%02x\n", \
                              state->pc, state->sp, state->a, *(state->b), *(state->c), *(state->d), *(state->e), BYTETOBINARY(state->f), *(state->h), *(state->l), state->intr, state->shift_reg, state->shift_reg_offset); \
                      print_stack(state)

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
    unsigned short shift_reg;
    unsigned char shift_reg_offset;
};

static struct keyboard_t *keyboard;
static int stop = -1;
static unsigned long long count = 0;
static unsigned long long total_cycles = 0;

static int cycles[][16] = { { 4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4, },
                            { 4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4, },
                            { 4, 10, 16, 5, 5, 5, 7, 4, 4, 10, 16, 5, 5, 5, 7, 4, },
                            { 4, 10, 13, 5, 10, 10, 10, 4, 4, 10, 13, 5, 5, 5, 7, 4, },
                            { 5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5, },
                            { 5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5, },
                            { 5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5, },
                            { 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 5, 5, 7, 5, },
                            { 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4, },
                            { 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4, },
                            { 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4, },
                            { 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4, },
                            { 111, 10, 10, 10, 117, 11, 7, 11, 111, 10, 10, 10, 117, 17, 7, 11, },
                            { 111, 10, 10, 10, 117, 11, 7, 11, 111, 10, 10, 10, 117, 17, 7, 11, },
                            { 111, 10, 10, 10, 117, 11, 7, 11, 111, 10, 10, 10, 117, 17, 7, 11, },
                            { 111, 10, 10, 4, 117, 11, 7, 11, 111, 5, 10, 4, 117, 17, 7, 11, }, };

static void print_stack(struct state_t *state)
{
    int i;

    TRACE("\t\t\t\t");

    for (i = STACK_BOTTOM; i >= state->sp; --i) {
        TRACE("0x%02x ", MEM_LOC(i));
    }

    TRACE("\n");
}

static long init_program(const char *name, unsigned char *buffer)
{
    FILE *bin;
    long size = -1;

    bin = fopen(name, "rb");
    if (NULL == bin) {
        ABORT(("fopen() failed\n"));
    }

    if (0 != fseek(bin, 0, SEEK_END)) {
        ABORT(("fseek() failed\n"));
    }

    size = ftell(bin);
    if (0 > size) {
        ABORT(("ftell() failed\n"));
    }
    if (size > ROM_SIZE) {
        ABORT(("program size too big\n"));
    }

    rewind(bin);

    if (size != fread(buffer, 1, size, bin)) {
        ABORT(("didn't read enough data\n"));
    }

    fclose(bin);

    return size;
}

struct cpu_mem_t *init_machine(const char *bin_name, struct keyboard_t *k)
{
    struct cpu_mem_t *res;
    struct state_t *state;

    res = (struct cpu_mem_t *)malloc(sizeof(struct cpu_mem_t));
    if (res == NULL) {
        ABORT(("OOM\n"));
    }

    res->state = malloc(sizeof(struct state_t));
    if (res->state == NULL) {
        ABORT(("OOM\n"));
    }
    state = res->state;

    state->intr = 0;
    state->shift_reg = 0;
    state->shift_reg_offset = 0;
    state->pc = 0;
    state->sp = STACK_BOTTOM;
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
    posix_memalign((void **)&(state->mem), 0x1000, MEM_SIZE + MIRROR_SIZE);
    memset(state->mem, 0xdd, MEM_SIZE + MIRROR_SIZE);

    state->program = state->mem;
    state->program_size = init_program(bin_name, state->mem);
    if (0 >= state->program_size) {
        ABORT(("loading program failed\n"));
    }

    if (0 != mprotect(state->program, ROM_SIZE, PROT_READ)) {
        perror("mprotect() failed");
        ABORT(("%p\n", state->program));
    }

    if (0 != mprotect(state->mem + MEM_SIZE, MIRROR_SIZE, PROT_NONE)) {
        perror("mprotect() failed");
        ABORT(("%p\n", state->program));
    }

    res->mem = state->mem;

    keyboard = k;
    memset(keyboard, 0, sizeof(*keyboard));

    return res;
}

void deinit_machine(struct cpu_mem_t *machine)
{
    struct state_t *state = (struct state_t *)machine->state;
    mprotect(state->program, ROM_SIZE, PROT_READ | PROT_WRITE);
    free(state->mem);
    free(state);
    free(machine);
}

static unsigned char read_8b(struct state_t *state)
{
    unsigned char res;

    ++(state->pc);

    res = MEM_LOC(state->pc);

    TRACE("0x%02x\t", res);

    return res;
}

static unsigned int read_16b(struct state_t *state)
{
    unsigned int res;

    res = (MEM_LOC(state->pc + 2) << 8) + MEM_LOC(state->pc + 1);

    state->pc += 2;

    TRACE("0x%04x\t", res);

    return res;
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

static unsigned char *get_reg(struct state_t *state, unsigned char encoding)
{
    switch (encoding) {
        case 0x00:
            TRACE("B\t");
            return state->b;
        case 0x01:
            TRACE("C\t");
            return state->c;
        case 0x02:
            TRACE("D\t");
            return state->d;
        case 0x03:
            TRACE("E\t");
            return state->e;
        case 0x04:
            TRACE("H\t");
            return state->h;
        case 0x05:
            TRACE("L\t");
            return state->l;
        case 0x06:
            TRACE("M\t");
            return &(MEM_LOC(state->hl));
        case 0x07:
            TRACE("A\t");
            return &(state->a);
        default:
            ABORT(("Invalid register encoding\n"));
    }

    return NULL;
}

static unsigned int *get_dreg(struct state_t *state, unsigned char encoding)
{
    switch (encoding) {
        case 0x00:
            TRACE("BC\t");
            return &(state->bc);
        case 0x01:
            TRACE("DE\t");
            return &(state->de);
        case 0x02:
            TRACE("HL\t");
            return &(state->hl);
        case 0x03:
            TRACE("SP\t");
            return &(state->sp);
        default:
            ABORT(("Invalid register encoding\n"));
    }

    return NULL;
}

static unsigned char get_cond(struct state_t *state, unsigned char encoding)
{
    switch (encoding) {
        case 0x00:
            TRACE("NZ\t");
            return ((state->f & FZ) == 0);
        case 0x01:
            TRACE("Z\t");
            return ((state->f & FZ) != 0);
        case 0x02:
            TRACE("NC\t");
            return ((state->f & FC) == 0);
        case 0x03:
            TRACE("C\t");
            return ((state->f & FC) != 0);
        case 0x04:
            TRACE("PO\t");
            return ((state->f & FP) == 0);
        case 0x05:
            TRACE("PE\t");
            return ((state->f & FP) != 0);
        case 0x06:
            TRACE("P\t");
            return ((state->f & FS) == 0);
        case 0x07:
            TRACE("M\t");
            return ((state->f & FS) != 0);
        default:
            ABORT(("Invalid condition encoding\n"));
    }

    return 0;
}

static void set_A(struct state_t *state, unsigned char before, unsigned char after)
{
    if ((before & 0x08) != (after & 0x08)) {
        state->f |= FA;
    } else {
        state->f &= ~FA;
    }
}

static int NOP(struct state_t *state)
{
    ++(state->pc);
    return 1;
}

static void jump_if(struct state_t *state, unsigned char c)
{
    unsigned int pc = read_16b(state);

    if (c != 0) {
        state->pc = pc;
    } else {
        ++(state->pc);
    }
}

static int PCHL(struct state_t *state)
{
    state->pc = state->hl;
    return 1;
}

static int JMP(struct state_t *state)
{
    jump_if(state, 1);
    return 1;
}

static int JNZ(struct state_t *state)
{
    jump_if(state, (state->f & FZ) == 0);
    return 1;
}

static int JNC(struct state_t *state)
{
    jump_if(state, (state->f & FC) == 0);
    return 1;
}

static int JC(struct state_t *state)
{
    jump_if(state, (state->f & FC) > 0);
    return 1;
}

static int JZ(struct state_t *state)
{
    jump_if(state, (state->f & FZ) > 0);
    return 1;
}

static int JM(struct state_t *state)
{
    jump_if(state, (state->f & FS) > 0);
    return 1;
}

static int LXI(struct state_t *state, unsigned int *reg)
{
    *reg = read_16b(state);
    ++(state->pc);
    return 1;
}

static int MVI(struct state_t *state, unsigned char *reg)
{
    *reg = read_8b(state);
    ++(state->pc);
    return 1;
}

static void call_if(struct state_t *state, unsigned char c)
{
    unsigned int pc = read_16b(state);

    ++(state->pc);

    if (c != 0) {
        --state->sp; MEM_LOC(state->sp) = ((state->pc >> 8) & 0x00FF);
        --state->sp; MEM_LOC(state->sp) = (state->pc & 0x00FF);

        state->pc = pc;
    }
}

static int CX(struct state_t *state, unsigned char instr)
{
    unsigned char cond = (instr & 0x38);
    unsigned char taken = get_cond(state, cond >> 3);
    call_if(state, taken);
    return taken;
}

static int CAL(struct state_t *state)
{
    call_if(state, 1);
    return 1;
}

static void return_if(struct state_t *state, unsigned char c)
{
    if (c != 0) {
        unsigned int pc = 0;

        pc = MEM_LOC(state->sp); ++state->sp;
        pc |= (MEM_LOC(state->sp) << 8); ++state->sp;

        state->pc = pc;
    } else {
        ++(state->pc);
    }
}

static int RET(struct state_t *state)
{
    return_if(state, 1);
    return 1;
}

static int RZ(struct state_t *state)
{
    int z = ((state->f & FZ) > 0);
    return_if(state, z);
    return z;
}

static int RC(struct state_t *state)
{
    int c = ((state->f & FC) > 0);
    return_if(state, c);
    return c;
}

static int RNZ(struct state_t *state)
{
    int nz = ((state->f & FZ) == 0);
    return_if(state, nz);
    return nz;
}

static int RNC(struct state_t *state)
{
    int c = ((state->f & FC) == 0);
    return_if(state, c);
    return c;
}

static int LDA(struct state_t *state)
{
    unsigned int mem = read_16b(state);

    state->a = MEM_LOC(mem);

    ++(state->pc);
    return 1;
}

static int LHLD(struct state_t *state)
{
    unsigned int mem = read_16b(state);

    *state->l = MEM_LOC(mem);
    *state->h = MEM_LOC(mem + 1);

    ++(state->pc);
    return 1;
}

static int SHLD(struct state_t *state)
{
    unsigned int mem = read_16b(state);

    MEM_LOC(mem) = *state->l;
    MEM_LOC(mem + 1) = *state->h;

    ++(state->pc);
    return 1;
}

static int LDAX(struct state_t *state, unsigned int reg)
{
    state->a = MEM_LOC(reg);
    ++(state->pc);
    return 1;
}

static int MOV(struct state_t *state, unsigned char instr)
{
    unsigned char dst, src;

    dst = ((instr & 0x38) >> 3);
    src = (instr & 0x07);

    *get_reg(state, dst) = *get_reg(state, src);
    ++(state->pc);
    return 1;
}

static unsigned char add(struct state_t *state, unsigned int a, unsigned int b)
{
    unsigned char res = a + b;

    if (a + b > 0xFF) {
        state->f |= FC;
    } else {
        state->f &= ~FC;
    }

    set_A(state, a, res);
    set_ZSP(state, res);

    return res;
}

static unsigned char sub(struct state_t *state, unsigned int a, unsigned int b)
{
    unsigned char res = a - b;

    if (a < b) {
        state->f |= FC;
    } else {
        state->f &= ~FC;
    }

    set_A(state, a, res);
    set_ZSP(state, res);

    return res;
}

static int ADD(struct state_t *state, unsigned char instr, int carry)
{
    unsigned char src;

    src = (instr & 0x07);

    state->a = add(state, state->a, (int)*get_reg(state, src) + (carry ? 1 : 0));

    ++(state->pc);
    return 1;
}

static int SUB(struct state_t *state, unsigned char instr, int carry)
{
    unsigned char src;

    src = (instr & 0x07);

    state->a = sub(state, state->a, (int)*get_reg(state, src) + (carry ? 1 : 0));

    ++(state->pc);
    return 1;
}

static int SBI(struct state_t *state)
{
    state->a = sub(state, state->a, read_8b(state) + 1);

    ++(state->pc);
    return 1;
}

static int MVIM(struct state_t *state)
{
    unsigned char i = read_8b(state);
    MEM_LOC(state->hl) = i;
    ++(state->pc);
    return 1;
}

static int INX(struct state_t *state, unsigned char instr)
{
    unsigned char dreg = (instr & 0x30) >> 4;

    ++(*get_dreg(state, dreg));

    ++(state->pc);
    return 1;
}

static int DCX(struct state_t *state, unsigned int *reg)
{
    --(*reg);
    ++(state->pc);
    return 1;
}

static unsigned char inc_dec(struct state_t *state, unsigned char i, int p)
{
    unsigned char res;

    res = i + p;

    set_A(state, i, res);
    set_ZSP(state, res);

    return res;
}

static int INR(struct state_t *state, unsigned char instr, int inc)
{
    unsigned char enc = (instr & 0x38);
    unsigned char *reg = get_reg(state, enc >> 3);

    *reg = inc_dec(state, *reg, (inc ? 1 : -1));

    ++(state->pc);
    return 1;
}

static int CPI(struct state_t *state)
{
    unsigned char i = read_8b(state);

    sub(state, state->a, i);

    ++(state->pc);
    return 1;
}

static int PUSH(struct state_t *state, unsigned int reg)
{
    --state->sp; MEM_LOC(state->sp) = ((reg >> 8) & 0x00FF);
    --state->sp; MEM_LOC(state->sp) = (reg & 0x00FF);

    ++(state->pc);
    return 1;
}

static int PUSHPSW(struct state_t *state)
{
    --state->sp; MEM_LOC(state->sp) = state->a;
    --state->sp; MEM_LOC(state->sp) = state->f;

    ++(state->pc);
    return 1;
}

static int POP(struct state_t *state, unsigned int *reg)
{
    *reg = 0;
    *reg = MEM_LOC(state->sp); ++state->sp;
    *reg |= MEM_LOC(state->sp) << 8; ++state->sp;

    ++(state->pc);
    return 1;
}

static int POPPSW(struct state_t *state)
{
    state->f = MEM_LOC(state->sp); ++state->sp;
    state->a = MEM_LOC(state->sp); ++state->sp;

    ++(state->pc);
    return 1;
}

static int XTHL(struct state_t *state)
{
    unsigned char t;

    t = *state->l;
    *state->l = MEM_LOC(state->sp);
    MEM_LOC(state->sp) = t;

    t = *state->h;
    *state->h = MEM_LOC(state->sp + 1);
    MEM_LOC(state->sp + 1) = t;

    ++(state->pc);
    return 1;
}

static int DAD(struct state_t *state, unsigned int reg)
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
    return 1;
}

static int XCHG(struct state_t *state)
{
    unsigned int tmp;

    tmp = state->hl;
    state->hl = state->de;
    state->de = tmp;

    ++(state->pc);
    return 1;
}

// TODO
static int IN(struct state_t *state)
{
    unsigned char i = read_8b(state);

    switch (i) {
        case 1:
            state->a = (keyboard->p1_start << 2);
            break;
        case 3:
            state->a = ((state->shift_reg >> (8 - state->shift_reg_offset)) & 0xFF);
            break;
        default:
            state->a = 0;
            break;
    }

    ++(state->pc);
    return 1;
}

// TODO
static int OUT(struct state_t *state)
{
    unsigned char i = read_8b(state);

    switch (i) {
        case 2:
            state->shift_reg_offset = (state->a & 0x07);
            break;
        case 4:
            state->shift_reg >>= 8;
            state->shift_reg |= ((int)state->a << 8);
            break;
        default:
            break;
    }

    ++(state->pc);
    return 1;
}

static int RLC(struct state_t *state)
{
    unsigned int c = (state->a & 0x80);

    state->a <<= 1;

    state->a |= c >> 7;

    state->f &= ~FC;
    state->f |= (FC * c);

    ++(state->pc);
    return 1;
}

static int RRC(struct state_t *state)
{
    unsigned int c = (state->a & 0x01);

    state->a >>= 1;

    state->a |= c << 7;

    state->f &= ~FC;
    state->f |= (FC * c);

    ++(state->pc);
    return 1;
}

static int RAR(struct state_t *state)
{
    unsigned int c = (state->a & 0x01);

    state->a >>= 1;

    state->a |= ((state->f & FC) != 0) << 7;

    state->f &= ~FC;
    state->f |= (FC * c);

    ++(state->pc);
    return 1;
}

static unsigned char and(struct state_t *state, unsigned char a, unsigned char b)
{
    unsigned char res = a & b;

    set_A(state, a, res);
    set_ZSP(state, res);

    state->f &= ~FC;

    return res;
}

static unsigned char or(struct state_t *state, unsigned char a, unsigned char b)
{
    unsigned char res = a | b;

    set_ZSP(state, res);

    state->f &= ~FC;
    state->f &= ~FA;

    return res;
}

static unsigned char xor(struct state_t *state, unsigned char a, unsigned char b)
{
    unsigned char res = a ^ b;

    set_ZSP(state, res);

    state->f &= ~FC;
    state->f &= ~FA;

    return res;
}

static int ANI(struct state_t *state)
{
    unsigned char b = read_8b(state);

    state->a = and(state, state->a, b);

    ++(state->pc);
    return 1;
}

static int ANA(struct state_t *state, unsigned char instr)
{
    unsigned char src;

    src = (instr & 0x07);

    state->a = and(state, state->a, *get_reg(state, src));

    ++(state->pc);
    return 1;
}

static int ORA(struct state_t *state, unsigned char instr)
{
    unsigned char src;

    src = (instr & 0x07);

    state->a = or(state, state->a, *get_reg(state, src));

    ++(state->pc);
    return 1;
}

static int CMP(struct state_t *state, unsigned char instr)
{
    unsigned char src;

    src = (instr & 0x07);

    sub(state, state->a, *get_reg(state, src));

    ++(state->pc);
    return 1;
}

static int ORI(struct state_t *state)
{
    unsigned char i = read_8b(state);

    state->a = or(state, state->a, i);

    ++(state->pc);
    return 1;
}

static int CMA(struct state_t *state)
{
    state->a = ~(state->a);

    ++(state->pc);
    return 1;
}

static int ADI(struct state_t *state)
{
    unsigned char b = read_8b(state);
    unsigned int sum;

    sum = state->a + b;
    set_A(state, state->a, sum);

    state->a = (sum & 0xFF);
    set_ZSP(state, state->a);

    if (sum > 0xFF) {
        state->f |=  FC;
    } else {
        state->f &= ~FC;
    }

    ++(state->pc);
    return 1;
}

static int SUI(struct state_t *state)
{
    unsigned char b = read_8b(state);
    unsigned int sum;

    if (state->a < b) {
        state->f |=  FC;
    } else {
        state->f &= ~FC;
    }

    sum = state->a - b;
    set_A(state, state->a, sum);

    state->a = (sum & 0xFF);
    set_ZSP(state, state->a);

    ++(state->pc);
    return 1;
}

static int STA(struct state_t *state)
{
    unsigned int mem = read_16b(state);
    MEM_LOC(mem) = state->a;

    ++(state->pc);
    return 1;
}

static int XRA(struct state_t *state, unsigned char instr)
{
    unsigned char src;

    src = (instr & 0x07);

    state->a = xor(state, state->a, *get_reg(state, src));

    ++(state->pc);
    return 1;
}

static int EI(struct state_t *state)
{
    state->intr = 1;

    ++(state->pc);
    return 1;
}

static int STC(struct state_t *state)
{
    state->f ^= FC;
    ++(state->pc);
    return 1;
}

static int DAA(struct state_t *state)
{
    if ((state->a & 0x0F) > 9 || (state->f & FA)) {
        state->a = add(state, state->a, 0x06);
    }

    if ((state->a & 0xF0) > 9 || (state->f & FC)) {
        state->a = add(state, state->a, 0x60);
    }

    ++(state->pc);
    return 1;
}

static int execute_one(struct state_t *state)
{
    unsigned char instruction;
    int cycle;
    int taken;

    if (state->pc > state->program_size) {
        ABORT(("pc out of bound! %lu\n", state->pc));
    }

    instruction = state->program[state->pc];

    if ((instruction & 0xC0) == 0x40) {
    // 01XX XXXX
        if (instruction == 0x76) {
            // HLT
            exit(0);
        }

        TRACE_INS(MOV);
        taken = MOV(state, instruction);
    } else if ((instruction & 0xF8) == 0x80) {
    // 1000 0XXX
        TRACE_INS(ADD);
        taken = ADD(state, instruction, 0);
    } else if ((instruction & 0xF8) == 0x88) {
    // 1000 1XXX
        TRACE_INS(ADC);
        taken = ADD(state, instruction, 1);
    } else if ((instruction & 0xF8) == 0x90) {
    // 1001 0XXX
        TRACE_INS(SUB);
        taken = SUB(state, instruction, 0);
    } else if ((instruction & 0xF8) == 0x98) {
    // 1001 1XXX
        TRACE_INS(SBB);
        taken = SUB(state, instruction, 1);
    } else if ((instruction & 0xF8) == 0xA0) {
    // 1010 0XXX
        TRACE_INS(ANA);
        taken = ANA(state, instruction);
    } else if ((instruction & 0xF8) == 0xA8) {
    // 1010 1XXX
        TRACE_INS(XRA);
        taken = XRA(state, instruction);
    } else if ((instruction & 0xF8) == 0xB0) {
    // 1011 0XXX
        TRACE_INS(ORA);
        taken = ORA(state, instruction);
    } else if ((instruction & 0xF8) == 0xB8) {
    // 1011 1XXX
        TRACE_INS(CMP);
        taken = CMP(state, instruction);
    } else if ((instruction & 0xCF) == 0x03) {
    // 00XX 0011
        TRACE_INS(INX);
        taken = INX(state, instruction);
    } else if ((instruction & 0xC7) == 0x04) {
    // 00XX X100
        TRACE_INS(INR);
        taken = INR(state, instruction, 1);
    } else if ((instruction & 0xC7) == 0x05) {
    // 00XX X101
        TRACE_INS(DCR);
        taken = INR(state, instruction, 0);
    } else if ((instruction & 0xC7) == 0xC4) {
    // 11XX X100
        TRACE_INS(CX);
        taken = CX(state, instruction);
    } else {
        switch(instruction) {
            case 0x00:
                TRACE_INS(NOP);
                taken = NOP(state);
                break;
            case 0x01:
                TRACE_INS(LXI);
                TRACE("BC\t");
                taken = LXI(state, &(state->bc));
                break;
            case 0x06:
                TRACE_INS(MVI);
                TRACE("B\t");
                taken = MVI(state, state->b);
                break;
            case 0x07:
                TRACE_INS(RLC);
                taken = RLC(state);
                break;
            case 0x09:
                TRACE_INS(DAD);
                TRACE("BC\t");
                taken = DAD(state, state->bc);
                break;
            case 0x0A:
                TRACE_INS(LDAX);
                TRACE("BC\t");
                taken = LDAX(state, state->bc);
                break;
            case 0x0E:
                TRACE_INS(MVI);
                TRACE("C\t");
                taken = MVI(state, state->c);
                break;
            case 0x0F:
                TRACE_INS(RRC);
                taken = RRC(state);
                break;
            case 0x11:
                TRACE_INS(LXI);
                TRACE("DE\t");
                taken = LXI(state, &(state->de));
                break;
            case 0x16:
                TRACE_INS(MVI);
                TRACE("D\t");
                taken = MVI(state, state->d);
                break;
            case 0x19:
                TRACE_INS(DAD);
                TRACE("DE\t");
                taken = DAD(state, state->de);
                break;
            case 0x1A:
                TRACE_INS(LDAX);
                TRACE("DE\t");
                taken = LDAX(state, state->de);
                break;
            case 0x1E:
                TRACE_INS(MVI);
                TRACE("E\t");
                taken = MVI(state, state->e);
                break;
            case 0x1F:
                TRACE_INS(RAR);
                taken = RAR(state);
                break;
            case 0x21:
                TRACE_INS(LXI);
                TRACE("HL\t");
                taken = LXI(state, &(state->hl));
                break;
            case 0x22:
                TRACE_INS(SHLD);
                taken = SHLD(state);
                break;
            case 0x26:
                TRACE_INS(MVI);
                TRACE("H\t");
                taken = MVI(state, state->h);
                break;
            case 0x27:
                TRACE_INS(DAA);
                taken = DAA(state);
                break;
            case 0x29:
                TRACE_INS(DAD);
                TRACE("HL\t");
                taken = DAD(state, state->hl);
                break;
            case 0x2A:
                TRACE_INS(LHLD);
                taken = LHLD(state);
                break;
            case 0x2B:
                TRACE_INS(DCX);
                TRACE("HL\t");
                taken = DCX(state, &(state->hl));
                break;
            case 0x2E:
                TRACE_INS(MVI);
                TRACE("L\t");
                taken = MVI(state, state->l);
                break;
            case 0x2F:
                TRACE_INS(CMA);
                taken = CMA(state);
                break;
            case 0x31:
                TRACE_INS(LXI);
                TRACE("SP\t");
                taken = LXI(state, &(state->sp));
                break;
            case 0x32:
                TRACE_INS(STA);
                taken = STA(state);
                break;
            case 0x36:
                TRACE_INS(MVIM);
                taken = MVIM(state);
                break;
            case 0x37:
                TRACE_INS(STC);
                taken = STC(state);
                break;
            case 0x3A:
                TRACE_INS(LDA);
                taken = LDA(state);
                break;
            case 0x3E:
                TRACE_INS(MVI);
                TRACE("A\t");
                taken = MVI(state, &(state->a));
                break;
            case 0xC0:
                TRACE_INS(RNZ);
                taken = RNZ(state);
                break;
            case 0xC1:
                TRACE_INS(POP);
                TRACE("BC\t");
                taken = POP(state, &(state->bc));
                break;
            case 0xC2:
                TRACE_INS(JNZ);
                taken = JNZ(state);
                break;
            case 0xC3:
                TRACE_INS(JMP);
                taken = JMP(state);
                break;
            case 0xC5:
                TRACE_INS(PUSH);
                TRACE("B\tC\t");
                taken = PUSH(state, state->bc);
                break;
            case 0xC6:
                TRACE_INS(ADI);
                taken = ADI(state);
                break;
            case 0xC8:
                TRACE_INS(RZ);
                taken = RZ(state);
                break;
            case 0xC9:
                TRACE_INS(RET);
                taken = RET(state);
                break;
            case 0xCA:
                TRACE_INS(JZ);
                taken = JZ(state);
                break;
            case 0xCD:
                TRACE_INS(CAL);
                taken = CAL(state);
                break;
            case 0xD0:
                TRACE_INS(RNC);
                taken = RNC(state);
                break;
            case 0xD1:
                TRACE_INS(POP);
                TRACE("DE\t");
                taken = POP(state, &(state->de));
                break;
            case 0xD2:
                TRACE_INS(JNC);
                taken = JNC(state);
                break;
            case 0xD3:
                TRACE_INS(OUT);
                taken = OUT(state);
                break;
            case 0xD5:
                TRACE_INS(PUSH);
                TRACE("D\tE\t");
                taken = PUSH(state, state->de);
                break;
            case 0xD6:
                TRACE_INS(SUI);
                taken = SUI(state);
                break;
            case 0xD8:
                TRACE_INS(RC);
                taken = RC(state);
                break;
            case 0xDA:
                TRACE_INS(JC);
                taken = JC(state);
                break;
            case 0xDB:
                TRACE_INS(IN);
                taken = IN(state);
                break;
            case 0xDE:
                TRACE_INS(SBI);
                taken = SBI(state);
                break;
            case 0xE1:
                TRACE_INS(POP);
                TRACE("H\tL\t");
                taken = POP(state, &(state->hl));
                break;
            case 0xE3:
                TRACE_INS(XTHL);
                taken = XTHL(state);
                break;
            case 0xE5:
                TRACE_INS(PUSH);
                TRACE("H\tL\t");
                taken = PUSH(state, state->hl);
                break;
            case 0xE6:
                TRACE_INS(ANI);
                taken = ANI(state);
                break;
            case 0xE9:
                TRACE_INS(PCHL);
                taken = PCHL(state);
                break;
            case 0xEB:
                TRACE_INS(XCHG);
                TRACE("HL\tDE\t");
                taken = XCHG(state);
                break;
            case 0xF1:
                TRACE_INS(POPPSW);
                taken = POPPSW(state);
                break;
            case 0xF5:
                TRACE_INS(PUSHPSW);
                taken = PUSHPSW(state);
                break;
            case 0xF6:
                TRACE_INS(ORI);
                taken = ORI(state);
                break;
            case 0xFA:
                TRACE_INS(JM);
                taken = JM(state);
                break;
            case 0xFB:
                TRACE_INS(EI);
                taken = EI(state);
                break;
            case 0xFE:
                TRACE_INS(CPI);
                taken = CPI(state);
                break;
            case 0x08:
            case 0x10:
            case 0x18:
            case 0x20:
            case 0x28:
            case 0x30:
            case 0x38:
            case 0xCB:
            case 0xD9:
            case 0xDD:
            case 0xED:
            case 0xFD:
                ABORT(("Alternative opcode used (0x%02x). Aborting...\n", instruction));
            default:
                ABORT(("Unrecognized instruction 0x%02x at 0x%04lx\n", state->program[state->pc], state->pc));
        }
    }

    cycle = cycles[instruction >> 4][instruction & 0x0F];
    if (cycle > 100) {
        if (taken) {
            cycle -= 100;
        } else {
            cycle -= 106;
        }
    }
    if (cycle <= 0 || cycle >= 20) {
        ABORT(("cycle number incorrect!\n"));
    }
    total_cycles += cycle;
    TRACE("cycle: %d/%llu\n", cycle, total_cycles);

    TRACE_STATE();

    if (stop == 0) {
        exit(1);
    }
    return cycle;
}

void generate_intr(struct cpu_mem_t *machine, int intr_num)
{
    struct state_t *state = (struct state_t *)machine->state;

    if (!state->intr) {
        return;
    }

    --state->sp; MEM_LOC(state->sp) = ((state->pc >> 8) & 0x00FF);
    --state->sp; MEM_LOC(state->sp) = (state->pc & 0x00FF);

    state->pc = 0x08 * intr_num;
}

void execute(struct cpu_mem_t *machine, int cycles)
{
    int cycle = 0;
    struct state_t *state = (struct state_t *)machine->state;

    while (cycle < cycles) {
        cycle += execute_one(state);
    }
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <GL/gl.h>
#include <GL/glut.h>
#include <time.h>

#define MEM_SIZE (0x4000)
#define ROM_START (0x0000)
#define ROM_SIZE (0x2000)
#define RAM_START ROM_SIZE
#define STACK_BOTTOM (0x2400)
#define DISPLAY_ADDRESS (0x2400)

#define FS  (0x80)
#define FZ  (0x40)
#define F00 (0x20)
#define FA  (0x10)
#define F01 (0x08)
#define FP  (0x04)
#define F1  (0x02)
#define FC  (0x01)

/*
 * The display is 256*224 in portrait mode at 59.94Hz
 * Monochrome, one bit per pixel, 32B per scan line.
 *
 * According to NTSC, among the 262 scan lines, 224 is used
 * and the rest is vblank. It generates interrupt (1)
 * before vblank and interrupt (2) after it. Combining this
 * with the 2MHz frequency of the 8080, this gives us:
 *
 * ns per frame: 1e9/59.94 ~= 16683350ns
 * cycles per frame: (1/59.94) / (1/2M) ~= 33367
 * cycles before vblank: 33367 * (224/262) ~= 28527
 */
#define DISPLAY_WIDTH  (224)
#define DISPLAY_HEIGHT (256)
#define CYCLES_BEFORE_VBLANK (28527)
#define CYCLES_AFTER_VBLANK (4839)
#define NS_PER_FRAME (16683350)

#define TRACE(...) //fprintf(stderr, __VA_ARGS__)

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

#define ABORT(msg) \
    do {                            \
        printf msg;                 \
        exit(1);                    \
    } while (0)

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

static unsigned long long get_ns()
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void usage()
{
    int i, j;
    fprintf(stderr, "Usage: a.out <ROM FILE>\n");
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

static int CAL(struct state_t *state)
{
    unsigned int pc;

    pc = read_16b(state);
    ++(state->pc);
    state->mem[--state->sp] = ((state->pc >> 8) & 0x00FF);
    state->mem[--state->sp] = (state->pc & 0x00FF);

    state->pc = pc;
    return 1;
}

static void return_if(struct state_t *state, unsigned char c)
{
    if (c != 0) {
        unsigned int pc = 0;

        pc = state->mem[state->sp++];
        pc |= (state->mem[state->sp++] << 8);

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

static int LDA(struct state_t *state)
{
    unsigned int mem = read_16b(state);

    state->a = state->mem[mem];

    ++(state->pc);
    return 1;
}

static int LDAX(struct state_t *state, unsigned int reg)
{
    state->a = state->mem[reg];
    ++(state->pc);
    return 1;
}

static int MOVM(struct state_t *state, unsigned char src)
{
    state->mem[state->hl] = src;
    ++(state->pc);
    return 1;
}

static int MOVXM(struct state_t *state, unsigned char *dst)
{
    *dst = state->mem[state->hl];
    ++(state->pc);
    return 1;
}

static int MOV(struct state_t *state, unsigned char *dst, unsigned char src)
{
    *dst = src;
    ++(state->pc);
    return 1;
}

static int MVIM(struct state_t *state)
{
    unsigned char i = read_8b(state);
    state->mem[state->hl] = i;
    ++(state->pc);
    return 1;
}

static int INX(struct state_t *state, unsigned int *reg)
{
    ++(*reg);
    ++(state->pc);
    return 1;
}

static unsigned char decrement(struct state_t *state, unsigned char i)
{
    unsigned int ac = (i & 0x08);

    --i;

    set_ZSP(state, i);

    if ((i & 0x08) != ac) {
        state->f |=  FA;
    } else {
        state->f &= ~FA;
    }

    return i;
}

static int DCR_M(struct state_t *state)
{
    state->mem[state->hl] = decrement(state, state->mem[state->hl]);
    ++(state->pc);
    return 1;
}

static int DCR(struct state_t *state, unsigned char *reg)
{
    *reg = decrement(state, *reg);
    ++(state->pc);
    return 1;
}

static int CPI(struct state_t *state)
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
    return 1;
}

static int PUSH(struct state_t *state, unsigned int reg)
{
    state->mem[--state->sp] = ((reg >> 8) & 0x00FF);
    state->mem[--state->sp] = (reg & 0x00FF);

    ++(state->pc);
    return 1;
}

static int PUSHPSW(struct state_t *state)
{
    state->mem[--state->sp] = state->a;
    state->mem[--state->sp] = state->f;

    ++(state->pc);
    return 1;
}

static int POP(struct state_t *state, unsigned int *reg)
{
    *reg = 0;
    *reg = state->mem[state->sp++];
    *reg |= state->mem[state->sp++] << 8;

    ++(state->pc);
    return 1;
}

static int POPPSW(struct state_t *state)
{
    state->f = state->mem[state->sp++];
    state->a = state->mem[state->sp++];

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
    read_8b(state);

    ++(state->pc);
    return 1;
}

// TODO
static int OUT(struct state_t *state)
{
    read_8b(state);

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

static unsigned char and(struct state_t *state, unsigned char a, unsigned char b)
{
    unsigned char res = a & b;

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

static int ANA(struct state_t *state, unsigned char reg)
{
    state->a = and(state, state->a, reg);

    ++(state->pc);
    return 1;
}

static int ADI(struct state_t *state)
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
    return 1;
}

static int STA(struct state_t *state)
{
    unsigned int mem = read_16b(state);
    state->mem[mem] = state->a;

    ++(state->pc);
    return 1;
}

static int XRA(struct state_t *state, unsigned char reg)
{
    state->a ^= reg;

    set_ZSP(state, state->a);

    state->f &= ~FC;
    state->f &= ~FA;

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

static int ORA(struct state_t *state)
{

}

static int execute_one(struct state_t *state)
{
    unsigned char instruction;
    int cycle;
    int taken;

    if (state->pc > state->program_size) {
        ABORT(("pc out of bound!\n"));
    }

    instruction = state->program[state->pc];

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
        case 0x03:
            TRACE_INS(INX);
            TRACE("BC\t");
            taken = INX(state, &(state->bc));
            break;
        case 0x05:
            TRACE_INS(DCR);
            TRACE("B\t");
            taken = DCR(state, state->b);
            break;
        case 0x06:
            TRACE_INS(MVI);
            TRACE("B\t");
            taken = MVI(state, state->b);
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
        case 0x0D:
            TRACE_INS(DCR);
            TRACE("C\t");
            taken = DCR(state, state->c);
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
        case 0x13:
            TRACE_INS(INX);
            TRACE("DE\t");
            taken = INX(state, &(state->de));
            break;
        case 0x15:
            TRACE_INS(DCR);
            TRACE("D\t");
            taken = DCR(state, state->d);
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
        case 0x1D:
            TRACE_INS(DCR);
            TRACE("E\t");
            taken = DCR(state, state->e);
            break;
        case 0x1E:
            TRACE_INS(MVI);
            TRACE("E\t");
            taken = MVI(state, state->e);
            break;
        case 0x21:
            TRACE_INS(LXI);
            TRACE("HL\t");
            taken = LXI(state, &(state->hl));
            break;
        case 0x23:
            TRACE_INS(INX);
            TRACE("HL\t");
            taken = INX(state, &(state->hl));
            break;
        case 0x26:
            TRACE_INS(MVI);
            TRACE("H\t");
            taken = MVI(state, state->h);
            break;
        case 0x29:
            TRACE_INS(DAD);
            TRACE("HL\t");
            taken = DAD(state, state->hl);
            break;
        case 0x2E:
            TRACE_INS(MVI);
            TRACE("L\t");
            taken = MVI(state, state->l);
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
        case 0x35:
            TRACE_INS(DCR);
            TRACE("M\t");
            taken = DCR_M(state);
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
        case 0x3D:
            TRACE_INS(DCR);
            TRACE("A\t");
            taken = DCR(state, &(state->a));
            break;
        case 0x3E:
            TRACE_INS(MVI);
            TRACE("A\t");
            taken = MVI(state, &(state->a));
            break;
        case 0x4F:
            TRACE_INS(MOV);
            TRACE("C\tA\t");
            taken = MOV(state, state->c, state->a);
            break;
        case 0x56:
            TRACE_INS(MOVXM);
            TRACE("D\t");
            taken = MOVXM(state, state->d);
            break;
        case 0x57:
            TRACE_INS(MOV);
            TRACE("D\tA\t");
            taken = MOV(state, state->d, state->a);
            break;
        case 0x5E:
            TRACE_INS(MOVXM);
            TRACE("E\t");
            taken = MOVXM(state, state->e);
            break;
        case 0x5F:
            TRACE_INS(MOV);
            TRACE("E\tA\t");
            taken = MOV(state, state->e, state->a);
            break;
        case 0x66:
            TRACE_INS(MOVXM);
            TRACE("H\t");
            taken = MOVXM(state, state->h);
            break;
        case 0x67:
            TRACE_INS(MOV);
            TRACE("H\tA\t");
            taken = MOV(state, state->h, state->a);
            break;
        case 0x6F:
            TRACE_INS(MOV);
            TRACE("L\tA\t");
            taken = MOV(state, state->l, state->a);
            break;
        case 0x77:
            TRACE_INS(MOVM);
            TRACE("A\t");
            taken = MOVM(state, state->a);
            break;
        case 0x7A:
            TRACE_INS(MOV);
            TRACE("A\tD\t");
            taken = MOV(state, &(state->a), *state->d);
            break;
        case 0x7B:
            TRACE_INS(MOV);
            TRACE("A\tE\t");
            taken = MOV(state, &(state->a), *state->e);
            break;
        case 0x7C:
            TRACE_INS(MOV);
            TRACE("A\tH\t");
            taken = MOV(state, &(state->a), *state->h);
            break;
        case 0x7D:
            TRACE_INS(MOV);
            TRACE("A\tL\t");
            taken = MOV(state, &(state->a), *state->l);
            break;
        case 0x7E:
            TRACE_INS(MOVXM);
            TRACE("A\t");
            taken = MOVXM(state, &(state->a));
            break;
        case 0xA7:
            TRACE_INS(ANA);
            TRACE("A\t");
            taken = ANA(state, state->a);
            break;
        case 0xAF:
            TRACE_INS(XRA);
            TRACE("A\t");
            taken = XRA(state, state->a);
            break;
            /*
        case 0xC0:
            TRACE_INS(RNZ);
            taken = RNZ(state);
            break;
            */
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
            TRACE_INS(*OUT);
            taken = OUT(state);
            break;
        case 0xD5:
            TRACE_INS(PUSH);
            TRACE("D\tE\t");
            taken = PUSH(state, state->de);
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
            TRACE_INS(*IN);
            taken = IN(state);
            break;
        case 0xE1:
            TRACE_INS(POP);
            TRACE("H\tL\t");
            taken = POP(state, &(state->hl));
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
        case 0xFB:
            TRACE_INS(EI);
            taken = EI(state);
            break;
        case 0xFE:
            TRACE_INS(CPI);
            taken = CPI(state);
            break;
        default:
            ABORT(("Unrecognized instruction 0x%02x at 0x%04lx\n", state->program[state->pc], state->pc));
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
    TRACE("cycle: %d\n", cycle);

    TRACE_STATE();
    return cycle;
}

static void generate_intr(struct state_t *state, int intr_num)
{
    state->mem[--state->sp] = ((state->pc >> 8) & 0x00FF);
    state->mem[--state->sp] = (state->pc & 0x00FF);

    state->pc = 0x08 * intr_num;
}

static unsigned char *display;
static struct state_t state;
static unsigned long long last_duration;

static int execute(struct state_t *state, int cycles)
{
    int cycle = 0;

    while (cycle < cycles) {
        int res = execute_one(state);
        if (res < 0) {
            return -1;
        }

        cycle += res;
    }

    return 0;
}

static void init_state(struct state_t *state, const char *filename)
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
        ABORT(("loading program failed\n"));
    }

    if (0 != mprotect(state->program, ROM_SIZE, PROT_READ)) {
        perror("mprotect() failed");
        ABORT(("%p\n", state->program));
    }
}

static void deinit_state(struct state_t *state)
{
    free(state->mem);
}

static void draw()
{
    int i, j;

    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_POINTS);
    for (i = 0; i < 256; ++i) {
        for (j = 0; j < 224; ++j) {
            int x = i / 8;
            int b = i % 8;
            if ((display[x + j * 32] & (0x01 << b)) != 0) {
                glVertex3f(((float)j / 224 * 2 - 1.0f), ((float)i / 256 * 2 - 1.0f), 0.0);
            }
        }
    }
    glEnd();
    glFlush();
}

static void display_loop()
{
    unsigned long long now, then;

    then = get_ns();

    execute(&state, CYCLES_BEFORE_VBLANK);

    if (state.intr) {
        generate_intr(&state, 1);
    }

    draw();

    execute(&state, CYCLES_AFTER_VBLANK);

    if (state.intr) {
        generate_intr(&state, 2);
    }

    printf("Last frame took %llu to render\n", last_duration);

    now = get_ns();
    last_duration = now - then;

    if (last_duration < NS_PER_FRAME) {
        usleep((NS_PER_FRAME - last_duration) / 1000);
    }
}

static void start_gl_loop(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE);
    glutInitWindowSize(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    glutCreateWindow("Space Invaders");

    display = state.mem + DISPLAY_ADDRESS;
    glutIdleFunc(display_loop);
    glutMainLoop();
}

int main(int argc, char **argv)
{
    struct options_t options;
    unsigned char *program = NULL;
    int res;

    if (parse_options(argc, argv, &options) != 0) {
        usage();
        ABORT(("Invalid options.\n"));
    }

    init_state(&state, options.bin_name);

    start_gl_loop(argc, argv);

    deinit_state(&state);
    return 0;
}

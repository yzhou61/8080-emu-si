/*

1 tab = 8 spaces

"It's a trap!" 0.0, a simple Win32 Space Invaders arcade emulator, (C) 2006 hap,
http://home.planet.nl/~haps/ .. (source may be used freely, as long as you give
proper credit, and your project's open source too)

Ackbar icon by Talos Tsui, http://www.iconfactory.com/
Space Invaders hardware and overlay information from MAME, http://www.mamedev.org/
8080 emulation based on document "Intel 8080 Assembly Language Programming Manual"

== HOW TO USE ==
Set the options, and compile (I use MinGW).
Put the 4 ROMs in the same folder as the executable, and run it.
Default keys are:
	action:		left/right/Z
	coin:		3
	start:		1/2
	tilt:		T
	hard reset:	R

================
Space Invaders, (C) Taito 1978, Midway 1979

CPU: Intel 8080 @ 2MHz (CPU similar to the (newer) Zilog Z80)

Interrupts: $cf (RST 8) at the start of vblank, $d7 (RST $10) at the end of vblank.

Video: 256(x)*224(y) @ 60Hz, vertical monitor. Colours are simulated with a
plastic transparent overlay and a background picture.
Video hardware is very simple: 7168 bytes 1bpp bitmap (32 bytes per scanline).

Sound: SN76477 and samples.

Memory map:
	ROM
	$0000-$07ff:	invaders.h
	$0800-$0fff:	invaders.g
	$1000-$17ff:	invaders.f
	$1800-$1fff:	invaders.e
	
	RAM
	$2000-$23ff:	work RAM
	$2400-$3fff:	video RAM
	
	$4000-:		RAM mirror

Ports:
	Read 0
	BIT	0	coin (0 when active)
		1	P2 start button
		2	P1 start button
		3	?
		4	P1 shoot button
		5	P1 joystick left
		6	P1 joystick right
		7	?
	
	Read 1
	BIT	0,1	dipswitch number of lives (0:3,1:4,2:5,3:6)
		2	tilt 'button'
		3	dipswitch bonus life at 1:1000,0:1500
		4	P2 shoot button
		5	P2 joystick left
		6	P2 joystick right
		7	dipswitch coin info 1:off,0:on
	
	Read 2		shift register result
	
	Write 2		shift register result offset (bits 0,1,2)
	Write 3		sound related
	Write 4		fill shift register
	Write 5		sound related
	Write 6		strange 'debug' port? eg. it writes to this port when
			it writes text to the screen (0=a,1=b,2=c, etc)
	
	(write ports 3,5,6 can be left unemulated, read port 0=$01 and 1=$00
	will make the game run, but but only in attract mode)

I haven't looked into sound details.

16 bit shift register:
	f              0	bit
	xxxxxxxxyyyyyyyy
	
	Writing to port 4 shifts x into y, and the new value into x, eg.
	$0000,
	write $aa -> $aa00,
	write $ff -> $ffaa,
	write $12 -> $12ff, ..
	
	Writing to port 2 (bits 0,1,2) sets the offset for the 8 bit result, eg.
	offset 0:
	rrrrrrrr		result=xxxxxxxx
	xxxxxxxxyyyyyyyy
	
	offset 2:
	  rrrrrrrr		result=xxxxxxyy
	xxxxxxxxyyyyyyyy
	
	offset 7:
	       rrrrrrrr		result=xyyyyyyy
	xxxxxxxxyyyyyyyy
	
	Reading from port 2 returns said result.

Overlay dimensions (screen rotated 90 degrees anti-clockwise):
	,_______________________________.
	|WHITE            ^             |
	|                32             |
	|                 v             |
	|-------------------------------|
	|RED              ^             |
	|                32             |
	|                 v             |
	|-------------------------------|
	|WHITE                          |
	|         < 224 >               |
	|                               |
	|                 ^             |
	|                120            |
	|                 v             |
	|                               |
	|                               |
	|                               |
	|-------------------------------|
	|GREEN                          |
	| ^                  ^          |
	|56        ^        56          |
	| v       72         v          |
	|____      v      ______________|
	|  ^  |          | ^            |
	|<16> |  < 118 > |16   < 122 >  |
	|  v  |          | v            |
	|WHITE|          |         WHITE|
	`-------------------------------'
	
	Way of out of proportion :P

*/

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OPTIONS */
/* set timer interval in ms (60hz=~16ms) */
#define SPEED	5
/* set initial dipswitches (8 bit) */
#define DIPS	0
/* set window size multiplication */
#define SIZE	2
/* simple CPU tracelog */
#define DEBUG 	0
/* see how many times each opcode executes */
#define PROFILE	0


static HWND window;
static HDC hdc,bitmaphdc;
static HBITMAP hbitmap;
static unsigned char* screen;
static unsigned char mem[0x4000];

/* i8080 CPU */
/* opcode cycles */
static const unsigned char lut_cycles[0x100]={
	4, 10,7, 5, 5, 5, 7, 4, 0, 10,7, 5, 5, 5, 7, 4,
	0, 10,7, 5, 5, 5, 7, 4, 0, 10,7, 5, 5, 5, 7, 4,
	0, 10,16,5, 5, 5, 7, 4, 0, 10,16,5, 5, 5, 7, 4,
	0, 10,13,5, 10,10,10,4, 0, 10,13,5, 5, 5, 7, 4,
	5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
	5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
	5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
	7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 5, 5, 7, 5,
	4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	5, 10,10,10,11,11,7, 11,5, 10,10,0, 11,17,7, 11,
	5, 10,10,10,11,11,7, 11,5, 0, 10,10,11,0, 7, 11,
	5, 10,10,18,11,11,7, 11,5, 5, 10,4, 11,0, 7, 11,
	5, 10,10,4, 11,11,7, 11,5, 5, 10,4, 11,0, 7, 11
};

static unsigned char lut_parity[0x100]; /* quick parity flag lookuptable, 4: even, 0: uneven */

#if PROFILE
static unsigned int lut_profiler[0x100]; /* occurance of opcodes */
#endif

/* opcode mnemonics (debug) */
#if DEBUG|PROFILE
static const char* lut_mnemonic[0x100]={
	"nop",     "lxi b,#", "stax b",  "inx b",   "inr b",   "dcr b",   "mvi b,#", "rlc",     "ill",     "dad b",   "ldax b",  "dcx b",   "inr c",   "dcr c",   "mvi c,#", "rrc",
	"ill",     "lxi d,#", "stax d",  "inx d",   "inr d",   "dcr d",   "mvi d,#", "ral",     "ill",     "dad d",   "ldax d",  "dcx d",   "inr e",   "dcr e",   "mvi e,#", "rar",
	"ill",     "lxi h,#", "shld",    "inx h",   "inr h",   "dcr h",   "mvi h,#", "daa",     "ill",     "dad h",   "lhld",    "dcx h",   "inr l",   "dcr l",   "mvi l,#", "cma",
	"ill",     "lxi sp,#","sta $",   "inx sp",  "inr M",   "dcr M",   "mvi M,#", "stc",     "ill",     "dad sp",  "lda $",   "dcx sp",  "inr a",   "dcr a",   "mvi a,#", "cmc",
	"mov b,b", "mov b,c", "mov b,d", "mov b,e", "mov b,h", "mov b,l", "mov b,M", "mov b,a", "mov c,b", "mov c,c", "mov c,d", "mov c,e", "mov c,h", "mov c,l", "mov c,M", "mov c,a",
	"mov d,b", "mov d,c", "mov d,d", "mov d,e", "mov d,h", "mov d,l", "mov d,M", "mov d,a", "mov e,b", "mov e,c", "mov e,d", "mov e,e", "mov e,h", "mov e,l", "mov e,M", "mov e,a",
	"mov h,b", "mov h,c", "mov h,d", "mov h,e", "mov h,h", "mov h,l", "mov h,M", "mov h,a", "mov l,b", "mov l,c", "mov l,d", "mov l,e", "mov l,h", "mov l,l", "mov l,M", "mov l,a",
	"mov M,b", "mov M,c", "mov M,d", "mov M,e", "mov M,h", "mov M,l", "hlt",     "mov M,a", "mov a,b", "mov a,c", "mov a,d", "mov a,e", "mov a,h", "mov a,l", "mov a,M", "mov a,a",
	"add b",   "add c",   "add d",   "add e",   "add h",   "add l",   "add M",   "add a",   "adc b",   "adc c",   "adc d",   "adc e",   "adc h",   "adc l",   "adc M",   "adc a",
	"sub b",   "sub c",   "sub d",   "sub e",   "sub h",   "sub l",   "sub M",   "sub a",   "sbb b",   "sbb c",   "sbb d",   "sbb e",   "sbb h",   "sbb l",   "sbb M",   "sbb a",
	"ana b",   "ana c",   "ana d",   "ana e",   "ana h",   "ana l",   "ana M",   "ana a",   "xra b",   "xra c",   "xra d",   "xra e",   "xra h",   "xra l",   "xra M",   "xra a",
	"ora b",   "ora c",   "ora d",   "ora e",   "ora h",   "ora l",   "ora M",   "ora a",   "cmp b",   "cmp c",   "cmp d",   "cmp e",   "cmp h",   "cmp l",   "cmp M",   "cmp a",
	"rnz",     "pop b",   "jnz $",   "jmp $",   "cnz $",   "push b",  "adi #",   "rst 0",   "rz",      "ret",     "jz $",    "ill",     "cz $",    "call $",  "aci #",   "rst 1",
	"rnc",     "pop d",   "jnc $",   "out p",   "cnc $",   "push d",  "sui #",   "rst 2",   "rc",      "ill",     "jc $",    "in p",    "cc $",    "ill",     "sbi #",   "rst 3",
	"rpo",     "pop h",   "jpo $",   "xthl",    "cpo $",   "push h",  "ani #",   "rst 4",   "rpe",     "pchl",    "jpe $",   "xchg",    "cpe $",   "ill",     "xri #",   "rst 5",
	"rp",      "pop psw", "jp $",    "di",      "cp $",    "push psw","ori #",   "rst 6",   "rm",      "sphl",    "jm $",    "ei",      "cm $",    "ill",     "cpi #",   "rst 7"
};
#endif

static struct {
	
	union {
		struct {
			unsigned short pc;		/* programcounter */
			unsigned short sp;		/* stackpointer */
			unsigned short psw,bc,de,hl;	/* register pairs */
		};
		struct {
			unsigned char pc_low,pc_high;
			unsigned char sp_low,sp_high;
			unsigned char flags;		/* sz0a0p1c */
			unsigned char a,c,b,e,d,l,h;	/* regs */
		};
	} reg;
	
	int cycles;
	unsigned short result;				/* temp result */
	unsigned char i;				/* interrupt bit */
	unsigned char ipend;				/* pending int */
	unsigned char a;				/* aux carry bit */
} cpu;

#define A		cpu.reg.a
#define F		cpu.reg.flags
#define B		cpu.reg.b
#define C		cpu.reg.c
#define D		cpu.reg.d
#define E		cpu.reg.e
#define H		cpu.reg.h
#define L		cpu.reg.l
#define PC		cpu.reg.pc
#define PCL		cpu.reg.pc_low
#define PCH		cpu.reg.pc_high
#define SP		cpu.reg.sp
#define SPL		cpu.reg.sp_low
#define SPH		cpu.reg.sp_high
#define PSW		cpu.reg.psw
#define BC		cpu.reg.bc
#define DE		cpu.reg.de
#define HL		cpu.reg.hl
#define RES		cpu.result

#define ISNOTZERO()	((RES&0xff)!=0)
#define ISZERO()	((RES&0xff)==0)
#define ISNOTCARRY()	((RES&0x100)==0)
#define ISCARRY()	((RES&0x100)!=0)
#define ISPODD()	(lut_parity[RES&0xff]==0)
#define ISPEVEN()	(lut_parity[RES&0xff]!=0)
#define ISPLUS()	((RES&0x80)==0)
#define ISMIN()		((RES&0x80)!=0)

#define R8(a)		mem[((a)>0x1fff)?((a)&0x1fff)|0x2000:a]
#define R16(a)		(R8((a)+1)<<8|(R8(a)))
#define W8(a,v)		if ((a)>0x1fff) mem[((a)&0x1fff)|0x2000]=v
#define PUSH16(v)	SP-=2; W8(SP,(v)&0xff); W8(SP+1,(v)>>8&0xff)
#define POP16()		R16(SP); SP+=2
#define JUMP()		PC=R16(PC)
#define CALL()		PUSH16(PC+2); JUMP()
#define CCON()		cpu.cycles-=6; CALL()
#define RET()		PC=POP16()
#define RCON()		cpu.cycles-=6; RET()
#define RST(x)		PUSH16(PC); PC=(x)<<3
#define FSZP(r)		RES=(RES&0x100)|r
#define INR(r)		r++; FSZP(r)
#define DCR(r)		r--; FSZP(r)
#define FAUX()		cpu.a=(A&0xf)>(RES&0xf)
#define ADD(r)		RES=A+r; FAUX(); A=RES&0xff
#define ADC(r)		RES=(RES>>8&1)+A+r; FAUX(); A=RES&0xff
#define DAD(r)		RES=(RES&0xff)|((r+HL)>>8&0x100); HL+=r
#define CMP(r)		RES=A-r; FAUX()
#define SUB(r)		CMP(r); A=RES&0xff
#define SBB(r)		RES=(A-r)-(RES>>8&1); FAUX(); A=RES&0xff
#define ANA(r)		RES=A=A&r
#define XRA(r)		RES=A=A^r
#define ORA(r)		RES=A=A|r

/* HW PORTS */
static unsigned char shift_offset=0;
static unsigned short shift_reg=0;

static unsigned char key=0;
static unsigned char dips=DIPS; /* dipswitches */


static unsigned char in_port(unsigned char port)
{
	unsigned char ret=port;
	
	switch (port) {
		case 1: ret=key&0x7f; break;
		case 2: ret=dips|(key&0x70)|(key>>5&4); break;
		case 3: ret=(shift_reg<<shift_offset)>>8; break;
		
		default: break;
	}
	
	return ret;
}

static void out_port(unsigned char port,unsigned char v)
{
	switch (port) {
		
		case 2: shift_offset=v&7; break;
		case 3: break; /* sound */
		case 4: shift_reg=shift_reg>>8|(v<<8); break;
		case 5: break; /* sound */
		case 6: /*if (v<26) printf("%c",v+65);*/ break; /* strange 'debug' port? */
		
		default: break;
	}
}

/* INTERRUPTS */
static void interrupt(int i)
{
	if (cpu.i) {
		cpu.i=cpu.ipend=0;
		cpu.cycles-=11;
		RST(i);
	}
	else cpu.ipend=0x80|i;
}

/* RUN CPU OPCODES */
static void cpu_run(int cycles)
{
	int opcode;
	
	cpu.cycles+=cycles;
	
	while (cpu.cycles>0) {
	
	opcode=R8(PC); PC++;
	cpu.cycles-=lut_cycles[opcode];
	
	switch (opcode) {
		/* MOVE, LOAD, AND STORE */
		case 0x40: break;				/* mov b,b */
		case 0x41: B=C; break;				/* mov b,c */
		case 0x42: B=D; break;				/* mov b,d */
		case 0x43: B=E; break;				/* mov b,e */
		case 0x44: B=H; break;				/* mov b,h */
		case 0x45: B=L; break;				/* mov b,l */
		case 0x46: B=R8(HL); break;			/* mov b,M */
		case 0x47: B=A; break;				/* mov b,a */
		
		case 0x48: C=B; break;				/* mov c,b */
		case 0x49: break;				/* mov c,c */
		case 0x4a: C=D; break;				/* mov c,d */
		case 0x4b: C=E; break;				/* mov c,e */
		case 0x4c: C=H; break;				/* mov c,h */
		case 0x4d: C=L; break;				/* mov c,l */
		case 0x4e: C=R8(HL); break;			/* mov c,M */
		case 0x4f: C=A; break;				/* mov c,a */
		
		case 0x50: D=B; break;				/* mov d,b */
		case 0x51: D=C; break;				/* mov d,c */
		case 0x52: break;				/* mov d,d */
		case 0x53: D=E; break;				/* mov d,e */
		case 0x54: D=H; break;				/* mov d,h */
		case 0x55: D=L; break;				/* mov d,l */
		case 0x56: D=R8(HL); break;			/* mov d,M */
		case 0x57: D=A; break;				/* mov d,a */
		
		case 0x58: E=B; break;				/* mov e,b */
		case 0x59: E=C; break;				/* mov e,c */
		case 0x5a: E=D; break;				/* mov e,d */
		case 0x5b: break;				/* mov e,e */
		case 0x5c: E=H; break;				/* mov e,h */
		case 0x5d: E=L; break;				/* mov e,l */
		case 0x5e: E=R8(HL); break;			/* mov e,M */
		case 0x5f: E=A; break;				/* mov e,a */
		
		case 0x60: H=B; break;				/* mov h,b */
		case 0x61: H=C; break;				/* mov h,c */
		case 0x62: H=D; break;				/* mov h,d */
		case 0x63: H=E; break;				/* mov h,e */
		case 0x64: break;				/* mov h,h */
		case 0x65: H=L; break;				/* mov h,l */
		case 0x66: H=R8(HL); break;			/* mov h,M */
		case 0x67: H=A; break;				/* mov h,a */
		
		case 0x68: L=B; break;				/* mov l,b */
		case 0x69: L=C; break;				/* mov l,c */
		case 0x6a: L=D; break;				/* mov l,d */
		case 0x6b: L=E; break;				/* mov l,e */
		case 0x6c: L=H; break;				/* mov l,h */
		case 0x6d: break;				/* mov l,l */
		case 0x6e: L=R8(HL); break;			/* mov l,M */
		case 0x6f: L=A; break;				/* mov l,a */
		
		case 0x70: W8(HL,B); break;			/* mov M,b */
		case 0x71: W8(HL,C); break;			/* mov M,c */
		case 0x72: W8(HL,D); break;			/* mov M,d */
		case 0x73: W8(HL,E); break;			/* mov M,e */
		case 0x74: W8(HL,H); break;			/* mov M,h */
		case 0x75: W8(HL,L); break;			/* mov M,l */
		/* mov M,M = hlt */
		case 0x77: W8(HL,A); break;			/* mov M,a */
		
		case 0x78: A=B; break;				/* mov a,b */
		case 0x79: A=C; break;				/* mov a,c */
		case 0x7a: A=D; break;				/* mov a,d */
		case 0x7b: A=E; break;				/* mov a,e */
		case 0x7c: A=H; break;				/* mov a,h */
		case 0x7d: A=L; break;				/* mov a,l */
		case 0x7e: A=R8(HL); break;			/* mov a,M */
		case 0x7f: break;				/* mov a,a */
		
		case 0x06: B=R8(PC); PC++; break;		/* mvi b,# */
		case 0x0e: C=R8(PC); PC++; break;		/* mvi c,# */
		case 0x16: D=R8(PC); PC++; break;		/* mvi d,# */
		case 0x1e: E=R8(PC); PC++; break;		/* mvi e,# */
		case 0x26: H=R8(PC); PC++; break;		/* mvi h,# */
		case 0x2e: L=R8(PC); PC++; break;		/* mvi l,# */
		case 0x36: W8(HL,R8(PC)); PC++; break;		/* mvi M,# */
		case 0x3e: A=R8(PC); PC++; break;		/* mvi a,# */
		
		case 0x01: BC=R16(PC); PC+=2; break;		/* lxi b,# */
		case 0x11: DE=R16(PC); PC+=2; break;		/* lxi d,# */
		case 0x21: HL=R16(PC); PC+=2; break;		/* lxi h,# */
		
		case 0x02: W8(BC,A); break;			/* stax b */
		case 0x12: W8(DE,A); break;			/* stax d */
		case 0x0a: A=R8(BC); break;			/* ldax b */
		case 0x1a: A=R8(DE); break;			/* ldax d */
		case 0x22: W8(R16(PC),L); W8(R16(PC)+1,H); PC+=2; break;	/* shld */
		case 0x2a: L=R8(R16(PC)); H=R8(R16(PC)+1); PC+=2; break;	/* lhld */
		case 0x32: W8(R16(PC),A); PC+=2; break;		/* sta $ */
		case 0x3a: A=R8(R16(PC)); PC+=2; break;		/* lda $ */
		
		case 0xeb: HL^=DE; DE^=HL; HL^=DE; break;	/* xchg */
		
		
		/* STACK OPS */
		case 0xc5: PUSH16(BC); break;			/* push b */
		case 0xd5: PUSH16(DE); break;			/* push d */
		case 0xe5: PUSH16(HL); break;			/* push h */
		case 0xf5: F=(RES>>8&1)|2|lut_parity[RES&0xff]|(cpu.a<<4)|(((RES&0xff)==0)<<6)|(RES&0x80); PUSH16(PSW); break;	/* push psw */
		
		case 0xc1: BC=POP16(); break;			/* pop b */
		case 0xd1: DE=POP16(); break;			/* pop d */
		case 0xe1: HL=POP16(); break;			/* pop h */
		case 0xf1: PSW=POP16(); RES=(F<<8&0x100)|(lut_parity[F&0x80]!=(F&4))|(F&0x80)|((F&0x40)?0:6); cpu.a=F>>4&1; break;	/* pop psw */
		
		case 0xe3: L^=R8(SP); W8(SP,R8(SP)^L); L^=R8(SP); H^=R8(SP+1); W8(SP+1,R8(SP+1)^H); H^=R8(SP+1); break;	/* xthl */
		
		case 0xf9: SP=HL; break;			/* sphl */
		
		case 0x31: SP=R16(PC); PC+=2; break;		/* lxi sp,# */
		
		case 0x33: SP++; break;				/* inx sp */
		case 0x3b: SP--; break;				/* dcx sp */
		
		
		/* JUMP */
		case 0xc3: JUMP(); break;			/* jmp $ */
		
		case 0xc2: if (ISNOTZERO()) JUMP(); else PC+=2; break;	/* jnz $ */
		case 0xca: if (ISZERO()) JUMP(); else PC+=2; break;	/* jz $ */
		case 0xd2: if (ISNOTCARRY()) JUMP(); else PC+=2; break;	/* jnc $ */
		case 0xda: if (ISCARRY()) JUMP(); else PC+=2; break;	/* jc $ */
		case 0xe2: if (ISPODD()) JUMP(); else PC+=2; break;	/* jpo $ */
		case 0xea: if (ISPEVEN()) JUMP(); else PC+=2; break;	/* jpe $ */
		case 0xf2: if (ISPLUS()) JUMP(); else PC+=2; break;	/* jp $ */
		case 0xfa: if (ISMIN()) JUMP(); else PC+=2; break;	/* jm $ */
		
		case 0xe9: PC=HL; break;			/* pchl */
		
		
		/* CALL */
		case 0xcd: CALL(); break;			/* call $ */
		
		case 0xc4: if (ISNOTZERO()) { CCON(); } else PC+=2; break;	/* cnz $ */
		case 0xcc: if (ISZERO()) { CCON(); } else PC+=2; break;		/* cz $ */
		case 0xd4: if (ISNOTCARRY()) { CCON(); } else PC+=2; break;	/* cnc $ */
		case 0xdc: if (ISCARRY()) { CCON(); } else PC+=2; break;	/* cc $ */
		case 0xe4: if (ISPODD()) { CCON(); } else PC+=2; break;		/* cpo $ */
		case 0xec: if (ISPEVEN()) { CCON(); } else PC+=2; break;	/* cpe $ */
		case 0xf4: if (ISPLUS()) { CCON(); } else PC+=2; break;		/* cp $ */
		case 0xfc: if (ISMIN()) { CCON(); } else PC+=2; break;		/* cm $ */
		
		
		/* RETURN */
		case 0xc9: RET(); break;			/* ret */
		
		case 0xc0: if (ISNOTZERO()) { RCON(); } break;	/* rnz */
		case 0xc8: if (ISZERO()) { RCON(); } break;	/* rz */
		case 0xd0: if (ISNOTCARRY()) { RCON(); } break;	/* rnc */
		case 0xd8: if (ISCARRY()) { RCON(); } break;	/* rc */
		case 0xe0: if (ISPODD()) { RCON(); } break;	/* rpo */
		case 0xe8: if (ISPEVEN()) { RCON(); } break;	/* rpe */
		case 0xf0: if (ISPLUS()) { RCON(); } break;	/* rp */
		case 0xf8: if (ISMIN()) { RCON(); } break;	/* rm */
		
		
		/* RESTART */
		case 0xc7: case 0xcf: case 0xd7: case 0xdf: case 0xe7: case 0xef: case 0xf7: case 0xff:
			RST(opcode>>3&7); break;		/* rst x */
		
		
		/* INCREMENT AND DECREMENT */
		case 0x04: INR(B); break;			/* inr b */
		case 0x0c: INR(C); break;			/* inr c */
		case 0x14: INR(D); break;			/* inr d */
		case 0x1c: INR(E); break;			/* inr e */
		case 0x24: INR(H); break;			/* inr h */
		case 0x2c: INR(L); break;			/* inr l */
		case 0x34: W8(HL,(R8(HL)+1)&0xff); FSZP(R8(HL)); break;	/* inr M */
		case 0x3c: INR(A); break;			/* inr a */
		
		case 0x05: DCR(B); break;			/* dcr b */
		case 0x0d: DCR(C); break;			/* dcr c */
		case 0x15: DCR(D); break;			/* dcr d */
		case 0x1d: DCR(E); break;			/* dcr e */
		case 0x25: DCR(H); break;			/* dcr h */
		case 0x2d: DCR(L); break;			/* dcr l */
		case 0x35: W8(HL,(R8(HL)+0xff)&0xff); FSZP(R8(HL)); break;	/* dcr M */
		case 0x3d: DCR(A); break;			/* dcr a */
		
		case 0x03: BC++; break;				/* inx b */
		case 0x13: DE++; break;				/* inx d */
		case 0x23: HL++; break;				/* inx h */
		
		case 0x0b: BC--; break;				/* dcx b */
		case 0x1b: DE--; break;				/* dcx d */
		case 0x2b: HL--; break;				/* dcx h */
		
		
		/* ADD */
		case 0x80: ADD(B); break;			/* add b */
		case 0x81: ADD(C); break;			/* add c */
		case 0x82: ADD(D); break;			/* add d */
		case 0x83: ADD(E); break;			/* add e */
		case 0x84: ADD(H); break;			/* add h */
		case 0x85: ADD(L); break;			/* add l */
		case 0x86: ADD(R8(HL)); break;			/* add M */
		case 0x87: ADD(A); break;			/* add a */
		
		case 0x88: ADC(B); break;			/* adc b */
		case 0x89: ADC(C); break;			/* adc c */
		case 0x8a: ADC(D); break;			/* adc d */
		case 0x8b: ADC(E); break;			/* adc e */
		case 0x8c: ADC(H); break;			/* adc h */
		case 0x8d: ADC(L); break;			/* adc l */
		case 0x8e: ADC(R8(HL)); break;			/* adc M */
		case 0x8f: ADC(A); break;			/* adc a */
		
		case 0xc6: ADD(R8(PC)); PC++; break;		/* adi # */
		case 0xce: ADC(R8(PC)); PC++; break;		/* aci # */
		
		case 0x09: DAD(BC); break;			/* dad b */
		case 0x19: DAD(DE); break;			/* dad d */
		case 0x29: DAD(HL); break;			/* dad h */
		case 0x39: DAD(SP); break;			/* dad sp */
		
		
		/* SUBTRACT */
		case 0x90: SUB(B); break;			/* sub b */
		case 0x91: SUB(C); break;			/* sub c */
		case 0x92: SUB(D); break;			/* sub d */
		case 0x93: SUB(E); break;			/* sub e */
		case 0x94: SUB(H); break;			/* sub h */
		case 0x95: SUB(L); break;			/* sub l */
		case 0x96: SUB(R8(HL)); break;			/* sub M */
		case 0x97: SUB(A); break;			/* sub a */
		
		case 0x98: SBB(B); break;			/* sbb b */
		case 0x99: SBB(C); break;			/* sbb c */
		case 0x9a: SBB(D); break;			/* sbb d */
		case 0x9b: SBB(E); break;			/* sbb e */
		case 0x9c: SBB(H); break;			/* sbb h */
		case 0x9d: SBB(L); break;			/* sbb l */
		case 0x9e: SBB(R8(HL)); break;			/* sbb M */
		case 0x9f: SBB(A); break;			/* sbb a */
		
		case 0xd6: SUB(R8(PC)); PC++; break;		/* sui # */
		case 0xde: SBB(R8(PC)); PC++; break;		/* sbi # */
		
		
		/* LOGICAL */
		case 0xa0: ANA(B); break;			/* ana b */
		case 0xa1: ANA(C); break;			/* ana c */
		case 0xa2: ANA(D); break;			/* ana d */
		case 0xa3: ANA(E); break;			/* ana e */
		case 0xa4: ANA(H); break;			/* ana h */
		case 0xa5: ANA(L); break;			/* ana l */
		case 0xa6: ANA(R8(HL)); break;			/* ana M */
		case 0xa7: ANA(A); break;			/* ana a */
		
		case 0xe6: ANA(R8(PC)); PC++; break;		/* ani # */
		
		case 0xa8: XRA(B); break;			/* xra b */
		case 0xa9: XRA(C); break;			/* xra c */
		case 0xaa: XRA(D); break;			/* xra d */
		case 0xab: XRA(E); break;			/* xra e */
		case 0xac: XRA(H); break;			/* xra h */
		case 0xad: XRA(L); break;			/* xra l */
		case 0xae: XRA(R8(HL)); break;			/* xra M */
		case 0xaf: XRA(A); break;			/* xra a */
		
		case 0xee: XRA(R8(PC)); PC++; break;		/* xri # */
		
		case 0xb0: ORA(B); break;			/* ora b */
		case 0xb1: ORA(C); break;			/* ora c */
		case 0xb2: ORA(D); break;			/* ora d */
		case 0xb3: ORA(E); break;			/* ora e */
		case 0xb4: ORA(H); break;			/* ora h */
		case 0xb5: ORA(L); break;			/* ora l */
		case 0xb6: ORA(R8(HL)); break;			/* ora M */
		case 0xb7: ORA(A); break;			/* ora a */
		
		case 0xf6: ORA(R8(PC)); PC++; break;		/* ori # */
		
		case 0xb8: CMP(B); break;			/* cmp b */
		case 0xb9: CMP(C); break;			/* cmp c */
		case 0xba: CMP(D); break;			/* cmp d */
		case 0xbb: CMP(E); break;			/* cmp e */
		case 0xbc: CMP(H); break;			/* cmp h */
		case 0xbd: CMP(L); break;			/* cmp l */
		case 0xbe: CMP(R8(HL)); break;			/* cmp M */
		case 0xbf: CMP(A); break;			/* cmp a */
		
		case 0xfe: CMP(R8(PC)); PC++; break;		/* cpi # */
		
		
		/* ROTATE */
		case 0x07: RES=(RES&0xff)|(A<<1&0x100); A<<=1; break;	/* rlc */
		case 0x0f: RES=(RES&0xff)|(A<<8&0x100); A>>=1; break;	/* rrc */
		case 0x17: { int c=A<<1&0x100; A=A<<1|(RES>>8&1); RES=(RES&0xff)|c; break; }	/* ral */
		case 0x1f: { int c=A<<8&0x100; A=A>>1|(RES>>1&0x80); RES=(RES&0xff)|c; break; }	/* rar */
		
		
		/* SPECIALS */
		case 0x2f: A=~A; break;				/* cma */
		case 0x37: RES|=0x100; break;			/* stc */
		case 0x3f: RES^=0x100; break;			/* cmc */
		
		case 0x27: {					/* daa */
			int c=RES,a=cpu.a;
			if (cpu.a||((A&0xf)>9)) { ADD(6); } if (cpu.a) a=cpu.a;
			if ((RES&0x100)||((A&0xf0)>0x90)) { ADD(0x60); } if (RES&0x100) c=RES;
			RES=(RES&0xff)|(c&0x100); cpu.a=a;
			break;
		}
		
		
		/* INPUT/OUTPUT */
		case 0xd3: out_port(R8(PC),A); PC++; break;	/* out p */
		case 0xdb: A=in_port(R8(PC)); PC++; break;	/* in p */
		
		
		/* CONTROL */
		case 0xf3: cpu.i=0; break;			/* di */
		case 0xfb: cpu.i=1; if (cpu.ipend&0x80) interrupt(cpu.ipend&0x7f); break;	/* ei */
		case 0x00: break;				/* nop */
		case 0x76: cpu.cycles=0; break;			/* hlt (mov M,M) */
		
		default: break;
	}
	
	#if DEBUG
	printf("%04x:%10s a%02X f%02X b%02X c%02X d%02X e%02X h%02X l%02X sp%04X\n",PC,lut_mnemonic[opcode],A,F,B,C,D,E,H,L,SP);
	#endif
	
	#if PROFILE
	lut_profiler[opcode]++;
	#endif
	
	}
}


/* SHELL */
static void vblank(void)
{
	/* draw screen */
	int x,o,data=0,dp=0;
	unsigned char* m=mem+0x2400;
	
	for (x=0;x<224;x++)
		for (o=(255*224)+x;o>=x;o-=224) {
			if (!dp--) { dp=7; data=*m++; }
			screen[o]=(screen[o]&0xfe)|(data&1);
			data>>=1;
		}
	
	PostMessage(window,WM_PAINT,0,0);
	
	/* read input */
	if (window!=GetForegroundWindow()) return;
	#define KEY(x) GetAsyncKeyState(x)
	key=(~KEY(51)>>15&1)|(KEY(50)>>14&2)|(KEY(49)>>13&4)|(KEY(90)>>11&0x10)|(KEY(VK_RIGHT)>>9&0x40)|(KEY(84)>>8&0x80);
	key|=((key>>1^(KEY(VK_LEFT)>>10))&0x20);
	
	if (KEY(82)&0x8000) { PC=0; memset(mem+0x2000,0x0,0x02000); } /* 'reset' */
}

static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg) {
		
		case WM_PAINT: {
			PAINTSTRUCT ps;
			BeginPaint(wnd,&ps);
			#if SIZE==1
				BitBlt(hdc,0,0,224,256,bitmaphdc,0,0,SRCCOPY);
			#else
				StretchBlt(hdc,0,0,224*SIZE,256*SIZE,bitmaphdc,0,0,224,256,SRCCOPY);
			#endif
			EndPaint(wnd,&ps);
			break;
		}
		
		case WM_TIMER: {
			cpu_run(28527);
			interrupt(1);
			vblank();
			cpu_run(4839);
			interrupt(2);
			break;
		}
		
		case WM_CLOSE: DestroyWindow(wnd); break;
		case WM_DESTROY: PostQuitMessage(0); break;
		default: return DefWindowProc(wnd, msg, wParam, lParam);
	}
	return FALSE;
}

static int loadrom(const char* fn,int offset)
{
	FILE* f;
	int flen;
	
	if ((f=fopen(fn,"rb"))==NULL) { return 1; }
	fseek(f,0,SEEK_END); flen=ftell(f); fseek(f,0,SEEK_SET);
	if (flen!=0x800||(!fread(mem+offset,1,0x800,f))) { clearerr(f); fclose(f); return 1; }
	fclose(f); return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	#define APPNAME "\"It's a trap!\""
	
	WNDCLASSEX wc={0};
	MSG Msg;
	RECT rect={0};
	BITMAPINFO bmp_info={{0}};
	int i;
	
	timeBeginPeriod(1);
	
	InitCommonControls();
	
	/* init hardware */
	memset(mem,0x0,0x04000);
	if (loadrom("invaders.h",0)||loadrom("invaders.g",0x800)||loadrom("invaders.f",0x1000)||loadrom("invaders.e",0x1800)) {
		MessageBox(NULL,"Couldn't open ROM!",APPNAME,MB_ICONEXCLAMATION|MB_OK); return 1;
	}
	
	memset(&cpu,0,sizeof(cpu));
	cpu.result=1;
	for (i=0;i<0x100;i++) lut_parity[i]=4&(4^(i<<2)^(i<<1)^i^(i>>1)^(i>>2)^(i>>3)^(i>>4)^(i>>5));
	
	#if PROFILE
	memset(lut_profiler,0,0x100);
	#endif
	
	
	/* register the window class */
	wc.cbSize=sizeof(WNDCLASSEX);
	wc.lpfnWndProc=WndProc;
	wc.hInstance=hInstance;
	wc.hCursor=LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName=APPNAME;
	wc.hIcon=LoadIcon(GetModuleHandle(NULL),MAKEINTRESOURCE(1000));
	wc.hIconSm=(HICON)LoadImage(GetModuleHandle(NULL),MAKEINTRESOURCE(1000),IMAGE_ICON,16,16,0);
	if(!RegisterClassEx(&wc)) { MessageBox(NULL,"Window registration failed!",APPNAME,MB_ICONEXCLAMATION|MB_OK); return 1; }
	
	
	/* create window */
	AdjustWindowRectEx(&rect,WS_SYSMENU|WS_MINIMIZEBOX|WS_CAPTION|WS_VISIBLE,0,WS_EX_CLIENTEDGE);
	window=CreateWindowEx(WS_EX_CLIENTEDGE,APPNAME,APPNAME,WS_SYSMENU|WS_MINIMIZEBOX|WS_CAPTION|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,(rect.right-rect.left)+224*SIZE,(rect.bottom-rect.top)+256*SIZE,NULL,NULL,hInstance,NULL);
	if(window==NULL) { MessageBox(NULL,"Window creation failed!",APPNAME,MB_ICONEXCLAMATION|MB_OK); return 1; }
	
	hdc=GetDC(window);
	bitmaphdc=CreateCompatibleDC(hdc);
	
	bmp_info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bmp_info.bmiHeader.biWidth=224;
	bmp_info.bmiHeader.biHeight=-256;
	bmp_info.bmiHeader.biPlanes=1;
	bmp_info.bmiHeader.biBitCount=8;
	bmp_info.bmiHeader.biCompression=BI_RGB;
	
	bmp_info.bmiHeader.biClrUsed=256;
	
	#define PAL(p,r,g,b) bmp_info.bmiColors[p].rgbRed=r; bmp_info.bmiColors[p].rgbGreen=g; bmp_info.bmiColors[p].rgbBlue=b
	PAL(0,0x00,0x00,0x00); PAL(1,0xff,0xff,0xff); /* white */
	PAL(2,0x00,0x10,0x00); PAL(3,0x40,0xff,0x40); /* green */
	PAL(4,0x10,0x00,0x00); PAL(5,0xff,0x40,0x40); /* red */
	
	hbitmap=CreateDIBSection(hdc,&bmp_info,DIB_RGB_COLORS,(void*)&screen,NULL,0);
	
	memset(screen,0,256*224);
	memset(screen+32*224,4,32*224);
	memset(screen+184*224,2,56*224);
	i=16; while (i--) memset(screen+(240+i)*224+16,2,118);
	
	SelectObject(bitmaphdc,hbitmap);
	
	ShowWindow(window,nCmdShow);
	UpdateWindow(window);
	
	SetTimer(window,0,SPEED,NULL); /* inaccurate for strict timing; 16ms should be 60hz, but that results in slowness here */
	
	
	/* messageloop */
	while(GetMessage(&Msg,NULL,0,0)>0) {
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}
	
	
	if (screen) free (screen);
	DeleteObject(hbitmap);
	DeleteDC(bitmaphdc);
	ReleaseDC(window,hdc);
	
	timeEndPeriod(1);
	
	UnregisterClass(wc.lpszClassName,hInstance);
	
	#if PROFILE
	for (i=0;i<0x100;i++) printf("%02X - %10s - %d\n",i,lut_mnemonic[i],lut_profiler[i]);
	#endif
	
	return Msg.wParam;
}

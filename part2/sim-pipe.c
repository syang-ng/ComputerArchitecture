/*
 * Author: syang2forever
 * Mail: syang2forever@gmail.com
 * Blog: syang2forever.github.io
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* An implementation of 5-stage classic pipeline simulation */

/* don't count instructions flag, enabled by default, disable for inst count */
#undef NO_INSN_COUNT

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "dlite.h"
#include "sim.h"
#include "sim-pipe.h"

/* simulated registers */
static struct regs_t regs;

/* simulated memory */
static struct mem_t *mem = NULL;

/* register simulator-specific options */
void
sim_reg_options(struct opt_odb_t *odb)
{
  opt_reg_header(odb, 
"sim-pipe: This simulator implements based on sim-fast.\n"
		 );
}

/* check simulator-specific option values */
void
sim_check_options(struct opt_odb_t *odb, int argc, char **argv)
{
  if (dlite_active)
    fatal("sim-pipe does not support DLite debugging");
}

/* register simulator-specific statistics */
void
sim_reg_stats(struct stat_sdb_t *sdb)
{
#ifndef NO_INSN_COUNT
  stat_reg_counter(sdb, "sim_num_insn",
		   "total number of instructions executed",
		   &sim_num_insn, sim_num_insn, NULL);
#endif /* !NO_INSN_COUNT */
  stat_reg_int(sdb, "sim_elapsed_time",
	       "total simulation time in seconds",
	       &sim_elapsed_time, 0, NULL);
#ifndef NO_INSN_COUNT
  stat_reg_formula(sdb, "sim_inst_rate",
		   "simulation speed (in insts/sec)",
		   "sim_num_insn / sim_elapsed_time", NULL);
#endif /* !NO_INSN_COUNT */
  ld_reg_stats(sdb);
  mem_reg_stats(mem, sdb);
}

FILE *stream;

struct ifid_buf fd;
struct idex_buf de;
struct exmem_buf em;
struct memwb_buf mw;
struct wb_buf wb;

struct control_buf ctl;

#define DNA			(-1)

/* general register dependence decoders */
#define DGPR(N)			(N)
#define DGPR_D(N)		((N) &~1)

/* floating point register dependence decoders */
#define DFPR_L(N)		(((N)+32)&~1)
#define DFPR_F(N)		(((N)+32)&~1)
#define DFPR_D(N)		(((N)+32)&~1)

/* miscellaneous register dependence decoders */
#define DHI			(0+32+32)
#define DLO			(1+32+32)
#define DFCC		(2+32+32)
#define DTMP		(3+32+32)

/* initialize the simulator */
void
sim_init(void)
{
  /* allocate and initialize register file */
  regs_init(&regs);

  /* allocate and initialize memory space */
  mem = mem_create("mem");
  mem_init(mem);

  /* initialize stage latches*/
  sim_num_insn = 0;
  /* IF/ID */
  init_fd();
  /* ID/EX */
  init_de();
  /* EX/MEM */
  init_em();  
  /* MEM/WB */
  init_mw();
  /* AFTER WB */  
  init_wb();

  ctl.flag = 0;
  ctl.cond = 0;
  ctl.regs = 0;
  ctl.stall = 0;
  stream = stdout;
}

/* load program into simulated state */
void
sim_load_prog(char *fname,		/* program to load */
	      int argc, char **argv,	/* program arguments */
	      char **envp)		/* program environment */
{
  /* load program text and data, set up environment, memory, and regs */
  ld_load_prog(fname, argc, argv, envp, &regs, mem, TRUE);
}

/* print simulator-specific configuration information */
void
sim_aux_config(FILE *stream)
{  
	/* nothing currently */
}

/* dump simulator-specific auxiliary simulator statistics */
void
sim_aux_stats(FILE *stream)
{  /* nada */}

/* un-initialize simulator-specific state */
void 
sim_uninit(void)
{ /* nada */ }


/*
 * configure the execution engine
 */

/* next program counter */
#define SET_NPC(EXPR)		(regs.regs_NPC = (EXPR))

/* current program counter */
#define CPC			(regs.regs_PC)

/* general purpose registers */
#define GPR(N)			(regs.regs_R[N])
#define SET_GPR(N,EXPR)		(regs.regs_R[N] = (EXPR))
#define DECLARE_FAULT(EXP) 	{;}
#if defined(TARGET_PISA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_L(N)		(regs.regs_F.l[(N)])
#define SET_FPR_L(N,EXPR)	(regs.regs_F.l[(N)] = (EXPR))
#define FPR_F(N)		(regs.regs_F.f[(N)])
#define SET_FPR_F(N,EXPR)	(regs.regs_F.f[(N)] = (EXPR))
#define FPR_D(N)		(regs.regs_F.d[(N) >> 1])
#define SET_FPR_D(N,EXPR)	(regs.regs_F.d[(N) >> 1] = (EXPR))

/* miscellaneous register accessors */
#define SET_HI(EXPR)		(regs.regs_C.hi = (EXPR))
#define HI			(regs.regs_C.hi)
#define SET_LO(EXPR)		(regs.regs_C.lo = (EXPR))
#define LO			(regs.regs_C.lo)
#define FCC			(regs.regs_C.fcc)
#define SET_FCC(EXPR)		(regs.regs_C.fcc = (EXPR))

#endif

/* precise architected memory state accessor macros */
#define READ_BYTE(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_BYTE(mem, (SRC)))
#define READ_HALF(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_HALF(mem, (SRC)))
#define READ_WORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_WORD(mem, (SRC)))
#ifdef HOST_HAS_QWORD
#define READ_QWORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_QWORD(mem, (SRC)))
#endif /* HOST_HAS_QWORD */

#define WRITE_BYTE(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_BYTE(mem, (DST), (SRC)))
#define WRITE_HALF(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_HALF(mem, (DST), (SRC)))
#define WRITE_WORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_WORD(mem, (DST), (SRC)))
#ifdef HOST_HAS_QWORD
#define WRITE_QWORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_QWORD(mem, (DST), (SRC)))
#endif /* HOST_HAS_QWORD */

/* system call handler macro */
#define SYSCALL(INST)	sys_syscall(&regs, mem_access, mem, INST, TRUE)

#ifndef NO_INSN_COUNT
#define INC_INSN_CTR()	sim_num_insn++
#else /* !NO_INSN_COUNT */
#define INC_INSN_CTR()	/* nada */
#endif /* NO_INSN_COUNT */


/* start simulation, program loaded, processor precise state initialized */
void
sim_main(void)
{
  fprintf(stderr, "sim: ** starting *pipe* functional simulation **\n");

  /* must have natural byte/word ordering */
  if (sim_swap_bytes || sim_swap_words)
    fatal("sim: *pipe* functional simulation cannot swap bytes or words");

  /* set up initial default next PC */
  regs.regs_NPC = regs.regs_PC + sizeof(md_inst_t);
  /* maintain $r0 semantics */
  regs.regs_R[MD_REG_ZERO] = 0;
 
  fd.PC = regs.regs_PC - sizeof(md_inst_t);
  
  while (TRUE){
    pipeline_control();
    do_wb();
    do_mem();
    do_ex();
    do_id();
    do_forward();    
    do_if();
    #ifndef NO_INSN_COUNT
      sim_num_insn++;
    #endif
    do_log();
    if(sim_num_insn > 1000) {
      exit(0);
    }
  }
}

void forward(int *val, int *src) {
  if(*src != DNA) {
    if(*src == em.dstE) {
      *val = em.valE;
    } else if(*src == mw.dstE) {
      *val = mw.valE;      
    } else if(*src == mw.dstM) {
      *val = mw.valM;      
    } else if(*src == wb.dstE) {
      *val = wb.valE;            
    } else if(*src == wb.dstM) {
      *val = wb.valM;      
    } else {
      *val = GPR(*src);      
    }
  } else {
    *val = 0;
  }
}

void do_forward() {
  forward(&de.aluA, &de.srcA);
  forward(&de.aluB, &de.srcB);
  forward(&de.valA, &de.oprand.in1);
}

/* since load-use hazard can't be forwarding*/
void pipeline_control() {
  if(ctl.cond) {
    /* predict wrong */    
    if(ctl.flag&2){
      de.inst.a = NOP;
    }
    ctl.flag = FALSE;
    ctl.cond = FALSE;
  }
  /* check for rw */
  if(ctl.stall) {
    fd.PC = de.PC;
    fd.inst = de.inst;
    de.inst.a = NOP;
  }
}

void do_if() {
  if(ctl.cond&2) {
    fd.NPC = em.target;
  } else if(ctl.cond&1) {
    fd.NPC = de.target;
  } else {
    fd.NPC = fd.PC + sizeof(md_inst_t);
  }
  md_inst_t instruction;
  fd.PC = fd.NPC;  
  MD_FETCH_INSTI(instruction, mem, fd.PC);
  fd.inst = instruction;
}

void do_id() {
    de.inst = fd.inst;
    de.PC = fd.PC;
    de.rw = 0;
    /* check if inst is nop, simply return */
    if(de.inst.a == NOP) {
      return;
    }
    MD_SET_OPCODE(de.opcode, de.inst);
    md_inst_t inst = de.inst;
#define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3)\
  if (OP==de.opcode){\
    de.instFlags = FLAGS;\
    de.oprand.out1 = O1;\
    de.oprand.out2 = O2;\
    de.oprand.in1 = I1;\
    de.oprand.in2 = I2;\
    de.oprand.in3 = I3;\
    goto READ_OPRAND_VALUE;\
  }
#define DEFLINK(OP,MSK,NAME,MASK,SHIFT)
#define CONNECT(OP)
#include "machine.def"
READ_OPRAND_VALUE:
  /* check for stall */    
  if((de.oprand.in1 >= 0 && (ctl.regs&1<<de.oprand.in1)) || (de.oprand.in2 >= 0 && (ctl.regs&1<<de.oprand.in2))) {
    ctl.stall = TRUE;
    return;
  } else {
    ctl.stall = FALSE;
  }
  
  switch(de.opcode){
    case LW:
    case SW:
    case ADD:
    case ADDI:
    case ADDU:
    case ADDIU:
    case LUI:
      de.func = ALU_ADD;
      break;
    case BNE:
      ctl.flag = TRUE; 
      de.target = fd.PC + 8 + ((de.inst.b & 0xffff) << 2);
      de.func = ALU_SUB;
      break;
    case ANDI:
      de.func = ALU_AND;
      break;
    case SLL:
      de.func = ALU_SHTL;
      break;
    case SLTI:
      de.func = ALU_SLT;
      break;
    case JUMP:
      ctl.flag = TRUE;
      ctl.cond |= 1;
      de.target = (fd.PC & 0xf0000000) | ((de.inst.b & 0x3ffffff) << 2);
    default:
      de.func = ALU_NOP;
      break;        
  }
  /* src A*/
  if(de.instFlags & F_DISP) {
    de.srcA = de.oprand.in2; 
  } else {
    de.srcA = de.oprand.in1; 
  }
  /* src B */
  de.srcB = de.oprand.in2; 
  /* load */
  if(de.instFlags&F_STORE) {
    de.rw |= 2;    
  }
  /* dst/read */ 
  if(de.instFlags&F_LOAD) {
    de.rw |= 4;
    de.dstM = de.oprand.out1;
    de.dstE = DNA;
    ctl.regs |= 1 << de.dstM;
  } else {
    de.dstE = de.oprand.out1;
    de.dstM = DNA;
  }
}

void do_ex() {
  em.inst = de.inst;
  em.PC = de.PC;
  em.dstE = de.dstE;
  em.dstM = de.dstM;  
  em.valA = de.valA;
  em.rw = de.rw;
  em.target = de.target;
  /* alu A */
  int aluA = de.aluA;
  /* alu B */  
  int aluB;
  if(de.instFlags&F_IMM || de.instFlags&F_DISP) {
    aluB = (int)(short)(em.inst.b & 0xffff);
  } else if(de.func == ALU_SHTL) {
    aluB = em.inst.b & 0xff;
  } else {
    aluB = de.aluB; 
  }
  /* alu part */
  switch(de.func) {
    case ALU_ADD:
      em.valE = aluA + aluB;
      break;
    case ALU_SUB:
      em.valE = aluA - aluB;
      break;
    case ALU_AND:
      em.valE = aluA & aluB;
      break;
    case ALU_OR:
      em.valE = aluA | aluB;    
      break;
    case ALU_SLT:
      em.valE = aluA < aluB;      
      break;
    case ALU_SHTL:
      em.valE = aluA << aluB;
      break;
    default:
      em.valE = 0;
      break;
  }
  if(ctl.flag) {
    if(em.valE) {    
      ctl.cond |= 2;
    } else {
      ctl.flag = FALSE;
    }
  }
}

void do_mem() {
  enum md_fault_type _fault;
  
  mw.inst = em.inst;
  mw.dstE = em.dstE;
  mw.dstM = em.dstM;
  mw.valE = em.valE;
  mw.PC = em.PC;
  mw.rw = em.rw;
  if(mw.rw&4) { /* load */
    mw.valM = READ_WORD(mw.valE, _fault);
    printf("src: %d, val: %d", mw.valE, mw.valM);    
    ctl.regs &= ~(1 << mw.dstM);           
  } else if(mw.rw&2) { /* save */
    printf("dst: %d, val: %d", mw.valE, em.valA);
    WRITE_WORD(em.valA, mw.valE, _fault);
  }
}                                                                                        

void do_wb() {
  wb.inst = mw.inst;
  wb.PC = mw.PC;
  wb.dstE = mw.dstE;
  wb.dstM = mw.dstM;
  wb.valE = mw.valE;
  wb.valM = mw.valM;
  if(mw.dstM != DNA) {
    SET_GPR(mw.dstM, mw.valM);
  }
  if(mw.dstE != DNA) {
    SET_GPR(mw.dstE, mw.valE);
  }
  if(wb.inst.a == SYSCALL){
    SYSCALL(wb.inst);
  }
}

void do_log() {
  enum md_fault_type _fault;

  fprintf(stream, "[Cycle %4lld]---------------------------------------", sim_num_insn);
  fprintf(stream, "\n[IF]\t");
  md_print_insn(fd.inst, fd.PC, stream);
  fprintf(stream, "\n[ID]\t");
  md_print_insn(de.inst, de.PC, stream);
  fprintf(stream, "\n[EX]\t");
  md_print_insn(em.inst, em.PC, stream);
  fprintf(stream, "\n[MEM]\t");
  md_print_insn(mw.inst, mw.PC, stream);
  fprintf(stream, "\n[WB]\t");
  md_print_insn(wb.inst, wb.PC, stream);
  fprintf(stream, "\n[REGS]\t");
  fprintf(stream, "r[2]=%d r[3]=%d r[4]=%d r[5]=%d r[6]=%d mem=%d\n",
          GPR(2), GPR(3), GPR(4), GPR(5), GPR(6), READ_WORD(GPR(30)+16, _fault));
  fprintf(stream, "---------------------------------------------------\n");
  if (_fault != md_fault_none)
    DECLARE_FAULT(_fault);
}

void init_fd() {
  fd.inst.a = NOP;
  fd.PC = 0;  
}
void init_de() {
  de.inst.a = NOP;
  de.PC = 0;
  de.srcA = 0;
  de.srcB = 0;
  de.aluA = 0;
  de.aluB = 0;
  de.valA = 0;
  de.dstE = DNA;
  de.dstM = DNA;
  de.rw = 0;
}
void init_em() {
  em.inst.a = NOP;
  em.PC = 0;
  em.valE = 0;
  em.valA = 0;
  em.dstE = DNA;
  em.dstM = DNA;
  em.rw = 0;
}
void init_mw() {
  mw.inst.a = NOP;
  mw.PC = 0;
  mw.valE = 0;
  mw.valM = 0;
  mw.dstE = DNA;
  mw.dstM = DNA;
  mw.rw = 0;
}
void init_wb() {
  wb.inst.a = NOP;
  wb.PC = 0;
  wb.valE = 0;
  wb.valM = 0;
  wb.dstE = DNA;
  wb.dstM = DNA;
}
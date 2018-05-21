/*
 * Author: syang2forever
 * Mail: syang2forever@gmail.com
 * Blog: syang2forever.github.io
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
#include "ucache.h"

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

counter_t sim_num_clk;
FILE *stream;

struct ifid_buf fd;
struct idex_buf de;
struct exmem_buf em;
struct memwb_buf mw;
struct wb_buf wb;

struct control_buf ctl;
cache_t cache;

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
  ctl.cmp = 0;
  ctl.cond = 0;
  ctl.regs = 0;
  ctl.stall = 0;

  cache.enable = 1;
  cache.access = 0;
  cache.hit = 0;
  cache.miss = 0;
  cache.replace = 0;
  cache.wb = 0;

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

#define INC_CYCLE(NUM) (sim_num_clk+=NUM)

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
    INC_INSN_CTR();
    INC_CYCLE(1);
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
    if(ctl.cond&2){
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
  int cycles = 10;
  if(cache.enable) {
      cycles = cache_read(&cache, fd.PC, &(instruction.a));
      cycles += cache_read(&cache, fd.PC+4, &(instruction.b));
  } else {
      MD_FETCH_INSTI(instruction, mem, fd.PC);
  }
  fd.inst = instruction;
  INC_CYCLE(cycles);
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
    case LUI:
      de.instFlags |= F_IMM;
    case LW:
    case SW:
    case ADD:
    case ADDI:
    case ADDU:
    case ADDIU:    
      de.func = ALU_ADD;
      break;
    case BNE:
      ctl.flag = TRUE;
      ctl.cmp = 1;
      de.target = fd.PC + 8 + ((de.inst.b & 0xffff) << 2);
      de.func = ALU_SUB;
      break;
    case BEQ:
      ctl.flag = TRUE;
      ctl.cmp = 0;
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
      de.func = ALU_NOP;      
      break;
    case MFLO:
      SET_GPR(de.oprand.out1, LO);
    case MULTU:
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
  if(em.inst.a == NOP) {
    return;
  }
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
  /* multu part */
  if(de.opcode == MULTU) {
    int i;
    SET_HI(0);
    SET_LO(0);
    if (aluB & 020000000000){
      SET_LO(aluA);
    }
    for (i = 0; i < 31; i++) {		
      SET_HI(HI << 1);
      SET_HI(HI + extractl(LO, 31, 1));
      SET_LO(LO << 1);
      if ((extractl(aluB, 30 - i, 1)) == 1){
        if (((unsigned)037777777777 - (unsigned)LO) < (unsigned)aluA){
          SET_HI(HI + 1);
        }
        SET_LO(LO + aluA);
      }
    }
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
    if((ctl.cmp && em.valE) || (!ctl.cmp && !em.valE)) {    
      ctl.cond |= 2;
    } else {
      ctl.flag = FALSE;
    }
  }
}

void do_mem() {  
  enum md_fault_type _fault;
  mw.inst = em.inst;
  if(mw.inst.a == NOP) {
    return;
  }
  mw.dstE = em.dstE;
  mw.dstM = em.dstM;
  mw.valE = em.valE;
  mw.PC = em.PC;
  mw.rw = em.rw;
  int cycles = 0;    
  if(mw.rw&4) { /* load */
    if(cache.enable){
      cycles = cache_read(&cache, mw.valE, (word_t *)&mw.valM);
    } else {
      mw.valM = READ_WORD(mw.valE, _fault);
      if(_fault != md_fault_none){
        DECLARE_FAULT(_fault);
      }
      cycles = 10;
    }
    ctl.regs &= ~(1 << mw.dstM);
  } else if(mw.rw&2) { /* save */
    if(cache.enable){
      cycles = cache_write(&cache, mw.valE, (word_t *)&em.valA);
    } else {
      WRITE_WORD(em.valA, mw.valE, _fault);
      if(_fault != md_fault_none){
        DECLARE_FAULT(_fault);
      }
      cycles = 10;                        
    }
  }
  INC_CYCLE(cycles);
}                                                                                        

void do_wb() {
  wb.inst = mw.inst;
  if(wb.inst.a == NOP) {
    return;
  }
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
    cache_flush(&cache);
    cache_log();
    SYSCALL(wb.inst);
  }
}

/**
 * Log Part
 */

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
    fprintf(stream, "\n[REGS]\n");
    fprintf(stream, "r[0]=%x r[1]=%x r[2]=%x r[3]=%x r[4]=%x r[5]=%x r[6]=%x r[7]=%x\n",
      GPR(0), GPR(1), GPR(2), GPR(3), GPR(4), GPR(5), GPR(6), GPR(7), _fault);
    fprintf(stream, "r[8]=%x r[9]=%x r[10]=%x r[11]=%x r[12]=%x r[13]=%x r[14]=%x r[15]=%x\n",
      GPR(8), GPR(9), GPR(10), GPR(11), GPR(12), GPR(13), GPR(14), GPR(15), _fault);
    fprintf(stream, "r[16]=%x r[17]=%x r[18]=%x r[19]=%x r[20]=%x r[21]=%x r[22]=%x r[23]=%x\n",
      GPR(16), GPR(17), GPR(18), GPR(19), GPR(20), GPR(21), GPR(22), GPR(23), _fault);
    fprintf(stream, "r[24]=%x r[25]=%x r[26]=%x r[27]=%x r[28]=%x r[29]=%x r[30]=%x r[31]=%x\n",
      GPR(24), GPR(25), GPR(26), GPR(27), GPR(28), GPR(29), GPR(30), GPR(31), _fault);
    fprintf(stream, "---------------------------------------------------\n");
    if (_fault != md_fault_none) {
      DECLARE_FAULT(_fault);
    }
}

void cache_log() {
  enum md_fault_type _fault;
  fprintf(stream, "Clock Cycles: %d\n", sim_num_clk);
  fprintf(stream, "Memory Accesses: %d\n", cache.access);
  fprintf(stream, "Memory Hits: %d\n", cache.hit);
  fprintf(stream, "Memory Misses: %d\n", cache.miss);
  fprintf(stream, "Line Replacements: %d\n", cache.replace);
  fprintf(stream, "Line Write-backs: %d\n", cache.wb);
  if(_fault != md_fault_none){
    DECLARE_FAULT(_fault);
  }
}

/**
 * Init Part 
 */
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

/**
 * Cache Part  
 * Update time: Thu May 17 2018 09:59:35 GMT+0800
 */

void en_cache_set(cache_set_t *set, cache_line_t *line) {
    if (set->tail != NULL) {
        set->tail->next = line;
    }
    set->tail = line;
    if (set->head == NULL) {
        set->head = line;
    }
    set->n += 1;
}

void de_cache_set(cache_set_t *set) {
    if (set->n == 0) {
        return;
    }
    cache_line_t *head = set->head;
    set->head = head->next;
    set->n -= 1;
    if (set->n == 0) {
        set->tail = NULL;
    }
    free(head);
}

void cache_word_read(cache_line_t *line, word_t *dst, int offset) {
    memcpy(dst, (void *)(&line->data)+offset, sizeof(word_t));   
}

void cache_word_write(cache_line_t *line, word_t *src, int offset) {
    memcpy((void *)(&line->data)+offset, src, sizeof(word_t));
    line->dirty = 1;
}

int cache_access(cache_t *cache, md_addr_t addr, word_t *word, cache_word_func func) {
  unsigned int tag = ADDR_TAG(addr);
  unsigned int index = ADDR_INDEX(addr);
  unsigned int offset = ADDR_OFFSET(addr);

  cache_set_t *set = &cache->sets[index];
  cache_line_t *line = NULL;
  md_addr_t align_addr = addr & (~0xF);
  int cycles = 1;
  int miss = 1;
  cache->access++;
  for(line = set->head; line != NULL; line = line->next) {
    if(line->valid && tag == line->tag) {
      miss = 0;
      line->ref_count++;
      cache->hit++;
      func(line, word, offset);
    }
  }
  // miss here
  if(miss) {
    cycles = 10;
    cache->miss++;
    line = malloc_cache_line(align_addr);
    add_into_cache_set(set, line, index);
    func(line, word, offset);
  }
  return cycles;
}

int cache_read(cache_t *cache, md_addr_t addr, word_t *word) {
  return cache_access(cache, addr, word, cache_word_read);
}

int cache_write(cache_t *cache, md_addr_t addr, word_t *word) {
  return cache_access(cache, addr, word, cache_word_write);
}

void cache_flush(cache_t* cache) {
  cache_set_t *set;
  cache_line_t *line;
  int i;
  for(i = 0; i < 16; i++) {
    set = &cache->sets[i];
    for(line = set->head; line != NULL; line = line->next) {
      if(line->dirty) {
        cache_write_back(line, i);
      }
    }
  }
}

cache_line_t *malloc_cache_line(md_addr_t addr) {
  cache_line_t *line = malloc(sizeof(cache_line_t));
  int i;
  enum md_fault_type _fault;    
  for(i = 0; i < 4; i++) {
    line->data[i] = READ_WORD(addr+(i*4), _fault);
  }
  if(_fault != md_fault_none) {
    panic("Memory Write Error!");
  }
  line->ref_count = 0;
  line->tag = ADDR_TAG(addr);
  line->dirty = 0;
  line->valid = 1;
  line->next = NULL;
  return line;
}

void cache_write_back(cache_line_t *line, int index) {
  md_addr_t addr = (line->tag<<8) | (index<<4);
  int i;
  enum md_fault_type _fault;
  for(i = 0; i < 4; i++) {
    WRITE_WORD(line->data[i], addr+(i*4), _fault);
  }
  line->dirty = 0;    
  if(_fault != md_fault_none) {
    panic("MEMORY ERROR!");
  }
}

void add_into_cache_set(cache_set_t *set, cache_line_t *line, int index) {
  if(set->n >= SET_NUM) {
    if(set->head->dirty) {
      cache.wb++;
      cache_write_back(set->head, index);
    }
    cache.replace++;
    de_cache_set(set);
  }
  en_cache_set(set, line);
}

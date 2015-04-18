/* sim-safe.c - sample functional simulator implementation */

/* SimpleScalar(TM) Tool Suite
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 * All Rights Reserved. 
 * 
 * THIS IS A LEGAL DOCUMENT, BY USING SIMPLESCALAR,
 * YOU ARE AGREEING TO THESE TERMS AND CONDITIONS.
 * 
 * No portion of this work may be used by any commercial entity, or for any
 * commercial purpose, without the prior, written permission of SimpleScalar,
 * LLC (info@simplescalar.com). Nonprofit and noncommercial use is permitted
 * as described below.
 * 
 * 1. SimpleScalar is provided AS IS, with no warranty of any kind, express
 * or implied. The user of the program accepts full responsibility for the
 * application of the program and the use of any results.
 * 
 * 2. Nonprofit and noncommercial use is encouraged. SimpleScalar may be
 * downloaded, compiled, executed, copied, and modified solely for nonprofit,
 * educational, noncommercial research, and noncommercial scholarship
 * purposes provided that this notice in its entirety accompanies all copies.
 * Copies of the modified software can be delivered to persons who use it
 * solely for nonprofit, educational, noncommercial research, and
 * noncommercial scholarship purposes provided that this notice in its
 * entirety accompanies all copies.
 * 
 * 3. ALL COMMERCIAL USE, AND ALL USE BY FOR PROFIT ENTITIES, IS EXPRESSLY
 * PROHIBITED WITHOUT A LICENSE FROM SIMPLESCALAR, LLC (info@simplescalar.com).
 * 
 * 4. No nonprofit user may place any restrictions on the use of this software,
 * including as modified by the user, by any other authorized user.
 * 
 * 5. Noncommercial and nonprofit users may distribute copies of SimpleScalar
 * in compiled or executable form as set forth in Section 2, provided that
 * either: (A) it is accompanied by the corresponding machine-readable source
 * code, or (B) it is accompanied by a written offer, with no time limit, to
 * give anyone a machine-readable copy of the corresponding source code in
 * return for reimbursement of the cost of distribution. This written offer
 * must permit verbatim duplication by anyone, or (C) it is distributed by
 * someone who received only the executable form, and is accompanied by a
 * copy of the written offer of source code.
 * 
 * 6. SimpleScalar was developed by Todd M. Austin, Ph.D. The tool suite is
 * currently maintained by SimpleScalar LLC (info@simplescalar.com). US Mail:
 * 2395 Timbercrest Court, Ann Arbor, MI 48105.
 * 
 * Copyright (C) 1994-2003 by Todd M. Austin, Ph.D. and SimpleScalar, LLC.
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "options.h"
#include "stats.h"
#include "sim.h"

/*
 * This file implements a functional simulator.  This functional simulator is
 * the simplest, most user-friendly simulator in the simplescalar tool set.
 * Unlike sim-fast, this functional simulator checks for all instruction
 * errors, and the implementation is crafted for clarity rather than speed.
 */

static counter_t g_icache_miss;
static counter_t g_timestamp;

static counter_t sim_num_loads;
static counter_t sim_num_stores;

static counter_t sim_num_load_misses;
static counter_t sim_num_store_misses;

static counter_t sim_num_writebacks;

/* simulated registers */
static struct regs_t regs;

/* simulated memory */
static struct mem_t *mem = NULL;

/* track number of refs */
static counter_t sim_num_refs = 0;

/* maximum number of inst's to execute */
static unsigned int max_insts;

/* register simulator-specific options */
void
sim_reg_options(struct opt_odb_t *odb)
{
  opt_reg_header(odb, 
"sim-safe: This simulator implements a functional simulator.  This\n"
"functional simulator is the simplest, most user-friendly simulator in the\n"
"simplescalar tool set.  Unlike sim-fast, this functional simulator checks\n"
"for all instruction errors, and the implementation is crafted for clarity\n"
"rather than speed.\n"
		 );

  /* instruction limit */
  opt_reg_uint(odb, "-max:inst", "maximum number of inst's to execute",
	       &max_insts, /* default */0,
	       /* print */TRUE, /* format */NULL);

}

/* check simulator-specific option values */
void
sim_check_options(struct opt_odb_t *odb, int argc, char **argv)
{
  /* nada */
}

/* register simulator-specific statistics */
void
sim_reg_stats(struct stat_sdb_t *sdb)
{
  stat_reg_counter(sdb, "sim_num_insn",
		   "total number of instructions executed",
		   &sim_num_insn, sim_num_insn, NULL);
  stat_reg_counter(sdb, "sim_num_refs",
		   "total number of loads and stores executed",
		   &sim_num_refs, 0, NULL);
  stat_reg_int(sdb, "sim_elapsed_time",
	       "total simulation time in seconds",
	       &sim_elapsed_time, 0, NULL);
  stat_reg_formula(sdb, "sim_inst_rate",
		   "simulation speed (in insts/sec)",
		   "sim_num_insn / sim_elapsed_time", NULL);

  stat_reg_counter(sdb, "sim_num_icache_miss",
                "total number of instruction cache misses",
                &g_icache_miss, 0, NULL);
  stat_reg_formula(sdb, "sim_icache_miss_rate",
                "instruction cache miss rate (percentage)",
                "100*(sim_num_icache_miss / sim_num_insn)", NULL);

  stat_reg_counter(sdb, "sim_num_loads",
		   "total number of loads executed",
		   &sim_num_loads, 0, NULL);

  stat_reg_counter(sdb, "sim_num_stores",
		   "total number of stores executed",
		   &sim_num_stores, 0, NULL);

   stat_reg_counter(sdb, "sim_num_load_misses",
		   "total number of load misses",
		   &sim_num_load_misses, 0, NULL);

  stat_reg_counter(sdb, "sim_num_store_misses",
		   "total number of store misses",
		   &sim_num_store_misses, 0, NULL);

  stat_reg_counter(sdb, "sim_num_writebacks",
		   "total number of writeback events",
		   &sim_num_store_writebacks, 0, NULL);  

  stat_reg_formula(sdb, "sim_load_miss_rate",
		   "simulation load miss rate",
		   "sim_num_loads / sim_num_load_misses", NULL);

  stat_reg_formula(sdb, "sim_store_miss_rate",
		   "simulation store miss rate",
		   "sim_num_stores / sim_num_store_misses", NULL);

  stat_reg_formula(sdb, "sim_store_writeback_rate",
		   "simulation writeback rate",
		   "sim_num_writebacks / sim_num_stores", NULL);


  ld_reg_stats(sdb);
  mem_reg_stats(mem, sdb);
}

/* initialize the simulator */
void
sim_init(void)
{
  sim_num_refs = 0;
  g_timestamp = 0;
  sim_num_loads = 0;
  sim_num_stores = 0;
  sim_num_load_misses = 0;
  sim_num_store_misses = 0;
  sim_num_writebacks = 0;

  /* allocate and initialize register file */
  regs_init(&regs);

  /* allocate and initialize memory space */
  mem = mem_create("mem");
  mem_init(mem);
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
sim_aux_config(FILE *stream)		/* output stream */
{
  /* nothing currently */
}

/* dump simulator-specific auxiliary simulator statistics */
void
sim_aux_stats(FILE *stream)		/* output stream */
{
  /* nada */
}

/* un-initialize simulator-specific state */
void
sim_uninit(void)
{
  /* nada */
}


/*
 * configure the execution engine
 */

/*
 * precise architected register accessors
 */

/* next program counter */
#define SET_NPC(EXPR)		(regs.regs_NPC = (EXPR))

/* current program counter */
#define CPC			(regs.regs_PC)

/* general purpose registers */
#define GPR(N)			(regs.regs_R[N])
#define SET_GPR(N,EXPR)		(regs.regs_R[N] = (EXPR))

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

#elif defined(TARGET_ALPHA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_Q(N)		(regs.regs_F.q[N])
#define SET_FPR_Q(N,EXPR)	(regs.regs_F.q[N] = (EXPR))
#define FPR(N)			(regs.regs_F.d[(N)])
#define SET_FPR(N,EXPR)		(regs.regs_F.d[(N)] = (EXPR))

/* miscellaneous register accessors */
#define FPCR			(regs.regs_C.fpcr)
#define SET_FPCR(EXPR)		(regs.regs_C.fpcr = (EXPR))
#define UNIQ			(regs.regs_C.uniq)
#define SET_UNIQ(EXPR)		(regs.regs_C.uniq = (EXPR))

#else
#error No ISA target defined...
#endif

/* precise architected memory state accessor macros */
#define READ_BYTE(SRC, FAULT)						\
  ((FAULT) = md_fault_none, addr = (SRC), MEM_READ_BYTE(mem, addr))
#define READ_HALF(SRC, FAULT)						\
  ((FAULT) = md_fault_none, addr = (SRC), MEM_READ_HALF(mem, addr))
#define READ_WORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, addr = (SRC), MEM_READ_WORD(mem, addr))
#ifdef HOST_HAS_QWORD
#define READ_QWORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, addr = (SRC), MEM_READ_QWORD(mem, addr))
#endif /* HOST_HAS_QWORD */

#define WRITE_BYTE(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, addr = (DST), MEM_WRITE_BYTE(mem, addr, (SRC)))
#define WRITE_HALF(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, addr = (DST), MEM_WRITE_HALF(mem, addr, (SRC)))
#define WRITE_WORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, addr = (DST), MEM_WRITE_WORD(mem, addr, (SRC)))
#ifdef HOST_HAS_QWORD
#define WRITE_QWORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, addr = (DST), MEM_WRITE_QWORD(mem, addr, (SRC)))
#endif /* HOST_HAS_QWORD */

/* system call handler macro */
#define SYSCALL(INST)	sys_syscall(&regs, mem_access, mem, INST, TRUE)

#define DNA         (0)

/* general register dependence decoders */
#define DGPR(N)         (N)
#define DGPR_D(N)       ((N) &~1)

/* floating point register dependence decoders */
#define DFPR_L(N)       (((N)+32)&~1)
#define DFPR_F(N)       (((N)+32)&~1)
#define DFPR_D(N)       (((N)+32)&~1)

/* miscellaneous register dependence decoders */
#define DHI         (0+32+32)
#define DLO         (1+32+32)
#define DFCC            (2+32+32)
#define DTMP            (3+32+32)

struct block {
  int           m_valid; // is block valid?
  int			m_dirty;
  md_addr_t     m_tag;   // tag used to determine whether we have a cache hit
  counter_t m_timestamp;
};

struct cache {
  struct block *m_tag_array;
  unsigned m_total_blocks;
  unsigned m_set_shift;
  unsigned m_set_mask;
  unsigned m_tag_shift;
  unsigned m_nways;
};

void cache_access( struct cache *c, unsigned addr, counter_t *miss_counter, register int is_read, register int is_write) {
    unsigned  index, tag;
    unsigned  set_index;

    int i;
    int j;
    int k;
    int nways_shift;
    int block_miss;
    int block_evict;
    int block_timestamp;
    int block_LRU;

    block_miss = 0;		// reset block miss counter
    block_evict = 0;	// reset block evict bit
    nways_shift = log(c->m_nways)/log(2);		// calculate shift amount for associative sets
    index = (addr>>c->m_set_shift)&c->m_set_mask;
    // printf("\n\n**** Cash Access ****");
    // printf("\nindex=%d", index);
    set_index = index<<nways_shift;	// shift over to include associative sets
    // printf("\nset_index=%d", set_index);

    tag = (addr>>c->m_tag_shift);

    g_timestamp++;		// increase timestamp

    // printf("\ng_timestamp=%d", g_timestamp);

    assert( set_index < c->m_total_blocks );

    for(i=0 ; i<c->m_nways ; i++){		// iterate through each block in a set 
    	// printf("\nset_index+i=%d", set_index+i);
    	if(!(c->m_tag_array[set_index+i].m_valid&&(c->m_tag_array[set_index+i].m_tag==tag))){	// check if block is valid and has the same tag
    		block_miss++;				// if not, increase block_miss counter
    	}else{
    		c->m_tag_array[set_index+i].m_timestamp = g_timestamp;	// if yes, cache hit and update blocks timestamp
    		if(is_write){
    			c->m_tag_array[set_index+i].m_dirty = 1;	// writeback, set dirty bit
    		}
    		// printf("\n*Cash Hit*");
    	}
    }

    // printf("\nblock_miss=%d", block_miss);


    if(block_miss == c->m_nways){		// check if block misses is equal to number of blocks in a set, indicating a cache miss
    	*miss_counter = *miss_counter + 1;
    	// printf("\n*Cash Miss*");
    	for(j=0 ; j<c->m_nways ; j++){	// iterate through each block in a set 
    		if(!(c->m_tag_array[set_index+j].m_valid)){		// check if block is invalid, and then fill block and update timestamp
    			c->m_tag_array[set_index+j].m_valid = 1;
    			c->m_tag_array[set_index+j].m_tag = tag;
    			c->m_tag_array[set_index+j].m_timestamp = g_timestamp;
    			if(is_write){
    				c->m_tag_array[set_index+j].m_dirty = 1;	// writeback, set dirty bit
    			}
    			block_evict = 1;	// no block needs evicting
    			// printf("\nset_index+j=%d", set_index+j);
    			break;
    		}
    	}
    	if(!(block_evict == 1)){	// if no invalid blocks and therefore a block needs evicting
			block_timestamp = c->m_tag_array[set_index].m_timestamp;	// record timestamp of first block in set
    		block_LRU = set_index;	// record index of first block in set
    		for(k=0 ; k<c->m_nways ; k++){	// iterate through each block in a set
    			if(c->m_tag_array[set_index+k].m_timestamp < block_timestamp){	// compare timestamp values and determine least recently used block
    				block_timestamp = c->m_tag_array[set_index+k].m_timestamp;
    				block_LRU = set_index+k;
    			}
    		}
    		// printf("\nblock_LRU=%d", block_LRU);
    		if(is_read&&is_write){		// if reading or loading to cache and evicting a block
    			if(c->m_tag_array[block_LRU].m_dirty){	// if block is dirty(needs to be written back to memory)
    				sim_num_writebacks++;	// writeback event
    			}
    		}
    		c->m_tag_array[block_LRU].m_valid = 1;	// evict and fill least recently used block and update timestamp
    		c->m_tag_array[block_LRU].m_tag = tag;
    		c->m_tag_array[block_LRU].m_timestamp = g_timestamp;
    		if(is_write){
    			c->m_tag_array[block_LRU].m_dirty = 1;	// writeback, set dirty bit
    		}
    	}
    } 
}



/* start simulation, program loaded, processor precise state initialized */
void
sim_main(void)
{
  md_inst_t inst;
  register md_addr_t addr;
  enum md_opcode op;
  register int is_read;
  register int is_write;
  enum md_fault_type fault;

	struct cache *icache = (struct cache *) calloc( sizeof(struct cache), 1 );
	icache->m_tag_array = (struct block *) calloc( sizeof(struct block), 1024 );
	icache->m_total_blocks = 1024;
	icache->m_set_shift = 5;
	icache->m_set_mask = (1<<8)-1;
	icache->m_tag_shift = 13;
	icache->m_nways = 4;

  fprintf(stderr, "sim: ** starting functional simulation **\n");

  /* set up initial default next PC */
  regs.regs_NPC = regs.regs_PC + sizeof(md_inst_t);


  while (TRUE)
    {
      /* maintain $r0 semantics */
      regs.regs_R[MD_REG_ZERO] = 0;
#ifdef TARGET_ALPHA
      regs.regs_F.d[MD_REG_ZERO] = 0.0;
#endif /* TARGET_ALPHA */





      /* get the next instruction to execute */
      MD_FETCH_INST(inst, mem, regs.regs_PC);

      /* keep an instruction count */
      sim_num_insn++;

      /* set default reference address and access mode */
      addr = 0; is_write = FALSE;

      /* set default fault - none */
      fault = md_fault_none;

      /* decode the instruction */
      MD_SET_OPCODE(op, inst);

      /* execute the instruction */
      switch (op)
	{
#define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3)		\
	case OP:							\
          SYMCAT(OP,_IMPL);						\
          break;
#define DEFLINK(OP,MSK,NAME,MASK,SHIFT)					\
        case OP:							\
          panic("attempted to execute a linking opcode");
#define CONNECT(OP)
#define DECLARE_FAULT(FAULT)						\
	  { fault = (FAULT); break; }
#include "machine.def"
	default:
	  panic("attempted to execute a bogus opcode");
      }

      if (fault != md_fault_none)
	fatal("fault (%d) detected @ 0x%08p", fault, regs.regs_PC);

      if (verbose)
	{
	  myfprintf(stderr, "%10n [xor: 0x%08x] @ 0x%08p: ",
		    sim_num_insn, md_xor_regs(&regs), regs.regs_PC);
	  md_print_insn(inst, regs.regs_PC, stderr);
	  if (MD_OP_FLAGS(op) & F_MEM)
	    myfprintf(stderr, "  mem: 0x%08p", addr);
	  fprintf(stderr, "\n");
	  /* fflush(stderr); */
	}

	


      if (MD_OP_FLAGS(op) & F_MEM)
	{
	  sim_num_refs++;
	  if(((MD_OP_FLAGS(op)&F_LOAD)!=0)){	// check if load instruction
	  		sim_num_loads++;
	  		is_read = TRUE;
	  }
	  if(((MD_OP_FLAGS(op)&F_STORE)!=0)){	// check if store instruction
	  		sim_num_stores++;
	  		is_write = TRUE;
	  }
	}

	cache_access(icache, regs.regs_PC, &g_icache_miss, is_read, is_write);

      /* go to the next instruction */
      regs.regs_PC = regs.regs_NPC;
      regs.regs_NPC += sizeof(md_inst_t);

      /* finish early? */
      if (max_insts && sim_num_insn >= max_insts)
	return;
    }
}

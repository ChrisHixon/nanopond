/* *********************************************************************** */
/*                                                                         */
/* Nanopond CH -- Copyright (C) 2014-2015 Chris Hixon                      */
/* based upon Nanopond version 1.9, Copyright (C) 2005 Adam Ierymenko      */
/*                                                                         */
/* *********************************************************************** */
/*                                                                         */
/* Nanopond version 1.9 -- A teeny tiny artificial life virtual machine    */
/* Copyright (C) 2005 Adam Ierymenko - http://www.greythumb.com/people/api */
/*                                                                         */
/* This program is free software; you can redistribute it and/or modify    */
/* it under the terms of the GNU General Public License as published by    */
/* the Free Software Foundation; either version 2 of the License, or       */
/* (at your option) any later version.                                     */
/*                                                                         */
/* This program is distributed in the hope that it will be useful,         */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           */
/* GNU General Public License for more details.                            */
/*                                                                         */
/* You should have received a copy of the GNU General Public License       */
/* along with this program; if not, write to the Free Software             */
/* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110 USA     */
/*                                                                         */
/* *********************************************************************** */

/*
 * Changelog:
 *
 * 1.0 - Initial release
 * 1.1 - Made empty cells get initialized with 0xffff... instead of zeros
 *       when the simulation starts. This makes things more consistent with
 *       the way the output buf is treated for self-replication, though
 *       the initial state rapidly becomes irrelevant as the simulation
 *       gets going.  Also made one or two very minor performance fixes.
 * 1.2 - Added statistics for execution frequency and metabolism, and made
 *       the visualization use 16bpp color.
 * 1.3 - Added a few other statistics.
 * 1.4 - Replaced SET with KILL and changed EAT to SHARE. The SHARE idea
 *       was contributed by Christoph Groth (http://www.falma.de/). KILL
 *       is a variation on the original EAT that is easier for cells to
 *       make use of.
 * 1.5 - Made some other instruction changes such as XCHG and added a
 *       penalty for failed KILL attempts. Also made access permissions
 *       stochastic.
 * 1.6 - Made cells all start facing in direction 0. This removes a bit
 *       of artificiality and requires cells to evolve the ability to
 *       turn in various directions in order to reproduce in anything but
 *       a straight line. It also makes pretty graphics.
 * 1.7 - Added more statistics, such as original lineage, and made the
 *       genome dump files CSV files as well.
 * 1.8 - Fixed LOOP/REP bug reported by user Sotek.  Thanks!  Also
 *       reduced the default mutation rate a bit.
 * 1.9 - Added a bunch of changes suggested by Christoph Groth: a better
 *       coloring algorithm, right click to switch coloring schemes (two
 *       are currently supported), and a few speed optimizations. Also
 *       changed visualization so that cells with generations less than 2
 *       are no longer shown.
 */

/*
 * Nanopond is just what it says: a very very small and simple artificial
 * life virtual machine.
 *
 * It is a "small evolving program" based artificial life system of the same
 * general class as Tierra, Avida, and Archis.  It is written in very tight
 * and efficient C code to make it as fast as possible, and is so small that
 * it consists of only one .c file.
 *
 * How Nanopond works:
 *
 * The Nanopond world is called a "pond."  It is an NxN two dimensional
 * array of Cell structures, and it wraps at the edges (it's toroidal).
 * Each Cell structure consists of a few attributes that are there for
 * statistics purposes, an energy level, and an array of POND_DEPTH
 * four-bit values.  (The four-bit values are actually stored in an array
 * of machine-size words.)  The array in each cell contains the genome
 * associated with that cell, and POND_DEPTH is therefore the maximum
 * allowable size for a cell genome.
 *
 * The first four bit value in the genome is called the "logo." What that is
 * for will be explained later. The remaining four bit values each code for
 * one of 16 instructions. Instruction zero (0x0) is NOP (no operation) and
 * instruction 15 (0xf) is STOP (stop cell execution). Read the code to see
 * what the others are. The instructions are exceptionless and lack fragile
 * operands. This means that *any* arbitrary sequence of instructions will
 * always run and will always do *something*. This is called an evolvable
 * instruction set, because programs coded in an instruction set with these
 * basic characteristics can mutate. The instruction set is also
 * Turing-complete, which means that it can theoretically do anything any
 * computer can do. If you're curious, the instruciton set is based on this:
 * http://www.muppetlabs.com/~breadbox/bf/
 *
 * At the center of Nanopond is a core loop. Each time this loop executes,
 * a clock counter is incremented and one or more things happen:
 *
 * - Every REPORT_FREQUENCY clock ticks a line of comma seperated output
 *   is printed to STDOUT with some statistics about what's going on.
 * - Every DUMP_FREQUENCY clock ticks, all viable replicators (cells whose
 *   generation is >= 2) are dumped to a file on disk.
 * - Every INFLOW_FREQUENCY clock ticks a random x,y location is picked,
 *   energy is added (see INFLOW_RATE_MEAN and INFLOW_RATE_DEVIATION)
 *   and it's genome is filled with completely random bits.  Statistics
 *   are also reset to generation==0 and parentID==0 and a new cell ID
 *   is assigned.
 * - Every tick a random x,y location is picked and the genome inside is
 *   executed until a STOP instruction is encountered or the cell's
 *   energy counter reaches zero. (Each instruction costs one unit energy.)
 *
 * The cell virtual machine is an extremely simple register machine with
 * a single four bit register, one memory pointer, one spare memory pointer
 * that can be exchanged with the main one, and an output buffer. When
 * cell execution starts, this output buffer is filled with all binary 1's
 * (0xffff....). When cell execution is finished, if the first byte of
 * this buffer is *not* 0xff, then the VM says "hey, it must have some
 * data!". This data is a candidate offspring; to reproduce cells must
 * copy their genome data into the output buffer.
 *
 * When the VM sees data in the output buffer, it looks at the cell
 * adjacent to the cell that just executed and checks whether or not
 * the cell has permission (see below) to modify it. If so, then the
 * contents of the output buffer replace the genome data in the
 * adjacent cell. Statistics are also updated: parentID is set to the
 * ID of the cell that generated the output and generation is set to
 * one plus the generation of the parent.
 *
 * A cell is permitted to access a neighboring cell if:
 *    - That cell's energy is zero
 *    - That cell's parentID is zero
 *    - That cell's logo (remember?) matches the trying cell's "guess"
 *
 * Since randomly introduced cells have a parentID of zero, this allows
 * real living cells to always replace them or eat them.
 *
 * The "guess" is merely the value of the register at the time that the
 * access attempt occurs.
 *
 * Permissions determine whether or not an offspring can take the place
 * of the contents of a cell and also whether or not the cell is allowed
 * to EAT (an instruction) the energy in it's neighbor.
 *
 * If you haven't realized it yet, this is why the final permission
 * criteria is comparison against what is called a "guess." In conjunction
 * with the ability to "eat" neighbors' energy, guess what this permits?
 *
 * Since this is an evolving system, there have to be mutations. The
 * MUTATION_RATE sets their probability. Mutations are random variations
 * with a frequency defined by the mutation rate to the state of the
 * virtual machine while cell genomes are executing. Since cells have
 * to actually make copies of themselves to replicate, this means that
 * these copies can vary if mutations have occurred to the state of the
 * VM while copying was in progress.
 *
 * What results from this simple set of rules is an evolutionary game of
 * "corewar." In the beginning, the process of randomly generating cells
 * will cause self-replicating viable cells to spontaneously emerge. This
 * is something I call "random genesis," and happens when some of the
 * random gak turns out to be a program able to copy itself. After this,
 * evolution by natural selection takes over. Since natural selection is
 * most certainly *not* random, things will start to get more and more
 * ordered and complex (in the functional sense). There are two commodities
 * that are scarce in the pond: space in the NxN grid and energy. Evolving
 * cells compete for access to both.
 *
 * If you want more implementation details such as the actual instruction
 * set, read the source. It's well commented and is not that hard to
 * read. Most of it's complexity comes from the fact that four-bit values
 * are packed into machine size words by bit shifting. Once you get that,
 * the rest is pretty simple.
 *
 * Nanopond, for it's simplicity, manifests some really interesting
 * evolutionary dynamics. While I haven't run the kind of multiple-
 * month-long experiment necessary to really see this (I might!), it
 * would appear that evolution in the pond doesn't get "stuck" on just
 * one or a few forms the way some other simulators are apt to do.
 * I think simplicity is partly reponsible for this along with what
 * biologists call embeddedness, which means that the cells are a part
 * of their own world.
 *
 * Run it for a while... the results can be... interesting!
 *
 * Running Nanopond:
 *
 * Nanopond can use SDL (Simple Directmedia Layer) for screen output. If
 * you don't have SDL, comment out USE_SDL below and you'll just see text
 * statistics and get genome data dumps. (Turning off SDL will also speed
 * things up slightly.)
 *
 * After looking over the tunable parameters below, compile Nanopond and
 * run it. Here are some example compilation commands from Linux:
 *
 * For Pentiums:
 *  gcc -O6 -march=pentium -funroll-loops -fomit-frame-pointer -s
 *   -o nanopond nanopond.c -lSDL
 *
 * For Athlons with gcc 4.0+:
 *  gcc -O6 -msse -mmmx -march=athlon -mtune=athlon -ftree-vectorize
 *   -funroll-loops -fomit-frame-pointer -o nanopond nanopond.c -lSDL
 *
 * The second line is for gcc 4.0 or newer and makes use of GCC's new
 * tree vectorizing feature. This will speed things up a bit by
 * compiling a few of the loops into MMX/SSE instructions.
 *
 * This should also work on other Posix-compliant OSes with relatively
 * new C compilers. (Really old C compilers will probably not work.)
 * On other platforms, you're on your own! On Windows, you will probably
 * need to find and download SDL if you want pretty graphics and you
 * will need a compiler. MinGW and Borland's BCC32 are both free. I
 * would actually expect those to work better than Microsoft's compilers,
 * since MS tends to ignore C/C++ standards. If stdint.h isn't around,
 * you can fudge it like this:
 *
 * #define uintptr_t unsigned long (or whatever your machine size word is)
 * #define uint8_t unsigned char
 * #define uint16_t unsigned short
 * #define uint64_t unsigned long long (or whatever is your 64-bit int)
 *
 * When Nanopond runs, comma-seperated stats (see doReport() for
 * the columns) are output to stdout and various messages are output
 * to stderr. For example, you might do:
 *
 * ./nanopond >>stats.csv 2>messages.txt &
 *
 * To get both in seperate files.
 *
 * <plug>
 * Be sure to visit http://www.greythumb.com/blog for your dose of
 * technobiology related news. Also, if you're ever in the Boston
 * area, visit http://www.greythumb.com/bostonalife to find out when
 * our next meeting is!
 * </plug>
 *
 * Have fun!
 */

/* ----------------------------------------------------------------------- */
/* Tunable parameters                                                      */
/* ----------------------------------------------------------------------- */

/* Size of pond in X and Y dimensions. */
#define POND_SIZE_X 640
#define POND_SIZE_Y 480
/*
#define POND_SIZE_X 200
#define POND_SIZE_Y 200
*/

/* Iteration to stop at. Comment this out to run forever. */
/* #define STOP_AT 150000000000ULL */
/* #define STOP_AT 100000000ULL */

/* Frequency of comprehensive reports-- lower values will provide more
 * info while slowing down the simulation. Higher values will give less
 * frequent updates. */
#define REPORT_FREQUENCY    1000000

/* This is the frequency of screen refreshes if SDL is enabled. */
#define REFRESH_FREQUENCY   20000

/* Frequency at which to dump all viable replicators (generation > 2)
 * to a file named <clock>.dump in the current directory.  Comment
 * out to disable. The cells are dumped in hexadecimal, which is
 * semi-human-readable if you look at the big switch() statement
 * in the main loop to see what instruction is signified by each
 * four-bit value. */
#define DUMP_FREQUENCY 10000000

/* Mutation rate -- range is from 0 (none) to 0xffffffff (all mutations!) */
/* To get it from a float probability from 0.0 to 1.0, multiply it by
 * 4294967295 (0xffffffff) and round. */
/*#define MUTATION_RATE 21475 */ /* p=~0.000005 */
#define MUTATION_RATE 100000

/* How frequently should random cells / energy be introduced?
 * Making this too high makes things very chaotic. Making it too low
 * might not introduce enough energy. */
#define INFLOW_FREQUENCY 100

/* Base amount of energy to introduce per INFLOW_FREQUENCY ticks */
#define INFLOW_RATE_BASE 2000


/* A random amount of energy between 0 and this is added to
 * INFLOW_RATE_BASE when energy is introduced. Comment this out for
 * no variation in inflow rate. */
#define INFLOW_RATE_VARIATION 4000

/* Don't add energy to system, or cell, respectively, if energy level is greater than these. */
// #define TOTAL_ENERGY_CAP (12000L * POND_SIZE_X * POND_SIZE_Y)
#define CELL_ENERGY_CAP 10000

/* This is the divisor that determines how much energy is taken
 * from cells when they try to KILL a viable cell neighbor and
 * fail. Higher numbers mean lower penalties. */
#define FAILED_KILL_PENALTY 3

#define REPRODUCTION_COST 20

/* Depth of pond in four-bit codons -- this is the maximum
 * genome size. This *must* be a multiple of 16! */
#define POND_DEPTH 512

/* Define this to use SDL. To use SDL, you must have SDL headers
 * available and you must link with the SDL library when you compile. */
/* Comment this out to compile without SDL visualization support. */
#define USE_SDL 1
#define SDL_TITLE "nanopond-ch"

#define INIT_SEED time(NULL)
//#define INIT_SEED 1111

/* ----------------------------------------------------------------------- */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef USE_SDL
#ifdef _MSC_VER
#include <SDL.h>
#else
#include <SDL/SDL.h>
#endif /* _MSC_VER */
#endif /* USE_SDL */

/* ----------------------------------------------------------------------- */
/* This is the Mersenne Twister by Makoto Matsumoto and Takuji Nishimura   */
/* http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/MT2002/emt19937ar.html  */
/* ----------------------------------------------------------------------- */

/* A few very minor changes were made by me - Adam */

/* 
   A C-program for MT19937, with initialization improved 2002/1/26.
   Coded by Takuji Nishimura and Makoto Matsumoto.

   Before using, initialize the state by using init_genrand(seed)  
   or init_by_array(init_key, key_length).

   Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
   All rights reserved.                          

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

     1. Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.

     2. Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in the
        documentation and/or other materials provided with the distribution.

     3. The names of its contributors may not be used to endorse or promote 
        products derived from this software without specific prior written 
        permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


   Any feedback is very welcome.
   http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
   email: m-mat @ math.sci.hiroshima-u.ac.jp (remove space)
*/

/* Period parameters */  
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */

static unsigned long mt[N]; /* the array for the state vector  */
static int mti=N+1; /* mti==N+1 means mt[N] is not initialized */

/* initializes mt[N] with a seed */
static void init_genrand(unsigned long s)
{
    mt[0]= s & 0xffffffffUL;
    for (mti=1; mti<N; mti++) {
        mt[mti] = 
	    (1812433253UL * (mt[mti-1] ^ (mt[mti-1] >> 30)) + mti); 
        /* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
        /* In the previous versions, MSBs of the seed affect   */
        /* only MSBs of the array mt[].                        */
        /* 2002/01/09 modified by Makoto Matsumoto             */
        mt[mti] &= 0xffffffffUL;
        /* for >32 bit machines */
    }
}

/* generates a random number on [0,0xffffffff]-interval */
static inline uint32_t genrand_int32()
{
    uint32_t y;
    static uint32_t mag01[2]={0x0UL, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */

    if (mti >= N) { /* generate N words at one time */
        int kk;

        for (kk=0;kk<N-M;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        for (;kk<N-1;kk++) {
            y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
            mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
        }
        y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1UL];

        mti = 0;
    }
  
    y = mt[mti++];

    /* Tempering */
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);

    return y;
}

/* ----------------------------------------------------------------------- */

#define INST_BITS 5 
#define NUM_INST (1<<INST_BITS)
#define INST_MASK (NUM_INST-1) 
static const char *inst_chars = "0123456789abcdefghijklmnopqrstuv";
enum {
    /* 1 (0, 1, 2, 3) */
    OP_STOP,
    OP_FWD,
    OP_BACK,
    OP_INC,

    /* 2 (4, 5, 6, 7) */
    OP_DEC,
    OP_READG,
    OP_WRITEG,
    OP_READO,

    /* 3 (8, 9, a, b) */
    OP_WRITEO,
    OP_LOOP,
    OP_REP,
    OP_TURN,

    /* 4 (c, d, e, f) */
    OP_XCHG,
    OP_KILL,
    OP_SHARE,
    OP_ZERO,

    /* 5 (g, h, i, j) */
    OP_SETP,
    OP_NEXTB,
    OP_PREVB,
    OP_NEXTM,

    /* 6 (k, l, m, n) */
    OP_PREVM,
    OP_READM,
    OP_WRITEM,
    OP_CLEARM,

    /* 7 (o, p, q, r) */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,

    /* 8 (s, t, u, v) */
    OP_SHL,
    OP_SHR,
    OP_SETMP,
    OP_RAND
};

/* bits for register and RAM */
#define REG_BITS 8
#define REG_MASK ((1<<REG_BITS)-1) 

/* bits used for logo */
#define LOGO_BITS 5
#define LOGO_MASK ((1<<LOGO_BITS)-1) 

/* bits used for facing */
#define FACING_BITS 5
#define FACING_MASK ((1<<FACING_BITS)-1) 

/* Number of bits in a machine-size word */
#define SYSWORD_BITS (sizeof(uintptr_t) * 8)

/* allocated RAM per cell */
#define RAM_SIZE 16 
/* mask used for direct RAM references */
#define RAM_MASK (RAM_SIZE-1) 

/* mapped RAM per cell */
#define MEM_SIZE 32 
/* mask used for mapped memory references */
#define MEM_MASK (MEM_SIZE-1) 

/* clear RAM on new cells (otherwise it'll be randomized) */
#define CLEAR_RAM 0
/* decay RAM when a cell has no energy  */
#define DECAY_RAM 0

/* Constants representing neighbors in the 2D grid. */
#define DIRECTIONS 6

#if (DIRECTIONS == 4)
#define DIR_NORTH 0
#define DIR_EAST  1
#define DIR_SOUTH 2
#define DIR_WEST  3

#elif (DIRECTIONS == 6)
#define DIR_0 0
#define DIR_1 1
#define DIR_2 2
#define DIR_3 3
#define DIR_4 4
#define DIR_5 5
static const uint8_t dirmap[NUM_INST] = {
    0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 3, 4, 5,
    0, 1, 2, 3, 4, 5,
    0, 1, 2, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5
};

#elif (DIRECTIONS == 8)
#define DIR_NORTH 0
#define DIR_NE    1
#define DIR_EAST  2
#define DIR_SE    3
#define DIR_SOUTH 4
#define DIR_SW    5
#define DIR_WEST  6
#define DIR_NW    7

#else
#error "Please set DIRECTIONS TO 4, 6, or 8"

#endif

/* Sense to use when checking if combination is allowed */
#define COMBINE_SENSE 0

/* Instruction at which to start execution */
#define EXEC_START_INST 0

/* Number of bits set in binary numbers 0000 through 1111 */
/* static const uintptr_t BITS_IN_FOURBIT_WORD[16] = { 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4 }; */
static const uintptr_t BITS_IN_FIVEBIT_WORD[32] = { 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5 };

/**
 * Structure for a cell in the pond
 */
struct Cell
{
  /* Globally unique cell ID */
  uint64_t ID;
  
  /* ID of the cell's parent */
  uint64_t parentID;
  
  /* Counter for original lineages -- equal to the cell ID of
   * the first cell in the line. */
  uint64_t lineage;
  
  /* Generations start at 0 and are incremented from there. */
  uintptr_t generation;
  
  /* Energy level of this cell */
  uintptr_t energy;

  uintptr_t logo;
  uintptr_t facing;

  /* Memory space for cell genome (genome is stored as four
   * bit instructions packed into machine size words) */
  uint8_t genome[POND_DEPTH];

  uint8_t ram[RAM_SIZE];

};

/* The pond is a 2D array of cells */
struct Cell pond[POND_SIZE_X][POND_SIZE_Y];

/* Currently selected color scheme */
enum {
    KINSHIP, LINEAGE,
    LOGO, FACING,
    ENERGY1, ENERGY2,
    RAM0, RAM1,
    MAX_COLOR_SCHEME
} colorScheme = KINSHIP;

const char *colorSchemeName[MAX_COLOR_SCHEME] = {
    "KINSHIP", "LINEAGE",
    "LOGO", "FACING",
    "ENERGY1", "ENERGY2",
    "RAM0", "RAM1"
};

/**
 * Get a random number
 *
 * @return Random number
 */
static inline uintptr_t getRandom()
{
  /* A good optimizing compiler should optimize out this if */
  /* This is to make it work on 64-bit boxes */
  if (sizeof(uintptr_t) == 8)
    return (uintptr_t)((((uint64_t)genrand_int32()) << 32) ^ ((uint64_t)genrand_int32()));
  else return (uintptr_t)genrand_int32();
}

/**
 * Structure for keeping some running tally type statistics
 */
struct PerReportStatCounters
{
  /* Counts for the number of times each instruction was
   * executed since the last report. */
  double instructionExecutions[NUM_INST];
  
  /* Number of cells executed since last report */
  double cellExecutions;

  /* Number of viable cells replaced by other cells' offspring */
  uintptr_t viableCellsReplaced;
  
  /* Number of viable cells KILLed */
  uintptr_t viableCellsKilled;
  
  /* Number of successful SHARE operations */
  uintptr_t viableCellShares;

  /* memory access stats */

  uintptr_t memSpecialReads;
  uintptr_t memPrivateReads;
  uintptr_t memOutputReads;
  uintptr_t memInputReads;

  uintptr_t memSpecialWrites;
  uintptr_t memPrivateWrites;
  uintptr_t memOutputWrites;
  uintptr_t memInputWrites;

};

/* Global statistics counters */
struct PerReportStatCounters statCounters;

/* Highest cell energy seen (updated during report) */
uintptr_t totalEnergy;
uintptr_t maxCellEnergy;
uintptr_t maxLivingCellEnergy;
  

/**
 * Output a line of comma-seperated statistics data
 *
 * @param clock Current clock
 */
static void doReport(const uint64_t clock)
{
  static uint64_t lastTotalViableReplicators = 0;
  
  uintptr_t x,y;
  
  uint64_t totalActiveCells = 0;
  uint64_t totalLivingCells = 0;
  uint64_t totalViableReplicators = 0;

  uint64_t totalLivingEnergy = 0;
  uint64_t totalViableEnergy = 0;

  uintptr_t maxGeneration = 0;

  totalEnergy = 0;
  maxCellEnergy = 0;
  maxLivingCellEnergy = 0;
  
  for(x=0;x<POND_SIZE_X;++x) {
      for(y=0;y<POND_SIZE_Y;++y) {
          struct Cell *const c = &pond[x][y];
          if (c->energy) {
              ++totalActiveCells;
              totalEnergy += (uint64_t)c->energy;
              if (c->energy > maxCellEnergy) {
                  maxCellEnergy = c->energy;
              }
              if (c->generation > 1) {
                  ++totalLivingCells;
                  totalLivingEnergy += c->energy;
                  if (c->energy > maxLivingCellEnergy) {
                      maxLivingCellEnergy = c->energy;
                  }
                  if (c->generation > 2) {
                      ++totalViableReplicators;
                      totalViableEnergy += c->energy;
                  }
              }
              if (c->generation > maxGeneration) {
                  maxGeneration = c->generation;
              }
          }
      }
  }
  
  /* Look here to get the columns in the CSV output */
  
  printf("%ju,%ju,%ju,%ju,%.2f,%.2f,|,%ju,%ju,%ju,%ju,|,%ju,%ju,%ju,%ju,%ju,%ju,%ju,%ju,|,%ju,%ju,%ju,|",
    (uintmax_t)clock,
    (uintmax_t)totalEnergy,
    (uintmax_t)maxCellEnergy,
    (uintmax_t)maxLivingCellEnergy,
    (double)totalLivingEnergy / (double)totalLivingCells,
    (double)totalViableEnergy / (double)totalViableReplicators,

    (uintmax_t)totalActiveCells,
    (uintmax_t)totalLivingCells,
    (uintmax_t)totalViableReplicators,
    (uintmax_t)maxGeneration,

    (uintmax_t)statCounters.memSpecialReads,
    (uintmax_t)statCounters.memPrivateReads,
    (uintmax_t)statCounters.memOutputReads,
    (uintmax_t)statCounters.memInputReads,
    (uintmax_t)statCounters.memSpecialWrites,
    (uintmax_t)statCounters.memPrivateWrites,
    (uintmax_t)statCounters.memOutputWrites,
    (uintmax_t)statCounters.memInputWrites,

    (uintmax_t)statCounters.viableCellsReplaced,
    (uintmax_t)statCounters.viableCellsKilled,
    (uintmax_t)statCounters.viableCellShares
    );
  
  /* The next NUM_INST are the average frequencies of execution for each
   * instruction per cell execution. */
  double totalMetabolism = 0.0;
  for(x=0;x<NUM_INST;++x) {
    totalMetabolism += statCounters.instructionExecutions[x];
    printf(",%.4f",(statCounters.cellExecutions > 0.0) ? (statCounters.instructionExecutions[x] / statCounters.cellExecutions) : 0.0);
  }
  
  /* The last column is the average metabolism per cell execution */
  printf(",%.4f\n",(statCounters.cellExecutions > 0.0) ? (totalMetabolism / statCounters.cellExecutions) : 0.0);
  fflush(stdout);
  
  if ((lastTotalViableReplicators > 0)&&(totalViableReplicators == 0))
    fprintf(stderr,"[EVENT] Viable replicators have gone extinct. Please reserve a moment of silence.\n");
  else if ((lastTotalViableReplicators == 0)&&(totalViableReplicators > 0))
    fprintf(stderr,"[EVENT] Viable replicators have appeared!\n");
  
  lastTotalViableReplicators = totalViableReplicators;
  
  /* Reset per-report stat counters */
  for(x=0;x<sizeof(statCounters);++x)
    ((uint8_t *)&statCounters)[x] = (uint8_t)0;
}

/**
 * Dumps the genome of a cell to a file.
 *
 * @param file Destination
 * @param cell Source
 */
static void dumpCell(FILE *file, struct Cell *cell)
{
    uintptr_t inst, stopCount, i;

    //if (force || (cell->energy && (cell->generation > 2))) {
    fprintf(file, "%ju,%ju,%ju,%ju,%c,%c,",
            (uintmax_t)cell->ID,
            (uintmax_t)cell->parentID,
            (uintmax_t)cell->lineage,
            (uintmax_t)cell->generation,
            inst_chars[cell->logo],
            inst_chars[cell->facing]);
    stopCount = 0;
    for(i = 0; i < POND_DEPTH; ++i) {
        inst = cell->genome[i];
        if (inst == OP_STOP) { /* STOP */
            ++stopCount;
        }
        else {
            stopCount = 0;
        }
        if (stopCount < 5) {
            fprintf(file, "%c", (stopCount > 1) ? '.' : inst_chars[inst]);
        }
    }
    //}
    fprintf(file,"\n");
}

/**
 * Dumps all viable (generation > 2) cells to a file called <clock>.dump
 *
 * @param clock Clock value
 */
static void doDump(const uint64_t clock)
{
    char buf[POND_DEPTH*2];
    FILE *d;
    uintptr_t x, y;
    struct Cell *pptr;

    sprintf(buf,"%ju.dump.csv", (uintmax_t)clock);
    d = fopen(buf,"w");
    if (!d) {
        fprintf(stderr,"[WARNING] Could not open %s for writing.\n",buf);
        return;
    }

    fprintf(stderr,"[INFO] Dumping viable cells to %s\n",buf);

    for(x=0;x<POND_SIZE_X;++x) {
        for(y=0;y<POND_SIZE_Y;++y) {
            pptr = &pond[x][y];
            if (pptr->energy&&(pptr->generation > 2)) {
                dumpCell(d, pptr);
            }
        }
    }

    fclose(d);
}

  
/**
 * Get a neighbor in the pond
 *
 * @param x Starting X position
 * @param y Starting Y position
 * @param dir Direction to get neighbor from
 * @return Pointer to neighboring cell
 */
#define X_EAST  x < (POND_SIZE_X-1) ? x+1 : 0
#define X_WEST  x ? x-1 : POND_SIZE_X-1
#define X_NONE  x
#define Y_SOUTH y < (POND_SIZE_Y-1) ? y+1 : 0
#define Y_NORTH y ? y-1 : POND_SIZE_Y-1
#define Y_NONE  y
static inline struct Cell *getNeighbor(const uintptr_t x, const uintptr_t y, const uintptr_t dir)
{
    /* Space is toroidal; it wraps at edges */

#if (DIRECTIONS == 4)
    switch(dir & 0x3) {
        case DIR_NORTH:
            return &pond[ X_NONE ][ Y_NORTH ];
        case DIR_EAST:
            return &pond[ X_EAST ][ Y_NONE  ];
        case DIR_SOUTH:
            return &pond[ X_NONE ][ Y_SOUTH ];
        case DIR_WEST:
            return &pond[ X_WEST ][ Y_NONE  ];
    }
#elif (DIRECTIONS == 6)
    if (y & 1) {
        switch(dirmap[dir]) {
            case DIR_0:
                return &pond[ X_EAST ][ Y_NORTH ];
            case DIR_1:
                return &pond[ X_EAST ][ Y_NONE ];
            case DIR_2:
                return &pond[ X_EAST ][ Y_SOUTH ];
            case DIR_3:
                return &pond[ X_NONE ][ Y_SOUTH ];
            case DIR_4:
                return &pond[ X_WEST ][ Y_NONE  ];
            case DIR_5:
                return &pond[ X_NONE ][ Y_NORTH ];
        }
    }
    else {
        switch(dirmap[dir]) {
            case DIR_0:
                return &pond[ X_NONE ][ Y_NORTH ];
            case DIR_1:
                return &pond[ X_EAST ][ Y_NONE  ];
            case DIR_2:
                return &pond[ X_NONE ][ Y_SOUTH ];
            case DIR_3:
                return &pond[ X_WEST ][ Y_SOUTH ];
            case DIR_4:
                return &pond[ X_WEST ][ Y_NONE  ];
            case DIR_5:
                return &pond[ X_WEST ][ Y_NORTH ];
        }
    }
#elif (DIRECTIONS == 8)
    switch(dir & 0x7) {
        case DIR_NORTH:
            return &pond[ X_NONE ][ Y_NORTH ];
        case DIR_NE:
            return &pond[ X_EAST ][ Y_NORTH ];
        case DIR_EAST:
            return &pond[ X_EAST ][ Y_NONE  ];
        case DIR_SE:
            return &pond[ X_EAST ][ Y_SOUTH ];
        case DIR_SOUTH:
            return &pond[ X_NONE ][ Y_SOUTH ];
        case DIR_SW:
            return &pond[ X_WEST ][ Y_SOUTH ];
        case DIR_WEST:
            return &pond[ X_WEST ][ Y_NONE  ];
        case DIR_NW:
            return &pond[ X_WEST ][ Y_NORTH ];
    }
#endif
    return &pond[ X_NONE ][ Y_NONE ]; /* This should never be reached */
}
#undef X_EAST
#undef X_WEST
#undef X_NONE
#undef Y_SOUTH
#undef Y_NORTH
#undef Y_NONE

/**
 * Determines if c1 is allowed to access c2
 *
 * @param c2 Cell being accessed
 * @param c1guess c1's "guess"
 * @param sense The "sense" of this interaction
 * @return True or false (1 or 0)
 */
static inline int accessAllowed(struct Cell *const c2,const uintptr_t c1guess,int sense)
{
  /* Access permission is more probable if they are more similar in sense 0,
   * and more probable if they are different in sense 1. Sense 0 is used for
   * "negative" interactions and sense 1 for "positive" ones. */
  return sense ?
      (((getRandom() & 0xf) >= BITS_IN_FIVEBIT_WORD[(c2->logo ^ c1guess) & 0x1f]) || (!c2->parentID)) :
      (((getRandom() & 0xf) <= BITS_IN_FIVEBIT_WORD[(c2->logo ^ c1guess) & 0x1f]) || (!c2->parentID));
}

/**
 * Gets the color that a cell should be
 *
 * @param c Cell to get color for
 * @return 8-bit color value
 */
static inline uint8_t getColor(struct Cell *c)
{
  uintptr_t i, sum; /* inst, stopCount, skipnext; */

  if (c->energy) {
    switch(colorScheme) {
      case KINSHIP:
        /*
         * Kinship color scheme by Christoph Groth
         *
         * For cells of generation > 1, saturation and value are set to maximum.
         * Hue is a hash-value with the property that related genomes will have
         * similar hue (but of course, as this is a hash function, totally
         * different genomes can also have a similar or even the same hue).
         * Therefore the difference in hue should to some extent reflect the grade
         * of "kinship" of two cells.
         */
        if (c->generation > 1) {
          sum = 0;
#if 0
          skipnext = 0;
          stopCount = 0;
#endif
          for(i = 0; i < POND_DEPTH; ++i) {
              sum += c->genome[i];

#if 0
              inst = c->genome[i];

              if (skipnext) {
                  skipnext = 0;
              }
              else {
                  if (inst != OP_STOP) {
                      sum += inst;
                  }

                  if (inst == OP_STOP) { /* STOP */
                      if (++stopCount >= 4) {
                          break;
                      }
                  }
                  else {
                      stopCount = 0;
                      if (inst == 0xc) /* 0xc == XCHG */
                          skipnext = 1; /* Skip "operand" after XCHG */
                  }
              }
#endif
          }
          /* For the hash-value use a wrapped around sum of the sum of all
           * commands and the length of the genome. */
          return (uint8_t)((sum % 192) + 64);
        }
        return 0;
      case LINEAGE:
        /*
         * Cells with generation > 1 are color-coded by lineage.
         */
        return (c->generation > 1) ? (((uint8_t)c->lineage) | (uint8_t)1) : 0;
      case LOGO:
        return (c->generation > 1) ? (uint8_t)(73 + c->logo) : 0;
      case FACING:
        return (c->generation > 1) ? (uint8_t)(157 + c->facing) : 0;
      case ENERGY1:
        return (c->generation > 1 && maxLivingCellEnergy > 0) ?
            (uint8_t)(255.0 * ((double)c->energy / (double)maxLivingCellEnergy)) : 0;
      case ENERGY2:
        return (maxCellEnergy > 0) ?
            (uint8_t)(255.0 * ((double)c->energy / (double)maxCellEnergy)) : 0;
      case RAM0:
        if (c->generation > 1) {
          sum = 0;
          for(i = 0; i < 8; ++i) {
              sum += c->ram[i];
          }
          return (uint8_t)((sum & 0x7f) + 128);
        }
        return 0;
      case RAM1:
        if (c->generation > 1) {
          sum = 0;
          for(i = 0; i < 8; ++i) {
              sum += c->ram[8 + i];
          }
          return (uint8_t)((sum & 0x7f) + 128);
        }
        return 0;
      case MAX_COLOR_SCHEME:
        /* ... never used... to make compiler shut up. */
        break;
    }
  }
  return 0; /* Cells with no energy are black */
}

static inline uint8_t read_mem(struct Cell *c, const uintptr_t x, const uintptr_t y, const uintptr_t ptr_mem)
{
    switch (ptr_mem) {
        case 0x00: /* logo */
            statCounters.memSpecialReads++;
            return c->logo;
        case 0x01: /* facing */
            statCounters.memSpecialReads++;
            return c->facing;
        case 0x02: /* energy */
            statCounters.memSpecialReads++;
            if (c->energy == 0)
                return 0;
            else if (c->energy > 126975)
                return 31;
            else
                return 1 + (c->energy >> 12); 
        case 0x03:
            return c->lineage & REG_MASK;
        case 0x04:
            return c->ID & REG_MASK;
        case 0x05:
            return c->parentID & REG_MASK;
        case 0x06:
            return (c->generation >> REG_BITS) & REG_MASK;
        case 0x07:
            return c->generation & REG_MASK;
            break;

        case 0x08: /* private RAM */
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            statCounters.memPrivateReads++;
            return c->ram[ptr_mem & 0x7];

        case 0x10: /* public RAM */
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            statCounters.memOutputReads++;
            return c->ram[8 + (ptr_mem & 0x7)];

        case 0x18: /* neighbor (facing) public ram */
        case 0x19:
        case 0x1a:
        case 0x1b:
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x1f:
            {
                statCounters.memInputReads++;
                struct Cell *tmpptr = getNeighbor(x, y, c->facing);
                return tmpptr->ram[8 + (ptr_mem & 0x7)];
            }
    }
    return 0;
}

static inline void write_mem(struct Cell *c, const uintptr_t x, const uintptr_t y, 
        const uintptr_t ptr_mem, const uintptr_t value)
{
    switch (ptr_mem) {
        case 0x00: /* logo */
            statCounters.memSpecialWrites++;
            c->logo = value & LOGO_MASK;
            break;
        case 0x01: /* facing */
            statCounters.memSpecialWrites++;
            c->facing = value & FACING_MASK;
            break;

        case 0x02: /* read only */
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            statCounters.memSpecialWrites++;
            break;

        case 0x08: /* private RAM */
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            statCounters.memPrivateWrites++;
            c->ram[ptr_mem & 0x7] = value & REG_MASK;
            break;

        case 0x10: /* public RAM */
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
            statCounters.memOutputWrites++;
            c->ram[8 + (ptr_mem & 0x7)] = value & REG_MASK;
            break;

        case 0x18: /* neighbor (facing) public ram (if permitted by logo) */
        case 0x19:
        case 0x1a:
        case 0x1b:
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x1f:
            {
                statCounters.memInputWrites++;
                struct Cell *tmpptr = getNeighbor(x, y, c->facing);
                if (accessAllowed(tmpptr, c->logo, 1)) {
                    tmpptr->ram[8 + (ptr_mem & 0x7)] = value & REG_MASK;
                }
            }
            break;
    }
}

/**
 * Main method
 *
 * @param argc Number of args
 * @param argv Argument array
 */
int main(int argc,char **argv)
{
  uintptr_t i, x, y;
  
  /* Buffer used for execution output of candidate offspring */
  uint8_t outputBuf[POND_DEPTH];
  
  /* Seed and init the random number generator */
  init_genrand(INIT_SEED);
  for(i=0;i<1024;++i)
    getRandom();

  /* Reset per-report stat counters */
  for(x=0;x<sizeof(statCounters);++x)
    ((uint8_t *)&statCounters)[x] = (uint8_t)0;
  
  maxCellEnergy = 0;
  maxLivingCellEnergy = 0;
  
  /* Set up SDL if we're using it */
#ifdef USE_SDL
  SDL_Surface *screen;
  SDL_Event sdlEvent;
  if (SDL_Init(SDL_INIT_VIDEO) < 0 ) {
    fprintf(stderr,"*** Unable to init SDL: %s ***\n",SDL_GetError());
    exit(1);
  }
  atexit(SDL_Quit);
  SDL_WM_SetCaption(SDL_TITLE, SDL_TITLE);
  screen = SDL_SetVideoMode(POND_SIZE_X,POND_SIZE_Y,8,SDL_SWSURFACE);
  if (!screen) {
    fprintf(stderr, "*** Unable to create SDL window: %s ***\n", SDL_GetError());
    exit(1);
  }
  const uintptr_t sdlPitch = screen->pitch;
#endif /* USE_SDL */
 
  /* Clear the pond and initialize all genomes */
  for(x=0;x<POND_SIZE_X;++x) {
    for(y=0;y<POND_SIZE_Y;++y) {
      pond[x][y].ID = 0;
      pond[x][y].parentID = 0;
      pond[x][y].lineage = 0;
      pond[x][y].generation = 0;
      pond[x][y].energy = 0;
      pond[x][y].logo = 0;
      pond[x][y].facing = 0;
      for(i = 0; i < POND_DEPTH; ++i)
        pond[x][y].genome[i] = OP_STOP;
      for(i = 0; i < RAM_SIZE; ++i)
        pond[x][y].ram[i] = 0x0;
    }
  }
  
  /* Clock is incremented on each core loop */
  uint64_t clock = 0;
  
  /* This is used to generate unique cell IDs */
  uint64_t cellIdCounter = 0;
  
  /* Miscellaneous variables used in the loop */
  uintptr_t instPtr, inst, tmp;
  struct Cell *pptr,*tmpptr;
  
  /* Virtual machine I/O pointer register (used for both genome and output buffer) */
  uintptr_t ptr_ioPtr;

  /* memory pointers */
  uintptr_t ptr_mem;
  
  /* The main "register" */
  uintptr_t reg;
  
  /* Which way is the cell facing? */
  //uintptr_t facing;
  
  /* Virtual machine loop/rep stack */
  uintptr_t loopStack[POND_DEPTH];
  uintptr_t loopStackPtr;
  
  /* If this is nonzero, we're skipping to matching REP */
  /* It is incremented to track the depth of a nested set
   * of LOOP/REP pairs in false state. */
  uintptr_t falseLoopDepth;
  
  /* If this is nonzero, cell execution stops. This allows us
   * to avoid the ugly use of a goto to exit the loop. :) */
  int stop;

#define TEST_NEIGHBOR 0
#if TEST_NEIGHBOR
#define TEST_DIR(dir,x,y,xo,yo) \
  if (getNeighbor(x, y, dir) != &pond[(POND_SIZE_X + x + xo) % POND_SIZE_X][(POND_SIZE_Y + y + yo) % POND_SIZE_Y]) { \
    fprintf(stderr, "TEST_NEIGHBOR dir=%d failed at x=%d, y=%d, xo=%d, yo=%d\n", (int)dir, (int)x, (int)y, (int)xo, (int)yo); \
    return 1;\
  }
  for (y = 0; y < POND_SIZE_Y; ++y) {
    for (x = 0; x < POND_SIZE_X; ++x) {
#if (DIRECTIONS == 4)
        TEST_DIR(DIR_NORTH, x, y,  0, -1);
        TEST_DIR(DIR_EAST,  x, y,  1,  0);
        TEST_DIR(DIR_SOUTH, x, y,  0,  1);
        TEST_DIR(DIR_WEST,  x, y, -1,  0);
#elif (DIRECTIONS == 6)
        if (y & 1) {
            TEST_DIR(DIR_0, x, y,  1, -1);
            TEST_DIR(DIR_1, x, y,  1,  0);
            TEST_DIR(DIR_2, x, y,  1,  1);
            TEST_DIR(DIR_3, x, y,  0,  1);
            TEST_DIR(DIR_4, x, y, -1,  0);
            TEST_DIR(DIR_5, x, y,  0, -1);
        }
        else {
            TEST_DIR(DIR_0, x, y,  0, -1);
            TEST_DIR(DIR_1, x, y,  1,  0);
            TEST_DIR(DIR_2, x, y,  0,  1);
            TEST_DIR(DIR_3, x, y, -1,  1);
            TEST_DIR(DIR_4, x, y, -1,  0);
            TEST_DIR(DIR_5, x, y, -1, -1);
        }
#elif (DIRECTIONS == 8)
        TEST_DIR(DIR_NORTH, x, y,  0, -1);
        TEST_DIR(DIR_NE,    x, y,  1, -1);
        TEST_DIR(DIR_EAST,  x, y,  1,  0);
        TEST_DIR(DIR_SE,    x, y,  1,  1);
        TEST_DIR(DIR_SOUTH, x, y,  0,  1);
        TEST_DIR(DIR_SW,    x, y, -1,  1);
        TEST_DIR(DIR_WEST,  x, y, -1,  0);
        TEST_DIR(DIR_NW,    x, y, -1, -1);
#endif
    }
  }
  return 0;
#endif
  
  /* Main loop */
  for(;;++clock) {
    /* Stop at STOP_AT if defined */
#ifdef STOP_AT
    if (clock >= STOP_AT) {
      /* Also do a final dump if dumps are enabled */
#ifdef DUMP_FREQUENCY
      doDump(clock);
#endif /* DUMP_FREQUENCY */
      fprintf(stderr,"[QUIT] STOP_AT clock value reached\n");
      break;
    }
#endif /* STOP_AT */

    /* Increment clock and run reports periodically */
    /* Clock is incremented at the start, so it starts at 1 */
    if (!(clock % REPORT_FREQUENCY)) {
      doReport(clock);
    }
    /* SDL display is also refreshed every REFRESH_FREQUENCY */
#ifdef USE_SDL
    if (!(clock % REFRESH_FREQUENCY)) {
      while (SDL_PollEvent(&sdlEvent)) {
        if (sdlEvent.type == SDL_QUIT) {
          fprintf(stderr,"[QUIT] Quit signal received!\n");
          exit(0);
        } else if (sdlEvent.type == SDL_MOUSEBUTTONDOWN) {
          switch (sdlEvent.button.button) {
            case SDL_BUTTON_LEFT:
              tmpptr = &pond[sdlEvent.button.x][sdlEvent.button.y];
              if (tmpptr->energy && (tmpptr->generation > 2)) {
                fprintf(stderr,"[INTERFACE] Genome of cell at (%d, %d):\n", sdlEvent.button.x, sdlEvent.button.y);
                dumpCell(stderr, tmpptr);
              }
              break;
            case SDL_BUTTON_RIGHT:
              colorScheme = (colorScheme + 1) % MAX_COLOR_SCHEME;
              fprintf(stderr,"[INTERFACE] Switching to color scheme \"%s\".\n",colorSchemeName[colorScheme]);
              break;
          }
        }
      }
      for (y=0;y<POND_SIZE_Y;++y) {
          for (x=0;x<POND_SIZE_X;++x) {
              ((uint8_t *)screen->pixels)[x + (y * sdlPitch)] = getColor(&pond[x][y]);
          }
      }
      SDL_UpdateRect(screen,0,0,POND_SIZE_X,POND_SIZE_Y);
    }
#endif /* USE_SDL */

    /* Periodically dump the viable population if defined */
#ifdef DUMP_FREQUENCY
    if (!(clock % DUMP_FREQUENCY))
      doDump(clock);
#endif /* DUMP_FREQUENCY */

    /* Introduce a random cell somewhere with a given energy level */
    /* This is called seeding, and introduces both energy and
     * entropy into the substrate. This happens every INFLOW_FREQUENCY
     * clock ticks. */
    if (!(clock % INFLOW_FREQUENCY)) {
      x = getRandom() % POND_SIZE_X;
      y = getRandom() % POND_SIZE_Y;
      pptr = &pond[x][y];
      pptr->ID = cellIdCounter;
      pptr->parentID = 0;
      pptr->lineage = cellIdCounter;
      pptr->generation = 0;
      pptr->logo = 0;
      pptr->facing = 0;
#if TOTAL_ENERGY_CAP
      if (totalEnergy < TOTAL_ENERGY_CAP) {
#endif
#if CELL_ENERGY_CAP
         if (pptr->energy < CELL_ENERGY_CAP) {
#endif
#ifdef INFLOW_RATE_VARIATION
        pptr->energy += INFLOW_RATE_BASE + (getRandom() % INFLOW_RATE_VARIATION);
#else
        pptr->energy += INFLOW_RATE_BASE;
#endif /* INFLOW_RATE_VARIATION */
#if CELL_ENERGY_CAP
         }
#endif
#if TOTAL_ENERGY_CAP
      }
#endif
      instPtr = 0;
      for(i = 0; i < POND_DEPTH; ++i) {
        pptr->genome[i] = getRandom() & INST_MASK;
      }
      for(i = 0; i < RAM_SIZE; ++i) {
#if CLEAR_RAM
        pptr->ram[i] = 0; 
#else
        pptr->ram[i] = getRandom() & REG_MASK;
#endif
      }

      ++cellIdCounter;
    }
    
    /* Pick a random cell to execute */
    x = getRandom() % POND_SIZE_X;
    y = getRandom() % POND_SIZE_Y;
    pptr = &pond[x][y];

    /* Reset the state of the VM prior to execution */
    for(i=0;i<POND_DEPTH;++i) {
      outputBuf[i] = OP_STOP;
    }

    ptr_ioPtr = 0;
    loopStackPtr = 0;
    falseLoopDepth = 0;
    stop = 0;

    //facing = pptr->genome[1] & 0x7;
    //reg = pptr->genome[2] & INST_MASK;
    reg = 0;
    instPtr = EXEC_START_INST;

    ptr_mem = 0;
    
    /* Keep track of how many cells have been executed */
    statCounters.cellExecutions += 1.0;

    /* Core execution loop */
    while (pptr->energy&&(!stop)) {
      /* Get the next instruction */
      inst = pptr->genome[instPtr];
      
      /* Randomly frob either the instruction or the register with a
       * probability defined by MUTATION_RATE. This introduces variation,
       * and since the variation is introduced into the state of the VM
       * it can have all manner of different effects on the end result of
       * replication: insertions, deletions, duplications of entire
       * ranges of the genome, etc. */
      if ((getRandom() & 0xffffffff) < MUTATION_RATE) {
        tmp = getRandom(); /* Call getRandom() only once for speed */
        if (tmp & 0x20000)
            if (tmp & 0x10000)
                inst = tmp & INST_MASK;
            else
                reg = tmp & REG_MASK;
        else
            if (tmp & 0x10000)
                ptr_mem = tmp & MEM_MASK;
            else
                pptr->ram[(tmp >> 8) & RAM_MASK] = tmp & REG_MASK;
      }
      
      /* Each instruction processed costs one unit of energy */
      --pptr->energy;
      
      /* Execute the instruction */
      if (falseLoopDepth) {
        /* Skip forward to matching REP if we're in a false loop. */
        if (inst == 0x9) /* Increment false LOOP depth */
          ++falseLoopDepth;
        else if (inst == 0xa) /* Decrement on REP */
          --falseLoopDepth;
      } else {
        /* If we're not in a false LOOP/REP, execute normally */
        
        /* Keep track of execution frequencies for each instruction */
        statCounters.instructionExecutions[inst] += 1.0;
        
        switch(inst) {
          case OP_SETP: /* SETP */
            ptr_ioPtr = reg;
            break;
          case OP_NEXTB: /* NEXTB */
            ptr_mem = (ptr_mem + 8) & MEM_MASK;
            break;
          case OP_PREVB: /* PREVB */
            ptr_mem = (ptr_mem - 8) & MEM_MASK;
            break;
          case OP_NEXTM: /* NEXTM */
            ptr_mem = (ptr_mem + 1) & MEM_MASK;
            break;
          case OP_PREVM: /* PREVM */
            ptr_mem = (ptr_mem - 1) & MEM_MASK;
            break;
          case OP_READM: /* READM */
            reg = read_mem(pptr, x, y, ptr_mem);
            break;
          case OP_WRITEM: /* WRITEM */
            write_mem(pptr, x, y, ptr_mem, reg);
            break;
          case OP_CLEARM: /* CLEARM */
            for (i = 0; i < RAM_SIZE; ++i) {
                pptr->ram[i] = 0x0;
            }
            break;
          case OP_ADD:
            reg = (reg + read_mem(pptr, x, y, ptr_mem)) & REG_MASK;
            break;
          case OP_SUB:
            reg = (reg - read_mem(pptr, x, y, ptr_mem)) & REG_MASK;
            break;
          case OP_MUL:
            reg = (reg * read_mem(pptr, x, y, ptr_mem)) & REG_MASK;
            break;
          case OP_DIV:
            tmp = read_mem(pptr, x, y, ptr_mem);
            reg = tmp ? (reg / read_mem(pptr, x, y, ptr_mem)) & REG_MASK : 0;
            break;
          case OP_SHL:
            reg = (reg << 1) & REG_MASK;
            break;
          case OP_SHR:
            reg = (reg >> 1) & REG_MASK;
            break;
          case OP_SETMP:
            ptr_mem = reg & MEM_MASK;
            break;
          case OP_RAND:
            reg = getRandom() & REG_MASK;
            break;
          case OP_ZERO: /* ZERO: Zero VM state registers */
            reg = 0;
            //ptr_ioPtr = 0;
            //facing = 0;
            break;
          case OP_FWD: /* FWD: Increment the pointer (wrap at end) */
            if (++ptr_ioPtr >= POND_DEPTH) {
                ptr_ioPtr = 0;
            }
            break;
          case OP_BACK: /* BACK: Decrement the pointer (wrap at beginning) */
            if (ptr_ioPtr) {
                --ptr_ioPtr;
            }
            else {
                ptr_ioPtr = POND_DEPTH - 1;
            }
            break;
          case OP_INC: /* INC: Increment the register */
            reg = (reg + 1) & REG_MASK;
            break;
          case OP_DEC: /* DEC: Decrement the register */
            reg = (reg - 1) & REG_MASK;
            break;
          case OP_READG: /* READG: Read into the register from genome */
            reg = pptr->genome[ptr_ioPtr];
            break;
          case OP_WRITEG: /* WRITEG: Write out from the register to genome */
            pptr->genome[ptr_ioPtr] = reg & INST_MASK;
            break;
          case OP_READO: /* READO: Read into the register from output buffer */
            reg = outputBuf[ptr_ioPtr];
            break;
          case OP_WRITEO: /* WRITEO: Write out from the register to output buffer */
            outputBuf[ptr_ioPtr] = reg & INST_MASK;
            break;
          case OP_LOOP: /* LOOP: Jump forward to matching REP if register is zero */
            if (reg) {
              if (loopStackPtr >= POND_DEPTH)
                stop = 1; /* Stack overflow ends execution */
              else {
                loopStack[loopStackPtr] = instPtr;
                ++loopStackPtr;
              }
            } else falseLoopDepth = 1;
            break;
          case OP_REP: /* REP: Jump back to matching LOOP if register is nonzero */
            if (loopStackPtr) {
              --loopStackPtr;
              if (reg) {
                instPtr = loopStack[loopStackPtr];
                /* This ensures that the LOOP is rerun */
                continue;
              }
            }
            break;
          case OP_TURN: /* using this inst for ideas */
            /* TURN: Turn in the direction specified by register */
            //pptr->facing = reg & FACING_MASK; /* TURN */

            //ptr_mem = (ptr_mem + 4) & MEM_MASK; /* inc mem+ptr by 4 */

            /* read one instruction from either own genome or facing cell */
            if (pptr->generation > 2) {
                tmpptr = getNeighbor(x, y, pptr->facing);
                if (tmpptr->generation > 2 && accessAllowed(tmpptr, reg, COMBINE_SENSE)) {
                    reg = ((getRandom() & 0x8) ? pptr->genome : tmpptr->genome)[ptr_ioPtr];
                }
                else {
                    reg = pptr->genome[ptr_ioPtr];
                }
            }
            else {
                reg = pptr->genome[ptr_ioPtr];
            }
            break;
          case OP_XCHG: /* XCHG: Skip next instruction and exchange value of register with it */
            if (++instPtr >= POND_DEPTH) {
                instPtr = EXEC_START_INST;
            }
            tmp = reg;
            reg = pptr->genome[instPtr];
            pptr->genome[instPtr] = tmp & INST_MASK;
            break;
          case OP_KILL: /* KILL: Blow away neighboring cell if allowed with penalty on failure */
            tmpptr = getNeighbor(x, y, pptr->facing);
            if (accessAllowed(tmpptr, reg, 0)) {
              if (tmpptr->generation > 2)
                ++statCounters.viableCellsKilled;

              /* clear all */
              for (i = 0; i < POND_DEPTH; ++i) {
                tmpptr->genome[i] = OP_STOP;
              }
              tmpptr->ID = cellIdCounter;
              tmpptr->parentID = 0;
              tmpptr->lineage = cellIdCounter;
              tmpptr->generation = 0;
              tmpptr->logo = 0;
              tmpptr->facing = 0;
              ++cellIdCounter;
            } else if (tmpptr->generation > 2) {
              tmp = pptr->energy / FAILED_KILL_PENALTY;
              if (pptr->energy > tmp)
                pptr->energy -= tmp;
              else pptr->energy = 0;
            }
            break;
          case OP_SHARE: /* SHARE: Equalize energy between self and neighbor if allowed */
            tmpptr = getNeighbor(x, y, pptr->facing);
            if (accessAllowed(tmpptr,reg,1)) {
              if (tmpptr->generation > 2)
                ++statCounters.viableCellShares;

              tmp = pptr->energy + tmpptr->energy;
              tmpptr->energy = tmp / 2;
              pptr->energy = tmp - tmpptr->energy;
            }
            break;
          case OP_STOP: /* STOP: End execution */
            stop = 1;
            break;
        }
      }
      
      /* Advance the instruction pointer, and loop around to the beginning at the end of the genome. */
      if (++instPtr >= POND_DEPTH) {
          instPtr = EXEC_START_INST;
      }
    }

    /* Decay part of RAM if there is no energy. */
    if (!pptr->energy) {
#if DECAY_RAM
        tmp = getRandom();
        pptr->ram[(tmp >> 8) & RAM_MASK] = tmp & REG_MASK;
#endif
    }
#if (REPRODUCTION_COST > 0)
    else if (pptr->energy >= REPRODUCTION_COST) {
#else
    else {
#endif

        /* Copy outputBuf into neighbor if access is permitted and there
         * is energy there to make something happen. There is no need
         * to copy to a cell with no energy, since anything copied there
         * would never be executed and then would be replaced with random
         * junk eventually. See the seeding code in the main loop above. */
        if (outputBuf[0] != OP_STOP) {
            tmpptr = getNeighbor(x, y, pptr->facing);
            if ((tmpptr->energy)&&accessAllowed(tmpptr, reg, 0)) {
                /* Log it if we're replacing a viable cell */
                if (tmpptr->generation > 2)
                    ++statCounters.viableCellsReplaced;

                tmpptr->ID = ++cellIdCounter;
                tmpptr->parentID = pptr->ID;
                tmpptr->lineage = pptr->lineage; /* Lineage is copied in offspring */
                tmpptr->generation = pptr->generation + 1;
                tmpptr->logo = 0;
                tmpptr->facing = 0;
                for(i = 0; i < POND_DEPTH; ++i) {
                    tmpptr->genome[i] = outputBuf[i];
                }
                for(i = 0; i < RAM_SIZE; ++i) {
#if CLEAR_RAM
                    tmpptr->ram[i] = 0;
#else
                    tmpptr->ram[i] = getRandom() & REG_MASK;
#endif
                }
                pptr->energy -= REPRODUCTION_COST;
            }
        }
    }

  } /* main loop */
  
  exit(0);
  return 0; /* Make compiler shut up */
}

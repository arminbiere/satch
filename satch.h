#ifndef _satch_h_INCLUDED
#define _satch_h_INCLUDED

// This header file defines the API of the library of the SAT solver SATCH.
// All functions are implemented in 'satch.c' except for the last three
// returning build information. Those are implemented in 'config.c'.

// The parser and witness printer implemented in the stand-alone solver
// front-end 'main.c' are not considered to be part of the library.

#include <stdio.h>

/*========================================================================*/

// Functions with IPASIR semantics are listed in this section.

// The solver is only partially incremental as it is not allowed to add
// clauses after returning from 'satch_solve'.  Furthermore 'satch_assume',
// 'satch_set_terminate', and 'satch_set_learn' are missing too.

struct satch;			// One solver instance.

// SAT competition format conforming exit codes used for 'satch_solve'.

#define UNKNOWN 0
#define SATISFIABLE 10
#define UNSATISFIABLE 20

struct satch *satch_init (void);	// Initialize solver.
void satch_release (struct satch *);	// Release solver.

// Add literals and thus clauses to the solver, where a zero literal
// terminates a clause.  Trivial or (root-level) satisfied are implicitly
// dropped.  You further have to make sure to add a clause completely (add
// the zero literal) before calling 'satch_solve'.

// Literals have to be different from 'INT_MIN' on a 64-bit machine and its
// absolute value smaller or equal to '2^30' on a 32-bit machine.

void satch_add (struct satch *, int literal);

// Solve the current formula.  The routine returns the solution status
// encoded as above 'UNKNOW=0', 'SATISFIABLE=10', or 'UNSATISFIABLE=20'.
// The second argument if non-negative limits the number of conflicts
// before the solver returns.

int satch_solve (struct satch *, int conflict_limit_non_negative);

// If 'satch_solve' returns 'SATISFIABLE=10' then (before adding further
// clauses) you can query the generated satisfying assignment (model).  The
// function returns 'literal' (as is) if the model assigns the literal to
// 'true' and '-literal' if it assigns it to 'false'.

int satch_val (struct satch *, int literal);

/*========================================================================*/

/*------------------------------------------------------------------------*/

// The non-IPASIR functions start here and we first have some short hands.

void satch_add_empty (struct satch *);
void satch_add_unit (struct satch *, int);
void satch_add_binary_clause (struct satch *, int, int);
void satch_add_ternary_clause (struct satch *, int, int, int);
void satch_add_quaternary_clause (struct satch *, int, int, int, int);

/*------------------------------------------------------------------------*/

// Allocate and activate the given number of variables.  This avoids
// repeated internal resizing of the solver and thus slightly speeds up the
// solver if you know already the maximum number of variables needed.

void satch_reserve (struct satch *, int maximum_variable_index);

// By default the library does not print any messages (the solver executable
// however does switch on 'verbose' messages by default unless '-q' is
// specified). There are four non-zero levels of verbose messages.

void satch_set_verbose_level (struct satch *, int level);

// Enable logging at run-time with the following function.
//
// However, logging code needs to be compiled in which is not the default
// and in order to achieve this, you need to either configure the library
// with './configure -g' (keeping 'NDEBUG' undefined) or alternatively use
// './configure -l' (defining the 'LOGGING' macro) to include logging code.
//
// We define in the library this function in any case, and thus you can
// reference it in your code, no matter how the library was compiled, but
// without configuring to include logging code it will not have any effect.

void satch_enable_logging_messages (struct satch *);

// Use ASCII proof format (default is to use the binary DRAT proof format).

void satch_ascii_proof (struct satch *);

// Trace DRAT (actually a DRUP) proof to the given file.

void satch_trace_proof (struct satch *, FILE *);

/*------------------------------------------------------------------------*/

// Return largest added variable index.

int satch_maximum_variable (struct satch *);

// Get process time used by the current process.

double satch_process_time (void);

// Print a nicely formatted 'c ---- [ <name> ] ---- ....' section header.

void satch_section (struct satch *, const char *name);

// Print profiling, statistics and resource usage.

void satch_statistics (struct satch *);

// Return the number of conflicts (saturated at INT_MAX).

int satch_conflicts (struct satch *);

/*------------------------------------------------------------------------*/

// Record and compute time spent in parsing.

// These two functions are needed for the front-end to enter parsing time
// into the library internal profiling table.

void satch_start_profiling_parsing (struct satch *);
double satch_stop_profiling_parsing (struct satch *);

/*------------------------------------------------------------------------*/

// These functions returning build information are implemented in 'config.c'
//
// In the default build process 'config.c' is automatically generated by
// 'mkconfig.sh'.  Thus these functions are only available if you link
// against the full library. If you link against 'satch.o' only, then they
// are missing and you should not use them.

const char *satch_version (void);	// Version string.
const char *satch_compile (void);	// Build information.
const char *satch_identifier (void);	// GIT hash.

/*------------------------------------------------------------------------*/

#ifndef NQUEUE
unsigned max_stamped_unassigned_variable_on_decision_queue (struct satch
							    *solver);
#endif

#ifndef NHEAP
unsigned max_score_unassigned_variable_on_binary_heap (struct satch *solver);
#endif
#endif

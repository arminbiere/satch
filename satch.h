#ifndef _satch_h_INCLUDED
#define _satch_h_INCLUDED

/*------------------------------------------------------------------------*/

// SAT competition conformant exit codes also use for 'satch_solve'.

#define UNKNOWN 0
#define SATISFIABLE 10
#define UNSATISFIABLE 20

/*------------------------------------------------------------------------*/

// Functions with IPASIR semantics.

struct satch;

struct satch *satch_init (void);	// Initialize solver.
void satch_release (struct satch *);	// Release solver.

void satch_add (struct satch *, int);	// Add literal of clause.
int satch_solve (struct satch *);	// Solve instance.
int satch_val (struct satch *, int);	// Get value of literal.

/*------------------------------------------------------------------------*/

// Additional API functions.

// Allocate and activate the given number of variables.
//
void satch_reserve (struct satch *, int max_var);

// Return the largest active variable.
//
int satch_maximum_variable (struct satch *);

// By default the library does not print any messages (the binary however
// does switch on 'verbose' messages by default unless '-q' is specified)
// There are currently four non-zero levels of verbose messages.
//
void satch_set_verbose_level (struct satch *, int level);

#ifndef NDEBUG
void satch_enable_logging_messages (struct satch *);
#endif

// Get process time used by the current process.
//
double satch_process_time (void);

// Print a nicely formatted 'c ---- [ <name> ] ---- ....' section header.
//
void satch_section (struct satch *, const char *name);


// Print statistics and resource usage.
//
void satch_statistics (struct satch *);

/*------------------------------------------------------------------------*/

// Record and compute time spent in parsing.

void satch_start_profiling_parsing (struct satch *);
double satch_stop_profiling_parsing (struct satch *);

/*------------------------------------------------------------------------*/

// These are implemented in the automatically generated file 'config.h' and
// only available if you link against the full library. If you link against
// 'satch.o' only, then they are missing.

const char *satch_version (void);	// Version string.
const char *satch_compile (void);	// Build information.
const char *satch_identifier (void);	// GIT hash.

/*------------------------------------------------------------------------*/

#endif

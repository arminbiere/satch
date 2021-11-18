/*------------------------------------------------------------------------*/

// *INDENT-OFF*

static const char * usage =

"usage: gencombi [ <option> ] [ <k> ]\n"
"\n"
"where '<option>' is\n"
"\n"
"  -h | --help     print this command line option summary\n"
"  -a | --all      print all possible combinations of options up to '<k>'\n"
"  -d | --dimacs   CNF encoding all pairs for '<k>'\n"
"  -i | --invalid  only print invalid combinations\n"
"  -u | --unsorted do not sort configurations\n"
"  -v | --verbose  set verbose mode\n"
"  -w | --weak     do not enforce absence of pairs\n"
"\n"
"This is a tool to generate a list of configuration options. The list of\n"
"possible options as well as incompatible pairs are hard-coded into the\n"
"program at compile time at this point.\n"
"\n"
"By default the SAT solver SATCH is used to search for a list of as few\n"
"as possible configurations which contain all valid pairs of options and\n"
"prints them. For all pair of options we also add a constraint that their\n"
"combination should not occur in at least one chosen configuration.\n"
"\n"
"Using '--all' or '-a' generates all valid combinations of options by\n"
"combining at most '<k>' options.  Again all configurations are printed.\n"
"\n"
"The third mode produces a CNF in DIMACS format which is satisfiable if\n"
"the '<k>' configurations cover all pairs of valid options.\n"
;

// *INDENT-ON*

/*------------------------------------------------------------------------*/

// This part is in essence a hard / compile-time coded set of options, the
// list of incompatible / invalid pairs of options and abbreviations for
// printing options (which is kind of redundant since we could have put the
// abbreviations directly into the list of options).

static const char *options[] = {

  // Basic options ordered with most likely failing compilation first.

  "--pedantic", "--debug", "--check", "--symbols", "--logging",

  // Two options which are only enabled for '--debug'.

  "--no-check",
#define LAST_HARD_CODED_OPTION "--no-logging"
  LAST_HARD_CODED_OPTION,

  // Options to disable features sorted alphabetically.  During
  // initialization 'features' is set to point to the first.

#include "features/list.h"

  0				// Zero sentinel
};

// Pairs of implied / incompatible options (sorted alphabetically also
// within the individual pairs).

static const char *incompatible[] = {

  "--check", "--debug",
  "--check", "--no-check",
  "--debug", "--logging",
  "--debug", "--symbols",
  "--logging", "--no-logging",

#include "features/invalid.h"

  0,				// Zero sentinel.

};

// First option requires second.

static const char *requires[] = {

  "--no-check", "--debug",
  "--no-logging", "--debug",
  0
};

static const char *abbrevs[] = {
  "--check", "-c",
  "--debug", "-g",
  "--logging", "-l",
  "--pedantic", "-p",
  "--symbols", "-s",
  0
};

/*------------------------------------------------------------------------*/

#include "satch.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/

// Global options.

static const char *all;		// Option to print all combinations.
static const char *dimacs;	// Option to print DIMACS files.
static const char *invalid;	// Option to only print invalid.
static const char *unsorted;	// Option to disable sorting.
static const char *verbose;	// Option to increase verbosity.
static const char *weak;	// Option for weaker check.

static int k = -1;

/*------------------------------------------------------------------------*/

static void die (const char *fmt, ...) __attribute__((format (printf, 1, 2)));
static void msg (const char *fmt, ...) __attribute__((format (printf, 1, 2)));

static void
die (const char *fmt, ...)
{
  fputs ("gencombi: error: ", stderr);
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void
msg (const char *fmt, ...)
{
  if (!verbose)
    return;
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

static void
out_of_memory (size_t bytes)
{
  die ("out-of-memory allocating %zu bytes", bytes);
}

static void *
allocate (size_t bytes)
{
  void *res = malloc (bytes);
  if (bytes && !res)
    out_of_memory (bytes);
  return res;
}

/*------------------------------------------------------------------------*/

static const char *
shorten (const char *s)
{
  for (const char **p = abbrevs; *p; p += 2)
    if (!strcmp (s, p[0]))
      return p[1];
  return s;
}

/*------------------------------------------------------------------------*/

static int noptions;		// Number of options.

static char **valid;		// Indicates valid option pairs.
static char **needs;		// Indicate required option pairs.
static int *needed;		// At least once.

// Return non-zero if the given option pair is incompatible.

static int
filter_incompatible (const char *a, const char *b)
{
  for (const char **pair = incompatible; *pair; pair += 2)
    {
      if (!strcmp (a, pair[0]) && !strcmp (b, pair[1]))
	return 1;
      if (!strcmp (a, pair[1]) && !strcmp (b, pair[0]))
	return 1;
    }
  return 0;
}

//  Return non-zero if the given option pair is a required pair.

static int
filter_requires (const char *a, const char *b)
{
  for (const char **pair = requires; *pair; pair += 2)
    if (!strcmp (a, pair[0]) && !strcmp (b, pair[1]))
      return 1;
  return 0;
}

// Initialize 'noptions'.

static void
init_options (void)
{
  for (const char **p = options; *p; p++)
    noptions++;
  msg ("found %d options", noptions);
}

// Initialize the 'needs' 'needed' tables (using 'filter_requires').

static void
init_needs (void)
{
  needs = allocate (noptions * sizeof *needs);
  needed = allocate (noptions * sizeof *needed);
  int count = 0;

  for (int p = 0; p < noptions; p++)
    {
      needs[p] = allocate (noptions * sizeof *needs[p]);
      needed[p] = 0;
      for (int q = 0; q < noptions; q++)
	{
	  int filtered = filter_requires (options[p], options[q]);
	  needs[p][q] = filtered;
	  needed[p] += filtered;
	}
      if (needed[p] > 1)
	die ("option '%s' with %d required options", options[p], needed[p]);
      else if (needed[p])
	count++;
    }
  if (count)
    msg ("found %u options which require other options", count);
}

// Initialize the 'valid' table (using 'filter_incompatible').

static void
init_valid (void)
{
  valid = allocate (noptions * sizeof *valid);
  int count = 0;
  for (int p = 0; p < noptions; p++)
    {
      valid[p] = allocate (noptions * sizeof *valid[p]);
      for (int q = 0; q < noptions; q++)
	if (!(valid[p][q] = !filter_incompatible (options[p], options[q])))
	  count++;
    }

  msg ("found %u incompatible option pairs", count);

  int changed;
  int round = 0;
  count = 0;

  do
    {
      round++;
      changed = 0;
      for (int p = 0; p < noptions; p++)
	{
	  if (!needed[p])
	    continue;
	  for (int q = 0; q < noptions; q++)
	    if (needs[p][q])
	      {
		for (int r = 0; r < noptions; r++)
		  if (p != r && !valid[q][r] && valid[p][r])
		    {
		      msg ("forced incompatible pair \"%s\", \"%s\"",
			   options[p], options[r]);
		      valid[p][r] = 0;
		      valid[r][p] = 0;
		      changed = 1;
		      count++;
		    }
	      }
	}
    }
  while (changed);

  msg ("forced %d incompatible pairs due to requirements in %d rounds",
       count, round);
}

// Sanity checking for 'options'.

static void
check_options (void)
{
  const char **features = 0;

  for (const char **p = options, *o; !features && (o = *p); p++)
    if (!strcmp (o, LAST_HARD_CODED_OPTION))
      features = p + 1;

  assert (features);

  for (const char **p = features + 1; *p; p++)
    if (strcmp (p[-1], *p) > 0)
      die ("option '%s' before '%s'", p[-1], p[0]);
}

// Sanity checking for 'incompatible'.

static void
check_incompatible (void)
{
  for (const char **p = incompatible; *p; p += 2)
    {
      assert (p[1]);
      if (strcmp (p[0], p[1]) >= 0)
	die ("unsorted incompatible pair '\"%s\", \"%s\"'", p[0], p[1]);

      if (incompatible < p && strcmp (p[-2], p[0]) > 0)
	die ("incompatible pair '\"%s\", \"%s\"' before '\"%s\", \"%s\"'",
	     p[-2], p[-1], p[0], p[1]);
    }
}

// Sanity checking for 'requires'.

static void
check_requires (void)
{
  for (const char **p = requires; *p; p += 2)
    {
      assert (p[1]);
      if (requires < p && strcmp (p[-2], p[0]) > 0)
	die ("requires pair '%s;%s' before '%s;%s'",
	     p[-2], p[-1], p[0], p[1]);
    }
}

static void
reset_valid (void)
{
  for (int p = 0; p < noptions; p++)
    free (valid[p]);
  free (valid);

  for (int p = 0; p < noptions; p++)
    free (needs[p]);
  free (needs);

  free (needed);
}

/*========================================================================*/
//   Code to generate all valid options up to the given maximum number    //
/*========================================================================*/

// Stack of 'selected' options.
//
static int selected;
static int *config;

// Recursively generate and print all valid configurations of up-to 'select'
// options.  The argument 'select' gives the number of remaining to be
// selected options while 'selected' is the number of already selected.

static void
generate (int current, int select)
{
  if (!select)
    {
      if (!invalid)
	fputs ("./configure\n", stdout);
    }
  else if (selected == select)
    {
      int config_valid = 1;

      for (int i = 0; config_valid && i + 1 < selected; i++)
	for (int j = i + 1; config_valid && j < selected; j++)
	  config_valid = valid[config[i]][config[j]];

      if (config_valid)
	{
	  for (int i = 0; config_valid && i < selected; i++)
	    if (needed[config[i]])
	      {
		config_valid = 0;
		for (int j = 0; !config_valid && j < selected; j++)
		  if (i != j && needs[config[i]][config[j]])
		    config_valid = 1;
	      }
	}

      if (invalid && config_valid)
	return;

      if (!invalid && !config_valid)
	return;

      fputs ("./configure", stdout);
      for (int i = 0; i < selected; i++)
	{
	  fputc (' ', stdout);
	  fputs (shorten (options[config[i]]), stdout);
	}
      fputc ('\n', stdout);
    }
  else if (current < noptions)
    {
      config[selected++] = current;
      generate (current + 1, select);
      selected--;

      generate (current + 1, select);
    }
}

/*========================================================================*/
// Code to generate either a CNF for the given '<k>' or a minimum set of  //
// configurations which cover all pairs once also not once (uses SAT).    //
/*========================================================================*/

static int noptions;

/*------------------------------------------------------------------------*/

// Print literals and clauses or encode them into the SAT solver.

static void
literal (struct satch *solver, int lit)
{
  if (dimacs)
    {
      printf ("%d", lit);
      fputc (lit ? ' ' : '\n', stdout);
    }
  else
    satch_add (solver, lit);
}

static void
binary (struct satch *solver, int a, int b)
{
  literal (solver, a);
  literal (solver, b);
  literal (solver, 0);
}

static void
ternary (struct satch *solver, int a, int b, int c)
{
  literal (solver, a);
  literal (solver, b);
  literal (solver, c);
  literal (solver, 0);
}

/*------------------------------------------------------------------------*/

// This is main encoding function to produce a CNF which is satisfiable if
// the there is a list of configurations of size 'k' which covers all valid
// pairs of options.  The CNF is either printed to '<stdout>' or given to
// the SAT solver.  In the later case it is checked to be satisfiable
// (optionally printing some verbose messages) and if so the resulting set
// of configurations is printed to '<stdout>'. Only then the function
// returns a non-zero result.

struct frame
{
  bool encoded;
  bool released;
  int limit;			// Conflict limit.
  int conflicts;		// Previous conflicts.
  int status;			// Result status.
  int ***pair;			// DIMACS variable index table for pairs.
  int **option;			// DIMACS variable index for options.
  int **sorted;			// DIMACS variable for sorting options.
  struct satch *solver;		// The actual solver.
};

static void
init_frame (struct frame *frame, int k)
{
  frame->pair = allocate ((k + 1) * sizeof *frame->pair);
  frame->option = allocate ((k + 1) * sizeof *frame->option);

  if (!unsorted)
    frame->sorted = allocate ((k + 1) * sizeof *frame->sorted);

  if (!dimacs)
    frame->solver = satch_init ();
}

static void
release_frame (struct frame *frame, int k)
{
  if (frame->released)
    return;

  frame->released = true;

  if (!frame->encoded)
    return;

  int **option = frame->option;
  for (int i = 0; i < k; i++)
    free (option[i]);
  free (option);

  if (!unsorted)
    {
      int **sorted = frame->sorted;
      for (int i = 1; i < k; i++)
	free (sorted[i]);
      free (sorted);
    }

  int ***pair = frame->pair;
  for (int i = 0; i < k; i++)
    {
      for (int p = 0; p + 1 < noptions; p++)
	free (pair[i][p]);
      free (pair[i]);
    }
  free (pair);

  if (dimacs)
    return;

  satch_release (frame->solver);
  msg ("frame[%d] released with status %d conflicts %d",
       k, frame->status, frame->conflicts);
}

static struct frame *frames;
static int nframes;

static void
new_frame (int k)
{
  assert (nframes <= k);
  const size_t bytes = (k + 1) * sizeof *frames;
  frames = realloc (frames, bytes);
  if (!frames)
    out_of_memory (bytes);
  memset (frames + nframes, 0, (k + 1 - nframes) * sizeof *frames);
  nframes = k + 1;
}

static struct frame *
get_frame (int k)
{
  assert (2 <= k);
  if (nframes <= k)
    new_frame (k);
  return frames + k;
}

static void
release_frames (void)
{
  if (dimacs)
    release_frame (frames + k, k);
  else
    for (int i = 2; i < nframes; i++)
      release_frame (frames + i, i);
  free (frames);
}

static bool
encoded (int k)
{
  return k < nframes && frames[k].encoded;
}

static void
encode (int k)			// Thus 'encode' sees only local 'k'!
{
  assert (!encoded (k));

  struct frame *frame = get_frame (k);
  init_frame (frame, k);
  frame->encoded = true;

  struct satch *solver = frame->solver;
  assert (dimacs || solver);

  int nvars = 0;
  int nclauses = 0;

  int **option = frame->option;

  for (int i = 0; i < k; i++)
    {
      option[i] = allocate (noptions * sizeof *option[i]);
      for (int p = 0; p < noptions; p++)
	option[i][p] = ++nvars;
    }

  int ***pair = frame->pair;

  for (int i = 0; i < k; i++)
    {
      pair[i] = allocate (noptions * sizeof *pair[i]);
      for (int p = 0; p + 1 < noptions; p++)
	{
	  pair[i][p] = allocate (noptions * sizeof *pair[i][p]);
	  for (int q = p + 1; q < noptions; q++)
	    if (valid[p][q])
	      pair[i][p][q] = ++nvars;
	}
    }

  if (!unsorted)
    {
      int **sorted = frame->sorted;

      for (int i = 1; i < k; i++)
	{
	  sorted[i] = allocate (noptions * sizeof *sorted[i]);
	  for (int p = 1; p < noptions; p++)
	    sorted[i][p] = ++nvars;

	  nclauses += 3 * (noptions - 1) + 2;
	}
    }

  if (dimacs)
    printf ("c gencombi --dimacs %d\n", k);

  // Compute number of clauses for verbose message as well as DIMACS.

  if (dimacs || verbose)
    {
      for (int i = 0; i < k; i++)
	{
	  if (dimacs)
	    for (int p = 0; p < noptions; p++)
	      printf ("c option[%d,%d] %d %s\n",
		      i, p, option[i][p], options[p]);

	  for (int p = 0; p + 1 < noptions; p++)
	    for (int q = p + 1; q < noptions; q++)
	      if (valid[p][q])
		{
		  if (dimacs)
		    printf ("c pair[%d,%d,%d] %d %s %s\n", i, p, q,
			    pair[i][p][q], options[p], options[q]);

		  nclauses += 3;

		  if (!i)
		    {
		      nclauses += 1;	// Pairs occur once.
		      if (!weak)
			nclauses += 1;	// Pairs do not occur too.
		    }
		}
	      else
		nclauses++;

	  for (int p = 0; p < noptions; p++)
	    if (needed[p])
	      nclauses++;
	}

      if (dimacs)
	printf ("p cnf %d %d\n", nvars, nclauses);

      msg ("frame[%d] encoded with %d variables and %d clauses",
	   k, nvars, nclauses);
    }

  // We use symmetry breaking and sort options globally.

  // Our experiments however did not give any benefit in using this
  // restriction, but it does produce nicer (sorted) configuration lists.

  if (!unsorted)
    for (int i = 1; i < k; i++)
      {
	if (dimacs)
	  printf ("c sorting %d\n", i);

	int **sorted = frame->sorted;

	binary (solver, option[i - 1][0], -option[i][0]);
	binary (solver, option[i - 1][0], sorted[i][1]);
	binary (solver, -option[i][0], sorted[i][1]);

	for (int p = 1; p + 1 < noptions; p++)
	  {
	    ternary (solver, -sorted[i][p], option[i - 1][p], -option[i][p]);
	    ternary (solver, -sorted[i][p], option[i - 1][p],
		     sorted[i][p + 1]);
	    ternary (solver, -sorted[i][p], -option[i][p], sorted[i][p + 1]);
	  }

	binary (solver, -sorted[i][noptions - 1],
		option[i - 1][noptions - 1]);
	binary (solver, -sorted[i][noptions - 1], -option[i][noptions - 1]);
      }

  // First add all the pairs 'pair[i][p][q] = option[i][p] & option[i][q]'
  // for 'valid[p][q]' pairs and disable that pair, i.e., add the clause
  // '!option[i][p] | !option[j][q]', otherwise.

  for (int i = 0; i < k; i++)
    {
      if (dimacs)
	printf ("c pairs[%d]\n", i);
      for (int p = 0; p + 1 < noptions; p++)
	for (int q = p + 1; q < noptions; q++)
	  if (valid[p][q])
	    {
	      binary (solver, -pair[i][p][q], option[i][p]);
	      binary (solver, -pair[i][p][q], option[i][q]);
	      ternary (solver, -option[i][p], -option[i][q], pair[i][p][q]);
	    }
	  else
	    binary (solver, -option[i][p], -option[i][q]);
    }

  // Add required constraints.

  for (int i = 0; i < k; i++)
    {
      if (dimacs)
	printf ("c required[%d]\n", i);
      for (int p = 0; p < noptions; p++)
	if (needed[p])
	  {
	    literal (solver, -option[i][p]);
	    for (int q = 0; q < noptions; q++)
	      if (p != q && needs[p][q])
		literal (solver, option[i][q]);
	    literal (solver, 0);
	  }
    }

  // Now every pair should occur at least once (which also makes sure that
  // every option is selected at least once).

  if (dimacs)
    printf ("c positive occurrence of all pairs\n");

  for (int p = 0; p + 1 < noptions; p++)
    for (int q = p + 1; q < noptions; q++)
      if (valid[p][q])
	{
	  for (int i = 0; i < k; i++)
	    literal (solver, pair[i][p][q]);
	  literal (solver, 0);
	}

  // Finally every pair should not occur at least once.

  if (dimacs)
    printf ("c negative occurrence of all pairs\n");

  if (!weak)
    for (int p = 0; p + 1 < noptions; p++)
      for (int q = p + 1; q < noptions; q++)
	if (valid[p][q])
	  {
	    for (int i = 0; i < k; i++)
	      literal (solver, -pair[i][p][q]);
	    literal (solver, 0);
	  }

  assert (encoded (k));
}

/*------------------------------------------------------------------------*/

static void
print_solution (int k)
{
  struct frame *frame = get_frame (k);
  assert (frame->encoded);
  assert (!frame->released);
  assert (frame->status == SATISFIABLE);
  struct satch *solver = frame->solver;
  assert (solver);
  for (int i = 0; i < k; i++)
    {
      fputs ("./configure", stdout);
      for (int p = 0; p < noptions; p++)
	{
	  int lit = frame->option[i][p];
	  int value = satch_val (solver, lit);
	  if (value != lit)
	    continue;
	  fputc (' ', stdout);
	  fputs (shorten (options[p]), stdout);
	}
      fputc ('\n', stdout);
    }
}

/*------------------------------------------------------------------------*/

// Solve under the current limits the formulas of all remaining solvers.

static const int initial_conflict_limit = 100;
static const int expected_margin = 10;

static int
solve (int k)
{
  struct frame *frame = get_frame (k);
  if (frame->status)
    return frame->status;
  if (!frame->encoded)
    encode (k);
  struct satch *solver = frame->solver;
  assert (solver);
  if (!frame->limit)
    frame->limit = initial_conflict_limit;
  else
    frame->limit *= 2;
  const double start = satch_process_time ();
  msg ("frame[%d] solving with limit %d after %.2f seconds",
       k, frame->limit, start);
  int res = frame->status = satch_solve (solver, frame->limit);
  const int conflicts = satch_conflicts (solver);
  const int delta = conflicts - frame->conflicts;
  frame->conflicts = conflicts;
  const double end = satch_process_time ();
  const double seconds = end - start;
  msg ("frame[%d] solved with status %d in %.2f seconds and %d conflicts",
       k, frame->status, seconds, delta);
  return res;
  if (res == UNSATISFIABLE)
    release_frame (frame, k);
  return res;
}

static void
update_limits (int ub)
{
  int limit = expected_margin * frames[ub].conflicts;
  msg ("updating limits to %d conflicts in total", limit);
  for (int k = 2; k < ub; k++)
    {
      struct frame *frame = frames + k;
      int conflicts = frame->conflicts;
      if (conflicts > limit)
	frame->limit = initial_conflict_limit / 2;
      else
	frame->limit = (limit - conflicts) / 2;
    }
}

/*------------------------------------------------------------------------*/

static void
repeated (const char *first, const char *second)
{
  if (strcmp (first, second))
    die ("'%s' and '%s' have the same effect (try '-h')", first, second);
  else
    die ("repeated '%s' option (try '-h')", first);
}

static void
set (const char **previous, const char *arg)
{
  if (*previous)
    repeated (*previous, arg);
  *previous = arg;
}

/*------------------------------------------------------------------------*/

int
main (int argc, char **argv)
{
  for (int i = 1; i < argc; i++)
    {
      const char *arg = argv[i];
      if (!strcmp (arg, "-h") || !strcmp (arg, "--help"))
	{
	  fputs (usage, stdout);
	  exit (0);
	}
      else if (!strcmp (arg, "-a") || !strcmp (arg, "--all"))
	set (&all, arg);
      else if (!strcmp (arg, "-d") || !strcmp (arg, "--dimacs"))
	set (&dimacs, arg);
      else if (!strcmp (arg, "-u") || !strcmp (arg, "--unsorted"))
	set (&unsorted, arg);
      else if (!strcmp (arg, "-v") || !strcmp (arg, "--verbose"))
	set (&verbose, arg);
      else if (!strcmp (arg, "-w") || !strcmp (arg, "--weak"))
	set (&weak, arg);
      else if (!strcmp (arg, "-i") || !strcmp (arg, "--invalid"))
	set (&invalid, arg);
      else if (k > 0)
	die ("multiple numbers '%d' and '%s' (try '-h')", k, arg);
      else if ((k = atoi (arg)) <= 0)
	die ("invalid number '%s' (try '-h')", arg);
    }


  if (k < 0)
    k = !all;
  else if (!dimacs && !all)
    die ("can not use '<k> = %d' in default mode", k);

  if (dimacs && k < 2)
    die ("dimacs encoding for 'k=%d' does not make sense", k);

  if (invalid && !all)
    die ("can only use '%s' with '--all'", invalid);

  if (unsorted && all)
    die ("can not use '%s' with '%s'", unsorted, all);

  if (weak && all)
    die ("can not use '%s' with '%s'", weak, all);

  check_options ();
  check_incompatible ();
  check_requires ();

  init_options ();
  init_needs ();
  init_valid ();

  if (all)
    {
      config = allocate (k * sizeof *config);
      for (int i = 0; i <= k; i++)
	generate (0, i);
      free (config);
    }
  else if (dimacs)
    encode (k);
  else
    {
      // In the first step search for an upper bound geometrically.

      int ub = 2;
      while (solve (ub) != SATISFIABLE)
	{
	  // But re-run previous encoded frames with some effort.

	  int k;
	  for (k = 2; k < ub; k++)
	    if (encoded (k) && solve (k) == SATISFIABLE)
	      break;

	  if (k < ub)
	    {
	      ub = k;
	      break;
	    }

	  ub *= 2;
	}

      msg ("initial upper bound %d", ub);

      // After we have an upper bound search for a reasonable lower bound
      // with binary search, where reasonable means that it uses more
      // conflicts.

      update_limits (ub);

      int lb = 2;
      while (lb + 1 < ub)
	{
	  int m = lb + (ub - lb) / 2;
	  assert (lb < m);
	  assert (m < ub);
	  int res = solve (m);
	  if (res == SATISFIABLE)
	    {
	      lb = 2;
	      ub = m;
	      update_limits (ub);
	    }
	  else
	    lb = m;
	}

      msg ("lower bound %d", lb);

      print_solution (ub);
    }

  reset_valid ();
  if (!all)
    release_frames ();

  if (!all && !dimacs)
    msg ("used %.2f seconds in total", satch_process_time ());

  return 0;
}

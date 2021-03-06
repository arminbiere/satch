/*------------------------------------------------------------------------*/
//   Copyright (c) 2021, Armin Biere, Johannes Kepler University Linz     //
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

  "--pedantic", "--debug", "--check", "--symbols",

  // Options to disable features sorted alphabetically.  During
  // initialization 'features' is set to point to the first.

#include "features/list.h"

  0				// Zero sentinel
};

// Pairs of implied / incompatible options (sorted alphabetically also
// within the individual pairs).

static const char *incompatible[] = {

  "--check", "--debug",
  "--debug", "--symbols",

#include "features/invalid.h"

  0,				// Zero sentinel.

};

static const char *abbrevs[] = {
  "--check", "-c",
  "--debug", "-g",
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

static void *
callocate (size_t bytes)
{
  void *res = malloc (bytes);
  if (bytes && !res)
    out_of_memory (bytes);
  memset (res, 0, bytes);
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

static int found_leader;
static int leader[2];		// First pair which can be combined.

static char *constrained;	// Counts how often an option is constrained.

static int nindependent;
static char **independent;	// Indicates independent option pairs.

// Return non-zero if the given option pair is invalid / incompatible.

static int
filter (const char *a, const char *b)
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

// Initialize the 'valid' table (using 'filter' and 'incompatible').

static void
init_valid (void)
{
  for (const char **p = options; *p; p++)
    noptions++;

  valid = allocate (noptions * sizeof *valid);
  constrained = callocate (noptions * sizeof *constrained);

  for (int p = 0; p < noptions; p++)
    {
      valid[p] = allocate (noptions * sizeof *valid[p]);

      for (int q = 0; q < noptions; q++)
	{
	  int filtered = filter (options[p], options[q]);
	  if (filtered)
	    {
	      valid[p][q] = 0;
	      if (p < q)
		{
		  constrained[p]++;
		  constrained[q]++;
		}
	      continue;
	    }
	  valid[p][q] = 1;
	  if (found_leader)
	    continue;
	  leader[0] = p, leader[1] = q;
	  found_leader = 1;
	  msg ("leading pair '%s,%s'", options[p], options[q]);
	}
    }

  independent = allocate (noptions * sizeof *independent);

  for (int p = 0; p < noptions; p++)
    {
      independent[p] = callocate (noptions * sizeof *independent[p]);

      if (constrained[p] != 1)
	continue;

      for (int q = p + 1; q < noptions; q++)
	{
	  if (constrained[q] != 1)
	    continue;
	  if (valid[p][q])
	    continue;
	  msg ("independent pair '%s,%s'", options[p], options[q]);
	  independent[p][q] = 1;
	  nindependent++;
	  break;
	}
    }

  msg ("found in total %d independent pairs", nindependent);
}

// Sets 'features' and does some sanity checking.

static void
init_options (void)
{
  const char **features = 0;

  for (const char **p = options, *o; !features && (o = *p); p++)
    if (o[0] == '-' && o[1] == '-' && o[2] == 'n' && o[3] == 'o'
	&& o[4] == '-')
      features = p;

  assert (features);

  for (const char **p = features + 1; *p; p++)
    assert (strcmp (p[-1], *p) < 0);
}

static void
init_incompatible (void)
{
  for (const char **p = incompatible; *p; p += 2)
    {
      assert (p[1]);
      assert (strcmp (p[0], p[1]) < 0);
      if (incompatible < p)
	assert (strcmp (p[-2], p[0]) <= 0);
    }
}

static void
reset_valid (void)
{
  for (int p = 0; p < noptions; p++)
    free (valid[p]);
  free (valid);

  free (constrained);

  for (int p = 0; p < noptions; p++)
    free (independent[p]);
  free (independent);
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
unary (struct satch *solver, int a)
{
  literal (solver, a);
  literal (solver, 0);
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

  if (found_leader)
    nclauses += 2;

  if (nindependent)
    nclauses += 2 * nindependent;

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
	      printf ("c option[%d,%d] = %d\n", i, p, option[i][p]);

	  for (int p = 0; p + 1 < noptions; p++)
	    for (int q = p + 1; q < noptions; q++)
	      if (valid[p][q])
		{
		  if (dimacs)
		    printf ("c pair[%d,%d,%d] = %d\n", i, p, q,
			    pair[i][p][q]);

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
	}

      if (dimacs)
	printf ("p cnf %d %d\n", nvars, nclauses);

      msg ("frame[%d] encoded with %d variables and %d clauses",
	   k, nvars, nclauses);
    }

  // Trivial symmetry breaking by forcing first valid pair to be enabled in
  // first configuration.  We put this symmetry breaking first since it
  // involves units, which simplifies the encoded formula on-the-fly.

  if (found_leader)
    {
      unary (solver, option[0][leader[0]]);
      unary (solver, option[0][leader[1]]);
    }

  // Second simple to encode form of symmetry breaking uses independent
  // pairs of incompatible options which allows to order the options within
  // this pair arbitrarily.

  if (nindependent)
    {
      for (int p = 0; p + 1 < noptions; p++)
	for (int q = p + 1; q < noptions; q++)
	  if (independent[p][q])
	    unary (solver, option[0][p]), unary (solver, -option[0][q]);
    }

  // Third form of symmetry breaking sorts the options globally.  Our
  // experiments however did not give any benefit in using this restriction,
  // but it does produce nicer (sorted) configuration lists.

  if (!unsorted)
    for (int i = 1; i < k; i++)
      {
	int **sorted = frame->sorted;

	binary (solver, option[i - 1][0], -option[i][0]),
	  binary (solver, option[i - 1][0], sorted[i][1]),
	  binary (solver, -option[i][0], sorted[i][1]);

	for (int p = 1; p + 1 < noptions; p++)
	  ternary (solver, -sorted[i][p], option[i - 1][p], -option[i][p]),
	    ternary (solver, -sorted[i][p], option[i - 1][p],
		     sorted[i][p + 1]), ternary (solver, -sorted[i][p],
						 -option[i][p],
						 sorted[i][p + 1]);

	binary (solver, -sorted[i][noptions - 1],
		option[i - 1][noptions - 1]), binary (solver,
						      -sorted[i][noptions -
								 1],
						      -option[i][noptions -
								 1]);
      }

  // First add all the pairs 'pair[i][p][q] = option[i][p] & option[i][q]'
  // for 'valid[p][q]' pairs and disable that pair, i.e., add the clause
  // '!option[i][p] | !option[j][q]', otherwise.

  for (int i = 0; i < k; i++)
    {
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

  // Now every pair should occur at least once (which also makes sure that
  // every option is selected at least once).

  for (int p = 0; p + 1 < noptions; p++)
    for (int q = p + 1; q < noptions; q++)
      if (valid[p][q])
	{
	  for (int i = 0; i < k; i++)
	    literal (solver, pair[i][p][q]);
	  literal (solver, 0);
	}

  // Finally every pair should not occur at least once.

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
  struct frame * frame = get_frame (k);
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
  struct frame * frame = get_frame (k);
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
      struct frame * frame = frames + k;
      int conflicts = frame->conflicts;
      if (conflicts > limit)
	frame->limit = initial_conflict_limit/2;
      else
	frame->limit = (limit - conflicts)/2;
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
    die ("can only use '%s' with '%s'", invalid, all);

  if (unsorted && all)
    die ("can not use '%s' with '%s'", unsorted, all);

  if (weak && all)
    die ("can not use '%s' with '%s'", weak, all);

  init_options ();
  init_incompatible ();
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

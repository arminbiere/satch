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
"  -v | --verbose  set verbose mode\n"
"\n"
"By default the SAT solver SATCH is used to search for as few as possible\n"
"configurations which contains all valid pairs of options and prints them.\n"
"For all pair of options we also add a constraint that their combination\n"
"should not occur in at least one chosen configuration.\n"
"\n"
"Using '--all' will generate all valid combinations of options by combining\n" 
"of at most '<k>' options.  Again all configurations are printed.\n"
"\n"
"The third mode produces a CNF in DIMACS format which is satisfiable if\n"
"there '<k>' configurations cover all pairs of valid options.\n"
;

// *INDENT-ON*

/*------------------------------------------------------------------------*/

// This part is in essence a hard / compile-time coded set of options, the
// list of incompatible / invalid pairs of options and abbreviations for
// printing options (which is kind of redundant since we could have put the
// abbreviations directly into the list of options).

static const char *options[] = {
  "--pedantic", "--debug", "--check", "--symbols",
  "--no-sort", "--no-block", "--no-flex", "--no-learn", "--no-reduce",
  "--no-restart", "--no-stable", 0
};

static const char *incompatible[] = {
  "--check", "--debug",
  "--debug", "--symbols",
  "--no-learn", "--no-reduce",
  "--no-restart", "--no-stable",
  0,
};

static const char *abbrevs[] = {
  "--debug", "-g",
  "--check", "-c",
  "--symbols", "-s",
  "--pedantic", "-p",
  0
};

/*------------------------------------------------------------------------*/

#include "satch.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------*/

// Global options.

static const char *all;		// Option to print all combinations.
static const char *dimacs;	// Option to print DIMACS files.
static const char *invalid;	// Option to only print invalid.
static const char *verbose;	// Option to increase verbosity.
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
static signed char **valid;	// Table of valid option pairs.

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

  for (int p = 0; p < noptions; p++)
    {
      valid[p] = allocate (noptions * sizeof *valid[p]);
      for (int q = 0; q < noptions; q++)
	valid[p][q] = !filter (options[p], options[q]);
    }
}

static void
reset_valid (void)
{
  for (int p = 0; p < noptions; p++)
    free (valid[p]);
  free (valid);
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
// selected options while 'selected' is the numebr of already selected.

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

static int noptions, nvars, nclauses;
static struct satch *solver;

static int ***pair;		// DIMACS variable index table for pairs.
static int **option;		// DIMACS variable index for options.

/*------------------------------------------------------------------------*/

// Print literals and clauses or encode them into the SAT solver.

static void
literal (int lit)
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
binary (int a, int b)
{
  literal (a);
  literal (b);
  literal (0);
}

static void
ternary (int a, int b, int c)
{
  literal (a);
  literal (b);
  literal (c);
  literal (0);
}

/*------------------------------------------------------------------------*/

// The main encoder to produce a CNF which is satisfiable if the there is a
// list of configurations of size 'k' which covers all valid pairs of
// options.  The CNF is either printed to '<stdout>' or given to the SAT
// solver.  In the later case it is checked to be satisfiable (optionally
// printing some verbose messages) and if so the resulting set of
// configurations is printed to '<stdout>'. Only then the function returns a
// non-zero result.

static int
encode (int k)			// Thus 'encode' sees only local 'k'!
{
  if (!dimacs)
    solver = satch_init ();

  nvars = nclauses = 0;

  // Allocate tables and assigned DIMACS indices.

  option = allocate ((k + 1) * sizeof *option);
  pair = allocate ((k + 1) * sizeof *pair);

  for (int i = 0; i < k; i++)
    {
      option[i] = allocate (noptions * sizeof *option[i]);
      for (int p = 0; p < noptions; p++)
	option[i][p] = ++nvars;
    }

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
		    nclauses += 2;	// Pairs occur once and not.
		}
	      else
		nclauses++;
	}

      if (dimacs)
	printf ("p cnf %d %d\n", nvars, nclauses);

      if (verbose)
	printf ("c need %d variables and %d clauses for k = %d\n",
		nvars, nclauses, k), fflush (stdout);
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
	      binary (-pair[i][p][q], option[i][p]);
	      binary (-pair[i][p][q], option[i][q]);
	      ternary (-option[i][p], -option[i][q], pair[i][p][q]);
	    }
	  else
	    binary (-option[i][p], -option[i][q]);
    }

  // Now every pair should occur at least once (which also makes sure that
  // every option is selected at least once).

  for (int p = 0; p + 1 < noptions; p++)
    for (int q = p + 1; q < noptions; q++)
      if (valid[p][q])
	{
	  for (int i = 0; i < k; i++)
	    literal (pair[i][p][q]);
	  literal (0);
	}

  // Finally every pair should not occur at least once.

  for (int p = 0; p + 1 < noptions; p++)
    for (int q = p + 1; q < noptions; q++)
      if (valid[p][q])
	{
	  for (int i = 0; i < k; i++)
	    literal (-pair[i][p][q]);
	  literal (0);
	}

  int status = 0;

  if (!dimacs)
    {
      const double start = satch_process_time ();
      status = satch_solve (solver);
      const double end = satch_process_time ();
      const double seconds = end - start;
      if (verbose)
	{
	  printf ("c solver returns %d for k = %d in %.2f seconds\n",
		  status, k, seconds);
	  fflush (stdout);
	}
      if (status == 10)
	{
	  for (int i = 0; i < k; i++)
	    {
	      fputs ("./configure", stdout);
	      for (int p = 0; p < noptions; p++)
		{
		  int lit = option[i][p];
		  int value = satch_val (solver, lit);
		  if (value != lit)
		    continue;
		  fputc (' ', stdout);
		  fputs (shorten (options[p]), stdout);
		}
	      fputc ('\n', stdout);
	    }
	}
      satch_release (solver);
    }

  // Release memory for tables (for this '<k>').  Reusing this memory does
  // not make sense since the SAT solver will occupy at least as much.

  for (int i = 0; i < k; i++)
    free (option[i]);
  free (option);

  for (int i = 0; i < k; i++)
    {
      for (int p = 0; p + 1 < noptions; p++)
	free (pair[i][p]);
      free (pair[i]);
    }
  free (pair);

  return status == 10;
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
      else if (!strcmp (arg, "-v") || !strcmp (arg, "--verbose"))
	set (&verbose, arg);
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

  if (invalid && !all)
    die ("can only use '%s' with '-a' or '--all'", invalid);

  init_valid ();

  if (all)
    {
      config = allocate (k * sizeof *config);
      for (int i = 0; i <= k; i++)
	generate (0, i);
      free (config);
    }
  else if (dimacs)
    (void) encode (k);
  else
    {
      int i = 1;
      while (!encode (i++))
	;
      if (verbose)
	printf ("c used %.2f seconds in total\n", satch_process_time ());
    }

  reset_valid ();

  return 0;
}

/*------------------------------------------------------------------------*/

// This is an online proof checker with the same semantics as the DRAT format
// of the SAT competition.  More precisely we only have DRUP semantics,
// where added clauses are implied by the formula (also called "asymmetric
// tautologies" or AT).  It checks learned clauses and deletion of clauses
// on-the-fly in a slow forward manner and thus is meant for testing and
// debugging purposes only.  The code depends on the header-only-file
// implementation of a generic stack in 'stack.h'. Therefore this checker
// can easily be used for other SAT solvers by just linking against
// 'catch.o' and using the API in 'catch.h'.  A failure triggers a call to
// 'abort ()'.  For satisfiable instances we also check at the very end
// (during 'checker_release') that all clauses ever added which are not
// root-level satisfied have been deleted.  This is stronger than what is
// expected by DRUP/DRAT and useful to find clauses that have been forgotten
// to be deleted (from the checker or in general have been 'lost').

/*------------------------------------------------------------------------*/

#include "catch.h"
#include "colors.h"
#include "stack.h"

/*------------------------------------------------------------------------*/

// We want to make the code more portable by keeping dependencies at a
// minimum.  For instance we use 'size_t' and not 'uint64_t' for statistics
// counters to avoid including '<stdint.h>' and '<intypes.h>' headers.

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*------------------------------------------------------------------------*/

#define INVALID				UINT_MAX
#define MAX_SIZE_T			(~(size_t)0)
#define GARBAGE_COLLECTION_INTERVAL	10000

/*------------------------------------------------------------------------*/

static unsigned
LITERAL (unsigned idx)
{
  return idx << 1;
}

static unsigned
NOT (unsigned lit)
{
  return lit ^ 1;
}

/*------------------------------------------------------------------------*/

struct clause
{
  struct clause *next[2];	// As in 'PicoSAT' and original 'Chaff'.
  unsigned size;		// The size of the variadic literal array.
  unsigned literals[];		// The actual literals of 'size'.
};

struct checker
{
  size_t size;			// Number of allocated literals.
  int inconsistent;		// Empty clause added or learned.
  signed char *marks;		// Mark bits for clause simplification
  signed char *values;		// Values '-1', '0', '1'.
  struct clause **watches;	// Singly linked lists through 'next'.

  struct unsigned_stack trail;	// Partial assignment trail.
  struct unsigned_stack clause;	// Temporary clause added or deleted.

  // Limits to control garbage collection frequency (and avoid thrashing).
  //
  unsigned new_units;
  size_t wait_to_collect_satisfied_clauses;

  // Statistics
  //
  size_t original, learned, deleted;
  size_t collected, collections;
  size_t clauses, remained;

  int leak_checking;		// Enable leak checking at the end.
  int verbose;			// Print (few) verbose messages.
#ifdef LOGGING
  int logging;			// Log all calls.
#endif
};

/*------------------------------------------------------------------------*/

static unsigned
SIGN (unsigned lit)
{
  return lit & 1;
}

static unsigned
INDEX (unsigned lit)
{
  return lit >> 1;
}

static int
checker_export (unsigned ilit)
{
  const unsigned iidx = INDEX (ilit);
  assert (iidx < (unsigned) INT_MAX - 1);
  const int eidx = iidx + 1;
  const int elit = SIGN (ilit) ? -eidx : eidx;
  return elit;
}

/*------------------------------------------------------------------------*/

static void checker_fatal_error (const char *, ...)
  __attribute__((format (printf, 1, 2)));

static void checker_failed (struct checker *, const char *, ...)
  __attribute__((format (printf, 2, 3)));

#define CHECKER_FATAL_ERROR_PREFIX \
do { \
  COLORS (2); \
  fflush (stdout); \
  fprintf (stderr, "%schecker: %sfatal error: %s", BOLD, RED, NORMAL); \
  va_list ap; \
  va_start (ap, msg); \
  vfprintf (stderr, msg, ap); \
  va_end (ap); \
} while (0)

static void
checker_fatal_error (const char *msg, ...)
{
  CHECKER_FATAL_ERROR_PREFIX;
  fputc ('\n', stderr);
  fflush (stderr);
  abort ();
}

static void
checker_failed (struct checker *checker, const char *msg, ...)
{
  CHECKER_FATAL_ERROR_PREFIX;
  fputc ('\n', stderr);
  for (all_elements_on_stack (unsigned, lit, checker->clause))
      fprintf (stderr, "%d ", checker_export (lit));
  fputs ("0\n", stderr);
  fflush (stderr);
  abort ();
}

#define checker_prefix "c [checker] "

#ifdef LOGGING
#define logging_prefix "c CHECKER "
#endif

// The 'stack.h' code calls 'fatal_error' in case of out-of-memory and thus
// we just define a macro here to refer to the checker internal fatal-error
// function above.  Defining it here after 'stack.h' has been included above
// is fine, since 'stack.h' is only using macros to implement a stack.

#define fatal_error checker_fatal_error

#define RESIZE(NAME) \
do { \
  const size_t old_bytes = old_size * sizeof *checker->NAME; \
  const size_t new_bytes = new_size * sizeof *checker->NAME; \
  void * chunk = calloc (new_bytes, 1); \
  if (!chunk) \
    fatal_error ("out-of-memory resizing '" #NAME "'"); \
  if (old_bytes) \
    memcpy (chunk, checker->NAME, old_bytes); \
  free (checker->NAME); \
  checker->NAME = chunk; \
} while (0)

// The importing and resizing code is slightly easier here since we do not
// care about the actual number of variables but simply always increase the
// size to match the largest literal we have seen (and its negation).

static unsigned
checker_import (struct checker *checker, int elit)
{
  assert (elit);
  assert (elit != INT_MIN);
  const unsigned eidx = abs (elit);
  const unsigned iidx = eidx - 1;
  const unsigned ilit = LITERAL (iidx) + (elit < 0);
  assert ((ilit | 1) < UINT_MAX);
  const size_t required_size = (ilit | 1) + 1;
  const size_t old_size = checker->size;
  if (required_size > old_size)
    {
      assert (old_size <= (~(size_t) 0) / 2);
      size_t new_size = old_size ? 2 * old_size : 1;
      while (required_size > new_size)
	new_size *= 2;
      RESIZE (marks);
      RESIZE (values);
      RESIZE (watches);
      checker->size = new_size;
    }
  return ilit;
}

/*------------------------------------------------------------------------*/

// Trivial clauses are neither added nor deleted.  A clause is trivial it if
// contains two clashing literals or contains a literal assigned to 'true'.

static int
checker_trivial_clause (struct checker *checker)
{
  assert (EMPTY_STACK (checker->trail));	// Otherwise backtrack first.

  const signed char *values = checker->values;
  signed char *marks = checker->marks;

  const unsigned *const end = checker->clause.end;
  unsigned *const begin = checker->clause.begin;

  unsigned *q = begin;
  int trivial = 0;

  for (unsigned *p = begin; p != end; p++)
    {
      unsigned lit = *p;
      assert (lit < checker->size);
      const signed char value = values[lit];
      if (value > 0)
	{
	  trivial = 1;
	  break;
	}
      if (marks[lit])
	continue;
      if (marks[NOT (lit)])
	{
	  trivial = 1;
	  break;
	}
      marks[lit] = 1;
      *q++ = lit;
    }
  checker->clause.end = q;
  return trivial;
}

// Unmark the literals marked above.

static void
checker_clear_clause (struct checker *checker)
{
  const unsigned *const end = checker->clause.end;
  unsigned *const begin = checker->clause.begin;
  signed char *marks = checker->marks;
  for (unsigned *p = begin; p != end; p++)
    {
      const unsigned lit = *p;
      assert (lit < checker->size);
      assert (marks[lit]);
      marks[lit] = 0;
    }
  CLEAR_STACK (checker->clause);
}

/*------------------------------------------------------------------------*/

// We do not need decision levels.  Everything on the trail is either
// unassigned or if the propagation started from added units then all the
// implied literals are permanently forced to that value.  In any case the
// trail is forced to become empty after unit propagation completes.

static void
checker_assign (struct checker *checker, unsigned lit)
{
  const unsigned not_lit = NOT (lit);
  assert (lit < checker->size);
  assert (not_lit < checker->size);
  signed char *values = checker->values;
  assert (!values[lit]);
  assert (!values[not_lit]);
  values[not_lit] = -1;
  values[lit] = 1;
  PUSH (checker->trail, lit);
}

// This is standard boolean constraint propagation until completion.  The
// function returns zero iff a conflict was found.  Otherwise watch lists
// are used and updated.  The watching scheme follows the one from 'PicoSAT'
// and the original 'Chaff' SAT solvers with two links in each clause for
// the two watched literals at the first two positions.  Replacement of
// watches is otherwise standard.  We do not use blocking literals though.

static int
checker_propagate (struct checker *checker)
{
  const signed char *const values = checker->values;
  struct clause **const watches = checker->watches;

  size_t propagate = 0;

  while (propagate < SIZE_STACK (checker->trail))
    {
      const unsigned lit = ACCESS (checker->trail, propagate);
      propagate++;

      const unsigned not_lit = NOT (lit);
      assert (not_lit < checker->size);

      struct clause **p = watches + not_lit;
      struct clause *c;

      while ((c = *p))
	{
	  const size_t size = c->size;
	  assert (size > 1);
	  unsigned *literals = c->literals;
	  const unsigned *end = literals + size;
	  const unsigned pos = (literals[1] == not_lit);
	  assert (literals[pos] == not_lit);
	  const unsigned other = literals[!pos];
	  const signed char other_value = values[other];
	  if (other_value > 0)
	    {
	      p = &c->next[pos];
	      continue;
	    }
	  unsigned replacement = INVALID;
	  signed char replacement_value = -1;
	  unsigned *r;
	  for (r = literals + 2; r != end; r++)
	    {
	      replacement = *r;
	      replacement_value = values[replacement];
	      if (replacement_value >= 0)
		break;
	    }
	  if (replacement_value >= 0)
	    {
	      *r = not_lit;
	      *p = c->next[pos];
	      literals[pos] = replacement;
	      c->next[pos] = watches[replacement];
	      watches[replacement] = c;
	    }
	  else if (other_value < 0)
	    return 0;
	  else
	    {
	      assert (!other_value);
	      checker_assign (checker, other);
	      p = &c->next[pos];
	    }
	}
    }
  return 1;
}

// Backtracking just pops literals from the trail and unassigns them.

static void
checker_backtrack (struct checker *checker)
{
  signed char *values = checker->values;
  while (!EMPTY_STACK (checker->trail))
    {
      const unsigned lit = POP (checker->trail);
      const unsigned not_lit = NOT (lit);
      assert (values[not_lit] < 0);
      assert (values[lit] > 0);
      values[lit] = values[not_lit] = 0;
    }
}

/*------------------------------------------------------------------------*/

// We do not use a global stack of clauses and thus can only reach all
// clauses through the watch lists.  For garbage collection as well as
// deleting clauses during releasing the checker we need to make sure not to
// traverse deleted clauses though.  The strategy to avoid this is as
// follows. We first disconnect from all clauses the second watch.  Then
// deleting clauses can be done by following first watch links only.

static void
checker_disconnect_second_watch (unsigned lit, struct clause **p)
{
  struct clause *c;
  while ((c = *p))
    {
      const unsigned pos = (c->literals[1] == lit);
      assert (c->literals[pos] == lit);
      if (pos)
	{
	  *p = c->next[1];
#ifndef NDEBUG
	  c->next[1] = 0;	// See assertion ASSERTION below.
#endif
	}
      else
	p = &c->next[0];
    }
}

// After deleting satisfied clauses during garbage collection we need to
// watch the second literals in each clause again. This is slightly more
// tricky since the order in which we add those watches can be random and
// thus when we traverse the first literal links we might occasionally
// already use that literal as second watch in the watch list.  For
// 'checker_release_clauses' this is not possible.

static void
checker_reconnect_second_watch (unsigned lit, struct clause **watches)
{
  for (struct clause * c = watches[lit], *next; c; c = next)
    {
      if (c->literals[0] == lit)
	{
	  const unsigned other = c->literals[1];
	  assert (!c->next[1]);	// ASSERTION
	  c->next[1] = watches[other];
	  watches[other] = c;
	  next = c->next[0];
	}
      else
	{
	  assert (c->literals[1] == lit);
	  next = c->next[1];
	}
    }
}

/*------------------------------------------------------------------------*/

// While the two functions above work on watches of individual literals the
// following two functions go over all literals (using the former though).

static void
checker_disconnect_all_second_watches (struct checker *checker)
{
  struct clause **watches = checker->watches;
  for (size_t lit = 0; lit < checker->size; lit++)
    checker_disconnect_second_watch (lit, watches + lit);
}

static void
checker_reconnect_all_second_watches (struct checker *checker)
{
  struct clause **watches = checker->watches;
  for (size_t lit = 0; lit < checker->size; lit++)
    checker_reconnect_second_watch (lit, watches);
}

/*------------------------------------------------------------------------*/

// We find and collect root-level satisfied clauses in garbage collections
// and need to make sure not to thrash the checker with redundant work.
// Thus we delay garbage collection in arithmetically increasing intervals
// and also only perform garbage collection if new units have been added
// since the last garbage collection.

static void
checker_schedule_next_garbage_collection (struct checker *checker)
{
  const size_t collections = checker->collections;
  size_t wait;
  assert (GARBAGE_COLLECTION_INTERVAL);
  if (MAX_SIZE_T / GARBAGE_COLLECTION_INTERVAL < collections)
    wait = MAX_SIZE_T;
  else
    wait = collections * GARBAGE_COLLECTION_INTERVAL;
  checker->new_units = 0;
  checker->wait_to_collect_satisfied_clauses = wait;
}

// This is similar to connecting / reconnecting watches above.  We assume
// that we only have first literals watched.  Then we can traverse those
// first literal links and check a clause for being satisfied.  If we find a
// satisfied clause we disconnect and delete it from the watch list.

static size_t
checker_flush_satisfied_clauses (struct checker *checker, unsigned lit,
				 struct clause **const watches,
				 const signed char *const values)
{
  struct clause **p = watches + lit, *c;
  size_t collected = 0;
  while ((c = *p))
    {
      const unsigned *const literals = c->literals;
      assert (literals[0] == lit);
      const unsigned *const end = literals + c->size;
      int satisfied = 0;
      for (const unsigned *p = literals; !satisfied && p != end; p++)
	{
	  const unsigned other = *p;
	  const signed char value = values[other];
	  satisfied = (value > 0);
	}
      if (satisfied)
	{
	  collected++;
	  *p = c->next[0];
	  assert (checker->clauses);
	  checker->clauses--;
	  free (c);
	}
      else
	p = c->next;
    }
  return collected;
}

// Applies the above function to all literals and collects and prints
// statistics (the latter only if verbose messages are enabled).

static void
checker_flush_all_satisfied_clauses (struct checker *checker)
{
  assert (EMPTY_STACK (checker->trail));

  size_t collected = 0;

  struct clause **const watches = checker->watches;
  const signed char *const values = checker->values;

  for (size_t lit = 0; lit < checker->size; lit++)
    collected += checker_flush_satisfied_clauses (checker, lit,
						  watches, values);
  checker->collected += collected;

  if (checker->verbose)
    printf (checker_prefix "collected %zu satisfied clauses "
	    "in garbage collection %zu\n", collected,
	    checker->collections), fflush (stdout);
}

// The satisfied clause garbage collection function.

static void
checker_garbage_collection (struct checker *checker)
{
  checker->collections++;
  checker_disconnect_all_second_watches (checker);
  checker_flush_all_satisfied_clauses (checker);
  checker_reconnect_all_second_watches (checker);
  checker_schedule_next_garbage_collection (checker);
}

/*------------------------------------------------------------------------*/

// Add and watch a clause unless the clause is empty or a unit clause.  In
// the last case the unit is assigned and propagated instead.  The user code
// of course allowed to add clauses which contain literals falsified by
// the checker assignment but later might actually delete them as is (with
// the falsified literals still in it).  Therefore we also add falsified
// literals, otherwise we can not find the extended clause later.

static void
checker_add_clause (struct checker *checker)
{
  const signed char *const values = checker->values;

  const unsigned *const end = checker->clause.end;
  unsigned *const begin = checker->clause.begin;
  unsigned *q = begin;

  unsigned unit = INVALID;
  size_t non_false = 0;

  for (unsigned *p = begin; p != end; p++)
    {
      const unsigned lit = *p;
      assert (lit < checker->size);
      const signed char value = values[lit];
      assert (value <= 0);
      if (value < 0)
	continue;
      if (p != q)
	{
	  *p = *q;
	  *q = lit;
	}
      q++;
      if (!non_false++)
	unit = lit;
      if (non_false > 1)
	break;
    }

  if (!non_false)
    checker->inconsistent = 1;
  else if (non_false == 1)
    {
      assert (unit != INVALID);
      assert (unit == begin[0]);
      checker_assign (checker, unit);
      assert (checker->new_units < UINT_MAX);
      checker->new_units++;	// For garbage collection!
      if (checker_propagate (checker))
	CLEAR_STACK (checker->trail);	// We are done, reset trail!
      else
	checker->inconsistent = 1;
    }
  else
    {
      const unsigned lit = begin[0];
      const unsigned other = begin[1];
      assert (lit == unit);
      assert (!values[lit]);
      assert (!values[other]);
      const size_t size = SIZE_STACK (checker->clause);
      assert (2 <= size);
      assert (size <= UINT_MAX);
      const size_t bytes = sizeof (struct clause) + size * sizeof (unsigned);
      struct clause *clause = malloc (bytes);
      if (!clause)
	fatal_error ("out-of-memory allocating clause of size %zu", size);
      assert (checker->clauses < MAX_SIZE_T);
      checker->clauses++;
      struct clause **const watches = checker->watches;
      clause->next[0] = watches[lit];
      clause->next[1] = watches[other];
      watches[lit] = watches[other] = clause;
      clause->size = size;
      memcpy (clause->literals, begin, size * sizeof (unsigned));
    }

  if (checker->wait_to_collect_satisfied_clauses)
    --checker->wait_to_collect_satisfied_clauses;

  if (!checker->inconsistent && checker->new_units &&
      !checker->wait_to_collect_satisfied_clauses)
    checker_garbage_collection (checker);
}

/*------------------------------------------------------------------------*/

// The delete function works in a similar way but uses mark flags set in
// 'checker_trivial_clause' to compare clauses.  We try all literals, which
// is slightly redundant (a one watch scheme for finding the clause would be
// enough).  In principle we could skip one literal (say the one with the
// longest watch list).  On the other removing the clause requires to walk
// that list anyhow, and thus this optimization would not give much.

static void
checker_internal_delete_clause (struct checker *checker)
{
  const size_t size = SIZE_STACK (checker->clause);
  assert (size < UINT_MAX);

  struct clause **const watches = checker->watches;
  signed char *marks = checker->marks;

  for (all_elements_on_stack (unsigned, lit, checker->clause))
    {
      // First search for the link 'cp' which points to the clause 'c' which
      // matches the marked literals in the temporary clause.
      //
      struct clause **cp, *c, **cnext = 0;

      for (cp = watches + lit; (c = *cp); cp = cnext)
	{
	  const unsigned *const clits = c->literals;
	  const unsigned cpos = (clits[1] == lit);
	  assert (clits[cpos] == lit);
	  cnext = c->next + cpos;

	  if (c->size != size)	// Size has to match.
	    continue;

	  const unsigned *const cend = clits + c->size, *cq;
	  for (cq = clits; cq != cend; cq++)
	    if (!marks[*cq])
	      break;		// Literal '*cq' not in temporary clause.

	  if (cq != cend)	// Not all literals marked.
	    continue;

	  // Now 'c' has exactly the literals as the temporary clause.

	  *cp = *cnext;		// Remove 'lit' watch on 'c'.

	  const unsigned other = clits[!cpos];	// The other watched literal.

	  // Then find the link 'dp' to 'c' but walking the watched list of
	  // the other watched literal 'other' in 'c'.

	  struct clause **dp = watches + other, *d;

	  while ((d = *dp) != c)
	    {
	      assert (d);	// The clause has to be found.

	      const unsigned *const dlits = d->literals;
	      const unsigned dpos = (dlits[1] == other);
	      assert (dlits[dpos] == other);

	      dp = d->next + dpos;
	    }

	  *dp = c->next[!cpos];	// Remove 'other' watch.

	  assert (checker->clauses);
	  checker->clauses--;

	  free (c);

	  return;
	}
    }

  checker_failed (checker, "clause requested to delete not found");
}

/*------------------------------------------------------------------------*/

// The most important function checking that an added clause is implied
// is now rather easy to implement after propagation is in place.
// It goes over the literals in the temporary clause and propagates their
// negation (unless the literal is already assigned).  If the literal is
// 'true' then the clause is clearly satisfied and thus implied.  If it is
// false we can skip it.  Otherwise we just assign the literal in the clause
// to 'false' and propagate.  If propagation fails (a conflict was found)
// 'failed' is set to 'true' as well and we have proven that the temporary
// clause is unit implied.  If at the end no conflict was produced the
// clause is not unit implied and we raise a fatal-error message.

static void
check_clause_implied (struct checker *checker)
{
  assert (EMPTY_STACK (checker->trail));
  const signed char *const values = checker->values;
  int failed = 0;
  for (all_elements_on_stack (unsigned, lit, checker->clause))
    {
      const signed char value = values[lit];
      if (value > 0)
	failed = 1;
      else if (!value)
	{
	  const unsigned not_lit = NOT (lit);
	  checker_assign (checker, not_lit);
	  if (!checker_propagate (checker))
	    failed = 1;
	}
      if (failed)
	break;
    }

  if (!failed)
    checker_failed (checker, "learned clause not implied");

  checker_backtrack (checker);
}

/*------------------------------------------------------------------------*/

static void
checker_release_clauses (struct checker *checker, struct clause *c)
{
  const signed char *const values = checker->values;

  if (!EMPTY_STACK (checker->trail))
    checker_backtrack (checker);

  while (c)
    {
      struct clause *next = c->next[0];
      assert (!c->next[1]);
      const unsigned *const literals = c->literals;
      const unsigned *const end = literals + c->size;
      int satisfied = 0;
      for (const unsigned *p = literals; !satisfied && p != end; p++)
	satisfied = (values[*p] > 0);
      if (!satisfied)
	checker->remained++;
      assert (checker->clauses);
      checker->clauses--;
      free (c);
      c = next;
    }
}

static void
checker_release_all_clauses (struct checker *checker)
{
  checker_disconnect_all_second_watches (checker);
  for (size_t lit = 0; lit < checker->size; lit++)
    checker_release_clauses (checker, checker->watches[lit]);
}

/*------------------------------------------------------------------------*/

static void
checker_invalid_usage (const char *message, const char *function)
{
  COLORS (2);
  fprintf (stderr, "%schecker: %sfatal error: "
	   "%sinvalid API usage in '%s': %s\n",
	   BOLD, RED, NORMAL, function, message);
  fflush (stderr);
  abort ();
}

// Macros to enforce valid API usage.

#define REQUIRE(CONDITION,MESSAGE) \
do { \
  if (!(CONDITION)) \
    checker_invalid_usage (MESSAGE, __func__); \
} while (0)

#define REQUIRE_NON_ZERO_CHECKER() \
  REQUIRE (checker, "zero checker argument")

/*------------------------------------------------------------------------*/

static double
percent (double a, double b)
{
  return b ? 100.0 * a / b : 0;
}

static void
checker_statistics (struct checker *checker)
{
  const size_t original = checker->original;
  const size_t learned = checker->learned;
  const size_t deleted = checker->deleted;
  const size_t collected = checker->collected;
  const size_t total = original + learned;

  // *INDENT-OFF*

  printf (checker_prefix
          "added %zu original clauses %.0f%%\n"
	  checker_prefix
	  "checked %zu learned clauses %.0f%%\n"
	  checker_prefix
	  "found and deleted %zu clauses %.0f%%\n"
	  checker_prefix
	  "collected %zu satisfied clauses %.0f%%\n"
	  checker_prefix
	  "triggered %zu garbage collections\n"
	  checker_prefix
	  "%zu clauses remained\n",
	  original, percent (original, total),
	  learned, percent (learned, total),
	  deleted, percent (deleted, total),
	  collected, percent (collected, total),
	  checker->collections, checker->remained);

  // *INDENT-ON*

  fflush (stdout);
}

/*------------------------------------------------------------------------*/

#ifdef LOGGING

static void
checker_log_clause (struct checker *checker, const char *type)
{
  COLORS (1);
  COLOR (MAGENTA);
  assert (checker->logging);
  fputs (logging_prefix, stdout);
  fputs (type, stdout);
  for (all_elements_on_stack (unsigned, lit, checker->clause))
      printf (" %d", checker_export (lit));
  COLOR (NORMAL);
  fputc ('\n', stdout);
  fflush (stdout);
}

#endif

/*========================================================================*/
//      Non-static functions defined by the API are put below.            //
/*========================================================================*/

struct checker *
checker_init (void)
{
  struct checker *checker = calloc (1, sizeof *checker);
  if (!checker)
    fatal_error ("out-of-memory allocating checker");
  checker->wait_to_collect_satisfied_clauses = GARBAGE_COLLECTION_INTERVAL;
  return checker;
}

void
checker_verbose (struct checker *checker)
{
  assert (checker);
  checker->verbose = 1;
  printf (checker_prefix "enabling verbose mode of internal proof checker\n");
  fflush (stdout);
}

void
checker_logging (struct checker *checker)
{
#ifdef LOGGING
  assert (checker);
  checker->logging = 1;
  printf (logging_prefix "enabling logging mode of internal proof checker\n");
  fflush (stdout);
#else
  (void) checker;
#endif
}

void
checker_enable_leak_checking (struct checker *checker)
{
  assert (checker);
  checker->leak_checking = 1;
  if (!checker->verbose)
    return;
  printf (checker_prefix
	  "enabling leak checking of internal proof checker\n");
  fflush (stdout);
}

void
checker_release (struct checker *checker)
{
  REQUIRE_NON_ZERO_CHECKER ();
  checker_release_all_clauses (checker);
  if (checker->verbose)
    checker_statistics (checker);
  if (!checker->inconsistent && checker->leak_checking && checker->remained)
    {
      if (checker->remained == 1)
	fatal_error ("exactly one clause remains");
      else
	fatal_error ("%zu clauses remain", checker->remained);
    }
  free (checker->marks);
  free (checker->values);
  free (checker->watches);
  RELEASE_STACK (checker->clause);
  RELEASE_STACK (checker->trail);
  free (checker);
}

/*------------------------------------------------------------------------*/

void
checker_add_literal (struct checker *checker, int elit)
{
  REQUIRE_NON_ZERO_CHECKER ();
  REQUIRE (elit, "zero literal argument");
  REQUIRE (elit != INT_MIN, "'INT_MIN' literal argument");
  assert (elit);
  assert (elit != INT_MIN);
  unsigned ilit = checker_import (checker, elit);
  PUSH (checker->clause, ilit);
}

/*------------------------------------------------------------------------*/

void
checker_add_original_clause (struct checker *checker)
{
  REQUIRE_NON_ZERO_CHECKER ();
#ifdef LOGGING
  if (checker->logging)
    checker_log_clause (checker, "original");
#endif
  if (checker->inconsistent)
    {
      CLEAR_STACK (checker->clause);
      return;
    }
  checker->original++;
  if (!checker_trivial_clause (checker))
    checker_add_clause (checker);
  checker_clear_clause (checker);
}

void
checker_add_learned_clause (struct checker *checker)
{
  REQUIRE_NON_ZERO_CHECKER ();
#ifdef LOGGING
  if (checker->logging)
    checker_log_clause (checker, "learned");
#endif
  if (checker->inconsistent)
    {
      CLEAR_STACK (checker->clause);
      return;
    }
  checker->learned++;
  check_clause_implied (checker);
  if (!checker_trivial_clause (checker))
    checker_add_clause (checker);
  checker_clear_clause (checker);
}

void
checker_delete_clause (struct checker *checker)
{
  REQUIRE_NON_ZERO_CHECKER ();
#ifdef LOGGING
  if (checker->logging)
    checker_log_clause (checker, "delete");
#endif
  if (checker->inconsistent)
    {
      CLEAR_STACK (checker->clause);
      return;
    }
  checker->deleted++;
  if (!checker_trivial_clause (checker))
    checker_internal_delete_clause (checker);
  checker_clear_clause (checker);
}

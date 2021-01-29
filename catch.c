/*------------------------------------------------------------------------*/
//   Copyright (c) 2021, Armin Biere, Johannes Kepler University Linz     //
/*------------------------------------------------------------------------*/

// This is an online proof checker with the same semantics as the DRAT format
// of the SAT competition.  More precisely we only have DRUP semantics,
// where added clauses are implied by the formula (asymmetric tautologies).
// It checks learned clauses and deletion of clauses on-the-fly in a forward
// way and thus is meant for testing and debugging purposes only.  The code
// depends on the header-only-file implementation of a generic stack in
// 'stack.h'. Therefore this checker can easily be used for other SAT
// solvers by just linking against 'catch.o' and using the API in 'catch.h'.
// A failure triggers a call to 'abort ()'.

/*------------------------------------------------------------------------*/

#include "catch.h"
#include "stack.h"

/*------------------------------------------------------------------------*/

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*------------------------------------------------------------------------*/

#define INVALID UINT_MAX

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
  struct clause *next[2];
  unsigned size;
  unsigned literals[];
};

struct checker
{
  size_t size;
  bool inconsistent;
  signed char *marks;
  signed char *values;
  struct clause **watches;
  struct unsigned_stack clause;
  struct unsigned_stack trail;
};

/*------------------------------------------------------------------------*/

static void checker_fatal_error (const char *, ...)
  __attribute__((format (printf, 1, 2)));

static void
checker_fatal_error (const char *msg, ...)
{
  fputs ("checker: fatal error: ", stderr);
  va_list ap;
  va_start (ap, msg);
  vfprintf (stderr, msg, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  abort ();
}

// The 'stack.h' code calls 'fatal_error' in case of out-of-memory and thus
// we just define a macro to refer to the checker internal fatal error
// message.  Defining it here after 'stack.h' has been included above is
// fine since 'stack.h' is only using macros to implement a stack.

#define fatal_error checker_fatal_error

#define RESIZE(NAME) \
do { \
  const size_t old_bytes = old_size * sizeof *checker->NAME; \
  const size_t new_bytes = new_size * sizeof *checker->NAME; \
  void * chunk = calloc (new_bytes, 1); \
  if (!chunk) \
    fatal_error ("out-of-memory resizing '" #NAME "'"); \
  memcpy (chunk, checker->NAME, old_bytes); \
  free (checker->NAME); \
  checker->NAME = chunk; \
} while (0)

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

static bool
checker_trivial_clause (struct checker *checker)
{
  const signed char *values = checker->values;
  signed char *marks = checker->marks;

  const unsigned *const end = checker->clause.end;
  unsigned *const begin = checker->clause.begin;

  unsigned *q = begin;
  bool trivial = false;

  for (unsigned *p = begin; p != end; p++)
    {
      unsigned lit = *p;
      assert (lit < checker->size);
      const signed char value = values[lit];
      if (value > 0)
	{
	  trivial = true;
	  break;
	}
      if (marks[lit])
	continue;
      if (marks[NOT (lit)])
	{
	  trivial = true;
	  break;
	}
      marks[lit] = 1;
      *q++ = lit;
    }
  checker->clause.end = q;
  return trivial;
}

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
  CLEAR (checker->clause);
}

/*------------------------------------------------------------------------*/

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

static bool
checker_propagate (struct checker *checker)
{
  const signed char *const values = checker->values;
  struct clause **const watches = checker->watches;

  size_t propagate = 0;

  while (propagate < SIZE (checker->trail))
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
	    return false;
	  else
	    {
	      assert (!other_value);
	      checker_assign (checker, other);
	      p = &c->next[pos];
	    }
	}
    }
  return true;
}

static void
checker_backtrack (struct checker *checker)
{
  signed char *values = checker->values;
  while (!EMPTY (checker->trail))
    {
      const unsigned lit = POP (checker->trail);
      const unsigned not_lit = NOT (lit);
      assert (values[not_lit] < 0);
      assert (values[lit] > 0);
      values[lit] = values[not_lit] = 0;
    }
}

/*------------------------------------------------------------------------*/

static void
checker_add_clause (struct checker *checker)
{
  const signed char *const values = checker->values;

  const unsigned * const end = checker->clause.end;
  unsigned * const begin = checker->clause.begin;
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
    checker->inconsistent = true;
  else if (non_false == 1)
    {
      assert (unit != INVALID);
      assert (unit == begin[0]);
      checker_assign (checker, unit);
      if (checker_propagate (checker))
	CLEAR (checker->trail);
      else
	checker->inconsistent = true;
    }
  else
    {
      const unsigned lit = begin[0];
      const unsigned other = begin[1];
      assert (lit == unit);
      assert (!values[lit]);
      assert (!values[other]);
      const size_t size = SIZE (checker->clause);
      assert (2 <= size);
      assert (size <= UINT_MAX);
      const size_t bytes = sizeof (struct clause) + size * sizeof (unsigned);
      struct clause *clause = malloc (bytes);
      if (!clause)
	fatal_error ("out-of-memory allocating clause of size %zu", size);
      struct clause **const watches = checker->watches;
      clause->next[0] = watches[lit];
      clause->next[1] = watches[other];
      watches[lit] = watches[other] = clause;
      clause->size = size;
      memcpy (clause->literals, begin, size * sizeof (unsigned));
    }
}

/*------------------------------------------------------------------------*/

static void
checker_remove_clause (struct checker *checker)
{
  struct clause *const *const watches = checker->watches;
  signed char *marks = checker->marks;

  const size_t size = SIZE (checker->clause);
  assert (size < UINT_MAX);

  for (all_elements_on_stack (unsigned, lit, checker->clause))
    {
      struct clause * c, * next;
      for (c = watches[lit]; c; c = next)
	{
	  const unsigned * const literals = c->literals;
	  const unsigned pos = (literals[1] == lit);
	  assert (literals[pos] == lit);
	  next = c->next[pos];

	  if (c->size != size)
	    continue;

	  const unsigned * const end = literals + c->size;
	  const unsigned * p;
	  for (p = literals; p != end; p++)
	    if (!marks[*p])
	      break;

	  if (p == end)
	    break;
	}
      if (c)
	return;
    }

  fatal_error ("clause requested to delete not found");
}

/*------------------------------------------------------------------------*/

static void
check_clause_implied (struct checker *checker)
{
  assert (EMPTY (checker->trail));
  const signed char *const values = checker->values;
  bool failed = false;
  for (all_elements_on_stack (unsigned, lit, checker->clause))
    {
      const signed char value = values[lit];
      if (value > 0)
	failed = true;
      else if (!value)
	{
	  const unsigned not_lit = NOT (lit);
	  checker_assign (checker, not_lit);
	  if (!checker_propagate (checker))
	    failed = true;
	}
      if (failed)
	break;
    }
  if (!failed)
    fatal_error ("learned clause not implied");
  checker_backtrack (checker);
}

/*------------------------------------------------------------------------*/

static void
checker_disconnect_second_watch (struct checker *checker,
				 unsigned lit, struct clause **p)
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
	  c->next[1] = 0;	// See assertion '(*)' below.
#endif
	}
      else
	p = &c->next[0];
    }
}

static void
checker_release_clauses (struct checker *checker, struct clause *c)
{
  while (c)
    {
      struct clause *next = c->next[0];
      assert (!c->next[1]);
      free (c);
      c = next;
    }
}

static void
checker_release_all_clauses (struct checker *checker)
{
  for (size_t lit = 0; lit < checker->size; lit++)
    checker_disconnect_second_watch (checker, lit, checker->watches + lit);
  for (size_t lit = 0; lit < checker->size; lit++)
    checker_release_clauses (checker, checker->watches[lit]);
}

/*------------------------------------------------------------------------*/

static void
checker_invalid_usage (const char *message, const char *function)
{
  fprintf (stderr,
	   "checker: invalid API usage in '%s': %s\n", function, message);
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

struct checker *
checker_init (void)
{
  struct checker *checker = calloc (1, sizeof *checker);
  if (!checker)
    fatal_error ("out-of-memory allocating checker");
  return checker;
}

void
checker_release (struct checker *checker)
{
  REQUIRE_NON_ZERO_CHECKER ();
  checker_release_all_clauses (checker);
  free (checker->marks);
  free (checker->values);
  free (checker->watches);
  RELEASE (checker->clause);
  RELEASE (checker->trail);
  free (checker);
}

/*------------------------------------------------------------------------*/

void
checker_add (struct checker *checker, int elit)
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
checker_original (struct checker *checker)
{
  REQUIRE_NON_ZERO_CHECKER ();
  if (checker->inconsistent)
    return;
  if (!checker_trivial_clause (checker))
    checker_add_clause (checker);
  checker_clear_clause (checker);
}

void
checker_learned (struct checker *checker)
{
  REQUIRE_NON_ZERO_CHECKER ();
  if (checker->inconsistent)
    return;
  check_clause_implied (checker);
  if (!checker_trivial_clause (checker))
    checker_add_clause (checker);
  checker_clear_clause (checker);
}

void
checker_remove (struct checker *checker)
{
  REQUIRE_NON_ZERO_CHECKER ();
  if (checker->inconsistent)
    return;
  if (!checker_trivial_clause (checker))
    checker_remove_clause (checker);
  checker_clear_clause (checker);
}

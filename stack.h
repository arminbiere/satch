#ifndef _stack_h_INCLUDED
#define _stack_h_INCLUDED

// Generic stack implementation similar to 'std::vector' API in C++.
// In order to use it you need to provide 'fatal_error' below which could
// also be a local macro in the user compilation unit since this
// implementation here is header only and also only uses macros (beside type
// issue this part would also not be that easy with inline functions).

#include <stdlib.h>		// For 'size_t', 'realloc', 'free'.

/*------------------------------------------------------------------------*/

// Predicates.

#define EMPTY(S) ((S).end == (S).begin)
#define FULL(S) ((S).end == (S).allocated)

/*------------------------------------------------------------------------*/

#define SIZE(S) ((size_t) ((S).end - (S).begin))
#define CAPACITY(S) ((size_t) ((S).allocated - (S).begin))

/*------------------------------------------------------------------------*/

#define INIT(S) \
do { \
  (S).end = (S).begin = (S).allocated = 0; \
} while (0)

#define RELEASE(S) \
do { \
  free ((S).begin); \
  (S).begin = (S).end = (S).allocated = 0; \
} while (0)

/*------------------------------------------------------------------------*/

// Duplicate size of stack.

#define ENLARGE(S) \
do { \
  const size_t old_size = SIZE (S); \
  const size_t old_capacity = CAPACITY (S); \
  const size_t new_capacity = old_capacity ? 2*old_capacity : 1; \
  const size_t new_bytes = new_capacity * sizeof *(S).begin; \
  (S).begin = realloc ((S).begin, new_bytes); \
  if (!(S).begin) \
    fatal_error ("out-of-memory reallocating '%zu' bytes", new_bytes); \
  (S).end = (S).begin + old_size; \
  (S).allocated = (S).begin + new_capacity; \
} while (0)

#define PUSH(S,E) \
do { \
  if (FULL (S)) \
    ENLARGE (S); \
  *(S).end++ = (E); \
} while (0)

/*------------------------------------------------------------------------*/

// Flush all elements.

#define CLEAR(S) \
do { \
  (S).end = (S).begin; \
} while (0)

/*------------------------------------------------------------------------*/

// Access element at a certain position (also works as 'lvalue').

#define ACCESS(S,I) \
  ((S).begin[assert ((size_t) (I) < SIZE(S)), (I)])

/*------------------------------------------------------------------------*/

// Access least recently added 'last' element of stack.

#define TOP(S) \
  (assert (!EMPTY (S)), (S).end[-1])

#define POP(S) \
  (assert (!EMPTY (S)), *--(S).end)

/*------------------------------------------------------------------------*/

// Common types of stacks.

struct unsigned_stack		// Generic stack with 'unsigned' elements.
{
  unsigned *begin, *end, *allocated;
};

struct int_stack		// Generic stack with 'int' elements.
{
  int *begin, *end, *allocated;
};

/*------------------------------------------------------------------------*/

// Explicitly typed iterator over non-pointer stack elements, e.g.,
//
//   struct int_stack stack;
//   INIT (stack);
//   for (int i = 0; i < 10; i++)
//     PUSH (stack, i);
//   for (all_elements_on_stack (int, i, stack))
//     printf ("%d\n", i);
//   RELEASE (stack);
//
// pushes the integers 0,...,9 onto a stack and then prints its elements.

#define all_elements_on_stack(TYPE,E,S) \
  TYPE E, * PTR_ ## E = (S).begin, * const END_ ## E = (S).end; \
  (PTR_ ## E != END_ ## E) && (E = *PTR_ ## E, true); ++PTR_ ## E

// For pointer elements we need additional '*'s in the declaration and
// the 'TYPE' argument is the base type of the pointer.  To iterate a stack
// of 'struct clause *' use 'for (all_pointers_on_stack (clause, c, S))'.

#define all_pointers_on_stack(TYPE,E,S) \
  TYPE * E, ** PTR_ ## E = (S).begin, ** const END_ ## E = (S).end; \
  (PTR_ ## E != END_ ## E) && (E = *PTR_ ## E, true); ++PTR_ ## E

/*------------------------------------------------------------------------*/

#endif

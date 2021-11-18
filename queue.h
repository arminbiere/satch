#ifndef _queue_h_INCLUDED
#define _queue_h_INCLUDED

/*------------------------------------------------------------------------*/

// This is a simple almost generic implementation of a queue with enqueue
// and dequeue operations.  It is only 'almost' generic since it requires
// too much memory in case the number of elements in the queue remains much
// smaller than the number of enqueued elements. For such a use case you
// should not use this queue.  See also the comment below before 'DEQUEUE'.

/*------------------------------------------------------------------------*/

#include "stack.h"

struct int_queue
{
  struct int_stack stack;
  int *head;
};

/*------------------------------------------------------------------------*/

#define EMPTY_QUEUE(Q) ((Q).stack.end == (Q).head)
#define SIZE_QUEUE(Q) ((size_t)((Q).stack.end - (Q).head))

#define INIT_QUEUE(Q) \
  do { INIT_STACK ((Q).stack); (Q).head = (Q).stack.begin; } while (0)

#define RELEASE_QUEUE(Q) RELEASE_STACK ((Q).stack)

/*------------------------------------------------------------------------*/

// Enqueuing an elements amounts to push it on the stack and adapt the
// 'head' pointer in case the stack has to be enlarged (which we do here
// explicitly but borrowing the 'ENLARGE_STACK' operation from stacks).

#define ENQUEUE(Q,E) \
do { \
  if (FULL_STACK ((Q).stack)) \
    { \
      size_t OFFSET = (Q).head - (Q).stack.begin; \
      ENLARGE_STACK ((Q).stack); \
      (Q).head = (Q).stack.begin + OFFSET; \
    } \
  *(Q).stack.end++ = (E); \
} while (0)

/*------------------------------------------------------------------------*/

// This is too simplistic in general, since we will allocate as much stack
// memory as there are 'ENQUEUE' operations.  If for instance the stack size
// is twice as big as the queue size, we might want to shrink everything.

#define DEQUEUE(Q) \
  (assert (!EMPTY_QUEUE (Q)), *(Q).head++)

/*------------------------------------------------------------------------*/

#endif

#ifndef _features_h_INCLUDED
#define _features_h_INCLUDED

/*------------------------------------------------------------------------*/

// The first set of the following checks are in essence documentation. The
// 'configure' script would on purpose leave out the implied macro and raise
// a fatal error if you try to use both. Beside documentation they also make
// sure that compiling the solver directly without relying on 'configure' to
// do sanity checking on the selected feature macros, that we still get the
// same consistent option setting.  For instance, in the actual solver code
// below we might have places where the implied macro is used without
// checking the stronger implying one and then forcing the implied one here.

#include "features/check.h"

/*------------------------------------------------------------------------*/

// Now set some internal options, which simplify the solver code.

// We have an internal implied feature 'switch' for mode switching which is
// defined if either 'NFOCUSED' or 'NSTABLE' is defined.  Note that, both
// features can not be disabled at the same time.

#ifdef NCDCL
#define DLIS
#define NVSIDS
#define NVMTF
#define NQUEUE
#define NHEAP
#define NSWITCH
#endif

#if defined(NFOCUSED) || defined(NSTABLE)
#define NSWITCH
#endif


#ifndef NCDCL

// We want to allow the following combination of features for queues and
// heaps.  Beware that the table below has the features positively while our
// macros are negated.  It is clear that this double negation is mind
// boggling, but it has the advantage that the default solver compilation
// without any macros defined (except possibly '-DNDEBUG') enables all
// features.  This is preferred for those trying to compile the solver 'as
// is' without './configure'.  It also avoids long compiler argument lists.
// The standard 'NDEBUG' is negated too, and thus requires to think in terms
// of negation anyhow.
//
//     focused  vmtf    queue0  queue   heap1
// switch   stable  vsids   queue1  heap0   heap
// ------------------------------------------------------------------
//    1   1   1   1   1   1   0   1   0   1   1   (1)   default      
//    1   1   1   1   0   1   1   1   0   0   0   (2)                
//    1   1   1   0   1   0   0   0   1   1   1   (3)                
// ------------------------------------------------------------------
//    0   1   0   1   0   1   0   1   0   0   0   (4)                
//    0   1   0   0   1   0   0   0   1   0   1   (5)                
// ------------------------------------------------------------------
//    0   0   1   1   0   0   1   1   0   0   0   (6)                
//    0   0   1   0   1   0   0   0   0   1   1   (7)                
// ------------------------------------------------------------------
//
// We are now going to define those helper conditions
//
//   NQUEUE0   disable queue in focused mode (queue[0])
//   NQUEUE1   disable queue in stable mode (queue[1])
//   NQUEUE    disable completely all queue code
//
//   NHEAP0    disable heap in focused mode (scores[0])
//   NHEAP1    disable heap in stable mode (scores[1])
//   NQUEUE    disable completely all scores code
//
// ------------------------------------------------------------------
// *INDENT-OFF*
#ifndef NSWITCH
// ------------------------------------------------------------------
#if !defined(NVMTF) && !defined(NVSIDS)
#define NQUEUE1	// (1)  default
#define NHEAP0	// (1)  default
#elif !defined(NVMTF)
#define NHEAP0	// (2)
#define NHEAP1	// (2)
#else
#define NQUEUE0 // (3)
#define NQUEUE1 // (3)
#endif
// ------------------------------------------------------------------
#elif !defined(NFOCUSED)
// ------------------------------------------------------------------
#ifndef NVMTF
#define NQUEUE1	// (4)
#define NHEAP0	// (4)
#define NHEAP1	// (4)
#else
#define NQUEUE0 // (5)
#define NQUEUE1 // (5)
#define NHEAP1	// (5)
#endif
// ------------------------------------------------------------------
#else
// ------------------------------------------------------------------
#ifndef NVSIDS
#define NQUEUE0	// (7)
#define NQUEUE1 // (7)
#define NHEAP0	// (7)
#else
#define NQUEUE0	// (6)
#define NHEAP0 	// (6)
#define NHEAP1	// (6)
#endif
// ------------------------------------------------------------------
#endif
// *INDENT-ON*
// ------------------------------------------------------------------

#if defined(NQUEUE0) && defined(NQUEUE1)
#define NQUEUE
#endif

#if defined(NHEAP0) && defined(NHEAP1)
#define NHEAP
#endif

// If neither VMTF and VSIDS are activated, we switch to DLIS. There is currently
// no way to use it only during one phase.
#if defined(NVMTF) && defined (NVSIDS)
#define NSWITCH
#define DLIS
#endif

// We need to pick one of 'NVSIDS' or 'NVMTF' if 'NSWITCH' is on.  This is
// in essence a disjunctive implication 'NSWITCH -> (NVSIDS || NVMTF)' which
// we currently do not support with our feature generation automatically.

#ifdef NSWITCH
#if defined(NHEAP) && !defined(NVSIDS)
#define NVSIDS
#endif
#if defined(NQUEUE) && !defined(NVMTF)
#define NVMTF
#endif
#endif

#endif

// Making these choices at run-time does not simplify the complexity of
// having all of them available.  The advantage would be that checking could
// be done with C code instead with preprocessing directives.  On the other
// hand testing would be more complicated and we loose compiler support for
// generating warnings or even errors if code uses disabled features.

/*------------------------------------------------------------------------*/

// After checking consistency we force implied and positive options.

#include "features/init.h"

/*------------------------------------------------------------------------*/
// The following is used to diagnose and thus debug the final setting of
// options after everything is checked and initialized.  Using the '-d' or
// '--diagnose' configuration option we enable the following pragmas.
// Otherwise it is very hard to figure out which macros are set.  For
// instance the issue with the disjunctive implication above is difficult to
// get right without looking at exactly what macros are defined here.

#ifdef IAGNOSE

// First diagnose 'NDEBUG' even though that should be visible.

#ifdef NDEBUG
#pragma message "#define NDEBUG"
#endif

// Second diagnose automatically generated options.

#include "features/diagnose.h"

// Third diagnose internal virtual options.

#ifdef NSWITCH
#pragma message "#define NSWITCH"
#endif

#ifdef NQUEUE0
#pragma message "#define NQUEUE0"
#endif
#ifdef NQUEUE1
#pragma message "#define NQUEUE1"
#endif
#ifdef NQUEUE
#pragma message "#define NQUEUE"
#endif

#ifdef NHEAP0
#pragma message "#define NHEAP0"
#endif
#ifdef NHEAP1
#pragma message "#define NHEAP1"
#endif
#ifdef NHEAP
#pragma message "#define NHEAP"
#endif

#endif
/*------------------------------------------------------------------------*/

#endif

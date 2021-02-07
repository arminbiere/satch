#ifndef _features_h_INCLUDED
#define _features_h_INCLUDED

// The library has the following configuration parameters in form of compile
// time macros, which disable or enable certain features through arguments
// to './configure'.  As with the common 'NDEBUG' macro we use negated
// semantics, i.e., defining the macro disables the feature (since for
// instance the 'N' in 'NDEBUG' means [N]o debugging - see 'man assert').
//
//   NDEBUG    disable proof, witness and assertion checking
//
// Beside this common macro we use the following internal ones:
//
//   NBLOCK    disable blocking literals (thus slower propagation)
//   NBUMP     disable variable bumping (during conflict analysis)
//   NCOMPACT  disable compact watch data structures (slower + more space)
//   NFOCUSED  disable focused mode and always use stable mode instead
//   NLEARN    disable keeping learned clauses (DPLL with backjumping)
//   NMINIMIZE disable clause minimization during learning
//   NREDUCE   disable clause reduction completely (keep all clauses)
//   NRESTART  disable restarts completely (moving average based)
//   NSORT     disable sorting of bumped literals in focused mode
///  NSTABLE   disable stable mode and always use focused mode instead
//   NSTABLE   disable switching between stable and focused mode
//   NVARIADIC disable embedding of literals as variadic array into clause
//   NVMTF     disable VMTF and always use VSIDS instead
///  NVSIDS    disable VSIDS and always use VMTF instead
//   
// While 'NDEBUG' is used frequently through-out the code the other macros
// are only used to disable specific default features in order to run and
// profile the code without a certain feature.

/*------------------------------------------------------------------------*/

// The first set of the following checks are in essence documentation. The
// 'configure' script would on purpose leave out the implied macro and raise
// a fatal error if you try to use both. Beside documentation they also make
// sure that compiling the solver directly without relying on 'configure' to
// do sanity checking on the selected feature macros, we still get the same
// consistent option setting.  For instance, in the actual solver code below
// we might have places where the implied macro is used without checking the
// stronger implying one and then forcing the implied one here.

#ifdef NBLOCK
#ifdef NCOMPACT
#error "'NBLOCK' implies 'NCOMPACT' (latter should not be defined then)"
#else
#define NCOMPACT
#endif
#endif

#ifdef NBUMP
#ifdef NSORT
#error "'NBUMP' implies 'NSORT' (latter should not be defined then)"
#else
#define NSORT
#endif
#endif

#ifdef NLEARN
#ifdef NREDUCE
#error "'NLEARN' implies 'NREDUCE' (latter should not be defined then)"
#else
#define NREDUCE
#endif
#ifdef NMINIMIZE
#error "'NLEARN' implies 'NMINIMIZE' (latter should not be defined then)"
#else
#define NMINIMIZE
#endif
#endif

#if defined(NVMTF) && !defined(NBUMP)
#ifdef NSORT
#error "'NVMTF' implies 'NSORT' (latter should not be defined then)"
#else
#define NSORT
#endif
#endif

// Here we check those features which should never be disabled at the same
// time.  In contrast to the implied ones above, where we define the implied
// one, for these incompatible features we only raise an error.

#if defined(NVMTF) && defined(NVSIDS)
#error "'NVMTF' and 'NVSIDS' can not both be defined"
#endif

// We also have an internal implied feature 'switch' for mode switching
//
//  NSWITCH   disable mode switching
//
// which is defined if either 'NFOCUSED' or 'NSTABLE' is defined and both
// features can not be disabled at the same time.

#if defined(NFOCUSED) && defined(NSTABLE)
#error "'NFOCUSED' and 'NSTABLE' can not both be defined"
#elif defined(NFOCUSED) || defined(NSTABLE)
#define NSWITCH
#endif

// We want to allow the following combination of features.   Beware that the
// table below has the features positively while our macros are negated.
// It is clear that this double negation is mind boggling, but it has the
// advantage that the default solver compilation without any macros defined
// (except possibly '-DNDEBUG') enables all features.  This is preferred for
// those trying to compile the solver 'as is' without './configure'.  It
// also avoids long compiler argument lists.  The standard 'NDEBUG' is
// negated too, and thus requires to think in terms of negation anyhow.
//
// switch  focused stable vmtf  vsids
//
//    1       1      1      1     1   default
//    1       1      1      0     1
//    1       1      1      1     0
//           
//    0       1      0      1     0   (1)
//    0       1      0      0     1
//
//    0       0      1      1     0
//    0       0      1      0     1   (2)
//           
// The first three switch between focused and stable mode and either allow
// to have only 'vsids', 'vmtf' or both.  Note that, in the default
// configuration 'vmtf' is used in 'focused' mode and 'vsids' in 'stable'
// mode.  This can not be changed as it is the main distinguishing feature
// between those modes (except for how restarts are scheduled of course).
//
// For the other two pairs of configurations where the solver stays either
// completely in 'focused' mode or completely in 'stable' mode, i.e.,
// switching is disabled, exactly one of 'vmtf' or 'vsids' has to be
// enabled.  By default we use 'vtmf' (1) for 'focused' mode and
// 'vsids' (2) for 'stable' mode in case non has been selected yet.

#if defined(NFOCUSED) && defined(NVMTF)
#error "'NFOCUSED' by default implies 'NVMTF' (latter should not be defined then)"
#endif

#if defined(NSTABLE) && defined(NVSIDS)
#error "'NSTABLE' by default implies 'NSIDS' (latter should not be defined then)"
#endif

#if defined(NSTABLE) && !defined(NVMTF) && !defined(NVSIDS)
#define NVSIDS			/* (1) */
#endif

#if defined(NFOCUSED) && !defined(NVMTF) && !defined(NVSIDS)
#define NVMTF			/* (2) */
#ifndef NSORT
#define NSORT
#endif
#endif

// Making these choices at run-time does not simplify the complexity of
// having all of them available.  The advantage would be that checking could
// be done with C code instead with preprocessing directives.  On the other
// hand testing would be complicated.

/*------------------------------------------------------------------------*/

#endif

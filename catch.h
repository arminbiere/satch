#ifndef _catch_h_INCLUDED
#define _catch_h_INCLUDED

/*------------------------------------------------------------------------*/

// This is the API of a small library for  online proof checking in style of
// the DRUP / DRAT format used in the SAT competition even though only DRUP
// is supported at this point.  It is meant for testing and debugging only.

/*------------------------------------------------------------------------*/

struct checker;

/*------------------------------------------------------------------------*/

struct checker *checker_init (void);
void checker_release (struct checker *);

// Verbose messages are printed when they are enabled after garbage
// collection and when the checker is releases (and prints some statistics).
//
void checker_verbose (struct checker *);

// Logging messages are enabled by calling this function. This is mainly
// useful to debug discrepancies between checking proofs online and offline.
// We keep a definition in the code even if 'LOGGING' is undefined so you
// can always reference this function in your code.

void checker_logging (struct checker *);

// The checker can be enabled to check that all added clauses are also
// removed before the checker is released.  Clauses satisfied by unit
// implication are ignored in this test though.  This test breaks down if
// the user is sloppy in removing clauses and thus on the other hand can be
// used to track down 'lost' clauses.  Lost or leaked clauses are those not
// seen by the solver anymore but the checker still has a copy of them.

void checker_enable_leak_checking (struct checker *);

/*------------------------------------------------------------------------*/

// In contrast to the IPASIR interface, the checker only expects (non-zero)
// literals as argument to its 'add' function.  Then you need to call one of
// the functions below afterwards to either add an original clause, check
// and add an implied clause or remove a clause (the latter corresponds to
// 'd' lines the DRUP/DRAT file based proof checking format).

void checker_add_literal (struct checker *, int lit);

void checker_delete_clause (struct checker *);
void checker_add_original_clause (struct checker *);
void checker_add_learned_clause (struct checker *);

/*------------------------------------------------------------------------*/

#endif

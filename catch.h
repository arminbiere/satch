#ifndef _catch_h_INCLUDED
#define _catch_h_INCLUDED

/*------------------------------------------------------------------------*/

struct checker;

/*------------------------------------------------------------------------*/

struct checker *checker_init (void);
void checker_release (struct checker *);

// Verbose messages are printed when they are enabled after garbage
// collection and when the checker is release (and prints some statistics).
//
void checker_verbose (struct checker *);

// The checker can be enabled to check that all added closes are also
// removed before the checker is released.  Clauses satisfied by unit
// implication are ignored in this test though.  This test breaks down if
// the user is sloppy in removing clauses and thus on the other hand can be
// used to track down 'lost' clauses.  Lost or leaked clauses are not
// seen by the solver anymore but the checker still has a copy of them.
//
void checker_enable_leak_checking (struct checker *);

/*------------------------------------------------------------------------*/

// In contrast to the IPASIR interface, the checker only expects (non-zero)
// literals as argument to its 'add' function.  Then you need to call one of
// the functions afterwards to either add an original clause, check and add
// an implied clause or remove a clause (the latter corresponds to 'd' lines
// the DRUP/DRAT file based proof checking format).

void checker_add (struct checker *, int lit);

void checker_original (struct checker *);
void checker_remove (struct checker *);
void checker_learned (struct checker *);

/*------------------------------------------------------------------------*/

#endif

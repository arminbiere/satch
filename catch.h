#ifndef _catch_h_INCLUDED
#define _catch_h_INCLUDED

struct checker;

struct checker *checker_init (void);
void checker_release (struct checker *);

void checker_add (struct checker *, int lit);

void checker_original (struct checker *);
void checker_remove (struct checker *);
void checker_learned (struct checker *);

#endif

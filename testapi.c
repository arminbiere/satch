#include "satch.h"
#include <stdio.h>
#include <stdlib.h>
#undef NDEBUG
#include <assert.h>
int
main (void)
{
  {
    struct satch *solver = satch_init ();
    int res = satch_solve (solver);
    assert (res == 10);
    satch_release (solver);
  }
  {
    struct satch *solver = satch_init ();
    satch_add (solver, 0);
    int res = satch_solve (solver);
    assert (res == 20);
    satch_release (solver);
  }
  {
    struct satch *solver = satch_init ();
    satch_add (solver, 1);
    satch_add (solver, 0);
    int res = satch_solve (solver);
    assert (res == 10);
    int value = satch_val (solver, 1);
    assert (value == 1);
    satch_release (solver);
  }
  {
    struct satch *solver = satch_init ();
    satch_add (solver, 1), satch_add (solver, 2), satch_add (solver, 0);
    satch_add (solver, 1), satch_add (solver, -2), satch_add (solver, 0);
    satch_add (solver, -1), satch_add (solver, 2), satch_add (solver, 0);
    int res = satch_solve (solver);
    assert (res == 10);
    int tmp = satch_val (solver, 1);
    assert (tmp == 1);
    tmp = satch_val (solver, 2);
    assert (tmp == 2);
    satch_release (solver);
  }
  {
    struct satch *solver = satch_init ();
    satch_add (solver, -1), satch_add (solver, -2), satch_add (solver, 0);
    satch_add (solver, -1), satch_add (solver, 2), satch_add (solver, 0);
    satch_add (solver, 1), satch_add (solver, -2), satch_add (solver, 0);
    satch_add (solver, 1), satch_add (solver, 2), satch_add (solver, 0);
    int res = satch_solve (solver);
    assert (res == 20);
    satch_release (solver);
  }
  return 0;
}

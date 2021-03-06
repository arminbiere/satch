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
    int res = satch_solve (solver, -1);
    assert (res == 10);
    satch_release (solver);
  }
  {
    struct satch *solver = satch_init ();
    satch_add (solver, 0);
    int res = satch_solve (solver, -1);
    assert (res == 20);
    satch_release (solver);
  }
  {
    struct satch *solver = satch_init ();
    satch_add (solver, 1);
    satch_add (solver, 0);
    int res = satch_solve (solver, -1);
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
    int res = satch_solve (solver, -1);
    assert (res == 10);
    for (int i = 0; i < 3; i++)
      {
	int tmp = satch_val (solver, 1);
	assert (tmp == 1);
	tmp = satch_val (solver, 2);
	assert (tmp == 2);
      }
    satch_release (solver);
  }
  {
    struct satch *solver = satch_init ();
    satch_add (solver, -1), satch_add (solver, -2), satch_add (solver, 0);
    satch_add (solver, -1), satch_add (solver, 2), satch_add (solver, 0);
    satch_add (solver, 1), satch_add (solver, -2), satch_add (solver, 0);
    satch_add (solver, 1), satch_add (solver, 2), satch_add (solver, 0);
    int res = satch_solve (solver, -1);
    assert (res == 20);
    satch_release (solver);
  }
  {
    struct satch *solver = satch_init ();
    for (int r = -1; r <= 1; r += 2)
      for (int s = -1; s <= 1; s += 2)
	for (int t = -1; t <= 1; t += 2)
	  satch_add (solver, r * 1), satch_add (solver, s * 2),
	    satch_add (solver, t * 3), satch_add (solver, 0);
    satch_set_verbose_level (solver, 1);
    int limit = 0, res;
    while (!(res = satch_solve (solver, limit++)))
      ;
    assert (res == 20);
    satch_release (solver);
  }
  return 0;
}

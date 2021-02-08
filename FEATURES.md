The library allows to disable certain features at compile time.  This
simplifies the process of figuring out which code is dependent on which
feature by for instance relying on compiler warnings to help trim the
redundant code down.  We then also want the test suite to pass on all these
different configuration options of course.

In order to add a 'NFOO' compile-time option which can be used to disable
the 'foo' feature with the C preprocessor the following steps are necessary:

  1. Add a corresponding '--no-foo' usage in './configure'.
  2. Add the same description for 'NFOO' to 'features.h'.
  3. Then add 'foo=yes' initialization in './configure'.
  4. Parse the '--no-foo' command line option in './configure'.
  5. Add '[ $foo = no ] && -DNFOO' to './configure' too.
  6. Add three corresponding lines to 'mkconfig.sh'.

Then you might start using that option '#ifndef NFOO' in the code.  After it
basically compiles with './configure --no-foo && make' you want to harden
this change as follows:

  7. Add '--no-foo' to 'gencombi.c' to the list of features..

Now assume there is another feature 'bar' which if disabled makes 'foo'
redundant.  That is 'NBAR' implies 'NFOO'.

  8. Add a check with '#error' and add '#define NFOO' to 'features.h'.
  9. Add a filter rule 'noobarnofoo' to 'combi.sh'.
  10. Add i '--no-bar', '--no-foo' incompatible pair to './gencombi.c'.

These three steps can be ignored if there is no such feature 'bar' and of
course should be repeated if there are multiple such features.

Use a similar approach for features which can not be disabled at the same
time (such as disabling both 'focused' and 'stable' mode), but do not add a
'#define' in '8.'.  Note that, the logic between those two types
-- implication versus mutual exclusion -- of disabling features is
different (in the first case disabling the first feature forces the second
also to be disabled, while in the second case both can not be disabled at
the same time).  However, such pairs are treated in the same way in
'./configure', 'combi.sh' and 'gencombi.c' as an invalid disabling feature
combination (in the first case disabling the second feature is redundant and
in the second case disabling both is not allowed).

Now run basic tests

  11. ./configure --no-foo && make test

and fix issues before turning to

  12. ./configure && make gencombi && make pairwise

and maybe even to the much more exhaustive

  ./gencombi -a 10 | ./checkconfig

which should check disabling all conceivable feature combinations.

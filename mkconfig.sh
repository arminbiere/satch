#!/bin/sh
die () {
  echo "mkconfig.sh: error: $*" 1>&2
  exit 1
}
[ -f makefile ] || die "could not find 'makefile': run './configure.sh' first"
VERSION="`cat VERSION`"
COMPILE="`sed -e '/^COMPILE/!d' -e 's,^COMPILE=,,' makefile`"
IDENTIFIER="`git show 2>/dev/null|awk '{print $2; exit}'`"
cat <<EOF
#include "satch.h"

const char *
satch_version (void)
{
  return "$VERSION"
#ifdef NBLOCK
  "-block"
#endif
#ifdef NLEARN
  "-learn"
#endif
#ifdef NMINIMIZE
  "-minimize"
#endif
#ifdef NMODE
  "-mode"
#endif
#ifdef NREDUCE
  "-reduce"
#endif
#ifdef NRESTART
  "-restart"
#endif
#ifdef NSORT
  "-sort"
#endif
  ;
}

const char *
satch_compile (void)
{
  return "$COMPILE";
}

const char *
satch_identifier (void)
{
EOF
if [ x"$IDENTIFIER" = x ]
then
cat <<EOF
  return 0;
EOF
else
cat <<EOF
  return "$IDENTIFIER";
EOF
fi
echo "}"

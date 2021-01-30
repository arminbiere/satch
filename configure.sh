#!/bin/sh

usage () {
cat <<EOF
usage: configure.sh [ <option> ... ]

where '<option>' is one of the following:

-h | --help       print this command line option summary
-g | --debug      include symbol table, logging and internal checking code
-c | --check      include internal checking code (forced by '-g')
-s | --symbols    include symbol table (forced by '-g')
                 
--no-block        disable blocking literals (thus slower propagation)
--no-learn        disable clause learning (do not add learned clauses)
--no-mode         disable switching between focused and stable mode
--no-reduce       disable clause reduction (keep learned clauses forever)
--no-restart      disable restarting (otherwise moving average based)
--no-sort         disable sorting of bumped literals

-f...             passed to compiler directly (like '-fsanitize=address')
EOF
}

die () {
  echo "configure.sh: error: $*" 1>&2
  exit 1
}

debug=no
check=no
symbols=no

block=yes
learn=yes
minimize=yes
mode=yes
reduce=yes
restart=yes
sort=yes

options=""

while [ $# -gt 0 ]
do
  case $1 in
    --default) ;;
    -h|--help) usage; exit 0;;
    -g|--debug) debug=yes;;
    -c|--check) check=yes;;
    -s|--symbols) symbols=yes;;
    --no-block) block=no;;
    --no-minimize) minimize=no;;
    --no-mode) mode=no;;
    --no-learn) learn=no;;
    --no-reduce) reduce=no;;
    --no-restart) restart=no;;
    --no-sort) sort=no;;
    -f*) options="$options $1";;
    *) die "invalid option '$1' (try '-h')";;
  esac
  shift
done

[ $restart = no -a $mode = no ] && \
die "'--no-restart' implies '--no-mode'"

[ $learn = no -a $reduce = no ] && \
die "'--no-learn' implies '--no-reduce'"

[ $learn = no -a $minimize = no ] && \
  die "'--no-learn' implies '--no-minimize'"

CC=gcc

CFLAGS="-Wall"
if [ $debug = yes ]
then
  check=yes
  CFLAGS="$CFLAGS -ggdb3"
else
  CFLAGS="$CFLAGS -O3"
fi

# Here come the options which disable certain code features mostly for
# didactic purposes in order to show the effect of certain ideas.

[ $symbols = yes ] && CFLAGS="$CFLAGS -ggdb3"
CFLAGS="$CFLAGS$options"
[ $block = no ] && CFLAGS="$CFLAGS -DNBLOCK"
[ $check = no ] && CFLAGS="$CFLAGS -DNDEBUG"
[ $learn = no ] && CFLAGS="$CFLAGS -DNLEARN"
[ $minimize = no ] && CFLAGS="$CFLAGS -DNMINIMIZE"
[ $mode = no ] && CFLAGS="$CFLAGS -DNMODE"
[ $reduce = no ] && CFLAGS="$CFLAGS -DNREDUCE"
[ $restart = no ] && CFLAGS="$CFLAGS -DNRESTART"
[ $sort = no ] && CFLAGS="$CFLAGS -DNSORT"

COMPILE="$CC $CFLAGS"
echo "$COMPILE"

rm -f makefile
sed -e "s#@COMPILE@#$COMPILE#" makefile.in > makefile

# Remove the proof checker dependencies in the generated 'makefile' if
# checking is disabled.
#
if [ $check = no ]
then
  sed -i -e '/^catch.o:/d' -e 's/ catch.o//' makefile
fi

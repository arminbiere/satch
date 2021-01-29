#!/bin/sh

die () {
  echo "tatch.sh: error: $*" 1>&2
  exit 1
}

[ -f satch ] || \
   die "could not find 'satch': run './configure.sh && make' first"

[ -f makefile ] || die "could not find 'makefile'"

run () {
  expected=$1
  shift
  command="$*"
  echo -n "$command # expected '$expected'"
  $command 1>/dev/null 2>/dev/null
  status=$?
  if [ $status = $expected ]
  then
    echo " OK"
  else
    echo " but status '$status' FAILED"
    exit 1
  fi
}

msg () {
  echo "tatch.sh: $*"
}

if [ x"`grep DNLEARN makefile`" = x ]
then
  learning=no
else
  learning=yes
fi

msg "first running basic usage tests"

run 0 ./satch -h
run 0 ./satch --version

msg "now solving CNF files"

run 10 ./satch cnfs/true.cnf
run 20 ./satch cnfs/false.cnf

run 10 ./satch cnfs/unit1.cnf
run 10 ./satch cnfs/unit2.cnf
run 10 ./satch cnfs/unit3.cnf
run 10 ./satch cnfs/unit4.cnf
run 20 ./satch cnfs/unit5.cnf
run 20 ./satch cnfs/unit6.cnf

run 10 ./satch cnfs/unit7.cnf
run 20 ./satch cnfs/unit8.cnf
run 20 ./satch cnfs/unit9.cnf

run 20 ./satch cnfs/full2.cnf
run 20 ./satch cnfs/full3.cnf
run 20 ./satch cnfs/full4.cnf

run 20 ./satch cnfs/ph2.cnf
run 20 ./satch cnfs/ph3.cnf
run 20 ./satch cnfs/ph4.cnf
run 20 ./satch cnfs/ph5.cnf
run 20 ./satch cnfs/ph6.cnf

run 10 ./satch cnfs/sqrt2809.cnf
run 10 ./satch cnfs/sqrt3481.cnf
run 10 ./satch cnfs/sqrt3721.cnf
run 10 ./satch cnfs/sqrt4489.cnf
run 10 ./satch cnfs/sqrt5041.cnf
run 10 ./satch cnfs/sqrt5329.cnf
run 10 ./satch cnfs/sqrt6241.cnf
run 10 ./satch cnfs/sqrt6889.cnf
run 10 ./satch cnfs/sqrt7921.cnf
run 10 ./satch cnfs/sqrt9409.cnf
run 10 ./satch cnfs/sqrt10201.cnf
run 10 ./satch cnfs/sqrt10609.cnf
run 10 ./satch cnfs/sqrt11449.cnf
run 10 ./satch cnfs/sqrt11881.cnf
run 10 ./satch cnfs/sqrt12769.cnf
run 10 ./satch cnfs/sqrt16129.cnf
run 10 ./satch cnfs/sqrt63001.cnf
run 10 ./satch cnfs/sqrt259081.cnf
run 10 ./satch cnfs/sqrt1042441.cnf

run 10 ./satch cnfs/prime4.cnf
run 10 ./satch cnfs/prime9.cnf
run 10 ./satch cnfs/prime25.cnf
run 10 ./satch cnfs/prime49.cnf
run 10 ./satch cnfs/prime121.cnf
run 10 ./satch cnfs/prime169.cnf
run 10 ./satch cnfs/prime289.cnf
run 10 ./satch cnfs/prime361.cnf
run 10 ./satch cnfs/prime529.cnf
run 10 ./satch cnfs/prime841.cnf
run 10 ./satch cnfs/prime961.cnf
run 10 ./satch cnfs/prime1369.cnf
run 10 ./satch cnfs/prime1681.cnf
run 10 ./satch cnfs/prime1849.cnf
run 10 ./satch cnfs/prime2209.cnf

[ $learning = no ] && \
run 20 ./satch cnfs/prime65537.cnf

run 20 ./satch cnfs/add4.cnf
run 20 ./satch cnfs/add8.cnf

if [ $learning = no ]
then
run 20 ./satch cnfs/add16.cnf
run 20 ./satch cnfs/add32.cnf
run 20 ./satch cnfs/add64.cnf
run 20 ./satch cnfs/add128.cnf
fi

msg "compiling 'testapi.c' and linking against library"

compiler="`grep ^COMPILE makefile|sed 's,^COMPILE=,,'`"
[ x"$compiler" = x ] && die "could not determine compiler"

compile="$compiler -c testapi.c"
echo $compile
$compile || exit 1

compile="$compiler -o testapi testapi.o -L. -lsatch -lm"
echo $compile
$compile || exit 1

run 0 ./testapi

msg "compiling 'testapi.c' directly without linking against library"

compiler="`echo "$compiler"|sed 's, -DNDEBUG,,'`"
compile="$compiler -DNDEBUG -o testapi testapi.c satch.c -lm"
echo $compile
$compile || exit 1
run 0 ./testapi


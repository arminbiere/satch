#!/bin/sh

RED='\033[0;31m'
GREEN='\033[0;32m'
NONE='\033[0m'

die () {
  echo "tatch.sh: error: $*" 1>&2
  exit 1
}

msg () {
  echo "tatch.sh: $*"
}

[ -f satch ] || \
   die "could not find 'satch': run './configure && make' first"

[ -f makefile ] || die "could not find 'makefile'"

msg "SATCH Version `./satch --version` `./satch --id`"

drattrim="`type drat-trim 2>/dev/null |awk '{print $NF}'`"

if [ "$drattrim" ]
then
  rm -f cnfs/false.proof
  touch cnfs/false.proof
  if [ "`$drattrim cnfs/false.cnf cnfs/false.cnf 2>/dev/null|grep VERIFIED`" ]
  then
    msg "${GREEN}checking proofs with '$drattrim'$NONE"
    proofsmod3=0
  else
    msg "${RED}checking 'drat-trim cnfs/false.cnf cnfs/false.cnf' failed$NONE"
    drattrim=""
  fi
else
  msg "could not find 'drat-trim' ('type drat-trim' failed)"
fi

[ "$drattrim" ] || \
  msg "(make sure 'drat-trim ' is found for more thorough testing)"

run () {
  expected=$1
  shift
  command="$*"
  proof=""
  if [ "$drattrim" -a $expected = 20 ]
  then
    cnf="$2"
    case "$cnf" in
      *.cnf);;
      *) die "expected CNF as second argument in '$expected $command'";;
    esac
    proofpath=cnfs/`basename $cnf .cnf`.proof
    rm -f $proofpath 2>/dev/null || die "failed to 'rm -f $proofpath'"
    options=""
    case $proofsmod3 in
      0) proofsmod3=1; ;;
      1) proofsmod3=2; proof="$proofpath";;
      2) proofsmod3=0; options="-a"; proof="$proofpath";;
    esac
    [ "$options" ] && command="$command $options"
    [ "$proof" ] && command="$command $proof"
  fi
  printf "$command # expected '$expected'"
  $command 1>/dev/null 2>/dev/null
  status=$?
  if [ $status = $expected ]
  then
    if [ "$proof" -a $expected = 20 ]
    then
      if [ -f "$proof" ]
      then
        output="`$drattrim $cnf $proof 2>/dev/null|grep 's VERIFIED$'`"
	if [ "$output" ]
	then 
	  echo "$GREEN proof OK$NONE"
	else
	  echo "$RED proof FAILED$NONE"
	  echo "$drattrim $cnf $proof"
	  exit 1
	fi
      else
	echo " OK but '$proof' missing"
	exit 1
      fi
    else
      echo "$GREEN OK$NONE"
    fi
  else
    echo "$RED status '$status' FAILED$NONE"
    exit 1
  fi
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


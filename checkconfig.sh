#!/bin/sh

usage () {
cat <<EOF
usage: checkconfig.sh [ <option> ]

-h | --help      print this command line option summary
-i | --invalid   assume configuration is invalid and should fail

Reads sets of configuration options from '<stdin>', one configuration per
line, configures the solver with that set of options, compiles it and then
runs make test
EOF
  exit 0
}

die () {
  echo "checkconfig.sh: error: $*" 1>&2
  exit 1
}

failed () {
  echo
  die "last command failed"
}

succeeded () {
  echo
  die "last command succeeded unexpectedly"
}

invalid=no

while [ $# -gt 0 ]
do
  case $1 in
    -i|--invalid) invalid=yes;;
    -h|--help) usage;;
    *) die "invalid option '$1' (try '-h')";;
  esac
  shift
done

if [ $invalid = no ]
then
  while IFS= read -r command
  do
    echo -n "$command"
    $command 1>/dev/null 2>/dev/null || failed
    echo -n " && make"
    make 1>/dev/null 2>/dev/null || failed
    echo -n " test"
    make test 1>/dev/null 2>/dev/null || failed
    echo -n " && make clean"
    make clean 1>/dev/null 2>/dev/null || failed
    echo
  done
else
  while IFS= read -r command
  do
    echo -n "$command"
    $command 1>/dev/null 2>/dev/null && succeeded
    echo " # failed as expected"
  done
fi

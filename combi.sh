#!/bin/sh

# 4-fold combinatorial testing of all configurations

basic="default pedantic debug check symbols"
features="sort block flex learn reduce restart stable"

echo "basic: $basic"
echo "features: $features"

options="$basic `echo $features|sed 's,\<,no,g'`"

failed () {
  echo
  echo "combi.sh: error: last command failed"
  exit 1
}

succeeded () {
  echo
  echo "combi.sh: error: last command succeeded unexpectedly"
  exit 1
}

filter () {
  case $1$2 in
    debugcheck) filtered=yes;;
    debugsymbols) filtered=yes;;
    nolearnnoreduce) filtered=yes;;
    norestartnostable) filtered=yes;;
  esac
}

run () {
  filtered=no
  case $# in
    2)
      filter $1 $2
      ;;
    3)
      filter $1 $2
      filter $1 $3
      filter $2 $3
      ;;
    4)
      filter $1 $2
      filter $1 $3
      filter $1 $4
      filter $2 $3
      filter $2 $4
      filter $3 $4
      ;;
  esac
  args="`echo $*|sed 's,default,,;s,\<,--,g;s,--no,--no-,g'`"
  args="`echo $args|sed 's,-check,c,;s,-debug,g,'`"
  args="`echo $args|sed 's,-symbols,s,;s,-pedantic,p,g'`"
  command="./configure $args"
  echo -n $command
  if [ $filtered = no ]
  then
    $command 1>/dev/null 2>/dev/null || failed
    echo -n " && make"
    make 1>/dev/null 2>/dev/null || failed
    echo -n " test"
    make test 1>/dev/null 2>/dev/null || failed
    echo -n " && make clean"
    make clean 1>/dev/null 2>/dev/null || failed
  else
    $command 1>/dev/null 2>/dev/null && succeeded
    echo -n " # failed as expected"
  fi
  echo
}

for first in $options
do
  run $first
done

for first in $options
do
  for second in `echo $options|fmt -0|sed "1,/$first/d"`
  do
    run $first $second
  done
done

for first in $options
do
  for second in `echo $options|fmt -0|sed "1,/$first/d"`
  do
    for third in `echo $options|fmt -0|sed "1,/$second/d"`
    do
      run $first $second $third
    done
  done
done

for first in $options
do
  for second in `echo $options|fmt -0|sed "1,/$first/d"`
  do
    for third in `echo $options|fmt -0|sed "1,/$second/d"`
    do
      for fourth in `echo $options|fmt -0|sed "1,/$third/d"`
      do
        run $first $second $third $fourth
      done
    done
  done
done

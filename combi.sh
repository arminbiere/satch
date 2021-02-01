#!/bin/sh

# 4-fold combinatorial testing of all configurations

basic="default check debug symbols"
features="sort block flex learn reduce restart stable"

echo "basic: $basic"
echo "features: $features"

options="$basic `echo $features|sed 's,\<,no,g'`"

failed () {
  echo
  echo "combi.sh: error: last command failed"
  exit 1
}

run () {
  args="`echo $*|sed 's,default,,;s,\<,--,g;s,--no,--no-,g'`"
  args="`echo $args|sed 's,-check,c,;s,-debug,g,;s,-symbols,s,'`"
  command="./configure $args"
  echo -n $command
  $command 1>/dev/null 2>/dev/null || failed
  echo -n " && make"
  make 1>/dev/null 2>/dev/null || failed
  echo -n " test"
  make test 1>/dev/null 2>/dev/null || failed
  echo -n " && make clean"
  make clean 1>/dev/null 2>/dev/null || failed
  echo
}

filter () {
  case $1$2 in
    nolearnnoreduce) return 0;;
    norestartnostable) return 0;;
    *) return 1;;
  esac
}

for first in $options
do
  run $first
done
for first in $options
do
  for second in `echo $options|fmt -0|sed "1,/$first/d"`
  do
    filter $first $second && continue
    run $first $second
  done
done
for first in $options
do
  for second in `echo $options|fmt -0|sed "1,/$first/d"`
  do
    filter $first $second && continue
    for third in `echo $options|fmt -0|sed "1,/$second/d"`
    do
      filter $second $third && continue
      run $first $second $third
    done
  done
done
for first in $options
do
  for second in `echo $options|fmt -0|sed "1,/$first/d"`
  do
    filter $first $second && continue
    for third in `echo $options|fmt -0|sed "1,/$second/d"`
    do
      filter $second $third && continue
      for fourth in `echo $options|fmt -0|sed "1,/$third/d"`
      do
        filter $third $fourth && continue
        run $first $second $third $fourth
      done
    done
  done
done

#!/bin/sh
sed -e 's/,/ /;s,-,_,g;s,\<,",g;s,\>,",g;s,_,-,g' implied.csv | \
awk 'BEGIN{print "digraph implies {"}{print $1 " -> " $2 ";"}END{print "}"}' | \
dot -Tpdf > implied.pdf

#!/bin/bash

for ((t=1; t<17; t = t*2))
do
    for ((i=0; i<10; i++))
    do
        ./sort_array -t $t dataSet2/datSeq16M.bin | tail -1 >> results/results_thread$t.txt
    done
done
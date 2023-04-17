#!/bin/bash

for ((t=1; t<9; t = t*2))
do
    for ((i=0; i<10; i++))
    do
        ./text_processing -t $t examples/text0.txt examples/text1.txt examples/text2.txt examples/text3.txt examples/text4.txt | tail -1 >> results_thread$t.txt
    done
done
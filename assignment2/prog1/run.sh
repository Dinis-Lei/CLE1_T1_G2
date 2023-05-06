#!/bin/bash
mpicc -Wall -O3 -o text_processing text_processing.c utf8_parser.c
rm results/*
echo start
for ((t=2; t<17; t = t*2))
do
    for ((i=0; i<10; i++))
    do
        mpiexec -n $t ./text_processing examples/text0.txt examples/text1.txt examples/text2.txt examples/text3.txt examples/text4.txt | tail -1 >> results/results$t.txt
    done
    sleep 1
done
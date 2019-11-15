#!/bin/bash
val=2000

bins=(./collatz-ivec-hw7 ./collatz-ivec-par ./collatz-ivec-sys ./collatz-list-hw7 ./collatz-list-par ./collatz-list-sys)
rm -f data.dat
for ii in {0..5}
do
	{ /usr/bin/time -f "%e" ${bins[ii]} $val ; } 2>> data.dat
done
gnuplot -e "set title 'Frick'; set terminal png; set output 'data.png'; plot 'data.dat'"

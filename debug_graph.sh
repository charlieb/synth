#!/bin/bash

awk '/OCC/{ print $4, $9; print $4, $11; print $4, $13; print $4, $15; }' out | gnuplot -persist -e "plot '-'"


#!/bin/bash

path_to_script=${1:-/home/zli89/clion_fb/scripts}

for i in $(eval echo {1..${1:-4}}); do

sbatch ${path_to_script}/run-all-configuration-prg.sbatch

sleep 1

done # end of an experimental round
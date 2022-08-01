#!/bin/bash

path_to_exe=${1:-/home/zli89/clion_fb/cmake-build-debug/benchmark}
path_to_script=${1:-/home/zli89/clion_fb/scripts}

for i in $(eval echo {1..${1:-4}}); do

sbatch ${path_to_script}/run-all-configuration-symmetric.sbatch

sleep 1

done # end of an experimental round

#[zli89@login01 7-27-results]$ squeue -u zli89 --start
#             JOBID PARTITION     NAME     USER ST          START_TIME  NODES SCHEDNODES           NODELIST(REASON)
#          14820267   compute    basic    zli89 PD 2022-07-31T23:20:01      2 exp-1-30,exp-2-01    (Priority)
#          14820268   compute    basic    zli89 PD 2022-07-31T23:21:40      2 exp-2-[02-03]        (Priority)
#          14820269   compute    basic    zli89 PD 2022-07-31T23:25:00      2 exp-1-30,exp-2-01    (Priority)
#          14820270   compute    basic    zli89 PD 2022-07-31T23:25:35      2 exp-1-[28-29]        (Priority)


#       14820268   compute    basic    zli89 PD 2022-07-31T22:54:51      2 exp-2-03,exp-12-12   (Priority)
#          14820269   compute    basic    zli89 PD 2022-07-31T22:59:00      2 exp-2-03,exp-12-12   (Priority)
#          14820270   compute    basic    zli89 PD 2022-07-31T23:04:00      2 exp-2-03,exp-12-12   (Priority)
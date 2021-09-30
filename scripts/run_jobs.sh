#!/bin/bash
export OMP_PLACES=cores
path_to_exe=${1:-/home/zli89/clion_fb/cmake-build-debug/benchmark}
for i in $(eval echo {1..${1:-4}}); do
  echo "srun -n 2 pingpong_prg_test(1 prg)" >> slurm_output_$i.txt
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 7 1 8 8
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 8 1 8 8
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 9 1 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 10 1 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 11 1 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 12 1 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 13 1 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 14 1 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 15 1 8 8
#
#  echo "srun -n 2 pingpong_prg_test(2 prg)" >> slurm_output_$i.txt
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 10 2 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 11 2 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 12 2 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 13 2 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 14 2 8 8

  echo "srun -n 2 pingpong_sym" >> slurm_output_$i.txt
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 7 8 8
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 8 8 8
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 9 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 10 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 11 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 12 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 13 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 14 8 8
#  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 15 8 8


done
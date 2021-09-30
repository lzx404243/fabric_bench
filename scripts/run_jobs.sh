#!/bin/bash
export OMP_PLACES=cores
path_to_exe=${1:-/home/zli89/clion_fb/cmake-build-debug/benchmark}
for i in $(eval echo {1..${1:-7}}); do
  echo "srun -n 2 pingpong_prg_test(1 worker, 1 prg)" >> slurm_output_$i.txt
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 1 1 8192 8192

  echo "srun -n 2 pingpong_prg_test(2 worker, 1 prg)" >> slurm_output_$i.txt
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 2 1 8192 8192

  echo "srun -n 2 pingpong_prg_test(4 worker, 1 prg)" >> slurm_output_$i.txt
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 4 1 8192 8192

  echo "srun -n 2 pingpong_prg_test(8 worker, 1 prg)" >> slurm_output_$i.txt
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 8 1 8192 8192

  echo "srun -n 2 pingpong_prg_test(16 worker, 1 prg)" >> slurm_output_$i.txt
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 16 1 8192 8192

  echo "srun -n 2 pingpong_prg_test(32 worker, 1 prg)" >> slurm_output_$i.txt
  srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 32 1 8192 8192
done
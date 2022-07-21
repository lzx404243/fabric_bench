#!/bin/bash
path_to_exe=${1:-/home/zli89/clion_fb/cmake-build-debug/benchmark}

# create a seperate directory for results for different compute time
mkdir -p pingpong_data_ibv
cd pingpong_data_ibv

for i in $(eval echo {1..${1:-4}}); do
export OMP_PLACES=cores

echo "srun -n 2 pingpong_sym(ibv)" >> slurm_output_$i.txt
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 1 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 2 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 4 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 8 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 16 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 32 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8

sleep 1
done # end of an experimental round


#!/bin/bash
#SBATCH --partition=compute
#SBATCH --account=uic193
#SBATCH --time=01:30
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=64
#SBATCH --exclusive
#SBATCH --job-name=basic
##SBATCH --output=slurm_output.%x-o%j
##SBATCH --error=slurm_error.%x-o%j

path_to_exe=${1:-/home/zli89/clion_fb/cmake-build-debug/benchmark}
export OMP_PLACES=cores
echo "srun -n 2 pingpong_sym(ibv-64-workers)" >> slurm_output_$i.txt
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 8


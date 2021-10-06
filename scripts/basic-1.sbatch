#!/bin/bash
#SBATCH --partition=compute
#SBATCH --time=15:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=16
#SBATCH --account=uic193
##SBATCH --exclusive
#SBATCH --job-name=basic
#SBATCH --output=slurm_output.%x-o%j
#SBATCH --error=slurm_error_8_sending.%x-o%j

path_to_exe=${1:-/home/zli89/clion_fb/cmake-build-debug/benchmark}

# module load intel mvapich2
# echo "srun -n 2 osu_bw"
# srun -n 2 --mpi=pmi2 ~/osu_bw -m 8:262144 -i 40000

# export OMP_DISPLAY_ENV=verbose
# export OMP_DISPLAY_AFFINITY=TRUE

export OMP_PLACES=cores

echo "srun -n 2 pingpong_prg_test(1 worker, 1 prg)"
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${path_to_exe}/pingpong_sym_optimized 1 8 8

echo "srun -n 2 pingpong_prg_test(2 worker, 1 prg)"
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 2 1 8 8

echo "srun -n 2 pingpong_prg_test(4 worker, 1 prg)"
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 4 1 8 8

echo "srun -n 2 pingpong_prg_test(8 worker, 1 prg)"
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 8 1 8 8

echo "srun -n 2 pingpong_prg_test(16 worker, 1 prg)"
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${path_to_exe}/pingpong_prg_test 16 1 8
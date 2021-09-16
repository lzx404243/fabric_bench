#!/bin/bash
#SBATCH --partition=secondary
#SBATCH --time=15:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=8
##SBATCH --exclusive
#SBATCH --job-name=basic
#SBATCH --output=slurm_output.%x-o%j
#SBATCH --error=slurm_error_8_sending.%x-o%j

path_to_exe=${1:-/home/zli89/fabric_bench/cmake-build-debug/benchmark}

# module load intel mvapich2
# echo "srun -n 2 osu_bw"
# srun -n 2 --mpi=pmi2 ~/osu_bw -m 8:262144 -i 40000

# export OMP_DISPLAY_ENV=verbose
# export OMP_DISPLAY_AFFINITY=TRUE

export OMP_PLACES=cores

# echo "srun -n 2 (96 sending processes)"
# srun --cpu-bind=verbose,cores --nodes=2 --ntasks-per-node=96 --ntasks=192 --mpi=pmi2 ${path_to_exe}/ibv_zhengxi_fix_buf_large_heap_mem_align_page_pmi_barrier 1 8 262144

echo "srun -n 2 pingpong_prg_test(1 sending thread)"
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${path_to_exe}/pingpong_sym 1 8 8
# export OMP_PLACES="{64},{65},{66},{67},{68},{69},{70},{71},{72},{73},{74},{75},{76},{77},{78},{79},{80},{81},{82},{83},{84},{85},{86},{87},{88},{89},{90},{91},{92},{93},{94},{95},{96},{97},{98},{99},{100},{101},{102},{103},{104},{105},{106},{107},{108},{109},{110},{111},{112},{113},{114},{115},{116},{117},{118},{119},{120},{121},{122},{123},{124},{125},{126},{127}"
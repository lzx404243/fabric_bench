#!/bin/bash
#SBATCH --partition=compute
#SBATCH --account=uic193
#SBATCH --time=01:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=128
#SBATCH --exclusive
#SBATCH --job-name=basic
#SBATCH --output=slurm_output_%j.txt
#SBATCH --error=slurm_error_%j.txt

fabric=ibv
path_to_exe=${1:-/home/zli89/clion_fb/cmake-build-debug/benchmark/${fabric}}
numactl_command='numactl --interleave=all'
perf_command='perf record -o /home/zli89/clion_fb/scripts/progress/7-27-results/profile/test_perf.data -a -g --'
#numactl_command=''
echo "running with numa command: ${numactl_command}"

export OMP_PLACES=cores

#echo "srun -n 2 pingpong_sym(${fabric}-multi-thread-numa-interleave)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 2 8 8
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 4 8 8
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 8 8 8
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 16 8 8
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 32 8 8
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 64 8 8
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 128 8 8

# with numactl interleave
#echo "srun -n 2 pingpong_sym_numactl(${fabric}-multi-thread-8b-all-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 128 8 8

# perf

## multi-thread perf
#echo "srun -n 2 pingpong_sym_perf_64_worker(${fabric}-multi-thread-8b-all-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 perf stat -e task-clock,cs,cpu-migrations,cycles,stalled-cycles-frontend,stalled-cycles-backend,instructions,page-faults,cache-references,cache-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses -o /home/zli89/perf_output_64.txt ${path_to_exe}/pingpong_sym 64 8 8
#echo "srun -n 2 pingpong_sym_perf_128_worker(${fabric}-multi-thread-8b-all-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 perf stat -e task-clock,cs,cpu-migrations,cycles,stalled-cycles-frontend,stalled-cycles-backend,instructions,page-faults,cache-references,cache-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses -o /home/zli89/perf_output_128.txt ${path_to_exe}/pingpong_sym 128 8 8
#echo "srun -n 2 pingpong_sym_perf_test(${fabric}-multi-thread-8b-all-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${perf_command} ${path_to_exe}/pingpong_sym 2 8 8

## multi-process perf
#echo "srun -n 2 pingpong_sym_perf_1_worker(${fabric}-multi-process-8b-all-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --cpu-bind=core --mpi=pmi2 perf stat -e task-clock,cs,cpu-migrations,cycles,stalled-cycles-frontend,stalled-cycles-backend,instructions,page-faults,cache-references,cache-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses -o /home/zli89/perf_output_mp_1_worker.txt ${path_to_exe}/pingpong_sym 1 8 8

#echo "srun -n 2 pingpong_sym_perf_64_worker(${fabric}-multi-thread-8b-all-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 perf record -o /home/zli89/clion_fb/scripts/progress/7-27-results/profile/perf_64_cache_misses.data -e cache-misses ${path_to_exe}/pingpong_sym 64 8 8
#echo "srun -n 2 pingpong_sym_perf_128_worker(${fabric}-multi-thread-8b-all-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 perf record -o /home/zli89/clion_fb/scripts/progress/7-27-results/profile/perf_128_cache_misses_with_callstack_${SLURM_NODEID}.data -e cache-misses ${path_to_exe}/pingpong_sym 128 8 8
echo "srun -n 2 pingpong_sym_perf_128_worker(${fabric}-multi-thread-8b-all-threads)"
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 bash /home/zli89/clion_fb/scripts/perf.sh

#echo "srun -n 2 pingpong_sym(${fabric}-multi-process-8b-all-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --cpu-bind=core --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8
#srun --nodes=2 --ntasks-per-node=2 --ntasks=4 --cpu-bind=core --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8
#srun --nodes=2 --ntasks-per-node=4 --ntasks=8 --cpu-bind=core --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8
#srun --nodes=2 --ntasks-per-node=8 --ntasks=16 --cpu-bind=core --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8
#srun --nodes=2 --ntasks-per-node=16 --ntasks=32 --cpu-bind=core --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8
#srun --nodes=2 --ntasks-per-node=32 --ntasks=64 --cpu-bind=core --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8
#srun --nodes=2 --ntasks-per-node=64 --ntasks=128 --cpu-bind=core --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8
#srun --nodes=2 --ntasks-per-node=128 --ntasks=256 --cpu-bind=core --mpi=pmi2 ${numactl_command} ${path_to_exe}/pingpong_sym 1 8 8

### all messages sizes
#echo "srun -n 2 pingpong_sym(${fabric}-multi-threaded-8b-64k-64-threads)"
#srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --mpi=pmi2 ${path_to_exe}/pingpong_sym 64 8 65536
#
#echo "srun -n 2 pingpong_sym(${fabric}-multi-process-8b-64k-64-threads)"
#srun --nodes=2 --ntasks-per-node=64 --ntasks=128 --cpu-bind=core --mpi=pmi2 ${path_to_exe}/pingpong_sym 1 8 65536
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
for i in $(eval echo {1..${1:-4}}); do

export OMP_PLACES=cores
echo "srun -n 2 prg_test_compute(1 prg-0us-prefill)" >> slurm_output_$i.txt
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 15 1 8 8 '15' 0 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 31 1 8 8 '31' 0 8
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 63 1 8 8 '63' 0 8

export OMP_PROC_BIND=close;
export OMP_PLACES=cores;
echo "srun -n 2 prg_test_compute(2 prg-0us-prefill)" >> slurm_output_$i.txt
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 14 2 8 8 '14,15' 0 8
export OMP_PLACES='{0},{16},{1},{17},{2},{18},{3},{19},{4},{20},{5},{21},{6},{22},{7},{23},{8},{24},{9},{25},{10},{26},{11},{27},{12},{28},{13},{29},{14},{30}'
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 30 2 8 8 '15,31' 0 8
export OMP_PLACES='{0},{16},{32},{48},{1},{17},{33},{49},{2},{18},{34},{50},{3},{19},{35},{51},{4},{20},{36},{52},{5},{21},{37},{53},{6},{22},{38},{54},{7},{23},{39},{55},{8},{24},{40},{56},{9},{25},{41},{57},{10},{26},{42},{58},{11},{27},{43},{59},{12},{28},{44},{60},{13},{29},{45},{61},{14},{30},{46},{62},{15},{31}'
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 62 2 8 8 '47,63' 0 8

echo "srun -n 2 prg_test_compute(4 prg-0us-prefill)" >> slurm_output_$i.txt
export OMP_PLACES='{0},{16},{1},{17},{2},{18},{3},{19},{4},{20},{5},{21},{6},{22},{7},{23},{8},{24},{9},{25},{10},{26},{11},{27},{12},{28},{13},{29}'
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 28 4 8 8 '14,30,15,31' 0 8
export OMP_PLACES='{0},{16},{32},{48},{1},{17},{33},{49},{2},{18},{34},{50},{3},{19},{35},{51},{4},{20},{36},{52},{5},{21},{37},{53},{6},{22},{38},{54},{7},{23},{39},{55},{8},{24},{40},{56},{9},{25},{41},{57},{10},{26},{42},{58},{11},{27},{43},{59},{12},{28},{44},{60},{13},{29},{45},{61},{14},{30},{46},{62}'
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 60 4 8 8 '15,31,47,63' 0 8

echo "srun -n 2 prg_test_compute(8 prg-0us-prefill)" >> slurm_output_$i.txt
export OMP_PLACES='{0},{16},{32},{48},{1},{17},{33},{49},{2},{18},{34},{50},{3},{19},{35},{51},{4},{20},{36},{52},{5},{21},{37},{53},{6},{22},{38},{54},{7},{23},{39},{55},{8},{24},{40},{56},{9},{25},{41},{57},{10},{26},{42},{58},{11},{27},{43},{59},{12},{28},{44},{60},{13},{29},{45},{61}'
srun --nodes=2 --ntasks-per-node=1 --ntasks=2 --output=slurm_output_$i.txt --error=slurm_error_$i.txt --open-mode=append --mpi=pmi2 ${path_to_exe}/pingpong_prg_tmp 56 8 8 8 '14,30,46,62,15,31,47,63' 0 8

done

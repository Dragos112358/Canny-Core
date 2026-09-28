#!/bin/bash
#SBATCH --job-name=profiling_canny
#SBATCH --output=profiling.%j.out
#SBATCH --error=profiling.%j.err
#SBATCH --partition=haswell
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=1G
#SBATCH --time=00:05:00

NUM_THREADS=8
LOW=15
HIGH=45
KERNEL_SIZE=9
SIGMA=1.5

echo "=== SLURM job info ==="
echo "Job ID: $SLURM_JOB_ID"
echo "Node(s): $SLURM_JOB_NODELIST"
echo "User: $USER"

echo "Profiling..."
#make run ARGS="./build/debug-tools.sif perf stat ./../Canny_serial/Canny_contur ../images/input/city.jpg ../images/output/city_canny.jpg"
#apptainer exec ./build/debug-tools.sif perf stat ./../Canny_serial/Canny_contur ../images/input/city.jpg ../images/output/city.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA
#apptainer exec ./build/debug-tools.sif valgrind ./../Canny_serial/Canny_contur ../images/input/poza.jpg ../images/output/poza.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA
#apptainer exec ./build/debug-tools.sif perf record -g ./../Canny_serial/Canny_contur ../images/input/poza.jpg ../images/output/poza.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA
#apptainer exec ./build/debug-tools.sif perf report

#Pthreads
#apptainer exec ./build/debug-tools.sif perf stat ./../Canny_pthreads/Canny_pthreads ../images/input/poza.jpg ../images/output/poza.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA $NUM_THREADS
#apptainer exec ./build/debug-tools.sif valgrind ./../Canny_pthreads/Canny_pthreads ../images/input/poza.jpg ../images/output/poza.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA $NUM_THREADS

#apptainer exec ./build/debug-tools.sif perf stat ./../Canny_pthreads/Canny_pthreads ../images/input/1_earth_8k.jpg ../images/output/1_earth_8k $LOW $HIGH $KERNEL_SIZE $SIGMA $NUM_THREADS
#apptainer exec ./build/debug-tools.sif valgrind ./../Canny_pthreads/Canny_pthreads ../images/input/poza.jpg ../images/output/poza.jpg $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA) $NUM_THREADS

#OpenMp
#apptainer exec ./build/debug-tools.sif perf stat ./../Canny_openmp/Canny_openmp ../images/input/poza.jpg ../images/output/poza.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA $NUM_THREADS
#apptainer exec ./build/debug-tools.sif valgrind ./../Canny_openmp/Canny_openmp ../images/input/poza.jpg ../images/output/poza.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA $NUM_THREADS

#MPI
apptainer exec ./build/debug-tools.sif perf stat mpirun -np $NUM_THREADS --oversubscribe ./../Canny_mpi/Canny_mpi ../images/input/poza.jpg ../images/output/poza.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA
#apptainer exec ./build/debug-tools.sif valgrind ./../Canny_mpi/Canny_mpi ../images/input/poza.jpg ../images/output/poza.jpg $LOW $HIGH $KERNEL_SIZE $SIGMA  

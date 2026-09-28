#!/bin/bash
#SBATCH --job-name=canny_matrix
#SBATCH --output=profiling/intel_vtune/profiling_%j.out
#SBATCH --error=profiling/intel_vtune/profiling_%j.err
#SBATCH --partition=xl
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=8G
#SBATCH --time=9:59
#SBATCH --gres=gpu:1

# Load modules
module avail
module load libraries/cuda-11.4
module load mpi/openmpi-x86_64

# Load intelvtune
source /opt/intel/oneapi/vtune/latest/vtune-vars.sh

# Parameters
INPUT="images/input/1_earth_8k.jpg"
OUTPUT="images/output/output_canny.png"
LOW=15
HIGH=45
KERNEL_SIZE=9
SIGMA=1.5
TASKS=${SLURM_NTASKS}
THREADS=${SLURM_CPUS_PER_TASK}
BLOCK_SIZE=16

# Default to serial if not specified
IMPL=${1:-serial}

case "${IMPL,,}" in
    serial)
        make build_serial
        vtune -collect performance-snapshot -r profiling/${IMPL}_perf make run_serial INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
        vtune -collect hotspots -r profiling/${IMPL}_hotspots make run_serial INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	;;
    pthreads|pthread)
        make build_pthreads
        vtune -collect performance-snapshot -r profiling/${IMPL}_perf make run_pthreads THREADS=$THREADS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
        vtune -collect hotspots -r profiling/${IMPL}_hotspots make run_pthreads THREADS=$THREADS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	;;
    openmp|omp)
        export OMP_NUM_THREADS=$THREADS
        make build_openmp
        vtune -collect performance-snapshot -r profiling/${IMPL}_perf make run_openmp THREADS=$THREADS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
        vtune -collect hotspots -r profiling/${IMPL}_hotspots make run_openmp THREADS=$THREADS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	;;
    mpi)
        module load mpi/openmpi-x86_64

        make build_mpi
        vtune -collect performance-snapshot -r profiling/${IMPL}_perf make run_mpi TASKS=$TASKS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
        vtune -collect hotspots -r profiling/${IMPL}_hotspots make run_mpi TASKS=$TASKS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	;;
    cuda)
        make build_cuda
        make run_cuda INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA BLOCK_SIZE=$BLOCK_SIZE
        ;;
    hybrid_mpi_openmp | mpi_openmp)
        export OMP_NUM_THREADS=$THREADS
        module load mpi/openmpi-x86_64

        make build_hybrid_mpi_openmp
        make run_hybrid_mpi_openmp TASKS=$TASKS THREADS=$THREADS  INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
        vtune -collect performance-snapshot -r profiling/${IMPL}_perf make run_hybrid_mpi_openmp TASKS=$TASKS THREADS=$THREADS  INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	vtune -collect hotspots -r profiling/${IMPL}_hotspots make run_hybrid_mpi_openmp TASKS=$TASKS THREADS=$THREADS  INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	;;
    hybrid_mpi_pthreads | mpi_pthreads)
        export OMP_NUM_THREADS=$THREADS
        module load mpi/openmpi-x86_64
        make build_hybrid_mpi_pthreads
        make run_hybrid_mpi_pthreads TASKS=$TASKS THREADS=$THREADS  INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	vtune -collect performance-snapshot -r profiling/${IMPL}_perf make run_hybrid_mpi_pthreads TASKS=$TASKS THREADS=$THREADS  INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	vtune -collect hotspots -r profiling/${IMPL}_hotspots make run_hybrid_mpi_pthreads TASKS=$TASKS THREADS=$THREADS  INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA
	;;

    build)
        make build
        ;;
    cuda_mpi|mpi_cuda)
        module load mpi/openmpi-x86_64

        make build_cuda_mpi
        make run_cuda_mpi TASKS=$TASKS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA BLOCK_SIZE=$BLOCK_SIZE
	vtune -collect performance-snapshot -r profiling/${IMPL}_perf make run_cuda_mpi TASKS=$TASKS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA BLOCK_SIZE=$BLOCK_SIZE
        vtune -collect hotspots -r profiling/${IMPL}_hotspots make run_cuda_mpi TASKS=$TASKS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA BLOCK_SIZE=$BLOCK_SIZE
	;;
    cuda_openmp|openmp_cuda)
        make build_cuda_openmp
        make run_cuda_openmp THREADS=$THREADS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA BLOCK_SIZE=$BLOCK_SIZE
	vtune -collect performance-snapshot -r profiling/${IMPL}_perf make run_cuda_openmp THREADS=$THREADS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA BLOCK_SIZE=$BLOCK_SIZE
	vtune -collect hotspots -r profiling/${IMPL}_hotspots make run_cuda_openmp THREADS=$THREADS INPUT=$INPUT OUTPUT=$OUTPUT LOW=$LOW HIGH=$HIGH KERNEL_SIZE=$KERNEL_SIZE SIGMA=$SIGMA BLOCK_SIZE=$BLOCK_SIZE
        ;;
    clean)
        make clean
        ;;
    *)
        echo "Usage: $0 {serial|pthreads|openmp|mpi|cuda|build|hybrid_mpi_openmp|cuda_mpi|cuda_openmp|clean|all}"
        exit 1
        ;;
esac

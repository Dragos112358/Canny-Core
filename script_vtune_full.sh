#!/bin/bash
#SBATCH --job-name=vtune_full_matrix
#SBATCH --output=vtune_log_%j.out
#SBATCH --error=vtune_log_%j.err
#SBATCH --partition=xl
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=16G
#SBATCH --time=9:59
#SBATCH --gres=gpu:1

# ================= CONFIGURARE =================
# 1. Incarcare module
module purge
module load libraries/cuda-11.4
module load mpi/openmpi-x86_64
source /opt/intel/oneapi/vtune/latest/vtune-vars.sh

# 2. Parametri Algoritm
INPUT="images/input/1_earth_8k.jpg"
OUTPUT="images/output/output_vtune.png"
LOW=15
HIGH=45
KERNEL_SIZE=9
SIGMA=1.5
BLOCK_SIZE=16

# 3. Directorul principal pentru rezultate VTune
BASE_RES_DIR="profiling/vtune_results"
mkdir -p $BASE_RES_DIR

# ================= FUNCTIE AJUTATOARE =================
# Aceasta functie ruleaza VTune si gestioneaza directoarele
run_vtune() {
    local test_name=$1
    local cmd=$2

    # Numele folderului de rezultat specific acestui test
    local res_dir="${BASE_RES_DIR}/${test_name}"

    echo ">>> [START] Profiling: $test_name"
    echo "    CMD: $cmd"

    # Curatam rezultatul vechi daca exista (VTune crapa daca folderul exista)
    rm -rf "$res_dir"

    # Rulam doar Hotspots pentru viteza si relevanta pe paralelism
    # Daca vrei si performance-snapshot, decomenteaza linia corespunzatoare, dar va dura dublu.

    # vtune -collect performance-snapshot -r "${res_dir}_perf" $cmd
    vtune -collect hotspots -r "$res_dir" $cmd

    echo ">>> [DONE] Saved to: $res_dir"
    echo "----------------------------------------------------------------"
}

# ================= COMPILARE =================
echo ">>> Building executables..."
# Le compilam pe toate de la inceput sa nu pierdem timp in bucla
make build_mpi
make build_cuda
make build_hybrid_mpi_openmp
make build_hybrid_mpi_pthreads
make build_cuda_mpi
# make build_cuda_openmp # Decomenteaza daca ai si asta

# ================= SCENARII DE TESTARE =================

# -------------------------------------------------------
# 1. MPI (1, 2, 4, 8 Task-uri)
# -------------------------------------------------------
echo "=== Starting MPI Profiling ==="
for P in 1 2 4 8; do
    # Nota: Folosim mpirun in fata executabilului
    CMD="mpirun -np $P --oversubscribe ./Canny_mpi/Canny_mpi $INPUT $OUTPUT $LOW $HIGH $KERNEL_SIZE $SIGMA"
    run_vtune "mpi_tasks_${P}" "$CMD"
done

# -------------------------------------------------------
# 2. CUDA (Standard)
# -------------------------------------------------------
echo "=== Starting CUDA Profiling ==="
CMD="./Canny_cuda/Canny_cuda $INPUT $OUTPUT $LOW $HIGH $KERNEL_SIZE $SIGMA $BLOCK_SIZE"
run_vtune "cuda_basic" "$CMD"

# -------------------------------------------------------
# 3. HYBRID: MPI + OpenMP
# -------------------------------------------------------
# Matrice: MPI (1, 2, 4) x Threads (2, 4)
# Nu mergem pana la 8x8 pentru ca depasim nucleele fizice uzuale (oversubscribe excesiv)
echo "=== Starting Hybrid MPI + OpenMP ==="
for P in 1 2 4; do
    for T in 2 4; do
        # Exportam variabila pentru OpenMP
        export OMP_NUM_THREADS=$T
        CMD="mpirun -np $P --oversubscribe ./Canny_hybrid_mpi_openmp/Canny_hybrid_mpi_openmp $INPUT $OUTPUT $LOW $HIGH $KERNEL_SIZE $SIGMA $T"
        # Resetam variabila in interiorul comenzii VTune nu e nevoie, dar e safe
        run_vtune "hybrid_mpi_omp_P${P}_T${T}" "$CMD"
    done
done

# -------------------------------------------------------
# 4. HYBRID: MPI + Pthreads
# -------------------------------------------------------
echo "=== Starting Hybrid MPI + Pthreads ==="
for P in 1 2 4; do
    for T in 2 4; do
        CMD="mpirun -np $P --oversubscribe ./Canny_mpi_pthreads/Canny_mpi_pthreads $INPUT $OUTPUT $LOW $HIGH $KERNEL_SIZE $SIGMA $T"
        run_vtune "hybrid_mpi_pthread_P${P}_T${T}" "$CMD"
    done
done

# -------------------------------------------------------
# 5. HYBRID: CUDA + MPI
# -------------------------------------------------------
# Aici iteram doar procesele MPI, GPU-ul e unul singur partajat
echo "=== Starting Hybrid CUDA + MPI ==="
for P in 1 2 4; do
    CMD="mpirun -np $P --oversubscribe ./Canny_cuda_mpi/Canny_cuda_mpi $INPUT $OUTPUT $LOW $HIGH $KERNEL_SIZE $SIGMA $BLOCK_SIZE"
    run_vtune "hybrid_cuda_mpi_P${P}" "$CMD"
done

echo "=========================================="
echo "ALL PROFILING COMPLETED."
echo "Results are in folder: $BASE_RES_DIR"
echo "To download them, archive the folder first:"
echo "tar -czf vtune_results.tar.gz $BASE_RES_DIR"

#!/bin/bash
#SBATCH --job-name=cuda_omp_scale
#SBATCH --output=cuda_omp_scale_%j.out
#SBATCH --error=cuda_omp_scale_%j.err
#SBATCH --partition=xl
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=8G
#SBATCH --time=9:59
#SBATCH --gres=gpu:1

# ================= CONFIGURARE =================
module load libraries/cuda-11.4 2>/dev/null || true
module load mpi/openmpi-x86_64

# Parametri Algoritm
LOW=15
HIGH=45
KERNEL_SIZE=9
SIGMA=1.5
BLOCK_SIZE=16

# Cai Foldere
INPUT_DIR="images/input"
OUTPUT_DIR="images/output"
RESULT_FILE="RESULTS_CUDA_OMP_SCALING.log"

# Executabilul Tinta
EXECUTABLE="./Canny_cuda_openmp/Canny_cuda_openmp"

# --- CONFIGURARE CONTAINER ---
SIF_IMAGE="$(pwd)/profiling/build/debug-tools.sif"
if [ ! -f "$SIF_IMAGE" ]; then
    SIF_IMAGE=$(find $(pwd) -name "debug-tools.sif" | head -n 1)
fi

if [ -z "$SIF_IMAGE" ]; then
    echo "Eroare: Nu am gasit debug-tools.sif!"
    exit 1
fi

CONTAINER="apptainer exec --nv -B $(pwd) $SIF_IMAGE"
PERF_CMD="perf stat -d"

mkdir -p $OUTPUT_DIR

# Initializare Log
echo "=== CANNY CUDA + OPENMP SCALING BENCHMARK ===" > $RESULT_FILE
echo "Date: $(date)" >> $RESULT_FILE
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null || echo 'Unknown')" >> $RESULT_FILE
echo "Node CPU Cores Available: $SLURM_CPUS_PER_TASK" >> $RESULT_FILE
echo "=============================================" >> $RESULT_FILE

# =========================================================
# --- COMPILARE ---
# =========================================================
echo "Building project..."
make clean > /dev/null
make build > /dev/null || { echo "Build failed!"; exit 1; }

if [ ! -f "$EXECUTABLE" ]; then
    echo "Eroare: $EXECUTABLE nu a fost gasit dupa compilare."
    exit 1
fi

# =========================================================
# --- BUCLA DE TESTARE (1, 2, 4, 8 Thread-uri) ---
# =========================================================

# Iteram prin numarul de thread-uri dorit
for T in 1 2 4 8; do
    echo ">>> Running CUDA + OpenMP with OMP_NUM_THREADS=$T ..."

    # Scriere header in fisierul de rezultate pentru claritate
    {
        echo ""
        echo "################################################################"
        echo " CONFIG: CUDA + OpenMP | Threads: $T | BlockSize: $BLOCK_SIZE"
        echo "################################################################"
    } >> $RESULT_FILE

    # Construim comanda
    # Setam variabila de mediu OMP_NUM_THREADS chiar inainte de executie
    CMD="env OMP_NUM_THREADS=$T $EXECUTABLE $INPUT_DIR $OUTPUT_DIR $LOW $HIGH $KERNEL_SIZE $SIGMA $BLOCK_SIZE"

    # Afisam comanda si in log
    echo "CMD: $CMD" >> $RESULT_FILE

    # Executie
    $CONTAINER $PERF_CMD $CMD >> $RESULT_FILE 2>&1
done

echo "Done. All scaling tests completed."
echo "Results saved in: $RESULT_FILE"

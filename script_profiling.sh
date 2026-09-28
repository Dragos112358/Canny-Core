#!/bin/bash
#SBATCH --job-name=canny_matrix
#SBATCH --output=profiling_%j.out
#SBATCH --error=profiling_%j.err
#SBATCH --partition=haswell
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=8G
#SBATCH --time=9:59
##SBATCH --gres=gpu:1

# ================= CONFIGURARE =================
# Incarcam modulele (ignoram erorile de CUDA pe Haswell)
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
RESULT_FILE="FULL_RESULTS.log"

# --- CONFIGURARE CONTAINER ---
SIF_IMAGE="$(pwd)/profiling/build/debug-tools.sif"
if [ ! -f "$SIF_IMAGE" ]; then
    SIF_IMAGE=$(find $(pwd) -name "debug-tools.sif" | head -n 1)
fi

CONTAINER="apptainer exec --nv -B $(pwd) $SIF_IMAGE"
PERF_CMD="perf stat -d"

mkdir -p $OUTPUT_DIR

# =========================================================
# --- DETECTIE AUTOMATA GPU / PARTITIE ---
# =========================================================
ENABLE_CUDA=true
CURRENT_PARTITION="${SLURM_JOB_PARTITION:-unknown}"

echo ">>> Checking Environment..."
echo ">>> Current Partition: $CURRENT_PARTITION"

# 1. Daca suntem pe Haswell, oprim CUDA
if [[ "$CURRENT_PARTITION" == "haswell" ]]; then
    echo ">>> DETECTED CPU-ONLY PARTITION (haswell). DISABLING CUDA TESTS."
    ENABLE_CUDA=false
fi

# 2. Verificare suplimentara hardware
if [ "$ENABLE_CUDA" = true ]; then
    if ! command -v nvidia-smi &> /dev/null; then
        echo ">>> WARNING: nvidia-smi not found! Disabling CUDA."
        ENABLE_CUDA=false
    else
        echo ">>> GPU Detected. CUDA tests ENABLED."
    fi
fi

# =========================================================
# --- LOGICA SELECTIE IMAGINI ---
# =========================================================
if [ -n "$1" ]; then
    TARGET_IMG="$INPUT_DIR/$1"
    if [ -f "$TARGET_IMG" ]; then
        IMAGES="$TARGET_IMG"
        echo ">>> MODE: SINGLE IMAGE PROCESSING ($1)"
        SINGLE_MODE=true
    else
        echo "ERROR: Image '$1' not found inside '$INPUT_DIR'"
        ls $INPUT_DIR
        exit 1
    fi
else
    IMAGES=$(find $INPUT_DIR -name "*.jpg" -o -name "*.png" -o -name "*.jpeg")
    echo ">>> MODE: BATCH PROCESSING (ALL IMAGES)"
    SINGLE_MODE=false
fi

# Header Log File
echo "=== CANNY EDGE DETECTION PROFILING LOG ===" > $RESULT_FILE
echo "Date: $(date)" >> $RESULT_FILE
echo "Partition: $CURRENT_PARTITION | CUDA Enabled: $ENABLE_CUDA" >> $RESULT_FILE
echo "Mode: $( [ "$SINGLE_MODE" = true ] && echo "Single ($1)" || echo "Batch" )" >> $RESULT_FILE
echo "==========================================" >> $RESULT_FILE

# =========================================================
# --- COMPILARE ADAPTIVA ---
# =========================================================
echo "Building project..."
make clean > /dev/null

if [ "$ENABLE_CUDA" = true ]; then
    # Pe GPU compilam tot (build-ul default care include si build_gpu)
    echo "  -> Compiling FULL project (CPU + GPU)..."
    make build > /dev/null || { echo "Build failed!"; exit 1; }
else
    # Pe CPU compilam doar target-ul 'build_cpu' creat in Makefile
    echo "  -> Compiling CPU-ONLY components..."
    make build_cpu > /dev/null || { echo "CPU Build failed!"; exit 1; }
    echo "  -> CPU Build finished."
fi

# ================= FUNCTIE DE TESTARE =================
run_test_case() {
    local impl=$1
    local img_path=$2
    local proc=$3
    local threads=$4
    local cmd=$5

    local img_name=$(basename "$img_path")
    echo "  -> [$impl] Config: P=$proc | T=$threads on $img_name"

    {
        echo ""
        echo "################################################################"
        echo " IMAGE: $img_name | IMPL: $impl | Config: P=$proc x T=$threads"
        echo " CMD: $cmd"
        echo "################################################################"
    } >> $RESULT_FILE

    $CONTAINER $PERF_CMD $cmd >> $RESULT_FILE 2>&1
}

# ================= ITERARE IMAGINI =================
for IMG in $IMAGES; do
    IMG_BASE=$(basename "$IMG")
    OUT_IMG="$OUTPUT_DIR/${IMG_BASE}_out.png"

    echo "------------------------------------------------"
    echo ">>> PROCESSING IMAGE: $IMG_BASE"
    echo "------------------------------------------------"

    # 1. SERIAL
    CMD="./Canny_serial/Canny_contur $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA"
    run_test_case "Serial" "$IMG" "1" "1" "$CMD"

    # 2. CUDA
    if [ "$ENABLE_CUDA" = true ]; then
        CMD="./Canny_cuda/Canny_cuda $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA $BLOCK_SIZE"
        run_test_case "CUDA" "$IMG" "1" "1" "$CMD"
    fi

    # 3. PTHREADS & OPENMP
    for T in 1 2 4 8; do
        CMD="./Canny_pthreads/Canny_pthreads $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA $T"
        run_test_case "Pthreads" "$IMG" "1" "$T" "$CMD"

        CMD="env OMP_NUM_THREADS=$T ./Canny_openmp/Canny_openmp $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA"
        run_test_case "OpenMP" "$IMG" "1" "$T" "$CMD"
    done

    # 4. MPI
    for P in 1 2 4 8; do
        CMD="mpirun -np $P --oversubscribe ./Canny_mpi/Canny_mpi $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA"
        run_test_case "MPI" "$IMG" "$P" "1" "$CMD"
    done

    # 5. MATRICEA HIBRIDA
    for P in 1 2 4; do
        for T in 1 2 4; do
            # Hybrid MPI + OpenMP
            CMD="mpirun -np $P --oversubscribe ./Canny_hybrid_mpi_openmp/Canny_hybrid_mpi_openmp $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA $T"
            run_test_case "Hybrid_MPI_OMP" "$IMG" "$P" "$T" "$CMD"

            # Hybrid MPI + Pthreads
            CMD="mpirun -np $P --oversubscribe ./Canny_mpi_pthreads/Canny_mpi_pthreads $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA $T"
            run_test_case "Hybrid_MPI_Pthreads" "$IMG" "$P" "$T" "$CMD"

            # Hybrid CUDA + MPI
            if [ "$ENABLE_CUDA" = true ]; then
                CMD="mpirun -np $P --oversubscribe ./Canny_cuda_mpi/Canny_cuda_mpi $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA $BLOCK_SIZE"
                run_test_case "Hybrid_CUDA_MPI" "$IMG" "$P" "1" "$CMD"
            fi
        done
    done
done

# 6. CUDA + OPENMP (FOLDER)
if [ "$SINGLE_MODE" = false ] && [ "$ENABLE_CUDA" = true ]; then
    echo "" >> $RESULT_FILE
    echo " SPECIAL: CUDA + OPENMP (Folder Level Processing) | T=4" >> $RESULT_FILE
    CMD="env OMP_NUM_THREADS=4 ./Canny_cuda_openmp/Canny_cuda_openmp $INPUT_DIR $OUTPUT_DIR $LOW $HIGH $KERNEL_SIZE $SIGMA $BLOCK_SIZE"
    $CONTAINER $PERF_CMD $CMD >> $RESULT_FILE 2>&1
fi

echo "Done. All results saved in $RESULT_FILE"
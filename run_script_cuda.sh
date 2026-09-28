#!/bin/bash
#SBATCH --job-name=cuda_blocks
#SBATCH --output=cuda_blocks_%j.out
#SBATCH --error=cuda_blocks_%j.err
#SBATCH --partition=xl
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=10:00
#SBATCH --gres=gpu:1

# ================= CONFIGURARE =================
module purge
module load libraries/cuda-11.4
module load mpi/openmpi-x86_64

# Parametri Algoritm Constanți
LOW=15
HIGH=45
KERNEL_SIZE=9
SIGMA=1.5

# --- CELE 5 TIPURI DE BLOCK SIZE ---
# 4x4, 8x8, 16x16, 32x32, 64x64 (doar dimensiunea pe o latura)
BLOCK_SIZES="2 4 8 16 32 64"

# Cai Foldere
INPUT_DIR="images/input"
OUTPUT_DIR="images/output_cuda_blocks"
RESULT_FILE="FULL_RESULTS_CUDA_BLOCKS.log"

# --- CONFIGURARE CONTAINER ---
SIF_IMAGE="$(pwd)/profiling/build/debug-tools.sif"
if [ ! -f "$SIF_IMAGE" ]; then
    SIF_IMAGE=$(find $(pwd) -name "debug-tools.sif" | head -n 1)
fi

CONTAINER="apptainer exec --nv -B $(pwd) $SIF_IMAGE"
PERF_CMD="perf stat -d"

mkdir -p $OUTPUT_DIR

# Verificare GPU
if ! command -v nvidia-smi &> /dev/null; then
    echo "ERROR: nvidia-smi not found! This script requires a GPU node."
    exit 1
fi

# =========================================================
# --- SELECTIE IMAGINI ---
# =========================================================
IMAGES=$(find $INPUT_DIR -name "*.jpg" -o -name "*.png" -o -name "*.jpeg")
if [ -z "$IMAGES" ]; then
    echo "ERROR: No images found in $INPUT_DIR"
    exit 1
fi

# Header Log File
echo "=== CUDA BLOCK SIZE PROFILING LOG ===" > $RESULT_FILE
echo "Date: $(date)" >> $RESULT_FILE
echo "Block Sizes Tested: $BLOCK_SIZES" >> $RESULT_FILE
echo "=====================================" >> $RESULT_FILE

# =========================================================
# --- COMPILARE ---
# =========================================================
echo "Building CUDA project..."
make clean > /dev/null
make build_cuda > /dev/null || { echo "CUDA Build failed!"; exit 1; }

# ================= FUNCTIE DE TESTARE =================
run_test_case() {
    local impl=$1
    local img_path=$2
    local proc=$3      # P (nu e relevant la cuda simplu, punem 1)
    local threads=$4   # T (aici vom pune BlockSize pentru referinta)
    local cmd=$5

    local img_name=$(basename "$img_path")

    # Scriem in log formatul pe care il asteapta scriptul Python
    # Hack: Punem BlockSize in loc de Threads sau in nume ca sa le diferentiem
    {
        echo ""
        echo "################################################################"
        echo " IMAGE: $img_name | IMPL: $impl | Config: P=$proc x T=$threads"
        echo " CMD: $cmd"
        echo "################################################################"
    } >> $RESULT_FILE

    echo "  -> Running: $impl on $img_name"
    $CONTAINER $PERF_CMD $cmd >> $RESULT_FILE 2>&1
}

# ================= ITERARE =================
for IMG in $IMAGES; do
    IMG_BASE=$(basename "$IMG")

    echo "------------------------------------------------"
    echo ">>> PROCESSING IMAGE: $IMG_BASE"
    echo "------------------------------------------------"

    for BS in $BLOCK_SIZES; do
        # Definim un nume de output unic sa nu se suprascrie imaginile
        OUT_IMG="$OUTPUT_DIR/${IMG_BASE}_BS${BS}.png"

        # Comanda CUDA
        CMD="./Canny_cuda/Canny_cuda $IMG $OUT_IMG $LOW $HIGH $KERNEL_SIZE $SIGMA $BS"

        # Numele implementarii va fi "CUDA_BS_16", "CUDA_BS_32" etc.
        # Astfel, scriptul Python va genera bare separate pentru fiecare!
        IMPL_NAME="CUDA_BS_${BS}"

        # Apelam functia (P=1, T=BS doar pentru log)
        run_test_case "$IMPL_NAME" "$IMG" "1" "$BS" "$CMD"
    done
done

echo "Done. Results saved in $RESULT_FILE"

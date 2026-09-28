build:
	g++ -std=c++11 -O3 Canny_serial/Canny_contur.cpp Canny_serial/Image.cpp -o Canny_serial/Canny_contur
	g++ -std=c++11 -O3 -pthread Canny_pthreads/Canny_pthreads.cpp Canny_pthreads/Image.cpp -o Canny_pthreads/Canny_pthreads
	g++ -std=c++11 -O3 -fopenmp Canny_openmp/Canny_openmp.cpp Canny_openmp/Image.cpp -o Canny_openmp/Canny_openmp
	mpic++ -o Canny_mpi/Canny_mpi Canny_mpi/Canny_mpi.cpp Canny_mpi/Image.cpp
	nvcc -std=c++11 -O3 Canny_cuda/Canny_cuda.cu Canny_cuda/Image.cu -o Canny_cuda/Canny_cuda
	mpic++ -std=c++11 -O3 -fopenmp -o Canny_hybrid_mpi_openmp/Canny_hybrid_mpi_openmp\
                            Canny_hybrid_mpi_openmp/Canny_hybrid_mpi_openmp.cpp Canny_hybrid_mpi_openmp/Image.cpp
	mpic++ -O3 -o Canny_mpi_pthreads/Canny_mpi_pthreads Canny_mpi_pthreads/Canny_mpi_pthreads.cpp\
                            Canny_mpi_pthreads/Image.cpp -lpthread
	nvcc -std=c++11 -ccbin mpic++ -O3 Canny_cuda_mpi/Canny_cuda_mpi.cu Canny_cuda_mpi/Image.cu -o Canny_cuda_mpi/Canny_cuda_mpi
	nvcc -std=c++11 -O3 -Xcompiler -fopenmp Canny_cuda_openmp/Canny_cuda_openmp.cu Canny_cuda_openmp/Image.cu -o Canny_cuda_openmp/Canny_cuda_openmp 

clean:
	rm -f Canny_serial/Canny_contur Canny_pthreads/Canny_pthreads Canny_openmp/Canny_openmp Canny_cuda/Canny_cuda Canny_mpi/Canny_mpi\
                     Canny_hybrid_mpi_openmp/Canny_hybrid_mpi_openmp Canny_mpi_pthreads/Canny_mpi_pthreads Canny_cuda_mpi/Canny_cuda_mpi\
                     Canny_cuda_openmp/Canny_cuda_openmp

build: build_cpu build_gpu

# Regula noua: Compileaza DOAR ce merge pe procesor (fara nvcc)
build_cpu: build_serial build_pthreads build_openmp build_mpi build_hybrid_mpi_openmp build_hybrid_mpi_pthreads

# Regula noua: Compileaza partea de GPU
build_gpu: build_cuda build_cuda_mpi build_cuda_openmp
# Executable names
EXEC_SERIAL = ./Canny_serial/Canny_contur
EXEC_PTHREADS = ./Canny_pthreads/Canny_pthreads
EXEC_OPENMP = ./Canny_openmp/Canny_openmp
EXEC_MPI = ./Canny_mpi/Canny_mpi
EXEC_CUDA = ./Canny_cuda/Canny_cuda
EXEC_hybrid_MPI_OPENMP = ./Canny_hybrid_mpi_openmp/Canny_hybrid_mpi_openmp
EXEC_hybrid_MPI_PTHREADS = ./Canny_mpi_pthreads/Canny_mpi_pthreads
EXEC_CUDA_MPI = ./Canny_cuda_mpi/Canny_cuda_mpi
EXEC_CUDA_OPENMP = ./Canny_cuda_openmp/Canny_cuda_openmp

# Parameters
INPUT = images/input/1_earth_8k.jpg
OUTPUT = images/output/output_canny.png
INPUT_FOLDER = images/input
OUTPUT_FOLDER = images/output/output_canny
LOW = 15
HIGH = 45
KERNEL_SIZE = 9
SIGMA = 1.5
THREADS ?= $(shell nproc)
TASKS ?= $(shell nproc)
BLOCK_SIZE = 16


build_serial:
	g++ -std=c++11 -O3 Canny_serial/Canny_contur.cpp Canny_serial/Image.cpp -o Canny_serial/Canny_contur

build_pthreads:
	g++ -std=c++11 -O3 -pthread Canny_pthreads/Canny_pthreads.cpp Canny_pthreads/Image.cpp -o Canny_pthreads/Canny_pthreads

build_openmp:
	g++ -std=c++11 -O3 -fopenmp Canny_openmp/Canny_openmp.cpp Canny_openmp/Image.cpp -o Canny_openmp/Canny_openmp

build_mpi:
	mpic++ -o Canny_mpi/Canny_mpi Canny_mpi/Canny_mpi.cpp Canny_mpi/Image.cpp

build_cuda:
	nvcc -std=c++11 -O3 Canny_cuda/Canny_cuda.cu Canny_cuda/Image.cu -o Canny_cuda/Canny_cuda

build_hybrid_mpi_openmp:
	mpic++ -std=c++11 -O3 -fopenmp -o Canny_hybrid_mpi_openmp/Canny_hybrid_mpi_openmp Canny_hybrid_mpi_openmp/Canny_hybrid_mpi_openmp.cpp Canny_hybrid_mpi_openmp/Image.cpp

build_hybrid_mpi_pthreads:
	mpic++ -O3 -o Canny_mpi_pthreads/Canny_mpi_pthreads Canny_mpi_pthreads/Canny_mpi_pthreads.cpp\
                            Canny_mpi_pthreads/Image.cpp -lpthread

build_cuda_mpi:
	nvcc -std=c++11 -ccbin mpic++ -O3 Canny_cuda_mpi/Canny_cuda_mpi.cu Canny_cuda_mpi/Image.cu -o Canny_cuda_mpi/Canny_cuda_mpi

build_cuda_openmp:
	nvcc -std=c++11 -O3 -Xcompiler -fopenmp Canny_cuda_openmp/Canny_cuda_openmp.cu Canny_cuda_openmp/Image.cu -o Canny_cuda_openmp/Canny_cuda_openmp 


# Run targets
run_serial:
	$(EXEC_SERIAL) $(INPUT) $(OUTPUT) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA)

run_pthreads:
	$(EXEC_PTHREADS) $(INPUT) $(OUTPUT) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA) $(THREADS)

run_openmp:
	$(EXEC_OPENMP) $(INPUT) $(OUTPUT) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA) $(THREADS)

run_mpi:
	mpirun -np $(TASKS) --oversubscribe $(EXEC_MPI) $(INPUT) $(OUTPUT) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA)

run_cuda:
	$(EXEC_CUDA) $(INPUT) $(OUTPUT) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA) $(BLOCK_SIZE)

run_hybrid_mpi_openmp:
	mpirun -np $(TASKS) --oversubscribe $(EXEC_hybrid_MPI_OPENMP) $(INPUT) $(OUTPUT) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA) $(THREADS)

run_hybrid_mpi_pthreads:
	mpirun -np $(TASKS) --oversubscribe $(EXEC_hybrid_MPI_PTHREADS) $(INPUT) $(OUTPUT) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA) $(THREADS)

run_cuda_mpi:
	mpirun -np $(TASKS) --oversubscribe $(EXEC_CUDA_MPI) $(INPUT) $(OUTPUT) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA) $(BLOCK_SIZE)

run_cuda_openmp:
	$(EXEC_CUDA_OPENMP) $(INPUT_FOLDER) $(OUTPUT_FOLDER) $(LOW) $(HIGH) $(KERNEL_SIZE) $(SIGMA) $(BLOCK_SIZE) $(THREADS)

.PHONY: build clean

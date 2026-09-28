build:
	mpic++ -O3 -o Canny_mpi_pthreads Canny_mpi_pthreads.cpp Image.cpp -lpthread

clean:
	rm -f Canny_mpi_pthreads

run:
	mpirun -np 8 ./Canny_mpi_pthreads input.jpg output.png

.PHONY: build clean run

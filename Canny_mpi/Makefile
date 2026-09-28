
build:
	mpic++ -o Canny_mpi Canny_mpi.cpp Image.cpp
clean:
	rm -f Canny_mpi
run:
	mpirun -np 8 ./Canny_mpi
.PHONY: build clean

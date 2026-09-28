## Jurnal de progres săptămânal

### Săptămâna 1 (17 Noiembrie - 23 Noiembrie)
* Implementare algoritm complet Canny Edge Detection (versiune serială C++).
* Profiling detaliat folosind `perf` și `valgrind` pentru a găsi funcțiile cele mai costisitoare.
* Creare scripturi Python (`matplotlib`) pentru vizualizarea timpilor de rulare și a output-ului.

### Săptămâna 2 (24 Noiembrie - 30 Noiembrie)
* Refactoring varianta serială: automatizare generare kernel Gaussian.
* Implementare **MPI** și **Pthreads**.
* Profiling preliminar pentru versiunile paralele.

### Săptămâna 3 (1 Decembrie 🇷🇴 - 7 Decembrie)
* Implementare versiune **CUDA** (modificarea funcțiilor din host în device).
* Implementare versiune **OpenMP** (paralelizare bucle `for`).
* Rulare teste de corectitudine (output-ul trebuie să fie identic cu varianta serială).
* Realizat profiling pentru CUDA (`Nsight Compute` local) și OpenMP.
* Adăugat pagină de wiki completă pentru OpenMP, MPI și CUDA.
* Începutul implementării hibridelor CUDA + pthreads, CUDA + MPI.

### Săptămâna 4 (8 Decembrie - 14 Decembrie)
* Implementare versiune **CUDA + MPI** (procesele preiau un slice din imaginea originală).
* Implementare versiuni **MPI + OpenMP**, **MPI + pthreads**.
* Implementare versiune **CUDA + OpenMP** (toate imaginile sunt prelucrate in paralel pe thread-uri din CPU)
* Adăugat pagină de wiki pentru hibride, pentru varianta serială, pthreads.
* Realizat grafice și profiling în Intel vTune.

### Săptămâna 5 (15 Decembrie - 21 Decembrie)
* Completarea wiki-ului cu informații și grafice noi extrase din Intel vTune.
* Adăugat pagină pentru analiză comparativă.
* Adăugat grafice pentru multithread vs nomultithread pe variantele implementate.

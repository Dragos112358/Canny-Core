
## Implemenatre Hibrid MPI cu OpenMP - Inovații & Diferențe

Implementarea hibrid a algoritmului **Canny Edge Detection** combină paradigma de **memorie distribuită** (MPI) cu cea de **memorie partajată** (OpenMP), creând o arhitectură cu două niveluri de paralelizare. Această abordare exploatează simultan paralelismul între noduri (MPI) și paralelismul la nivel de thread-uri pe fiecare nod (OpenMP).


Strategia de combinare a mai multor tehnici de paralelizare este ideală pentru clustere unde fiecare nod are multiple nuclee CPU. Astfel, MPI distribuie munca între procese, procesând benzi orizontale din imagine, în timp ce OpenMP paralelizează operațiile intensive de procesare a benzii, folosind thread-urile disponibile pe nodul respectiv.


## Modificările realizate


### Arhitectura pe Două Niveluri
* **Nivel MPI** - Imaginea este împărțită pe orizontală în benzi, fiecare proces MPI primind o porțiune distinctă din imagine prin `MPI_Scatterv`. Această distribuție evită duplicarea datelor și reduce overhead-ul de comunicare, fiecare proces lucrând independent pe banda sa.
* **Nivel OpenMP** - Pe fiecare bandă locală, operațiile computațional intensive (Gaussian Blur, Sobel, NMS, Hysteresis) sunt paralelizate folosind `#pragma omp parallel for`. Fiecare thread procesează un subset de rânduri din banda alocată procesului MPI.


### Optimizări specifice

* **Separarea 1D a Kernel-ului Gaussian** - Kernel-ul Gaussian 2D este descompus în două convoluții 1D consecutive (orizontală și verticală). Această optimizare reduce complexitatea de la O(n^2) la O(2n) pentru fiecare pixel și permite paralelizarea mai eficientă: fiecare rând este independent în pasul orizontal, iar fiecare coloană este independentă în pasul vertical.
* **Schedule Strategy Adaptiv** - `schedule(static)` este folosit pentru operații cu complexitate uniformă (Sobel, NMS), asigurând o distribuție echilibrată a iterațiilor între thread-uri cu overhead minim. `schedule(dynamic)` este aplicat pentru Gaussian Blur unde pot apărea variații mici în timpul execuției, permițând echilibrarea dinamică a sarcinilor.
* **Reduction în Kernel Creation** - Crearea kernel-ului Gaussian folosește `reduction(+:sum)` pentru acumularea sumei în paralel, evitând race conditions și reducând sincronizarea între thread-uri.
* **Eliminarea Recursivității în Hysteresis** - Funcția `trackEdge` recursivă din versiunea serială este înlocuită cu iterații paralele pentru threshold marking. Impactul este redus deoarece fiecare proces MPI procesează doar o bandă locală.


### Gestionarea memoriei

* **Memorie Distribuită (MPI)** - Fiecare proces alocă doar memoria necesară pentru parțiunea primită de imagine (`localImg`), reducând presiunea pe memoria de tip cache și evitând duplicarea întregii imagini pentru fiecare proces.
* **Memorie Partajată (OpenMP)** - Thread-urile din același proces MPI partajează aceeași proțiune de imagine, eliminând necesitatea copierii datelor între thread-uri.

### Algoritm configurabil

Implementarea permite configurarea:
- Numărului de procese MPI
- Numărul de thread-uri OpenMP per proces
- Parametrii algoritmului (thresholds, kernel size, sigma)

Această flexibilitate permite testarea pentru diferite arhitecturi hardware și dimensiuni de imagini.


## Flow-ul final al execuției

**Procesul root:**
- Serial: Citirea imaginii din fișier
- Serial: Încărcarea în structura Image completă
- Paralel MPI: `MPI_Bcast` pentru distribuire dimensiuni (width, height, channels) la toți procesele
- Paralel MPI: `MPI_Scatterv` pentru distribuire rânduri de pixeli fiecarui proces
- Paralel OpenMP: Executa **Canny Edge Detection** pe sub-imaginea locală
- Paralel MPI: `MPI_Gatherv` pentru colectare rezultatelor de la toate procesele
- Serial: Salvarea imaginii complete rezultate în fișier

**Procesele worker:**
- Paralel MPI: Primire dimensiuni cu `MPI_Bcast`
- Paralel MPI: Primire rânduri locale cu `MPI_Scatterv`
- Paralel OpenMP: Executa complet **Canny Edge Detection** pe sub-imaginea locală (conversie grayscale, blur, Sobel, NMS)
- Paralel MPI: Trimitere rezultatelor la procesul root cu `MPI_Gatherv`


## Provocări întâmpinate


1. **Overhead** - Combinarea MPI și OpenMP introduce overhead prin crearea și sincronizarea thread-urilor OpenMP, comunicarea între procesele MPI (funcțiile `Scatter` si `Gather`) și gestionarea memoriei pentru ambele niveluri


2. **Load balancing** - Distribuția neuniformă a calculelor poate apărea la ambele niveluri:

 - **Nivel MPI** unde procesele care primesc benzi cu mai multe muchii pot avea mai mult de calculat în etapa Hysteresis

 - **Nivel OpenMP** unde thread-urile care procesează linii cu variație mare în complexitate pot crea dezechilibre


3. **Sincronizare în etapa de Hysteresis** - Etapa DFS pentru edge tracking este recursivă, fiind dificil de paralelizat eficient. Astfel, s-a paralelizat doar threshold marking (identificarea muchiilor strong sau weak), apoi s-a făcut DFS secvențial de către fiecare proces MPI pe banda locală.



## Profiling & Rezultate obținute



### Eficiență

| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/efficiency.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/xl_efficiency.png) |


Graficul eficienței pentru implementarea hibridă arată un comportament neașteptat în formă de "W" pe măsură ce crește numărul total de unități de procesare. Eficiența pornește de la valoarea ideală pentru configurația serială, dar scade rapid când se adaugă thread-uri OpenMP pe un singur proces MPI. Pentru configurația cu un proces și patru thread-uri, eficiența ajunge la valori destul de scăzute pentru majoritatea imaginilor.

Îmbunătățirea vine când se trece de la configurația cu un singur proces la cele cu două sau mai multe procese MPI. Eficiența crește brusc, acest fenomen sugerând că distribuția datelor între procese diferite elimină anumite bottleneck-uri care apar când prea multe thread-uri încearcă să acceseze simultan memoria partajată pe același nod.

Imaginile mari precum __earth_8k__ și __city__ mențin o eficiență relativ bună pentru configurațiile cu două procese MPI, indiferent de numărul de thread-uri per proces. Totuși, când se crește la patru procese MPI, eficiența scade din nou dramatic, sugerând că overhead-ul de comunicare între procese și sincronizarea thread-urilor devin prea mari.

Imaginea cea mai mică prezintă cea mai drastică scădere a eficienței, ajungând la valori foarte scăzute pentru configurații complexe. Acest comportament indică clar faptul că pentru date de dimensiuni mici, costul de paralelizare hibridă depășește cu mult beneficiile obținute din procesarea paralelă.



### Memorie și memorie cache comparație

| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/memory_cache.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/xl_memory_cache.png) |



Comportamentul memoriei și cache-ului arată caracteristici distincte comparativ cu algoritmul pure MPI sau OpenMP. Numărul de page faults crește în trepte, fiecare treaptă corespunzând adăugării unui nou proces MPI. Când se trece de la un proces la două procese, numărul de page faults face un salt semnificativ, deoarece al doilea proces necesită propriul spațiu de memorie virtuală.

Interesant este că adăugarea de thread-uri OpenMP pe același proces MPI nu crește semnificativ numărul de page faults, deoarece thread-urile partajează memoria procesului. Acest pattern în trepte continuă la patru procese MPI, unde se observă o nouă creștere substanțială. Graficul evidențiază clar diferența fundamentală între memoria distribuită a MPI și memoria partajată a OpenMP.

Branch miss rate-ul are o tendință descrescătoare pe măsură ce cresc unitățile de procesare, dar cu variații nelineare. Scăderea este mai pronunțată pentru configurațiile cu două procese MPI, unde fiecare proces execută bucle mai mici. Totuși, această îmbunătățire în predicția branch-urilor este contrabalansată de creșterea page faults-urilor, creând un trade-off între eficiența cache-ului de instrucțiuni și costul accesului la memorie.

Pentru configurațiile cu patru procese MPI, branch miss rate-ul continuă să scadă dar presiunea pe memoria virtuală crește substanțial.


### Durata execuției etapei


| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/stage_timings.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/xl_stage_timings.png) |



Analiza timpului pe fiecare etapă a algoritmului arată clar unde abordarea MPI cu OpenMP aduce cele mai mari beneficii. Etapa de conversie Grayscale (funcția `toGrayscale`) necesită timp neglijabil indiferent de configurație, fiind o operație simplă care nu beneficiază semnificativ de paralelizare complexă.

Gaussian Blur (funcția `gaussianBlur`) domină timpul total de execuție și prezintă cele mai mari îmbunătățiri. Pentru configurația serială, această etapă consumă majoritatea timpului, dar scade consistent pe măsură ce se adaugă paralelizare. Trecerea de la un proces cu patru thread-uri la două procese cu un thread fiecare produce o reducere substanțială, demonstrând din nou că distribuția MPI este mai eficientă decât simpla creștere a thread-urilor OpenMP pentru operații intensive pe date de dimensiuni mari.

Sobel Gradient (funcția `sobelGradient`) urmează un model similar dar la o scară mai mică, beneficiind de static scheduling-ul uniform. De asemenea, Non-Maximum Suppression (funcția `nonMaxSuppression`) scade constant, dar prezintă o ușoară creștere pentru configurația cu patru procese și patru thread-uri, sugerând că overhead-ul de sincronizare începe să domine la configurații foarte complexe.

Hysteresis (funcția `hysteresisThreshold`) prezintă cea mai mică variație între configurații, timpul rămânând relativ constant. Acest comportament confirmă că DFS-ul secvențial limitează beneficiile paralelizării.


### Speedup


| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/speedup_efficiency.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/xl_speedup_efficiency.png) |



Imaginile mari ating speedup-uri bune pentru configurațiile cu două procese MPI, indiferent de numărul de thread-uri per proces. Aceste imagini beneficiază clar de distribuția datelor între procese, reducând presiunea pe memoria și cache-ul individual.

Imaginile de dimensiune medie prezintă speedup-uri ușor mai mari decât imaginile foarte mari, sugerând un raport mai favorabil între timpul de calcul și overhead-ul de comunicare. Pentru aceste imagini, configurațiile cu două procese și două sau patru thread-uri oferă cele mai bune rezultate, apropiindu-se de speedup-uri de aproape trei ori față de versiunea serială.

Imaginea cea mai mică arată o limitare clară a abordării hibride. Speedup-ul crește moderat pentru configurații simple, dar la configurații complexe cu patru procese și patru thread-uri, performanța scade chiar sub versiunea serială. Overhead-ul de scatter și gather MPI, combinat cu costul creării și sincronizării thread-urilor OpenMP, depășește beneficiile paralelizării pentru volume mici de date.


### Analiză scalabilitate


| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/scaling.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/xl_scaling.png) |



Graficul arată clar limitările fundamentale ale paralelizării hibride pentru acest algoritm, deoarece toate curbele pentru diferite imagini se îndepărtează progresiv de speedup-ul ideal pe măsură ce crește numărul total de unități de procesare.

Pentru configurațiile cu un singur proces MPI, curbele rămân aproape plate, indicând că simpla adăugare de thread-uri OpenMP aduce beneficii minime. Când se introduce distribuția MPI cu două procese, curbele fac un salt, apropiindu-se temporar de linia ideală. Acest salt dramatic demonstrează impactul pozitiv al eliminării bottleneck-ului memoriei partajate.

Zona cu două procese MPI reprezintă regiunea de scalare cea mai bună, unde imaginile mari și medii mențin un speedup rezonabil. Totuși, chiar și aici, distanța față de linia ideală crește constant pe măsură ce se adaugă mai multe thread-uri per proces, reflectând overhead-ul crescător de sincronizare OpenMP.

Comparația între diferite dimensiuni de imagini arată că imaginile mari scalează cel mai bine, menținându-se mai aproape de linia ideală decât imaginile mici. Totuși, chiar și pentru imaginile mari, abaterea de la idealul liniar devine substanțială la configurații complexe, sugerând că limitările algoritmului **Canny Edge Detection**, cu componentele sale secvențiale și overhead-ul de comunicare, previne scalarea eficientă peste de un anumit prag.



### Intel VTune

Pentru a înțelege mai profund comportamentul implementării, au fost efectuate analize detaliate cu Intel VTune pe două configurații reprezentative: configurația cu un proces și patru thread-uri, și configurația cu patru procese și patru thread-uri. Aceste două configurații ilustrează extremele spectrului de paralelizare hibridă.


#### Configurația cu 1 proces si 4 thread-uri

| Summary | Time |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/VTune_P1_T4_summary.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/VTune_P1_T4_time.png) |

Analiza VTune pentru configurația cu un singur proces MPI și patru thread-uri OpenMP arată un profil de execuție dominat de funcțiile computaționale ale algoritmului. Timpul total de execuție se apropie de șase secunde, iar cele mai consumatoare funcții sunt cele legate de Gaussian Blur și calculul gradienților Sobel.

Funcția de Gaussian Blur apare de două ori în lista top hotspots, corespunzând celor două etape separate pentru procesarea orizontală și verticală. Împreună, aceste două funcții ocupă o porțiune semnificativă din timpul total de CPU. Sobel Gradient și Non-Maximum Suppression completează lista funcțiilor intensive, fiecare contribuind cu aproximativ o zecime din timpul total.

Histograma de utilizare a CPU-urilor arată un pattern concentrat la stânga, unde majoritatea timpului este petrecut cu zero până la câteva CPU-uri active simultan. Există un vârf proeminent aproape de origine, indicând că pentru o porțiune semnificativă din execuție, thread-urile nu rulează efectiv simultan, deoarece fie memoria, fie cache împiedică thread-urile să progreseze în paralel.

Paralelismul detectat de VTune este foarte scăzut, doar câteva procente, confirmând că deși sunt patru thread-uri disponibile, ele nu reușesc să lucreze eficient în paralel. Această metrică explică de ce eficiența configurației cu un proces și patru thread-uri este atât de redusă comparativ cu așteptările.



#### Configurația cu 4 procese si 4 thread-uri pentru fiecare proces

| Summary | Time |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/VTune_P4_T4_summary.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/VTune_P4_T4_time.png) |


Profilul VTune arată schimbări dramatice în comportamentul algoritmului, deoarece timpul total de execuție scade la aproximativ 4.5s, dar topul hotspots se modifică semnificativ. Funcția `MPI_Bcast` apare acum pe prima poziție, consumând aproximativ un sfert din timpul total de CPU.

Această dominație a comunicării MPI în profilul de execuție explică de ce configurația cu 4 procese nu reușește să scaleze eficient. În loc ca procesele să petreacă timpul calculând, o porțiune substanțială este dedicată **sincronizării și comunicării dintre procese**. Funcțiile precum Gaussian Blur, Sobel și operațiile matematice rămân în top, dar contribuția lor scade.


Histograma de utilizare a CPU-urilor arată un comportament complet diferit față de configurația cu un proces. Distribuția este mult mai răspândită, cu mai multe bare la valori intermediare și chiar la valori mari de CPU-uri active simultan. Acest pattern indică că algoritmul reușește să utilizeze mai multe nuclee simultan, dar presiunea asupra sistemului crește dramatic.


Paralelismul crește, o îmbunătățire față de configurația cu un proces, dar încă foarte departe de idealul pe care l-ar sugera cele 16 thread-uri. Această metrică subliniază limitările fundamentale ale scalării pentru acest algoritm.


**Concluzie**: ambele configurații prezintă paralelism detectat foarte scăzut, confirmând că nici algoritmul pur OpenMP (întrucât e un singur proces MPI), nici cel cu multe procese MPI nu reprezintă soluții optime pentru acest algoritm. Punctul optim observat în graficele anterioare la configurațiile cu 2 procese se explică prin minimizarea overhead-ului de comunicare MPI.


### Timp de execuție

| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/execution_time.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi_openmp/xl_execution_time.png) |


Pentru imaginile mari, timpul de execuție scade semnificativ când se trece de la configurații cu un singur proces la cele cu două procese MPI. Reducerea este mult mai dramatică decât cea obținută prin simpla creștere a thread-urilor OpenMP pe același proces.

Configurațiile cu un singur proces și mai multe thread-uri produc îmbunătățiri modeste, timpul rămânând relativ aproape de versiunea serială. Acest comportament confirmă că pentru Gaussian Blur și Sobel, operațiile dominante din algoritm, accesul concurent la memoria partajată limitează beneficiile thread-urilor multiple.

Când se introduce distribuția MPI, timpul de execuție scade substanțial. Configurațiile cu două procese MPI și unul sau două thread-uri per proces oferă cei mai buni timpi pentru majoritatea imaginilor. Adăugarea mai multor thread-uri pe fiecare proces aduce îmbunătățiri mici, sugerând că de la un anumit punct, overhead-ul de sincronizare OpenMP întrece beneficiile paralelizării.

Imaginile mici prezintă un comportament complet diferit. Timpii lor de execuție rămân foarte mici, dar pentru configurații complexe cresc chiar peste versiunea serială. Curba ascendentă pentru configurațiile cu patru procese și patru thread-uri demonstrează clar că pentru date de dimensiuni reduse, costul infrastructurii hibride depășește complet orice beneficiu din paralelizare.

Analiza timpilor confirmă că există un punct optim clar pentru fiecare dimensiune de imagine. Pentru **imagini mari**, configurația cu **două procese și două sau patru thread-uri** oferă cel mai bun compromis între viteză și eficiență. Pentru **imagini mici**, **versiunea serială sau configurațiile minimale** sunt de preferat, deoarece overhead-ul nu este justificat pentru imaginile de dimensiuni mici.



#### Imaginea **poza**

| Procese x Threads | Total Time (s) | Speedup | Efficiency (%) | Time Reduction | Page Faults | IPC | Branch Miss % |
|-----------------|---------------|---------|----------------|----------------|-------------|-----|---------------|
| 1 x 1 | 0.10 | 1.00x | 100.0% | 0.0% | 10,075 | 1.89 | 1.80% |
| 1 x 2 | 0.08 | 1.17x | 58.6% | 14.7% | 10,097 | 1.53 | 1.78% |
| 1 x 4 | 0.09 | 1.10x | 27.4% | 8.7% | 10,094 | 1.58 | 2.02% |
| 2 x 1 | 0.06 | 1.57x | 78.7% | 36.4% | 14,761 | 1.47 | 1.88% |
| 2 x 2 | 0.04 | 2.42x | 60.6% | 58.7% | 14,767 | 1.61 | 1.57% |
| 2 x 4 | 0.03 | 2.88x | 36.1% | 65.3% | 14,789 | 1.41 | 1.72% |
| 4 x 1 | 0.04 | 2.32x | 57.9% | 56.8% | 24,401 | 1.41 | 1.68% |
| 4 x 2 | 0.03 | 3.16x | 39.5% | 68.4% | 24,364 | 1.33 | 1.71% |
| 4 x 4 | 0.07 | 1.44x | 9.0% | 30.6% | 24,500 | 1.10 | 1.51% |


#### Imaginea **1_earth_8k**

| Procese x Threads | Total Time (s) | Speedup | Efficiency (%) | Time Reduction | Page Faults | IPC | Branch Miss % |
|-----------------|---------------|---------|----------------|----------------|-------------|-----|---------------|
| 1 x 1 | 4.49 | 1.00x | 100.0% | 0.0% | 21,157 | 2.37 | 2.14% |
| 1 x 2 | 3.93 | 1.14x | 57.2% | 12.6% | 19,606 | 1.69 | 2.15% |
| 1 x 4 | 3.98 | 1.13x | 28.2% | 11.4% | 19,832 | 1.70 | 2.15% |
| 2 x 1 | 2.85 | 1.57x | 78.7% | 36.5% | 27,968 | 2.36 | 2.10% |
| 2 x 2 | 2.44 | 1.84x | 46.0% | 45.7% | 28,884 | 2.33 | 2.11% |
| 2 x 4 | 2.70 | 1.66x | 20.8% | 39.9% | 28,881 | 1.64 | 2.12% |
| 4 x 1 | 2.14 | 2.10x | 52.5% | 52.4% | 48,533 | 2.25 | 2.04% |
| 4 x 2 | 2.21 | 2.03x | 25.4% | 50.7% | 48,512 | 1.65 | 2.06% |
| 4 x 4 | 2.13 | 2.10x | 13.2% | 52.5% | 47,432 | 1.56 | 2.03% |



### Rezumat analiză


| Imagine | Resolution | Speedup Max | Eficiență Max (%) | Timp Min (s) |
|---------|------------|-------------|------------------|---------------|
| 1_earth_8k | 8192x4096 | 2.52x | 31.4% | 1.679s |
| city | 4928x3264 | 2.34x | 29.2% | 0.935s |
| istockphoto-478656454-612x612 | 612x461 | 2.16x | 26.9% | 0.021s |
| pexels-eberhardgross-858115 | 5472x3648 | 2.57x | 32.1% | 0.935s |
| pexels-joey-kyber-31917-134643 | 5851x3901 | 2.59x | 32.4% | 1.032s |
| poza | 612x408 | 2.33x | 29.1% | 0.016s |


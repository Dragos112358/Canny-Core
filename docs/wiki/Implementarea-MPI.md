## Implementare MPI - Inovații & Diferențe

În implementarea algoritmului **Canny Edge Detection** folosind **MPI**, se transformă abordarea de la paralelizare pe un singur proces în distribuirea pe mai multe procese capabile să comunice între ele. Fiecare procesor MPI poate fi pe o mașină fizică diferită, iar comunicația între procese se realizează prin transmiterea de mesaje.

## Modificările realizate

* **Distribuirea imaginii orizontal între procese** - în loc ca fiecare thread să proceseze o bandă orizontală din aceeași memorie, fiecare proces MPI primește o copie proprie a unei benzi orizontale a imaginii. Procesul root (task id 0) încarcă imaginea completă din fișier, apoi folosește `MPI_Scatterv` pentru a distribui rândurile de pixeli către toate celelalte procese.

* **Procesarea identică pe fiecare proces** - fiecare proces execută întregul algoritm **Canny Edge Detection** pe sub-imaginea locală. Pașii sunt identici cu cei din versiunea serială.

* **Conversie grayscale optimizată** - pentru imaginile RGB, se utilizează o buclă optimizată care parcurge toți pixelii și calculează valoarea **grayscale**. Implementarea (din funcția `toGrayscale`) folosește pointeri direct la date(src și dst) și efectuează calcule pe indici pentru evita apeluri de funcție care ar fi costisitoare.

* **Gaussian blur separabil** - **Gaussian blur-ul** este optimizat prin separare, astfel blur-ul 2D este aplicat ca blur 1D orizontal urmat de blur 1D vertical. Kernel-ul 1D este calculat doar o singură dată. Blur-ul separabil este esențial pentru performanță cu kernel-uri mari. În loc de 81 de operații per pixel cu un kernel 9x9, sunt doar 18 operații per pixel (9 orizontal și 9 vertical).


## Flow-ul final al execuției

**Procesul root:**
- Serial: Citirea imaginii din fișier
- Serial: Încărcarea în structura Image completă
- Paralel: `MPI_Bcast` pentru distribuire dimensiuni (width, height, channels) la toți procesele
- Paralel: `MPI_Scatterv` pentru distribuire rânduri de pixeli fiecarui proces
- Paralel: Executa **Canny Edge Detection** pe sub-imaginea locală
- Paralel: `MPI_Gatherv` pentru colectare rezultatelor de la toate procesele
- Serial: Salvarea imaginii complete rezultate în fișier

**Procesele worker:**
- Paralel: Primire dimensiuni cu `MPI_Bcast`
- Paralel: Primire rânduri locale cu `MPI_Scatterv`
- Paralel: Executa complet **Canny Edge Detection** pe sub-imaginea locală (conversie grayscale, blur, Sobel, NMS)
- Paralel: Trimitere rezultatelor la procesul root cu `MPI_Gatherv`


## Provocări întâmpinate

1. **Comunicația între procese** - comunicarea dintre procese (folosind `scatter` și `gather`) are overhead semnificativ. Pentru imagini mici, overhead-ul de comunicație poate fi mai mare decât beneficiul paralelizării. Pentru imagini mari, comunicația devine neglijabilă comparativ cu calculul.

2. **Dependențele la margini** - Pixelii de la marginea superioară a unei sub-imagini au vecini care se află în sub-imaginea procesului anterior. Calculul corect al gradienților pentru acești pixeli necesită comunicație suplimentară între procese. Implementarea actuală ignora aceste dependențe, introduc o pierdere de pixeli la margini, dar pierderea este mică, deci effectul este minim.

## Profiling & Rezultate obținute

### Eficiență

![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/efficiency.png)


Eficiența scade dramatic de la 100% pentru 1 proces la 50-60% pentru 2 procese și continuă să scadă la 30-50% la 8 procese. Imaginile **poza** și **1_earth_8k** au eficiența cea mai scăzută (25-30% pentru 8 procese), sugerând că aceste imagini sunt prea mici sau prea mari pentru a beneficia de paralelizare MPI.

Imaginile de dimensiune medie (**sky**) au eficiență relativ mai bună, deci există o dimensiune a imaginii pentru care algoritmul area cea mai mare eficiență.


### Memorie și memorie cache comparație

#### Imaginea **poza**
![Memory vs cache memory](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/poza_memory_cache.png "poza")

Contrar intuiției, numărul de page faults crește, iar branch miss rate-ul scade ușor pe măsură ce procesele cresc. Rata erorilor de predicție scade, deoarece codul algoritmului Canny rămâne identic pentru fiecare proces, iar procesorul învață mai bine să prezică atunci când se execută ramuri din cod.

Creșterea page faults-urilor apare din cauza unui salt brusc la 2 procese, unde apar 49.000. Acest vârf se întâmplă, deoarece sistemul trebuie să facă tranziția de la **un singur proces** care folosește toată memoria la **doua procese** care trebuie să împartă resursele. După acest spike, numărul de page faults descrește ușor pe măsură ce sistemul se stabilizează și procesele continuă să ruleze normal.

Astfel, Branch prediction rămâne eficient indiferent de numărul de procese, dar schimbările de context și overhead-ul de sincronizare introduc spike-uri în numărul de page faults.


#### Imaginea **1_earth_8k**
![Memory vs cache memory](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/earth_8k_memory_cache.png "1_earth_8k")

Graficul arată că numărul de page faults crește liniar de la aproximativ 23.000 la 140.000, indicând că fiecare proces nou adaugă presiune asupra memoriei virtuale. Această degradare a miss rate-ului (de la 84% la 97%) este normală pentru algoritmii MPI, deoarece pe măsură ce numărul de procese crește, ele ajung să concureze pentru cache-ul partajat, și fiecare proces are o porțiune mai mică din cache disponibil.

La 8 procese, fiecare proces primește aproximativ 1/8 din cache-ul total, ceea ce duce la miss rate-uri mari și latență crescută la memorie. Page faults-urile cresc datorită faptului că stiva și heap-ul fiecărui proces nou ocupă pagini virtuale diferite.

Prin urmare, performanța memoriei se degradează semnificativ cu numărul de procese, ceea ce limitează speedup-ul algoritmului și explică eficiența sub-liniară observată la 8 procese.


### Durata execuției etapei

![Stage timings](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/stage_timings.jpeg)

Graficul arată timpul de execuție al fiecărei etape (Grayscale, Gaussian Blur, Hysteresis) pe măsură ce numărul de procese crește de la 1 la 8. Grayscale și Hysteresis sunt etape mici care rămân relativ constante (200-1600 ms pentru Gaussian Blur și 100-1500 ms pentru Hysteresis). Gaussian Blur domină timpul total, scăzând de la 6000 ms la 1400 ms pentru 8 procese.

Astfel, Gaussian Blur este problema principală a algoritmului, fiind responsabil pentru 80% din timp pentru 1 proces și 60% pentru 8 procese. Paralelizarea acestei etape este critică pentru îmbunătățirea performanței.

### Speedup

![Speedup](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/speedup_efficiency.png)

Graficul compară speedup-ul pe mai multe imagini diferite pe măsură ce numărul de procese crește de la 1 la 8. Imaginile mici au speedup mic, deoarece overhead-ul comunicației MPI are un impact mare. Imaginea Sky are un speedup mai liniar, iar pentru imaginile mari se atinge speedup de 4 la 8 procese.

Este important de precizat faptul că niciun speedup nu depășește 4.5, departe de limita teoretică de 8 pentru cele 8 procese. Aceasta arată că algoritmul este limitat de overhead-ul provenit din comunicația MPI dintre procese și sincronizări. Imaginile mai mari tind să aibă speedup ușor mai bun, deoarece overhead-ul comunicației reprezintă o porțiune mai mică din timpul total.

Astfel, MPI este ineficient pentru imagini mici datorită overhead-ului fix de comunicație.


### Analiză scalabilitate
![Scaling](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/scaling.png)

Graficul compară speedup ideal cu speedup observat pentru mai multe imagini. Nicio imagine nu scalează liniar, iar abaterea de la ideal crește cu numărul de procese. Aceasta confirmă că MPI are un overhead semnificativ.


### Intel VTune

Pentru a înțelege mai bine comportamentul algoritmului MPI, au fost efectuate analize cu Intel VTune pentru două configurații: configurația cu 2 procese și configurația cu 8 procese. Aceste două configurații arată faptul că programul MPI începe să scaleze și respectiv limita practică a scalabilității pentru acest algoritm.

#### Configurația cu 2 procese


| Summary | Time |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/VTune_P2_summary.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/VTune_P2_time.png) |


Analiza arată că Gaussian Blur rămâne funcția dominantă, ocupând aproximativ 20% din timpul total de CPU. `MPI_Bcast` apare în top cu o contribuție semnificativă, indicând că operațiile de broadcast pentru dimensiunile imaginii și parametri introduc o latență considerabilă.

Histograma de utilizare a procesoarelor arată un pattern extrem de concentrat la valori mici, cu un vârf aproape de 0 CPU-uri active simultan. Acest comportament este caracteristic librăriei MPI unde diferitele procese nu rulează neapărat sincron.

Pattern-ul din histogramă relevă o problemă fundamentală a MPI pentru acest algoritm, deoarece deși există potențial pentru paralelism, sincronizarea la operațiile colective creează momente lungi de subutilizare. Fiecare proces lucrează independent pe subimaginea sa, dar la început și final trebuie să se sincronizeze prin funcțiile de `Scatter` și `Gather`, creând bottleneck-uri.


#### Configurația cu 8 procese

| Summary | Time |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/VTune_P8_summary.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/VTune_P8_time.png) |



Pentru configurația cu 8 procese MPI, overhead-ul de comunicare devine dominant. Timpul total de execuție este de aproximativ 12s, reprezentând o îmbunătățire minimă față de varianta cu 2 procese.

`MPI_Bcast` apare acum pe prima poziție în top hotspots, consumând aproximativ 35% din timpul total de procesare. Această dominație a comunicării în profilul de execuție explică perfect de ce eficiența scade atât de dramatic pentru 8 procese. Procesele petrec mai mult timp comunicând și sincronizându-se decât efectuând calculele propriu-zise.

Gaussian Blur, deși încă prezent în top, contribuie doar cu aproximativ 15%. Sobel Gradient și alte funcții completează topul, dar toate sunt eclipsate de overhead-ul MPI. Funcția `ompi_mpi_finalize` apare cu o contribuție notabilă, sugerând că cleanup-ul celor 8 procese introduce și el o latență semnificativă.

Histograma de utilizare a procesorului arată o distribuție ușor mai răspândită decât pentru două procese, cu bare vizibile la valori intermediare. Totuși, vârful rămâne la valori foarte mici. Cele 8 procese lucrează în paralel, dar momentele de comunicare creează perioade lungi unde majoritatea CPU-urilor așteaptă.


### Timp de execuție
![](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/mpi/execution_time.png)

Timpul de execuție pentru imaginile mari, precum **1_earth_8k**, **sky**, scade exponențial de la 10s pentru 1 proces la 2-3s pentru 8 procese, iar pentru cele mici, rămâne constant, indicând că s-a atins un plafon la 2-3 procese.

Astfel, imaginile mari beneficiază mai mult de paralelizarea MPI, dar pentru imaginile mici rămân probleme cu  overhead-ul mare și nu scade semnificativ după 2-3 procese.


#### Imaginea **poza**

| Processes | Total Time (s) | Speedup | Efficiency (%) | Time Reduction | CPUs Utilized | IPC | Branch Miss % |
|---------|---------------|---------|----------------|----------------|---------------|-----|---------------|
| 1 | 0.09 | 1.00x | 100.0% | 0.0% | 0.14 | 2.14 | 1.00% |
| 2 | 0.08 | 1.18x | 59.2% | 15.5% | 0.30 | 1.30 | 1.40% |
| 4 | 0.04 | 2.19x | 54.8% | 54.4% | 0.51 | 1.28 | 1.26% |
| 8 | 0.02 | 3.89x | 48.6% | 74.3% | 1.07 | 1.20 | 1.22% |

#### Imaginea **1_earth_8k**

| Processes | Total Time (s) | Speedup | Efficiency (%) | Time Reduction | CPUs Utilized | IPC | Branch Miss % |
|---------|---------------|---------|----------------|----------------|---------------|-----|---------------|
| 1 | 12.14 | 1.00x | 100.0% | 0.0% | 0.85 | 2.28 | 0.84% |
| 2 | 10.15 | 1.20x | 59.8% | 16.3% | 1.54 | 1.49 | 0.89% |
| 4 | 5.31 | 2.29x | 57.1% | 56.2% | 2.52 | 1.46 | 0.91% |
| 8 | 3.17 | 3.82x | 47.8% | 73.9% | 3.80 | 1.40 | 0.97% |


### Rezumat analiză

| Imagine | Resolution | Speedup Max | Eficiență Max (%) | Timp Min (s) |
|---------|------------|------------|-----------------|-------------|
| 1_earth_8k | 8192x4096 | 3.82x | 100.0% | 3.174s |
| city | 4928x3264 | 4.20x | 100.0% | 1.411s |
| istockphoto-478656454-612x612 | 612x461 | 4.04x | 100.0% | 0.028s |
| pexels-eberhardgross-858115 | 5472x3648 | 4.27x | 100.0% | 1.843s |
| pexels-joey-kyber-31917-134643 | 5851x3901 | 4.01x | 100.0% | 1.970s |
| poza | 612x408 | 3.89x | 100.0% | 0.024s |
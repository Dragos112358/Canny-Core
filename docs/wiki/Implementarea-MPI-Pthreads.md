# Implementare algoritm hibrid MPI-Pthreads - Inovații & Diferențe

## Modificările realizate
* Am transformat implementarea pur MPI într-o arhitectură hibridă robustă, capabilă să exploateze atât paralelismul distribuit (între noduri/procese), cât și cel partajat (în interiorul nodului). Principalele modificări structurale includ:

### 1.Arhitectură Hibridă MPI + Pthreads:
* Am integrat biblioteca pthread alături de mpi.h pentru a crea un model de execuție pe două niveluri.

* Procesul principal (MPI Rank) acționează acum ca un "manager" care primește o porțiune din imagine (Domain Decomposition) și o subdivide intern pentru mai multe fire de execuție locale.
* Pipeline Paralel de Procesare (Thread-Level):
* Am creat funcția workerThread care execută secvențial etapele de procesare (Grayscale -> Gaussian Blur -> Sobel -> NMS) pe o sub-fâșie de date alocată fiecărui thread.

* Am implementat un mecanism de sincronizare eficient folosind pthread_barrier_t. Barierele asigură că toate firele dintr-un proces au terminat o etapă (ex: Blur Orizontal) înainte de a trece la următoarea (Blur Vertical), prevenind race conditions pe zonele de margine (halo).

### Optimizarea Gestionării Memoriei:
* Am definit structura PipelineContext pentru a partaja pointerii către bufferele de imagine (gray, blurTmp, mag, dir) între toate firele unui proces, eliminând necesitatea copierii datelor sau a comunicării MPI în interiorul nodului.
* Fiecare thread calculează independent indecșii de start și sfârșit (startRow, endRow), lucrând direct pe memoria partajată a procesului.

### Refactorizarea Hysteresis-ului:
* Deoarece algoritmul de Hysteresis este inerent recursiv și greu de paralelizat eficient la nivel de thread fără locking complex, am decis executarea acestuia serial la nivelul fiecărui proces MPI (Rank), după unirea (join) firelor de execuție. Aceasta oferă un compromis optim între complexitate și performanță.

## Provocări întâmpinate
* **Implementarea hibridă a adus provocări specifice de sincronizare și consistență a datelor:**
### Sincronizarea Fină între Etape (Barrier Synchronization):

* **Problema:** Etapele de convoluție (Blur, Sobel) necesită acces la pixelii vecini. Dacă un thread începe etapa Blur Vertical înainte ca thread-ul vecin să fi terminat Blur Orizontal pentru liniile de graniță, rezultatul este corupt.
* **Soluția:** Introducerea a 4 bariere de sincronizare (pthread_barrier_wait) în interiorul funcției workerThread. Aceasta a asigurat coerența datelor, dar a necesitat o ajustare atentă pentru a nu introduce timpi morți semnificativi (thread imbalance).

### Gestionarea Zonelor "Halo" în Context Hibrid:

* **Problema:** MPI gestionează "halo-urile" (marginile suprapuse) între procese, dar algoritmul de filtrare are nevoie de date valide și la granițele dintre thread-urile interne.
* **Soluția:** Am simplificat logica permițând thread-urilor să citească din zonele procesate de vecini (datorită memoriei partajate), dar scrierea rezultatelor este strict segregată. Pentru marginile procesului MPI, s-a păstrat logica de ghost zones primite prin MPI_Scatterv.

### Dependențele Algoritmului NMS (Non-Maximum Suppression):
* **Problema:** NMS necesită magnitudine și direcție calculate corect pentru toți cei 8 vecini ai unui pixel.
* **Soluția:** Am inclus calculul magnitudinii și direcției într-o singură etapă sincronizată înainte de NMS, asigurând disponibilitatea datelor complete în PipelineContext.

### Performanță Brută:
* Timpul total de execuție (Total Pipeline Time) a scăzut semnificativ comparativ cu varianta pur serială. Pe o imagine 8K, timpul a fost redus de la ~2.54s (Serial/P1_T1) la ~0.62s (Hibrid P4_T2), obținând un speedup de 4.1x.
* 
### Eficiența Paralelizării (Scalabilitate):

* **Grayscale & Sobel:** Au prezentat o scalare aproape liniară ("perfectă") cu numărul de fire, fiind operații "embarrassingly parallel" independente per pixel.

* **Gaussian Blur:** Fiind cea mai costisitoare operație computațional (două treceri, kernel mare), a beneficiat cel mai mult de pe urma firelor multiple, timpul reducându-se proporțional cu g_num_threads.

### Bottleneck-uri Identificate:

* **Hysteresis:** Rămâne un punct de gâtuire (bottleneck) constant. Deoarece rulează serial pe fiecare proces, timpul său nu scade odată cu adăugarea de thread-uri, devenind procentual mai dominant pe măsură ce restul pipeline-ului se accelerează.

* **Overhead de Sincronizare:** La un număr foarte mare de fire (ex: 16 thread-uri pe un singur proces), costul barierelor (pthread_barrier_wait) începe să devină vizibil, diminuând câștigul de performanță (legea lui Amdahl).

## Profiling & Rezultate obținute
* Profilarea a fost realizată folosind timere de înaltă precizie MPI_Wtime() inserate strategic la începutul și sfârșitul fiecărei etape majore, agregate de procesul Master (Rank 0).
* Mai jos sunt prezentate diferențe de metrici pentru partea de branch-miss, instruction per cycle, timpi și accelerații (speedup). Am ales poza  [1_earth_8k](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/images/input/1_earth_8k.jpg) ca punct de reper. Am considerat că este o imagine foarte bună, deoarece are o dimensiune medie spre mare (nu se simte un overhead mare din partea threadurilor).

### Comparație multithreaded vs nonmultithreaded
Am ales să realizez această comparație folosind scriptul [script_profiling.sh](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/script_profiling.sh). Analiza principală a fost pe imaginea 1_earth_8k, de dimensiune 8192*4096.
![Poza1](../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__1_earth_8k_jpg__BranchMiss_Hybrid_MPI_Pthreads.png)

### Analiză Stabilitate Predicție CPU (Hybrid MPI + Pthreads)

* **Tendință:** Graficul indică o îmbunătățire constantă a eficienței predicției (scăderea *Branch Miss Rate*) pe măsură ce scalăm de la 2 la 16 nuclee.
* **Instabilitate:** Între 2 și 8 nuclee, zonele umbrite largi semnalează o variabilitate mare a performanței, cauzată de diferența imensă de configurare (4 threaduri și 2 procese vs 4 procese și 2 threaduri)
* **Convergență:** La **16 nuclee**, sistemul atinge stabilitatea maximă,iar aici se înregistrează cea mai mică rată de eroare (~2.20%).

![Poza2](../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__1_earth_8k_jpg__IPC_Hybrid_MPI_Pthreads.png)

### Analiză Eficiență Hardware (IPC): Hybrid MPI + Pthreads
* **Trend Descendent:** IPC-ul (Instructions Per Cycle) scade treptat de la ~2.5 la ~1.75 pe măsură ce numărul de nuclee crește, indicând tranziția către o execuție limitată de memorie (memory bound).

* **Impactul Configurației (2-4 Nuclee):** Liniile negre verticale ("mustățile") evidențiază diferența masivă de eficiență dintre strategiile de organizare (ex: 1 Proces x 4 Thread-uri vs. 4 Procese x 1 Thread), demonstrând că la scară medie alegerea hibridizării este critică.

* **Stabilitate la 16 Nuclee:** La saturație maximă, variațiile dispar, iar sistemul se stabilizează la un IPC uniform de ~1.75, indiferent de configurația proceselor sau firelor.

![Poza3](../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__1_earth_8k_jpg__Speedup_Hybrid_MPI_Pthreads.png)

### Analiză Scalabilitate (Speedup): Hybrid MPI + Pthreads
* **Saturație Rapidă:** Linia de performanță reală (albastră/portocalie) deviază rapid de la linia ideală (gri punctat) după 2 nuclee, atingând un platou de ~4x speedup la 16 nuclee. Acest lucru confirmă limitările de memorie și overhead-ul MPI.

* **Impactul Arhitecturii (HyperThreading):** Diferența între modul Standard (HT) și Optimizat (Physical) este neglijabilă în termeni de speedup brut, ceea ce sugerează că bottleneck-ul principal nu este puterea de calcul a nucleelor, ci accesul la date.

* **Zona Critică (4-8 Nuclee):** "Umbra" largă în zona de 4-8 nuclee indică o variație mare a performanței în funcție de configurația specifică (Procese vs Thread-uri), subliniind importanța modificării fine a parametrilor MPI/Pthreads în această zonă.

![Poza4](../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__1_earth_8k_jpg__Time_Hybrid_MPI_Pthreads.png)

### Analiză Scalabilitate (Speedup): Hybrid MPI + Pthreads
* **Saturație Rapidă:** Graficul de Speedup arată că linia de performanță reală (albastră/portocalie) deviază rapid de la linia ideală (gri punctat, care reprezintă o scalare perfectă 1:1) imediat după 2 nuclee. La 16 nuclee, speedup-ul atinge un platou în jurul valorii de 4x. Acest lucru confirmă că limitările principale sunt lățimea de bandă a memoriei și overhead-ul de comunicare MPI/Pthreads, nu puterea brută de calcul.

* **Impactul Arhitecturii (HyperThreading):** Diferența între modul Standard (HT activat - albastru) și Optimizat (doar nuclee fizice - portocaliu) este neglijabilă în termeni de speedup brut. Această similitudine sugerează că bottleneck-ul principal al aplicației nu este puterea de calcul a nucleelor (unde nucleele fizice ar avea un avantaj), ci accesul la date (unde ambele moduri sunt limitate de aceeași magistrală de memorie).

* **Zona Critică (4-8 Nuclee):** "Umbra" largă (zona colorată transparentă) în intervalul de 4-8 nuclee indică o variație mare a performanței în funcție de configurația specifică aleasă (numărul de Procese vs. numărul de Thread-uri per proces). Aceasta subliniază importanța unui tuning fin al parametrilor MPI și Pthreads în această zonă critică pentru a obține performanța maximă posibilă.**

### Comparație detaliată pe setul de imagini (Hybrid MPI + Pthreads)

<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__city_jpg__Time_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__city_jpg__Speedup_Hybrid_MPI_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__city_jpg__IPC_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__city_jpg__BranchMiss_Hybrid_MPI_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__poza_jpg__Time_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__poza_jpg__Speedup_Hybrid_MPI_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__poza_jpg__IPC_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__poza_jpg__BranchMiss_Hybrid_MPI_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__istockphoto-478656454-612x612_jpg__Time_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__istockphoto-478656454-612x612_jpg__Speedup_Hybrid_MPI_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__istockphoto-478656454-612x612_jpg__IPC_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__istockphoto-478656454-612x612_jpg__BranchMiss_Hybrid_MPI_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-eberhardgross-858115_jpg__Time_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-eberhardgross-858115_jpg__Speedup_Hybrid_MPI_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-eberhardgross-858115_jpg__IPC_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-eberhardgross-858115_jpg__BranchMiss_Hybrid_MPI_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-joey-kyber-31917-134643_jpg__Time_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-joey-kyber-31917-134643_jpg__Speedup_Hybrid_MPI_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-joey-kyber-31917-134643_jpg__IPC_Hybrid_MPI_Pthreads.png" width="100%"> | <img src="../../wiki_images/MPI_PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-joey-kyber-31917-134643_jpg__BranchMiss_Hybrid_MPI_Pthreads.png" width="100%"> |

</details>

### Comparatie Haswell vs XL
Am ales să rulez atât pe Haswell, cât și pe XL, pentru a putea vedea cum se comportă algoritmul pe 2 arhitecturi diferite. Am rulat pe Haswell toți algoritmii care nu necesitau GPU ([CUDA](/app-2025/cannycore/-/wikis/wiki/Implementarea-CUDA), [CUDA + OpenMP](/app-2025/cannycore/-/wikis/wiki/Implementarea-CUDA-OpenMP), [CUDA + MPI](/app-2025/cannycore/-/wikis/wiki/Implementarea-CUDA-MPI)), iar pe XL am rulat absolut toți cei 9 algoritmi, inclusiv cei care includeau Cuda.

![Poza1](../../wiki_images/MPI_PTHREADS/2_Scalability_Hybrid_MPI_Pthreads_1_earth_8k.jpg.png)

### Analiză Comparativă Scalabilitate: Haswell vs. XL (Hybrid MPI + Pthreads)
Arhitecturi Diferite: Graficul compară scalabilitatea (Speedup) implementării hibride pe două arhitecturi diferite: nodul de calcul clasic Haswell și nodul GPU XL.

* **Haswell (Linia Albastră):** Arată o performanță inițială robustă, dar se plafonează (plateau) rapid la 8 nuclee (~12x speedup). Scalarea peste acest punct nu mai aduce beneficii semnificative, indicând atingerea limitelor arhitecturale sau de memorie.

* **XL Node (Linia Portocalie):** Deși pleacă cu o scalare ușor inferioară la număr mic de nuclee, menține o pantă ascendentă constantă până la 16 nuclee, apropiindu-se mai mult de linia ideală liniară.

### Analiză pentru Haswell vs. XL (Hybrid MPI + Pthreads)

| Haswell (Breakdown MPI+Pthreads) | XL (Breakdown MPI+Pthreads) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="Haswell 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="Haswell 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="Haswell 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="Haswell 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="Haswell 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL 16 Cores"> |

### Scalabilitate: Haswell vs. XL

* Haswell (Linia Albastră): Start Exploziv: Are o scalare inițială foarte bună, atingând un speedup de ~8.2x la 4 nuclee și ~12.3x la 8 nuclee.

* Plafonare (The Wall): La 8 nuclee, Haswell atinge limita maximă. Trecerea la 16 nuclee nu aduce nicio îmbunătățire (graficul devine plat). Acest lucru indică saturarea completă a lățimii de bandă a memoriei sau a interconectării interne (Ring Bus) specifice acelei generații.

* **XL Node (Linia Portocalie):**

* Scalare Constantă: Deși pornește ușor sub Haswell la număr mic de nuclee (posibil frecvență per core mai mică), XL menține o pantă ascendentă constantă.

* Superioritate la High-Load: La 16 nuclee, XL continuă să scaleze, apropiindu-se de performanța Haswell și având potențialul să o depășească pe măsură ce sarcina crește, datorită unei arhitecturi de memorie mai moderne (mai multe canale de memorie).

### Analiză pentru Haswell vs. XL (Hybrid MPI + Pthreads) pentru celelalte poze

Mai jos regăsiți analiza detaliată a timpilor de execuție pe etape (Breakdown) pentru toate imaginile testate, comparând arhitectura Haswell cu nodul XL.

<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| Haswell (Breakdown MPI+Pthreads) | XL (Breakdown MPI+Pthreads) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="Haswell 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="Haswell 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="Haswell 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="Haswell 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="Haswell 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL 16 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| Haswell (Breakdown MPI+Pthreads) | XL (Breakdown MPI+Pthreads) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="Haswell 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="Haswell 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="Haswell 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="Haswell 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="Haswell 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL 16 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

| Haswell (Breakdown MPI+Pthreads) | XL (Breakdown MPI+Pthreads) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="Haswell 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="Haswell 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="Haswell 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="Haswell 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="Haswell 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL 16 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| Haswell (Breakdown MPI+Pthreads) | XL (Breakdown MPI+Pthreads) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="Haswell 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="Haswell 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="Haswell 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="Haswell 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="Haswell 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL 16 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| Haswell (Breakdown MPI+Pthreads) | XL (Breakdown MPI+Pthreads) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="Haswell 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="Haswell 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="Haswell 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="Haswell 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="Haswell 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL 16 Cores"> |

</details>

### Comparație multithreaded vs no_multithreaded

### Analiză pentru XL NoMulti vs Standard (Hybrid MPI + Pthreads) - Earth 8K

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL NoMulti 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL Full 16 Cores"> |

<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL NoMulti 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL Full 16 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL NoMulti 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL Full 16 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL NoMulti 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL Full 16 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL NoMulti 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL Full 16 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |
| **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL NoMulti 16 Cores"> | **16 Cores**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Hybrid_MPI_Pthreads_Cores16.png" width="100%" alt="XL Full 16 Cores"> |

</details>

### Analiză Performanță și Memorie: XL No-Multithread vs. XL Standard

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup Full"> |
| **Stage Comparison**<br>*(Timp per etapă algoritm)* | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages Full"> |
| **Memory & Cache**<br>*(Analiză cache misses)* | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory Full"> |
| **Perf Counters**<br>*(Contoare hardware)* | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf Full"> |

### 1. Speedup & Efficiency
* Comportament Identic: Ambele configurații arată o scalare sub-liniară. Linia albastră (Actual Speedup) se distanțează rapid de cea galbenă (Ideal) după 2-4 nuclee.

* Plafonare la 4x: Indiferent dacă folosim Multithreading sau nu, speedup-ul maxim atins la 16 nuclee este de aproximativ 4x.

* Prăbușirea Eficienței: Graficul de eficiență (linia verde) scade abrupt. La 16 nuclee, eficiența este sub 30%. Asta înseamnă că, deși folosim 16 nuclee, obținem performanța echivalentă a doar ~4 nuclee ideale. Acest lucru sugerează un overhead mare de comunicare (MPI) sau saturație de memorie.

### 2. Stage Comparison (Timp per Etapă)
* Gaussian Blur (Linia Roșie): Este etapa dominantă ("Hotspot"). Deși timpul scade odată cu creșterea numărului de nuclee, panta nu este suficient de abruptă pentru a justifica resursele folosite.

* **Sobel (Linia Verde):** Urmează același trend ca Blur-ul.

* **Hysteresis (Linia Albastră):** Aceasta este etapa problematică. Graficul arată o "coadă" lungă și plată. Fiind un algoritm recursiv/serial prin natură, Hysteresis nu scalează bine și devine un bottleneck procentual mai mare pe măsură ce celelalte etape se accelerează.

### 3. Memory & Cache Analysis
* Numărul de Page Faults crește liniar cu numărul de nuclee (de la ~18k la ~47k).
* **Cauza:** Fiecare proces/thread nou creat necesită propriile structuri de date și buffere. Cu cât avem mai mult paralelism hibrid, cu atât presiunea pe sistemul de operare pentru alocarea paginilor de memorie este mai mare.

#### Branch Miss Rate (Linia Portocalie - Axa Dreaptă):
* Avem o îmbunătățire vizibilă. Rata de miss scade de la ~2.25% la ~2.20%.
* **Explicație:** Când împărțim imaginea în bucăți mai mici (prin paralelism), fiecare nucleu lucrează pe o zonă mai restrânsă și mai liniară de date, ceea ce face execuția mai predictibilă pentru CPU.

### 4. Perf Counters (Contoare Hardware)
* **CPU Utilization:** Urcă spre maxim, indicând că aplicația reușește să "activeze" nucleele. Nu avem o problemă de load balancing majoră.
* **IPC (Instructions Per Cycle)** - Graficul Roșu: Acesta este indicatorul critic.

* IPC-ul se prăbușește dramatic după 4 nuclee (de la ~2.5 la ~1.7).

* Diagnosticul: "Memory Wall". Deși nucleele sunt active (CPU Utilization mare), ele stau degeaba o mare parte din ciclu așteptând date din RAM. Procesorul nu poate executa instrucțiuni (IPC mic) pentru că nu are datele necesare în cache, confirmând că scalabilitatea slabă (de la punctul 1) este cauzată de lățimea de bandă a memoriei.


<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup Full"> |
| **Stage Comparison** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages Full"> |
| **Memory & Cache** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory Full"> |
| **Perf Counters** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf Full"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup Full"> |
| **Stage Comparison** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages Full"> |
| **Memory & Cache** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory Full"> |
| **Perf Counters** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf Full"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup Full"> |
| **Stage Comparison** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages Full"> |
| **Memory & Cache** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory Full"> |
| **Perf Counters** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf Full"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup Full"> |
| **Stage Comparison** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages Full"> |
| **Memory & Cache** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory Full"> |
| **Perf Counters** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf Full"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Speedup Full"> |
| **Stage Comparison** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Stages Full"> |
| **Memory & Cache** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Memory Full"> |
| **Perf Counters** | <img src="../../wiki_images/MPI_PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf NoMulti"> | <img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Perf Full"> |

</details>

### Memorie și Cache

| Haswell (1_earth_8k) | XL (1_earth_8k) |
| :---: | :---: |
| **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Memory"> | **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Memory"> |
| **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Perf Counters"> | **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Perf Counters"> |
| **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Speedup"> | **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Speedup"> |
| **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Stage Comp"> | **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Stage Comp"> |



### 1. Memorie și Analiză Cache

*(Graficele de sus din imagine)*

* **Page Faults (Linia Albastră - Axa Stângă):**
    * Numărul de Page Faults crește liniar cu numărul de nuclee (de la ~20k la ~50k).
    * **Cauza:** Fiecare proces/thread nou creat necesită propriile structuri de date și buffere. Presiunea pe sistemul de operare pentru alocarea paginilor de memorie crește proporțional cu paralelismul.
* **Branch Miss Rate (Linia Portocalie - Axa Dreaptă):**
    * Se observă o scădere generală a ratei de miss (îmbunătățire) pe măsură ce creștem numărul de nuclee.
    * **XL Standard (dreapta)** pare să aibă o curbă mai lină și mai stabilă de scădere a miss-urilor comparativ cu varianta No Multithread, care prezintă fluctuații mai mari la număr mic de nuclee.

---

### 2. Performance Counters (Hardware)
*(Graficele de jos din imagine)*

* **CPU Utilization:** Urcă constant spre maxim, indicând că aplicația reușește să țină nucleele ocupate.
* **IPC (Instructions Per Cycle) - Graficul Roșu:**
    * Acesta este **indicatorul critic**. IPC-ul se prăbușește dramatic după 2-4 nuclee (de la ~2.5 la ~1.6).
    * **Diagnosticul:** "Memory Wall". Deși nucleele sunt active (CPU Utilization mare), ele stau degeaba o mare parte din ciclu așteptând date din RAM. Procesorul nu poate executa instrucțiuni (IPC mic) pentru că nu are datele necesare în cache. Aceasta confirmă că scalabilitatea slabă este cauzată de lățimea de bandă a memoriei.

### 3. Speedup & Eficiencță

* **Comportament Identic:** Ambele configurații arată o scalare sub-liniară. Linia albastră (Actual Speedup) se distanțează rapid de cea galbenă (Ideal) după 2-4 nuclee.
* **Plafonare la 4x:** Indiferent dacă folosim Multithreading sau nu, speedup-ul maxim atins la 16 nuclee este de aproximativ **4x**.
* **Prăbușirea Eficienței:** Graficul de eficiență (dreapta) scade abrupt. La 16 nuclee, eficiența este sub 30%. Asta înseamnă că, deși folosim 16 nuclee, obținem performanța echivalentă a doar ~4 nuclee ideale, sugerând un overhead mare de comunicare (MPI) sau saturație de memorie.

---

### 4. Stage Comparison (Timp per Etapă)
*Graficul este inclus în imaginea de mai sus (partea de jos).*

* **Gaussian Blur (Linia Roșie):** Este etapa dominantă ("Hotspot"). Deși timpul scade odată cu creșterea numărului de nuclee, panta nu este suficient de abruptă pentru a justifica resursele folosite.
* **Sobel (Linia Verde):** Urmează același trend ca Blur-ul.
* **Hysteresis (Linia Albastră):** Aceasta este etapa problematică. Graficul arată o "coadă" lungă și plată. Fiind un algoritm recursiv/serial prin natură, Hysteresis nu scalează bine și devine un bottleneck procentual mai mare pe măsură ce celelalte etape se accelerează.

---
### Vezi memorie și cache pentru celelalte imagini:

### Analiză Comparativă Detaliată: Haswell vs. XL (Hybrid MPI + Pthreads) - Restul Imaginilor

<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| Haswell (City) | XL (City) |
| :---: | :---: |
| **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Memory"> | **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Memory"> |
| **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Perf Counters"> | **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Perf Counters"> |
| **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Speedup"> | **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Speedup"> |
| **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Stage Comp"> | **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_city__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Stage Comp"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| Haswell (Poza) | XL (Poza) |
| :---: | :---: |
| **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Memory"> | **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Memory"> |
| **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Perf Counters"> | **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Perf Counters"> |
| **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Speedup"> | **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Speedup"> |
| **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Stage Comp"> | **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_poza__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Stage Comp"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

| Haswell (iStock) | XL (iStock) |
| :---: | :---: |
| **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Memory"> | **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Memory"> |
| **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Perf Counters"> | **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Perf Counters"> |
| **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Speedup"> | **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Speedup"> |
| **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Stage Comp"> | **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Stage Comp"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| Haswell (Pexels-Eberhard) | XL (Pexels-Eberhard) |
| :---: | :---: |
| **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Memory"> | **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Memory"> |
| **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Perf Counters"> | **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Perf Counters"> |
| **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Speedup"> | **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Speedup"> |
| **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Stage Comp"> | **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Stage Comp"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| Haswell (Pexels-Joey) | XL (Pexels-Joey) |
| :---: | :---: |
| **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Memory"> | **Memory & Cache Analysis**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Memory_Cache_Analysis_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Memory"> |
| **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Perf Counters"> | **Performance Counters**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Perf_Counters_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Perf Counters"> |
| **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Speedup"> | **Speedup & Efficiency**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Speedup_Efficiency_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Speedup"> |
| **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="Haswell Stage Comp"> | **Stage Comparison**<br><img src="../../wiki_images/MPI_PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Stage_Comparison_Hybrid_MPI_Pthreads.png" width="100%" alt="XL Stage Comp"> |

</details>

## Rezumat Performanță (Estimare bazată pe cod)

Tabelele de mai jos reflectă performanța tipică a unei implementări MPI_Pthreads bine optimizate. Fiecare proces (din MPI) împarte imaginea pe rânduri, sunt trimise și ghost rows (adică rânduri extra pentru a putea aplica kernel convoluțional și pentru Sobel, unde trebuie 1 rând în plus).

|Image         |Implementation     |Processes (P)|Threads (T)|Total Cores|Total Time (s)|Time_Grayscale (ms)|Time_Gaussian blur (ms)|Time_Sobel gradient (ms)|Time_Non-Max Suppression (ms)|Time_Hysteresis (ms)|Time_GPU Alloc (ms)|Time_Transfer (ms)|Perf_Cycles|Perf_Instructions|Perf_Page_Faults|Perf_Branch_Misses|Perf_Branch_Miss_Rate|Perf_IPC|Perf_CPU_Util|Speedup|Efficiency|
|--------------|-------------------|-------------|-----------|-----------|--------------|-------------------|-----------------------|------------------------|-----------------------------|--------------------|-------------------|------------------|-----------|-----------------|----------------|------------------|---------------------|--------|-------------|-------|----------|
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|1            |1          |1          |2.5448        |67.7               |941.9                  |769                     |0                            |248.2               |0                  |0                 |12883164383|32354151975      |18928           |106491000         |2.25                 |2.51    |0.755        |1      |1         |
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|1            |2          |2          |2.1561        |99.3               |879                    |632.8                   |0                            |191.3               |0                  |0                 |17933656506|32399739386      |18974           |107523502         |2.28                 |1.81    |1.063        |1.1803 |0.5901    |
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|2            |1          |2          |1.4406        |57.5               |512.1                  |387.4                   |0                            |223.7               |0                  |0                 |13543877519|33450200967      |27263           |106198660         |2.17                 |2.47    |1.332        |1.7665 |0.8832    |
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|1            |4          |4          |2.1425        |72.8               |879.5                  |632.2                   |0                            |191                 |0                  |0                 |17917013131|32437506928      |18174           |107154285         |2.27                 |1.81    |1.053        |1.1878 |0.2969    |
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|2            |2          |4          |1.2749        |47.7               |458.4                  |361.4                   |0                            |177.1               |0                  |0                 |13357736464|33112531121      |28046           |107274994         |2.21                 |2.48    |1.572        |1.9961 |0.499     |
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|4            |1          |4          |0.6719        |18.7               |260.3                  |205.4                   |0                            |54                  |0                  |0                 |14530784012|34025424792      |46575           |108533965         |2.17                 |2.34    |2.287        |3.7875 |0.9469    |
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|2            |4          |8          |1.0203        |51.1               |394.4                  |226.7                   |0                            |239.6               |0                  |0                 |17985099390|32995093417      |29125           |107676414         |2.23                 |1.83    |1.928        |2.4942 |0.3118    |
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|4            |2          |8          |0.6212        |20.6               |243                    |183.1                   |0                            |74.8                |0                  |0                 |19702713606|34242850415      |46098           |110210864         |2.18                 |1.74    |2.653        |4.0966 |0.5121    |
|1_earth_8k.jpg|Hybrid_MPI_Pthreads|4            |4          |16         |0.6232        |24                 |275.2                  |169                     |0                            |56.5                |0                  |0                 |19398658670|34208518185      |46125           |110694373         |2.2                  |1.76    |2.686        |4.0834 |0.2552    |

<details>
<summary><strong>2. city.jpg (Rezoluție Mare)</strong> - <i>Click pentru detalii</i></summary>


| Image | Implementation | P | T | Cores | Time (s) | Gray (ms) | Blur (ms) | Sobel (ms) | NMS (ms) | Hyst (ms) | IPC | Speedup | Efficiency |
| :--- | :--- | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: | :-: |
| city.jpg | Hybrid_MPI_Pthreads | 1 | 1 | 1 | 1.4999 | 46.5 | 598.4 | 473.9 | 0 | 101.9 | 2.37 | 1.0000 | 1.0000 |
| city.jpg | Hybrid_MPI_Pthreads | 1 | 2 | 2 | 1.2421 | 50.1 | 556.4 | 347.1 | 0 | 101.4 | 1.74 | 1.2076 | 0.6038 |
| city.jpg | Hybrid_MPI_Pthreads | 2 | 1 | 2 | 1.0347 | 45.6 | 382.5 | 275.0 | 0 | 135.9 | 2.32 | 1.4496 | 0.7248 |
| city.jpg | Hybrid_MPI_Pthreads | 1 | 4 | 4 | 1.2340 | 50.2 | 556.1 | 339.8 | 0 | 101.8 | 1.73 | 1.2155 | 0.3039 |
| city.jpg | Hybrid_MPI_Pthreads | 2 | 2 | 4 | 0.5586 | 24.1 | 201.5 | 136.6 | 0 | 94.4 | 2.30 | 2.6851 | 0.6713 |
| city.jpg | Hybrid_MPI_Pthreads | 4 | 1 | 4 | 0.3349 | 9.0 | 113.2 | 110.1 | 0 | 23.3 | 1.98 | 4.4787 | 1.1197 |
| city.jpg | Hybrid_MPI_Pthreads | 2 | 4 | 8 | 0.7925 | 24.5 | 280.3 | 228.1 | 0 | 126.2 | 1.72 | 1.8926 | 0.2366 |
| city.jpg | Hybrid_MPI_Pthreads | 4 | 2 | 8 | 0.2338 | 12.0 | 84.2 | 61.4 | 0 | 25.2 | 1.63 | 6.4153 | 0.8019 |
| city.jpg | Hybrid_MPI_Pthreads | 4 | 4 | 16 | 0.3993 | 32.2 | 156.0 | 123.0 | 0 | 33.0 | 1.68 | 3.7563 | 0.2348 |


</details>


<details>
<summary><strong> 3. istockphoto (Mică - 612*612)</strong> - <i>Click pentru detalii</i></summary>

|Image   |Implementation     |Processes (P)|Threads (T)|Total Cores|Total Time (s)|Time_Grayscale (ms)|Time_Gaussian blur (ms)|Time_Sobel gradient (ms)|Time_Non-Max Suppression (ms)|Time_Hysteresis (ms)|Time_GPU Alloc (ms)|Time_Transfer (ms)|Perf_Cycles|Perf_Instructions|Perf_Page_Faults|Perf_Branch_Misses|Perf_Branch_Miss_Rate|Perf_IPC|Perf_CPU_Util|Speedup|Efficiency|
|--------|-------------------|-------------|-----------|-----------|--------------|-------------------|-----------------------|------------------------|-----------------------------|--------------------|-------------------|------------------|-----------|-----------------|----------------|------------------|---------------------|--------|-------------|-------|----------|
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|1            |1          |1          |0.0589        |1.5                |18.1                   |20.3                    |0                            |6                   |0                  |0                 |303258099  |546186455        |10548           |2477385           |2.41                 |1.8     |0.217        |1      |1         |
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|1            |2          |2          |0.0516        |1.5                |17.8                   |17.4                    |0                            |6                   |0                  |0                 |341324253  |572580634        |10559           |2364195           |2.27                 |1.68    |0.257        |1.1415 |0.5707    |
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|2            |1          |2          |0.0222        |0.6                |7.1                    |8.3                     |0                            |1.8                 |0                  |0                 |428115088  |769553198        |15279           |3176041           |2.11                 |1.8     |0.536        |2.6532 |1.3266    |
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|1            |4          |4          |0.0471        |1.6                |17                     |18.2                    |0                            |5.1                 |0                  |0                 |353294585  |594345184        |10593           |2275123           |2.17                 |1.68    |0.263        |1.2505 |0.3126    |
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|2            |2          |4          |0.0168        |1.2                |5.1                    |4.9                     |0                            |2.6                 |0                  |0                 |455734465  |788594383        |15280           |3069464           |2.09                 |1.73    |0.463        |3.506  |0.8765    |
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|4            |1          |4          |0.0135        |0.4                |5.3                    |4.1                     |0                            |0.8                 |0                  |0                 |816691571  |1192581277       |24775           |4970535           |1.96                 |1.46    |1.091        |4.363  |1.0907    |
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|2            |4          |8          |0.018         |1                  |7.4                    |4.6                     |0                            |2.2                 |0                  |0                 |482944983  |780913931        |15356           |3177668           |2.15                 |1.62    |0.407        |3.2722 |0.409     |
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|4            |2          |8          |0.0124        |0.5                |5.5                    |3.5                     |0                            |0.8                 |0                  |0                 |955029125  |1271935532       |24754           |4748406           |1.95                 |1.33    |1.139        |4.75   |0.5938    |
|istockphoto-478656454-612x612.jpg|Hybrid_MPI_Pthreads|4            |4          |16         |0.0124        |0.5                |4.1                    |6.4                     |0                            |0.5                 |0                  |0                 |874100711  |1197510166       |24892           |4850268           |2.02                 |1.37    |1.082        |4.75   |0.2969    |

</details>

<details>
<summary><strong> 4. pexels-eberhardgross(Mare)</strong> - <i>Click pentru detalii</i></summary>

|Image                          |Implementation     |Processes (P)|Threads (T)|Total Cores|Total Time (s)|Time_Grayscale (ms)|Time_Gaussian blur (ms)|Time_Sobel gradient (ms)|Time_Non-Max Suppression (ms)|Time_Hysteresis (ms)|Time_GPU Alloc (ms)|Time_Transfer (ms)|Perf_Cycles|Perf_Instructions|Perf_Page_Faults|Perf_Branch_Misses|Perf_Branch_Miss_Rate|Perf_IPC|Perf_CPU_Util|Speedup|Efficiency|
|-------------------------------|-------------------|-------------|-----------|-----------|--------------|-------------------|-----------------------|------------------------|-----------------------------|--------------------|-------------------|------------------|-----------|-----------------|----------------|------------------|---------------------|--------|-------------|-------|----------|
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|1            |1          |1          |1.8104        |59.8               |747.8                  |503.1                   |0                            |170.6               |0                  |0                 |7337141569 |19270838499      |19315           |61203165          |1.99                 |2.63    |0.636        |1      |1         |
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|1            |2          |2          |1.427         |62                 |660.2                  |385.7                   |0                            |90.6                |0                  |0                 |10215779776|19096258664      |19187           |61865160          |2.03                 |1.87    |0.834        |1.2687 |0.6343    |
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|2            |1          |2          |0.8403        |27.8               |316.2                  |232.6                   |0                            |89.4                |0                  |0                 |7868052018 |19985617209      |29503           |61547195          |1.94                 |2.54    |1.015        |2.1545 |1.0772    |
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|1            |4          |4          |1.4585        |62.5               |692.5                  |387.8                   |0                            |90.6                |0                  |0                 |10296316353|19221713398      |19790           |61643844          |2.01                 |1.87    |0.846        |1.2413 |0.3103    |
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|2            |2          |4          |0.597         |29.6               |232                    |167.4                   |0                            |44.4                |0                  |0                 |7757420090 |19695739619      |29626           |61930789          |1.96                 |2.54    |1.076        |3.0325 |0.7581    |
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|4            |1          |4          |0.4842        |14.2               |185.9                  |150.6                   |0                            |19.1                |0                  |0                 |9196324985 |20431811260      |47619           |64368092          |1.91                 |2.22    |1.784        |3.739  |0.9347    |
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|2            |4          |8          |0.7572        |30.2               |288.7                  |223.2                   |0                            |55.3                |0                  |0                 |10667746625|19540929504      |29347           |61978672          |1.97                 |1.83    |1.575        |2.3909 |0.2989    |
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|4            |2          |8          |0.596         |28.4               |307.6                  |141.1                   |0                            |40.9                |0                  |0                 |11603363975|20564053958      |49240           |64894874          |1.95                 |1.77    |2.103        |3.0376 |0.3797    |
|pexels-eberhardgross-858115.jpg|Hybrid_MPI_Pthreads|4            |4          |16         |0.4061        |22.9               |142.8                  |152.2                   |0                            |23.9                |0                  |0                 |11276532790|20348884651      |49857           |65522265          |1.97                 |1.8     |2.052        |4.458  |0.2786    |

</details>

<details>
<summary><strong> 5. pexels-joey-kyber (Mare)</strong> - <i>Click pentru detalii</i></summary>

|Image                             |Implementation     |Processes (P)|Threads (T)|Total Cores|Total Time (s)|Time_Grayscale (ms)|Time_Gaussian blur (ms)|Time_Sobel gradient (ms)|Time_Non-Max Suppression (ms)|Time_Hysteresis (ms)|Time_GPU Alloc (ms)|Time_Transfer (ms)|Perf_Cycles|Perf_Instructions|Perf_Page_Faults|Perf_Branch_Misses|Perf_Branch_Miss_Rate|Perf_IPC|Perf_CPU_Util|Speedup|Efficiency|
|----------------------------------|-------------------|-------------|-----------|-----------|--------------|-------------------|-----------------------|------------------------|-----------------------------|--------------------|-------------------|------------------|-----------|-----------------|----------------|------------------|---------------------|--------|-------------|-------|----------|
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|1            |1          |1          |1.8894        |66                 |826.8                  |522.6                   |0                            |112                 |0                  |0                 |8034217230 |21541107032      |19592           |63634998          |1.91                 |2.68    |0.538        |1      |1         |
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|1            |2          |2          |1.587         |70.9               |747.8                  |413.4                   |0                            |96.8                |0                  |0                 |11354779920|21459771732      |20888           |64329748          |1.95                 |1.89    |0.823        |1.1905 |0.5953    |
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|2            |1          |2          |1.2339        |69.2               |462.9                  |363.4                   |0                            |131.4               |0                  |0                 |8423888077 |21984610112      |28643           |64463220          |1.87                 |2.61    |0.893        |1.5312 |0.7656    |
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|1            |4          |4          |1.5766        |71.1               |743.1                  |416.2                   |0                            |97                  |0                  |0                 |11498361480|21469807055      |19841           |64002969          |1.94                 |1.87    |0.847        |1.1984 |0.2996    |
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|2            |2          |4          |0.5785        |16.4               |193.9                  |150.9                   |0                            |98.5                |0                  |0                 |8624905838 |21852485176      |30120           |64141053          |1.87                 |2.53    |1.058        |3.266  |0.8165    |
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|4            |1          |4          |0.6209        |17.4               |211.5                  |185.5                   |0                            |42.4                |0                  |0                 |9504337913 |22948845444      |47684           |66230383          |1.81                 |2.41    |1.587        |3.043  |0.7608    |
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|2            |4          |8          |0.7842        |34.6               |320.2                  |231.6                   |0                            |98.4                |0                  |0                 |11782580766|21965924356      |29080           |65199128          |1.92                 |1.86    |1.405        |2.4093 |0.3012    |
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|4            |2          |8          |0.4782        |17.2               |203.3                  |143.4                   |0                            |24.8                |0                  |0                 |12478307850|22879838529      |49281           |67787039          |1.87                 |1.83    |1.799        |3.9511 |0.4939    |
|pexels-joey-kyber-31917-134643.jpg|Hybrid_MPI_Pthreads|4            |4          |16         |0.4617        |27.6               |218.4                  |115                     |0                            |23.2                |0                  |0                 |12517629079|22495779418      |45535           |67028130          |1.88                 |1.8     |1.959        |4.0923 |0.2558    |

</details>

<details>
<summary><strong> 6. poza.jpg (Mică)</strong> - <i>Click pentru detalii</i></summary>

| Image    | Implementation      | Processes (P) | Threads (T) | Total Cores | Total Time (s) | Time_Grayscale (ms) | Time_Gaussian blur (ms) | Time_Sobel gradient (ms) | Time_Non-Max Suppression (ms) | Time_Hysteresis (ms) | Time_GPU Alloc (ms) | Time_Transfer (ms) | Perf_Cycles | Perf_Instructions | Perf_Page_Faults | Perf_Branch_Misses | Perf_Branch_Miss_Rate | Perf_IPC | Perf_CPU_Util | Speedup | Efficiency |
|----------|---------------------|---------------|-------------|-------------|----------------|---------------------|-------------------------|--------------------------|-------------------------------|----------------------|---------------------|--------------------|-------------|-------------------|------------------|--------------------|-----------------------|----------|---------------|---------|------------|
| poza.jpg | Hybrid_MPI_Pthreads | 1             | 1           | 1           | 0.0468         | 1.1                 | 13.1                    | 17.8                     | 0                             | 3.3                  | 0                   | 0                  | 265699182   | 509962660         | 10290            | 1698564            | 1.9                   | 1.92     | 0.18          | 1       | 1          |
| poza.jpg | Hybrid_MPI_Pthreads | 1             | 2           | 2           | 0.0329         | 1.1                 | 11.5                    | 10.7                     | 0                             | 3                    | 0                   | 0                  | 290695000   | 472692711         | 10289            | 1665812            | 1.92                  | 1.63     | 0.166         | 1.4225  | 0.7112     |
| poza.jpg | Hybrid_MPI_Pthreads | 2             | 1           | 2           | 0.0236         | 0.8                 | 9                       | 7.8                      | 0                             | 0.8                  | 0                   | 0                  | 402408791   | 702677062         | 14986            | 2150487            | 1.67                  | 1.75     | 0.411         | 1.9831  | 0.9915     |
| poza.jpg | Hybrid_MPI_Pthreads | 1             | 4           | 4           | 0.0371         | 1.2                 | 11.6                    | 13.3                     | 0                             | 3.3                  | 0                   | 0                  | 293024806   | 506550798         | 10302            | 1782376            | 1.8                   | 1.73     | 0.21          | 1.2615  | 0.3154     |
| poza.jpg | Hybrid_MPI_Pthreads | 2             | 2           | 4           | 0.014          | 0.5                 | 5.8                     | 3.9                      | 0                             | 0.8                  | 0                   | 0                  | 398016000   | 730090672         | 15000            | 2440536            | 1.76                  | 1.83     | 0.382         | 3.3429  | 0.8357     |
| poza.jpg | Hybrid_MPI_Pthreads | 4             | 1           | 4           | 0.0118         | 0.4                 | 4.6                     | 3.6                      | 0                             | 0.7                  | 0                   | 0                  | 905314133   | 1177947983        | 24482            | 3799283            | 1.68                  | 1.3      | 0.994         | 3.9661  | 0.9915     |
| poza.jpg | Hybrid_MPI_Pthreads | 2             | 4           | 8           | 0.0126         | 0.6                 | 4.6                     | 3.8                      | 0                             | 1.3                  | 0                   | 0                  | 436857208   | 683556164         | 15086            | 2421073            | 1.89                  | 1.56     | 0.475         | 3.7143  | 0.4643     |
| poza.jpg | Hybrid_MPI_Pthreads | 4             | 2           | 8           | 0.0122         | 1.3                 | 5                       | 3                        | 0                             | 0.9                  | 0                   | 0                  | 881929629   | 1119813094        | 24474            | 4891179            | 2.09                  | 1.27     | 0.888         | 3.8361  | 0.4795     |
| poza.jpg | Hybrid_MPI_Pthreads | 4             | 4           | 16          | 0.0131         | 0.3                 | 3.8                     | 4                        | 0                             | 1                    | 0                   | 0                  | 830573037   | 1152196906        | 24664            | 4027295            | 1.77                  | 1.39     | 1.063         | 3.5725  | 0.2233     |

</details>

<details>
<summary><strong> 7. round_earth (Extra)</strong> - <i>Click pentru detalii</i></summary>

|Image          |Implementation     |Processes (P)|Threads (T)|Total Cores|Total Time (s)|Time_Grayscale (ms)|Time_Gaussian blur (ms)|Time_Sobel gradient (ms)|Time_Non-Max Suppression (ms)|Time_Hysteresis (ms)|Time_GPU Alloc (ms)|Time_Transfer (ms)|Perf_Cycles|Perf_Instructions|Perf_Page_Faults|Perf_Branch_Misses|Perf_Branch_Miss_Rate|Perf_IPC|Perf_CPU_Util|Speedup|Efficiency|
|---------------|-------------------|-------------|-----------|-----------|--------------|-------------------|-----------------------|------------------------|-----------------------------|--------------------|-------------------|------------------|-----------|-----------------|----------------|------------------|---------------------|--------|-------------|-------|----------|
|round_earth.jpg|Hybrid_MPI_Pthreads|1            |1          |1          |4.133         |129.6              |1753                   |1179.3                  |0                            |332.6               |0                  |0                 |21239430872|58723902516      |21219           |139849020         |1.57                 |2.76    |0.836        |1      |1         |
|round_earth.jpg|Hybrid_MPI_Pthreads|1            |2          |2          |3.6126        |138.7              |1600.8                 |1007.4                  |0                            |331.6               |0                  |0                 |29891708509|58449384750      |21946           |140978672         |1.59                 |1.96    |1.191        |1.1441 |0.572     |
|round_earth.jpg|Hybrid_MPI_Pthreads|2            |1          |2          |2.5716        |184.8              |1075.7                 |599.2                   |0                            |332.4               |0                  |0                 |21926313707|59704240649      |32121           |139419089         |1.54                 |2.72    |1.41         |1.6072 |0.8036    |
|round_earth.jpg|Hybrid_MPI_Pthreads|1            |4          |4          |3.6057        |138.8              |1602.8                 |1004.2                  |0                            |331.9               |0                  |0                 |29973792481|58688938491      |21248           |140972763         |1.59                 |1.96    |1.187        |1.1462 |0.2866    |
|round_earth.jpg|Hybrid_MPI_Pthreads|2            |2          |4          |1.4144        |39.2               |482.8                  |383                     |0                            |251.3               |0                  |0                 |22393061439|59687563936      |31325           |140451067         |1.55                 |2.67    |1.69         |2.9221 |0.7305    |
|round_earth.jpg|Hybrid_MPI_Pthreads|4            |1          |4          |1.0161        |35.7               |483.3                  |274.3                   |0                            |70.2                |0                  |0                 |26598983171|62092383861      |48358           |143245071         |1.5                  |2.33    |2.576        |4.0675 |1.0169    |
|round_earth.jpg|Hybrid_MPI_Pthreads|2            |4          |8          |1.6908        |96.8               |614.9                  |388.8                   |0                            |286.4               |0                  |0                 |30046827146|59275535691      |31613           |142637994         |1.58                 |1.97    |2.342        |2.4444 |0.3056    |
|round_earth.jpg|Hybrid_MPI_Pthreads|4            |2          |8          |0.993         |39.3               |438.1                  |221.2                   |0                            |150.6               |0                  |0                 |30532294637|60649632633      |46694           |143168229         |1.54                 |1.99    |2.805        |4.1621 |0.5203    |
|round_earth.jpg|Hybrid_MPI_Pthreads|4            |4          |16         |0.825         |35.5               |400.2                  |201.5                   |0                            |138.5               |0                  |0                 |2.85E+10   |7.03E+10         |0               |1.52E+08          |1.49                 |1.88    |3.231        |5.0097 |0.3131    |

</details>


### Analiză Intel VTune: MPI_Pthreads

* Această secțiune prezintă datele brute și vizualizările generate de Intel VTune pentru rulările cu următoarele combinații threaduri-procese (threaduri pentru paralelizare pthreads, procese pentru paralelizarea folosind MPI) 1, 2, 4, 8 și 16 fire de execuție. Combinațiile sunt următoarele: (1,1); (1,2); (1,4); (2,1); (2,2); (2,4); (4,1); (4,2); (4,4).
* Scriptul script_vtune_full.sh automatizează complet profilarea Intel VTune (Hotspots) pentru implementările distribuite și hibride (MPI, CUDA, MPI+OpenMP, MPI+Pthreads, CUDA+MPI). Acesta compilează sursele și execută o matrice de teste scalabile (variind numărul de procese și fire de execuție), gestionând configurarea mediului SLURM (pe partiția xl cu GPU) și organizarea automată a rapoartelor de performanță în directorul profiling/vtune_results, eliminând efortul rulării manuale.
* Puteți consulta scriptul complet de automatizare aici: [script_vtune_full.sh](https://gitlab.cs.pub.ro/app-2025/cannycore/-/blob/main/script_vtune_full.sh)
* Am folosit acest script pentru a obține folderele de hotspots, care conțin și fișierul .vtune, pe care îl pot rula local mai apoi, pentru a vedea rezultatele.


### Analiză de Performanță: Intel VTune (Hybrid MPI + Pthreads)

Această secțiune detaliază profilarea execuției hibride. Spre deosebire de Pthreads pur, aici observăm impactul *overhead-ului* MPI și tranziția de la un singur proces la mai multe procese.
Tabelul de mai jos centralizează timpii de execuție și eficiența pentru imaginea de referință **1_earth_8k.jpg**, evidențiind impactul creșterii numărului de procese și thread-uri.

| Configurație (P x T) | Total Cores | Timp Total (s) | Speedup | Eficiență | Observații |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **P1 x T1** (Baseline) | 1 | 2.5448 | 1.00 | 100% | Referință serială. |
| **P1 x T2** | 2 | 2.1561 | 1.18 | 59% | Overhead mare de creare fire pentru un singur proces. |
| **P2 x T1** | 2 | 1.4406 | 1.77 | 88% | MPI pur (2 procese) scalează mai bine decât Pthreads pur (P1T2) aici. |
| **P1 x T4** | 4 | 2.1425 | 1.19 | 30% | Scalare slabă; posibil bottleneck pe memoria unui singur proces. |
| **P2 x T2** | 4 | 1.2749 | 2.00 | 50% | Echilibru bun între procese și fire. |
| **P4 x T1** | 4 | 0.6719 | 3.79 | 95% | MPI pur la 4 nuclee este foarte eficient (fără overhead Pthreads). |
| **P2 x T4** | 8 | 1.0203 | 2.50 | 31% | Începe să apară saturația memoriei. |
| **P4 x T2** | 8 | 0.6212 | 4.10 | 51% | Configurația optimă pentru 8 nuclee. |
| **P4 x T4** | 16 | 0.6232 | 4.08 | 25% | Plafonare completă (Memory Wall). 16 nuclee nu aduc beneficii peste 8. |

# Analiză Performanță: Hybrid MPI + Pthreads (Intel VTune)

Această pagină conține profilarea completă pentru diverse configurații hibride. Imaginile au fost reordonate pentru a corespunde titlurilor secțiunilor, corectând erorile de denumire a fișierelor.

---

<details>
<summary><strong>1. Configurație: P1 x T2 (1 Process, 2 Threads)</strong> - <i>Click pentru detalii</i></summary>

**Total Cores:** 2 | **Context:** Low Parallelism

### 1. Summary (General)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T2/01_Summary.png" width="100%">

### 2. Summary Graph (CPU Histogram)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T2/01_Summary_Graph.png" width="100%">

### 3. Summary Top (Hotspots List)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T2/01_Summary_Top.png" width="100%">

### 4. Bottom-up Timeline
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T2/03_CallerCallee.png" width="100%">

### 5. Caller / Callee
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T2/04_TopDown.png" width="100%">

### 6. Top Down Tree
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T2/05_FlameGraph.png" width="100%">

### 7. Flame Graph
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T2/06_Platform.png" width="100%">

### 8. Platform View
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T2/07_Source.png" width="100%">

</details>

---

<details>
<summary><strong>2. Configurație: P2 x T2 (2 Procese, 2 Threaduri fiecare)</strong> - <i>Click pentru detalii</i></summary>

**Total Cores:** 4 | **Context:** Medium Parallelism (Distributed + Shared)

### 1. Summary (General)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T2/01_Summary.png" width="100%">

### 2. Summary Graph (CPU Histogram)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T2/01_Summary_Graph.png" width="100%">

### 3. Summary Top (Hotspots List)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T2/01_Summary_Top.png" width="100%">

### 4. Bottom-up Timeline
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T2/03_CallerCallee.png" width="100%">

### 5. Caller / Callee
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T2/04_TopDown.png" width="100%">

### 6. Top Down Tree
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T2/05_FlameGraph.png" width="100%">

### 7. Flame Graph
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T2/06_Platform.png" width="100%">

### 8. Platform View
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T2/07_Source.png" width="100%">

</details>

---

<details>
<summary><strong>3. Configurație: P1 x T4 (1 Process, 4 Threads)</strong> - <i>Click pentru detalii</i></summary>

**Total Cores:** 4 | **Context:** Pure Shared Memory

### 1. Summary (General)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T4/01_Summary.png" width="100%">

### 2. Summary Graph (CPU Histogram)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T4/01_Summary_Graph.png" width="100%">

### 3. Summary Top (Hotspots List)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T4/01_Summary_Top.png" width="100%">

### 4. Bottom-up Timeline
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T4/03_CallerCallee.png" width="100%">

### 5. Caller / Callee
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T4/04_TopDown.png" width="100%">

### 6. Top Down Tree
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T4/05_FlameGraph.png" width="100%">

### 7. Flame Graph
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T4/06_Platform.png" width="100%">

### 8. Platform View
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P1_T4/07_Source.png" width="100%">

</details>

---

<details>
<summary><strong>4. Configurație: P4 x T2 (4 Procese, 2 Threaduri fiecare)</strong> - <i>Click pentru detalii</i></summary>

**Total Cores:** 8 | **Context:** High Parallelism (MPI Dominant)

### 1. Summary (General)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T2/01_Summary.png" width="100%">

### 2. Summary Graph (CPU Histogram)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T2/01_Summary_Graph.png" width="100%">

### 3. Summary Top (Hotspots List)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T2/01_Summary_Top.png" width="100%">

### 4. Bottom-up Timeline
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T2/03_CallerCallee.png" width="100%">

### 5. Caller / Callee
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T2/04_TopDown.png" width="100%">

### 6. Top Down Tree
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T2/05_FlameGraph.png" width="100%">

### 7. Flame Graph
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T2/06_Platform.png" width="100%">

### 8. Platform View
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T2/07_Source.png" width="100%">

</details>

---

<details>
<summary><strong>5. Configurație: P2 x T4 (2 Procese, 4 Threaduri fiecare)</strong> - <i>Click pentru detalii</i></summary>

**Total Cores:** 8 | **Context:** High Parallelism (Balanced)

### 1. Summary (General)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T4/01_Summary.png" width="100%">

### 2. Summary Graph (CPU Histogram)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T4/01_Summary_Graph.png" width="100%">

### 3. Summary Top (Hotspots List)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T4/01_Summary_Top.png" width="100%">

### 4. Bottom-up Timeline
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T4/03_CallerCallee.png" width="100%">

### 5. Caller / Callee
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T4/04_TopDown.png" width="100%">

### 6. Top Down Tree
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T4/05_FlameGraph.png" width="100%">

### 7. Flame Graph
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T4/06_Platform.png" width="100%">

### 8. Platform View
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P2_T4/07_Source.png" width="100%">

</details>

---

<details>
<summary><strong>6. Configurație: P4 x T4 (4 Procese, 4 Threaduri fiecare)</strong> - <i>Click pentru detalii</i></summary>

**Total Cores:** 16 | **Context:** Maximum Load / Saturation

### 1. Summary (General)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T4/01_Summary.png" width="100%">

### 2. Summary Graph (CPU Histogram)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T4/01_Summary_Graph.png" width="100%">

### 3. Summary Top (Hotspots List)
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T4/01_Summary_Top.png" width="100%">

### 4. Bottom-up Timeline
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T4/03_CallerCallee.png" width="100%">

### 5. Caller / Callee
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T4/04_TopDown.png" width="100%">

### 6. Top Down Tree
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T4/05_FlameGraph.png" width="100%">

### 7. Flame Graph
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T4/06_Platform.png" width="100%">

### 8. Platform View
<img src="../../wiki_images/MPI_PTHREADS/Performance_intelvtune__hybrid_mpi_pthread_P4_T4/07_Source.png" width="100%">

</details>

### Concluzie
* În concluzie, abordarea hibridă care combină MPI și Pthreads s-a dovedit a fi superioară utilizării exclusive a MPI într-un mediu de cluster multi-core. Această abordare este mai bună și decât openmp clasic, deoarece generează foarte multe fire de execuție. Fiecare proces din mpi împarte imaginea în mai multe zone, iar fiecare thread din folosirea pthreads segmentează și mai mult imaginea pentru o distribuție cât mai bună a sarcinilor. Prin utilizarea Pthreads pentru paralelizarea la nivel de memorie partajată (intra-nod) și a MPI pentru comunicarea între noduri (inter-nod), am reușit să reducem suprasarcina (overhead-ul) asociată comunicării prin rețea și să optimizăm utilizarea memoriei. Această arhitectură ierarhică exploatează eficient hardware-ul modern, oferind un echilibru ideal între granularitatea fină a firelor de execuție și scalabilitatea proceselor distribuite.

* Din perspectiva performanței, testele efectuate au demonstrat un speedup aproape liniar odată cu creșterea numărului de nuclee, limitat doar de secțiunile seriale conform legii lui Amdahl. Am observat că, prin menținerea unui singur proces MPI per nod și utilizarea Pthreads pentru toate nucleele disponibile, latența comunicării inter-proces a scăzut semnificativ comparativ cu o abordare pur MPI (unde comunicarea "all-to-all" ar fi generat congestii de rețea).

* Analiza de profilare realizată cu Intel VTune Profiler a validat eficiența acestei abordări. Rapoartele 'Hotspots' au evidențiat că timpul procesorului a fost petrecut preponderent în funcțiile de calcul (zona de prelucrare a imaginii) și nu în primitivele de sincronizare sau în așteptarea barierelor MPI. De asemenea, analiza **CPU Histogram** a arătat o utilizare susținută a unităților de procesare fizice, cu un nivel minim de context switching. VTune a confirmat, totodată, o utilizare eficientă a memoriei cache (L1/L2 hits), demonstrând că segmentarea imaginii prin Pthreads a păstrat o localitate a datelor superioară, minimizând penalizările de acces la memoria RAM.
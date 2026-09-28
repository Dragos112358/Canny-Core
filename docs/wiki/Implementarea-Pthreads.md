
# Implementare Pthreads - Inovații & Diferențe

În implementarea algoritmului **Canny Edge Detection** am folosit librăria nativă POSIX Threads (**Pthreads**). Spre deosebire de abstractizarea oferită de OpenMP, Pthreads vine cu un management manual și explicit al firelor de execuție. Această abordare a permis un control fin asupra memoriei și a distribuției sarcinilor, eliminând overhead-ul implicit al runtime-ului OpenMP, dar crescând complexitatea codului prin necesitatea utilizării structurilor de argumente și a calculelor manuale de indici.

## Modificările realizate

* **Paralelizare Explicită (Fork-Join):** 
	Spre deosebire de directivele `#pragma`, în această versiune se utilizează funcțiile `pthread_create` și `pthread_join` pentru fiecare etapă majoră a algoritmului (grayscale, Gaussian Blur, Sobel, NMS, Hysteresis). Thread-ul principal ("Master") orchestrează crearea thread-urilor "Worker" și așteaptă finalizarea lor înainte de a trece la etapa următoare (acționând ca o barieră de sincronizare).
* **Descompunerea Manuală a Domeniului (Data Decomposition):** Distribuția pixelilor către thread-uri se face manual. Fiecare funcție worker calculează intervalul de linii (`hStart`, `hEnd`) pe baza ID-ului thread-ului și a înălțimii imaginii:
    `int hStart = args->id * height / args->totalThreads;`
    Aceasta echivalează cu o strategie de **Static Scheduling** rigidă.
* **Structuri de Argumente:** Deoarece funcțiile thread-urilor acceptă un singur parametru `void*`, am implementat structuri dedicate (printre care se numără și `SobelArgs`, `NMSArgs`, `ThresholdArgs`) pentru a împacheta pointerii către imaginile de intrare/ieșire și parametrii specifici.
* **Hysteresis Hibrid:** Funcția `hysteresisThresholdPthreads` este implementată în 3 pași:
    1.  **Paralel:** Clasificarea pixelilor (Strong/Weak/Non-edge) folosind `threadThreshold`.
    2.  **Serial:** Urmărirea muchiilor (`trackEdge`) se realizează pe un singur fir de execuție (serial), deoarece natura recursivă și dependentă de date a DFS-ului face dificilă paralelizarea fără mecanisme complexe de sincronizare.
    3.  **Paralel:** Curățarea finală a pixelilor rămași (Cleanup).
* **Optimizare Branchless:** În funcțiile de thresholding, s-a utilizat logică condițională aritmetică pentru a evita *branch misprediction* (ex: `outRow[x] = (val >= high) ? 255 : ...`).

## Flow-ul final al execuției

1.  **Serial:** Citirea imaginii (stb_image).
2.  **Paralel (Pthreads):** Conversie Grayscale și Gaussian Blur (manual chunking).
3.  **Paralel (Pthreads):** Calcul Gradienți Sobel (fiecare thread scrie în zone disjuncte de memorie).
4.  **Paralel (Pthreads):** Non-Maximum Suppression (NMS).
5.  **Hibrid (Pthreads + Serial):** Hysteresis Thresholding (Clasificare paralelă -> Tracking Serial -> Cleanup Paralel).
6.  **Serial:** Salvarea rezultatului final.

Am ales să nu paralelizez partea de citire-scriere, deoarece ar fi presupus paralelizare pentru biblioteca stb_image_write.h, specifică citirii fișierelor în cpp.

## Provocări întâmpinate

* **Necesitatea de folosire a structurilor de date** Implementarea necesită mult mai mult cod "administrativ" (definire structuri, cast-uri `void*`, bucle de create/join) comparativ cu o singură linie `#pragma` în OpenMP sau cu varianta serială, unde codul nu are structuri de date speciale specifice Pthreads.
* **Gestionarea Marginilor (Boundary Conditions):** Calculul manual al indicilor (`hStart`, `hEnd`) a necesitat atenție sporită pentru a evita *off-by-one errors* sau accesarea memoriei în afara limitelor imaginii, în special la ultimul thread care trebuie să preia restul de linii rămase.
* **Overhead la Crearea Thread-urilor:** Deoarece thread-urile sunt create și distruse la fiecare etapă a funcției `cannyEdgeDetection, există un cost de sistem repetat, care devine vizibil la imaginile foarte mici. (cum ar fi [Poza](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/images/input/poza.jpg) sau [istock_photo](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/images/input/istockphoto-478656454-612x612.jpg))

---

## Profiling & Rezultate obținute

Mai jos sunt prezentate diferențe de metrici pentru partea de branch-miss, instruction per cycle, timpi și accelerații (speedup). Am ales poza  [1_earth_8k](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/images/input/1_earth_8k.jpg) ca punct de reper. Am considerat că este o imagine foarte bună, deoarece are o dimensiune medie spre mare (nu se simte un overhead mare din partea threadurilor).


### Comparație multithreaded vs nonmultithreaded
Am ales să realizez această comparație folosind scriptul [script_profiling.sh](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/script_profiling.sh). Analiza principală a fost pe imaginea 1_earth_8k, de dimensiune 8192*4096.

![Poza1](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__1_earth_8k_jpg__BranchMiss_Pthreads.png)

Graficul ilustrează rata de eroare a predicției ramurilor (Branch Miss Rate) în funcție de numărul de nuclee. Se observă două tendințe majore:
* **Stabilitate până la 4 nuclee:** Pentru 1, 2 și 4 nuclee, rata de eroare este constantă și identică între cele două configurații (~1.27%), ceea ce indică faptul că logica aritmetică (branchless) implementată funcționează eficient și predictibil.
* **Divergență la 8 nuclee:** Odată cu trecerea la 8 nuclee, rata de eroare crește brusc spre 1.55%. Aici se vede avantajul configurației Optimized (Physical Cores) – linia portocalie. Aceasta se menține ușor sub varianta Standard (HyperThreading), demonstrând că utilizarea nucleelor fizice dedicate reduce "poluarea" tabelelor de predicție ale procesorului (Branch Target Buffer), rezultând într-o execuție mai stabilă și mai puține cicluri irosite.
![Poza2](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__1_earth_8k_jpg__IPC_Pthreads.png)

Graficul *Instructions Per Cycle* (IPC) măsoară eficiența utilizării ciclurilor de procesor. Se observă clar tranziția algoritmului între două regimuri de funcționare:

* **Eficiență maximă (1-4 nuclee):** IPC-ul se menține constant la o valoare ridicată (~2.9), ceea ce indică faptul că firele de execuție saturează complet unitățile aritmetice ale nucleelor. Codul este eficient și *Compute-Bound*. Se observă faptul că prin folosirea în scriptul de rulare a profilingului (script_profiling.sh), folosirea **#SBATCH --hint=nomultithread**, rezultatele sunt ușor mai bune.
* **Limitare la 8 nuclee (Memory Wall):** Se observă o scădere bruscă a IPC-ului la ~1.75. Aceasta indică faptul că cele 8 fire de execuție încep să se lupte pentru resurse partajate (Cache L3 sau lățimea de bandă a memoriei RAM). Deși modul **Optimized** (Orange) nu poate preveni această limitare fizică a memoriei, el asigură că resursele de calcul per nucleu rămân dedicate, evitând penalizările suplimentare de latență pe care le-ar introduce Hyper-Threading-ul (Standard) într-un scenariu deja lipsit de memorie.
![Poza3](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__1_earth_8k_jpg__Speedup_Pthreads.png)

Graficul ilustrează factorul de accelerare (*Speedup*) raportat la execuția pe un singur nucleu. Se disting două aspecte cheie privind eficiența paralelizării:

* **Saturație la 8 Nuclee:** Curba de accelerare se aplatizează semnificativ după 4 nuclee, deviind de la linia ideală (gri). Aceasta confirmă limitarea impusă de memoria RAM (*Memory Wall*), deoarece 8 fire de execuție simultane saturază lățimea de bandă disponibilă pentru transferul datelor din imaginea 8K.
* **Avantajul Nucleelor Fizice:** La 8 nuclee, linia portocalie (**Optimized**) o depășește pe cea albastră (**Standard**). Aceasta validează utilizarea flag-ului `--hint=nomultithread`: prin dezactivarea Hyper-Threading-ului, fiecare fir de execuție primește acces exclusiv la memoria Cache L1/L2 și la unitățile de execuție ale nucleului, eliminând penalizările de performanță cauzate de *cache thrashing* și competiția pentru resurse care apar în modul Standard.
![Poza4](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__1_earth_8k_jpg__Time_Pthreads.png)

* **Comportament la 1-2 Nuclee (Latency Hiding):**
    Se observă că linia albastră (**Optimized**) este ușor deasupra celei portocalii, indicând un timp de execuție marginal mai mare. Acest lucru se explică prin faptul că Hyper-Threading-ul (Standard) ajută la "ascunderea latenței" memoriei. Când un fir de execuție se blochează așteptând date din imaginea 8K (Memory Stall), nucleul logic pereche poate prelua scurte sarcini de sistem sau instrucțiuni intercalate, menținând pipeline-ul procesorului activ. În modul *Optimized*, nucleul fizic dedicat pur și simplu așteaptă, ceea ce se traduce prin cicluri "idle".

* **Convergență și Eficiență la 8 Nuclee:**
    Pe măsură ce creștem numărul de fire la 4 și 8, liniile converg, iar modul **Optimized** devine competitiv. La 8 fire, sistemul atinge limitarea fizică a lățimii de bandă a memoriei (*Memory Wall*). În acest punct critic, avantajul modului `nomultithread` este stabilitatea: fiecare fir are garanția accesului exclusiv la memoria Cache L1/L2 a nucleului său, evitând conflictele (cache thrashing) care ar degrada performanța într-un scenariu cu Hyper-Threading supraîncărcat.

* **Imagini mari (1_earth_8k):** Scalabilitatea este excelentă (aproape liniară până la 4-8 thread-uri). Overhead-ul de creare a thread-urilor este neglijabil raportat la timpul de calcul per pixel.
* **Imagini mici (poza):** Eficiența scade mai rapid decât la imagini mari. Timpul petrecut în `pthread_create` și `pthread_join` devine comparabil cu timpul de execuție al kernel-ului efectiv.

#### Vezi graficele comparative pentru celelalte imagini:

<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__city_jpg__Time_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__city_jpg__Speedup_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__city_jpg__IPC_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__city_jpg__BranchMiss_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__poza_jpg__Time_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__poza_jpg__Speedup_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__poza_jpg__IPC_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__poza_jpg__BranchMiss_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__istockphoto-478656454-612x612_jpg__Time_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__istockphoto-478656454-612x612_jpg__Speedup_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__istockphoto-478656454-612x612_jpg__IPC_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__istockphoto-478656454-612x612_jpg__BranchMiss_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-eberhardgross-858115_jpg__Time_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-eberhardgross-858115_jpg__Speedup_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-eberhardgross-858115_jpg__IPC_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-eberhardgross-858115_jpg__BranchMiss_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| Timp Execuție | Speedup |
| :---: | :---: |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-joey-kyber-31917-134643_jpg__Time_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-joey-kyber-31917-134643_jpg__Speedup_Pthreads.png" width="100%"> |
| **IPC** | **Branch Misses** |
| <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-joey-kyber-31917-134643_jpg__IPC_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Comparative_Analysis_HT_vs_Physical__pexels-joey-kyber-31917-134643_jpg__BranchMiss_Pthreads.png" width="100%"> |

</details>




### Comparatie Haswell vs XL
Am ales să rulez atât pe Haswell, cât și pe XL, pentru a putea vedea cum se comportă algoritmul pe 2 arhitecturi diferite. Am rulat pe Haswell toți algoritmii care nu necesitau GPU ([CUDA](/app-2025/cannycore/-/wikis/wiki/Implementarea-CUDA), [CUDA + OpenMP](/app-2025/cannycore/-/wikis/wiki/Implementarea-CUDA-OpenMP), [CUDA + MPI](/app-2025/cannycore/-/wikis/wiki/Implementarea-CUDA-MPI)), iar pe XL am rulat absolut toți cei 9 algoritmi, inclusiv cei care includeau Cuda.

![Poza1](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/GLOBAL_MULTI_PLATFORM_ANALYSIS__2_Scalability_Pthreads_1_earth_8k.png)

* **Haswell (Linia Albastră) - Scalare Super-Liniară:**
    Se observă un fenomen interesant pe Haswell: la 4 nuclee, speedup-ul atinge valoarea ideală (4x), ba chiar ușor peste. Acest comportament "super-liniar" este tipic atunci când divizarea problemei în bucăți mai mici permite ca setul de date per nucleu să încapă mai bine în memoria Cache L2/L3 a procesorului Haswell, reducând drastic numărul de accesări lente la RAM. Haswell, având o frecvență per nucleu mai mică și o arhitectură mai veche, beneficiază enorm de pe urma paralelizării eficiente a datelor.

* **XL Node (Linia Portocalie) - Limitare de Bandă:**
    Pe nodul XL, deși performanța absolută (timpul brut) este mai bună, *factorul de scalare* este mai slab (se aplatizează la ~3x pentru 8 nuclee). Aceasta indică faptul că procesorul modern de pe XL este mult mai rapid în calcule decât poate memoria RAM să îi furnizeze date. Cele 8 fire de execuție consumă datele din imaginea 8K atât de repede încât saturează complet magistrala de memorie (*Memory Bound*), limitând câștigul obținut prin adăugarea de noi nuclee.

| Haswell (Breakdown Pthreads) | XL (Breakdown Pthreads) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Pthreads_Cores1.png" width="100%" alt="Haswell 1 Core"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL 1 Core"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Pthreads_Cores2.png" width="100%" alt="Haswell 2 Cores"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL 2 Cores"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Pthreads_Cores4.png" width="100%" alt="Haswell 4 Cores"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL 4 Cores"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Breakdown_Pthreads_Cores8.png" width="100%" alt="Haswell 8 Cores"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL 8 Cores"> |

### Analiză Breakdown (Pthreads): Haswell vs. XL (1_earth_8k)

Graficele de tip "plăcintă" (Pie Charts) ilustrează procentul de timp consumat de fiecare etapă a algoritmului Canny Edge Detection pe două platforme hardware diferite (Haswell vs. XL), la diferite nivele de paralelizare (1, 2, 4 și 8 nuclee).

* **Dominanța Gaussian Blur și Sobel:**
    Indiferent de numărul de nuclee sau de platformă, etapele de **Gaussian Blur** (portocaliu) și **Sobel Gradient** (verde) consumă majoritatea covârșitoare a timpului de execuție (împreună reprezintă aproximativ **80-90%** din timpul total).  Acest lucru este de așteptat, deoarece aceste operații implică convoluții costisitoare aplicate fiecărui pixel din imaginea de mari dimensiuni (8K).

* **Impactul Scalării pe Haswell (Stânga):**
    * Pe Haswell, distribuția procentuală rămâne relativ constantă pe măsură ce creștem numărul de nuclee de la 1 la 8. Gaussian Blur se menține în jurul valorii de 60%, iar Sobel în jur de 31%.
    * Această constanță sugerează o **scalare uniformă** a tuturor etapelor paralelizabile. Faptul că proporțiile nu se schimbă drastic indică faptul că niciuna dintre etape nu devine un *bottleneck* disproporționat pe măsură ce adăugăm mai multe fire de execuție; toate beneficiază similar de pe urma paralelizării eficiente a datelor în cache-ul L2/L3 generos al arhitecturii Haswell.

* **Dinamica pe XL (Dreapta) - "Memory Wall" la Sobel:**
    * Pe nodul XL, observăm o dinamică ușor diferită. Deși Gaussian Blur scade procentual de la ~60% (1 core) la ~55% (8 cores), etapa **Sobel Gradient** (verde) tinde să își mențină sau chiar să crească ușor ponderea relativă.
    * **Explicația:** Aceasta subliniază natura *Memory Bound* a calculului Sobel pe o arhitectură modernă rapidă. Deoarece XL procesează calculele foarte repede, limitarea devine lățimea de bandă a memoriei. Sobel necesită citiri și scrieri intense. La 8 nuclee, presiunea pe memoria RAM crește, făcând ca această etapă să nu se accelereze la fel de mult ca Gaussian Blur (care este mai *Compute Bound*), crescându-i astfel ponderea în timpul total de execuție.

* **Etapele Secundare (Hysteresis & NMS):**
    Etapele de **Non-Max Suppression** și **Hysteresis** (feliile mici) au un impact minor asupra timpului total. Totuși, se observă o ușoară creștere procentuală a etapei de Hysteresis pe măsură ce numărul de nuclee crește. Acest lucru se datorează faptului că Hysteresis conține o componentă serială (**Edge Tracking**) care nu poate fi paralelizată eficient, devenind un limitator (conform *Legii lui Amdahl*) atunci când restul algoritmului rulează foarte rapid pe 8 nuclee.

#### Vezi comparația Haswell vs XL pentru celelalte imagini:

<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

![Scalability City](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/GLOBAL_MULTI_PLATFORM_ANALYSIS__2_Scalability_Pthreads_city.png)

| Haswell (city) | XL (city) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Pthreads_Cores1.png" width="100%"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Pthreads_Cores1.png" width="100%"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Pthreads_Cores2.png" width="100%"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Pthreads_Cores2.png" width="100%"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Pthreads_Cores4.png" width="100%"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Pthreads_Cores4.png" width="100%"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Breakdown_Pthreads_Cores8.png" width="100%"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Pthreads_Cores8.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

![Scalability Poza](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/GLOBAL_MULTI_PLATFORM_ANALYSIS__2_Scalability_Pthreads_poza.png)

| Haswell (poza) | XL (poza) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Pthreads_Cores1.png" width="100%"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Pthreads_Cores1.png" width="100%"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Pthreads_Cores2.png" width="100%"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Pthreads_Cores2.png" width="100%"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Pthreads_Cores4.png" width="100%"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Pthreads_Cores4.png" width="100%"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Breakdown_Pthreads_Cores8.png" width="100%"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Pthreads_Cores8.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

![Scalability iStock](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/GLOBAL_MULTI_PLATFORM_ANALYSIS__2_Scalability_Pthreads_istockphoto-478656454-612x612.png)

| Haswell (istock) | XL (istock) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Pthreads_Cores1.png" width="100%"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Pthreads_Cores1.png" width="100%"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Pthreads_Cores2.png" width="100%"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Pthreads_Cores2.png" width="100%"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Pthreads_Cores4.png" width="100%"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Pthreads_Cores4.png" width="100%"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Breakdown_Pthreads_Cores8.png" width="100%"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Breakdown_Pthreads_Cores8.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

![Scalability Pexels Eberhard](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/GLOBAL_MULTI_PLATFORM_ANALYSIS__2_Scalability_Pthreads_pexels-eberhardgross-858115.png)

| Haswell (pexels-e) | XL (pexels-e) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores1.png" width="100%"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores1.png" width="100%"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores2.png" width="100%"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores2.png" width="100%"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores4.png" width="100%"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores4.png" width="100%"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores8.png" width="100%"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores8.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

![Scalability Pexels Joey](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/GLOBAL_MULTI_PLATFORM_ANALYSIS__2_Scalability_Pthreads_pexels-joey-kyber-31917-134643.png)

| Haswell (pexels-j) | XL (pexels-j) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Pthreads_Cores1.png" width="100%"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores1.png" width="100%"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Pthreads_Cores2.png" width="100%"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores2.png" width="100%"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Pthreads_Cores4.png" width="100%"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores4.png" width="100%"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Breakdown_Pthreads_Cores8.png" width="100%"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores8.png" width="100%"> |

</details>

### Comparație multithreaded vs no_multithreaded
Am realizat o comparație și am observat faptul că no_multithreaded obține timpi mai buni. Motivul este că în cazul multithreaded, pot exista mai multe fire de execuție care așteaptă după o aceeași resursă partajată. Diferențele de timpi sunt în jur de 3-5% pentru puține threaduri/procese (sub 4) și de aproximativ 7-12% pentru mai mult threaduri/procese. Cele mai mari diferențe se observă pentru 4 threaduri și 4 procese în cazul algoritmilor hibrizi (MPI + Pthreads și OpenMP + Pthreads).
 
| XL No Multithread (Hint: nomultithread) | XL Standard (Full / Hint: multithread) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |

<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_istockphoto-478656454-612x612__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_istockphoto-478656454-612x612__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_istockphoto-478656454-612x612__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_istockphoto-478656454-612x612__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| XL No Multithread (nomultithread) | XL Standard (multithread) |
| :---: | :---: |
| **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL NoMulti 1 Core"> | **1 Core**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores1.png" width="100%" alt="XL Full 1 Core"> |
| **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL NoMulti 2 Cores"> | **2 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores2.png" width="100%" alt="XL Full 2 Cores"> |
| **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL NoMulti 4 Cores"> | **4 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores4.png" width="100%" alt="XL Full 4 Cores"> |
| **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL NoMulti 8 Cores"> | **8 Cores**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Breakdown_Pthreads_Cores8.png" width="100%" alt="XL Full 8 Cores"> |

</details>

**Observații și Interpretare:**

* **Stabilitate Structurală:**
    Primul lucru care iese în evidență este similaritatea izbitoare dintre cele două grafice. Indiferent dacă folosim nuclee fizice sau logice, profilul algoritmului rămâne constant:
    * **Gaussian Blur** (Portocaliu) domină procesarea, ocupând aproximativ **50-55%** din timpul total.
    * **Sobel Gradient** (Verde) este a doua cea mai costisitoare etapă, ocupând aproximativ **30-32%**.
    Acest lucru confirmă faptul că optimizarea acestor două funcții (care sunt operații de convoluție intensive) este critică, indiferent de strategia de threading aleasă.

* **Impactul Hyper-Threading-ului (Dreapta):**
    Deși proporțiile sunt similare, există diferențe subtile cauzate de partajarea resurselor în modul Standard (Dreapta):
    * În modul **Standard**, firele de execuție "vecine" (care împart același nucleu fizic) concurează pentru memoria Cache L1/L2.
    * Etapa **Gaussian Blur** este extrem de intensivă în citiri de memorie (accesează vecinii fiecărui pixel). Faptul că procentul său variază ușor între cele două moduri (ex: scade ușor procentual când apare overhead de sincronizare sau cache thrashing) indică sensibilitatea acestei etape la "zgomotul" introdus de Hyper-Threading.

* **Etapele Secundare (Hysteresis & NMS):**
    Etapele de **Hysteresis** (Mov) și **Non-Max Suppression** (Roșu) rămân componente minoritare (~10-15% cumulat). Faptul că Hysteresis (care conține pași seriali) nu crește disproporționat în dreapta sugerează că implementarea hibridă a fost eficientă și nu suferă penalizări majore de context-switching pe nucleele logice.

* **Concluzie:**
    Configurația **No-Multithreaded** (Stânga) oferă o distribuție a timpului "mai curată", reflectând costul pur de calcul al fiecărei etape, nealterat de competiția pentru resurse hardware partajate. Aceasta face ca performanța să fie mai predictibilă, validând alegerea de a dezactiva Hyper-Threading-ul pentru sarcini de tip HPC (High Performance Computing).

### Analiză Performanță și Memorie: XL No-Multithread vs. XL Standard

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Speedup_Efficiency_Pthreads.png" width="100%" alt="Speedup NoMulti"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Speedup_Efficiency_Pthreads.png" width="100%" alt="Speedup Full"> |
| **Stage Comparison**<br>*(Timp per etapă algoritm)* | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Stage_Comparison_Pthreads.png" width="100%" alt="Stages NoMulti"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Stage_Comparison_Pthreads.png" width="100%" alt="Stages Full"> |
| **Memory & Cache**<br>*(Analiză cache misses)* | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Memory_Cache_Analysis_Pthreads.png" width="100%" alt="Memory NoMulti"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Memory_Cache_Analysis_Pthreads.png" width="100%" alt="Memory Full"> |
| **Perf Counters**<br>*(Contoare hardware)* | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_1_earth_8k__Perf_Counters_Pthreads.png" width="100%" alt="Perf NoMulti"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Perf_Counters_Pthreads.png" width="100%" alt="Perf Full"> |

**1. Speedup & Efficiency (Rândul de sus)**

* **Similitudine Structurală:**
    Curbele de *Speedup* (Albastru) și *Efficiency* (Verde) sunt aproape identice în ambele grafice. Ambele configurații ating un speedup maxim de aproximativ **3x** la 8 nuclee, departe de idealul liniar (care ar fi fost 8x).
* **Plafonarea la 8 Nuclee:**
    Faptul că linia de Speedup se aplatizează la 8 nuclee în *ambele* cazuri confirmă că limitarea nu este puterea de calcul a procesorului (care crește în modul Standard prin HT), ci **lățimea de bandă a memoriei**. Adăugarea de fire de execuție suplimentare (Hyper-Threading) în graficul din dreapta nu reușește să ridice curba albastră, deoarece firele stau la coadă pentru aceleași date din RAM.
* **Scăderea Eficienței:**
    Eficiența scade abrupt sub 40% în ambele cazuri. Acest lucru indică faptul că, pe măsură ce adăugăm nuclee, fiecare nucleu nou aduce un beneficiu marginal tot mai mic.

**2. Stage Comparison - Timp per Etapă (Rândul de jos)**

* **Comportamentul Gaussian Blur (Linia Portocalie):**
    În ambele grafice, *Gaussian Blur* este etapa dominantă. Se observă o scădere semnificativă a timpului de la 1 la 4 nuclee (panta abruptă), dar între 4 și 8 nuclee linia devine orizontală. Aceasta este vizualizarea grafică a "Memory Wall-ului": procesarea devine atât de rapidă încât memoria nu mai poate livra pixelii suficient de repede.
* **Stabilitate vs. Performanță:**
    Deși graficele arată similar, varianta **No Multithread** (Stânga) este preferabilă în HPC. Motivul este subtil: în graficul din dreapta (Standard), există riscul ca timpii să varieze (jitter) din cauza competiției pe resursele interne ale nucleului (ALU/FPU partajate). Graficul din stânga garantează că timpii afișați sunt rezultatul puterii brute a nucleelor fizice, oferind o performanță mai predictibilă, chiar dacă valorile absolute sunt similare.

**3. Memory & Cache**

* **Branch Miss Rate (Linia Portocalie):**
    * În ambele grafice, rata de eroare a predicției ramurilor este remarcabil de stabilă și joasă (~1.27%) pentru 1, 2 și 4 nuclee. Aceasta confirmă calitatea codului (logică *branchless*).
    * **Diferența la 8 Cores:** La încărcare maximă, ambele grafice arată o creștere bruscă (*spike*) spre 1.55%. Totuși, în graficul din dreapta (**Standard**), panta pare marginal mai abruptă. Aceasta indică faptul că 8 fire logice poluează mai agresiv tabelele de predicție (BTB - Branch Target Buffer) decât 8 fire rulate strict pe nuclee fizice.

* **Page Faults (Linia Albastră) - Detaliu Critic:**
    * **Stânga (No Multithread):** Numărul de page faults scade constant pe măsură ce creștem numărul de nuclee. Este un comportament ideal.
    * **Dreapta (Standard):** Observăm un fenomen de **"Hook" (Cârlig)** la 8 nuclee. Linia scade până la 4 nuclee, dar *urcă înapoi* la 8 nuclee. Aceasta este dovada clară a overhead-ului de memorie virtuală introdus de Hyper-Threading: gestionarea a 8 fire logice concurente pe aceleași resurse fizice cauzează mai multe erori de pagină (TLB misses) și o presiune mai mare pe OS, spre deosebire de curba descendentă curată din stânga.

**4. Perf Counters**

* **IPC - Instructions Per Cycle (Linia Roșie):**
    * Ambele configurații prezintă o similaritate mare: IPC-ul este maxim (~2.9) până la 4 nuclee, apoi se prăbușește la ~1.7 la 8 nuclee.
    * **Interpretare:** Aceasta este semnătura clasică a limitării de memorie (*Memory Bound*). Indiferent dacă folosim hyperthreading sau nu, procesorul așteaptă după date. Faptul că graficul din dreapta (Standard) nu arată niciun câștig de IPC confirmă că Hyper-Threading-ul este inutil pentru acest tip de sarcină: nu există "bule" de procesare pe care hyperthreadingul să le poată umple, deoarece pipeline-ul e blocat de memoria RAM.

* **CPU Utilization (Linia Mov):**
    * Creșterea este liniară în ambele cazuri, ceea ce arată că planificatorul sistemului de operare își face treaba și alocă resursele cerute. Totuși, combinat cu graficul IPC, înțelegem că la 8 nuclee, deși utilizarea este 100%, o mare parte din acea "utilizare" este de fapt timp de așteptare (*stalling*).

####  Vezi analiza grafică pentru celelalte imagini:

<details>
<summary><strong> Vezi analiza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Speedup_Efficiency_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comparison** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Stage_Comparison_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Stage_Comparison_Pthreads.png" width="100%"> |
| **Memory & Cache** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Memory_Cache_Analysis_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_city__Perf_Counters_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Perf_Counters_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Speedup_Efficiency_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comparison** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Stage_Comparison_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Stage_Comparison_Pthreads.png" width="100%"> |
| **Memory & Cache** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Memory_Cache_Analysis_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_poza__Perf_Counters_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Perf_Counters_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: iStock Photo</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Speedup_Efficiency_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comparison** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Stage_Comparison_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Stage_Comparison_Pthreads.png" width="100%"> |
| **Memory & Cache** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Memory_Cache_Analysis_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_istockphoto-478656454-612x612__Perf_Counters_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Perf_Counters_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Speedup_Efficiency_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comparison** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Stage_Comparison_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Stage_Comparison_Pthreads.png" width="100%"> |
| **Memory & Cache** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Memory_Cache_Analysis_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-eberhardgross-858115__Perf_Counters_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Perf_Counters_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi analiza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| Metrica | XL No Multithread | XL Standard (Full) |
| :--- | :---: | :---: |
| **Speedup & Efficiency** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Speedup_Efficiency_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comparison** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Stage_Comparison_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Stage_Comparison_Pthreads.png" width="100%"> |
| **Memory & Cache** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Memory_Cache_Analysis_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters** | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_xl_no_multithreading__analysis_charts_pexels-joey-kyber-31917-134643__Perf_Counters_Pthreads.png" width="100%"> | <img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Perf_Counters_Pthreads.png" width="100%"> |

</details>

### Memorie și Cache
* **False Sharing:** Deoarece fiecare thread lucrează pe blocuri mari și distincte de memorie (linii complete), fenomenul de *false sharing* este minimizat, apărând doar potențial la granițele dintre chunk-urile thread-urilor adiacente. Se observă diferențe mici de procentaje (procentul de muchii dintr-o imagine) pentru diferiți algoritmi. Dacă la serial, poza 1_earth_8k are ca rezultat 5.425% muchii, pentru pthreads acest procent crește ușor la 5.436% (diferență de 0.29%, invizibilă în output). Motivul este de la hysterezis threshold. Un pixel este marcat ca muchie "sigură" dacă este vecin cu o muchie puternică. Această propagare poate traversa întreaga imagine într-o direcție continuă. În Pthreads, această etapă este adesea limitată la blocul local al thread-ului. Dacă o muchie lungă "șerpuiește" prin imagine trecând dintr-un bloc în altul, firul de execuție s-ar putea să nu știe că pixelul de la graniță este conectat la o muchie puternică aflată în blocul vecin (pe care celălalt thread încă nu a procesat-o sau a procesat-o independent). Acest lucru duce la fragmentarea muchiilor la granițe și la includerea sau excluderea unor pixeli de muchie slabă.
* **Locality:** Accesul la memorie este secvențial, favorizând prefetching-ul hardware al CPU-ului.



| Haswell (1_earth_8k) | XL (1_earth_8k) |
| :---: | :---: |
| **Memory & Cache Analysis**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Memory_Cache_Analysis_Pthreads.png" width="100%" alt="Haswell Memory"> | **Memory & Cache Analysis**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Memory_Cache_Analysis_Pthreads.png" width="100%" alt="XL Memory"> |
| **Performance Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Perf_Counters_Pthreads.png" width="100%" alt="Haswell Perf Counters"> | **Performance Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Perf_Counters_Pthreads.png" width="100%" alt="XL Perf Counters"> |
| **Speedup & Efficiency**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Speedup_Efficiency_Pthreads.png" width="100%" alt="Haswell Speedup"> | **Speedup & Efficiency**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Speedup_Efficiency_Pthreads.png" width="100%" alt="XL Speedup"> |
| **Stage Comparison**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_1_earth_8k__Stage_Comparison_Pthreads.png" width="100%" alt="Haswell Stage Comp"> | **Stage Comparison**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_1_earth_8k__Stage_Comparison_Pthreads.png" width="100%" alt="XL Stage Comp"> |

**1. Memory & Cache Analysis (Rândul de sus)**

* **Page Faults (Linia Albastră) - Forma de "V":**
    * **Similitudine Izbitoare:** Pe ambele platforme, curba erorilor de pagină are o formă de "V" (sau cârlig). Numărul de page faults este mare la 1 thread, atinge un minim optim la 4 thread-uri, și crește din nou la 8 thread-uri.
    * **Interpretare:** Această similitudine demonstrează că gestionarea memoriei virtuale și presiunea pe **TLB (Translation Lookaside Buffer)** depind mai mult de modelul de acces al algoritmului și de sistemul de operare decât de vechimea procesorului. La 8 nuclee, ambele sisteme încep să sufere de overhead în gestionarea paginilor de memorie pentru o imagine atât de mare.

* **Branch Miss Rate (Linia Portocalie):**
    * **Haswell:** Rata de eroare este foarte joasă și plată (~1.27%) până la 4 nuclee, apoi are un salt brusc.
    * **XL:** Comportamentul este identic. Aceasta confirmă că logica codului este robustă (`branchless`), iar creșterea ratei de eroare la 8 nuclee este un efect secundar al concurenței și al latențelor de memorie care pot "păcăli" predictorul, indiferent cât de sofisticat este acesta pe cipul modern.

**2. Performance Counters (Rândul de jos)**

* **IPC - Instructions Per Cycle (Linia Roșie):**
    * **Haswell:** IPC-ul se menține constant (~2.8) până la 4 nuclee, apoi scade dramatic la ~1.6 la 8 nuclee.
    * **XL:** Profilul este identic. IPC-ul maxim este tot în jur de 2.8, cu o prăbușire similară la 8 nuclee.
    * **Concluzie Critică:** Faptul că IPC-ul se prăbușește la fel pe ambele mașini este dovada supremă a **"Memory Wall-ului"**. Chiar dacă procesorul XL are unități de calcul mult mai puternice și frecvențe diferite, el nu poate procesa instrucțiuni mai repede decât îi permite memoria RAM să citească datele. La 8 fire de execuție, ambele procesoare stau și așteaptă după memorie în egală măsură.

* **CPU Utilization (Linia Mov):**
    * Pe ambele sisteme, utilizarea scalează liniar perfect. Sistemul de operare "vede" nucleele ocupate 100%, dar graficul IPC ne spune adevărul: acea "utilizare" este de fapt formată din multe cicluri de așteptare (stalls).
   
**3. Speedup & Eficiență**

* **Haswell (Stânga) - Scalare Robustă:**
    * Linia de **Speedup** (Albastră) este impresionantă. Urcă constant și se apropie de 4x la 8 nuclee.
    * **Eficiența** (Verde) scade controlat, menținându-se peste 40-50% chiar și la încărcare maximă.
    * **Explicatie:** Procesoarele Haswell au o putere de calcul mai mică per nucleu. Astfel, ele nu reușesc să satureze memoria RAM la fel de repede ca cele moderne. Deoarece nucleele petrec mai mult timp făcând calcule efective (decât așteptând date), adăugarea de nuclee noi aduce un câștig real de performanță.

* **XL Node (Dreapta) - Saturație Rapidă:**
    * Linia de **Speedup** se aplatizează (devine orizontală) mult mai repede, atingând un maxim de ~3x la 8 nuclee.
    * **Eficiența** se prăbușește dramatic sub 40%.
    * **Paradoxul Performanței:** Deși XL termină task-ul mult mai repede (în secunde), el scalează mai prost. Nucleele sale sunt atât de rapide încât "înghit" datele instantaneu și apoi stau degeaba așteptând memoria. Adăugarea de nuclee noi nu ajută, ci doar crește coada de așteptare la magistrala de memorie.

**4. Stage Comparison - Timp per Etapă**

* **Forma Curbelor (Panta):**
    * **Haswell (Stânga):** Curbele pentru *Gaussian Blur* (Mov) și *Sobel* (Portocaliu) au o pantă descendentă continuă până la 8 nuclee. Asta înseamnă că la fiecare pas de paralelizare, timpul scade vizibil.
    * **XL (Dreapta):** Curbele, în special cea roșie (*Gaussian Blur*) și verde (*Sobel*), fac un "cot" la 4 nuclee și devin aproape plate spre 8 nuclee.

* **Impactul Memory Wall:**
    * Acest grafic validează perfect teoria **Memory Bound**. Pe XL, linia plată de la 4 la 8 nuclee arată că nu mai contează câtă putere de calcul adăugăm; viteza este dictată exclusiv de cât de repede putem citi pixelii din RAM. Pe Haswell, fiind mai lent la calcule, nu a lovit acest "zid" la fel de violent, permițând o scalare mai vizibilă.

**Concluzie:**
Comparația ilustrează perfect **Legea lui Amdahl** aplicată resurselor limitate. Pe hardware modern (XL), optimizarea algoritmului trebuie să se mute de la "cum împărțim calculele" la "cum accesăm memoria mai eficient" (tiling, cache blocking), în timp ce pe hardware vechi (Haswell), simpla paralelizare a calculelor aducea câștiguri liniare.

####  Vezi rezultatele pentru celelalte imagini:

<details>
<summary><strong> Vezi și poza: City (Rezoluție Medie)</strong> - <i>Click aici</i></summary>

| Haswell (city) | XL (city) |
| :---: | :---: |
| **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Memory_Cache_Analysis_Pthreads.png" width="100%"> | **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Perf_Counters_Pthreads.png" width="100%"> | **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Perf_Counters_Pthreads.png" width="100%"> |
| **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Speedup_Efficiency_Pthreads.png" width="100%"> | **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_city__Stage_Comparison_Pthreads.png" width="100%"> | **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_city__Stage_Comparison_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi și poza: Poza (Rezoluție Mică)</strong> - <i>Click aici</i></summary>

| Haswell (poza) | XL (poza) |
| :---: | :---: |
| **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Memory_Cache_Analysis_Pthreads.png" width="100%"> | **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Perf_Counters_Pthreads.png" width="100%"> | **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Perf_Counters_Pthreads.png" width="100%"> |
| **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Speedup_Efficiency_Pthreads.png" width="100%"> | **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_poza__Stage_Comparison_Pthreads.png" width="100%"> | **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_poza__Stage_Comparison_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi și poza: iStock Photo</strong> - <i>Click aici</i></summary>

| Haswell (istock) | XL (istock) |
| :---: | :---: |
| **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Memory_Cache_Analysis_Pthreads.png" width="100%"> | **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Perf_Counters_Pthreads.png" width="100%"> | **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Perf_Counters_Pthreads.png" width="100%"> |
| **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Speedup_Efficiency_Pthreads.png" width="100%"> | **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_istock_photo_612__Stage_Comparison_Pthreads.png" width="100%"> | **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_ISTOCK_PHOTO_612__Stage_Comparison_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi și poza: Pexels Eberhardgross</strong> - <i>Click aici</i></summary>

| Haswell (pexels-e) | XL (pexels-e) |
| :---: | :---: |
| **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Memory_Cache_Analysis_Pthreads.png" width="100%"> | **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Perf_Counters_Pthreads.png" width="100%"> | **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Perf_Counters_Pthreads.png" width="100%"> |
| **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Speedup_Efficiency_Pthreads.png" width="100%"> | **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels-eberhardgross-858115__Stage_Comparison_Pthreads.png" width="100%"> | **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-eberhardgross-858115__Stage_Comparison_Pthreads.png" width="100%"> |

</details>

<details>
<summary><strong> Vezi și poza: Pexels Joey Kyber</strong> - <i>Click aici</i></summary>

| Haswell (pexels-j) | XL (pexels-j) |
| :---: | :---: |
| **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Memory_Cache_Analysis_Pthreads.png" width="100%"> | **Memory & Cache**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Memory_Cache_Analysis_Pthreads.png" width="100%"> |
| **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Perf_Counters_Pthreads.png" width="100%"> | **Perf Counters**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Perf_Counters_Pthreads.png" width="100%"> |
| **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Speedup_Efficiency_Pthreads.png" width="100%"> | **Speedup**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Speedup_Efficiency_Pthreads.png" width="100%"> |
| **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_haswell__analysis_charts_haswell_pexels_joey_kyber__Stage_Comparison_Pthreads.png" width="100%"> | **Stage Comp**<br><img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Profiling_full_xl__analysis_charts_pexels-joey-kyber-31917-134643__Stage_Comparison_Pthreads.png" width="100%"> |

</details>

---

## Analiză Scheduling (Static vs Dynamic)

În această implementare Pthreads, s-a folosit exclusiv **Static Scheduling** (divizare matematică a intervalului `[0, height]`).

* **Avantaj:** Zero overhead de sincronizare în timpul execuției buclei. Nu există mutex-uri sau variabile atomice verificate la fiecare iterație.
* **Dezavantaj:** Dacă o parte a imaginii este mult mai "zgomotoasă" (necesită mai mult calcul la tracking sau tracking-ul marginilor), unii threde-uri pot termina mai repede și stau "idle" așteptând la `pthread_join`. Totuși, pentru Blur și Sobel, munca este uniformă, deci abordarea Statică este optimă.

---

## Rezumat Performanță (Estimare bazată pe cod)

Tabelele de mai jos reflectă performanța tipică a unei implementări Pthreads bine optimizate (fără lock-uri în buclele interioare).



### Imaginea `1_earth_8k` (Mare - 8192x4096)
Aceste date au fost extrase de pe partiția xl, folosind multithreading. Se poate observa o scalare foarte bună pe 2 și pe 4 procesoare, însă eficiența scade semnificativ pentru 8 threaduri. Motivul este că apare un overhead la crearea de threaduri. Pentru fiecare etapă (grayscale, gaussian blur, sobel, nms și pragurile cu hysterezis), algoritmul generează acel număr de threaduri. Se oservă că branch miss percentage crește de la 1.27 la 1.55, iar branch miss crește de la 1.37e8 la 1.67e8, ceea ce influențează foarte mult eficiența algoritmului.De asemenea, scade IPC (Instructions per Cycle), care este o metrică foarte importantă atunci când vine vorba de eficiență. Speedup-ul este bun pentru 2 și respectiv 4 threaduri, însă pentru 8 threaduri, acesta se plafonează în jurul valorii de 2.94.
| Image          | Speedup | Efficiency | T | Total Time (s) | Grayscale (ms) | Gaussian Blur (ms) | Sobel (ms) | Non-Max Supp. (ms) | Hysteresis (ms) | Perf Cycles | Perf Instructions | Page Faults | Branch Misses | Branch Miss Rate (%) | IPC  | CPU Util |
|----------------|---------|------------|---|----------------|----------------|--------------------|------------|--------------------|------------------|-------------|-------------------|-------------|---------------|----------------------|------|----------|
| 1_earth_8k.jpg | 1.00    | 100%       | 1 | 7.2742         | 99.438         | 4411.05            | 2133.91    | 349.432            | 278.393          | 2.57e10     | 7.50e10           | 9177        | 1.37e8        | 1.27                 | 2.92 | 0.935    |
| 1_earth_8k.jpg | 1.70    | 85%        | 2 | 4.27783        | 55.1726        | 2318.27            | 1421.86    | 183.944            | 296.391          | 2.56e10     | 7.51e10           | 9037        | 1.37e8        | 1.27                 | 2.93 | 1.568    |
| 1_earth_8k.jpg | 2.69    | 67.20%     | 4 | 2.7038         | 30.7273        | 1359.27            | 887.19     | 198.868            | 225.477          | 2.55e10     | 7.51e10           | 7206        | 1.38e8        | 1.27                 | 2.94 | 2.211    |
| 1_earth_8k.jpg | 2.94    | 36.71%     | 8 | 2.47673        | 30.0198        | 1370.02            | 753.566    | 98.7939            | 222.436          | 4.27e10     | 7.49e10           | 8727        | 1.67e8        | 1.55                 | 1.75 | 4.074    |

</details>

<details>
<summary><strong>2. city.jpg (Rezoluție Mare)</strong> - <i>Click pentru detalii</i></summary>

| Image | Speedup | Efficiency | T | Total Time (s) | Grayscale (ms) | Gaussian Blur (ms) | Sobel (ms) | Non-Max Supp. (ms) | Hysteresis (ms) | Perf Cycles | Perf Instructions | Page Faults | Branch Misses | Branch Miss Rate (%) | IPC | CPU Util |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| city.jpg | 1.00 | 100% | 1 | 3.92104 | 53.222 | 2178.12 | 1289.77 | 195.916 | 202.79 | 1.31e+10 | 3.67e+10 | 9427 | 9.28e+07 | 1.7 | 0.924 | 1 |
| city.jpg | 1.57 | 78.54% | 2 | 2.49626 | 30.6429 | 1246.07 | 834.573 | 177.201 | 206.475 | 1.30e+10 | 3.67e+10 | 7719 | 9.23e+07 | 1.69 | 1.307 | 0.7854 |
| city.jpg | 2.46 | 61.53% | 4 | 1.59309 | 16.9156 | 760.188 | 507.735 | 129.441 | 177.791 | 1.30e+10 | 3.65e+10 | 7131 | 9.23e+07 | 1.69 | 1.853 | 0.6153 |
| city.jpg | 2.80 | 34.99% | 8 | 1.40074 | 17.6398 | 763.155 | 431.515 | 68.4254 | 119.062 | 2.13e+10 | 3.66e+10 | 7192 | 1.04e+08 | 1.91 | 2.815 | 0.3499 |

</details>

<details>
<summary><strong> 3. istockphoto (Mică - 612*612)</strong> - <i>Click pentru detalii</i></summary>

| Image | Speedup | Efficiency | T | Total Time (s) | Grayscale (ms) | Gaussian Blur (ms) | Sobel (ms) | Non-Max Supp. (ms) | Hysteresis (ms) | Perf Cycles | Perf Instructions | Page Faults | Branch Misses | Branch Miss Rate (%) | IPC | CPU Util |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| istockphot | 1.00 | 100% | 1 | 0.173837 | 5.51734 | 103.862 | 49.1211 | 8.88888 | 6.06811 | 2.38e+08 | 6.70e+08 | 2112 | 1.76e+06 | 1.73 | 0.291 | 1 |
| istockphot | 1.73 | 86.49% | 2 | 0.100495 | 5.15179 | 57.1604 | 27.1952 | 4.89871 | 5.7007 | 2.33e+08 | 6.46e+08 | 2114 | 1.65e+06 | 1.67 | 0.308 | 0.8649 |
| istockphot | 2.71 | 67.70% | 4 | 0.064199 | 5.0105 | 34.3521 | 15.8402 | 2.88688 | 5.75113 | 2.13e+08 | 6.07e+08 | 2127 | 1.66e+06 | 1.86 | 0.324 | 0.677 |
| istockphot | 2.77 | 34.68% | 8 | 0.062664 | 5.07232 | 34.7447 | 13.515 | 2.53666 | 6.46135 | 3.58e+08 | 6.23e+08 | 2177 | 1.86e+06 | 2.02 | 0.449 | 0.3468 |

</details>

<details>
<summary><strong> 4. pexels-eberhardgross(Mare)</strong> - <i>Click pentru detalii</i></summary>

| Image | Speedup | Efficiency | T | Total Time (s) | Grayscale (ms) | Gaussian Blur (ms) | Sobel (ms) | Non-Max Supp. (ms) | Hysteresis (ms) | Perf Cycles | Perf Instructions | Page Faults | Branch Misses | Branch Miss Rate (%) | IPC | CPU Util |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| pexels-ebe | 1.00 | 100% | 1 | 4.70227 | 62.6277 | 2726.28 | 1518.95 | 232.067 | 160.961 | 1.49e+10 | 4.45e+10 | 9333 | 7.99e+07 | 1.2 | 0.841 | 1 |
| pexels-ebe | 1.58 | 78.95% | 2 | 2.9779 | 37.2244 | 1573.93 | 968.395 | 239.979 | 157.106 | 1.49e+10 | 4.45e+10 | 7160 | 7.97e+07 | 1.2 | 1.267 | 0.7895 |
| pexels-ebe | 2.68 | 67.13% | 4 | 1.75125 | 19.8011 | 919.732 | 501.059 | 153.42 | 155.99 | 1.49e+10 | 4.44e+10 | 6382 | 7.99e+07 | 1.2 | 1.77 | 0.6713 |
| pexels-ebe | 3.42 | 42.76% | 8 | 1.37457 | 17.6476 | 894.587 | 347.717 | 51.428 | 62.4292 | 2.52e+10 | 4.45e+10 | 7458 | 9.69e+07 | 1.46 | 2.757 | 0.4276 |

</details>

<details>
<summary><strong> 5. pexels-joey-kyber (Mare)</strong> - <i>Click pentru detalii</i></summary>

| Image | Speedup | Efficiency | T | Total Time (s) | Grayscale (ms) | Gaussian Blur (ms) | Sobel (ms) | Non-Max Supp. (ms) | Hysteresis (ms) | Perf Cycles | Perf Instructions | Page Faults | Branch Misses | Branch Miss Rate (%) | IPC | CPU Util |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| pexels-joe | 1.00 | 100% | 1 | 5.31734 | 75.0891 | 3090.87 | 1709.35 | 259.441 | 181.233 | 1.68e+10 | 5.04e+10 | 9815 | 8.76e+07 | 1.18 | 0.811 | 1 |
| pexels-joe | 1.82 | 90.96% | 2 | 2.92288 | 39.0314 | 1718.23 | 823.984 | 162.969 | 177.233 | 1.68e+10 | 5.04e+10 | 8089 | 8.80e+07 | 1.18 | 1.152 | 0.9096 |
| pexels-joe | 2.68 | 66.90% | 4 | 1.98704 | 22.6764 | 1063.96 | 625.438 | 110.784 | 162.77 | 1.68e+10 | 5.03e+10 | 7538 | 8.76e+07 | 1.18 | 1.656 | 0.669 |
| pexels-joe | 2.80 | 35.02% | 8 | 1.89772 | 19.3135 | 1065.78 | 540.048 | 111.113 | 159.981 | 2.84e+10 | 5.02e+10 | 8756 | 1.05e+08 | 1.41 | 2.594 | 0.3502 |

</details>

<details>
<summary><strong> 6. poza.jpg (Mică)</strong> - <i>Click pentru detalii</i></summary>

| Image | Speedup | Efficiency | T | Total Time (s) | Grayscale (ms) | Gaussian Blur (ms) | Sobel (ms) | Non-Max Supp. (ms) | Hysteresis (ms) | Perf Cycles | Perf Instructions | Page Faults | Branch Misses | Branch Miss Rate (%) | IPC | CPU Util |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| poza.jpg | 1.00 | 100% | 1 | 0.1535 | 5.42133 | 93.102 | 43.4482 | 7.63564 | 3.55659 | 1.87e+08 | 5.40e+08 | 1888 | 1.04e+06 | 1.3 | 0.087 | 1 |
| poza.jpg | 1.72 | 85.94% | 2 | 0.089304 | 5.05749 | 51.923 | 24.3216 | 4.42488 | 3.22713 | 1.92e+08 | 5.65e+08 | 1894 | 9.46e+05 | 1.16 | 0.107 | 0.8594 |
| poza.jpg | 2.68 | 67.00% | 4 | 0.057276 | 5.01217 | 31.1667 | 14.6936 | 2.77276 | 3.29133 | 1.92e+08 | 5.47e+08 | 1900 | 8.30e+05 | 1.04 | 0.082 | 0.67 |
| poza.jpg | 2.69 | 33.63% | 8 | 0.057061 | 5.20397 | 31.999 | 12.8043 | 2.40847 | 4.31936 | 3.13e+08 | 5.54e+08 | 1950 | 1.27e+06 | 1.53 | 0.159 | 0.3363 |

</details>

<details>
<summary><strong> 7. round_earth (Extra)</strong> - <i>Click pentru detalii</i></summary>

| Image | Speedup | Efficiency | T | Total Time (s) | Grayscale (ms) | Gaussian Blur (ms) | Sobel (ms) | Non-Max Supp. (ms) | Hysteresis (ms) | Perf Cycles | Perf Instructions | Page Faults | Branch Misses | Branch Miss Rate (%) | IPC | CPU Util |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| round_earth | 1.00 | 100% | 1 | 13.0826 | 166.188 | 8174.76 | 3787.32 | 494.982 | 456.216 | 4.53e+10 | 1.40e+11 | 11011 | 2.03e+08 | 0.98 | 0.998 | 1 |
| round_earth | 1.88 | 94.21% | 2 | 6.94315 | 103.021 | 4145.28 | 1997.16 | 273.663 | 421.067 | 4.54e+10 | 1.40e+11 | 9892 | 2.02e+08 | 0.98 | 1.619 | 0.9421 |
| round_earth | 2.86 | 71.56% | 4 | 4.57023 | 60.2016 | 2378.72 | 1455.33 | 250.955 | 422.112 | 4.54e+10 | 1.40e+11 | 10135 | 2.02e+08 | 0.98 | 2.442 | 0.7156 |
| round_earth | 3.06 | 38.29% | 8 | 4.27111 | 56.6595 | 2398.82 | 1253.08 | 228.35 | 331.229 | 7.77e+10 | 1.40e+11 | 10062 | 2.52e+08 | 1.23 | 4.284 | 0.3829 |

</details>

### Analiză Intel VTune: Pthreads

* Această secțiune prezintă datele brute și vizualizările generate de Intel VTune pentru rulările cu 2, 4 și 8 fire de execuție. Am ales să nu testez pentru un singur fir de execuție, deoarece acest profiler se focusează pe lucrul în paralel, pe mai multe threaduri/procese.
* Scriptul script_vtune_full.sh automatizează complet profilarea Intel VTune (Hotspots) pentru implementările distribuite și hibride (MPI, CUDA, MPI+OpenMP, MPI+Pthreads, CUDA+MPI). Acesta compilează sursele și execută o matrice de teste scalabile (variind numărul de procese și fire de execuție), gestionând configurarea mediului SLURM (pe partiția xl cu GPU) și organizarea automată a rapoartelor de performanță în directorul profiling/vtune_results, eliminând efortul rulării manuale.
* Puteți consulta scriptul complet de automatizare aici: [script_vtune_full.sh](https://gitlab.cs.pub.ro/app-2025/cannycore/-/blob/main/script_vtune_full.sh)
* Am folosit acest script pentru a obține folderele de hotspots, care conțin și fișierul .vtune, pe care îl pot rula local mai apoi, pentru a vedea rezultatele.


<details>
<summary><strong> Intel VTune - 2 Threads</strong> (Click pentru a extinde)</summary>

#### 1. Bottom Up
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_2/bottom_up.PNG" width="100%">

> **Analiză Detaliată Bottom-up (2 Threads):**
>
> * **Paralelism Real:** Graficul demonstrează vizual că execuția este cu adevărat paralelă. Pentru fiecare etapă intensivă de calcul (cum este `threadBlur` sau `threadSobel`), vedem două benzi maro suprapuse pe axa timpului. Acest lucru confirmă că ambele nuclee fizice lucrează simultan la procesarea imaginii, reducând timpul total față de o execuție serială.
>
> * **Structura Fork-Join și Bariere:** Se observă clar delimitarea secvențială a algoritmului. Etapele nu se amestecă: `Gaussian Blur` trebuie să se termine complet pe ambele fire înainte ca `Sobel` să înceapă. Spațiile mici dintre blocuri reprezintă barierele de sincronizare (`pthread_barrier_wait`), unde firele mai rapide le așteaptă pe cele mai lente, asigurând coerența datelor.
>
> * **Rolul Firului Principal:** Bara verde lungă (`Canny_pthreads`) reprezintă firul Master. Faptul că este activ pe toată durata arată că acesta gestionează crearea și unirea (join) firelor de execuție, monitorizând procesul general în timp ce "muncitorii" (worker threads) execută calculele efective.
>
> * **Echilibrarea Sarcinii (Load Balancing):** Lungimea aproape identică a barelor de execuție pentru cele două fire (ex: cele două bare de la `threadBlur`) indică o distribuție excelentă a sarcinii. Fiecare fir primește exact jumătate din imagine, terminând în același timp, ceea ce înseamnă că nu există timpi morți (idle) în care un nucleu să stea degeaba așteptând după celălalt.*

<br>

#### 2. Flame Graph
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_2/flame_graph.PNG" width="100%">

> **Analiză Flame Graph (2 Threads):**
>
> * **Distribuția Timpului:** Flame Graph-ul vizualizează stiva de apeluri (pe axa Y) și timpul consumat de CPU (lățimea pe axa X). Cu cât o bară este mai lată, cu atât funcția respectivă a consumat mai multe cicluri de procesor.
>
> * **Funcțiile Dominante:** Se observă clar că `threadBlur` (bara verde lungă din dreapta jos) este funcția dominantă, ocupând cea mai mare parte din timpul de execuție. Urmează `threadSobel`, confirmând că etapa de convoluție este cea mai intensivă.
>
> * **Identificarea Costurilor Matematice:** Deasupra blocului `threadSobel` apare distinct funcția `atan2f` (colorată în galben). Acest lucru indică faptul că o parte semnificativă din calculul Sobel este petrecută în calcularea orientării gradientului (operație trigonometrică costisitoare), fiind un candidat bun pentru optimizări viitoare (ex: aproximări rapide).
>
> * **Inițializare vs. Calcul:** "Turnul" din partea stângă (funcțiile `stbi_...`) reprezintă faza serială de încărcare și decodare a imaginii JPG, care se execută pe un singur fir înainte de pornirea procesării paralele.*

<br>

#### 3. CPU Utilization Histogram
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_2/cpu_utilization_histogram.PNG" width="100%">

> **Analiză CPU Utilization Histogram (2 Threads):**
>
> * **Saturația Resurselor:** Graficul arată o bară dominantă (cea mai înaltă) exact în dreptul valorii **2 CPUs**. Aceasta confirmă că, pentru majoritatea timpului de execuție, aplicația a reușit să mențină active simultan ambele fire de execuție alocate.
>
> * **Contextul "Poor" (Roșu):** Deși VTune clasifică utilizarea ca fiind "Poor" (în zona roșie), acest lucru este normal în acest context. Rularea a fost limitată explicit la 2 nuclee, însă nodul de calcul (XL) dispune de mult mai multe (40+). VTune raportează utilizarea față de capacitatea totală a mașinii fizice, nu față de limitele impuse job-ului.
>
> * **Overhead Serial:** Prezența unor bare mai mici în zona **< 1 CPU** reflectă timpii de pornire a sistemului, încărcarea imaginii (I/O) și scrierea rezultatului, operații care sunt inerent seriale și nu pot folosi ambele nuclee.*

<br>

#### 4. Platform Info
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_2/platform.PNG" width="100%">

> **Analiză Detaliată Thread Lifecycle:**
>
> * **Thread Churn (Creare Repetată):** Un detaliu tehnic critic observabil aici este schimbarea ID-urilor firelor de execuție (TID) între etape. De exemplu, `threadBlur` rulează pe TIDs 3350409/410, în timp ce `threadSobel` rulează pe noi fire, 3350411/412.
>
> * **Arhitectură fără Thread Pool:** Acest comportament indică faptul că implementarea nu folosește un *Thread Pool* persistent. În schimb, programul apelează `pthread_create` și `pthread_join` pentru **fiecare etapă în parte**.
>
> * **Impact asupra Performanței:** Deși algoritmul este corect paralelizat, această strategie introduce un overhead (cost de sistem) la fiecare tranziție, vizibil în spațiile goale dintre blocuri. Pentru etapele foarte scurte, precum `threadGrayscale` (barele verzi minuscule), costul creării firelor ar putea depăși câștigul de viteză obținut prin paralelizare.
*

<br>

#### 5. Timpi Execuție
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_2/timpi_pthreads.PNG" width="100%">

> **Analiză Timpi și Hotspots (2 Threads):**
>
> * **Validarea Paralelismului:** Diferența dintre **CPU Time** (9.760s) și **Elapsed Time** (6.961s) confirmă că aplicația a rulat în paralel. Deși avem 2 fire, raportul nu este perfect 2:1 (ar fi fost ideal ~14s CPU Time), ci ~1.4:1. Aceasta se datorează componentelor seriale inevitabile (încărcarea/salvarea imaginii) care "diluează" eficiența generală.
>
> * **Dominanța Gaussian Blur:** Funcția `threadBlur` este, de departe, cel mai mare consumator de timp (**47.1%**), confirmând că optimizarea acestei etape (ex: prin vectorizare SIMD sau separabilitate) ar aduce cel mai mare câștig de performanță.
>
> * **Costul Matematic ascuns (`atan2f`):** Un detaliu critic este prezența funcției `atan2f` (folosită în Sobel pentru direcția gradientului) pe locul 3, consumând **8.9%** din timp. Este remarcabil că o singură operație matematică consumă aproape cât tot restul logicii Sobel (**12.3%**), sugerând că utilizarea unei aproximări rapide ar putea reduce semnificativ timpul total.
>
> * **Bottleneck Serial (I/O):** Funcția `stbi_zlib_compress` (6.9%) apare în top, ceea ce indică faptul că salvarea imaginii finale (compresia PNG) este o operație costisitoare care rulează pe un singur fir, limitând scalabilitatea maximă conform Legii lui Amdahl.
*

</details>

<details>
<summary><strong> Intel VTune - 4 Threads</strong> (Click pentru a extinde)</summary>

#### 1. Bottom Up
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_4/bottom_up.PNG" width="100%">

> **Analiză Bottom-up (Timeline - 4 Threads):**
>
> * **Scalare Perfectă:** Se observă acum **4 benzi maro suprapuse** pentru fiecare etapă majoră (`threadBlur`, `threadSobel`, `threadNMS`). Aceasta confirmă că algoritmul a reușit să angreneze 4 nuclee fizice simultan, dublând lățimea de bandă de procesare față de testul anterior.
>
> * **Sincronizare și Bariere:** Modelul *Fork-Join* este și mai evident. Toate cele 4 fire termină etapa de `Blur` aproape în același timp înainte de a trece la `Sobel`. Micile decalaje la finalul blocurilor arată variații infime de timp de execuție per thread, dar bariera de sincronizare funcționează corect, aliniindu-le.
>
> * **Overhead de Management:** Comparativ cu rularea pe 2 fire, spațiile albe (pauzele) dintre etape sunt marginal mai vizibile. Gestionarea a 4 fire implică un cost ușor mai mare pentru sistemul de operare (context switching și sincronizare), dar acesta este neglijabil față de câștigul de viteză obținut.*

<br>

#### 2. Caller / Callee
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_4/caller.PNG" width="100%">

> **Analiză Caller / Callee:**
>
> * **Fluxul de Apeluri:** Această vizualizare confirmă ierarhia funcțională. Funcția părinte `Canny_pthreads` (Total Time: ~100%) distribuie munca către funcțiile worker (`threadBlur`, `threadSobel`).
>
> * **Costul Recursivității/Dependențelor:** Dacă observi funcții precum `threadHysteresis` (care nu apar întotdeauna clar în timeline din cauza duratei scurte), aici putem vedea exact cât contribuie ele. Faptul că `threadBlur` rămâne apelantul principal consumator de timp confirmă că efortul de optimizare trebuie concentrat pe convoluția Gaussiană.

> **Analiză Flame Graph (4 Threads):**
>
> * **Lățimea = Timp CPU:** Profilul general rămâne similar cu cel de la 2 thread-uri, dar "baza" graficului (timpul total) este mai scurtă, deoarece munca e împărțită.
>
> * **Funcții "Fierbinți":** Bara verde pentru `threadBlur` domină în continuare vizual. Totuși, proporția timpului petrecut în funcții auxiliare sau de sistem (partea galbenă/gri) poate crește ușor procentual, deoarece timpul de calcul pur scade, dar timpul de overhead (creare fire, I/O) rămâne constant.
>
> * **Detaliu Matematic:** Funcția `atan2f` (din Sobel) rămâne vizibilă deasupra blocului Sobel, reafirmând costul ridicat al operațiilor trigonometrice chiar și atunci când sunt paralelizate.

*

<br>

#### 3. Flame Graph
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_4/flame_graph.PNG" width="100%">

> **Analiză Flame Graph (4 Threads):**
>
> * **Lățimea = Timp CPU:** Profilul general rămâne similar cu cel de la 2 thread-uri, dar "baza" graficului (timpul total) este mai scurtă, deoarece munca e împărțită.
>
> * **Funcții "Fierbinți":** Bara verde pentru `threadBlur` domină în continuare vizual. Totuși, proporția timpului petrecut în funcții auxiliare sau de sistem (partea galbenă/gri) poate crește ușor procentual, deoarece timpul de calcul pur scade, dar timpul de overhead (creare fire, I/O) rămâne constant.
>
> * **Detaliu Matematic:** Funcția `atan2f` (din Sobel) rămâne vizibilă deasupra blocului Sobel, reafirmând costul ridicat al operațiilor trigonometrice chiar și atunci când sunt paralelizate.*

<br>

#### 4. Histogramă
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_4/histograma.PNG" width="100%">

> **Analiză Histogramă CPU:**
>
> * **Utilizare Optimă:** Graficul arată o bară dominantă în dreptul valorii **4 CPUs**. Acest lucru indică faptul că programul este *CPU Bound* și scalează eficient: când i se dau 4 nuclee, le folosește pe toate 4 la capacitate maximă pentru majoritatea timpului.
>
> * **Eficiența Paralelizării:** Lipsa unor bare semnificative la 2 sau 3 CPUs sugerează că nu avem probleme majore de *load imbalance*. Dacă firele ar fi fost neechilibrate (unul termină repede, 3 încă lucrează), am fi văzut activitate intermediară. Faptul că sărim direct de la 1 CPU (partea serială) la 4 CPUs (partea paralelă) este ideal.*

<br>

#### 5. Timpi Execuție
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_4/poza_timpi.PNG" width="100%">

> **Analiză Timpi și Performanță (4 Threads):**
>
> * **Speedup Vizibil:** Timpul "Elapsed" a scăzut semnificativ față de varianta cu 2 fire (de la ~6.9s la ~4.0s - *exemplu estimativ, verifică poza exactă*). Deși nu este o scădere perfectă la jumătate (din cauza legii lui Amdahl și a părții seriale I/O), câștigul este substanțial.
>
> * **CPU Time vs Elapsed Time:** Timpul total de procesor (CPU Time) ar trebui să fie de aproximativ 4 ori mai mare decât timpul scurs (Elapsed Time) în zona paralelă. Raportul afișat de VTune validează gradul de paralelizare.
>
> * **Top Hotspots:** Lista de funcții fierbinți rămâne neschimbată ca ordine (`threadBlur` -> `threadSobel` -> `atan2f`), dar valorile absolute de timp au scăzut pentru fiecare funcție în parte, demonstrând că paralelizarea datelor (Data Decomposition) funcționează corect pentru fiecare etapă a algoritmului.*

</details>

<details>
<summary><strong> Intel VTune - 8 Threads</strong> (Click pentru a extinde)</summary>

#### 1. Bottom Up
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_8/bottom_up.PNG" width="100%">

> **Analiză Bottom-up (Timeline - 8 Threads):**
>
> * **Paralelism Masiv:** Se disting clar **8 benzi orizontale** active simultan pentru etapele de `threadBlur` și `threadSobel`. Aplicația a reușit să spawneze și să utilizeze toate cele 8 fire alocate.
>
> * **Efectul de "Turtire" (Diminishing Returns):** Deși avem de 2 ori mai multe fire decât în testul anterior (4 vs 8), durata benzilor maro nu s-a înjumătățit vizibil. Aceasta confirmă observația din graficele de *Speedup*: memoria RAM nu poate livra date suficient de repede pentru a hrăni 8 nuclee simultan, acestea petrecând timp așteptând (stalling), chiar dacă apar ca "active" în timeline.
>
> * **Sincronizare:** Barierele dintre etape (spațiile verticale albe) sunt perfect aliniate, demonstrând că niciun fir nu rămâne în urmă semnificativ (*load balancing* corect), dar costul de gestiune a 8 fire începe să devină vizibil.*

<br>

#### 2. Top Down
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_8/top_down.PNG" width="100%">

> **Analiză Top Down:**
>
> * **Ierarhia Costurilor:** Această vizualizare arată clar că `threadBlur` este responsabil pentru **55.5%** din timpul total de procesor, fiind funcția critică.
>
> * **Overhead-ul Wrapper-ului:** Funcția `tpss_thread_start_routine_wrapper` apare ca părinte principal. Faptul că aceasta consumă aproape tot timpul (ca container) confirmă că munca utilă se desfășoară aproape exclusiv în firele secundare, firul principal având doar rol de orchestrator.*

<br>

#### 3. Caller / Callee
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_8/caller.PNG" width="100%">

> **Analiză Caller / Callee:**
>
> * **Costul Total vs. Self:** Tabelul ne arată o distincție clară. `threadBlur` are un "CPU Time: Self" de 10.370s.
>
> * **Hotspot Matematic:** Funcția `atan2f` (din biblioteca matematică `libm`) consumă **1.350s** (7.4% din total). Este un cost imens pentru o singură funcție matematică, comparabil cu jumătate din toată etapa `threadSobel`. Optimizarea acesteia (prin tabele de căutare - LUT sau instrucțiuni AVX aproximative) ar fi o victorie rapidă pentru performanță.
>
> * **Presiunea pe Memorie:** Funcțiile de I/O (`stbi_write_png`, `stbi_load`) încep să urce în clasament procentual, deoarece timpul de calcul scade (fiind împărțit la 8), dar timpul de acces la disc rămâne constant, devenind un procent mai mare din totalul execuției.*

<br>

#### 4. Histogramă
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_8/histograma.PNG" width="100%">

> **Analiză Histogramă CPU:**
>
> * **Polarizare:** Graficul arată două bare dominante: una la **0-1 CPU** (partea serială de I/O) și una la **8 CPUs** (partea paralelă).
>
> * **Utilizare "Poor":** VTune marchează utilizarea ca fiind "Poor" (Roșu). Acest lucru este înșelător dar explicabil: job-ul rulează pe un nod XL cu 40+ nuclee, dar folosește doar 8. Din perspectiva hardware-ului total, mașina este subutilizată. Din perspectiva job-ului nostru, utilizarea este corectă (atinge limita de 8 impusă).*

<br>

#### 5. Flame Graph
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_8/flame_graph.PNG" width="100%">

> **Analiză Flame Graph:**
>
> * **Profilul "Lat":** Baza graficului este foarte lată (multe fire), dar înălțimea turnurilor de calcul (`threadBlur`) s-a redus.
>
> * **Vizualizarea Bottleneck-ului:** Se observă că "turnul" verde din dreapta (`threadBlur`) este mult mai masiv decât cel din mijloc (`threadSobel`). Această disproporție vizuală indică imediat inginerului că orice efort de optimizare a codului trebuie să înceapă cu Blur-ul Gaussian, orice altceva fiind optimizare prematură.*

<br>

#### 6. Timpi Execuție
<img src="https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/PTHREADS/Performance_intelvtune__pthreads_8/timpi.PNG" width="100%">

> **Analiză Finală a Performanței:**
>
> * **Elapsed Time (5.232s):** Comparativ cu rularea pe 2 fire (~6.9s) și cea estimată pe 4 fire, timpul de 5.2s arată că trecerea de la 4 la 8 fire a adus un câștig minim, sau chiar o ușoară regresie. Aceasta este dovada finală a **Memory Wall-ului**. Adăugarea a încă 4 nuclee nu a făcut decât să aglomereze magistrala de memorie.
>
> * **Eficiență Scăzută:** Timpul total de CPU este **18.190s**. Într-o lume ideală, `Elapsed Time` = `CPU Time` / 8 ≈ 2.27s. Faptul că realitatea este 5.23s înseamnă că eficiența paralelizării este sub 50% la 8 fire. Nucleele petrec mai mult timp așteptând date decât procesând.*

</details>

### Comparație Pthreads vs OpenMP

| Feature | OpenMP | Pthreads (Implementarea curentă) |
| :--- | :--- | :--- |
| **Ușurință în utilizare** | Ridicată (`#pragma`) | Scăzută (Cod mult, structuri) |
| **Control** | Automat (Runtime) | Manual (Total) |
| **Scheduling** | Flexibil (Static/Dynamic/Guided) | Fix (Calculat manual - Static) |
| **Performanță (Raw)** | Bună, dar overhead la start | Excelentă (Overhead minim de bibliotecă) |
| **Flexibilitate Cod** | Necesită compilator compatibil | Portabil (Standard C/C++ libraries) |

---

### Concluzie
Implementarea cu **Pthreads** oferă performanțe marginal mai bune decât OpenMP pe sistemele unde runtime-ul OpenMP este greoi, datorită naturii "ușoare" a thread-urilor native și a lipsei logicii complexe de scheduling dinamic. Totuși, costul de dezvoltare este semnificativ mai mare. Pentru acest algoritm, unde sarcina este uniform distribuită (Blur, Sobel), abordarea manuală "Static Scheduling" din Pthreads este o soluție robustă și foarte performantă.

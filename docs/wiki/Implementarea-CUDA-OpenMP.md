## Implementare Hibridă: CUDA + OpenMP

Spre deosebire de implementarea MPI, care s-a concentrat pe descompunerea domeniului pentru a accelera procesarea unei singure imagini de dimensiuni mai mari, implementarea **CUDA + OpenMP** abordează un alt scenariu: procesarea unui volum mare de imagini într-un timp cât mai scurt.

Aici, paralelizarea nu se face *în interiorul* imaginii, ci *între* imagini. Practic, fiecare thread CPU gestionează complet fluxul de procesare pentru o imagine distinctă.

### Detalii de Implementare
Această implementare utilizează următoarele strategii:

* **Paralelizarea Buclei de Fișiere:** Am utilizat directiva `#pragma omp parallel for schedule(dynamic)` pentru a distribui imaginile din folderul de input către thread-urile disponibile.
    * *Notă:* Tipul de scheduling `schedule(dynamic)` este esențială deoarece imaginile pot avea dimensiuni diferite; un thread care termină o imagine mică va prelua imediat alta, asigurând un load balancing eficient.
* **Context Management pe GPU:** Fiecare thread OpenMP își alocă propriile resurse pe GPU (`cudaMalloc`, stream-uri implicite). Driverul NVIDIA gestionează execuția concurentă a kernel-urilor provenite de la thread-uri diferite.
* **Thread Safety la I/O:** Deoarece mai multe thread-uri încearcă să scrie rezultate în consolă simultan, am protejat aceste secțiuni critice folosind `#pragma omp critical` pentru a preveni coruperea output-ului.

Flow-ul execuției pentru fiecare thread OpenMP este independent:
```text
Thread i: Citește Imaginea X
Thread i: Alocă memorie GPU & Transfer H2D
Thread i: Lansează Pipeline Canny (Grayscale...Hysteresis)
Thread i: Transfer D2H & Salvare Imagine X
Thread i: Eliberează memorie & Trece la următoarea imagine
```

## Provocări întâmpinate

* **Disk I/O Bottleneck:** GPU-ul este extrem de rapid, procesează o imagine în milisecunde, dar citirea fișierelor de pe disc este lentă. Când 8 thread-uri încearcă să citească simultan, banda I/O este saturată, limitând viteza cu care GPU-ul este alimentat cu date.
* **Overhead-ul de Context Switching:** există un cost pentru comutarea între contextul thread-ului 1 și thread-ului 2, mai ales când numărul de thread-uri depășește capacitatea de execuție simultană a GPU-ului.

## Profiling & Rezultate obținute (OpenMP)

Testele au fost realizate procesând întregul set de imagini, schimbând numărul de thread-uri OpenMP (de la 1 la 8). Block size-ul a rămas constant 16x16.

### 1. Throughput vs. Latency

Acesta este cel mai interesant rezultat al implementării de tip batch.

![Throughput vs Latency](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_openmp/throughput_vs_latency.png)

Graficul de mai sus ilustrează un compromis fundamental în calculul paralel:

* **Throughput (barele verzi):** Numărul de imagini procesate pe secundă crește odată cu numărul de thread-uri, atingând un vârf la **4 Thread-uri (2.25 FPS)**.
* **Latența per Imagine (Linia Roșie):** Timpul necesar pentru a procesa o singură imagine crește constant (de la ~160ms la ~240ms).

Deși terminăm tot folderul mai repede, fiecare imagine individuală stă mai mult timp în așteptare din cauza aglomerației pe GPU. La 8 thread-uri, throughput-ul scade (2.08 FPS), semn că are loc un oversubscription al GPU-ului, adică resursele cerute de thread-uri sar de limita maximă posibilă.

### 2. Scalabilitate și Eficiență

![Speedup Efficiency](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_openmp/speedup_efficiency.png)
![Elapsed Time](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_openmp/elapsed_time.png)

Graficele de Speedup și Timp Total arată o scalare bună până la 4 thread-uri (Speedup ~2.5x), urmată de o plafonare.
Eficiența scade puternic la 8 thread-uri (~28%). Acest lucru indică faptul că **4 thread-uri** reprezintă punctul optim pentru acest sistem. Peste această valoare, thread-urile se blochează reciproc așteptând acces la disc/GPU.

### 3. Analiza pe Etape (De ce crește latența?)

![Stage Duration vs Threads](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_openmp/stage_comparison.png)

Graficul de mai sus arată durata medie a fiecărei etape a algoritmului în funcție de numărul de thread-uri concurente:

* **Hysteresis (Linia Verde):** Durata acestei etape crește cel mai mult (de la ~65ms la ~95ms). Deoarece este un kernel de lungă durată, acesta suferă cel mai mult din cauza partajării resurselor GPU.
* **Etapele scurte (Sobel, Blur):** Rămân relativ constante, fiind executate prea rapid pentru a fi afectate major de context switching.

### 4. Impactul asupra Resurselor Hardware

Analiza contoarelor de performanță explică degradarea performanței la un număr mare de thread-uri.

![Hardware Counters](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_openmp/hardware_counters.png)

IPC-ul scade de la **2.7** la **2.3** atunci când trecem la 8 thread-uri. Procesorul întâmpină dificultăți în a gestiona fluxul de instrucțiuni pentru instanțe.
   
![Memory Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_openmp/memory_analysis.png)
    Se observă o creștere a ratei de branch miss și page faults la 4 thread-uri, cauzată de presiunea pe memoria RAM și cache-ul procesorului, fiecare thread lucrând cu zone de memorie complet diferite (imagini diferite).

## 5. Analiza Datelor și Anomalii

Analizând log-urile detaliate de execuție, am identificat trei fenomene critice care justifică deciziile de arhitectură și explică comportamentul sistemului.

### A. Justificarea pentru `schedule(dynamic)`

Tabelul de mai jos compară timpul de execuție pentru diferite imagini pe un singur thread CPU:

| Imagine (Input) | Timp Total Procesare (s) | Observații |
| :--- | :--- | :--- |
| `poza.png` | **0.0019 s** | Foarte mică (procesare instantă) |
| `city.png` | 0.0548 s | Medie |
| `round_earth.png` | **0.2406 s** | Mare (complexă) |

Există un raport de **~120x** între cea mai rapidă și cea mai lentă imagine. Fără o planificare dinamică (`schedule(dynamic)`), thread-urile care primesc imagini mai complexe ar bloca execuția, în timp ce restul ar sta degeaba.

### B. Costul inițial al GPU-ului

Un detaliu observat în fișierul de loguri este costul de inițializare pentru CUDA. Primul apel către GPU pe un thread este întotdeauna mult mai lent decât următoarele.

| Ordine Execuție | Imagine | Timp Alocare GPU (ms) | Explicație |
| :--- | :--- | :--- | :--- |
| **Prima Imagine** | `istockphoto...` | **341.7 ms** | Inițializare Context CUDA |
| A doua imagine | `city` | **0.9 ms** | Context deja existent (Reuse) |
| A treia imagine | `round_earth` | **0.9 ms** | Alocare rapidă |

La rularea cu 8 thread-uri, acest cost inițial se plătește de 8 ori (o dată per thread). Totuși, în procesarea unui volum mare de imagini, acest cost devine neglijabil.

### C. Profilul de Execuție

Din punct de vedere al timpului efectiv petrecut pe GPU, algoritmul este dominat de o singură etapă. Tabelul de mai jos arată contribuția medie a fiecărei etape la timpul total:

| Etapă Pipeline | Timp Mediu (ms) | Procent din Total |
| :--- | :--- | :--- |
| **Hysteresis** | **~90 ms** | **~45%** |
| Gaussian Blur | ~15 ms | ~7% |
| Sobel Gradient | ~2 ms | < 1% |
| Non-Max Suppression | ~2 ms | < 1% |
| Altele (Memcpy, Alloc) | Variabil | ~47% |

Datele din tabel confirmă faptul că kernel-ul de _Hysteresis_ reprezintă principalul bottleneck computațional al algoritmului

## Concluzii (OpenMP)

Implementarea **CUDA + OpenMP** demonstrează eficiența procesării de tip **Batch**:

1.  **Best Config:** Configurația optimă pe sistemul de test este de **4 Thread-uri** concurente. Aceasta maximizează utilizarea GPU-ului fără a satura banda de I/O.
2.  **Trade-off:** Am obținut un throughput mai mare (mai multe imagini/secundă) cu prețul unei latențe crescute per imagine.
3.  **Limitare:** Spre deosebire de MPI (limitat de rețea), aici limitarea principală tinde să fie **viteza de citire de pe disc** și capacitatea GPU-ului de a gestiona contexte multiple simultan.
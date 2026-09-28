## Implementare Hibridă: CUDA + MPI

Implementarea **CUDA + MPI** extinde paralelizarea algoritmului Canny Edge Detection de la nivelul unui singur dispozitiv la nivelul unui cluster de calcul, folosind memoria distribuită. Această abordare permite procesarea unor imagini de dimensiuni foarte mari (care nu ar încăpea în memoria unui singur GPU) sau accelerarea procesării unei imagini prin împărțirea sarcinii între mai multe noduri/GPU-uri.

Modificările și adaptările specifice față de varianta CUDA simplă sunt:

* **Decompunerea Imaginii** - Imaginea este împărțită în fâșii orizontale. Fiecare proces MPI este responsabil de procesarea unei singure fâșii, reducând astfel încărcarea pe fiecare GPU individual.
* **Gestionarea Zonelor de Overlap** - Deoarece operațiile de convoluție (ex. _Gaussian Blur_, _Sobel_) necesită acces la vecinii pixelilor, simpla împărțire a imaginii ar crea artefacte la margini. Am implementat un mecanism de *overlap*, unde fiecare proces primește fâșia sa plus `kernelSize / 2` rânduri suplimentare de la vecini (sus/jos) pentru a asigura continuitatea calculelor matematice (kernel-ul utilizat de _Gaussian Blur_ se aplică pe o zonă de `kernelSize x kernelSize` centrată în pixelul curent, ceea ce implică necesitatea accesării pixelilor din afara fâșiei locale la margini).
* **Distribuția și Colectarea Datelor** - Procesul _Master_ (Rank 0) se ocupă de citirea imaginii folosind `stb_image` și trimiterea datelor brute către celelalte procese (`MPI_Bcast`). La final, rezultatele parțiale (doar pixelii valizi, fără zonele de overlap) sunt reasamblate folosind `MPI_Gatherv` pentru a reconstrui imaginea finală.
* **Pipeline Local pe GPU** - Odată ce un proces MPI și-a primit bucata de date, acesta execută pipeline-ul CUDA (Grayscale -> Hysteresis) local, exact ca în varianta CUDA normală, dar pe un subset de date (`localHeight * width`).

Flow-ul execuției distribuite este următorul:
```
Rank 0 (CPU): Încarcă imaginea completă
MPI Communication: Broadcast date brute către toate nodurile
Fiecare Rank (CPU): Calculează indicii de start/stop + overlap
Fiecare Rank (GPU): Transfer H2D (doar fâșia locală) → Pipeline CUDA → Transfer D2H
MPI Communication: Gather (Rank 0 colectează fâșiile procesate)
Rank 0 (CPU): Reasamblare și Salvare imagine
```

## Provocări întâmpinate

1. **Calculul Overlap-ului** - Determinarea corectă a indicilor pentru zonele de margine a fost critică. O greșeală de un singur rând ar fi dus la linii negre orizontale în imaginea finală sau la acces nevalid la memorie.
2. **Hysteresis la granițe** - Algoritmul de Hysteresis urmărește muchiile iterativ. Într-o arhitectură distribuită, dacă o muchie "puternică" trece dintr-un proces în altul, continuitatea urmăririi se pierde la granița fâșiei (deoarece procesele nu comunică între ele în timpul kernel-ului de Hysteresis). Aceasta este o limitare asumată a implementării curente MPI.
3. **Overhead-ul comunicării** - Pentru imagini mici, timpul necesar pentru `MPI_Bcast` și `MPI_Gatherv` depășește câștigul obținut prin paralelizare. Beneficiul real se observă la rezoluții masive (ex: imagini 8K precum _1_earth_8k.png_).

## Profiling & Rezultate obținute (MPI)

Testele au fost realizate pe cluster, folosind imaginea _1_earth_8k.png_, modificând numărul de procese MPI. Fiecare proces a rulat concurent pe GPU-ul disponibil, block size-ul ales fiind 16x16.

### 1. Scalabilitate și Eficiență

![Speedup Efficiency](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_mpi/speedup_efficiency_cuda_mpi.png)

Graficul de mai sus ilustrează o **scalare negativă** atunci când măsurăm timpul total de execuție (inclusiv alocarea resurselor).
* **Speedup-ul real** scade sub 1.0, ajungând la **0.44x** în cazul utilizării a 4 procese.
* **Eficiența** se degradează drastic, ajungând la **11.1%**.

Acest comportament indică faptul că, pe un singur nod fizic, overhead-ul introdus de gestionarea a 4 contexte CUDA simultane și serializarea alocării de memorie (`cudaMalloc`) depășește complet câștigul obținut prin împărțirea calculelor.

### 2. Identificarea Bottleneck-ului (Analiza pe Etape)

![Stage Comparison](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_mpi/stage_comp.png)

Descompunerea timpului pe etape explică clar pierderea de eficiență:
* **Hysteresis (Linia Portocalie):** Timpul scade aproape ideal pe măsură ce creștem numărul de nuclee. Acest lucru confirmă că **paralelizarea algoritmului funcționează corect**; munca de calcul este împărțită echitabil între procese.
* **Transfer (Linia Albastră):** Rămâne relativ constantă. Deoarece timpul de calcul scade, **timpul de transfer (Broadcast/Gather) devine procentual mult mai mare**, transformându-se în bottleneck-ul principal al aplicației. La 4 nuclee, comunicarea ocupă o parte semnificativă din timpul total (comparativ cu celealte 3 etape din algoritmul de Edge Detection), limitând scalabilitatea ulterioară.

### 3. Scalabilitate (CUDA + MPI)
Tabelul de mai jos prezintă timpii medii de execuție obținuți din 3 rulări succesive pentru fiecare configurație, procesând imaginea *1_earth_8k.png* pe nodul **xl**.

| Număr Procese (MPI Ranks) | Compute Time (ms) | GPU Alloc (ms) | Total Real (ms) | Speedup Real |
| :--- | :--- | :--- | :--- | :--- |
| **1** | 178 | 223 | **430** | 1.00x |
| **2** | 150 | 409 | **579** | 0.74x |
| **4** | 107 | 851 | **970** | 0.44x |

Deși timpul de calcul scade, timpul total **crește dramatic**. Acest lucru este cauzat de etapa de alocare pe CUDA: atunci când mai multe procese MPI de pe același nod încearcă să aloce memorie pe același GPU simultan, driverul serializează aceste cereri, ducând la timpi de alocare foarte mari.

### 4. Impactul asupra Resurselor Hardware

Dincolo de timpii de execuție, analiza contoarelor hardware (folosind *Intel vTune*) indică costurile ascunse ale paralelizării distribuite pe un singur nod.

![Hardware Counters](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_mpi/perf_counters.png)

Graficul de mai sus evidențiază două fenomene critice care apar odată cu creșterea numărului de procese MPI pe același nod fizic:

1.  **Scăderea Instructions Per Cycle - Grafic dreapta sus:**
    Valoarea IPC scade de la **~2.5** (1 proces) la **~1.9** (4 procese). Aceasta indică o "aglomerare" a resurselor CPU. Deoarece mai multe procese MPI concurează pentru resurse și pentru unitățile de execuție ale procesorului pentru a gestiona transferurile de date și logica de control, eficiența fiecărui ciclu de ceas scade.

2.  **Creșterea Page Faults - Grafic dreapta jos:**
    Numărul de *Page Faults* se dublează (de la ~16k la ~38k). Gestionarea (alocarea/dezalocarea) bufferelor pentru comunicarea MPI și pentru fâșiile de imagine creează presiune asupra sistemului de operare, introducând un overhead.

### 5. Validarea Limitărilor de Comunicare

Pentru a confirma ipotezele legate de limitările de comunicare, am efectuat o analiză detaliată a execuției folosind **Intel vTune Profiler** pentru scenariul cu 4 procese MPI.

![Intel vTune Top Hotspots](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_mpi/elapsed_info.png)

Raportul vTune oferă dovada definitivă a bottleneck-ului de comunicare:
* **Top Hotspot:** Funcția `MPI_Bcast` este responsabilă pentru **44.7%** din timpul total de utilizare a CPU-ului. Acest procent semnificativ confirmă că procesorul petrece aproape jumătate din timp gestionând difuzarea datelor către celelalte noduri, nu făcând calcule utile.
* **Serializarea I/O:** Următoarele funcții cele mai costisitoare sunt cele din biblioteca `stb_image` (ex: `stbi_zlib_compress`). Acest lucru subliniază impactul părții secvențiale a codului (citirea/scrierea imaginilor), care nu poate fi accelerată de GPU.

![Intel vTune Histogram](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/cuda_mpi/cpu_graph.png)

Histograma de mai sus arată distribuția nivelului de paralelism pe durata execuției.
Barele sunt concentrate masiv în partea stângă a graficului (număr redus de CPU-uri active simultan). Deși sistemul dispune de multiple nuclee logice, acestea sunt rareori utilizate simultan la capacitate maximă.

### Concluzii
Analiza performanței pentru implementarea hibridă (CUDA + MPI) a evidențiat trei aspecte fundamentale:

* **Paradoxul Scalării**: Din punct de vedere algoritmic, descompunerea imaginii funcționează: timpul efectiv de calcul scade odată cu creșterea numărului de procese (Speedup 1.66x la 4 procese). Acest beneficiu este însă șters de problema serializării alocării memoriei pe GPU.

* **Costul Comunicării**: analiza Intel vTune a demonstrat că funcția MPI_Bcast consumă aproximativ 44% din timpul CPU, transformând transferul de date în principalul bottleneck al aplicației distribuite.

* **Overhead-ul Sistemului**: creșterea numărului de procese pe același nod a dus la degradarea IPC (Instructions Per Cycle) și dublarea numărului de Page Faults, confirmând că gestionarea memoriei distribuite introduce costuri semnificative la nivel de SO.
## Implementare OpenMP - Inovații & Diferențe

În implementarea algoritmului **Canny Edge Detection** folosind **OpenMP**, am adaptat versiunea serială a algoritmului printr-o serie de optimizări specifice execuției pe CPU cu paralelizare pe mai multe nuclee. Aceste modificări au dus la accelerarea semnificativă a procesării imagini prin utilizarea tuturor resurselor disponibile pe procesor.

## Modificările realizate

* **Paralelizarea pe CPU** - versiunea serială procesează pixelii secvențial, calculând blur, gradienți etc. Astfel, în versiunea OpenMP, se paralelizează buclele de tip `for` folosind directivele specifice OpenMP `#pragma omp parallel for`, iar fiecare thread procesează o porțiune diferită din imagine, permițând procesarea simultană și reducând drastic timpul de execuție.
* **Schedule Static vs Dynamic** - constă în alegerea modului optim de distribuire a iterațiilor, iar în implementarea **Canny Edge Detection**, strategia **static scheduling** este utilizată pentru operațiile cu complexitate uniformă, cum ar fi calculul gradienților Sobel(`sobelGradient`) și non-maximum suppression(`nonMaxSuppression`), unde complexitatea pentru fiecare pixel este similară. **Dynamic scheduling** este utilizat pentru Gaussian Blur(`gaussianBlur`), unde complexitatea variază puțin din cauza edge handling-ului. Această alegere este o optimizare importantă care asigură că thread-urile sunt folosite eficient.
* **Optimizarea creării kernel-ului Gaussian** - pentru optimizarea buclei `for` din funcția `createGaussianKernel` unde se acumulează suma valorilor în variabila comună `sum`, se folosește clauza `reduction`. Astfel, se optimizează accesul la variabila comună și scade timpul total de rulare al algoritmului.
* **Eliminarea recursivității** - în funcția `hysteresisThreshold` s-a ales o abordare serială(față de cea recursivă din algoritmul inițial) pentru a putea fi paralelizată cu ușurintă folosind OpenMP.
* **Configurabilitate parametrilor programului** - numărul de thread-uri pentru paralelizarea buclelor, dimensiunea kernel-ului Gaussian și sigma pot fi setate pentru a testa îmbunătățirea programului.

## Flow-ul final al execuției

Serial: Citirea imaginii
Paralel: Copierea imaginii în structura **Image**
Paralel: Conversie grayscale, Blur, Sobel, NMS, Hysteresis
Paralel/Serial: Salvarea rezultatului final

## Provocări întâmpinate
1. **Load balancing** - necesită alegerea cu atenție a strategiei de schedule. **Static scheduling** este eficient pentru operații uniforme, dar **dynamic scheduling** este necesar pentru operații cu variație de complexitate. Astfel, identificarea strategiei care este potrivită pentru fiecare operație necesită profiling și testare.


## Profiling & Rezultate obținute


### Eficiență


| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/efficiency.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/xl_efficiency.png) |


Graficul arată o scădere constantă pe măsură ce crește numărul de thread-uri, comportament caracteristic paralelizării pe memorie partajată. Eficiența pornește de la valoarea ideală pentru execuția cu un singur thread, apoi scade progresiv la aproximativ jumătate pentru 2 thread-uri.

Imaginile mari precum **earth_8k** și **city** mențin o eficiență relativ bună până la 4 thread-uri, situându-se în jurul valorilor de 65%. Acest comportament sugerează că pentru imagini de dimensiuni mari, există suficient de multă muncă pentru a justifica overhead-ul de creare și sincronizare a thread-urilor. Operațiile intensive pe date mari beneficiază de distribuirea muncii, chiar dacă overhead-ul reduce eficiența totală.

Imaginea cea mai mică suferă cea mai drastică degradare a eficienței, scăzând rapid sub 50% pentru 2 thread-uri și ajungând la valori foarte reduse pentru 8 thread-uri. Pentru date de dimensiuni mici, costul creării thread-urilor, distribuirii iterațiilor și sincronizării la bariere devine comparabil sau chiar mai mare decât timpul efectiv de calcul. Acest pattern demonstrează clar că paralelizarea OpenMP nu este benefică pentru imagini de dimensiuni mici.


### Memorie și memorie cache comparație


#### Imaginea **1_earth_8k**

![Memory vs cache memory](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/earth_8k_memory_cache.png)

Graficul arată că numărul de page faults crește aproape liniar, indicând că fiecare thread nou adaugă presiune asupra memoriei virtuale. Această creștere liniară, mai lentă decât la imaginile mici, sugerează că sistemul de paging este mai stabil cu imagini mari.

Branch miss rate-ul rămâne relativ constant între, arătând că predicția nu se degradează semnificativ cu creșterea numărului de thread-uri. Aceasta indică că overhead-ul principal nu provine din branch prediction, ci din sincronizare.

Pentru 8 thread-uri, fiecare thread primește aproximativ 1/8 din cache-ul total, dar imaginea fiind mare, încă apar page faults, numărul acestora crescând ușor datorită faptului că stivele și heap-urile fiecărui thread nou ocupă pagini virtuale diferite, dar presiunea este mai uniformă decât la imagini mici.

Prin urmare, performanța memoriei se degradează mai ușor cu imagini mari decât mici, dar eficiență globală rămâne scăzută la 8 thread-uri datorită sincronizării.


#### Imaginea **poza**

![Memory vs cache memory](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/poza_memory_cache.png)

Pentru imaginile de dimensiune mică, numărul de page faults crește liniar (de la aprox. 1800 la 1900), ceea ce este un comportament mult mai stabil comparativ cu varianta MPI. Aceasta se datorează modelului de memorie partajată specific OpenMP-ului, unde nu este necesară duplicarea masivă a datelor între procese.

Branch Miss Rate-ul scade constant (de la 0.98% la 0.86%) pe măsură ce numărul de thread-uri crește. Acest comportament e explicat prin codul identic executat de fiecare thread și volumul de date mic pentru fiecare thread. Totuși, beneficiul este limitat de timpul total de execuție extrem de scurt, unde overhead-ul de creare a thread-urilor are un impact foarte mare.



### Durata execuției etapei


| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/stage_timings.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/xl_stage_timings.png) |


Analiza timpului pe fiecare etapă a algoritmului arată clar unde paralelizarea OpenMP aduce cele mai mari beneficii. Etapa de conversie Grayscale rămâne neglijabilă indiferent de numărul de thread-uri, fiind o operație simplă care nu justifică overhead-ul de paralelizare complexă.

Gaussian Blur domină complet timpul total de execuție și prezintă cea mai dramatică îmbunătățire prin paralelizare. Pentru un singur thread, această etapă scade constant pe măsură ce se adaugă thread-uri. La 8 thread-uri, timpul se reduce la aproximativ 1.3s, reprezentând o îmbunătățire substanțială.


Scăderea este mai uniformă și mai consistentă decât în alte metode de paralelizare, confirmând că separarea în două operații unidimensionale și distribuția uniformă pe thread-uri funcționează eficient. Totuși, curba începe să se aplatizeze după 4 thread-uri, sugerând că beneficiile se diminuează pe măsură ce overhead-ul de sincronizare crește.


Sobel Gradient urmează un pattern similar. Static scheduling-ul uniform asigură o distribuție echilibrată a muncii, fără thread-uri inactive. Non-Maximum Suppression beneficiază puternic de paralelizare, scăzând constant.

Hysteresis prezintă cea mai mică variație dintre configurații, timpul rămânând relativ constant. Componenta secvențială DFS limitează dramatic beneficiile paralelizării. Această etapă demonstrează clar limitările algoritmilor cu dependențe de date.


### Speedup

| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/speedup_efficiency.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/xl_speedup_efficiency.png) |


Graficul arată că implementarea OpenMP scalează mult mai bine decât implementarea MPI pentru toate categoriile de imagini. Imaginile mari ating un speedup de aproximativ 6 ori pentru 8 thread-uri. Această diferență semnificativă reflectă costul redus al sincronizării pe memorie comună comparativ cu comunicația între procese separate.


Imaginile mici arată un speedup mai modest dar totuși decent, atingând aproximativ 4 ori pentru 8 thread-uri. Deși overhead-ul este proporțional mai mare pentru date mici, OpenMP reușește să ofere îmbunătățiri rezonabile, spre deosebire de MPI unde overhead-ul comunicării devine prea mare.

Un pattern observat este că speedup-ul crește aproape liniar până la 4 thread-uri, apoi începe să se aplatizeze la 8 thread-uri. Această caracteristică sugerează că pentru acest algoritm, beneficiile paralelizării încep să fie înlocuite de overhead-ul de sincronizare. Totuși, chiar și cu această aplatizare, performanța rămâne superioară față de versiunea serială.


### Analiză scalabilitate

| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/scaling.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/xl_scaling.png) |


Graficul pentru comparația scalabilității arată că OpenMP se apropie mult mai mult de idealul liniar comparativ cu MPI. Pentru imagini mari (**1_earth_8k**, **city**), speedup-ul este aproape 5 pentru 8 thread-uri. Abaterea de la ideal este mai mică, sugerând un overhead redus de sincronizare față de implementarea MPI.

Un pattern constant este că speedup-ul crește aproape liniar până la 4 thread-uri, apoi începe să se aplatizeze la 8 thread-uri. Această caracteristică sugerează că pentru acest algoritm, faptul că beneficiile paralelizării încep să fie contracarate de overhead-ul de sincronizare după numărul de 4 thread-uri. Totuși, chiar și cu această aplatizare, performanța rămâne superioară față de versiunea serială.



### Analiză Scheduling (Static vs Dynamic vs Guided)


#### Imaginea **poza**
![Scheduling](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/poza_time_comparison.png)


Pentru imagini de dimensiuni mici, impactul strategiei de scheduling asupra performanței este foarte pronunțat. Graficul arată clar că varianta cu static scheduling și chunk size de 64 oferă cele mai bune performanțe, fiind aproape de două ori mai rapidă decât varianta cu dynamic scheduling și chunk size de 256.

Static scheduling cu chunk size-uri mai mici beneficiază de overhead-ul redus de management al iterațiilor. Pentru imagini mici, distribuția este deja relativ uniformă și nu necesită rebalansare dinamică. Costul de a gestiona cozi de task-uri și de a atribui dinamic chunk-uri către thread-uri devine dominant față de timpul efectiv de calcul.

Guided scheduling se situează la mijloc, oferind un compromis între static și dynamic. Chunk size-ul variabil permite o oarecare adaptare la dezechilibre, dar evită overhead-ul complet al scheduling-ului dinamic. Totuși, pentru imagini unde munca este deja uniform distribuită, acest compromis nu aduce beneficii semnificative față de static scheduling simplu.


#### Imaginea **earth_8k**
![Scheduling](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/earth_8k_time_comparison.png)


Pentru imagini mari, diferențele între strategiile de scheduling devin mult mai mici, dar pattern-ul general se menține. Static scheduling cu chunk size de o 128 sau 256 au cele mai bune performanțe, fiind ușor mai rapide decât variantele dynamic sau guided.

Motivul pentru care static rămâne optim este că munca este distribuită uniform pe pixelii imaginii. În algoritmul **Canny Edge Detection**, fiecare pixel necesită aproximativ aceeași cantitate de calcul pentru Gaussian Blur, Sobel și Non-Maximum Suppression. Nu există zone ale imaginii care să fie semnificativ mai complexe decât altele, deci overhead-ul de rebalansare dinamică nu este justificat.


Dynamic scheduling este cea mai lentă variantă chiar și pentru imagini mari, introducând un overhead de sincronizare inutil. Deși overhead-ul relativ este mai mic pentru imagini mari comparativ cu imagini mici, el rămâne consistent. Timpul pierdut în gestionarea cozii de task-uri și sincronizarea thread-urilor nu aduce niciun beneficiu compensator.

Guided scheduling oferă performanțe intermediare, dar diferența față de static este minimă. Pentru că algoritmul nu prezintă dezechilibre semnificative de load, capacitatea guided de a adapta chunk size-ul nu aduce avantaje practice.

Concluzia analizei de scheduling este clară: pentru algoritmul **Canny Edge Detection** implementat cu OpenMP, **static scheduling este varianta optimă**, indiferent de dimensiunea imaginii. Distribuția uniformă a muncii face ca overhead-ul scheduling-ului dinamic sau guided să fie nejustificat, iar chunk size-uri moderate oferă cel mai bun echilibru între distribuție și overhead de management.


### Intel VTune

Pentru a înțelege mai profund comportamentul implementării OpenMP, au fost efectuate analize detaliate cu Intel VTune pe două configurații: configurația cu 2 thread-uri și configurația cu 8 thread-uri. Aceste două configurații ilustrează comportamentul la un nivel moderat de paralelizare și respectiv la paralelizare maximă pe hardware-ul disponibil.


#### Configurația cu 2 thread-uri

| Summary | Time |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/VTune_T2_summary.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/VTune_T2_time.png) |


Analiza VTune pentru configurația cu 2 thread-uri arată un profil echilibrat și eficient. Timpul total de execuție este de aproximativ 7 secunde, iar cele mai solicitante funcții sunt cele computaționale ale algoritmului, fără overhead semnificativ din infrastructura OpenMP.

Funcția de Gaussian Blur domină profilul, consumând aproape jumătate din timpul total de procesare. Această dominație este de așteptat, având în vedere complexitatea operației de procesare pe întreaga imagine. Sobel Gradient apare pe locul doi, urmat de funcții matematice și Non-Maximum Suppression, toate contribuind cu părți rezonabile din timpul total.


Histograma de utilizare a CPU-urilor arată un pattern concentrat în jurul valorilor mici, cu un vârf proeminent la zero până la 2 CPU-uri active. Totuși, există și activitate la valori mai mari, indicând că cele 2 thread-uri reușesc să ruleze simultan pentru o porțiune semnificativă din timp. Pattern-ul sugerează că există momente de sincronizare la bariere unde thread-urile așteaptă, dar și perioade lungi de calcul paralel efectiv.

Absența funcțiilor de overhead OpenMP din top hotspots confirmă că pentru două thread-uri, costul infrastructurii de paralelizare este minim. 


#### Configurația cu 8 thread-uri

| Summary | Time |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/VTune_T8_summary.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/VTune_T8_time.png) |


Pentru configurația cu 8 thread-uri, profilul arată schimbări notabile în comportamentul algoritmului. Timpul total de execuție scade la aproximativ 5 secunde, reprezentând **o îmbunătățire semnificativă** față de 2 thread-uri, dar nu perfect proporțională cu numărul de thread-uri.

Gaussian Blur rămâne funcția care consumă cel mai mult, dar contribuția sa relativă scade ușor, consumând aproximativ jumătate din timpul total de CPU. Sobel Gradient, operațiile matematice și Non-Maximum Suppression mențin poziții similare în top, dar cu contribuții ușor reduse. Această redistribuire sugerează că overhead-ul sistemului crește proporțional cu numărul de thread-uri.


Histograma de utilizare CPU arată o distribuție mult mai răspândită comparativ cu două thread-uri. Activitatea este vizibilă pe tot spectrul de la 0 la 8 CPU-uri active, cu bare semnificative la valori intermediare. Totuși, vârful rămâne încă la valori mici, indicând că sincronizarea la bariere și contention-ul pe resurse partajate creează momente frecvente unde nu toate thread-urile rulează simultan.

Pattern-ul din histogramă arată limitările scalării pentru 8 thread-uri. Deși sistemul reușește să utilizeze toate cele 8 thread-uri pentru perioade de timp, există și multe momente unde doar câteva thread-uri sunt active. Această distribuție neuniformă explică de ce eficiența scade la aproximativ treizeci de procente pentru opt thread-uri, deși speedup-ul absolut rămâne pozitiv.


**Concluzie**: ambele configurații demonstrează că implementarea OpenMP scalează decent pentru acest algoritm. Absența overhead-ului mare sau a scăderii în performanță confirmă că implementarea este bună. Limitările observate sunt caracteristice **algoritmilor cu sincronizări dese** și **acces intens la memoria partajată**, nu probleme specifice implementării.


### Timp de execuție

| Haswell | XL |
|----------|----------|
| ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/execution_time.png) | ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/openmp/xl_execution_time.png) |


Fiecare thread adăugat contribuie cu o îmbunătățire măsurabilă, chiar dacă beneficiul scade progresiv. Această caracteristică demonstrează că modelul de memorie partajată al OpenMP elimină costurile de comunicare care limitează MPI.

Timpii de execuție descresc mai uniform și mai mult decât pentru MPI. Pentru imaginile mari precum **earth_8k**, timpul de execuție scade exponențial de la aproximativ 12 secunde pentru 1 thread la aproximativ 2 secunde pentru 8 thread-uri, reprezentând o reducere substanțială.

Imaginile mici prezintă timpi foarte reduși, în ordinul zecilor de milisecunde, dar arată totuși o reducere proporțională rezonabilă. De la aproximativ 90ms pentru un thread, timpul scade la aproximativ 30 ms pentru 8 thread-uri. Deși scăderea nu este la fel de mare ca la imagini mari, ea rămâne consistentă și fără regresii.



#### Imaginea **poza**

| Threads | Total Time (s) | Speedup | Efficiency (%) | Time Reduction | Page Faults | IPC | Branch Miss % |
|---------|---------------|---------|----------------|----------------|-------------|-----|---------------|
| 1 | 0.06 | 1.00x | 100.0% | 0.0% | 2,021 | 2.73 | 0.98% |
| 2 | 0.04 | 1.75x | 87.5% | 42.9% | 2,027 | 2.66 | 1.16% |
| 4 | 0.02 | 2.71x | 67.8% | 63.1% | 2,043 | 2.39 | 1.00% |
| 8 | 0.02 | 2.72x | 34.0% | 63.3% | 2,074 | 1.32 | 1.07% |


#### Imaginea **1_earth_8k**

| Threads | Total Time (s) | Speedup | Efficiency (%) | Time Reduction | Page Faults | IPC | Branch Miss % |
|---------|---------------|---------|----------------|----------------|-------------|-----|---------------|
| 1 | 7.76 | 1.00x | 100.0% | 0.0% | 8,256 | 2.83 | 1.05% |
| 2 | 4.05 | 1.92x | 95.8% | 47.8% | 10,724 | 2.81 | 1.05% |
| 4 | 2.38 | 3.26x | 81.6% | 69.4% | 9,391 | 2.80 | 1.06% |
| 8 | 2.18 | 3.56x | 44.6% | 71.9% | 9,679 | 1.68 | 1.04% |


### Rezumat analiză

| Imagine | Resolution | Speedup Max | Eficiență Max (%) | Timp Min (s) |
|---------|------------|------------|-----------------|-------------|
| 1_earth_8k | 8192x4096 | 3.56x | 44.6% | 2.177s |
| city | 4928x3264 | 3.55x | 44.4% | 1.073s |
| istockphoto-478656454-612x612 | 612x461 | 2.70x | 33.8% | 0.026s |
| pexels-eberhardgross-858115 | 5472x3648 | 3.61x | 45.1% | 1.275s |
| pexels-joey-kyber-31917-134643 | 5851x3901 | 3.58x | 44.8% | 1.445s |
| poza | 612x408 | 2.72x | 34.0% | 0.023s |


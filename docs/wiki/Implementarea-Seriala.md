## Implemenatrea Serială

Implementarea serială a algoritmului **Canny Edge Detection** reprezintă varianta de bază, care procesează imaginea pixel cu pixel într-o manieră liniară. Această implementare servește ca variantă de referință pentru toți algortimii paraleli, oferind o înțelegere clară a flow-ului algoritmului și a operațiilor computaționale.

Algoritmul **Canny Edge Detection** procesează imaginea în 5 etape distincte, fiecare contribuind la identificarea muchiilor. Prima etapă este conversia Grayscale, transformând imaginea color într-o reprezentare monocromatică folosind formula standard de luminozitate.

În a doua etapă se aplică Gaussian Blur pentru reducerea zgomotului din imagine. Kernel-ul Gaussian bidimensional este creat folosind formula exponențială bazată pe distanța față de centru și parametrul __sigma__. Fiecare pixel din imaginea rezultată este calculat prin convoluția kernel-ului cu vecinătatea pixelului original, procesul parcurgând întreaga imagine rând cu rând, coloană cu coloană.


Etapa a treia calculează gradienții folosind filtrul Sobel. Pentru fiecare pixel, se aplică două kernel-uri 3x3, unul pentru direcția orizontală și altul pentru direcția verticală.

Etapa finală implementează Double Threshold și Edge Tracking prin Hysteresis. Inițial, pixelii sunt clasificați în trei categorii: muchii puternice peste threshold-ul înalt, muchii slabe între cele două threshold-uri, și non-muchii sub threshold-ul scăzut. Apoi, se aplică un algoritm de depth-first search pornind de la fiecare muchie puternică, promovând recursiv toate muchiile slab conectate. Această tehnică asigură că muchiile slabe care fac parte din contururi continue sunt păstrate.


## Posibile modificări pentru algoritmii paraleli


Recursivitatea din Hysteresis, introduce overhead semnificativ și limitează performanța datorită limitărilor stivei. Pentru imagini cu muchii foarte lungi, această abordare poate chiar eșua cu stack overflow, necesitând o reimplementare iterativă cu stivă explicită.


## Analiză Intel VTune

<details>
  <summary>Click aici pentru a vizualiza graficele și analiza detaliată</summary>

  <br>

  ### Histograma Eficienței
  ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/serial/VTune_histograma.png)


* Histograma arată un pattern extrem de revelator pentru implementarea serială. Vârful masiv la zero sau un CPU activ domină complet graficul, aproape întreaga execuție petrecându-se cu un singur CPU utilizat. Bara este atât de pronunțată încât comprimă complet scala, făcând orice altă activitate practic nesemnificativă.

* Această concentrare absolută la un singur CPU confirmă natura strict secvențială a implementării. Deși sistemul are disponibile multiple nuclee de procesor, algoritmul folosește doar unul, celelalte rămânând complet idle pe durata execuției. Nu există momente detectabile unde mai mult de un CPU să fie utilizat simultan pentru procesarea imaginii.

* Pattern-ul este caracteristic pentru cod pur secvențial fără niciun grad de paralelizare. Fiecare operație este executată complet înainte de a începe următoarea, fiecare pixel este procesat înainte de a trece la următorul, și fiecare etapă a algoritmului se desfășoară liniar fără oportunități de execuție concurentă. Această utilizare de doar douăzeci până la treizeci de procente din puterea totală de procesare disponibilă reprezintă cea mai clară motivație pentru paralelizare.

  ### Timpul de Execuție
  ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/serial/VTune_time.png)

* Analiza arată un timp total de execuție de aproximativ 10s, timpul efectiv de procesare fiind de aproximativ 9s. Numărul de thread-uri raportat este trei, reprezentând thread-ul principal plus thread-uri auxiliare ale runtime-ului și sistemului de operare, nu paralelism efectiv în algoritmul utilizatorului.

* Funcția `gaussianBlur` domină complet profilul de execuție, consumând aproximativ 50% din timpul total de CPU. Această dominație confirmă că Gaussian Blur este bottleneck-ul principal al algoritmului, reprezentând aproape jumătate din întregul timp de execuție. Operația bidimensională cu kernel pentru fiecare pixel din imagine creează miliarde de operații care consumă majoritatea ciclurilor de procesor.

* Funcția sobelGradient apare pe locul doi cu aproximativ 10% din timpul total, urmată de funcția matematică `atan2f` cu 9%. Această prezență prominentă a `atan2f` în top hotspots subliniază costul operațiilor trigonometrice din calculul direcției gradientului.


  ### Studiu Serial
  ![Efficiency Analysis](https://gitlab.cs.pub.ro/app-2025/cannycore/-/raw/main/wiki_images/serial/VTune_studiu_serial.png)


* Vizualizarea call stack-ului și timeline-ului de execuție arată structura ierarhică a algoritmului. Gaussian Blur ocupă o bandă largă, vizibil mai substanțială decât toate celelalte componente. Această reprezentare vizuală confirmă datele numerice din top hotspots, arătând că blur-ul consumă nu doar majoritatea timpului ci și o porțiune continuă și neîntreruptă din execuție.

* Sobel Gradient și celelalte etape apar ca benzi mai înguste în timeline, reflectând timpul lor relativ redus de execuție. Pattern-ul de execuție este strict liniar, fără suprapuneri sau execuție paralelă între diferite componente. Fiecare etapă începe exact când precedenta se termină, creând o secvență perfect serială de la început până la final.


</details>


## Rezultate

| Imagine | Resolution | Timp Min (s) | Timp Baseline (s) |
|---------|------------|--------------|-------------------|
| 1_earth_8k | 8192x4096 | 7.580s | 7.580s |
| city | 4928x3264 | 3.822s | 3.822s |
| istock_photo_612 | 612x461 | 0.185s | 0.185s |
| pexels-eberhardgross-858115 | 5472x3648 | 4.724s | 4.724s |
| pexels_joey_kyber | 5851x3901 | 5.418s | 5.418s |
| poza | 612x408 | 0.161s | 0.161s |


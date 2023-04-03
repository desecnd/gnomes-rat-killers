# Gnomes Rat Killers 

### Kompilacja

``` bash
mpicxx solve.cpp -o solve
```

### Uruchomienie

```
mpirun -np 4 ./solve
```

**Uwaga:** Program zawsze wybiera min(np, N_WORKERS) jako liczbę faktycznych pracowników. Ilość łowców jest równa temu co "zostało". Należy upewnić się, że liczba uruchomionych procesów jest >= (N+M).  

Poniżej fragment, wykonania dla 5 celowników / agrafek, 0 broni, i losowych czasów stanów [3, 8]:

```
W[0] [t0]: Falling asleep... (SLEEP) {7s}
H[3] [t0]: Falling asleep... (SLEEP) {3s}
W[2] [t0]: Falling asleep... (SLEEP) {8s}
W[1] [t0]: Falling asleep... (SLEEP) {5s}
H[3] [t0]: Will rest for a bit... (SLEEP -> REST) {4s}
W[1] [t0]: Now, will rest a bit... (SLEEP -> REST) {3s}
H[3] [t0]: I need FiRePoWeR! (REST -> REQ)
W[0] [t0]: Now, will rest a bit... (SLEEP -> REST) {7s}
W[2] [t0]: Now, will rest a bit... (SLEEP -> REST) {7s}
W[1] [t0]: Acquiring pin & scope! (REST -> REQ)
W[0] [t2]: Acquiring pin & scope! (REST -> REQ)
W[2] [t4]: Acquiring pin & scope! (REST -> REQ)
W[1] [t7]: Assembing the weapon of mass ratstruction! (REQ -> WORK) {7s}
W[0] [t8]: Assembing the weapon of mass ratstruction! (REQ -> WORK) {3s}
W[2] [t9]: Assembing the weapon of mass ratstruction! (REQ -> WORK) {3s}
W[1] [t7]: Delivering the weapon... (WORK -> SLEEP)
W[1] [t9]: Falling asleep... (SLEEP) {4s}
H[3] [t9]: Sending the next RAT to the moon, boyz! (REQ -> WORK) {8s}
W[0] [t9]: Delivering the weapon... (WORK -> SLEEP)
W[0] [t11]: Falling asleep... (SLEEP) {8s}
W[2] [t11]: Delivering the weapon... (WORK -> SLEEP)
W[2] [t13]: Falling asleep... (SLEEP) {8s}
W[1] [t9]: Now, will rest a bit... (SLEEP -> REST) {6s}
H[3] [t13]: Headhunterz are back... (WORK -> SLEEP)
H[3] [t15]: Falling asleep... (SLEEP) {8s}
W[1] [t15]: Acquiring pin & scope! (REST -> REQ)
W[0] [t11]: Now, will rest a bit... (SLEEP -> REST) {3s}
W[2] [t13]: Now, will rest a bit... (SLEEP -> REST) {7s}
H[3] [t15]: Will rest for a bit... (SLEEP -> REST) {3s}
W[0] [t17]: Acquiring pin & scope! (REST -> REQ)
W[1] [t20]: Assembing the weapon of mass ratstruction! (REQ -> WORK) {8s}
W[2] [t19]: Acquiring pin & scope! (REST -> REQ)
H[3] [t15]: I need FiRePoWeR! (REST -> REQ)
W[0] [t22]: Assembing the weapon of mass ratstruction! (REQ -> WORK) {4s}
W[1] [t22]: Delivering the weapon... (WORK -> SLEEP)
W[1] [t24]: Falling asleep... (SLEEP) {7s}
W[2] [t24]: Assembing the weapon of mass ratstruction! (REQ -> WORK) {7s}
H[3] [t24]: Sending the next RAT to the moon, boyz! (REQ -> WORK) {3s}
W[0] [t23]: Delivering the weapon... (WORK -> SLEEP)
W[0] [t25]: Falling asleep... (SLEEP) {8s}
W[2] [t25]: Delivering the weapon... (WORK -> SLEEP)
W[2] [t27]: Falling asleep... (SLEEP) {5s}
H[3] [t27]: Headhunterz are back... (WORK -> SLEEP)
H[3] [t29]: Falling asleep... (SLEEP) {6s}
W[1] [t24]: Now, will rest a bit... (SLEEP -> REST) {3s}
W[2] [t27]: Now, will rest a bit... (SLEEP -> REST) {8s}
W[1] [t29]: Acquiring pin & scope! (REST -> REQ)
W[0] [t25]: Now, will rest a bit... (SLEEP -> REST) {7s}
```
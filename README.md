# 🐀 Gnomes Rat Killers 

Following code is implementation of mutual exclusion in distributed system for given problem statement. It was prepared as project for 6th semester _Distributed Programming_ subject course at Poznan University of Technology.

### Problem Statement

> There are two types of processes - N gnome workers and M gnome hunters. Workers compete for one of A pins and S target scopes. They combine pins with a scope to form a weapon. Hunters compete for a weapon. After getting the weapon, they kill the rats and return the pins and scopes to the workers resource pool.

### Algorithm Outline

#### Structures and variables:

1. `ResourceQueues` - Queues of processes waiting to access resource X, initially empty, separate for each resource
    1. `QueueEntry` - structure containing `{id, lamport_timestamp}` pair, ordered in ascending order by timestamp
    2. `AckSent` - an array of true/false values storing information about the sent ACK to the process id in the queue
2. `ResourceCount` - an array of the current resource count
3. `ReceivedAckCount` - the number of received ACK messages waiting for resource X, initially 0 
4. `SameTypeIDs` - an array of IDs of gnomes of the same type (workers for worker, hunters for hunter)
5. `otherTypeIDs` - an array of gnome IDs for the opposite type (hunters for worker, workers for hunter).

#### Messages:

Each message consists of 3 attributes: **message type**, **resource type** and **time stamp**. We define 3 types of resources: 

- `PIN` - Safety pin collected by employees
- `SCOPE` - A gun sight collected by employees
- `WEAPON` - Weapons produced by workers and consumed by hunters.

The timestamp is modified according to the rules of Lamport's scalar logical clock. There are 4 types of messages sent: 

- `REQUEST` - a request to receive one of the above-mentioned resources.
- `ACK` - granting permission to download the resource
- `CONSUME` - notification that the resource has been downloaded
- `PRODUCE` - notification that the resource has been produced


#### States:

Both workers and hunters can be in one of 4 states:

- `SLEEPING` - Gnome does nothing. It is assumed that in this state it does not respond to messages (not necessary for the solution, it was introduced to simulate "delays" from the process)
- `RESTING` - Gnome is resting. It is assumed that in this state it can already respond to all messages
- `REQUESTING` - Gnome is trying to get access to resource X, at the same time it can respond to messages
- `WORKING` - Gnome has already received access to all needed resources, it is now making weapons (workers) or consuming them (hunters)

### Algorithm Description

Gnomes requesting a resource X send a `REQUEST(X)` message to all gnomes of the same type (each id in `SameTypeIDs`). Each process in the same group adds a new QueueEntry structure to the `ResourceQueue[X]` resource queue and responds to the request with an ACK message if it is in the first `ResourceCount[X]` elements. Acknowledgement of receipt of access determines receipt of all ACK messages. The process requesting access counts the received ACK messages and if they are equal to the number of gnomes of the same type, it occupies the resource. After collecting all the required resources (safety pin and sight for the worker and weapon for the hunter), the gnome sends `CONSUME[X]` messages informing gnomes of the same type that it has processed the requested resource and its number has decreased, and `PRODUCE(Y)` messages to all gnomes of **the opposite type** (id in the OtherTypeIDs array) informing them that a new resource has arrived.

- Time complexity: 8 rounds (`REQ[PIN]`, `ACK[PIN]`, `REQ[SCOPE]`, `ACK[SCOPE]`, `CONSUME[PIN,SCOPE]` + `PRODUCE[WEAPON]`, `REQ[WEAPON]`, `ACK[WEAPON]`, `CONSUME[WEAPON]` + `PRODUCE[PIN, SCOPE]`)
    
    * In reality, we can combine `REQ[PIN]+REQ[SCOPE] => REQ[PIN+SCOPE]` and lower time complexity to 6 rounds.
- Communication complexity: `6n + 2m - 8`
    * n - number of employees
    * m - number of hunters

## Program

### Compile & Run

Code was written in C++ with MPI framework. 

``` bash
mpicxx solve.cpp -o solve
mpirun -np <X> ./solve
```

### Demo 

Following is a example execution of the algorithm for `3x worker` and `1x hunter`, given starting resources: 
- `5x pin & scope` for workers
- `0x weapon` for hunter

and random state times (between 3s - 8s). 

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
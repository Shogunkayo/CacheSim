# Building a cache coherant Cache Access Simulator 
`cache_sim.c` is a simple cache simulator. There is a memory region along with an L1 Cache. The cache is replaced using a simple direct-mapped policy.

The program reads a file `input_0.txt` which consists of a set of instructions, which can be either Reads or Writes, and then executes them. 

This cache simulator is parallelized using simulator using OpenMP, turning the single core cache simulator into a multi-core cache simulator. To maintain cache coherence, MESI protocol is implemented.

![state machine](https://github.com/Shogunkayo/CacheSim/tree/master/diagrams/statemachine.png)

## Instruction type:
There are two instruction types that the simulator can run:
`RD <address>` and `WR <address> <value>`.

## Input format
The emulator will be fed multiple input files, `input_0.txt` to `input_n.txt` where `n` is the number of threads we support. Each of the files will be read by individual threads which will run the instructions. 

There is a global memory area `memory`, and each "cpu core" will have it's own cache area `c`.

## Output format
The output format is a bunch of print statements of format:
```
Thread <thread_num>: RD <address>: <current_value>
Thread <thread_num>: WR <address>: <current_value>

```

Example:
```
Thread 0: WR 20: 10
Thread 1: WR 10: 5
Thread 0: RD 20: 10
Thread 1: WR 21: 12
Thread 0: WR 23: 30
Thread 1: WR 23: 13
Thread 0: RD 23: 13
Thread 1: RD 21: 12
Thread 0: WR 21: 15
Thread 0: RD 21: 15
Thread 1: RD 10: 5
Thread 0: RD 23: 13
Thread 1: RD 21: 12
Thread 1: RD 21: 12
```

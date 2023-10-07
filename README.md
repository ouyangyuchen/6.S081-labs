# 6.S081-2021
Labs and resources from MIT 6.S081 (6.828, 6.1800) course in 2021 Fall.

## About this course
- *tags*: **operating system**, **risc-v**, **C**
- *study time*: 2023 Fall
- *course homepage*: [6.S081 / Fall 2021](https://pdos.csail.mit.edu/6.828/2021/schedule.html)
- *Textbook*: [xv6 book](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)

## Useful links
- [Setup xv6 environment](https://pdos.csail.mit.edu/6.828/2021/tools.html)
- [risc-v calling convention](https://pdos.csail.mit.edu/6.828/2021/readings/riscv-calling.pdf)
- [QEMU Manual](https://wiki.qemu.org/Documentation), risc-v machine emulation platform

*Some notes taken during the coding and thinking.*

## [Lab Utilities](https://pdos.csail.mit.edu/6.828/2021/labs/util.html)

### primes
use pipe and fork to set up the pipeline. The first process feeds the numbers 2 through 35 into the pipeline. For each prime number, you will arrange to create one process that reads from its left neighbor over a pipe and writes to its right neighbor over another pipe. 

![image](https://github.com/ouyangyuchen/6.S081-labs/assets/107864216/3a13c876-e3cd-4c7b-a8f3-654548d9cbc7)

set up a state machine for **all child processes**:

![未命名绘图 drawio](https://github.com/ouyangyuchen/6.S081-labs/assets/107864216/28a031db-4222-44fb-8a43-4ad899e60382)

- What does main process do?

  This process just creates the first process, set up a pipe. Then it sends 2~35 to the pipe and close the reading end `p[0]`
  
- How to setup the pipe?

  Before `fork()`, the process creates a pipe. After `fork()`, child closes the writing end `p[1]`, and parent closes the reading end `p[0]`.
  This guarantees that only one process has the writing fd to each pipe.

- How does the process terminate?

  After the main process closes the writing end fd, the child will read from a pipe that has no writing end, which returns 0.
  Then the child process jumps off the while loop and closes its own writing end, so the same process will happen on its child too.

  Finally, the last process terminates. And all waiting parent processes will terminate one by one.

- All child processes should explicitly **wait for its own chlid** before exiting. This behavior guarantees the terminating order and printing correctness.

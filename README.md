# 6.S081-2021
Labs and resources from MIT 6.S081 (6.828, 6.1800) course in 2021 Fall.

> Check branches for source code

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

### primes (hard)
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

## [Lab System Calls](https://pdos.csail.mit.edu/6.828/2021/labs/syscall.html)
This lab shows us how to create a system call and use definitions in kernel code.

### Activate a system call in xv6

1. **ecall \<num\>** - There is a function declaration in `user/user.h`, a system call number in `kernel/syscall.h` and a stub to `user/usys.pl` (which generates user-level syscall instructions).
2. **jump to syscall()** - `ecall` will jump to the `syscall()` function in `kernel/syscall.c` with the calling number saved in `a7` register.
3. **syscall() calls corresponding kernel code** - `syscall()` use the number as an index of a function pointer array to get the address of its kernel code and call it.
4. **execute kernel code** - Execute the actual kernel system call, ex. `sys_fork()`, in `kernel/sysproc.c` and other files.

### Tracing
Add an mask integer in process structure. When the actual system call returns to `syscall()`, test the process mask with the system call number and print a line.

> How to pass the mask to child processes?
- Initial every process with an empty mask while allocating.
- The `sys_trace()` writes the mask integer of process from its argument. If the user doesn't call `trace()`, all processes will have empty masks and hence avoid printing lines.
- `fork()` will copy mask number to the child process.

### Sysinfo
- *freemem* - Traverse the free page list and get the result by multiplying `PGSIZE`.
- *nproc* - Traverse the process array and count the number of used ones.
- `sys_sysinfo()` copy the result numbers to the given address by calling `copyout()`.

## [Lab Page Tables](https://pdos.csail.mit.edu/6.828/2021/labs/pgtbl.html)
In this lab, the most important reference is `kernel/vm.c`, which relates to the creation, mapping, and freeing of page tables.
Among all the functions in `vm.c`, three are the most useful in this lab:
1. `int mappages()`: Setup the page table entries with physical address usually used with `kalloc()`.
2. `pte *walk()`: Fetch or create the corresponding page table entry in the three-level tree.
3. `void uvmunmap()`: Unmap vm pages by writing the page table entries to 0, optionally free the pointed physical memory.

### Speed up system calls
We can map a new **read-only** (dont't forget `PTE_U`) page at va=`USYSCALL`, and store the information in the pointed physical page.

If user calls `ugetpid()`, function will directly return the pid stored at `USYSCALL` in user mode, instead of asking kernel to return `getpid()`.

### Print a page table
Essentially this is a pre-order traversal in N-ary tree. You can imagine:
1. Every pagetable is a node, and each pte is a link to one child node.
2. If the pte is valid and labeled as `*pte & (PTE_W|PTE_R|PTE_X) == 0`, then this node is an internal node.
3. Recursively call `vmprint()` for internal nodes after printing the lines.

The indentation level can be passed as the second argument in the recursive function. You can construct the prefix string before traversing.

### Detecting which pages have been accessed (hard)
I think this task is easy level because it is quite straight-forward.

1. Call multiple `walk()` functions in the given range.
2. For each pte returned, check whether the `PTE_A` bit is set and clear it.
3. Set the corresponding bit based on the for loop counter `i` and shifting.
4. `copyout` the bitmask to the destination user address.

My suggestion is **setting a small size for the bitmask** (like 64 or 128) or you can allocate space from the input number (I use this approach).
If you set the size too much, like 512 or 1024, the kernel stack will probably overflow and you can see a bizarre bug about page fault and spend several hours on it (just like I did).

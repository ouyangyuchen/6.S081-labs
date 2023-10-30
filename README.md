# 6.S081-2021
Labs and resources from MIT 6.S081 (6.828, 6.1800) course in 2021 Fall. 

master branch only keeps the notes in `README.md`. **Please switch the *branch* if you want to check source code for the corresponding lab.**

## About this course
- *tags*: **operating system**, **risc-v**, **C**
- *study semester*: 2021 Fall
- *course homepage*: [6.S081 / Fall 2021](https://pdos.csail.mit.edu/6.828/2021/schedule.html)
- *Textbook*: [xv6 book](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)

## Useful links
- [Setup xv6 environment](https://pdos.csail.mit.edu/6.828/2021/tools.html)
- [risc-v calling convention](https://pdos.csail.mit.edu/6.828/2021/readings/riscv-calling.pdf)
- [QEMU Manual](https://wiki.qemu.org/Documentation), risc-v machine emulation platform

---
Here are my notes and thoughts for all labs.

## [Lab Utilities](https://pdos.csail.mit.edu/6.828/2021/labs/util.html)

### primes (hard)
use pipe and fork to set up the pipeline. The first process feeds the numbers 2 through 35 into the pipeline. For each prime number, you will arrange to create one process that reads from its left neighbor over a pipe and writes to its right neighbor over another pipe. 

<p align="center">
<img src="./assets/prime.png" alt="prime" width="600"/>
</p>

set up a state machine for **all child processes**:

<p align="center">
<img src="./assets/prime_sm.png" alt="prime_state_machine" width="600"/>
</p>

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

## [Lab Traps](https://pdos.csail.mit.edu/6.828/2021/labs/traps.html)
The `uservec` code in `kernel/trampoline.S` is used for 2 things:
1. switch the page table from user space to kernel space.
2. save all the general registers to trapframe page (per process)
3. jump to `usertrap()`.

`usertrap()` in `user/trap.c` is to handle the exception based on the `sscause` number: syscall, device interrupt, timer interrupt, other faults.
`usertrap()` then jumps to `usertrapret()` for configuring the trapframe head information, ex. kernel page table address, hartid ...

Then `userret` in the `trampoline.S` will do the reverse things in `uservec` and return to user mode by calling `sret`.

### Alarm (hard)
This lab requires us to **jump to a user function when the tick number passes the given limit**, and **restore the context when the handler returns**. 

**test0**

We can add some fields in process structure to store ticks, limit, and handler address.
When the condition is triggered (ticks is big enough), we need to write the handler address to pc register, right?. No! This will certainly fails, because:

> `usertrap` is in kernel address space. We cannot use user address directly!

We need to ask `usertrapret()` for help, which is also in kernel space.
It can help us jump back to the user space and restore the context saved in trapframe, including the pc register.

Therefore, we can change the epc register in trapframe to the handler address. And we call `usertrapret()`, kernel will do the right things for us.

**test1**

Now the 1st problem is solved, how about the next one? You may have noticed that all registers restored by `userret` will be corrupted by the handler rountine,
thus we can't safely jump back to the user code. So another question goes that where we can store the saved registers so that we can copy them back? 
If you have a look at `proc.h`, you will find there is space in the trapframe (4096) to store all registers (288), we can save them just beneath the original saving area.

Therefore, before `usertrapret()`, we should copy all registers to the new temporary area (including epc, the original one) and change epc above to our handler address.
Even if the handler uses many registers, we still have a copy for each one below. 

When the handler is going to return, it is forced to add a system call `sigreturn`, returning to kernel space. In this system call, we restore the original context
by simply copying back the memory below and calling `usertrapret()`. Where are we going to now? Of course, the original epc register, also is the user pc when the interruption occurs.

**test2**

Add a lock flag in process struct, and initialize it to open state in `allocproc()`.

Lock it before copying the registers, unlock it when `sigreturn` returns.

## [Lab Copy-on-Write](https://pdos.csail.mit.edu/6.828/2021/labs/cow.html)
We need to implement copy-on-write optimization in fork calls. Let's follow the hints:

When user calls `fork()`, a new process will create a new page table (`uvmcopy()`), which contains the same ptes as the parent. And we clear the write flag `PTE_W` for every valid page in both tables. Is this enough? Let's move on and leave it okay temporarily.

### Copy page table
So next, a store instruction in user code causes a store page fault `scause = 15`, we go into the `usertrap()` routine, a general exception handler.
We then retrieve the faulting virtual address from `stval` register and walk through the page table to find the cause.
Suppose we perfectly find it, now shall we allocate a new page and restore the writing bit? Wait!!!
If this page was read-only before copying, now we have a writable page in one of the processes, that's horrible!

Now the question comes, *how can you decide the page is originally read-only or r/w?* So we need to do something in the `uvmcopy()` so that
it is easy to distinguish the type.
My solution is using a bit in the Reserved-for-SoftWare area:
```c
#define PTE_COW (1L << 8)  // 1: cow page, 0: read-only page
```
<p align="center">
<img src="./assets/pte.png" alt="pte39" width="700" />
</p>

When copying the page table, we only **set the cow bit if this page was writable** for both tables.
Now we are trapped in the page fault handler, we can allocate pages only for the cow ones and leave the read-only ones just there.
Don't forget reset the flag bits:
```c
*pte |= PTE_W;
*pte &= ~PTE_COW; // not cow any more, avoid causing another copy
```

The faulting process restarts the store instruction, and this time it is writable.

### Add reference counts
Now the user wants to terminate its process, and kernel calls `uvmfree()` in `kernel/vm.c` file.
The `do_free` option is set in the `uvmunmap()` function, and we free all physical pages belonging to the process.

What about the read-only pages that have multiple mappings? If one process frees it, any other process have no access to this physical page.
That's why **we need to record the number of references to every physical page allocated by `kalloc()`**. (kernel data and text are excluded)
We only free the pages having no references.

How can we maintain this data structure? In my solution, just a `uint` array that have the same number entries as physical pages.

- `kalloc()` set the count number to 1 before returning.
- `kfree()` decrements the count by 1. If it is 0, we actually free this page.
- `uvmcopy` increments the corresponding count number for each page by 1.
- Page fault handler calls `kfree(old_pa)` after updating the pte and copying memory.

This approach requires few changes in the code: The kernel calls `kfree()` just as usual when freeing the process;
If the cow page has only one external reference and that process is going to write, kernel simply allocates a new physical page
and free the old one, which avoids the complicated judging code.

# [Lab Traps](https://pdos.csail.mit.edu/6.828/2021/labs/traps.html)

The `uservec` code in `kernel/trampoline.S` is used for 2 things:

1. switch the page table from user space to kernel space.
2. save all the general registers to trapframe page (per process)
3. jump to `usertrap()`.

`usertrap()` in `user/trap.c` is to handle the exception based on the `sscause` number: syscall, device interrupt, timer interrupt, other faults. `usertrap()` then jumps to `usertrapret()` for configuring the trapframe head information, ex. kernel page table address, hartid ...

Then `userret` in the `trampoline.S` will do the reverse things in `uservec` and return to user mode by calling `sret`.

## Alarm *(hard)*

This lab requires us to **jump to a user function when the tick number passes the given limit**, and **restore the context when the handler returns**.

### **test0**

We can add some fields in process structure to store ticks, limit, and handler address. When the condition is triggered (ticks is big enough), we need to write the handler address to pc register, right?. No! This will certainly fails, because:

> `usertrap()` is in kernel address space. We cannot use user address directly!
> 

We need to ask `usertrapret()` for help, which is also in kernel space. It can help us jump back to the user space and restore the context saved in trapframe, including the pc register.

Therefore, we can change the epc register in trapframe to the handler address. And we call `usertrapret()`, kernel will do the right things for us.

### **test1**

You may have noticed that all registers restored by `userret` will be corrupted by the handler rountine, thus we can't safely jump back to the user code. So another question goes that where we can store the saved registers so that we can copy them back? If you have a look at `proc.h`, you will find there is space in the trapframe (4096) to store all registers (288), we can save them just beneath the original saving area.

Therefore, before `usertrapret()`, we should copy all registers to the new temporary area (including `epc`, the original one) and change the former `epc` to our handler address. Even if the handler uses many registers, we still have a copy for each one below.

When the handler is going to return, it is forced to add a system call `sigreturn`, returning to kernel space. In this system call, we restore the original context by simply copying back the memory below and calling `usertrapret()`. Where are we going to now? Remember, the original `epc` register is the user pc when the interrupt occurs.

### **test2**

Add a lock flag in process struct, and initialize it to “unlock” state in `allocproc()`.

Lock it before copying the registers, unlock it when `sigreturn` returns.

- code
    
    ```c
    // kernel/trap.c
    void
    usertrap(void)
    {
      ...
      // give up the CPU if this is a timer interrupt.
      if(which_dev == 2) {
        // check whether the ticks have reached the limit
        if (p->TICKS > 0 && ++(p->ticks) >= p->TICKS) {
          p->ticks = 0;
          if (p->handler_lock == 1) {
            p->trapframe->a0 = -1;    // return -1 if the handler is executing
            usertrapret();
          }
          // copy all registers to temporary area
          p->handler_lock = 1;    // lock the signal handler
          memmove(p->trapframe->tempreg, p->trapframe, 288);
          p->trapframe->epc = p->handler;
          usertrapret();
        }
        yield();
      }
    	
      usertrapret();
    }
    ```
    
    ```c
    // kernel/sysproc.c
    uint64
    sys_sigalarm(void) {
      int ticks;
      uint64 handler;
    
      if (argint(0, &ticks) < 0)
        return -1;
      if (argaddr(1, &handler) < 0)
        return -1;
    
      struct proc *p = myproc();
      if (p == 0)
        return -1;
    
      p->TICKS = ticks;
      p->ticks = 0;
      p->handler = handler;
    
      printf("sigalarm: ticks=%d, handler=%p\n", p->TICKS, p->handler);
      return 0;
    }
    
    uint64
    sys_sigreturn(void) {
      struct proc *p = myproc();
    
    	// restore all registers in temporary area
      memmove(p->trapframe, p->trapframe->tempreg, 288);
      p->handler_lock = 0;    // unlock the signal handler
      usertrapret();
      return 0;
    }
    ```

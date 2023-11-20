# [Lab System calls](https://pdos.csail.mit.edu/6.828/2021/labs/syscall.html)

This lab shows us how to create a system call and use definitions in kernel code.

How to activate a system call in xv6?

1. **ecall <num>** - There is a function declaration in `user/user.h`, a system call number in `kernel/syscall.h` and a stub to `user/usys.pl` (which generates user-level syscall instructions).
2. **jump to syscall()** - `ecall` will jump to the `syscall()` function in `kernel/syscall.c` with the calling number saved in `a7` register.
3. **syscall() calls corresponding kernel code** - `syscall()` use the number as an index of a function pointer array to get the address of its kernel code and call it.
4. **execute kernel code** - Execute the actual kernel system call, ex. `sys_fork()`, in `kernel/sysproc.c` and other files.
5. ************resume user code************

## System Call Tracing (moderate)

Add an mask integer in process structure. When the actual system call returns to `syscall()`, test the process mask with the system call number and print a line.

> How to pass the mask to child processes?
> 
- Initial every process with an empty mask while allocating.
- The `sys_trace()` writes the mask integer of process from its argument. If the user doesn't call `trace()`, all processes will have empty masks and hence avoid printing lines.
- `fork()` will copy mask number to the child process.
- code
    
    ```c
    // kernel/syscall.c
    void
    syscall(void)
    {
      int num;
      struct proc *p = myproc();
    
      num = p->trapframe->a7;
      if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        p->trapframe->a0 = syscalls[num]();
        if (p->mask_bits & (0x1 << num))
          printf("%d: syscall %s -> %d\n", p->pid, syscall_names[num - 1], p->trapframe->a0);
      } else {
        printf("%d %s: unknown sys call %d\n",
                p->pid, p->name, num);
        p->trapframe->a0 = -1;
      }
    }
    ```
    

## Sysinfo (moderate)

- *freemem* - Traverse the free page list and get the result by multiplying `PGSIZE`.
- *nproc* - Traverse the process array and count the number of used ones.
- `sys_sysinfo()` copy the result numbers to the given address by calling `copyout()`.
- code
    
    ```c
    // kernel/sysproc.c
    uint64
    sys_sysinfo(void)
    {
      uint64 s;     // address of struct sysinfo
      uint64 res[2];
    
      if (argaddr(0, &s) < 0)
        return -1;
    
      res[0] = freemem();
      res[1] = nproc();
    
      struct proc *p = myproc();
      if (copyout(p->pagetable, s, (char *)res, sizeof(res)) < 0)
        return -1;
      return 0;
    }
    ```
    
    ```c
    // kernel/kalloc.c
    uint64 freemem(void)
    {
      uint size = 0;
      struct run *ptr = kmem.freelist;
      while (ptr != 0) {
        size += PGSIZE;
        ptr = ptr->next;
      }
      return size;
    }
    ```
    
    ```c
    // kernel/proc.c
    // Return the number of used processes, proc.c
    uint64
    nproc(void) {
      uint num = 0;
      struct proc *p;
      for (p = proc; p < &proc[NPROC]; p++) {
        if (p->state != UNUSED)
          num++;
      }
      return num;
    }
    ```

# [Lab page tables](https://pdos.csail.mit.edu/6.828/2021/labs/pgtbl.html)

In this lab, the most important reference is `kernel/vm.c`, which relates to the creation, mapping, and freeing of page tables. Three functions are the quite useful in this lab:

1. `mappages()`: Setup the page table entries with physical address usually used with `kalloc()`.
2. `walk()`: Fetch or create the corresponding page table entry in the three-level tree.
3. `uvmunmap()`: Unmap vm pages by writing the page table entries to 0, optionally free the pointed physical memory.

## Speed up system calls *(moderate)*

We can map a new **read-only** (don't forget `PTE_U`) page at `va=USYSCALL`, and store the information in the pointed physical page.

If user calls `ugetpid()`, function will directly return the pid stored at `USYSCALL` in user mode, instead of asking kernel to return `getpid()`.

- code
    
    ```c
    // kernel/proc.c
    pagetable_t
    proc_pagetable(struct proc *p)
    {
      ...
      // map struct usyscall page to va = USYSCALL
      if (mappages(pagetable, USYSCALL, PGSIZE, 
                   (uint64)(p->usyscall_pg), PTE_R | PTE_U) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmunmap(pagetable, TRAPFRAME, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
      }
      return pagetable;
    }
    
    static struct proc*
    allocproc(void)
    {
    	...
    found:
      ...
      // allocate a physical page for usyscall
      if ((p->usyscall_pg = kalloc()) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
      }
      memset(p->usyscall_pg, 0, PGSIZE);
      struct usyscall temp;
      temp.pid = p->pid;
      memmove(p->usyscall_pg, &temp, sizeof(temp));
    
      // An empty user page table.
      p->pagetable = proc_pagetable(p);
      if(p->pagetable == 0){
        freeproc(p);
        release(&p->lock);
        return 0;
      }
    	...
      return p;
    }
    
    void
    proc_freepagetable(pagetable_t pagetable, uint64 sz)
    {
      uvmunmap(pagetable, TRAMPOLINE, 1, 0);
      uvmunmap(pagetable, TRAPFRAME, 1, 0);
      uvmunmap(pagetable, USYSCALL, 1, 0);
      uvmfree(pagetable, sz);
    }
    
    static void
    freeproc(struct proc *p)
    {
      if(p->trapframe)
        kfree((void*)p->trapframe);
      p->trapframe = 0;
      if (p->usyscall_pg)
        kfree(p->usyscall_pg);
      p->usyscall_pg = 0;
      ...
    }
    ```
    
    ```c
    // user/ulib.c
    int
    ugetpid(void)
    {
    	struct usyscall *u = (struct usyscall *) USYSCALL;
      return u->pid;
    }
    ```
    

## Print a page table *(moderate)*

Essentially this is a pre-order traversal in N-ary tree. You can imagine:

1. Every page table is a node, and each pte is a pointer to one child node.
2. If the pte is valid and labeled as `pte & (PTE_W|PTE_R|PTE_X) == 0`, then this node is an internal node.
3. Recursively call `vmprint()` for internal nodes after printing the lines.

The indentation level can be passed as the second argument in the recursive function. You can construct the prefix string before traversing.

- code
    
    ```c
    // kernel/vm.c
    static void 
    vmprint_helper(pagetable_t pagetable, int level) {
      // level = 2: ' ..'
      // level = 1: ' .. ..'
      // level = 0: ' .. .. ..'
      char prefix[(3-level) * 3 + 1];
      prefix[(3-level) * 3] = 0;
      for (int i = 0; i < (3-level) * 3; i += 3) {
        prefix[i] = ' ';
        prefix[i+1] = '.';
        prefix[i+2] = '.';
      }
    
      for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if (pte & PTE_V) {
          printf("%s%d: pte %p pa %p\n", prefix, i, pte, PTE2PA(pte));
          if ((pte & (PTE_R|PTE_W|PTE_X)) == 0) {
            // pte contains the pa of sub-pagetable
            vmprint_helper((pagetable_t)PTE2PA(pte), level - 1);
          }
        }
      }
    }
    
    void
    vmprint(pagetable_t pagetable) {
      printf("page table %p\n", pagetable);
      vmprint_helper(pagetable, 2);
    }
    ```
    

## Detecting which pages have been accessed *(hard)*

I think this task is easy level. It is quite straight-forward:

1. Call multiple `walk()` functions in the given range.
2. For each pte returned, check whether the `PTE_A` bit is set and clear it.
3. Set the corresponding bit based on the for loop counter `i` and shifting.
4. `copyout` the bitmask to the destination user address.

My suggestion is **setting a small size for the bitmask** (like 64 or 128) or you can allocate space from the input number (I use this approach). If you set the size too much, like 512 or 1024, the kernel stack will probably overflow and you can see a bizarre bug about page fault (and spend several hours on it just like I did 😂).

- code
    
    ```c
    // kernel/vm.c
    int
    pgaccess(uint64 va, int pages, void *dest) {
      if (pages < 0 || pages > 1024) return -1;
    
      char mask[(pages + 7) >> 3];
      memset(mask, 0, sizeof(mask));    // clear all bits first
    
      struct proc *p = myproc();
      pagetable_t pagetable = p->pagetable;
    
      uint64 start = PGROUNDDOWN(va);
    
      for (int i = 0; i < pages; i++) {
        uint64 a = start + i * PGSIZE;
        // check whether the pte is valid
        pte_t* pte;
        if ((pte = walk(pagetable, a, 0)) == 0)
          continue;
        if ((*pte & PTE_V) == 0)
          continue;
    		// is valid
        if (*pte & PTE_A)
          mask[i / 8] |= (1L << (i % 8));     // set corresponding bit = 1
        *pte &= ~PTE_A;     // clear PTE_A after checking
      }
    
      return copyout(pagetable, (uint64)dest, mask, sizeof(mask));
    }
    ```

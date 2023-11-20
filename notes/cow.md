# [Lab Copy-on-Write](https://pdos.csail.mit.edu/6.828/2021/labs/cow.html)

## Implement Copy-on-Write ***(hard)***

We need to implement copy-on-write optimization in fork calls. Let's follow the hints: When user calls `fork()`, a new process will create a new page table (`uvmcopy()`), which contains the same ptes as the parent. And we clear the write flag `PTE_W` for every valid page in both tables. 

Is this enough? Let's move on and leave it okay temporarily.

### Copy Page Table on Fork

So next, a store instruction in user code causes a store page fault `scause = 15`, we go into the `usertrap()` routine, a general exception handler. We then retrieve the faulting virtual address from `stval` register and walk through the page table to find the cause. Suppose we perfectly find it, now shall we allocate a new page and restore the writing bit? Wait! If this page was read-only before copying, now we have a writable page in one of the processes, that's horrible!

Now the question comes, *how can you decide the page is originally read-only or r/w?* So we need to do something in the `uvmcopy()` so that it is easy to distinguish the type. My solution is using a bit in the RSW area:

![64-bit page table entry in risc-v](Lab%20Copy-on-Write%20b3828e500f044ab6a420edf4ee37d404/Untitled.png)

64-bit page table entry in risc-v

```c
#define PTE_COW (1L << 8)    // 1: cow page, 0: read-only page
```

When copying the page table, we only **set the cow bit if this page was writable** for both tables. Now we are trapped in the page fault handler, we can allocate pages only for the cow ones and leave the read-only ones just there. Don't forget reset the flag bits:

```c
*pte |= PTE_W;
*pte &= ~PTE_COW; // not cow any more, avoid causing another copy
```

The faulting process restarts the store instruction, and this time it is writable.

### Add Reference Count for Physical Pages

Now the user wants to terminate its process, and kernel calls `uvmfree()` in `kernel/vm.c` file. The `do_free` option is set in the `uvmunmap()` function, and we free all physical pages belonging to the process.

What about the read-only pages that have multiple mappings? If one process frees it, any other process have no access to this physical page. That's why we need to record the number of references to every physical page allocated by `kalloc()`. (kernel data and text are excluded) We only free the pages having no references.

How can we maintain this data structure? In my solution, just a `uint` array that have the same number entries as physical pages.

- `kalloc()` set the count number to 1 before returning.
- `kfree()` decrements the count by 1. If it is 0, we actually free this page.
- `uvmcopy` increments the corresponding count number for each page by 1.
- Page fault handler calls `kfree(old_pa)` after updating the pte and copying memory.

This approach requires few changes in the code: The kernel calls `kfree()` just as usual when freeing the process; If the cow page has only one external reference and that process is going to write, kernel simply allocates a new physical page to copy and frees the old one actually.

- code
    
    ```c
    // kernel/kalloc.c
    int refcnt[(PHYSTOP - KERNBASE) / PGSIZE];
    #define         REFINDEX(pa) ((pa - KERNBASE) >> 12)
    
    void
    kfree(void *pa)
    {
      struct run *r;
    
      if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");
    
      if (--refcnt[REFINDEX((uint64)pa)] > 0) {
        return;
      }
    
      // do the normal kfree()
    }
    
    void *
    kalloc(void)
    {
      ...
      if(r) {
        memset((char*)r, 5, PGSIZE); // fill with junk
        refcnt[REFINDEX((uint64)r)] = 1;
      }
      return (void*)r;
    }
    ```
    
    ```c
    // kernel/vm.c
    int
    uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
    {
      pte_t *pte;
      uint64 pa, i;
      uint flags;
      // char *mem;
    
      for(i = 0; i < sz; i += PGSIZE){
        if((pte = walk(old, i, 0)) == 0)
          panic("uvmcopy: pte should exist");
        if((*pte & PTE_V) == 0)
          panic("uvmcopy: page not present");
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        // map page table entry
        if(mappages(new, i, PGSIZE, pa, flags) != 0){
          goto err;
        }
        pte_t *new_pte = walk(new, i, 0);
        if (new_pte == 0) {
          printf("uvmcopy: new pte not exists\n");
          goto err;
        }
        // update flags: clear write, set cow
        if (*pte & PTE_W) {
          *pte &= ~PTE_W;
          *new_pte &= ~PTE_W;
          *pte |= PTE_COW;
          *new_pte |= PTE_COW;
        }
        // phyical page reference +1
        refcnt[REFINDEX(pa)]++;
      }
      return 0;
    
     err:
      uvmunmap(new, 0, i / PGSIZE, 1);
      return -1;
    }
    ```
    
    ```c
    // kernel/trap.c
    void
    usertrap(void)
    {
      ...
      uint64 scause = r_scause();
    
      if(scause == 8){
        // system call
    		...
      } else if (scause == 15) {
        // store page fault
        uint64 fva = r_stval();   // stval <- faulting va
        pte_t *pte;
        int flag = is_cow(p->pagetable, fva);
        if (flag < 0) {
          // not valid or read-only page
          p->killed = 1;
        }
        else if (flag == 0) {
          // writable page but with storing pgfault
          panic("pgfault: writable page triggers\n");
        }
        else {
          // copy-on-write page
          pte = walk(p->pagetable, fva, 0);
          if (cow_alloc(pte) == 0)
            p->killed = 1;
        }
      } else if((which_dev = devintr()) != 0){
        // ok
      } else {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        p->killed = 1;
      }
    
      ...
    }
    ```
    
    ```c
    // kernel/vm.c
    // Given the pte of cow page, allocate a new physical page, update the pte,
    // reset cow page to writable flag.
    // Return the address of the same pte if success, 0 if error
    void*
    cow_alloc(pte_t *pte) {
      if (*pte == 0 || (*pte & PTE_V) == 0)
        return 0;
      if ((*pte & PTE_COW) == 0)
        return 0;
    
      uint64 new_pa = (uint64)kalloc(); // allocate a new page
      if (new_pa == 0) {
        // memory not enough
        return 0;
      }
    
      uint64 old_pa = PTE2PA(*pte);
      memmove((void *)new_pa, (void *)old_pa, PGSIZE);  // copy page
      kfree((void *)old_pa);    // ref--, free the old page if ref=0
    
      *pte &= ~PTE_COW; // clear cow flag
      *pte |= PTE_W; // set write flag back
      *pte = PA2PTE(new_pa) | PTE_FLAGS(*pte);
    
      return pte;
    }
    
    // given pagetable and virtual address,
    // return 1 if the page at va is cow, 0 if writable, -1 if not valid or read-only
    int
    is_cow(pagetable_t pagetable, uint64 va) {
      // is va valid address?
      if (walkaddr(pagetable, va) == 0)
        return -1;
      pte_t* pte;
      pte = walk(pagetable, va, 0);
      if (*pte & PTE_COW)
        return 1;
      return (*pte & PTE_W)? 0 : -1;
    }
    ```

# [Lab Multithreading](https://pdos.csail.mit.edu/6.828/2021/labs/thread.html)

## Uthread: switching between threads *(moderate)*

This task is very similar to scheduling different processes in xv6 kernel:

1. User thread yield the CPU ↔ One process yield CPU in its kernel thread: `yield()`
2. User threads and xv6 processes all have to store their own running context: `proc->context`, `cpu->context`
3. Switching threads and processes both require storing and loading registers: `kernel/swtch.S`

Note:

- When the process creates a thread, it will store its starting address to `ra` in context. During the (fake) switch, a `ret` instruction will jump to the beginning of thread functions.
- Every thread has its own stack. We need to save `sp` in context in advance and change it to the right address after switching.
- **User stacks grow downwards!**
- code
    
    ```c
    // user/uthread.c
    struct thread {
      char       reg[112];          /* the thread's context saved here */
      char       stack[STACK_SIZE]; /* the thread's stack */
      int        state;             /* FREE, RUNNING, RUNNABLE */
    };
    
    void 
    thread_schedule(void)
    {
      ...
      if (current_thread != next_thread) {         /* switch threads?  */
        next_thread->state = RUNNING;
        t = current_thread;
        current_thread = next_thread;
    
        thread_switch((uint64)t->reg, (uint64)next_thread->reg);
      } else
        ...
    }
    
    void 
    thread_create(void (*func)())
    {
      struct thread *t;
    
      for (t = all_thread; t < all_thread + MAX_THREAD; t++) {
        if (t->state == FREE) break;
      }
      t->state = RUNNABLE;
      // YOUR CODE HERE
      // initialize sp -> t.stack
      *(uint64 *)(t->reg) = (uint64)func;   // ra = thread function
      *(uint64 *)(t->reg + 8) = (uint64)(t->stack + STACK_SIZE);   // sp = thread stack
    }
    ```
    

## Using threads *(moderate)*

We have to add locks to preserve concurrent reading and writing to a simple hash table.

- Add a single lock to the hash table. Every operation need to acquire and release the lock. → Each thread have to wait other threads to finish visiting the hash table. They seems to run sequentially, so there’s no performance benefit by using multiple threads.
- Assign one lock to each bucket. Every operation only reads or modifies the linked list in one bucket. We can only locks one element in the table entries. → Threads visiting different entries can actually run in parallel.
- code
    
    ```c
    // notxv6/ph.c
    pthread_mutex_t locks[NBUCKET];   // separate lock for different bucket
    
    static struct entry*
    get(int key)
    {
      int i = key % NBUCKET;
      pthread_mutex_lock(&locks[i]);
    	...
      pthread_mutex_unlock(&locks[i]);
      return e;
    }
    
    static 
    void put(int key, int value)
    {
      int i = key % NBUCKET;
      pthread_mutex_lock(&locks[i]);
    	...
      pthread_mutex_unlock(&locks[i]);
    }
    ```
    

## Barrier *(moderate)*

A point in an application at which all participating threads must wait until all other participating threads reach that point too. Here are my thoughts:

1. `bstate.round` represents which barrier **the farthest thread is on or will be on.** Besides, every thread will know the number of its own current barrier.
2. `bstate.nthread` represents the number threads blocked on the `bstate.round`, **not the number threads being blocked.**
3. The last coming thread acts as the “yelling person”. It should update the new round and clear the number of threads.

![“yelling person” will notify others to move forward](./assets/barrier.svg)

“yelling person” will notify others to move forward

- If the current round is equal to the newest round and somebody falls behind (nobody has yelled before) → wait
- If all threads have reached the barrier, that thread must update the round count and notify others. → yell
- code
    
    ```c
    // notxv6/barrier.c
    static void 
    barrier()
    {
      // YOUR CODE HERE
      //
      // Block until all threads have called barrier() and
      // then increment bstate.round.
      //
      pthread_mutex_lock(&bstate.barrier_mutex);    // acquire the condition lock
    
      bstate.nthread++;     // one thread has reached barrier
      
      int myround = bstate.round;   // record the current barrier number
    
      while (myround == bstate.round && bstate.nthread < nthread) {
        // no thread has passed this and there's someone else falling behind
        pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);   // wait for other threads
      }
    
      if (bstate.nthread >= nthread) {
        // all threads reach the barrier, yell!
        bstate.nthread = 0;
        bstate.round++;
        pthread_cond_broadcast(&bstate.barrier_cond);   // wake up other threads
      }
      
      pthread_mutex_unlock(&bstate.barrier_mutex);
    }
    ```
    
    > Why do you record the current round of every thread in `myround`?
    > 
    
    If the first condition is not specified in the while loop, all threads (except the yelling one) check that `bstate.nthread == 0 < nthread` , they continue sleeping. In other word, all waiting threads check the round condition, and one yelling thread checks the latter.

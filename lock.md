# [Lab Lock](https://pdos.csail.mit.edu/6.828/2021/labs/lock.html)

## Memory Allocator *(moderate)*

- Original Version: All kernel threads will allocate and free memory by the same allocator, which contains a free buffer linked list and a single lock. This will lead to heavy lock contention when multiple threads are allocating physical memory.
- Refined Version: All CPUs have their own memory allocator (a free buffer linked-list) and their own lock to protect the list. Ideally, there will be no lock contention and all CPU threads manipulate their own list.

    ```c
    struct {
      struct spinlock lock;
      struct run *freelist;
    } kmem[NCPU];
    ```

There is a problem: When a list on one CPU has run out, this CPU thread has to borrow a free buffer from another CPU’s allocator. And this will cause one CPU to **hold the other one’s lock** and hence incurs lock contention. But this situation occurs very rarely.

The basic idea of `kalloc()`:

1. Initialize all buffers onto one single list of the CPU.
2. `kalloc` will acquire the current lock, search the list for an available buffer to use. If there is one, pick it off the list.
3. If no available buffer is on the current list, it has to borrow one from others’ lists. It needs to traverse along all lists, hold their locks, try to pick one buffer off the list and release the lock.

    > **Remember: release current `kmem` lock before your traversal to avoid deadlock.**
    >
4. Return the available block.

- code

    ```c
    // kernel/kalloc.c
    void *kalloc(void)
    {
      struct run *r;
    
      push_off();
      int cid = cpuid();
      pop_off();
    
      acquire(&kmem[cid].lock);
      r = kmem[cid].freelist;
      if(r)
        kmem[cid].freelist = r->next;
      release(&kmem[cid].lock);
    
      if (r == 0) {
        // search other free lists
        for (int i = 0; i < NCPU; i++) {
          if (i == cid)
            continue;
          acquire(&kmem[i].lock);
          r = kmem[i].freelist;
          if (r) {
            // found
            kmem[i].freelist = r->next;
            release(&kmem[i].lock);
            break;
          }
          release(&kmem[i].lock);
        }
      }
    
      if(r)
        memset((char*)r, 5, PGSIZE); // fill with junk
      return (void*)r;
    }
    ```

`kfree()` is straight-forward: attach the freeing block to the current free list. You can just change the code by using array indices.

## Buffer Cache *(hard)*

The refined cache uses hash table to scatter visits uniformly. All buckets in the table are implemented as linked-lists and protected with separate locks. Therefore, visits to different buckets will have no lock contention.

```c
#define BUCKNUM 29

struct bucket {
  struct spinlock lock;
  struct buf *head;                // head of the linked list
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];            // buffer array contains data

  struct bucket table[BUCKNUM];    // hash table contains buffer pointers
} bcache;

int hash(uint dev, uint blockno) { return (dev * 67 + blockno) % BUCKNUM; }
```

`bget` using hash table:

1. Computes the hash value (bucket index) based on the `dev` and `blockno` values.
2. Acquire the lock of specific bucket and search for the buffer wanted.
    1. If found: Increment reference count, release the bucket lock and reacquire the buffer lock.
    2. If not found: find the unused buffer with the oldest usage time (ticks) in the whole cache and reattach it to the list of current bucket. Overwrite the buffer information and return.

`brelse` using hash table:

1. Acquire the bucket lock and decrement the reference count.
2. If it is unused now, update the ticks of the buffer for eviction in `bget`. (No need to attach it to the bucket list, because `bget` has done the work.)
3. Release the bucket lock.

Using hash table, all operations related to buffer cache **need only lock its own hash bucket**.

- code

    ```c
    // kernel/bio.c
    void binit(void) {
      struct buf *b;
    
      initlock(&bcache.lock, "bcache");
    
      for (int i = 0; i < BUCKNUM; i++) {
        initlock(&bcache.table[i].lock, "bcache");
        bcache.table[i].head = 0;
      }
    
      for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->refcnt = 0;
        initsleeplock(&b->lock, "buffer");
        insert_head(0, b);    // attach all buffers to the 0 bucket
      }
    }
    
    // Insert buffer into the given bucket
    // Caller must have bucket lock
    void insert_head(int index, struct buf *buf) {
      if (index < 0 || index >= BUCKNUM)
        panic("insert_head: index out of bound");
      buf->next = bcache.table[index].head;
      bcache.table[index].head = buf;
    }
    ```

    ```c
    static struct buf *bget(uint dev, uint blockno) {
      struct buf *b;
    
      int id = hash(dev, blockno);
    
      // visit the specific bucket, instead of the whole cache
      acquire(&bcache.table[id].lock);
      for (b = bcache.table[id].head; b != 0; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
          // found
          b->refcnt++;
          release(&bcache.table[id].lock);
    
          acquiresleep(&b->lock);
          return b;
        }
      }
      release(&bcache.table[id].lock);  // unlock for eviction
    
      // Not cached.
      b = evict();  // find the evicted buffer
      if (b) {
        acquire(&bcache.table[id].lock);
        insert_head(id, b);                 // insert to the current list
    
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
    
        release(&bcache.table[id].lock);
        acquiresleep(&b->lock);
        return b;
      }
      panic("bget: no buffers");
    }
    
    // Select an empty buffer with the smallest ticks and remove it from the original list
    // Return 0 if cache is full, else return the buffer with bucket locked
    // Must be called without any bucket locks or bcache lock
    struct buf *evict() {
      struct buf *bp;
      uint record = ~0x0;
      struct buf *record_ptr = 0;
    
      acquire(&bcache.lock);
    
    restart:
      // find the buffer with the smallest ticks
      for (int i = 0; i < BUCKNUM; i++) {
        acquire(&bcache.table[i].lock);
        for (bp = bcache.table[i].head; bp != 0; bp = bp->next) {
          if (bp->refcnt == 0) {
            if (bp->ticks < record) {
              record = bp->ticks;
              record_ptr = bp;
            }
          }
        }
        release(&bcache.table[i].lock);
      }
    
      if (record_ptr == 0) {
        release(&bcache.lock);
        return 0;
      }
    
      // remove it from the original list
      struct buf **ptr;
      int id = hash(record_ptr->dev, record_ptr->blockno);
      acquire(&bcache.table[id].lock);
      for (ptr = &bcache.table[id].head; *ptr != 0; ptr = &((*ptr)->next)) {
        if (*ptr == record_ptr) {
          if (record_ptr->refcnt > 0) {
            release(&bcache.table[id].lock);
            goto restart;
          }
          *ptr = (*ptr)->next;
          release(&bcache.table[id].lock);
          release(&bcache.lock);
          return record_ptr;
        }
      }
      release(&bcache.table[id].lock);
      panic("evict");
    }
    ```

    ```c
    void brelse(struct buf *b)
    {
      if(!holdingsleep(&b->lock))
        panic("brelse");
    
      releasesleep(&b->lock);
    
      int id = hash(b->dev, b->blockno);
    
      acquire(&bcache.table[id].lock);
      b->refcnt--;
      if (b->refcnt == 0) {
        // no one is waiting for it.
        b->ticks = ticks;
      }
    
      release(&bcache.table[id].lock);
    }
    ```

> Is it safe to just use bucket locks in eviction process?
>

If you look through the implementation, you will find that the eviction consists of 2 steps:

1. Find the unused buffer (`refcnt = 0`) with the oldest used time (`ticks`) in the whole cache. It will traverse all buckets to search and record the minimum ticks value.
2. Given the evicted buffer address `recorded_ptr`, search the corresponding bucket list for the buffer, and detach it from the list.

What if another process wants to evict just before 2 and after 1?

- They will find the same buffer for eviction, of course. One process will actually detach it from the linked list and reattach it to another list. Now, the other process will search in the original list, but either cannot find the buffer or the buffer is in usage now. Therefore, the eviction process has to be serialized and atomic.
- **My solution is to use cache lock `bcache.lock` to protect the whole eviction procedure.** Hence, Another process wanting to evict at the same time will be blocked.

> What will happen if `bget` or `brelse` is called between the 2 steps?
>
- `brelse` will just write the buffer information and cannot affect eviction.
- `bget` will increment the reference count of the wanted buffer and return. If the buffer is not the minimum ticks buffer selected in the 1st step, then it is ok to continue the removing step. But on the contrary, **if the buffer for `bget` is just the selected one, the next step will try to overwrite an used buffer and allocate it for another process! →** Therefore, when we try to evict the buffer in the 2nd step, we have to check whether its reference count has been incremented or used by another process. **If it is being used, we need to restart the eviction in the 1st step again.**

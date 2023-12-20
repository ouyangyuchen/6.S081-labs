// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BUCKNUM 29

struct bucket {
  struct spinlock lock;
  struct buf *head;
};

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  struct bucket table[BUCKNUM];
} bcache;

int hash(uint dev, uint blockno) { return (dev * 67 + blockno) % BUCKNUM; }

// Insert buffer into the given bucket
// Caller must have bucket lock
void insert_head(int index, struct buf *buf) {
  if (index < 0 || index >= BUCKNUM)
    panic("insert_head: index out of bound");
  buf->next = bcache.table[index].head;
  bcache.table[index].head = buf;
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

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
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

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
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

void
bpin(struct buf *b) {
  int id = hash(b->dev, b->blockno);
  acquire(&bcache.table[id].lock);
  b->refcnt++;
  release(&bcache.table[id].lock);
}

void
bunpin(struct buf *b) {
  int id = hash(b->dev, b->blockno);
  acquire(&bcache.table[id].lock);
  b->refcnt--;
  release(&bcache.table[id].lock);
}

#ifdef VM_SWAP_H
#define VM_SWAP_H
#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

#define SECTOR_FREE 0
#define SECTOR_USED 1

#define SECTOR_PER_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)

struct block *swap_block;
struct bitmap *swap_map;
struct lock swap_lock;

void swap_init(void);
size_t swap_out(void *frame);
void swap_in(size_t used_index, void *frame);
#endif
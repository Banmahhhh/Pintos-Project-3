#include "vm/swap.h"

struct block *global_swap_block;
struct bitmap *swap_map;
struct lock swap_lock;

/*called in threads/init.c/locate_block_devices
initialize swap_block, swap_map with 0 and swap_lock*/
void swap_init(void)
{
    global_swap_block = block_get_role(BLOCK_SWAP); //get a block fulfilling with BLOCK_SWAP
    swap_map = bitmap_create(block_size(global_swap_block)/8);
    bitmap_set_all(swap_map, SECTOR_FREE);
    lock_init(&swap_lock);
}

/*swap the content in the frame to a free block, and save the number of the sector to spte->swap_index*/
size_t swap_out(void *frame)
{
    lock_acquire(&swap_lock);
    size_t free_sector = bitmap_scan_and_flip(swap_map, 0, 1, SECTOR_FREE);
    int i;
    for(i=0; i<8; i++)
    {
        block_write(global_swap_block, free_sector * 8+i, (uint8_t*)frame+i*BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
    return free_sector;
}

/*swap in, read the content from sectors (block) to frame*/
void swap_in(struct sup_page_table_entry *spte)
{
    lock_acquire(&swap_lock);
    int i;
    for(i=0; i<8; i++)
    {
        block_read(global_swap_block, spte->swap_index*8+i, (uint8_t*)spte->uva+i*BLOCK_SECTOR_SIZE);
    }
    bitmap_flip(swap_map, spte->swap_index);
    lock_release(&swap_lock);
}

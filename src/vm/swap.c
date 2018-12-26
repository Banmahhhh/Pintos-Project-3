#include "vm/swap.h"

/*called in threads/init.c/locate_block_devices
initialize swap_block, swap_map with 0 and swap_lock*/
void swap_init(void)
{
    swap_block = block_get_role(BLOCK_SWAP); //get a block fulfilling with BLOCK_SWAP
    swap_map = bitmap_create(block_size(swap_block)/8);
    bitmap_set_all(swap_map, SECTOR_FREE);
    lock_init(&swap_lock);
}

/*swap the content in the frame to a free block*/
size_t swap_out(void *frame)
{
    lock_acquire(&swap_lock);
    size_t free_block = bitmap_scan_and_flip(swap_map, 0, 1, SECTOR_FREE);
    size_t i;
    for(i=0; i<8; i++)
    {
        block_write(swap_lock, free_block * 8+1, (uint8_t*)frame+i*BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
    return free_block;
}

/*swap in, read the content from sectors (block) to frame*/
void swap_in(size_t swapin_index, void* frame)
{
    lock_acquire(&swap_lock);
    bitmap_flip(swap_map, swapin_index);
    size_t i;
    for(i=0; i<8; i++)
    {
        block_read(swap_block, swapin_index*8+i, (uint8_t*)frame+i*BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
}

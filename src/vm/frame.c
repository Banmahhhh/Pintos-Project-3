#include "vm/frame.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/swap.h"

/*Function to initialize the frame table and lock*/
void frame_table_init (void){
    list_init(&frame_table);
    lock_init(&frame_table_lock);
}

/*Function to free the specific frame using given address*/
void frame_free (void *frame){
    struct list_elem *e;
    lock_acquire(&frame_table_lock);
    //Need lock cause we may have multipule access different processes
    for (e = list_begin(&frame_table); e != list_end(&frame_table);e = list_next(e)){
        struct frame_entry *fte = list_entry(e, struct frame_entry, elem);
        if (fte->frame == frame){
            //If we can find the frame within the given list
            list_remove(e);
            //First we remove it from the list
            free(fte);
            //Second we free the entire fte entry
            break;
        }
    }
    lock_release(&frame_table_lock);
    palloc_free_page(frame);
    //Finally we free that frame directly
}

/*Function to allocate the frame, the allocated frame will be added to the frame table*/
void* frame_allocate_user(struct sup_page_entry *spte) {
    void *kpage = palloc_get_page(PAL_USER | PAL_ZERO); //kpage is frame
    if(kpage != NULL) {
        // Successfully acquired a frame from user pool
        frame_add_to_table(kpage, spte);
        //Add that page to page table
        return kpage;
    }
    else {
        // NOT IMPLEMENTED ERROR: NEEDS SWAPPING
        //ASSERT(0);
        // not reached
        while(kpage == NULL)
          kpage = frame_evict(PAL_USER);
        frame_add_to_table(kpage, spte);
        return NULL;
    }
}

/*Function adding the allocated frame to the frame table*/
void frame_add_to_table (void *frame, struct sup_page_entry *spte){
    struct frame_entry *fte = malloc(sizeof(struct frame_entry));
    fte->frame = frame;
    //Set the address
    fte->owner = thread_current();
    fte->spte = spte;
    //Set the tid to be current thread's tid

    lock_acquire(&frame_table_lock);
    list_push_back(&frame_table, &fte->elem);
    //Add this thread to list
    lock_release(&frame_table_lock);
}
/*Function to evict a frame from the list*/
bool frame_evict (){
    struct list_elem *e;
    lock_acquire(&frame_table_lock);
    //Need lock cause we may have multipule access different processes
    while (1) //if all frames are used, choose the first one
      for (e = list_begin(&frame_table); e != list_end(&frame_table);e = list_next(e))
      {
          struct frame_entry *fra = list_entry(e, struct_entry, elem); //check each frame structure
          if(!fra->spte->pinning)
          {
            struct thread* thre = fra->owner;
            //if the page is recently accessed, reset it as not accessed
            if(pagedir_is_accessed(thre->pagedir, fra->spte->uva))
              pagedir_set_accessed(thre->pagedir, fra->spte->uva, false);
            //the frame, least recently used
            else
            {
              if(pagedir_is_dirty(thre->pagedir, fra->spte->uva) || fra->spt->type == SWAP)
              {
                if(fra->spte->type == MMAP)
                {
                  lock_acquire(&filesys_lock);
                  //write from frame to buffer
                  file_write_at(fra->spte->file, fra->frame, fra->spte->read_bytes, fra->spte->offset);
                  lock_release(&filesys_lock);
                }
                else{
                  fra->spte->type = SWAP;
                  //record the swapped frame
                  fra->spte->swap_index = swap_out(fra->frame);
                }
              }
              fra->spte->is_loaded = false; //change the is_loaded
              list_remove(&fra->elem); //remove the frame from frame table
              pagedir_clear_page(thre->pagedir, fra->spte->uva); //clean the corresponding page
              palloc_free_page(fra->frame);
              free(fra); //free the frame
              return palloc_get_page(flags); //the next free page, as one is evicted
            }
          }
      }
}

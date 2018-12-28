#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"

struct lock frame_table_lock;
/*Lock used when access the frame entry, because frame is not
owned by process, there might be a race condition#*/

struct list frame_table;
/*Global variable list to keep track of all frame*/

struct frame_entry {
	void *frame;
	// Pointer to the physical memory frame
   	struct thread *owner;
   	// The owner of the frame
   	struct sup_page_table_entry *spte;
   	// Pointer to the page table entry currently using this physical frame
   	struct list_elem elem;
};

/* Allocates a new physical frame for current user process. */
void* frame_allocate_user(struct sup_page_table_entry *spte);
void frame_table_init (void);
void frame_free (void *frame);
void frame_add_to_table (void *frame, struct sup_page_table_entry *spte);
void* frame_evict (void);

#endif /* vm/frame.h */

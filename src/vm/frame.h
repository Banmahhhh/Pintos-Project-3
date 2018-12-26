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
 	//tid_t tid;
 	// The tid of the thread using this memory frame
  struct sup_page_table_entry *aux;
 	//uint32_t *pte;
 	// Pointer to the page table entry currently using this physical frame
  struct list_elem elem;
};

/* Allocates a new physical frame for current user process. */
void* frame_allocate_user(void);

#endif /* vm/frame.h */

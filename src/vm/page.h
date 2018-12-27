#ifndef VM_PAGE_H
#define VM_PAGE_H
#define MAX_STACK_SIZE (1024*1024)//Set the MAx_STACK_SIZE to be 2MB

#include "threads/thread.h"
#include <hash.h>

/*Defind the different types of each page*/
#define FILE 0
#define SWAP 1
#define MMAP 2

struct sup_page_table_entry {
	uint8_t type;
	// Indicates whether the entry is a file, swap or mmap
   	void *uva;
   	// The user virtual address of physical memory page
   	bool writable;
   	// Whether the physical page is writable or not
   	bool is_loaded;
   	// Indicates whether the entry has been loaded into
    bool pinning;
    //buzhidaoshisha

   	// For files
   	struct file *file;
   	// The file the page was read from
   	size_t offset;
   	// The offset from which the page was read
   	size_t read_bytes;
   	// How many are valid read bytes
   	size_t zero_bytes;
   	// How many are zero bytes
 
    // For swap
   	size_t swap_index;
   	/*If the entry is of type swap and the entry has
    been swapped to disk, this indicates the sectors
    it has been swapped to on the swap partition*/
    struct hash_elem elem;
    // The hash element to add to the supplemental
 };

unsigned page_hash_func (const struct hash_elem *e, void *aux UNUSED);
bool page_hash_less_func (const struct hash_elem *elem1,const struct hash_elem *elem2,void *aux UNUSED);
void page_hash_action_func (struct hash_elem *e, void *aux UNUSED);
void page_table_init (struct hash *sup_page_table);
void page_table_destroy (struct hash *sup_page_table);
struct sup_page_table_entry * get_spte (void *uva);
bool page_load_swap (struct sup_page_table_entry * spte);
bool page_load_mmap (struct sup_page_table_entry * spte);
bool page_load_file (struct sup_page_table_entry * spte);
bool page_load (void *uva);

/* Allocates a new virtual page for current user process and install the page
    into the process' page table. */
//bool page_allocate_user(void *upage);
bool page_grow_stack (void *uva);

#endif /* vm/page.h */

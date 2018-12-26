#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <string.h>

/*Since the sup_page_table will be implemented as a hash table, those three function is needed by the the Pintos specifican*/
/*Returns a hash of element's data, as a value anywhere in the range of unsigned int.*/
unsigned page_hash_func (const struct hash_elem *e, void *aux UNUSED){
	struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry,elem);
   	return hash_int((int) spte->uva);
 }

/*Function to compare the keys stored in elements a and b. Returns true if a is less than b, false if a is greater than or equal to b.
If two elements compare equal, then they must hash to equal values.*/
bool page_hash_less_func (const struct hash_elem *elem1,const struct hash_elem *elem2,void *aux UNUSED){
   	struct sup_page_table_entry *spte1 = hash_entry(elem1, struct sup_page_table_entry, elem);
   	struct sup_page_table_entry *spte2 = hash_entry(elem2, struct sup_page_table_entry, elem);
   	if (spte1->uva < spte2->uva)
   		return true;
   	return false;
 }

/*Functions to perform free() action on hash elements.*/
void page_hash_action_func (struct hash_elem *e, void *aux UNUSED){
   struct sup_page_table_entry *spte = hash_entry(e, struct sup_page_table_entry, elem);
   free(spte);
}

/*Function to initialize the sup page table*/
void page_table_init (struct hash *sup_page_table){
	hash_init(sup_page_table, page_hash_func, page_hash_less_func, NULL);
}

/*Function to free the sup page table*/
void page_table_destroy (struct hash *sup_page_table){
	hash_destroy (sup_page_table, page_hash_action_func);
}

/*Function to find whether the current thread have the spte with given uva inside*/
struct sup_page_table_entry * get_spte (void *uva){
   	struct sup_page_table_entry spte;
   	spte.uva = pg_round_down(uva);
   	//We need to find the beginning of the page in order to hash.
    struct hash_elem *e = hash_find(&thread_current()->sup_page_table, &spte.elem);
   	if (e==NULL)
   		return NULL;
   	//Current thread don't have the given page.
   	return hash_entry (e, struct sup_page_table_entry, elem);
}

bool page_load_swap (struct sup_page_table_entry * spte){
    //Need impelementation
   	return false;
}

bool page_load_mmap (struct sup_page_table_entry * spte){
	//Need impelementation
   	return false;
}

/*Function to load page from a file*/
bool page_load_file (struct sup_page_table_entry * spte){
   	//Need impelementation
    return false;
}

/*Function to load in page using specific functions*/
bool page_load (void *uva){
    struct sup_page_table_entry * spte = get_spte(uva);
    //Using uva and get_spte() to acquire the specific spte
    if (spte==NULL)
      return false;
    bool success = false;
    //Whether load successful
    switch (spte->type){
      case FILE:
        success = page_load_file(spte);
        break;
      case SWAP:
        success = page_load_swap(spte);
        break;
      case MMAP:
        success = page_load_mmap(spte);
        break;
        default:
        printf("Unknown page type\n");
    }
    return success;
 }


bool page_allocate_user(void *upage) {
    void *kpage = frame_allocate_user();
    if(kpage == NULL) return false;  // Unknown error happened
    return install_page(upage, kpage, true);
}

/*Function to impelement stack growth when new page is needed (with given uav inside)*/
bool page_grow_stack (void *uva){
  if (PHYS_BASE - pg_round_down(uva) > MAX_STACK_SIZE)
    return false;
  //First we check whether we have reached the preset upper bound of stack.
  struct sup_page_table_entry *spte = malloc(
    sizeof(struct sup_page_table_entry));
  if (spte==NULL)
    return false;
  //Failed to allowcate the sup page entry
  spte->uva = pg_round_down(uva);
  spte->is_loaded = true;
  spte->type = SWAP;

  uint8_t *frame = frame_allocate_user();
  //Allowcate the new frame.
  if (frame==NULL){
      free(spte);
      return false;
  }

  if (install_page(spte->uva, frame, true)==false){
    //Whether we can map the frame to our given uav
    free(spte);
    frame_free(frame);
    return false;
  }

  return (hash_insert(&thread_current()->sup_page_table, &spte->elem) == NULL);
  //If all the process is OK, we will hash insert the new page into current thread's list.
}

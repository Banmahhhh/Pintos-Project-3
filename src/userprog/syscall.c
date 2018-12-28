#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/exception.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "vm/page.h"


// #define DEBUG

#define DEREF_INT(esp, offset) *(((int*)(esp))+(offset))
#define DEREF_UNSIGNED(esp, offset) *(((unsigned*)(esp))+(offset))
#define DEREF_PID_T(esp, offset) *(((unsigned*)(esp))+(offset))
#define DEREF_BUFFER(esp, offset) (void*)(*(((int*)(esp))+(offset)))
#define DEREF_CHARPTR(esp, offset) (char*)(*(((int*)(esp))+(offset)))

static void syscall_handler (struct intr_frame *);
static int fd_alloc(struct file * file);
static int mapid_alloc(struct file * file, void* vaddr, int n_pages);
static struct file* get_file_from_fd(int fd);
static struct file_desc* get_fdstruct_from_fd(int fd);

struct lock file_lock;
struct lock io_lock;

static bool is_valid_user_vaddr(const void* uvaddr, void* esp, bool write) {
    if(uvaddr != NULL && uvaddr < PHYS_BASE) {
        // not null and below PHYS_BASE
        // Grow the stack to esp
        if(is_user_vaddr(esp) && esp > PHYS_BASE - MAX_STACK_SIZE) page_grow_to_esp(esp);
        struct sup_page_table_entry* spte = get_spte(uvaddr);
        if(spte != NULL) {
            // Deny write to non-writable pages
            if (write) {
                if (spte->writable == false) return false;
            }
            // Load page if not loaded
            if(!spte->is_loaded) {
                page_load(uvaddr);
            }
            return true;
        }
        else {
            if (check_valid_stack_growth(uvaddr, esp)) {
                #ifdef DEBUG
                printf("grow user stack in kernel context, esp: %p, needed: %p\n",esp, uvaddr);
                #endif
                if( page_grow_stack(uvaddr)) {
                    #ifdef DEBUG
                    printf("grow successful: %p\n", uvaddr);
                    #endif
                    return true;
                } else{
                    #ifdef DEBUG
                    printf("stack growth failed\n");
                    #endif
                }
            } else {
                #ifdef DEBUG
                printf("invalid stack growth, uvaddr: %p, esp: %p\n", uvaddr, esp);
                #endif
            }
        }
    }
    return false;
}

static int fd_alloc(struct file * file) {
    // create a new file descriptor for current thread
    struct thread* t = thread_current();
    int fd_n = t->n_file_desc;
    t->n_file_desc ++;
    struct file_desc* fd = malloc(sizeof(struct file_desc));
    fd->file = file;
    fd->fd_number = fd_n;
    list_push_back(&t->file_desc, &fd->elem);
    return fd_n;
}

static int mapid_alloc(struct file * file, void* vaddr, int n_pages) {
    // create a new file descriptor for current thread
    struct thread* t = thread_current();
    int mapid = t->n_mmap++;
    struct mmap_desc* md = malloc(sizeof(struct mmap_desc));
    md->file = file;
    md->mapid = mapid;
    md->n_pages = n_pages;
    md->addr = vaddr;
    list_push_back(&t->mmap_desc, &md->elem);
    return mapid;
}

/* deacllocate a CLOSED file descriptor */
static int fd_dealloc(struct file_desc* target_file) {
    int fd = target_file->fd_number;
    list_remove(&target_file->elem);
    free(target_file);
    return fd;
}

/* deacllocate a WRITTEN BACK mmao descriptor */
static int md_dealloc(struct mmap_desc* target_mmap) {
    int md = target_mmap->mapid;
    list_remove(&target_mmap->elem);
    free(target_mmap);
    return md;
}

static struct file* get_file_from_fd(int fd) {
    struct file_desc* target_file = get_fdstruct_from_fd(fd);
    if(target_file == NULL) return NULL;
    return target_file->file;
}

static struct file_desc* get_fdstruct_from_fd(int fd) {
    struct thread* t = thread_current();
    if(fd < 2 || fd >= t->n_file_desc) return NULL;
    // Loop through list
    for(struct list_elem* iter = list_begin(&t->file_desc);
        iter != list_end(&t->file_desc);
        iter = list_next(iter)) {
        //do stuff with iter
        struct file_desc* this_fdstruct = list_entry(iter, struct file_desc, elem);
        if (this_fdstruct->fd_number == fd) return this_fdstruct;
    }
    return NULL;
}

static struct mmap_desc* get_mdstruct_from_md(int md) {
    struct thread* t = thread_current();
    if(md >= t->n_mmap) return NULL;
    // Loop through list
    for(struct list_elem* iter = list_begin(&t->mmap_desc);
        iter != list_end(&t->mmap_desc);
        iter = list_next(iter)) {
        //do stuff with iter
        struct mmap_desc* this_mdstruct = list_entry(iter, struct mmap_desc, elem);
        if (this_mdstruct->mapid == md) return this_mdstruct;
    }
    return NULL;
}

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
  lock_init(&io_lock);
}

static void
syscall_handler (struct intr_frame *f)
{
    if (!is_valid_user_vaddr(f->esp, f->esp, false) ||
        !is_valid_user_vaddr(f->esp+4, f->esp, false) ||
        !is_valid_user_vaddr(f->esp+8, f->esp, false) ||
        !is_valid_user_vaddr(f->esp+12, f->esp, false)) {
        exit(-1);
        return;
    }
    switch (DEREF_INT(f->esp, 0)) {
        case SYS_HALT: {
            halt();
            break;
        }
        case SYS_EXIT: {
            int exit_status = DEREF_INT(f->esp, 1);
            exit(exit_status);
            break;
        }
        case SYS_EXEC: {
            char* cmd_line = DEREF_CHARPTR(f->esp, 1);
            // check if is valid
            if(is_valid_user_vaddr(cmd_line, f->esp, false)) f->eax = exec(cmd_line);
            else exit(-1);
            break;
        }
        case SYS_WAIT: {
            tid_t pid = DEREF_PID_T(f->esp, 1);
            f->eax = wait(pid);
            break;
        }
        case SYS_CREATE: {
            char* file = DEREF_CHARPTR(f->esp, 4);
            unsigned initial_size = DEREF_UNSIGNED(f->esp, 5);
            if(is_valid_user_vaddr(file, f->esp, false)) f->eax = create(file, initial_size);
            else exit(-1);
            break;
        }
        case SYS_REMOVE: {
            char* file = DEREF_CHARPTR(f->esp, 1);
            if(is_valid_user_vaddr(file, f->esp, false)) f->eax = remove(file);
            else exit(-1);
            break;
        }
        case SYS_OPEN: {
            char* file = DEREF_CHARPTR(f->esp, 1);
            if(is_valid_user_vaddr(file, f->esp, false)) f->eax = open(file);
            else exit(-1);
            break;
        }
        case SYS_FILESIZE: {
            int fd = DEREF_INT(f->esp, 1);
            f->eax = filesize(fd);
            break;
        }
        case SYS_READ: {
            int fd = DEREF_INT(f->esp, 5);
            void* buffer = DEREF_BUFFER(f->esp, 6);
            unsigned size = DEREF_UNSIGNED(f->esp, 7);
            if(is_valid_user_vaddr(buffer, f->esp, true)) f->eax = read(fd, buffer, size);
            else exit(-1);
            break;
        }
        case SYS_WRITE: {
            int fd = DEREF_INT(f->esp, 5);
            void* buffer = DEREF_BUFFER(f->esp, 6);
            unsigned size = DEREF_UNSIGNED(f->esp, 7);
            if(is_valid_user_vaddr(buffer, f->esp, false))  {
                f->eax = write(fd, buffer, size);
            }
            else exit(-1);
            break;
        }
        case SYS_SEEK: {
            int fd = DEREF_INT(f->esp, 4);
            unsigned position = DEREF_UNSIGNED(f->esp, 5);
            seek(fd, position);
            break;
        }
        case SYS_TELL: {
            int fd = DEREF_INT(f->esp, 1);
            f->eax = tell(fd);
            break;
        }
        case SYS_CLOSE: {
            int fd = DEREF_INT(f->esp, 1);
            close(fd);
            break;
        }
        case SYS_MMAP: {
            int fd = DEREF_INT(f->esp, 4);
            void* addr = DEREF_BUFFER(f->esp, 5);
            //We do not check validity here since addr can be non-stack addresses
            f->eax = mmap(fd, addr);
            break;
        }
        case SYS_MUNMAP: {
            int fd = DEREF_INT(f->esp, 1);
            munmap(fd);
            break;
        }
    }

}

/* ============== Here starts syscall function implementation ============== */

void halt(void) {
    shutdown_power_off();
}

void exit(int exit_status) {
    struct thread* current = thread_current();
    current->my_stat->exit_status = exit_status;
    printf("%s: exit(%d)\n", current->name, exit_status);
    thread_exit();
}

tid_t exec(const char* cmd_line) {
    return process_execute(cmd_line);
}

int wait(tid_t pid) {
    return process_wait(pid);
}

bool create(const char* file, unsigned initial_size) {
    return filesys_create(file, initial_size);
}

bool remove(const char* file) {
    return filesys_remove(file);
}

int open(const char* file) {
    lock_acquire(&file_lock);
    struct file *f = filesys_open(file);
    lock_release(&file_lock);
    if(f != NULL) return fd_alloc(f);
    else return -1;
}

int filesize(int fd) {
    struct file* file = get_file_from_fd(fd);
    if(file == NULL) return -1;
    else return file_length(file);
}

int read(int fd, void* buffer, unsigned size) {
    char *cbuffer = (char*)buffer;
    unsigned bytes_read = 0;
    if(fd == STDIN_FILENO) {
        // read from stdin
        lock_acquire(&io_lock);           // use a lock to prevent race condition
        while( bytes_read < size ) {
            uint8_t byte = input_getc();
            if (byte != 0) {
                cbuffer[bytes_read] = byte;
                bytes_read++;
            }
            else break;
        }
        lock_release(&io_lock);
    } else  {
        // not stdin
        if (fd == STDOUT_FILENO) return -1;     // cannot read from stdout
        lock_acquire(&file_lock);
        struct file * f = get_file_from_fd(fd); // get file pointer
        if (f == NULL) {
            lock_release(&file_lock);           // release lock before returning
            return -1;
        }
        bytes_read = file_read(f, cbuffer, size);
        lock_release(&file_lock);
    }
    return bytes_read;
}

int write(int fd, void* buffer, unsigned size) {
    unsigned bytes_written = 0;
    if(fd == STDOUT_FILENO) {
        // write to stdout
        lock_acquire(&io_lock);
        putbuf(buffer, size);
        lock_release(&io_lock);
        bytes_written = size;
    } else {
        // not stdout
        if (fd == STDIN_FILENO) return -1;      // cannot write to stdin
        lock_acquire(&file_lock);
        struct file * f = get_file_from_fd(fd); // get file pointer
        if (f == NULL || f->deny_write == true) {
            lock_release(&file_lock);           // release lock before returning
            return -1;
        }
        bytes_written = file_write(f, buffer, size);
        lock_release(&file_lock);
    }
    return bytes_written;
}

void seek(int fd, unsigned position) {
    lock_acquire(&file_lock);
    struct file * f = get_file_from_fd(fd); // get file pointer
    if (f == NULL) {
        lock_release(&file_lock);           // release lock before returning
        return;
    }
    file_seek(f, position);
    lock_release(&file_lock);
}

unsigned tell(int fd) {
    lock_acquire(&file_lock);
    struct file * f = get_file_from_fd(fd); // get file pointer
    if (f == NULL) {
        lock_release(&file_lock);           // release lock before returning
        return 0;
    }
    unsigned position = file_tell(f);
    lock_release(&file_lock);
    return position;
}

void close(int fd) {
    lock_acquire(&file_lock);
    struct file_desc * target_file = get_fdstruct_from_fd(fd); // get file_desc pointer
    if (target_file == NULL) {
        lock_release(&file_lock);           // release lock before returning
        return;
    }
    file_close(target_file->file);
    fd_dealloc(target_file);
    lock_release(&file_lock);
}

int mmap(int fd, void* addr) {
    // check fd and addr
    if(fd == 0 || fd == 1 || addr == 0 || addr >= PHYS_BASE) return -1;
    struct file* f = get_file_from_fd(fd);
    if (f == NULL) return -1;
    f = file_reopen(f);
    if (f == NULL) return -1;
    // check file size
    int file_size = file_length(f);
    if(file_size == 0) return -1;
    bool multiple_pg_size = file_size % PGSIZE == 0;
    int n_pages = file_size/PGSIZE + (multiple_pg_size ? 0 : 1);
    // check validity of addr
    // page aligned?
    if(addr != pg_round_down(addr)) return -1;
    // overlaped?
    for(void* i=addr;i<addr+n_pages*PGSIZE;i+=PGSIZE) {
        if (i >= PHYS_BASE) return -1;
        if(get_spte(i) != NULL) return -1;
    }
    // Valid fd and addr. Start mmaping
    void* i = addr;
    for(i=addr;i<addr+(n_pages-1)*PGSIZE;i+=PGSIZE) {
        if(!page_grow_mmap(i, f, i-addr, PGSIZE, 0)) return -1;
    }
    int read_bytes = file_size - (file_size/PGSIZE)*PGSIZE;
    if(!page_grow_mmap(i, f, i-addr, read_bytes, PGSIZE-read_bytes)) return -1;
    // Successfully mapped, allocate new mapid
    int mapid = mapid_alloc(f, addr, n_pages);
    return mapid;
}

void munmap(int mapid) {
    struct mmap_desc *md = get_mdstruct_from_md(mapid);
    if(md == NULL) return;
    for (void* i = md->addr; i < md->addr+md->n_pages*PGSIZE; i+=PGSIZE) {
        struct sup_page_table_entry *spte = get_spte(i);
        mmap_release_page(i, spte->file, spte->offset, spte->read_bytes);
        //remove spte from supplementary page table
        page_delete_spte(spte);
    }
    file_close(md->file);
    md_dealloc(md);
}

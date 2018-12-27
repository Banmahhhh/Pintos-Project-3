#include "userprog/syscall.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
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
#include "vm/frame.h"
#include "vm/page.h"

// #define DEBUG

#define DEREF_INT(esp, offset) *(((int*)(esp))+(offset))
#define DEREF_UNSIGNED(esp, offset) *(((unsigned*)(esp))+(offset))
#define DEREF_PID_T(esp, offset) *(((unsigned*)(esp))+(offset))
#define DEREF_BUFFER(esp, offset) (void*)(*(((int*)(esp))+(offset)))
#define DEREF_CHARPTR(esp, offset) (char*)(*(((int*)(esp))+(offset)))

static void syscall_handler (struct intr_frame *);
static int fd_alloc(struct file * file);
static struct file* get_file_from_fd(int fd);
static struct file_desc* get_fdstruct_from_fd(int fd);

struct lock file_lock;
struct lock io_lock;

static struct sup_page_table_entry* is_valid_user_vaddr(const void* uvaddr, void* esp) {
    if(uvaddr == NULL || uvaddr >= PHYS_BASE) {
        // not null and below PHYS_BASE
        exit(ERROR);
    }
    void* result = pagedir_get_page(thread_current()->pagedir, uvaddr);
    if(result == NULL) exit(ERROR);

    bool load = false;

    struct sup_page_table_entry *spte = get_spte((void *) uvaddr);
    if(spte)
    {
      load_page(spte);
      load = spte->load;
    }
    else if(uvaddr >= esp - 32)
    {
      load = page_grow_stack((void*) uvaddr);
    }
    if(!load) exit(ERROR);
    return spte;
}

void check_buffer(void* buffer, unsigned size, void *esp, bool writable)
{
  char* temp_buffer = (char*) buffer;
  int i;
  for(i=0; i<(int*)size; i++, temp_buffer++)
  {
    struct sup_page_table_entry *spte = is_valid_user_vaddr((const void*)temp_buffer, esp);
    if(spte && writable)
    {
      if(!spte->writable)
        exit(ERROR);
    }
  }
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

/* deacllocate a CLOSED file descriptor */
static int fd_dealloc(struct file_desc* target_file) {
    int fd = target_file->fd_number;
    list_remove(&target_file->elem);
    free(target_file);
    return fd;
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
    if (!is_valid_user_vaddr((const void*)f->esp, f->esp) ||
        !is_valid_user_vaddr((const void*)f->esp+4, f->esp+4) ||
        !is_valid_user_vaddr((const void*)f->esp+8, f->esp+8) ||
        !is_valid_user_vaddr((const void*)f->esp+12, f->esp+12)) {
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
            if(is_valid_user_vaddr((const void*)cmd_line, f->esp)) f->eax = exec(cmd_line);
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
            if(is_valid_user_vaddr((const void*)file), f->esp) f->eax = create(file, initial_size);
            else exit(-1);
            break;
        }
        case SYS_REMOVE: {
            char* file = DEREF_CHARPTR(f->esp, 1);
            if(is_valid_user_vaddr((const void*)file, f->esp)) f->eax = remove(file);
            else exit(-1);
            break;
        }
        case SYS_OPEN: {
            char* file = DEREF_CHARPTR(f->esp, 1);
            if(is_valid_user_vaddr((const void*)file, f->esp)) f->eax = open(file);
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
            if(check_buffer((void*)buffer, (unsigned)size, f->esp, true)) f->eax = read(fd, buffer, size);
            else exit(-1);
            break;
        }
        case SYS_WRITE: {
            int fd = DEREF_INT(f->esp, 5);
            void* buffer = DEREF_BUFFER(f->esp, 6);
            unsigned size = DEREF_UNSIGNED(f->esp, 7);
            if(heck_buffer((void*)buffer, (unsigned)size, f->esp, false)) f->eax = write(fd, buffer, size);
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
    struct file *f = filesys_open(file);
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

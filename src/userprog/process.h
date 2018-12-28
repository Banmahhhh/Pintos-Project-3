#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool install_page (void *upage, void *kpage, bool writable);
bool install_page_user (void *upage, void *kpage, bool writable, struct thread *t);

struct arguments {
    char* exec_name;
    char* args;
    char* fn_copy;
    struct thread *parent;
    struct semaphore * chld_sema;
};

struct file_desc  {
    int fd_number;
    struct file* file;
    struct list_elem elem;
};

struct mmap_desc {
    int mapid;
    struct file* file;
    void* addr;
    int n_pages;
    struct list_elem elem;
};

struct chld_stat {
    struct thread *t;
    int tid;
    bool terminated;
    bool killed_by_kernel;
    int exit_status;
    struct list_elem elem;
};

#endif /* userprog/process.h */

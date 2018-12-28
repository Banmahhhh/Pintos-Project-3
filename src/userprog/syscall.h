#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "threads/synch.h"

extern struct lock file_lock;
extern struct lock io_lock;

void syscall_init (void);

void halt(void);
void exit(int exit_status);
tid_t exec(const char* cmd_line);
int wait(tid_t pid);
bool create(const char* file, unsigned initial_size);
bool remove(const char* file);
int open(const char* file);
int filesize(int fd);
int read(int fd, void* buffer, unsigned size);
int write(int fd, void* buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int mmap(int fd, void* addr);
void munmap(int mapid);

#endif /* userprog/syscall.h */

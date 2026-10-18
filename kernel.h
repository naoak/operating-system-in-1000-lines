#pragma once
#include "common.h"

#define PROCS_MAX 8
#define PROC_UNUSED   0
#define PROC_RUNNABLE 1
#define PROC_EXITED   2
#define PROC_WAITING  3
#define FILES_MAX   2
#define FILE_MAX_SIZE 1024
#define SECTOR_SIZE 512
#define DISK_MAX_SIZE (FILES_MAX * (SECTOR_SIZE + FILE_MAX_SIZE) + 2 * SECTOR_SIZE)
#define DISK_CAPACITY (32 * 1024)
#define INPUT_CAPACITY 256

struct process {
    int pid; // 0 if it's an idle process
    int state; // PROC_UNUSED, PROC_RUNNABLE, PROC_WAITING, PROC_EXITED
    int exit_code;
    uint8_t input[INPUT_CAPACITY];
    unsigned input_head;
    unsigned input_count;
};

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
    char data[];
} __attribute__((packed));

struct file {
    bool in_use;
    char name[100];
    char data[FILE_MAX_SIZE];
    size_t size;
};

// The host copies arguments here; user and kernel pointers are not shared.
struct syscall_message {
    char filename[100];
    char buf[FILE_MAX_SIZE];
};

#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);    \
        __builtin_trap();                                                      \
    } while (0)

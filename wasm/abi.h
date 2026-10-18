#pragma once
#include "../common.h"

#define EXPORT(name) __attribute__((export_name(name)))
#define IMPORT(module, name) \
    __attribute__((import_module(module), import_name(name)))

#define PROCS_MAX 8
#define PROC_UNUSED 0
#define PROC_RUNNABLE 1
#define PROC_WAITING 2
#define PROC_EXITED 3
#define INPUT_CAPACITY 256
#define FILES_MAX 2
#define FILE_CAPACITY 1024
#define NAME_CAPACITY 100
#define DISK_CAPACITY (32 * 1024)
#define WASM_PAGE_SIZE 65536u

// Only the trusted host can access this kernel-owned message buffer.
// User pointers refer to a different memory and must be copied by the host.
struct message {
    char name[NAME_CAPACITY];
    uint8_t data[FILE_CAPACITY];
};

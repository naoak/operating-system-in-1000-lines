#pragma once
#include "common.h"

struct sysret {
    int a0;
    int a1;
    int a2;
};

WASM_IMPORT("os", "syscall") int syscall(int sysno, int arg0, int arg1, int arg2);

void putchar(char ch);
// Returns -1 on input wait; the caller must save its state and return to the host.
int getchar(void);
int readfile(const char *filename, char *buf, int len);
int writefile(const char *filename, const char *buf, int len);
__attribute__((noreturn)) void exit(void);

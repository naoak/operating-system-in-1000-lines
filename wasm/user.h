#pragma once
#include "abi.h"

IMPORT("os", "syscall") int syscall(int number, int arg0, int arg1, int arg2);
void putchar(char ch);
int getchar(void);
int readfile(const char *name, char *buf, int len);
int writefile(const char *name, const char *buf, int len);
void process_exit(int code);

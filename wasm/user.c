#include "user.h"

void putchar(char ch) {
    syscall(SYS_PUTCHAR, (uint8_t) ch, 0, 0);
}

int getchar(void) {
    return syscall(SYS_GETCHAR, 0, 0, 0);
}

int readfile(const char *name, char *buf, int len) {
    return syscall(SYS_READFILE, (int) name, (int) buf, len);
}

int writefile(const char *name, const char *buf, int len) {
    return syscall(SYS_WRITEFILE, (int) name, (int) buf, len);
}

void process_exit(int code) {
    syscall(SYS_EXIT, code, 0, 0);
}

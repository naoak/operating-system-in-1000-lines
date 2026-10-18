#include "user.h"

// Each host invocation resumes input processing, then returns on input wait
// or after one command. Locals that must survive a wait live in linear memory.
WASM_EXPORT("step") void main(void) {
    static char cmdline[128];
    static unsigned i;
    static bool prompted, discarding;
    if (!prompted) {
        printf("> ");
        prompted = true;
    }
    for (unsigned budget = 0; budget < 64; budget++) {
        int ch = getchar();
        if (ch < 0)
            return;
        if (ch == '\r' || ch == '\n') {
            printf("\n");
            bool execute = !discarding;
            cmdline[i] = '\0';
            i = 0;
            prompted = discarding = false;
            if (execute)
                goto command;
            return;
        }
        if (discarding)
            continue;
        if (i == sizeof(cmdline) - 1) {
            printf("\ncommand line too long\n");
            discarding = true;
            continue;
        }
        putchar(ch);
        cmdline[i++] = ch;
    }
    return;

command:
    if (strcmp(cmdline, "hello") == 0)
        printf("Hello world from shell!\n");
    else if (strcmp(cmdline, "help") == 0)
        printf("hello  readfile  writefile  help  exit\n");
    else if (strcmp(cmdline, "exit") == 0)
        exit();
    else if (strcmp(cmdline, "readfile") == 0) {
        char buf[128];
        int len = readfile("hello.txt", buf, sizeof(buf) - 1);
        if (len < 0) {
            printf("cannot read hello.txt\n");
            return;
        }
        buf[len] = '\0';
        printf("%s\n", buf);
    }
    else if (strcmp(cmdline, "writefile") == 0)
        writefile("hello.txt", "Hello from shell!\n", sizeof("Hello from shell!\n") - 1);
    else
        printf("unknown command: %s\n", cmdline);
}

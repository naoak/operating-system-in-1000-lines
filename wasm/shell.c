#include "user.h"

static char cmdline[128];
static unsigned used;
static bool prompted, discarding;

static void execute(void) {
    if (!cmdline[0])
        return;
    if (strcmp(cmdline, "hello") == 0)
        printf("Hello world from shell!\n");
    else if (strcmp(cmdline, "help") == 0)
        printf("hello  readfile  writefile  help  exit\n");
    else if (strcmp(cmdline, "exit") == 0)
        process_exit(0);
    else if (strcmp(cmdline, "readfile") == 0) {
        char buf[128];
        int len = readfile("hello.txt", buf, sizeof(buf) - 1);
        if (len < 0) {
            printf("cannot read hello.txt\n");
            return;
        }
        buf[len] = '\0';
        printf("%s\n", buf);
    } else if (strcmp(cmdline, "writefile") == 0) {
        const char text[] = "Hello from shell!\n";
        if (writefile("hello.txt", text, sizeof(text) - 1) < 0)
            printf("cannot write hello.txt\n");
    } else
        printf("unknown command: %s\n", cmdline);
}

// Each invocation does bounded work and returns with its state in linear
// memory. No CPU register save/restore or hidden C call-stack switching.
EXPORT("step") void step(void) {
    if (!prompted) {
        printf("> ");
        prompted = true;
    }
    for (unsigned budget = 0; budget < 64; budget++) {
        int ch = getchar();
        if (ch < 0)
            return;
        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            if (!discarding) {
                cmdline[used] = '\0';
                execute();
            }
            used = 0;
            discarding = prompted = false;
            return;
        }
        if (discarding)
            continue;
        if (ch == '\b' || ch == 127) {
            if (used) {
                used--;
                printf("\b \b");
            }
        } else if (ch >= ' ') {
            if (used == sizeof(cmdline) - 1) {
                printf("\ncommand line too long\n");
                discarding = true;
            } else {
                cmdline[used++] = ch;
                putchar(ch);
            }
        }
    }
}

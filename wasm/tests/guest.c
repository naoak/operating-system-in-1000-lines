#include "../user.h"

static uint8_t buffer[1200];
static int number, arg0, arg1, arg2, result, calls, crash;

EXPORT("configure") void configure(int n, int a0, int a1, int a2) {
    number = n;
    arg0 = a0;
    arg1 = a1;
    arg2 = a2;
}

EXPORT("set_trap") void set_trap(int value) {
    crash = value;
}

EXPORT("buffer") unsigned get_buffer(void) {
    return (unsigned) buffer;
}

EXPORT("result") int get_result(void) {
    return result;
}

EXPORT("steps") int get_steps(void) {
    return calls;
}

EXPORT("step") void step(void) {
    if (crash)
        __builtin_trap();
    calls++;
    if (number)
        result = syscall(number, arg0, arg1, arg2);
}

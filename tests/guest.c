#include "../user.h"

static uint8_t buffer[1200];
static int number, arg0, arg1, arg2, result, calls, crash;

WASM_EXPORT("configure") void configure(int n, int a0, int a1, int a2) {
    number = n;
    arg0 = a0;
    arg1 = a1;
    arg2 = a2;
    result = 0x1234;
}

WASM_EXPORT("set_trap") void set_trap(int value) {
    crash = value;
}

WASM_EXPORT("buffer") unsigned get_buffer(void) {
    return (unsigned) buffer;
}

WASM_EXPORT("result") int get_result(void) {
    return result;
}

WASM_EXPORT("steps") int get_steps(void) {
    return calls;
}

WASM_EXPORT("step") void step(void) {
    if (crash)
        __builtin_trap();
    calls++;
    if (number)
        result = syscall(number, arg0, arg1, arg2);
}

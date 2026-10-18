#include "abi.h"

IMPORT("host", "putchar") void host_putchar(int pid, int ch);
IMPORT("host", "step") void host_step(int pid);

struct process {
    int state;
    int exit_code;
    uint8_t input[INPUT_CAPACITY];
    unsigned head, count;
};

struct file {
    char name[NAME_CAPACITY];
    uint8_t data[FILE_CAPACITY];
    unsigned size;
};

struct tar_header {
    char name[100], mode[8], uid[8], gid[8], size[12], mtime[12];
    char checksum[8], type, linkname[100], magic[6], version[2];
    char uname[32], gname[32], devmajor[8], devminor[8], prefix[155];
    char padding[12];
};

static struct process procs[PROCS_MAX];
static struct file files[FILES_MAX];
static unsigned file_count, disk_size, last_proc;
static int current_pid;
static uint8_t disk[DISK_CAPACITY];
static struct message message;
static unsigned heap_end;
extern uint8_t __heap_base[];

void putchar(char ch) {
    host_putchar(current_pid, (uint8_t) ch);
}

static struct process *process(int pid) {
    if (pid < 1 || pid > PROCS_MAX || procs[pid - 1].state == PROC_UNUSED)
        return NULL;
    return &procs[pid - 1];
}

EXPORT("alloc_pages") unsigned alloc_pages(unsigned count) {
    if (!heap_end)
        heap_end = align_up((unsigned) __heap_base, WASM_PAGE_SIZE);
    if (!count || count > (0xffffffffu - heap_end) / WASM_PAGE_SIZE)
        return 0;
    unsigned start = heap_end;
    unsigned end = start + count * WASM_PAGE_SIZE;
    unsigned pages = __builtin_wasm_memory_size(0);
    unsigned needed = end / WASM_PAGE_SIZE;
    if (needed > pages &&
        __builtin_wasm_memory_grow(0, needed - pages) == (size_t) -1)
        return 0;
    heap_end = end;
    memset((void *) start, 0, count * WASM_PAGE_SIZE);
    return start;
}

static int octal(const char *text, unsigned len) {
    unsigned value = 0;
    bool ended = false;
    for (unsigned i = 0; i < len; i++) {
        if (text[i] == '\0' || text[i] == ' ') {
            ended = true;
        } else {
            if (ended || text[i] < '0' || text[i] > '7' || value > 0x0fffffffu)
                return -1;
            value = value * 8 + text[i] - '0';
        }
    }
    return value <= 0x7fffffffu ? (int) value : -1;
}

static void format_octal(char *text, unsigned len, unsigned value) {
    text[--len] = '\0';
    while (len) {
        text[--len] = '0' + value % 8;
        value /= 8;
    }
}

static int fs_init(void) {
    unsigned offset = 0;
    while (offset + 512 <= disk_size) {
        struct tar_header *header = (struct tar_header *) &disk[offset];
        if (!header->name[0])
            return 0;
        int size = octal(header->size, sizeof(header->size));
        if (file_count == FILES_MAX || header->name[NAME_CAPACITY - 1] ||
            (header->type != '0' && header->type != '\0') ||
            header->prefix[0] || strcmp(header->magic, "ustar") ||
            size < 0 || size > FILE_CAPACITY ||
            offset + 512 + align_up((unsigned) size, 512) > disk_size)
            return -1;
        struct file *file = &files[file_count++];
        strcpy(file->name, header->name);
        file->size = size;
        memcpy(file->data, &disk[offset + 512], size);
        offset += 512 + align_up((unsigned) size, 512);
    }
    return -1;
}

static void fs_flush(void) {
    memset(disk, 0, disk_size);
    unsigned offset = 0;
    for (unsigned i = 0; i < file_count; i++) {
        struct file *file = &files[i];
        struct tar_header *header = (struct tar_header *) &disk[offset];
        strcpy(header->name, file->name);
        strcpy(header->mode, "0000644");
        strcpy(header->magic, "ustar");
        memcpy(header->version, "00", 2);
        header->type = '0';
        format_octal(header->size, sizeof(header->size), file->size);
        memset(header->checksum, ' ', sizeof(header->checksum));
        unsigned checksum = 0;
        for (unsigned j = 0; j < sizeof(*header); j++)
            checksum += disk[offset + j];
        format_octal(header->checksum, 7, checksum);
        header->checksum[7] = ' ';
        memcpy(&disk[offset + 512], file->data, file->size);
        offset += 512 + align_up(file->size, 512);
    }
}

EXPORT("disk_buffer") unsigned disk_buffer(void) {
    return (unsigned) disk;
}

EXPORT("message_buffer") unsigned message_buffer(void) {
    return (unsigned) &message;
}

EXPORT("init") int init(unsigned size) {
    // Reserve enough space to rewrite both files at their maximum size,
    // followed by the two end-of-archive blocks.
    if (size < FILES_MAX * (512 + FILE_CAPACITY) + 1024 ||
        size > DISK_CAPACITY || size % 512)
        return -1;
    disk_size = size;
    memset(procs, 0, sizeof(procs));
    memset(files, 0, sizeof(files));
    file_count = last_proc = current_pid = 0;
    if (fs_init() < 0)
        return -1;
    printf("1000 Lines OS / WebAssembly\n");
    printf("loaded %d files from disk\n", file_count);
    return 0;
}

EXPORT("create_process") int create_process(void) {
    for (int i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            procs[i].state = PROC_RUNNABLE;
            return i + 1;
        }
    }
    return -1;
}

EXPORT("process_state") int process_state(int pid) {
    struct process *p = process(pid);
    return p ? p->state : PROC_UNUSED;
}

EXPORT("process_exit_code") int process_exit_code(int pid) {
    struct process *p = process(pid);
    return p ? p->exit_code : -1;
}

EXPORT("kill_process") void kill_process(int pid, int code) {
    struct process *p = process(pid);
    if (p) {
        p->state = PROC_EXITED;
        p->exit_code = code;
    }
}

EXPORT("input") int input(int pid, int ch) {
    struct process *p = process(pid);
    if (!p || p->state == PROC_EXITED || p->count == INPUT_CAPACITY)
        return -1;
    p->input[(p->head + p->count++) % INPUT_CAPACITY] = ch;
    p->state = PROC_RUNNABLE;
    return 0;
}

EXPORT("input_space") unsigned input_space(int pid) {
    struct process *p = process(pid);
    return p && p->state != PROC_EXITED ? INPUT_CAPACITY - p->count : 0;
}

EXPORT("syscall") int syscall(int pid, int number, int arg, int len) {
    struct process *p = process(pid);
    // The host binds pid to the caller's instance. Only the running process
    // may issue syscalls; a user module cannot select another process's pid.
    if (!p || current_pid != pid || p->state != PROC_RUNNABLE)
        return -1;
    switch (number) {
        case SYS_PUTCHAR:
            putchar(arg);
            return 0;
        case SYS_GETCHAR:
            if (!p->count) {
                p->state = PROC_WAITING;
                return -1;
            }
            p->count--;
            arg = p->input[p->head];
            p->head = (p->head + 1) % INPUT_CAPACITY;
            return arg;
        case SYS_EXIT:
            kill_process(pid, arg);
            printf("process %d exited\n", pid);
            return 0;
        case SYS_READFILE:
        case SYS_WRITEFILE:
            if (len < 0 || len > FILE_CAPACITY ||
                message.name[NAME_CAPACITY - 1])
                return -1;
            for (unsigned i = 0; i < file_count; i++) {
                struct file *file = &files[i];
                if (strcmp(message.name, file->name))
                    continue;
                if (number == SYS_READFILE) {
                    if ((unsigned) len > file->size)
                        len = file->size;
                    memcpy(message.data, file->data, len);
                } else {
                    memcpy(file->data, message.data, len);
                    file->size = len;
                    fs_flush();
                }
                return len;
            }
            return -1;
        default:
            return -1;
    }
}

EXPORT("run") unsigned run(unsigned budget) {
    unsigned steps = 0;
    while (steps < budget) {
        int selected = -1;
        for (unsigned i = 0; i < PROCS_MAX; i++) {
            unsigned slot = (last_proc + i) % PROCS_MAX;
            if (procs[slot].state == PROC_RUNNABLE) {
                selected = slot;
                break;
            }
        }
        if (selected < 0)
            break;
        last_proc = (selected + 1) % PROCS_MAX;
        current_pid = selected + 1;
        host_step(current_pid);
        current_pid = 0;
        steps++;
    }
    return steps;
}

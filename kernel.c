#include "kernel.h"
#include "common.h"

extern char __heap_base[];

WASM_IMPORT("host", "putchar") void host_putchar(int pid, int ch);
WASM_IMPORT("host", "step") void host_step(int pid);

struct process procs[PROCS_MAX];
struct process *current_proc;
struct process *idle_proc;
static struct process idle;
static bool running_user;
static struct syscall_message message;

// Pages are offsets in this module's linear memory. User process memories
// belong to separate Wasm instances and are created by the host.
WASM_EXPORT("alloc_pages") void *alloc_pages(uint32_t n) {
    static uint32_t next_vaddr;
    if (!next_vaddr)
        next_vaddr = align_up((uint32_t) __heap_base, PAGE_SIZE);
    if (!n || n > (0xffffffffu - next_vaddr) / PAGE_SIZE)
        return NULL;

    uint32_t vaddr = next_vaddr;
    uint32_t end = vaddr + n * PAGE_SIZE;
    uint32_t pages = __builtin_wasm_memory_size(0);
    if (end / PAGE_SIZE > pages &&
        __builtin_wasm_memory_grow(0, end / PAGE_SIZE - pages) == (size_t) -1)
        return NULL;

    next_vaddr = end;
    memset((void *) vaddr, 0, n * PAGE_SIZE);
    return (void *) vaddr;
}

static uint8_t *blk_data;
uint64_t blk_capacity;

WASM_EXPORT("disk_buffer") void *disk_buffer(void) {
    if (!blk_data)
        blk_data = alloc_pages(align_up(DISK_CAPACITY, PAGE_SIZE) / PAGE_SIZE);
    return blk_data;
}

void blk_init(unsigned size) {
    blk_capacity = size;
    printf("disk: capacity is %d bytes\n", (int) blk_capacity);
}

void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("disk: tried to read/write sector=%d, but capacity is %d\n",
               sector, (int) (blk_capacity / SECTOR_SIZE));
        return;
    }

    if (is_write)
        memcpy(&blk_data[sector * SECTOR_SIZE], buf, SECTOR_SIZE);
    else
        memcpy(buf, &blk_data[sector * SECTOR_SIZE], SECTOR_SIZE);
}

struct file files[FILES_MAX];
uint8_t disk[DISK_MAX_SIZE];

int oct2int(char *oct, int len) {
    int dec = 0;
    for (int i = 0; i < len; i++) {
        if (oct[i] < '0' || oct[i] > '7')
            break;

        if ((unsigned) dec > (0x7fffffffu - (oct[i] - '0')) / 8)
            return -1;
        dec = dec * 8 + (oct[i] - '0');
    }
    return dec;
}

void fs_flush(void) {
    memset(disk, 0, sizeof(disk));
    unsigned off = 0;
    for (int file_i = 0; file_i < FILES_MAX; file_i++) {
        struct file *file = &files[file_i];
        if (!file->in_use)
            continue;

        struct tar_header *header = (struct tar_header *) &disk[off];
        memset(header, 0, sizeof(*header));
        strcpy(header->name, file->name);
        strcpy(header->mode, "000644");
        strcpy(header->magic, "ustar");
        memcpy(header->version, "00", 2);
        header->type = '0';

        int filesz = file->size;
        for (int i = sizeof(header->size) - 1; i > 0; i--) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        int checksum = ' ' * sizeof(header->checksum);
        for (unsigned i = 0; i < sizeof(struct tar_header); i++)
            checksum += (unsigned char) disk[off + i];

        for (int i = 5; i >= 0; i--) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        header->checksum[7] = ' ';
        memcpy(header->data, file->data, file->size);
        off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
    }

    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

    printf("wrote %d bytes to disk\n", sizeof(disk));
}

int fs_init(void) {
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, false);

    unsigned off = 0;
    for (int i = 0; i < FILES_MAX; i++) {
        struct tar_header *header = (struct tar_header *) &disk[off];
        if (header->name[0] == '\0')
            return 0;

        if (header->name[sizeof(header->name) - 1] ||
            header->prefix[0] ||
            (header->type != '0' && header->type != '\0') ||
            strcmp(header->magic, "ustar") != 0)
            return -1;

        int filesz = oct2int(header->size, sizeof(header->size));
        struct file *file = &files[i];
        if (filesz < 0 || filesz > (int) sizeof(file->data) ||
            off + align_up(sizeof(*header) + filesz, SECTOR_SIZE) > sizeof(disk))
            return -1;
        file->in_use = true;
        strcpy(file->name, header->name);
        memcpy(file->data, header->data, filesz);
        file->size = filesz;
        printf("file: %s, size=%d\n", file->name, file->size);

        off += align_up(sizeof(struct tar_header) + filesz, SECTOR_SIZE);
    }
    struct tar_header *end = (struct tar_header *) &disk[off];
    return off + sizeof(*end) <= sizeof(disk) && !end->name[0] ? 0 : -1;
}

struct file *fs_lookup(const char *filename) {
    for (int i = 0; i < FILES_MAX; i++) {
        struct file *file = &files[i];
        if (file->in_use && !strcmp(file->name, filename))
            return file;
    }

    return NULL;
}

void putchar(char ch) {
    host_putchar(current_proc ? current_proc->pid : 0, (uint8_t) ch);
}

long getchar(void) {
    if (!current_proc->input_count)
        return -1;
    int ch = current_proc->input[current_proc->input_head];
    current_proc->input_head = (current_proc->input_head + 1) % INPUT_CAPACITY;
    current_proc->input_count--;
    return ch;
}

struct process *create_process(void) {
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (!proc)
        return NULL;

    // The host instantiates the program with its own memory; there is no
    // kernel stack, page table, or executable image to copy into this memory.
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    return proc;
}

void yield(void) {
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    current_proc = next;
    if (next != idle_proc) {
        // Each call is one bounded quantum. The user program saves its state
        // before returning, so no CPU register or call-stack switching occurs.
        running_user = true;
        host_step(next->pid);
        running_user = false;
    }
}

WASM_EXPORT("handle_syscall")
int handle_syscall(int pid, int sysno, int arg0, int arg1, int arg2) {
    if (!running_user || current_proc->pid != pid ||
        current_proc->state != PROC_RUNNABLE)
        return -1;

    switch (sysno) {
        case SYS_PUTCHAR:
            putchar(arg0);
            return 0;
        case SYS_GETCHAR: {
            long ch = getchar();
            if (ch < 0)
                current_proc->state = PROC_WAITING;
            return ch;
        }
        case SYS_EXIT:
            printf("process %d exited\n", current_proc->pid);
            current_proc->state = PROC_EXITED;
            current_proc->exit_code = arg0;
            // The host unwinds this user's invocation after dispatch returns.
            return 0;
        case SYS_READFILE:
        case SYS_WRITEFILE: {
            // Only the host may pass kernel pointers, into the dedicated
            // message buffer after validating and copying the user arguments.
            if (arg0 != (int) message.filename || arg1 != (int) message.buf ||
                arg2 < 0 || arg2 > (int) sizeof(message.buf) ||
                message.filename[sizeof(message.filename) - 1])
                return -1;
            const char *filename = (const char *) arg0;
            char *buf = (char *) arg1;
            int len = arg2;
            struct file *file = fs_lookup(filename);
            if (!file) {
                printf("file not found: %s\n", filename);
                return -1;
            }

            if (sysno == SYS_READFILE && len > (int) file->size)
                len = file->size;

            if (sysno == SYS_WRITEFILE) {
                memcpy(file->data, buf, len);
                file->size = len;
                fs_flush();
            } else {
                memcpy(buf, file->data, len);
            }

            return len;
        }
        default:
            return -1;
    }
}

WASM_EXPORT("kernel_main") int kernel_main(unsigned disk_size) {
    if (!blk_data || disk_size < DISK_MAX_SIZE ||
        disk_size > DISK_CAPACITY || disk_size % SECTOR_SIZE)
        return -1;

    // Wasm initializes .bss on instantiation; the uploaded disk must survive.
    memset(procs, 0, sizeof(procs));
    memset(files, 0, sizeof(files));
    current_proc = NULL;
    printf("\n\n1000 Lines OS / WebAssembly\n");
    blk_init(disk_size);
    if (fs_init() < 0)
        return -1;

    idle_proc = &idle;
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    // The host loads the shell module, calls spawn(), then runs the scheduler.
    return 0;
}

// Host entry points. User modules only receive os.syscall, never these exports.
WASM_EXPORT("spawn") int spawn(void) {
    struct process *proc = create_process();
    return proc ? proc->pid : -1;
}

static struct process *lookup_process(int pid) {
    if (pid < 1 || pid > PROCS_MAX || procs[pid - 1].state == PROC_UNUSED)
        return NULL;
    return &procs[pid - 1];
}

WASM_EXPORT("process_state") int process_state(int pid) {
    struct process *proc = lookup_process(pid);
    return proc ? proc->state : PROC_UNUSED;
}

WASM_EXPORT("process_exit_code") int process_exit_code(int pid) {
    struct process *proc = lookup_process(pid);
    return proc ? proc->exit_code : -1;
}

WASM_EXPORT("kill_process") void kill_process(int pid, int code) {
    struct process *proc = lookup_process(pid);
    if (proc) {
        proc->state = PROC_EXITED;
        proc->exit_code = code;
    }
}

WASM_EXPORT("input_space") unsigned input_space(int pid) {
    struct process *proc = lookup_process(pid);
    return proc && proc->state != PROC_EXITED ? INPUT_CAPACITY - proc->input_count : 0;
}

WASM_EXPORT("input") int input(int pid, int ch) {
    struct process *proc = lookup_process(pid);
    if (!proc || !input_space(pid))
        return -1;
    proc->input[(proc->input_head + proc->input_count++) % INPUT_CAPACITY] = ch;
    proc->state = PROC_RUNNABLE;
    return 0;
}

WASM_EXPORT("run") unsigned run(unsigned budget) {
    unsigned steps = 0;
    while (steps < budget) {
        yield();
        if (current_proc == idle_proc)
            break;
        steps++;
    }
    return steps;
}

WASM_EXPORT("message_buffer") void *message_buffer(void) {
    return &message;
}

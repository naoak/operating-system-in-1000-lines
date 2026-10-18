// The host supplies loading, memory copies and device I/O. Process states,
// scheduling, file contents and syscall policy belong to kernel.wasm.
export const State = Object.freeze({ UNUSED: 0, RUNNABLE: 1, EXITED: 2, WAITING: 3 });
const SYS_EXIT = 3, SYS_READFILE = 4, SYS_WRITEFILE = 5;
const PROCESS_EXIT = Symbol("process exited");
const NAME_CAPACITY = 100, FILE_CAPACITY = 1024, DISK_CAPACITY = 32 * 1024;
const EFAULT = -14;

function bytes(value) {
  return value instanceof ArrayBuffer
    ? new Uint8Array(value)
    : new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
}

function range(memory, pointer, length) {
  const start = pointer >>> 0;
  const view = new Uint8Array(memory.buffer);
  if (!Number.isInteger(length) || length < 0 || start > view.length ||
      length > view.length - start) return null;
  return view.subarray(start, start + length);
}

async function compile(value) {
  return value instanceof WebAssembly.Module ? value : WebAssembly.compile(value);
}

export class WasmOS {
  static async create({ kernel, disk, onOutput = () => {} }) {
    const os = new WasmOS();
    os.processes = new Map();
    os.faults = [];
    os.kernel = new WebAssembly.Instance(await compile(kernel), {
      host: {
        putchar: (pid, ch) => onOutput(ch, pid),
        step: (pid) => os.step(pid),
      },
    }).exports;
    const image = bytes(disk);
    if (image.length > DISK_CAPACITY) throw new Error("Disk image exceeds 32 KiB");
    os.diskSize = image.length;
    const address = os.kernel.disk_buffer();
    if (!address) throw new Error("Cannot allocate the disk buffer");
    range(os.kernel.memory, address, image.length).set(image);
    if (os.kernel.kernel_main(image.length) !== 0) throw new Error("Invalid ustar disk image");
    return os;
  }

  async spawn(program) {
    const module = await compile(program);
    const pid = this.kernel.spawn();
    if (pid < 0) throw new Error("No free process slots");
    const entry = { instance: null };
    this.processes.set(pid, entry);
    try {
      entry.instance = new WebAssembly.Instance(module, {
        os: { syscall: (...args) => this.syscall(pid, ...args) },
      });
      const { memory, step } = entry.instance.exports;
      if (!(memory instanceof WebAssembly.Memory) || typeof step !== "function") {
        throw new Error("A process must export memory and step()");
      }
    } catch (error) {
      this.kernel.kill_process(pid, -1);
      this.processes.delete(pid);
      throw error;
    }
    return pid;
  }

  step(pid) {
    try {
      this.processes.get(pid).instance.exports.step();
    } catch (error) {
      if (error === PROCESS_EXIT) return;
      if (!(error instanceof WebAssembly.RuntimeError)) throw error;
      this.kernel.kill_process(pid, -1);
      this.faults.push({ pid, message: error.message });
    }
  }

  syscall(pid, number, arg0, arg1, arg2) {
    const instance = this.processes.get(pid)?.instance;
    if (!instance) return -1;
    if (number !== SYS_READFILE && number !== SYS_WRITEFILE) {
      const result = this.kernel.handle_syscall(pid, number, arg0, arg1, arg2);
      // Preserve exit()'s noreturn contract without a busy loop or a user trap.
      if (number === SYS_EXIT && result === 0) throw PROCESS_EXIT;
      return result;
    }
    if (arg2 < 0 || arg2 > FILE_CAPACITY) return -1;

    // Never interpret a user pointer as a kernel pointer, including on wasm32
    // where both are represented by integers. Check before copying either way.
    const memory = instance.exports.memory;
    const data = range(memory, arg1, arg2);
    const start = arg0 >>> 0;
    const user = new Uint8Array(memory.buffer);
    if (!data || start >= user.length) return EFAULT;
    const name = user.subarray(start, Math.min(start + NAME_CAPACITY, user.length));
    const end = name.indexOf(0);
    if (end < 0) return EFAULT;

    const address = this.kernel.message_buffer();
    const message = range(this.kernel.memory, address, NAME_CAPACITY + FILE_CAPACITY);
    message.fill(0, 0, NAME_CAPACITY);
    message.set(name.subarray(0, end), 0);
    if (number === SYS_WRITEFILE) message.set(data, NAME_CAPACITY);
    const result = this.kernel.handle_syscall(pid, number, address, address + NAME_CAPACITY, arg2);
    if (number === SYS_READFILE && result > 0) {
      data.set(range(this.kernel.memory, address + NAME_CAPACITY, result));
    }
    return result;
  }

  input(pid, text) {
    const encoded = new TextEncoder().encode(text.replace(/\r\n/g, "\n"));
    if (encoded.length > this.kernel.input_space(pid)) {
      throw new Error("Process input queue is full or the process has exited");
    }
    for (const ch of encoded) this.kernel.input(pid, ch);
  }

  run(budget = 32) {
    if (!Number.isInteger(budget) || budget < 1 || budget > 4096) {
      throw new RangeError("Scheduling budget must be between 1 and 4096");
    }
    return this.kernel.run(budget);
  }

  snapshot() {
    return range(this.kernel.memory, this.kernel.disk_buffer(), this.diskSize).slice();
  }
}

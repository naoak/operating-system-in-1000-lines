import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import test from "node:test";
import { WasmOS, State } from "../runtime.mjs";

const load = (name) => readFile(new URL(`../build/${name}`, import.meta.url));
const [kernel, shell, guest, disk] = await Promise.all([
  load("kernel.wasm").then(WebAssembly.compile),
  load("shell.wasm").then(WebAssembly.compile),
  load("guest.wasm").then(WebAssembly.compile),
  load("disk.tar"),
]);

async function boot(image = disk) {
  let output = "";
  const os = await WasmOS.create({ kernel, disk: image, onOutput: (ch) => {
    output += String.fromCharCode(ch);
  } });
  return { os, output: () => output };
}

function drain(os) {
  for (let i = 0; i < 100; i++) {
    if (os.run() < 32) return;
  }
  assert.fail("Processes did not go idle within the scheduling budget");
}

function command(os, pid, text) {
  os.input(pid, `${text}\n`);
  drain(os);
}

function exportsFor(os, pid) {
  return os.processes.get(pid).instance.exports;
}

function tarFile(image, name) {
  const result = spawnSync("tar", ["-xOf", "-", name], { input: image });
  assert.equal(result.status, 0, result.stderr.toString());
  return result.stdout;
}

test("shell commands read and write a disk that can be mounted on the next boot", async () => {
  const { os, output } = await boot();
  const pid = await os.spawn(shell);
  drain(os);
  command(os, pid, "hello");
  command(os, pid, "readfile");
  assert.match(output(), /Hello world from shell!/);
  assert.match(output(), /Virtio Newbie!/);
  command(os, pid, "writefile");
  command(os, pid, "readfile");
  assert.match(output(), /Hello from shell!/);
  const image = os.snapshot();
  assert.equal(tarFile(image, "hello.txt").toString(), "Hello from shell!\n");
  assert.equal(tarFile(image, "meow.txt").toString(), "meow!\n");
  const rebooted = await boot(image);
  const nextPid = await rebooted.os.spawn(shell);
  command(rebooted.os, nextPid, "readfile");
  assert.match(rebooted.output(), /Hello from shell!/);
});

test("input blocks the shell without spinning, wakes it, and exit ends scheduling", async () => {
  const { os, output } = await boot();
  const pid = await os.spawn(shell);
  assert.equal(os.run(), 1);
  assert.equal(os.kernel.process_state(pid), State.WAITING);
  assert.equal(os.run(), 0);
  assert.throws(() => os.input(pid, "x".repeat(257)), /queue/);
  assert.equal(os.kernel.input_space(pid), 256);
  os.input(pid, "hello\r\n");
  assert.equal(os.kernel.process_state(pid), State.RUNNABLE);
  drain(os);
  assert.match(output(), /Hello world from shell!/);
  command(os, pid, "exit");
  assert.equal(os.kernel.process_state(pid), State.EXITED);
  assert.equal(os.kernel.process_exit_code(pid), 0);
  assert.equal(os.run(), 0);
  assert.throws(() => os.input(pid, "hello\n"), /exited/);
});

test("round robin runs separate instances fairly and isolates input and globals", async () => {
  const { os } = await boot();
  const first = await os.spawn(guest), second = await os.spawn(guest);
  const a = exportsFor(os, first), b = exportsFor(os, second);
  assert.notEqual(a.memory, b.memory);
  assert.notEqual(a.memory, os.kernel.memory);
  a.configure(2, 0, 0, 0);
  os.input(first, "A");
  os.run(1);
  assert.equal(a.result(), 65);
  os.run(1);
  assert.equal(b.steps(), 1);
  a.configure(0, 0, 0, 0);
  os.run(6);
  assert.equal(a.steps(), 4);
  assert.equal(b.steps(), 4);
  assert.equal(os.kernel.input_space(second), 256);
  new Uint8Array(a.memory.buffer)[a.buffer()] = 99;
  assert.equal(new Uint8Array(b.memory.buffer)[b.buffer()], 0);
});

test("a user trap terminates that process while other processes keep running", async () => {
  const { os } = await boot();
  const bad = await os.spawn(guest), good = await os.spawn(guest);
  exportsFor(os, bad).set_trap(1);
  assert.equal(os.run(4), 4);
  assert.equal(os.kernel.process_state(bad), State.EXITED);
  assert.equal(os.kernel.process_exit_code(bad), -1);
  assert.equal(exportsFor(os, good).steps(), 3);
  assert.equal(os.faults.length, 1);
  assert.equal(os.faults[0].pid, bad);
});

test("syscalls reject invalid user pointers, lengths, filenames and unknown calls", async () => {
  const { os } = await boot();
  const pid = await os.spawn(guest);
  const user = exportsFor(os, pid);
  const pointer = user.buffer();
  let memory = new Uint8Array(user.memory.buffer);
  memory.set(new TextEncoder().encode("hello.txt\0"), pointer);
  const invoke = (number, a0, a1, a2) => {
    user.configure(number, a0, a1, a2);
    os.run(1);
    return user.result();
  };
  assert.equal(invoke(4, pointer, -4, 8), -14);
  assert.equal(invoke(5, -1, pointer + 100, 8), -14);
  assert.equal(invoke(4, pointer, pointer + 100, -1), -1);
  assert.equal(invoke(5, pointer, pointer + 100, 1025), -1);
  assert.equal(invoke(99, 0, 0, 0), -1);
  memory.fill(120, pointer, pointer + 100);
  assert.equal(invoke(4, pointer, pointer + 100, 8), -14);
  memory.set(new TextEncoder().encode("hello.txt\0"), pointer);
  memory.fill(120, memory.length - 5);
  assert.equal(invoke(4, memory.length - 5, pointer + 100, 8), -14);
  assert.equal(invoke(4, pointer, pointer + 100, 8), 8);
  assert.equal(new TextDecoder().decode(memory.subarray(pointer + 100, pointer + 108)), "Can you ");
  // New views must be created after growth; old ArrayBuffers are detached.
  user.memory.grow(1);
  memory = new Uint8Array(user.memory.buffer);
  memory.fill(90, pointer + 100, pointer + 100 + 1024);
  assert.equal(invoke(5, pointer, pointer + 100, 1024), 1024);
  assert.equal(tarFile(os.snapshot(), "hello.txt").length, 1024);
  assert.deepEqual(tarFile(os.snapshot(), "hello.txt"), Buffer.alloc(1024, 90));
  assert.equal(tarFile(os.snapshot(), "meow.txt").toString(), "meow!\n");
});

test("kernel page allocation grows linear memory and preserves files and syscall copies", async () => {
  const { os, output } = await boot();
  const before = os.kernel.memory.buffer.byteLength;
  const address = os.kernel.alloc_pages(4);
  assert.notEqual(address, 0);
  assert.equal(address % 65536, 0);
  assert.ok(os.kernel.memory.buffer.byteLength > before);
  assert.ok(new Uint8Array(os.kernel.memory.buffer, address, 4 * 65536).every((x) => x === 0));
  assert.equal(os.kernel.alloc_pages(0), 0);
  assert.equal(os.kernel.alloc_pages(0xffffffff), 0);
  assert.equal(os.kernel.alloc_pages(257), 0);
  const pid = await os.spawn(shell);
  command(os, pid, "readfile");
  assert.match(output(), /Virtio Newbie!/);
  command(os, pid, "writefile");
  assert.equal(tarFile(os.snapshot(), "hello.txt").toString(), "Hello from shell!\n");
});

test("overlong command lines are discarded and the next command still executes", async () => {
  const { os, output } = await boot();
  const pid = await os.spawn(shell);
  os.input(pid, `${"x".repeat(130)}\nhellp\bo\n`);
  drain(os);
  assert.match(output(), /command line too long/);
  assert.match(output(), /Hello world from shell!/);
  assert.doesNotMatch(output(), /unknown command/);
});

test("invalid and truncated disk images fail to boot", async () => {
  await assert.rejects(boot(disk.subarray(0, 1024)), /Invalid ustar/);
  await assert.rejects(boot(new Uint8Array(32769)), /exceeds/);
  const malformed = Uint8Array.from(disk);
  malformed.fill(55, 124, 135); // An overflowing octal file size.
  await assert.rejects(boot(malformed), /Invalid ustar/);
  const badName = Uint8Array.from(disk);
  badName.fill(120, 0, 100);
  await assert.rejects(boot(badName), /Invalid ustar/);
});

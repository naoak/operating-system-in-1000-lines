import { WasmOS, State } from "./runtime.mjs";

let os, pid, scheduled = false;
let output = [];
let reportedFaults = 0;
const decoder = new TextDecoder();

async function asset(path) {
  const response = await fetch(new URL(path, import.meta.url));
  if (!response.ok) throw new Error(`Cannot load ${path}: HTTP ${response.status}`);
  return response;
}

function flush() {
  if (output.length) {
    postMessage({ type: "output", text: decoder.decode(new Uint8Array(output), { stream: true }) });
    output = [];
  }
}

function schedule() {
  if (scheduled) return;
  scheduled = true;
  setTimeout(() => {
    scheduled = false;
    try {
      const steps = os.run();
      flush();
      for (const fault of os.faults.slice(reportedFaults)) {
        postMessage({ type: "error", message: `Process ${fault.pid} trapped: ${fault.message}` });
      }
      reportedFaults = os.faults.length;
      postMessage({ type: "state", exited: os.kernel.process_state(pid) === State.EXITED });
      if (steps === 32) schedule();
    } catch (error) {
      flush();
      postMessage({ type: "error", message: error.message });
    }
  }, 0);
}

self.onmessage = async ({ data }) => {
  try {
    if (data.type === "boot") {
      const [kernel, shell, disk] = await Promise.all([
        asset("build/kernel.wasm").then(WebAssembly.compileStreaming),
        asset("build/shell.wasm").then(WebAssembly.compileStreaming),
        asset("build/disk.tar").then((response) => response.arrayBuffer()),
      ]);
      os = await WasmOS.create({ kernel, disk, onOutput: (ch) => output.push(ch) });
      pid = await os.spawn(shell);
      flush();
      postMessage({ type: "ready" });
      schedule();
    } else if (data.type === "input") {
      os.input(pid, data.text);
      schedule();
    } else if (data.type === "snapshot") {
      const disk = os.snapshot();
      postMessage({ type: "snapshot", disk }, [disk.buffer]);
    }
  } catch (error) {
    flush();
    postMessage({ type: "error", message: error.message });
  }
};

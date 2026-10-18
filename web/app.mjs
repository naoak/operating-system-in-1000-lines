const terminal = document.querySelector("#terminal");
const command = document.querySelector("#command");
const send = document.querySelector("#send");
const save = document.querySelector("#save");
const status = document.querySelector("#status");
let worker;

function append(text) {
  terminal.textContent = (terminal.textContent + text).slice(-100000);
  terminal.scrollTop = terminal.scrollHeight;
}

function boot() {
  worker?.terminate();
  terminal.textContent = "";
  command.value = "";
  command.disabled = send.disabled = save.disabled = true;
  status.textContent = "Booting…";
  worker = new Worker(new URL("./worker.mjs", import.meta.url), { type: "module" });
  worker.onmessage = ({ data }) => {
    if (data.type === "output") append(data.text);
    else if (data.type === "ready") {
      command.disabled = send.disabled = save.disabled = false;
      status.textContent = "Ready";
      command.focus();
    } else if (data.type === "state" && data.exited) {
      command.disabled = send.disabled = true;
      status.textContent = "Shell exited. Restart to boot again.";
    } else if (data.type === "error") {
      append(`\n${data.message}\n`);
      status.textContent = "Stopped. Restart to try again.";
      command.disabled = send.disabled = true;
    } else if (data.type === "snapshot") {
      const url = URL.createObjectURL(new Blob([data.disk], { type: "application/x-tar" }));
      const link = document.createElement("a");
      link.href = url;
      link.download = "1000os-disk.tar";
      link.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    }
  };
  worker.onerror = (event) => {
    append(`\n${event.message}\n`);
    status.textContent = "Stopped. Restart to try again.";
    command.disabled = send.disabled = true;
  };
  worker.postMessage({ type: "boot" });
}

document.querySelector("#command-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if (command.disabled) return;
  worker.postMessage({ type: "input", text: `${command.value}\n` });
  command.value = "";
  command.focus();
});
document.querySelector("#restart").addEventListener("click", boot);
save.addEventListener("click", () => worker.postMessage({ type: "snapshot" }));
boot();

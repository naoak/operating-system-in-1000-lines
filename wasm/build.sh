#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -n "${WASM_CC:-}" ]]; then
    compiler="$WASM_CC"
elif [[ -n "${WASI_SDK_PATH:-}" ]]; then
    compiler="$WASI_SDK_PATH/bin/clang"
else
    compiler=clang
fi
if ! command -v "$compiler" >/dev/null; then
    echo "Set WASM_CC to a clang with the WebAssembly backend, or set WASI_SDK_PATH." >&2
    exit 1
fi

flags=(--target=wasm32-unknown-unknown -std=c11 -O2 -g -Wall -Wextra
    -Werror -ffreestanding -fno-builtin -nostdlib -Wl,--no-entry
    -Wl,--export-memory -Wl,-z,stack-size=65536
    -Wl,--initial-memory=262144 -Wl,--max-memory=16777216)
mkdir -p wasm/build
"$compiler" "${flags[@]}" wasm/kernel.c common.c -o wasm/build/kernel.wasm
"$compiler" "${flags[@]}" wasm/shell.c wasm/user.c common.c -o wasm/build/shell.wasm
tar -cf wasm/build/disk.tar --format=ustar -C disk hello.txt meow.txt
echo "Built wasm/build/kernel.wasm, shell.wasm and disk.tar"

if [[ "${1:-}" == --test ]]; then
    "$compiler" "${flags[@]}" wasm/tests/guest.c -o wasm/build/guest.wasm
    node --test wasm/tests/runtime.test.mjs
fi

#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

mode="${1:---serve}"
case "$mode" in
    --build|--test|--serve) ;;
    *) echo "Usage: $0 [--build|--test|--serve]" >&2; exit 2 ;;
esac

if [[ -n "${WASM_CC:-}" ]]; then
    CC="$WASM_CC"
elif [[ -n "${WASI_SDK_PATH:-}" ]]; then
    CC="$WASI_SDK_PATH/bin/clang"
else
    CC=clang
fi
if ! command -v "$CC" >/dev/null; then
    echo "Set WASM_CC to a clang with the WebAssembly backend, or set WASI_SDK_PATH." >&2
    exit 1
fi

CFLAGS=(-std=c11 -O2 -g3 -Wall -Wextra -Werror --target=wasm32-unknown-unknown
    -fno-stack-protector -fno-builtin -ffreestanding -nostdlib
    -Wl,--no-entry -Wl,--export-memory -Wl,-z,stack-size=65536
    -Wl,--initial-memory=262144 -Wl,--max-memory=16777216)
mkdir -p web/build

# Build the shell.
"$CC" "${CFLAGS[@]}" -o web/build/shell.wasm shell.c user.c common.c

# Build the kernel.
"$CC" "${CFLAGS[@]}" -o web/build/kernel.wasm kernel.c common.c

(cd disk && tar cf ../web/build/disk.tar --format=ustar *.txt)
echo "Built web/build/kernel.wasm, shell.wasm and disk.tar"

if [[ "$mode" == --test ]]; then
    "$CC" "${CFLAGS[@]}" -o web/build/guest.wasm tests/guest.c
    node --test tests/runtime.test.mjs
elif [[ "$mode" == --serve ]]; then
    exec python3 -m http.server "${WASM_PORT:-8000}" --bind 127.0.0.1 --directory web
fi

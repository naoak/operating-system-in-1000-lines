# Writing an Operating System in 1,000 Lines

**[English](https://1000os.seiya.me/en)** ∙ **[日本語](https://1000os.seiya.me/ja/)** ∙ **[简体中文](https://1000os.seiya.me/zh/)** *(translated by [@YianAndCode](https://github.com/YianAndCode))* ∙ **[한국어](https://1000os.seiya.me/ko/)** *(translated by [@JoHwanhee](https://github.com/JoHwanhee))* ∙ **[繁體中文](https://1000os.seiya.me/tw/)** *(translated by [@alanhc](https://github.com/alanhc))*

![Operating System in 1,000 Lines](./screenshot.png)

This repository contains the source code for the website [Operating System in 1,000 Lines](https://1000os.seiya.me/), and a reference implementation.

## WebAssembly port

The [WebAssembly port](wasm/README.md) compiles the C kernel and shell into separate
Wasm modules with isolated memories, cooperative scheduling, and the original
tar-based filesystem. It includes a browser console and Node.js integration tests.

```sh
WASI_SDK_PATH=/path/to/wasi-sdk ./wasm/build.sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory wasm
```

Open <http://localhost:8000/>. See the port's README for toolchain options and limitations.

## More interesting implementations

The book only covers the basics of an operating system. You can do more with the knowledge you have gained. Here are some ideas:

| Implementation | Author |
| --- | --- |
| [Shutdown command](https://github.com/nuta/operating-system-in-1000-lines/pull/59/files) | [@calvera](https://github.com/calvera) |
| [Creating files, and reading/writing files at arbitrary names](https://github.com/nuta/operating-system-in-1000-lines/pull/110) | [@CagriAtalar](https://github.com/CagriAtalar) |

Let me know if you have implemented something interesting!

# Writing an Operating System in 1,000 Lines

**[English](https://1000os.seiya.me/en)** ∙ **[日本語](https://1000os.seiya.me/ja/)** ∙ **[简体中文](https://1000os.seiya.me/zh/)** *(translated by [@YianAndCode](https://github.com/YianAndCode))* ∙ **[한국어](https://1000os.seiya.me/ko/)** *(translated by [@JoHwanhee](https://github.com/JoHwanhee))* ∙ **[繁體中文](https://1000os.seiya.me/tw/)** *(translated by [@alanhc](https://github.com/alanhc))*

![Operating System in 1,000 Lines](./screenshot.png)

This repository contains the source code for the website [Operating System in 1,000 Lines](https://1000os.seiya.me/), and a reference implementation.

## WebAssembly port

The kernel and shell in the existing root-level C and header files now target
WebAssembly. The port keeps the original filesystem structures and function names
so its changes can be compared with the RISC-V implementation. The browser host is
in `web/`, and integration tests are in `tests/`.

```sh
WASI_SDK_PATH=/path/to/wasi-sdk ./run.sh
```

Open <http://localhost:8000/>. Use `./run.sh --build` to build without starting the
server, or `./run.sh --test` to build and run the Node.js integration tests.
See [the porting notes](PORTING.md) for the code mapping, API changes, and limitations.
The book under `website/` describes the original RISC-V implementation.

## More interesting implementations

The book only covers the basics of an operating system. You can do more with the knowledge you have gained. Here are some ideas:

| Implementation | Author |
| --- | --- |
| [Shutdown command](https://github.com/nuta/operating-system-in-1000-lines/pull/59/files) | [@calvera](https://github.com/calvera) |
| [Creating files, and reading/writing files at arbitrary names](https://github.com/nuta/operating-system-in-1000-lines/pull/110) | [@CagriAtalar](https://github.com/CagriAtalar) |

Let me know if you have implemented something interesting!

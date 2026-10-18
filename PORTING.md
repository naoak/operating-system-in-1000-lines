# WebAssembly への移植

リポジトリ直下の `kernel.c`・`kernel.h`・`user.c`・`user.h`・`shell.c` を
Wasm 向けに変更しています。OS 本体の C・ヘッダを別ディレクトリに複製せず、
元の関数名・構造体・処理順をできるだけ維持しています。
ブラウザのホスト実装は `web/`、統合テストは `tests/` にあります。

## ビルドと起動

Wasm バックエンドと `wasm-ld` を持つ Clang、tar、Python 3 が必要です。
[wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) の Clang も使えます。
WASI のランタイムやライブラリには依存しません。

```sh
WASI_SDK_PATH=/path/to/wasi-sdk ./run.sh
# または WASM_CC=/path/to/clang ./run.sh
```

ブラウザで <http://localhost:8000/> を開きます。
`run.sh` が Wasm とディスクをビルドし、`web/` を配信する HTTP サーバーを起動します。
ポートは `WASM_PORT` で変更できます。HTML を `file://` で直接開くことはできません。

```sh
# ビルドのみ
WASI_SDK_PATH=/path/to/wasi-sdk ./run.sh --build
# 統合テスト。Node.js 18 以降が必要
WASI_SDK_PATH=/path/to/wasi-sdk ./run.sh --test
# ビルド済みファイルを配信する場合
python3 -m http.server 8000 --bind 127.0.0.1 --directory web
```

`hello`、`readfile`、`writefile`、`exit` は元のシェルと同じコマンドです。
`writefile` の後に `readfile` を実行すると変更を確認できます。
ディスクの変更は実行中のメモリに保持します。再起動前に **Download disk** で
変更後の tar を取り出せます。Restart はビルド時のイメージから起動します。

## 元のコードから差分を読む

移植前のコミットは `012efb9` です。例えば次のコマンドで本体の変更を比較できます。

```sh
git diff 012efb9 -- common.h kernel.h kernel.c user.h user.c shell.c run.sh
```

| ファイル・関数 | 維持する処理 | Wasm 向けの変更 |
| --- | --- | --- |
| `common.c` | メモリ・文字列操作、`printf` | 変更なし |
| `common.h` | 型とシステムコール番号 | ページサイズを 64 KiB に変更し、import/export 属性を追加 |
| `kernel.h` | `process` の PID・状態、`file.in_use`、`tar_header` | CPU レジスタ・ページテーブル・VirtIO の定義を削除し、入力待ち状態と受け渡し領域を追加 |
| `alloc_pages()` | 単調増加するページ確保 | カーネルの linear memory を `memory.grow` で拡張し、ディスクバッファの確保で使用 |
| `read_write_disk()` | セクタ単位の読み書き | VirtIO の代わりに、ホストが渡したディスクイメージへコピー |
| `oct2int()`・`fs_init()`・`fs_flush()`・`fs_lookup()` | tar の解析・生成、ファイル検索、`in_use` による管理 | サイズ・名前の検証、バッファ境界と tar フィールドの修正 |
| `create_process()` | 未使用スロットの検索、PID・実行状態の設定 | ページテーブル・カーネルスタック・実行イメージのコピーを削除。インスタンス化はホストが担当 |
| `yield()` | 実行可能なプロセスの選択順 | レジスタ保存の代わりに、選択したプログラムの `step()` を一回呼ぶ |
| `handle_syscall()` | コマンドの分岐、`fs_lookup()` を使うファイル操作 | トラップフレームの代わりに通常の引数と検証済みのコピー領域を使う |
| `kernel_main()` | ディスク・ファイルシステム・idle の初期化 | 起動時の CSR 操作とユーザー実行イメージの埋め込みを削除 |
| `user.c`・`user.h` | `putchar()`・`getchar()`・`readfile()`・`writefile()`・`exit()` | `syscall()` を Wasm のインポート関数に置き換え、RISC-V の `start()` を削除 |
| `shell.c` | 元のコマンド分岐 | 入力状態を保存し、入力待ち・コマンド実行後にホストへ戻る |
| `run.sh` | C のカーネル・シェルと tar ディスクのビルド | Wasm を生成し、HTTP サーバーまたは統合テストを実行 |

`kernel.ld` と `user.ld` は RISC-V の固定アドレス向けのため削除しています。
Wasm のメモリ・スタックのサイズは `run.sh` のリンカオプションで指定します。
教材の `website/` は RISC-V 版の説明です。移植前の実装は Git 履歴、または
[元のリポジトリ](https://github.com/nuta/operating-system-in-1000-lines)を参照してください。

## API と実行モデルで変わること

`getchar()` は入力がなければ `-1` を返し、プロセスを入力待ちにします。
呼び出し元は状態を保存してホストへ戻る必要があります。
`main()` は Wasm の `step` として公開し、一回の呼び出しで最大 64 文字を処理します。
元の同期的な無限ループをそのまま中断・再開する仕組みは実装していません。

`exit()` の「呼び出し元へ戻らない」という契約は維持しています。
`SYS_EXIT` の状態変更後、ホストがそのユーザー呼び出しを例外で巻き戻し、
スケジューラへ戻します。この終了は Wasm trap による異常終了とは区別します。

元のカーネルが実装していた Sv32 による仮想記憶と特権モード切り替えは削除します。
プロセスごとに独立したメモリを持つ Wasm インスタンスをホストが生成し、
Wasm エンジンがメモリ境界を保護します。`alloc_pages()` が確保するのは
カーネル自身のメモリであり、ユーザープロセスのメモリではありません。

## ホストとの境界

`web/runtime.mjs` はモジュールのロード、インスタンス化、メモリ間コピー、
コンソールの接続を担当します。プロセスには `os.syscall` だけを渡し、
カーネルのメモリや管理用エクスポートは渡しません。PID はホストが固定します。

システムコールのポインタはユーザーメモリ内のオフセットです。
ホストが文字列の終端とバッファの範囲を検証し、カーネルの `syscall_message` にコピーします。
カーネルは実行中の PID と状態、受け渡し領域のアドレスと長さを検証して処理します。
読み出し結果もコピーでユーザーメモリへ返します。

C のカーネルがスケジューリング、入力待ち・再開、終了状態を管理します。
ユーザープロセスの Wasm trap は、そのプロセスの異常終了として扱います。
ブラウザでは OS 全体を Web Worker 内で動かし、UI は入出力メッセージを扱います。

## 制約

- wasm32 向けです。各モジュールのメモリは初期 256 KiB、上限 16 MiB です。
- プロセスは起動ごとに最大 8 個です。終了したスロットの再利用は未実装です。
- 協調スケジューリングです。`step()` が戻らない処理の強制中断は未実装です。
  Restart は Worker 全体を終了して復旧します。
- ファイルは既存の 2 個・各 1024 バイトまで、ディスクは 32 KiB までです。
  ファイル作成、ディレクトリ、自動永続化は未実装です。

Wasm のメモリ、インポート、インスタンスのモデルは
[公式仕様](https://webassembly.github.io/spec/core/intro/overview.html)を参照してください。

# Manual Build Make Targets

手動でビルド・確認するときの入口一覧。基本的にはリポジトリ root で実行する。

## よく使う最短コマンド

```sh
make build/ag_c
make build/ag_c_wasm
make test
make test-wasm-obj-linker
make test-wasm-js-api
make test-wasm-linker-selfhost
make test-wasm-js-e2e
```

## ビルド成果物

| コマンド | 出力 | 用途 |
| --- | --- | --- |
| `make build/ag_c` | `build/ag_c` | 通常の arm64/macOS 向けコンパイラ本体。 |
| `make build/ag_c_wasm` | `build/ag_c_wasm` | Wasm backend 付きコンパイラ。`ag_c_wasm input.c` で WAT、`-c -o out.o` で wasm object。 |
| `make build/ag_wasm_link` | `build/ag_wasm_link` | wasm object 用の実験的リンカー。 |
| `make build/libagc_runtime.o` | `build/libagc_runtime.o` | wasm object リンク時の標準 runtime object。`ag_wasm_link` は既定でこれを追加する。 |
| `make wasm-selfhost-api` | `build/wasm_selfhost_api/ag_c_wasm_api.wasm` | wasm 化した C コンパイラ API。JS から `compileWat()` / `compileObject()` で使う。 |
| `make wasm-linker-selfhost` | `build/wasm_linker_selfhost/ag_wasm_link.wasm` | wasm 化した wasm object リンカー。JS から object bytes を渡してリンクできる。 |
| `make release` | `build/ag_c` | `-Oz -DNDEBUG -flto` で release 風にビルドする。 |

## テスト

| コマンド | 内容 | 目安 |
| --- | --- | --- |
| `make test` | tokenizer/parser/preprocess/fuzz quick/IR/Wasm/E2E をまとめて実行。 | 広い回帰確認。 |
| `make build/test_e2e && ./build/test_e2e` | 通常 backend の E2E fixture 実行。 | C 言語機能の回帰確認。 |
| `make build/test_parser && ./build/test_parser` | parser 単体テスト。 | parser 修正時の軽い確認。 |
| `make build/test_preprocess && ./build/test_preprocess` | preprocessor 単体テスト。 | preprocess 修正時。 |
| `make build/test_wasm32_backend && ./build/test_wasm32_backend` | Wasm WAT backend の単体寄り確認。 | WAT 出力修正時。 |
| `make build/test_wasm32_e2e && ./build/test_wasm32_e2e` | Wasm backend の E2E。 | WAT 経路の広い確認。 |
| `make build/test_wasm32_object && ./build/test_wasm32_object` | wasm object 出力の E2E。 | object emitter 修正時。 |
| `make test-wasm-obj-linker` | `ag_wasm_link` の smoke。runtime object あり/なしや relocation を確認。 | リンカー修正時の第一確認。 |
| `make test-wasm-js-api` | wasm 化したコンパイラの JS API smoke。 | browser/JS API 修正時。 |
| `make test-wasm-linker-selfhost` | wasm 化したリンカーの JS API smoke。 | wasm linker API 修正時。 |
| `make test-wasm-js-pipeline` | wasm 化したコンパイラの `compileObject()` と wasm 化したリンカーの `link()` を JS 上で直結する smoke。 | browser 上の compile+link 経路修正時。 |
| `make test-wasm-js-e2e` | `test_e2e.c` 登録 fixture を、wasm 化したコンパイラと wasm 化したリンカーで linked wasm にして実行。 | selfhost compile+link 経路の広い確認。 |
| `make test-asan` | clean 後、ASan 付きで `make test`。 | 重め。メモリ破壊が疑わしい時。 |

## Wasm fixture scans

`make test` には含まれない広めの scan。Wasm backend/object/linker の実装レベルを見るときに使う。

| コマンド | 内容 |
| --- | --- |
| `make wasm32-object-fixture-scan` | fixture を `ag_c_wasm -c` で object 化して確認。 |
| `make wasm32-object-link-fixture-scan` | wasm object を `ag_wasm_link` でリンクして確認。 |
| `make wasm32-object-link-all-fixture-scan` | 対象 fixture を広げてリンク確認。 |
| `make wasm32-wat-fixture-scan` | WAT 経路で fixture を確認。 |
| `make wasm32-object-c-testsuite-scan` | c-testsuite を wasm object 化して確認。 |
| `make wasm32-object-link-c-testsuite-scan` | c-testsuite object をリンクして確認。 |
| `make wasm32-wat-c-testsuite-scan` | c-testsuite を WAT 経路で確認。 |
| `make wasm32-scans` | 上記 scan をまとめて実行。 |

## c-testsuite

```sh
make c-testsuite
make c-testsuite-verbose
bash scripts/run_c_testsuite.sh --list-fail
```

`test/external/c-testsuite` は submodule。初回や未取得の環境では先に
`git submodule update --init` が必要。`make test` には含まれない。

## 言語とビルドオプション

| 指定 | 例 | 用途 |
| --- | --- | --- |
| `DIAG_LANG=ja` | `make build/ag_c` | 既定。日本語診断でビルド。 |
| `DIAG_LANG=en` | `make DIAG_LANG=en build/ag_c` | 英語診断でビルド。object は `build/obj/en` に分かれる。 |
| `DIAG_LANG=all` | `make DIAG_LANG=all build/ag_c` | 複数言語メッセージを含める構成。 |
| `CC=...` | `make CC=clang build/ag_c` | C コンパイラを明示。 |
| `CFLAGS=...` | `make CFLAGS='-std=c11 -O2 -Wall -Wextra' build/ag_c` | CFLAGS を上書き。診断言語用 define も必要なら自分で含める。 |

## 手動作業の例

通常コンパイラを作って fixture を回す:

```sh
make build/ag_c
make build/test_e2e
./build/test_e2e
```

Wasm object を手で作ってリンクする:

```sh
make build/ag_c_wasm build/ag_wasm_link build/libagc_runtime.o
./build/ag_c_wasm -c -o /tmp/main.o /tmp/main.c
./build/ag_wasm_link --no-entry --export=main -o /tmp/main.wasm /tmp/main.o
wasm-validate /tmp/main.wasm
wasm-interp /tmp/main.wasm --run-all-exports
```

runtime object を入れずに未解決 symbol を import として残す:

```sh
./build/ag_wasm_link --nostdlib --no-entry --export=main -o /tmp/main.wasm /tmp/main.o
```

wasm 化したコンパイラ API を作る:

```sh
make wasm-selfhost-api
make test-wasm-js-api
```

JS の統合 wrapper は `tools/wasm_js_api/agc-toolchain.js` です。
標準 runtime 付きで linked wasm を作る場合は `runtimeObject` も渡します。
同じディレクトリに `.d.ts` も置いているため、TypeScript からは `createToolchain` /
`createCompiler` / `createAgcRuntimeImports` / `inlineStandardIncludes` の型を参照できます。

```js
const toolchain = await createToolchain({
  compilerWasm: "build/wasm_selfhost_api/ag_c_wasm_api.wasm",
  linkerWasm: "build/wasm_linker_selfhost/ag_wasm_link.wasm",
  runtimeObject: "build/libagc_runtime.o",
});
```

`createToolchain({ compilerWasm, linkerWasm, runtimeObject })` から
`compileWat(source)` / `compileObject(source)` / `compileLinkedWasm(source)` /
`instantiateLinkedWasm(source)` を使えます。
`instantiateLinkedWasm()` の戻り値には `readStdout()` / `readStderr()` もあり、
runtime object の `printf` / `fprintf` 出力を `main()` 実行後に読めます。
`instantiateLinkedWasm(..., ..., { stdio: { stdin } })` に string / `Uint8Array` /
`ArrayBuffer` を渡すと、runtime object を使う linked wasm では `main()` 実行前に
標準入力バッファへ注入され、`getchar` / `fgets(..., stdin)` / `fread(..., stdin)` から
読めます。runtime object 側の stdin 容量は 64 KiB です。
`useStdlib: false` など JS runtime import 経由で直接動かす場合も、同じ
`stdin` option を `fgetc` / `getchar` / `fgets` / `fread` の入力に使えます。
`feof` / `ferror` / `clearerr` もそれぞれの stdio 状態を見ます。
`onStdout` / `onStderr` callback を渡すと `fwrite` / `fputs` などの出力を逐次受け取れます。
compiler 単体 wrapper は `tools/wasm_js_api/agc-wasm.js` です。
`compileWat(source)` は WAT 文字列を返し、`compileObject(source)` は wasm object bytes
(`Uint8Array`) を返します。
ブラウザで `#include <stdio.h>` などを使う場合は、コンパイル前に
`inlineStandardIncludes(source)` を通すと標準ヘッダを展開できます。デモと
`make test-wasm-js-pipeline` はこの helper を使っています。
browser demo は `tools/wasm_js_api/demo.html` で、WAT / wasm object / linked wasm の
出力を切り替えられます。Linked Wasm では複数 source textarea を別々に object 化してから
runtime object と一緒にリンクし、stdin textarea の内容も `stdio.stdin` として渡します。
`main` export を呼べる場合は戻り値も表示し、
`printf` の結果は `stdout:` として表示します。
compile/link は Web Worker 内で実行し、失敗時は `Compile error` として画面に表示します。
生成した `out.wat` / `out.o` / `out.wasm` は Download から保存できます。

wasm 化したリンカー API を作る:

```sh
make wasm-linker-selfhost
make test-wasm-linker-selfhost
```

wasm 化したコンパイラとリンカーを JS 上で直結する:

```sh
make test-wasm-js-pipeline
make test-wasm-js-e2e
```

## 掃除

```sh
make clean
```

`build/` を削除する。生成物、object、テストバイナリ、scan 出力は消える。

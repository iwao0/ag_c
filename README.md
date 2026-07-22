# ag_c

AntigravityとCodexで作ったCコンパイラ

## ビルド方法

```sh
make
```

リリースビルド（サイズ最適化: `-Oz` + LTO + `strip`）:

```sh
make release
```

| ビルド | サイズ | 説明 |
|--------|--------|------|
| `make` | 562 KB | デバッグ情報あり（`-g -O0`） |
| `make release` | 406 KB | サイズ最適化（`-Oz -flto -DNDEBUG` + `strip`）、28%削減 |

ビルド後の実行ファイル:

- `build/ag_c`

## ag_cコマンドの使い方

Cソースを入力してアセンブリを出力:

```sh
./build/ag_c <input.c> > out.s
```

例:

```sh
./build/ag_c test/fixtures/array/inferred_size_brace.c > basic.s
```

> `inferred_size_brace.c` は `int a[]={10,20,30,40};` の合計を返す最小ケース（exit=100）。

## 出力アセンブリをmacOS実行可能ファイルにする

`clang` でアセンブルとリンクを実行:

```sh
clang -o basic basic.s
```

実行:

```sh
./basic
```

1コマンドで行う場合:

```sh
./build/ag_c test/fixtures/array/inferred_size_brace.c > basic.s && clang -o basic basic.s && ./basic
```

ビルド例（アーキテクチャ指定）:

```sh
# Apple Silicon向け
clang -arch arm64 -o basic_arm64 basic.s
```

## Browser / Node から Wasm コンパイラを使う

Wasm self-host 版の最小 JS API は `tools/wasm_js_api/` にあります。
まず compiler wasm を生成します:

```sh
make wasm-selfhost-api
```

Node smoke:

```sh
make test-wasm-js-api
make test-wasm-js-pipeline
```

Wasm runtime symbolの正本は
`tools/wasm_obj_linker/runtime/symbol-manifest.json`です。C symbol、runtime実装、
import namespace、Wasm signature、memory read/write、target availability、bridge種別を定義し、
C linker table、JS import catalog、型検証用metadata、一覧ドキュメントを生成します。

```sh
make generate-runtime-symbol-manifest
make check-runtime-symbol-manifest
```

生成された一覧は
`tools/wasm_obj_linker/runtime/generated/runtime-symbols.md`で確認できます。

JS からは `createToolchain()` で compiler/linker wasm をまとめて読み込み、
`compileWat(source)`、`compileObject(source)`、`compileLinkedWasm(source)` を使います:

```js
import { readFile } from "node:fs/promises";
import { createToolchain } from "./tools/wasm_js_api/agc-toolchain.js";

const toolchain = await createToolchain({
  compilerWasm: await readFile("build/wasm_selfhost_api/ag_c_wasm_api.wasm"),
  linkerWasm: await readFile("build/wasm_linker_selfhost/ag_wasm_link.wasm"),
});
const source = "int main(){return 42;}\n";
const wat = toolchain.compileWat(source);
const wasm = toolchain.compileLinkedWasm(source, { exports: ["main"] });
const linked = await toolchain.instantiateLinkedWasm(source, { exports: ["main"] });
console.log(wat);
console.log(wasm.byteLength);
console.log(linked.instance.exports.main());
```

名前付きsourceは`{ name, source }`で渡せます。`name`は診断・debug用のopaqueな識別子で、
JS APIがpathやURLとしてopen/fetchすることはありません。1回の`compileLinkedWasm()`に渡す
明示nameはcase-sensitiveで一意である必要があり、重複時はコンパイル前に`TypeError`になります。

```js
const wasm = toolchain.compileLinkedWasm([
  { name: "main.c", source: mainSource },
  { name: "player.c", source: playerSource },
], { exports: ["main"] });
```

成功時のwarningを結果と一緒に保持する場合は`compileObjectWithDiagnostics()`または
`compileLinkedWasmWithDiagnostics()`を使います。返されるdiagnostic配列とその要素は、後続の
compileで変化しないimmutable snapshotです。複数sourceの`diagnostics`はsourceのcompile順、
各source内の診断発生順に平坦化され、`sourceDiagnostics`からsourceごとの配列も取得できます。
compile errorの例外にも同じschemaの`error.diagnostics`が付きます。
`compileLinkedWasmWithDiagnostics()`では、失敗sourceまでのsnapshotが`error.sourceDiagnostics`に残ります。
`compileObjectWithDiagnostics()`の`dependencies`には、そのtranslation unitが実際に開いた
virtual headerのcanonical nameが重複なし・昇順で入ります。条件が偽の`#if`内や未使用のheader、
source自身は含まれません。

```js
const result = toolchain.compileLinkedWasmWithDiagnostics([
  { name: "main.c", source: mainSource },
  { name: "player.c", source: playerSource },
], { exports: ["main"] });

console.log(result.wasm.byteLength);
console.log(result.diagnostics);
console.log(result.sourceDiagnostics[1].diagnostics);
```

`compilerOptions.onStderr`は人向けのtext streamであり、snapshotに含まれるものと同じ診断を
同じ発生順で重複して通知します。複数sourceではcompile順に通知しますが、callbackのchunk境界は
diagnostic境界を表さないため、機械処理にはstructured diagnosticsを使用してください。

共有sourceを扱うhostは、`createToolchain()`の`limits`でcompiler/linker共通のresource policyを
設定できます。同じ`limits`をcompile optionへ渡すと、その呼び出しだけ生成時policyを上書きします。
上限超過は`AgcResourceLimitError`となり、安定した`code`と`limit`、`max`、`actual`を持ちます。

```js
const toolchain = await createToolchain({
  compilerWasm,
  linkerWasm,
  runtimeObject,
  limits: {
    maxSources: 128,
    maxSourceBytes: 512 * 1024,
    maxTotalSourceBytes: 1024 * 1024,
    maxHeaders: 128,
    maxHeaderBytes: 512 * 1024,
    maxTotalHeaderBytes: 1024 * 1024,
    maxObjectBytes: 8 * 1024 * 1024,
    maxLinkedWasmBytes: 8 * 1024 * 1024,
    maxDiagnostics: 1000,
    maxDiagnosticBytes: 1024 * 1024,
  },
});
```

省略時の既定値は次のとおりです。既存のbytes返却APIとerror APIは変更せず、従来のlinker入力上限、
16 MiB object出力上限、128件のstructured diagnostic上限、virtual header既定値を引き継ぎます。

| limit | default |
| --- | ---: |
| `maxSources` | 4096 |
| `maxSourceBytes` | 2147483646 |
| `maxTotalSourceBytes` | 2147483647 |
| `maxHeaders` | 128 |
| `maxHeaderBytes` | 1 MiB |
| `maxTotalHeaderBytes` | 4 MiB |
| `maxIncludeDepth` | 32 |
| `maxObjectBytes` | 16 MiB |
| `maxLinkedWasmBytes` | 2147483647 |
| `maxDiagnostics` | 128 |
| `maxDiagnosticBytes` | 1 MiB |

source/header/diagnosticのbytesはUTF-8 byte数です。`maxDiagnosticBytes`は各recordのcode、source name、
messageの合計を制限します。`maxObjectBytes`は各compiler出力とruntime object、
`maxLinkedWasmBytes`は最終moduleへ適用されます。旧`headerLimits`を明示した項目は互換性のため
統一policyより優先され、従来のE1039〜E1042診断を返します。

これらはmemory/outputの上限であり、CPU時間を強制停止しません。信頼できないsourceを処理するhostは
compileをWeb Worker等で実行し、別途timeout時にWorkerをterminateしてください。

project内headerは`headers`へ明示的に登録します。virtual header modeでは未登録headerを
OS filesystem、current working directory、networkへfallbackしません。pathは`/`区切りの
canonicalな相対pathに限定され、絶対path、URL、空segment、`.`、`..`、backslashを拒否します。
非canonical pathを正規化しないため、aliasによる重複も作られません。

```js
const wasm = toolchain.compileLinkedWasm({
  name: "main.c",
  source: '#include "player.h"\nint main(void) { return PLAYER_VALUE; }\n',
}, {
  headers: { "player.h": "#define PLAYER_VALUE 42\n" },
  headerLimits: {
    maxFiles: 128,
    maxFileBytes: 1024 * 1024,
    maxTotalBytes: 4 * 1024 * 1024,
    maxIncludeDepth: 32,
  },
  exports: ["main"],
});
```

低レベルに compiler だけを使う場合は `tools/wasm_js_api/agc-wasm.js` の
`createCompiler()`、linker だけを使う場合は `tools/wasm_obj_linker/ag-wasm-link.js` の
`createLinker()` も使えます。
TypeScript 用の宣言は `tools/wasm_js_api/agc-toolchain.d.ts` です。
既定では wasm module が export する `malloc/free` で入出力バッファを確保します。
`useHeapBuffers: false` を指定した場合だけ fixed scratch buffer 経路を使います。
browser demo は `tools/wasm_js_api/demo.html` です。repo root を静的 file server で配信して開きます。
WAT、wasm object、linked wasm の 3 出力を切り替えられます。
Linked Wasm では複数の source textarea を別々に object 化してからリンクします。
`main` export を呼べる場合は、ブラウザ上で `main()` の戻り値も表示します。
compile/link は Web Worker 内で実行し、失敗時は画面に `Compile error` として表示します。
生成した `out.wat` / `out.o` / `out.wasm` は画面上の Download から保存できます。
`make test-wasm-js-pipeline` は wasm 化コンパイラの `compileObject()` と wasm 化リンカーの
`link()` を JS 上で直結し、2 つの C source を object 化して 1 つの wasm にリンクします。

## config.toml の設定

`ag_c` はまず入力ソースファイルと同じディレクトリの `config.toml` を読み込み、見つからない場合はカレントディレクトリの `config.toml` を読み込みます。  
設定ファイルがない場合はデフォルト値で動作します。
`config.toml` に文法エラー・未知キー/未知セクション・型不正・重複キーがある場合は、
読み込みを中断してエラーメッセージを標準エラー出力へ表示し、デフォルト値で動作します。

まずはサンプルをコピーして使います:

```sh
cp config.toml.example config.toml
```

設定例:

```toml
[tokenizer]
strict_c11 = false
enable_trigraphs = true
enable_binary_literals = true
enable_c11_audit_extensions = false

[parser]
enable_size_compatible_nonscalar_cast = true
enable_struct_scalar_pointer_cast = true
enable_union_scalar_pointer_cast = true
enable_union_array_member_nonbrace_init = true
```

主な項目:

- `tokenizer.strict_c11`: `true` で非標準の2進数リテラル (`0b...`) を無効化
- `tokenizer.enable_trigraphs`: `??=` などのトライグラフ置換を有効化
- `tokenizer.enable_binary_literals`: GNU拡張の2進数整数リテラルを有効化
- `tokenizer.enable_c11_audit_extensions`: 拡張トークン使用時の監査警告を出力
- `parser.enable_size_compatible_nonscalar_cast`: 同サイズ `struct/union` キャスト拡張を有効化
- `parser.enable_struct_scalar_pointer_cast`: スカラ/ポインタから `struct` 値 cast の段階受理を有効化
- `parser.enable_union_scalar_pointer_cast`: スカラ/ポインタから `union` 値 cast の段階受理を有効化
- `parser.enable_union_array_member_nonbrace_init`: `union` 先頭配列メンバの非波括弧初期化（`u={1,2}`）を有効化

`[parser]` 設定マトリクス（代表ケース）:

| 設定キー | `true` のとき | `false` のとき |
|---|---|---|
| `enable_size_compatible_nonscalar_cast` | `struct A`→`struct B`（同種同サイズ）cast を受理 | 同ケースを診断 |
| `enable_struct_scalar_pointer_cast` | `(struct S)7` / `(struct S)p` を受理 | 同ケースを診断 |
| `enable_union_scalar_pointer_cast` | `(union U)7` / `(union U)p` を受理 | 同ケースを診断 |
| `enable_union_array_member_nonbrace_init` | `union U u={1,2};` を受理 | 同ケースを診断 |

## テスト用 fixture

`test/fixtures/` 配下に、過去にバグや未実装だった機能の「動くことを示す最小ケース」を C ソースとして保存しています。各ファイルは単体で `ag_c → clang → 実行` を通り、期待 exit code をファイル先頭コメントに明記してあります。

ディレクトリはカテゴリごとに分割されています（カテゴリ名は `test/test_e2e.c` のカテゴリと一致）:

```
test/fixtures/
├── array/        配列宣言・初期化に関する fixture
│   └── inferred_size_brace.c        int a[]={...} の要素数推定
│   └── ...
└── type_decl/    型宣言・複合リテラル系
    └── compound_literal_array_inferred_size.c
    └── ...
```

新しい fixture を追加するときは、対応する e2e カテゴリのサブディレクトリを使ってください（無ければ新規作成）。ファイル先頭には「修正前の症状」「期待 exit」をコメントとして残します。

実行例:

```sh
./build/ag_c test/fixtures/array/inferred_size_brace.c > /tmp/out.s \
  && clang -arch arm64 -o /tmp/out /tmp/out.s \
  && /tmp/out; echo "exit=$?"
# => exit=100
```

なお、fixture ファイル自体を `make test` で自動実行する仕組みは現状ありません。等価ケースは `test/test_e2e.c` のインライン文字列として網羅され、`make test` で検証されます。

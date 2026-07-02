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
```

JS からは `createCompiler()` で読み込み、`compileWat(source)` で C ソースを WAT に変換します:

```js
import { readFile } from "node:fs/promises";
import { createCompiler } from "./tools/wasm_js_api/agc-wasm.js";

const wasm = await readFile("build/wasm_selfhost_api/ag_c_wasm_api.wasm");
const compiler = await createCompiler(wasm);
const wat = compiler.compileWat("int main(){return 42;}\n");
console.log(wat);
```

TypeScript 用の宣言は `tools/wasm_js_api/agc-wasm.d.ts` です。
v1 は fixed scratch buffer を使うため、既定では入力 32KB / 出力 96KB までを対象にしています。
browser demo は `tools/wasm_js_api/demo.html` です。repo root を静的 file server で配信して開きます。

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

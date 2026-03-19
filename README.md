# ag_c

AntigravityとCodexで作ったCコンパイラ

## ビルド方法

```sh
make
```

ビルド後の実行ファイル:

- `build/ag_c`

## ag_cコマンドの使い方

Cソースを入力してアセンブリを出力:

```sh
./build/ag_c <input.c> > out.s
```

例:

```sh
./build/ag_c test/fixtures/basic.c > basic.s
```

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
./build/ag_c test/fixtures/basic.c > basic.s && clang -o basic basic.s && ./basic
```

ビルド例（アーキテクチャ指定）:

```sh
# Apple Silicon向け
clang -arch arm64 -o basic_arm64 basic.s
```

## config.toml の設定

`ag_c` はカレントディレクトリの `config.toml` を起動時に読み込みます。  
設定ファイルがない場合はデフォルト値で動作します。

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
enable_union_scalar_pointer_cast = true
enable_union_array_member_nonbrace_init = true
```

主な項目:

- `tokenizer.strict_c11`: `true` で非標準の2進数リテラル (`0b...`) を無効化
- `tokenizer.enable_trigraphs`: `??=` などのトライグラフ置換を有効化
- `tokenizer.enable_binary_literals`: GNU拡張の2進数整数リテラルを有効化
- `tokenizer.enable_c11_audit_extensions`: 拡張トークン使用時の監査警告を出力
- `parser.enable_size_compatible_nonscalar_cast`: 同サイズ `struct/union` キャスト拡張を有効化
- `parser.enable_union_scalar_pointer_cast`: スカラ/ポインタから `union` 値 cast の段階受理を有効化
- `parser.enable_union_array_member_nonbrace_init`: `union` 先頭配列メンバの非波括弧初期化（`u={1,2}`）を有効化

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

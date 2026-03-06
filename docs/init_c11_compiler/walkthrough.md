# C11 Compiler Initial Implementation Walkthrough

本ドキュメントは、MacのApple Silicon上で動作するC11コンパイラの初期実装についての確認と結果をまとめたものです。

## 変更内容 (Changes Made)

1. **プロジェクトの初期設定とテスト基盤の構築**
   - ビルド用の [Makefile](../../Makefile) を作成しました。
   - `$?` (終了ステータス) を利用してコンパイル結果とアセンブル結果を照合する、TDD用テストスクリプト [test.sh](../../test.sh) を整備しました。

2. **アーキテクチャに依存しないパーサーの作成**
   - コマンドライン引数から文字を読み取り、`strtol` とループを用いて構成される簡易的な式評価パーサーを [src/main.c](../../src/main.c) に実装しました。
   - 可読性と移植性を考慮し、この段階でスペース文字 (`isspace`) も自動でスキップするよう対応しています。

3. **機種依存機能（Apple ARM64）の分離とアセンブリ出力**
   - アセンブリ出力に関するすべてのコードを `arch` ディレクトリ以下に分離し、今回はApple Silicon用の [src/arch/arm64_apple.c](../../src/arch/arm64_apple.c) を作成しました。
   - レジスタ `w0` への即値代入(`mov`)、加算(`add`)、減算(`sub`)の関数(`gen_return_int`, `gen_add`, `gen_sub`)などを提供しました。

4. **字句解析器 (Tokenizer) へのリファクタリング**
   - [src/main.c](../../src/main.c) に直書きされていた文字列のパース処理を廃止し、独立した字句解析モジュールとして [src/tokenizer/tokenizer.c](../../src/tokenizer/tokenizer.c) および [src/tokenizer/tokenizer.h](../../src/tokenizer/tokenizer.h) を実装しました。
   - 入力文字列を「意味のある記号や数値の区切り（トークン）」の連結リストへ変換(`tokenize`)してから構文解析する設計に移行しました。
   - 数値の取得や記号の消費を `expect`, `consume`, `expect_number` 等の関数で行うようになり、構文解釈が簡潔で拡張しやすくなりました。
   - 上記を保証するため、[test/test_tokenizer.c](../../test/test_tokenizer.c) にて単体テストを実装しました。

5. **抽象構文木 (AST) と再帰下降構文解析 (Parser) の実装**
   - 役割を分割するため新たに [src/parser/parser.c](../../src/parser/parser.c) および [src/parser/parser.h](../../src/parser/parser.h) を作成し、乗除算が加減算より優先される計算規則と括弧 `()` を解析するための「再帰下降構文解析器」を実装しました。
   - パース結果は抽象構文木（AST: `node_t`）として表現されるようになりました。
   - [src/arch/arm64_apple.c](../../src/arch/arm64_apple.c) のバックエンドコード生成をASTのトラバース（再帰的巡回）とスタックマシンを利用した処理へ書き換え、新たに乗算(`mul`)および除算(`sdiv`)の命令出力を追加しました。

## テストの実施内容 (What was tested)
* **単体テスト (Unit Test)**
   - [test/test_tokenizer.c](../../test/test_tokenizer.c) にて `tokenizer.c` の主要関数（`tokenize`, `consume`, `expect`, `expect_number`, `at_eof`）の挙動を直接検証しました。
   - トークンの種類（`TK_NUM`, `TK_RESERVED`, `TK_EOF`）と期待される値のパースが正しく行われることを確認しました。
* **結合テスト (E2E Test)**
   - 単一の整数 ("0", "42") が、そのままプログラムの終了ステータスとして返却されること。
   - 複数の加算・減算を含む式 ("5+20-4") が、正しく左から計算されること。
   - 優先順位の異なる四則演算 ("5+6*7" => 47) と、括弧による計算順序の変更 ("5*(9-6)" => 15, "(3+5)/2" => 4) が正しく反映されること。
   - 式の中に含まれるスペース文字 (" 12 + 34 - 5 ") が無視され、期待通りの値 (41) になること。

## 検証結果 (Validation Results)
`make test` コマンドにより自動でビルドおよびテストスクリプトが実行され、以下のようにすべて `OK` となることを確認しました（終了コード：`0`）。

```bash
$ make test
cc -std=c11 -g -O0 -Wall -Wextra -o build/test_tokenizer test/test_tokenizer.c build/tokenizer/tokenizer.o
build/test_tokenizer
Running tests for Tokenizer...
test_tokenize...
test_consume...
test_expect...
test_expect_number...
test_at_eof...
OK: All unit tests passed!
./test.sh
0 => 0
42 => 42
5+20-4 => 21
 12 + 34 - 5  => 41
5+6*7 => 47
5*(9-6) => 15
(3+5)/2 => 4
OK
```

初期的な「コンパイラのフロントエンド（文字列のパース）とバックエンド（アセンブリ生成）」の骨組みが、TDDサイクルと共に正常に立ち上がりました。

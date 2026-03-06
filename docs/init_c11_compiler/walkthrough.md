# C11 Compiler Implementation Walkthrough

本ドキュメントは、MacのApple Silicon上で動作するC11コンパイラの実装についての確認と結果をまとめたものです。

## 変更内容 (Changes Made)

1. **プロジェクトの初期設定とテスト基盤の構築**
   - ビルド用の [Makefile](../../Makefile) を作成しました。
   - TDD用テストスクリプト [test.sh](../../test.sh) を整備しました。

2. **字句解析器 (Tokenizer)**
   - [src/tokenizer/tokenizer.c](../../src/tokenizer/tokenizer.c) にて、入力文字列をトークン列（連結リスト）に変換する `tokenize` 関数を実装しました。
   - 対応トークン: 整数リテラル、記号(`+-*/()<>;=`)、2文字演算子(`==`,`!=`,`<=`,`>=`)、識別子(`a`〜`z`)、キーワード(`if`,`else`,`while`,`for`)。
   - 単体テスト: [test/test_tokenizer.c](../../test/test_tokenizer.c)

3. **抽象構文木 (AST) と再帰下降構文解析 (Parser)**
   - [src/parser/parser.c](../../src/parser/parser.c) にて再帰下降パーサーを実装しました。
   - 四則演算の優先順位、括弧、比較演算子、代入式、制御構文(if/else/while/for)に対応。
   - 単体テスト: [test/test_parser.c](../../test/test_parser.c)

4. **ARM64 コード生成 (Code Gen)**
   - [src/arch/arm64_apple.c](../../src/arch/arm64_apple.c) にて AST をトラバースし ARM64 アセンブリを出力します。
   - スタックマシン方式で演算、ローカル変数はフレームポインタ(`x29`) ベースのオフセットで管理。
   - 制御構文はラベルカウンタで一意なラベルを生成し、`cbz`/`b` 命令で分岐・ループを実現。

5. **テストインフラの改善**
   - 共通テストマクロを [test/test_common.h](../../test/test_common.h) に集約。
   - 結合テスト(E2E)をシェルスクリプトから C 言語ベースの [test/test_e2e.c](../../test/test_e2e.c) に移行し、テストケースのカテゴリ別管理・拡張性を改善しました。

## 実装済み機能一覧

| 機能 | 対応する文法規則 |
|---|---|
| 整数リテラル | `num` |
| 四則演算 (`+`,`-`,`*`,`/`) | `add`, `mul` |
| 括弧 `()` | `primary = "(" expr ")"` |
| 比較演算子 (`==`,`!=`,`<`,`<=`,`>`,`>=`) | `equality`, `relational` |
| ローカル変数 (`a`〜`z` の1文字) | `primary = ident`, `assign` |
| 代入式 (`=`) | `assign = equality ("=" assign)?` |
| 複文（セミコロン区切り） | `program = stmt*`, `stmt = expr ";"` |
| if/else 文 | `stmt = "if" "(" expr ")" stmt ("else" stmt)?` |
| while 文 | `stmt = "while" "(" expr ")" stmt` |
| for 文 | `stmt = "for" "(" expr? ";" expr? ";" expr? ")" stmt` |
| return 文 | `stmt = "return" expr ";"` |
| ブロック文 | `stmt = "{" stmt* "}"` |
| 関数定義 | `funcdef = ident "(" params? ")" "{" stmt* "}"` |
| 関数呼び出し | `primary = ident "(" args? ")"` |

> [!NOTE]
> 文法規則の完全な定義は [grammar.md](grammar.md) を参照してください。

## テストの実施内容 (What was tested)

* **単体テスト (Unit Test)**
   - `test_tokenizer.c`: トークナイザの全関数（`tokenize`, `consume`, `consume_str`, `consume_ident`, `expect`, `expect_number`, `at_eof`）。識別子・キーワード・2文字演算子の判定を含む。
   - `test_parser.c`: AST構築（四則演算の優先順位、括弧、比較、代入式、複文パース `program()`）の検証。

* **結合テスト (E2E Test) — `test/test_e2e.c`**
   - 7カテゴリ・39ケースで、コンパイル→アセンブル→実行→終了コード検証を自動化。
   - 整数, 四則演算, 比較演算, ローカル変数, if/else, while, for の全機能をカバー。

## 検証結果 (Validation Results)

```bash
$ make test
build/test_tokenizer
Running tests for Tokenizer...
OK: All unit tests passed!
build/test_parser
Running tests for Parser...
OK: All unit tests passed!
build/test_e2e
Running E2E tests...
test_integer...
test_arithmetic...
test_comparison...
test_local_variables...
test_if_else...
test_while...
test_for...
OK: All 55 E2E tests passed! (55/55)
```

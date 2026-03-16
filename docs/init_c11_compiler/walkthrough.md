# C11 Compiler Implementation Walkthrough

本ドキュメントは、MacのApple Silicon上で動作するC11コンパイラの実装についての確認と結果をまとめたものです。

## 変更内容 (Changes Made)

1. **プロジェクトの初期設定とテスト基盤の構築**
   - ビルド用の [Makefile](../../Makefile) を作成しました。
   - TDD用テストスクリプト [test.sh](../../test.sh) を整備しました。

2. **字句解析器 (Tokenizer)**
   - [src/tokenizer/tokenizer.c](../../src/tokenizer/tokenizer.c) にて、入力文字列をトークン列（連結リスト）に変換する `tokenize` 関数を実装しました。
   - 対応トークン: 数値/文字列リテラル、識別子、キーワード、記号・演算子（`TK_LPAREN`〜`TK_HASHHASH` など個別の種別）。
   - トークン型は用途ごとに分割し、`token_t` を最小共通型として `token_ident_t` / `token_string_t` / `token_num_t` / `token_pp_t` にキャストして扱います。
   - 単体テスト: [test/test_tokenizer.c](../../test/test_tokenizer.c)

3. **抽象構文木 (AST) と再帰下降構文解析 (Parser)**
   - [src/parser/parser.c](../../src/parser/parser.c) にて再帰下降パーサーを実装しました。
   - 四則演算の優先順位、括弧、比較演算子、代入式、制御構文(if/else/while/for)に対応。
   - ASTノード型は用途ごとに分割し、`node_t` を最小共通型として `node_num_t` / `node_lvar_t` / `node_string_t` / `node_mem_t` / `node_block_t` / `node_func_t` / `node_ctrl_t` にキャストして扱います。
   - 単体テスト: [test/test_parser.c](../../test/test_parser.c)

4. **ARM64 コード生成 (Code Gen)**
   - [src/arch/arm64_apple.c](../../src/arch/arm64_apple.c) にて AST をトラバースし ARM64 アセンブリを出力します。
   - スタックマシン方式で演算、ローカル変数はフレームポインタ(`x29`) ベースのオフセットで管理。
   - 制御構文はラベルカウンタで一意なラベルを生成し、`cbz`/`b` 命令で分岐・ループを実現。
   - 複文(`ND_BLOCK`)は値を返さないため、戻り値が必要な場合は `return` を明示します。`main` は `return` 省略時に 0 を返します。

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
| ローカル変数 (複数文字対応) | `primary = ident`, `assign` |
| 代入式 (`=`) | `assign = equality ("=" assign)?` |
| 複文（セミコロン区切り） | `program = stmt*`, `stmt = expr ";"` |
| if/else 文 | `stmt = "if" "(" expr ")" stmt ("else" stmt)?` |
| while 文 | `stmt = "while" "(" expr ")" stmt` |
| for 文 | `stmt = "for" "(" expr? ";" expr? ";" expr? ")" stmt` |
| return 文 | `stmt = "return" expr ";"` |
| ブロック文 | `stmt = "{" stmt* "}"` |
| 関数定義 | `funcdef = ident "(" params? ")" "{" stmt* "}"` |
| 関数呼び出し | `primary = ident "(" args? ")"` |
| 型宣言 | `type = "int" \| "char" \| "void" \| "short" \| "long" \| "float" \| "double"` |
| ポインタ (`*p`, `&x`) | `unary = ("*" \| "&") unary` |
| 配列 (`arr[N]`, `arr[i]`) | `postfix = "[" expr "]"` |
| 文字列リテラル (`"..."`) | `primary = ... \| string` |
| 文字リテラル (`'A'`) | `TK_NUM` としてASCII値を格納 |
| char型 1バイト対応 | `ldrb`/`strb` で char 変数・配列・文字列添字を処理 |
| short型 2バイト対応 | `ldrh`/`strh` で short 変数・配列を処理 |
| int型 4バイト対応 | `ldr w0`/`str w1` で int 変数を処理 |
| float / double対応 | FPU命令 (`fadd`, `fsub`, `fmul`, `fdiv`, `scvtf`, `fcvtzs`, `fcmp`) に対応 |
| 浮動小数点リテラル | `3.14`, `1.5f` 等のパースと `.data` セクションへの定数配置・ロードに対応 |

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
OK: All 96 E2E tests passed! (96/96)
```

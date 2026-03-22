# C11 Compiler Implementation Walkthrough

本ドキュメントは、MacのApple Silicon上で動作するC11コンパイラの実装についての確認と結果をまとめたものです。

## 変更内容 (Changes Made)

1. **プロジェクトの初期設定とテスト基盤の構築**
   - ビルド用の [Makefile](../../Makefile) を作成しました。
   - TDD用テストスクリプト [test.sh](../../test.sh) を整備しました。

2. **字句解析器 (Tokenizer)**
   - [src/tokenizer/tokenizer.c](../../src/tokenizer/tokenizer.c) にて、入力文字列をトークン列（連結リスト）に変換する `tokenize` 関数を実装しました。
   - 対応トークン: 数値/文字列/文字リテラル、識別子、C11キーワード全網羅、記号・演算子（`TK_LPAREN`〜`TK_HASHHASH` など個別の種別）。
   - トークン型は用途ごとに分割し、`token_t` を最小共通型として `token_ident_t` / `token_string_t` / `token_num_t` / `token_pp_t` にキャストして扱います。
   - コメント/空白の厳密処理、行継続 (`\\\n`) の取り扱い、`at_bol`/`has_space` の付与に対応しました。
   - 整数/浮動小数点リテラルの拡張（16進/2進/8進、16進浮動小数点、整数サフィックス `U/L/LL`、浮動小数点サフィックス `f/l`）に対応しました。
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
   - `printf` 呼び出しは Darwin/ARM64 の可変長引数ABIに合わせて特別扱いしています。
     - 原因: `printf` の可変引数を通常関数と同様にレジスタ渡しすると、`%d` などの値が崩れる/異常終了するケースが発生しました。
     - 方針: `ND_FUNCALL` の `printf` 経路で、可変引数（第2引数以降）をスタックへ配置し、call直前のSP 16byteアラインを維持して `_printf` を呼び出します。
     - 補足: 以前の `ag_printf` ブリッジは廃止し、`#include <stdio.h>` の `printf` を直接呼ぶ実装へ統一しました。

5. **テストインフラの改善**
   - 共通テストマクロを [test/test_common.h](../../test/test_common.h) に集約。
   - 結合テスト(E2E)をシェルスクリプトから C 言語ベースの [test/test_e2e.c](../../test/test_e2e.c) に移行し、テストケースのカテゴリ別管理・拡張性を改善しました。

6. **C11準拠強化（Tokenizer/Parser）**
   - `token_num_t.val` / `node_num_t.val` を `long long` に拡張し、大きい整数リテラルの早期切り詰めを回避しました。
   - 文字定数を拡張し、マルチ文字文字定数（`'ab'`）と接頭辞付き文字定数（`L'c'`, `u'c'`, `U'c'`）に対応しました。
   - 接頭辞付き文字列（`L"..."`, `u"..."`, `U"..."`, `u8"..."`）に対応しました。
   - Universal Character Name（`\uXXXX`, `\UXXXXXXXX`）を、識別子と文字列/文字定数のエスケープで扱えるようにしました。
   - トライグラフ置換（`??=` など）を導入しました。
   - `0b...` は拡張として維持しつつ、`strict C11` モードでは拒否する設定を追加しました。
   - 隣接文字列リテラル連結（`"a" "b"`）を Parser 側で実装しました。
   - トライグラフ置換は字句解析の先頭で実行するよう整理し、翻訳フェーズ順序との整合を明文化しました。
   - `config.toml`（`[tokenizer]`）から `strict_c11` / `enable_trigraphs` / `enable_binary_literals` を切り替え可能にしました。
   - `config.toml`（`[parser]`）から `enable_size_compatible_nonscalar_cast` を切り替え可能にしました（`false` で同種同サイズ cast 拡張を無効化）。
   - 浮動小数点サフィックス情報を `token_num_t.float_suffix_kind` として保持し、Parser/AST/Codegenへ伝搬するようにしました。
   - `l/L` サフィックス（long double）は `is_float=3` として分類し、Codegen は現時点で double 経路へ lowering する方針を明文化しました。
   - 接頭辞付きマルチ文字定数（`L/u/U`）を実装定義として受理するように更新しました。

7. **Tokenizer最適化（メンテナンス性重視）**
   - `src/tokenizer/` を `scanner` / `literals` / `keywords` / `punctuator` / `config_runtime` / `allocator` に分離し、責務を明確化しました。
   - 識別子の UCN なし経路をゼロコピー化し、文字列リテラルは escape 値デコード不要時にスキップする遅延処理を導入しました。
   - `match_punctuator()` は 2文字小テーブル + 3/4文字最長一致で分岐を整理しました。
   - `tk_skip_ignored()` は ASCII ホットパスとフォールバック（コメント/行継続）に分離しました。
   - ベンチを mixed / ident-heavy / numeric-heavy / punct-heavy の4系統で継続計測し、CIしきい値をケース別に設定しました。
   - `scripts/bench_tokenizer_opt_levels.sh` を追加し、`-O0`/`-O2` の2軸ベンチを定点実行できるようにしました。

8. **Tokenizer API命名統一**
   - `src/tokenizer/tokenizer.h` の公開関数を `tk_` 接頭辞へ統一しました。
   - `src/` と `test/` の呼び出し側も `tk_` 名に移行し、旧API互換マクロを削除しました。

9. **Parser初期化子/診断ポリシーの固定化**
   - 構造体の単一式初期化は「同型オブジェクトのみ受理」に統一しました（`,` 演算子の最終値が同型 `lvar` のケースを含む）。
   - 共用体初期化子は1要素制約を維持し、2要素目以降は診断固定としました。
   - `union` の先頭配列メンバに対する非波括弧初期化（`union U u={1,2};`）を段階受理しました（brace elision）。
   - `struct/union` 値 cast は「同一タグ型どうしのみ no-op 受理」を維持しつつ、同種同サイズ cast の段階受理を Parser 設定で切り替えられるようにしました。
   - `struct` へのスカラ/ポインタ cast（例: `(struct S)7`, `(struct S)p`）を、先頭メンバ初期化へ lowering する形で段階受理しました。
   - `union` へのスカラ cast（例: `(union U)7`）を、先頭メンバ初期化へ lowering する形で段階受理しました。
   - cast 式の直後に postfix を連鎖できるようにし、`((union U)&x).p` のような式を受理できるようにしました。
   - `struct/union` cast の非受理診断を理由別に分割し、型不整合と設定無効を区別して表示するようにしました。
   - Parser拡張の有効/無効切替は `config.toml` の `[parser]` で制御し、代表挙動は README の設定マトリクスに整理しました。
   - cast 型名で `const/volatile/restrict`、`_Atomic int`、`_Atomic(T)`、入れ子 `_Atomic(_Atomic(T))` を受理するように拡張しました。
   - cast 型名でのストレージ指定子（`_Thread_local` など）は `[cast]` 文脈の専用診断に統一しました。
   - `sizeof(struct S)` / `_Alignof(union U)` のようなタグ型 type-name を受理するように拡張しました。
   - `union` 配列メンバの非波括弧初期化（設定OFF時）にも専用診断を追加し、配列メンバ文脈を明示するようにしました。
   - 構造体/共用体メンバ初期化のサイズ制約を `1/2/4/8 byte` スカラ対応として明文化しました。

## 実装済み機能一覧

| 機能 | 対応する文法規則 |
|---|---|
| 整数リテラル | `num` |
| 四則演算 (`+`,`-`,`*`,`/`) | `add`, `mul` |
| 括弧 `()` | `primary = "(" expr ")"` |
| 比較演算子 (`==`,`!=`,`<`,`<=`,`>`,`>=`) | `equality`, `relational` |
| ローカル変数 (複数文字対応) | `primary = ident`, `assign` |
| 代入式/複合代入 (`=`, `+=`, `-=`, `*=`, `/=`) | `assign = conditional (("=" \| "+=" \| "-=" \| "*=" \| "/=") assign)?` |
| 複文（セミコロン区切り） | `stmt = expr ";"` |
| if/else 文 | `stmt = "if" "(" expr ")" stmt ("else" stmt)?` |
| while 文 | `stmt = "while" "(" expr ")" stmt` |
| do-while 文 | `stmt = "do" stmt "while" "(" expr ")" ";"` |
| for 文 | `stmt = "for" "(" (expr \| type declarator)? ";" expr? ";" expr? ")" stmt`（宣言初期化・スコープ対応） |
| switch/case/default | `stmt = "switch" "(" expr ")" stmt` / `stmt = "case" num ":" stmt` / `stmt = "default" ":" stmt` |
| break/continue | `stmt = "break" ";"` / `stmt = "continue" ";"` |
| 論理演算 (`&&`, `||`) | `logical_or`, `logical_and`（短絡評価） |
| 条件演算子 (`?:`) | `conditional = logical_or ("?" expr ":" conditional)?` |
| 前置/後置インクリメント・デクリメント | `unary/postfix` の `++` / `--` |
| return 文 | `stmt = "return" expr ";"` |
| ブロック文 | `stmt = "{" stmt* "}"` |
| 関数定義 | `funcdef = type? ident "(" params? ")" (";" \| "{" stmt* "}")` |
| 最外部宣言 | `program = external_decl*`（関数/タグ宣言・定義/型付きグローバル宣言） |
| 関数呼び出し | `postfix = "(" args? ")"` |
| 型宣言 | `type = "int" \| "char" \| "void" \| "short" \| "long" \| "float" \| "double" \| "signed" \| "unsigned" \| "_Bool" \| "_Complex" \| "_Atomic"` |
| タグ定義/参照 | `("struct"\|"union"\|"enum") ident?`（定義本体 `{...}` とブロックスコープに対応、匿名タグ・自己参照ポインタメンバ対応） |
| タグ型ポインタcast | `unary = "(" tag_type "*"* ")" unary`（例: `(struct S*)p`） |
| ポインタ (`*p`, `&x`) | `unary = ("*" \| "&") unary` |
| 配列 (`arr[N]`, `arr[i]`) | `postfix = "[" expr "]"` |
| メンバアクセス (`s.m`, `p->m`) | `postfix = "." ident \| "->" ident` |
| typedef | `"typedef" (type \| tag_type) "*"* ident ";"` |
| 文字列リテラル (`"..."`) | `primary = ... \| string` |
| 文字リテラル (`'A'`) | `TK_NUM` としてASCII値を格納 |
| char型 1バイト対応 | `ldrb`/`strb` で char 変数・配列・文字列添字を処理 |
| short型 2バイト対応 | `ldrh`/`strh` で short 変数・配列を処理 |
| int型 4バイト対応 | `ldr w0`/`str w1` で int 変数を処理 |
| float / double対応 | FPU命令 (`fadd`, `fsub`, `fmul`, `fdiv`, `scvtf`, `fcvtzs`, `fcmp`) に対応 |
| 浮動小数点リテラル | `3.14`, `1.5f` 等のパースと `.data` セクションへの定数配置・ロードに対応 |
| 16進/2進/8進整数 | `0x2a`, `0b101`, `077` などのパースに対応 |
| 16進浮動小数点 | `0x1.8p1`, `0x1p2f` などのパースに対応 |
| 整数サフィックス | `U/L/LL` をトークンに保持 |
| キーワード全網羅 | C11の予約語・`_`始まりキーワードをすべてトークン化 |
| コメント/空白 | `//` / `/* */` のスキップと行位置の追跡に対応 |
| マルチ文字文字定数 | `'ab'` などを受理（実装定義値として格納） |
| 接頭辞付き文字/文字列 | `L/u/U/u8` の文字列・`L/u/U` の文字定数に対応 |
| UCN | `\uXXXX`, `\UXXXXXXXX` を識別子とエスケープで扱う |
| トライグラフ | `??=` 等のトライグラフ置換に対応 |
| 隣接文字列連結 | `"a" "b"` を1つの文字列として扱う |
| strict C11モード | `0b...` を禁止（デフォルトは拡張として許可） |
| 空文 (null statement) | `stmt = ";"` |
| `_Static_assert` | `_Static_assert(const_expr, string)`（`sizeof(type)` 対応の定数式評価器を使用） |
| `_Generic` | `_Generic(expr, type: expr, ...)` リテラル式による型選択 |
| `_Complex` 型 | 実部/虚部セマンティクス（`__real__`, `__imag__` アクセス） |
| `_Atomic` 型 | load-acquire/store-release セマンティクス |
| `_Thread_local` | macOS TLV descriptor 経由のスレッドローカル変数 |
| ブロックスコープ変数シャドウイング | 内側ブロックで同名変数を再宣言可能 |
| フレキシブル配列メンバ | `struct S { int n; int data[]; };`（C99/C11 6.7.2.1） |
| unsigned 型演算 | `udiv`/`lsr`/符号なし比較による unsigned セマンティクス |
| 整数昇格 | signed/unsigned のロード区別と型昇格 |
| グローバル変数 | ファイルスコープ変数の宣言と `.data`/`.bss` セクション配置 |
| 末尾呼び出し最適化 | 自己再帰関数の TCO |

> [!NOTE]
> 文法規則の完全な定義は [grammar.md](grammar.md) を参照してください。

## テストの実施内容 (What was tested)

* **単体テスト (Unit Test)**
   - `test_tokenizer.c`: トークナイザの全関数（`tokenize`, `consume`, `consume_str`, `consume_ident`, `expect`, `expect_number`, `at_eof`）。識別子・キーワード・2文字演算子の判定を含む。
   - `test_parser.c`: AST構築（四則演算の優先順位、括弧、比較、代入式、複文パース `program()`）の検証。

* **結合テスト (E2E Test) — `test/test_e2e.c`**
   - 16カテゴリ・255ケースで、コンパイル→アセンブル→実行→終了コード検証を自動化。
   - `switch`, `break/continue`, `++/--`, `+=` 系、`&&/||`, `?:` を含む拡張機能をカバー。

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
OK: All 255 E2E tests passed! (255/255)
```

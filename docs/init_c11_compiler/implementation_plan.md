# C11 Compiler Initial Implementation Plan

C11に準拠したCコンパイラを作成する第一歩として、単純な整数の加算・減算をMac (Apple Silicon) 上のコンパイラ (clang) で実行可能なアセンブリコードへと変換する、最小構成の実装を行います。TDD（テスト駆動開発）のサイクルを確立し、`main` 関数から足し算を実行して結果を確認できるようにします。

## Goal Description
MacのApple Silicon (ARM64アーキテクチャ) 向けに、C言語ソースからアセンブリを出力する初期コンパイラを作成します。
初期段階の目標は「数式をパースし、計算結果をプログラムの終了ステータスとして返す `main` 関数のアセンブリを生成すること」です。テスト用のシェルスクリプトを通じて終了ステータスを標準出力へ表示することで、結果を確認可能とします。

> [!NOTE]
> 対応済みの文法規則（BNF）、トークン定義、ASTノード種別は [grammar.md](grammar.md) に記載しています。未実装箇所の確認にもご利用ください。

## Coding Conventions
本プロジェクトでは可読性と一貫性を保つため、以下の命名規則に従います。
* **関数名**: スネークケース（例: `gen_main_prologue`, `parse_expression` 等）を使用します。
* **型名**: `typedef` で作成する型は末尾に `_t` を付与します（例: `token_t`, `node_t` 等）。

## Development Policy (TDD)
本プロジェクトでは**テスト駆動開発（TDD）**を徹底します。今後の機能追加においては、コンパイラの本体コードを実装する前に、必ず以下のテストケースを「先に」実装・追記することをルールとします：
1. `test.sh` への結合テスト（E2E）ケース追加
2. （必要であれば）`test_*.c` への単体テストケース追加

## Directory Structure
今後の機能拡張に向け、パーサ（構文解析器）やトークナイザ（字句解析器）など、コンパイラ内で役割が明確に異なる機能については `src/` 配下の別ディレクトリ（例: `src/parser/`, `src/tokenizer/`）にコードを集約して管理します。
また、各モジュールの単体テスト（Unit Test）コードは `test/` ディレクトリに集約します。

## Proposed Changes

### [NEW] [Makefile](Makefile)
開發用のビルド・テストランチャーです。`make` コマンドでコンパイラ自身(`ag_c`)のビルド、`make test` で後述のテスト実行スクリプトの起動を行います。将来的にターゲットアーキテクチャを切り替えるための仕組み（例: `make TARGET=x86`）を組み込みやすい構成とします。

### [NEW] [test.sh](test.sh)
TDD用の自動テストスクリプトです。
当コンパイラに「1+2」などの数式を渡し、生成されたアセンブリ(`tmp.s`)をシステムの `clang` で実行可能バイナリ(`tmp`)としてアセンブル・リンクします。その後バイナリを実行し、終了ステータス(`$?`)が想定される計算結果と一致するかをチェックします。また同時にターミナルの標準出力(stdout)へ結果をプリントします。

### [NEW] [src/main.c](src/main.c)
プラットフォーム非依存なコンパイラのエントリポイントとパーサー（初期構文解析）を担う本体コードです。
1. 第一引数として渡された文字列（例: `"1+2"`）を受け取ります。
2. 初期的には文字列内の数字と `+` / `-` 記号を順番に解析（パース）します。
3. 機種依存のコード生成モジュールの関数（例: `gen_main_prologue()`, `gen_add()` など）を呼び出します（ここでアセンブリの直接出力は行いません）。

### [NEW] [src/arch/arm64_apple.c](src/arch/arm64_apple.c)
機種依存コード（今回はApple Silicon向けのARM64アセンブリ）の生成を担うモジュールです。
将来的なWindows(x86/ARM)やマイコン(ARM/RISC-V)への移植を容易にするため、アセンブリ文字列を標準出力へ吐き出す専用処理はこのファイル（および `arch` フォルダ以下）に隔離します。
* ARM64(Apple ABI)用の関数のボイラープレート(`_main:` など)やエピローグの出力
* 加減算命令(`add`, `sub`)やレジスタへの数値セット(`mov`)などのコード出力

### [NEW] [test/test_tokenizer.c](test/test_tokenizer.c)
`src/tokenizer/tokenizer.c` に含まれるすべての関数（`tokenize`, `consume`, `expect`, `expect_number`, `at_eof`）の単体テストを行います。C言語アサートや独自のテスト用マクロを利用して、個別の関数の振る舞いが仕様を満たしているかを検証します。
加えて、以下の拡張事項についてもテストケースを追加しています。
* コメント/空白の厳密処理（`//`, `/* ... */`、行継続）
* 16進/2進/8進整数、16進浮動小数点、整数/浮動小数点サフィックス
* C11キーワード全網羅

### [NEW] [test/test_parser.c](test/test_parser.c)
`src/parser/parser.c` に含まれる構文解析と抽象構文木（AST）構築処理の単体テストを行います。数式文字列をトークナイズし、`expr()` によるパース結果として得られるASTのノード種別（`ND_ADD`, `ND_SUB`, `ND_MUL`, `ND_DIV`, `ND_NUM`）やツリー構造が期待通りであることを検証します。

## Verification Plan

### Automated Tests
* 結合テスト（E2E）: 既存の `test.sh` によるアセンブルと実行確認を行います。
* 単体テスト: `Makefile` を修正して `build/test_tokenizer` および `build/test_parser` などのテスト用バイナリをビルドし、実行結果が正常終了（すべての `assert` が通る）することを確認します。コマンドは `make test` の一環として実行する形を想定します。

### Manual Verification
* `make` で `ag_c` 実行ファイルをビルドした後、`./ag_c "1+2"` をコンソールで実行し、期待されるApple Silicon用のARM64アセンブリ表現（`mov` や `add`, `sub` などの命令セット）が正しく標準出力にプリントされるかを目視で確認します。

## 追補: C11準拠強化（Tokenizer/Parser）

本フェーズでは、Tokenizer/ParserのC11準拠性を高めるため、以下を実装済みです。

- 整数値保持:
  - `token_num_t.val` / `node_num_t.val` を `long long` 化し、`int` への切り詰めを回避
- 文字定数:
  - マルチ文字文字定数（`'ab'`）対応
  - 接頭辞付き文字定数（`L'c'`, `u'c'`, `U'c'`）対応
  - 接頭辞付きマルチ文字定数（`L'AB'`, `u'CD'`, `U'EF'`）を実装定義として受理
- 文字列:
  - 接頭辞付き文字列（`L/u/U/u8`）対応
  - 隣接文字列リテラル連結（`"a" "b"`）を Parser 側で連結
- UCN:
  - `\uXXXX`, `\UXXXXXXXX` を識別子・文字列/文字定数のエスケープで対応
- トライグラフ:
  - `??=` などのトライグラフ置換を導入
- 方針明確化:
  - `0b...` は拡張として維持し、`strict C11` モード時は拒否
  - `config.toml` の `[tokenizer]` で `strict_c11` / `enable_trigraphs` / `enable_binary_literals` を切替可能
  - トライグラフ置換は字句解析の先頭で実行し、翻訳フェーズ順序と整合
  - strict C11 はデフォルトOFFを維持
  - 浮動小数点サフィックス種別を保持（`float_suffix_kind`）し、`l/L` は `is_float=3` として分類（Codegenは現時点で double 経路へ lowering）

### 追加テスト観点

- Tokenizer:
  - マルチ文字文字定数
  - 接頭辞付き文字列/文字定数
  - UCN（識別子・エスケープ）
  - トライグラフ入力
  - strict C11モードでの `0b...` 拒否
- Parser:
  - 隣接文字列連結結果（内容・長さ）

## 追補: Tokenizer最適化運用（2026-03-16）

本フェーズで、Tokenizerの最適化を「速度だけでなく保守性を維持する」方針で実施しました。

- モジュール分離:
  - `src/tokenizer/` を `scanner.c` / `literals.c` / `keywords.c` / `punctuator.c` / `config_adapter.c` / `allocator.c` に整理。
- Hot Path 改善:
  - 識別子（UCNなし）をゼロコピー経路化。
  - 文字列リテラルは escape を必要時のみ解釈する遅延化。
  - 記号判定は 2文字小テーブル + 3/4文字最長一致で分岐整理。
  - `tk_skip_ignored()` を ASCIIホットパス + フォールバックに分離。
- 計測基盤:
  - `test/bench_tokenizer.c` で mixed/ident/numeric/punct を測定。
  - `scripts/check_tokenizer_perf.sh` でケース別しきい値を検査。
  - `scripts/bench_tokenizer_opt_levels.sh` で `-O0/-O2` の2軸比較を実施。

性能結果の詳細と履歴は [tokenizer_perf_report.md](tokenizer_perf_report.md) を参照してください。

## Next Steps: ローカル変数サポート
比較演算子の実装が完了したため、次は **ローカル変数** をサポートします。これにより `a=1; b=2; a+b;` のような複数の文からなるプログラムが実行可能になります。

### 設計方針
* **TDD方針**: テストケース → 実装の順で進めます。
* **Tokenizer**:
  - 新しいトークン種別 `TK_IDENT`（識別子）を追加します。
  - セミコロン `;` と代入演算子 `=`（単体の `=`）を個別の記号トークンとして認識させます。
  - 変数名は1文字の英小文字 `a`〜`z` から開始し、段階的に拡張します。
* **Parser**:
  - 新しいASTノード `ND_ASSIGN`（代入）、`ND_LVAR`（ローカル変数）を追加します。
  - 文法を以下のように拡張します:
    ```
    program = stmt*
    stmt    = expr ";"
    expr    = assign
    assign  = equality ("=" assign)?
    ```
  - `program()` 関数を新設し、複数の文（`node_t*` の配列）を返すようにします。
* **Code Generation (ARM64)**:
  - プロローグにスタックフレームの確保を追加します（`stp x29, x30, [sp, #-N]!` / `mov x29, sp`）。
  - エピローグにフレームの解放を追加します（`ldp x29, x30, [sp], #N` / `ret`）。
  - 変数のアドレスを `x29`（フレームポインタ）からのオフセットとして算出する `gen_lval()` を実装し、変数の読み書きを `ldr`/`str` 命令で行います。
  - 各変数に8バイトのスロットを割り当てます（最大26個: a〜z）。
* **main.c**: `program()` を呼び出し、各文ごとに `gen()` → スタックポップを行い、最後の文の値を終了コードとします。

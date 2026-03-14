# C11 Compiler Initial Setup - Task List

- [x] プロジェクトの初期設定
  - [x] `Makefile`の作成
  - [x] 自動テスト用スクリプト `test.sh` の作成
- [x] コンパイラ本体の初期実装 (TDDフェーズ)
  - [x] 機種依存コード用ディレクトリ (`arch`) の作成
  - [x] 単一の整数を読み込む簡易パーサー (`main.c`) と、数値を返すアセンブリ出力の実装 (`arch/arm64_apple.c`)
  - [x] `test.sh` に単一整数のテストケースを追加し、`make test` で動作確認
  - [x] `+` と `-` を解釈する簡易パーサーの実装 (`main.c`)
  - [x] 加減算用のアセンブリを出力する機能の追加 (`arch/arm64_apple.c`)
  - [x] `test.sh` に加減算のテストケースを追加し、`make test` で動作確認

- [x] 字句解析器（Tokenizer）の実装 (リファクタリング)
  - [x] `src/tokenizer/` ディレクトリの作成とヘッダ定義
  - [x] トークンの定義とトークナイズ処理の実装
  - [x] `main.c` のベタ書きパーサーをトークナイザ経由の処理に置き換え
  - [x] テストが通ることを確認

- [x] 字句解析器（Tokenizer）の単体テスト実装
  - [x] `test/` ディレクトリの作成と `test_tokenizer.c` の実装
  - [x] `tokenizer.c` 内の全関数（`tokenize`, `consume`, `expect`, `expect_number`, `at_eof` 等）の網羅的なテストケース作成
  - [x] `Makefile` を修正して `make test` または `make unittest` で単体テストを実行できるように対応
  - [x] 期待通りにすべてのテストがPassすることを確認

- [x] 四則演算（加減乗除）と括弧の実装 (TDD)
  - [x] `test.sh` に `*`, `/`, `()` のテストケースを先行追加
  - [x] `tokenizer.c` とテストコードの拡張（`*`, `/`, `(`, `)` 対応）
  - [x] トークナイズの単体テストが通ることを確認
  - [x] `src/parser/` を新設し、再帰下降構文解析のパース処理(AST構築)を実装
  - [x] `arch/arm64_apple.c` に乗算(`mul`)・除算(`sdiv`)のアセンブリ生成を追加
  - [x] 結合テストが通ることを確認

- [x] 構文解析器（Parser）の単体テスト実装
  - [x] `test/test_parser.c` の実装およびAST構築(`expr()`)の検証ケース作成
  - [x] `Makefile` を修正して `make test` または `make unittest` でパーサーの単体テストを実行可能にする
  - [x] 期待通りにすべてのテストがPassすることを確認

- [x] テスト環境のリファクタリング
  - [x] `ASSERT_TRUE` などのテストマクロを `test/test_common.h` に分離

- [x] 比較演算子の実装 (`==`, `!=`, `<`, `<=`, `>`, `>=`) (TDD)
  - [x] `test.sh` に比較演算子のテストケースを先行追加
  - [x] `test_tokenizer.c` に比較演算子のトークナイズテストを追加
  - [x] `test_parser.c` に比較演算子のパース(AST)テストを追加
  - [x] `tokenizer.c` および `parser.c` のコードの拡張
  - [x] `arch/arm64_apple.c` に比較演算のアセンブリ生成を追加 (`cmp`, `cset`)
  - [x] 単体テストおよび結合テストが通ることを確認

- [x] ローカル変数サポートの実装 (TDD)
  - [x] `test.sh` に代入式・複文（セミコロン区切り）のテストケースを先行追加
  - [x] `test_tokenizer.c` に識別子(`TK_IDENT`)・セミコロン・`=` のトークナイズテストを追加
  - [x] `test_parser.c` に代入式・変数参照・複文のAST構造テストを追加
  - [x] `tokenizer.c` を拡張（`TK_IDENT` トークン、セミコロン、`=` 対応）
  - [x] `parser.h`/`parser.c` を拡張（`ND_ASSIGN`, `ND_LVAR` ノード、`program()` / `stmt()` / `assign()` 関数）
  - [x] `ag_c.h` / `arm64_apple.c` を拡張（スタックフレーム(`stp`/`ldp`)、変数アドレス計算(`gen_lval`)）
  - [x] `main.c` を拡張（複文対応：`program()` を呼び出し、各文ごとに `gen()` を実行）
  - [x] 単体テストおよび結合テストが通ることを確認

- [x] if/else 制御構文の実装 (TDD)
  - [x] テストケース・実装コードの作成
  - [x] 単体テストおよび結合テストが通ることを確認
  - [x] `grammar.md` の更新

- [x] for/while ループの実装 (TDD)
  - [x] テストケース・実装コードの作成
  - [x] 単体テストおよび結合テストが通ることを確認
  - [x] `grammar.md` の更新

- [x] 結合テストの C 言語化
  - [x] `test/test_e2e.c` を作成（test.sh の全ケースを移植）
  - [x] `Makefile` の `test` ターゲットを更新
  - [x] 全テストが通ることを確認
- [x] `walkthrough.md` への実装完了項目の追記

- [x] return 文の実装 (TDD)

- [x] ブロック文（中括弧）の実装 (TDD)

- [x] 関数定義・呼び出しの実装 (TDD)

- [x] 複数文字変数名の実装 (TDD)

- [x] 型宣言 (int) の実装 (TDD)

- [x] ポインタ・配列の実装 (TDD)

- [x] 文字列リテラルの実装 (TDD)

## 今後の課題・リファクタリング
- [ ] `token_t`、`node_t` を用途ごとに分割/整理する
- [x] `MAX_STMTS` 等による静的確保から、都度の動的メモリ確保（malloc/calloc）へと移行する

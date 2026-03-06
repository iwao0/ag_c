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

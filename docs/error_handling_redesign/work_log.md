# 作業ログ（error_handling_redesign）

## 運用ルール（1タスク1セクション）
- 新しい作業は「Task N」でセクションを追加する。
- 各セクションは以下の項目を持つ。
  - 日付
  - 目的
  - 実施内容
  - 変更ファイル
  - テスト
  - コミット
  - 次アクション

## Task 1: 計画・仕様ドキュメント作成
- 日付: 2026-03-19
- 目的:
  - エラー処理の再設計方針を文書化する。
- 実施内容:
  - 実装計画とエラーコード仕様を新規作成。
  - 採番体系、多言語方針、ビルド時言語選択方針を定義。
- 変更ファイル:
  - `docs/error_handling_redesign/implementation_plan.md`
  - `docs/error_handling_redesign/error_code_spec.md`
- テスト:
  - ドキュメント作成タスクのため該当なし。
- コミット:
  - `61d79d8`
- 次アクション:
  - 診断基盤の実装に着手。

## Task 2: 診断基盤の初期実装
- 日付: 2026-03-19
- 目的:
  - `E####` 出力と言語同梱切替の土台を実装する。
- 実施内容:
  - `src/diag/*` を新設（catalog/message/emit）。
  - `Makefile` に `DIAG_LANG=ja/en/all` を追加。
  - Tokenizer/Parser/Preprocess の一部経路をdiagへ接続。
- 変更ファイル:
  - `src/diag/diag.h`
  - `src/diag/diag.c`
  - `src/diag/error_catalog.h`
  - `src/diag/error_catalog.c`
  - `src/diag/messages.h`
  - `src/diag/messages_ja.c`
  - `src/diag/messages_en.c`
  - `src/diag/messages_all.c`
  - `Makefile`
  - `src/tokenizer/tokenizer.c`
  - `src/parser/diag.c`
  - `src/preprocess/preprocess.c`
- テスト:
  - `make DIAG_LANG=ja build/ag_c build/test_tokenizer build/test_parser`
  - `./build/test_tokenizer && ./build/test_parser`
  - `make clean && make DIAG_LANG=en build/ag_c`
  - `make clean && make DIAG_LANG=all build/ag_c`
- コミット:
  - `1a9b56b`
- 次アクション:
  - preprocess内の未移行エラー経路をdiagへ統合。

## Task 3: preprocess エラー経路の置換
- 日付: 2026-03-19
- 目的:
  - preprocess内の直接 `fprintf/exit` を `E1000` 系経路へ統一する。
- 実施内容:
  - `pp_error` / `diag_emit_*` 経由へ統合。
  - `#error` ディレクティブの出力もdiag経由へ変更。
  - `diag_emit_*` と `pp_error` を `noreturn` 指定して警告解消。
- 変更ファイル:
  - `src/preprocess/preprocess.c`
  - `src/diag/diag.h`
- テスト:
  - `make DIAG_LANG=ja build/ag_c build/test_preprocess`
  - `./build/test_preprocess`
  - `make DIAG_LANG=ja test`（All pass, E2E `255/255`）
- コミット:
  - `19f0a47`
- 次アクション:
  - codegenのエラー出力を `E4000` 系へ移行。

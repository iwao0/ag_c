# 実装計画：エラー処理の共通化・多言語対応・採番管理

## 目的
- エラーメッセージを一元管理し、一覧性と保守性を高める。
- 将来的な多言語切り替え（日本語/英語/他言語）に対応できる基盤を作る。
- すべてのエラーに一意な番号を付与して出力する。
- 実行ファイルサイズ最適化のため、ビルド時に含める言語を選択できるようにする。
- 番号帯を責務ごとに分離する。
  - `0000` 番台: プログラム不具合（内部エラー）
  - `1000` 番台: Preprocess（プリプロセッサ）
  - `2000` 番台: Tokenizer
  - `3000` 番台: Parser
  - `4000` 番台: Codegen（コード生成）
  - `5000` 番台以降: 新規カテゴリ（必要時に追加）

## スコープ
- `src/tokenizer/*` の `tk_error_at` / `tk_error_tok` 系呼び出し
- `src/parser/*` の診断関数（`diag.c` など）
- `src/preprocess/*` の直接エラー出力箇所
- 直接 `fprintf(stderr, ...)` している致命エラー箇所の置換
- テスト更新（期待メッセージが変わる箇所）
- `main` の使い方エラー（引数不足など）は採番対象外とする

## 基本設計

### 1. エラー定義を集中管理する
- 新規ファイル `src/diag/error_catalog.h` を追加し、全エラーIDを `enum` で定義する。
- 新規ファイル `src/diag/error_catalog.c` を追加し、IDごとのメタ情報を保持する。
  - エラーコード文字列（例: `E2001`）
  - メッセージキー（例: `tokenizer.unexpected_char`）
  - 既定言語（初期は日本語）メッセージ
  - 所属カテゴリ（INTERNAL/PREPROCESS/TOKENIZER/PARSER/CODEGEN/...）

### 2. 出力APIを共通化する
- 新規ファイル `src/diag/diag.h`, `src/diag/diag.c` を追加し、以下を提供する。
  - `diag_emit_tok(error_id_t id, token_t *tok, ...)`
  - `diag_emit_at(error_id_t id, char *loc, ...)`
  - `diag_emit_internal(error_id_t id, const char *detail, ...)`
- 出力フォーマットを統一する。
  - 例: `file.c:12: E3003: ';' が必要です (actual: 'if')`
- 既存の `tk_error_*` / `psx_diag_*` は段階的にラッパー化し、最終的に共通APIへ寄せる。

### 3. 多言語化の拡張ポイントを先に埋める
- 新規ファイル `src/diag/i18n.h`, `src/diag/i18n.c` を追加する。
- `message_key + 引数` から文言を生成する仕組みにする。
- 初期実装は `ja` を標準とし、`en` は未実装でもフォールバック（`ja`）で動作させる。
- 言語データはファイル分割し、将来は `messages_ja.c`, `messages_en.c` 追加だけで拡張可能にする。

### 4. ビルド時の言語選択（サイズ最適化）
- `DIAG_LANG` ビルド変数（例: `ja`, `en`, `all`）を導入する。
- `Makefile` で対象メッセージテーブルのみリンクする構成にする。
  - `DIAG_LANG=ja`: `messages_ja.c` のみ含める
  - `DIAG_LANG=en`: `messages_en.c` のみ含める（未定義キーは英語内既定文言または最小フォールバック）
  - `DIAG_LANG=all`: 複数言語同梱（開発用途）
- 単一言語ビルド時は実行時言語切替を無効化または固定値返却にする（APIは保持）。

### 5. 採番ルールをCIで守る
- `scripts/check_error_codes.sh`（新規）で以下を検査する。
  - 重複IDがないこと
  - 番号帯がカテゴリ規約に合っていること
  - カタログに未登録のIDを使っていないこと
  - 新規カテゴリ追加時に `5000` 番台以降を使っていること
- `Makefile` のテストターゲットに組み込む。

## 実装ステップ

### Step 1: カタログと共通APIの土台を作る
- `src/diag/*` を新設し、エラーID定義と出力関数を実装。
- 最小限のエラーID（内部/Preprocess/Tokenizer/Parser 各3〜5件）で動作確認。

### Step 2: メッセージ分割とビルドフラグ導入
- `messages_ja.c` を作成し、`DIAG_LANG` に応じてリンク対象を制御。
- `en` 未実装時は `ja` フォールバックを保証。
- 単一言語ビルドでバイナリサイズが減ることを `size` 等で確認する。

### Step 3: Preprocessを移行する（1000番台）
- `src/preprocess/preprocess.c` の直接 `fprintf(stderr, ...)` + `exit(1)` を共通APIへ置換。
- 呼び出し側の主要エラーをID化（例: 不正ディレクティブ、不正な `#include` 指定、条件式エラー）。

### Step 4: Tokenizerを移行する（2000番台）
- `tk_error_at` / `tk_error_tok` 内部を共通API呼び出しへ置換。
- 呼び出し側の主要エラーをID化（例: 不正文字、長さ超過、メモリ確保失敗）。

### Step 5: Parserを移行する（3000番台）
- `src/parser/diag.c` の文字列組み立てをID + 引数へ置換。
- `missing`, `duplicate`, `undefined`, `only_in_scope` 系を順に移行。

### Step 6: Codegenを移行する（4000番台）
- `src/arch/*` の直接エラー出力を共通APIへ置換。
- 呼び出し側の主要エラーをID化（例: 出力失敗、不正な左辺値、不正な制御フロー）。

### Step 7: 内部エラーを移行する（0000番台）
- `calloc/realloc` 失敗や「到達不能」分岐を `INTERNAL` として採番。
- 直接 `fprintf(stderr, ...)` して `exit(1)` している箇所を順次共通APIへ。
- `main` の使い方エラーは採番対象外として現行挙動を維持。

### Step 8: テストとドキュメント更新
- 既存テストの期待値を `E####` 付きに更新。
- 新規テスト追加:
  - エラーコード表示の有無
  - `DIAG_LANG` ごとのビルド確認（`ja` / `en` / `all`）
  - 単一言語ビルド時のフォールバック挙動
  - コード重複検知スクリプト

## 移行時の互換性方針
- 初期段階では既存エラーメッセージ本文をできるだけ維持し、先にコード付与を完了する。
- 1回のPRで全移行せず、Preprocess → Tokenizer → Parser → Codegen → Internal の順で段階導入する。
- 各段階で「出力が変わる範囲」を明記してレビューしやすくする。

## 受け入れ基準（Done）
- すべての致命エラー出力に `E####` が付与される。
- エラーIDが `error_catalog` で一元管理される。
- 番号帯規約（0000/1000/2000/3000/4000）がCIで検証される。
- `main` の使い方エラーは採番されない。
- `DIAG_LANG` により同梱言語を選択でき、単一言語ビルドが可能。
- 開発者向けドキュメントに採番表と追加手順が明記される。

# メッセージ移行バックログ

本ドキュメントは、`diag_emit_*` 呼び出し時に残っている日本語直書きメッセージを
多言語化しやすい形へ移行するためのバックログです。

## 完了済み
- `src/tokenizer/tokenizer.c`
  - `E2006` expected token の日本語直書き（`"'%c' が必要です"`）を廃止。
  - 現在は `diag_message_for(DIAG_ERR_TOKENIZER_EXPECTED_TOKEN)` と可変部 `'%c'` を組み合わせて出力。
- `src/parser/expr.c` / `src/parser/parser.c`
  - 型指定子組み合わせ不正を `E3006`（`parser.invalid_type_spec`）へ分離。
  - 呼び出し側の固定文言は `diag_message_for(DIAG_ERR_PARSER_INVALID_TYPE_SPEC)` 参照に移行。
- `src/parser/expr.c`
  - 文字列リテラルサイズ関連を専用IDへ分離:
    - `E3007`（`parser.string_literal_too_large`）
    - `E3008`（`parser.string_concat_size_invalid`）
  - 呼び出し側の固定文言は `diag_message_for(...)` 参照に移行。
- `src/parser` 全体（段階的）
  - `E3009-E3012`: `static_assert` 系診断を専用ID化。
  - `E3013-E3016`: 宣言子/typedef/型名/変数名の定型診断を専用ID化。
  - `E3017-E3023`: `_Alignas` / `_Atomic` / 配列サイズ正値 / メンバ型 / `typedef` 必須等を専用ID化。
  - `E3024-E3037`: `decl.c` 初期化子まわりの定型診断を専用ID化。
  - `E3038-E3045`: `expr.c` の cast / generic / primary 系定型診断を専用ID化。
  - `E3046-E3049`: enum未定義 / goto未定義ラベル / 括弧不足 / 関数定義必須を専用ID化。
  - `E3050-E3051`: 非負整数定数式チェック（`%s` 埋め込み）を専用IDテンプレート化。
  - `E3052-E3055`: `_Complex/_Imaginary` と `return` 文脈の定型診断を専用ID化。
  - `E3056-E3059`: `->`/`.` 左辺制約、文字列接頭辞混在、可変長引数 `...` 位置制約を専用ID化。
  - `E3060-E3063`: `switch` 重複診断、lvalue/integer-scalar 要件を専用ID化。
  - `E3064-E3069`: `parser/diag.c` の共通テンプレート（missing/undefined/duplicate/scope等）を専用ID化。
  - `E3070-E3071`: `dynarray` の直接stderr出力を専用ID + `diag_emit_internalf` へ移行。
  - `E1001-E1026`: `preprocess` の `pp_error` 経路を採番化し、ID直指定へ移行。
  - `E2014-E2028`: tokenizer の残存直書き診断を専用ID化。
  - `E4004-E4006`: codegen 制御フロー診断（break/continue/goto）を専用ID化。
- `src/preprocess/preprocess.c` / `src/arch/arm64_apple.c`
  - 主要な日本語直書き診断を `diag_message_for(...)` ベースへ移行。

## 優先度A: 残存する parser 直書き（要ID化）
- （現時点の優先度A残件なし）

## 優先度B: 文脈組み立てAPI側のテンプレート整理
- （現時点の優先度B残件なし）

## 優先度C: parser 以外の残件確認
- `src/main.c`
  - CLI利用メッセージは `diag` 系の対象外とするか、運用方針を明確化する。

## 推奨移行手順
1. `error_catalog.h/.c` に必要なIDを追加（parser は `3000` 番台を継続）。
2. `messages_ja.c` / `messages_en.c` / `messages_all.c` にテンプレート対応キーを追加。
3. 呼び出し側は可能な限り `diag_message_for(id)` ベースへ寄せ、可変部のみ `fmt` 引数で渡す。
4. テスト（`make test`）を都度実施し、回帰を防ぐ。

## メモ
- 既存方針どおり、英語は当面最小実装でよい。
- 実装の区切りごとに、本バックログから次の小タスクを1つ選んで消化する。

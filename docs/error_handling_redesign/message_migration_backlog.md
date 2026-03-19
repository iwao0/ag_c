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

## 優先度B: 追加キー/テンプレート化が必要（文脈付き）
- `src/parser/diag.c`
  - `"[%s] 診断メッセージの生成に失敗しました"`
  - `"[parser] %sが必要です"`
  - `"[parser] 未定義%s '%.*s' です"`
  - `"[parser] %s '%.*s' が重複しています"`
  - `"[parser] %s は %sでのみ使用できます"`
- `src/parser/switch_ctx.c`
  - `"[switch] case %lld が重複しています"`
  - `"[switch] default が重複しています"`
- `src/parser/node_utils.c`
  - `"%s の対象は左辺値である必要があります"`
  - `"%s の対象は整数スカラーである必要があります"`
- `src/parser/expr.c`
  - `"文字列リテラルが大きすぎます"`
  - `"文字列連結中にサイズが不正です"`

## 優先度C: Codegen / Preprocess の文脈特化メッセージ
- `src/arch/arm64_apple.c`
  - `"default が重複しています"`
  - `"代入の左辺値が変数ではありません"`
- `src/preprocess/preprocess.c`
  - `"ファイルが見つかりません: %s"`

## 推奨移行手順
1. `error_catalog.h/.c` に必要なIDを追加（必要なら `5000` 番台を利用）。
2. `messages_ja.c` / `messages_en.c` / `messages_all.c` にテンプレート対応キーを追加。
3. 呼び出し側は可能な限り `diag_message_for(id)` ベースへ寄せ、可変部のみ `fmt` 引数で渡す。
4. テスト（`make test`）を都度実施し、回帰を防ぐ。

## メモ
- 既存方針どおり、英語は当面最小実装でよい。
- 実装の区切りごとに、本バックログから次の小タスクを1つ選んで消化する。

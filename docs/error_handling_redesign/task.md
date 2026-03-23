# Error Handling Follow-up Tasks

- [x] Task 1: トークン名テーブル重複の解消
  - [x] `diag.c` と `tokenizer.c` の token kind 文字列定義を一元化する
  - [x] token kind 表示ロジックが単一実装になっていることを確認する
  - [x] `make test` が通ることを確認する

- [x] Task 2: ロケール切替ロジック重複の解消
  - [x] `diag_message_for` / `diag_warn_message_for` / `diag_text_for` の分岐重複を共通化する

- [x] Task 3: `diag_set_locale` の入力バリデーション強化
  - [x] API直呼び時も `ja` / `en` のみ受け入れる（または等価な安全策）

- [x] Task 4: config 読み込みエラー出力のdiag統一
  - [x] `config.toml` 読み込みエラーを `diag` 側の翻訳方針に寄せる

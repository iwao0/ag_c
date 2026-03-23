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

- [x] Task 5: Parserの直書き文言を `diag_text` 化
  - [x] `psx_diag_*` 呼び出しで使う文言を `diag_text_for(...)` 経由へ置換する
  - [x] `parse_nonneg_const_expr_decl(...)` の文言引数を `diag_text_for(...)` 化する
  - [x] `make test` が通ることを確認する

- [ ] Task 6: `config/toml_reader.c` 詳細エラー文の翻訳戦略整備
  - [ ] line情報つき詳細文を locale 切替可能にする設計を決める
  - [ ] エラーカタログ（または専用テキスト層）へ移す実装方針を文書化する

- [ ] Task 7: warning採番ルールの明文化
  - [ ] warning のカテゴリ帯・採番規則・追加手順を docs に追記する

- [ ] Task 8: 非致命diag APIの共通化検討
  - [ ] `diag_emit_internalf`（終了あり）と非終了通知の責務分離案をまとめる

# Tokenizer Internal Header Policy

`src/tokenizer/internal/` は Tokenizer 実装専用のヘッダ置き場です。

- 原則: `src/tokenizer/*.c` からのみ `internal/*.h` を `#include` する
- 他モジュール（`arch` / `preprocess` / `parser` / `config`）は公開ヘッダを使う
  - 例: `tokenizer.h`, `token.h`, `escape.h`, `allocator.h`

## include規約

- 公開型/公開APIを使う実装:
  - 公開ヘッダを優先（`tokenizer.h`, `token.h`, `escape.h`, `allocator.h`）
- Tokenizer内部ユーティリティを使う実装:
  - `internal/*.h` を利用

## `internal/allocator.h` と `internal/escape.h` の扱い

- 方針: ラッパー維持
  - 実体宣言は公開ヘッダ（`../allocator.h`, `../escape.h`）
  - Tokenizer内部コードは従来どおり `internal/...` を参照できる
- 目的:
  - 既存include行の局所性を維持しつつ、公開境界を明確化する

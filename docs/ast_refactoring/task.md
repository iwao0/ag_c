# ASTおよびTokenのリファクタリングと動的メモリ確保への移行タスク

- [x] トピックの把握と既存コードの解析
  - `parser.h` の `node_t` と `token_t` の構造を解析
  - `MAX_STMTS` と `MAX_ARGS` の使用箇所を特定 (`main.c`, `parser.c`, `arch/arm64_apple.c`, `test_codegen.c` 等)
- [x] ASTノード型の分割・整理
  - `node_t` 構造体のフィールドを用途ごとにグループ分けして可読性を向上
- [x] 静的配列から動的メモリ割り当てへの移行
  - `MAX_STMTS` の削除と `body` 、 `code` 配列の動的割り当て（`malloc`/`realloc`）への変更
  - `MAX_ARGS` の削除と引数リスト `args` の動的割り当てへの変更
- [x] テストコードの修正
  - `test_codegen.c` 等でASTを手動構築している箇所に動的配列割り当てを追加
- [x] 全テストの実行と検証
  - ASan (AddressSanitizer) を有効にして `make test` を実行
  - 全ての単体テスト・E2Eテスト・プリプロセッサテストが正常に通過することを確認
- [x] 元のタスクリスト (`docs/init_c11_compiler/task.md`) の更新

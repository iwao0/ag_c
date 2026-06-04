# `should_reject/` fixtures

「cc は拒否するが ag_c は受け入れてしまう」C ソースを集めた回帰用 fixture。
ag_c が本来検出すべき診断を**現状検出していない**ことを示すドキュメント兼
追跡用です。

## 使い方

```sh
make build/ag_c
./scripts/check_should_reject.sh
```

各 `.c` に対して以下を比較します:

- `cc -fsyntax-only -Werror=implicit-function-declaration` → rc != 0 なら標準 C 違反
- `./build/ag_c <file>` → rc == 0 なら ag_c が見落としている

出力例:

```
MISSED  test/fixtures/should_reject/assign_string_to_int.c
...
should_reject summary: total=18  rejected_by_agc=0  missed=18  spurious=0
```

ag_c が将来診断を追加してエラーにするようになると、その fixture は `missed` から
`rejected_by_agc` へ移ります。`make test` には組み込まれていない (CI を red に
しないため) ので、進捗確認は手動で行ってください。

## カバー範囲

| カテゴリ | fixture |
|---|---|
| 型不整合 (init/assign) | `assign_string_to_int`, `assign_int_to_ptr_implicit`, `assign_struct_to_int`, `assign_void_func_to_int` |
| 戻り値型 | `return_wrong_type_ptr`, `func_redef_different_ret` |
| 関数呼び出し引数数 | `too_many_args`, `too_few_args` |
| 重複定義 | `dup_local_var`, `dup_typedef_conflict`, `dup_enum_name` |
| 制約違反 (constraint) | `deref_int`, `subscript_int`, `bitfield_addr`, `void_ptr_deref`, `void_value_used`, `void_variable` |
| storage class 衝突 | `storage_class_conflict` |

## 追加するときの基準

1. `cc -fsyntax-only` がエラーになる (実際に C 標準違反)。
2. `./build/ag_c` が受け入れて asm を出してしまう。
3. 1 ファイル 1 ケース、ファイル名は問題を表す snake_case。
4. ヘッダコメントで「期待: ag_c は X エラー」と意図を書く。

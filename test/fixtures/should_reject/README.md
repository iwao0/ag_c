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

- `cc -std=c11 -pedantic-errors -fsyntax-only -Werror=implicit-function-declaration` → rc != 0 なら標準 C 違反
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
| block-scope `extern` の名前・scope制約 | `local_extern_after_automatic`, `automatic_after_local_extern`, `block_extern_after_typedef`, `block_extern_after_enum_constant`, `block_function_after_automatic`, `block_function_after_typedef`, `block_function_after_enum_constant`, `block_extern_object_out_of_scope`, `block_extern_object_leaks_to_later_function` |
| storage class / function specifier の適用文脈 | `file_scope_auto_object`, `parameter_extern_storage`, `aggregate_member_static_storage`, `block_thread_local_without_linkage`, `thread_local_function`, `block_static_function`, `repeated_thread_local`, `typedef_thread_local`, `inline_object`, `noreturn_typedef_function`, `inline_parameter` |
| enum の定義・完全型制約 | `empty_enum`, `incomplete_enum_object`, `incomplete_enum_member`, `sizeof_incomplete_enum` |
| array declarator / variably modified type | `array_static_bound_outside_parameter`, `file_scope_vla`, `static_local_vla`, `extern_pointer_to_vla`, `zero_array_bound` |
| flexible array member | `flexible_array_in_union`, `flexible_array_without_prior_member`, `member_after_flexible_array`, `array_of_flexible_record` |
| bit-field の型・幅・operator制約 | `bitfield_floating_type`, `bitfield_nonconstant_width`, `bool_bitfield_too_wide`, `named_zero_width_bitfield`, `sizeof_bitfield`, `incomplete_enum_bitfield` |
| 関数再宣言のcanonical parameter型 | `function_parameter_int_long_mismatch`, `function_parameter_int_unsigned_mismatch`, `function_parameter_char_signed_char_mismatch`, `function_parameter_float_double_mismatch`, `function_parameter_pointer_base_mismatch`, `function_parameter_pointee_qualifier_mismatch`, `function_parameter_nested_qualifier_mismatch` |
| 静的character arrayの文字列長 | `static_character_array_string_too_long`, `static_local_character_array_string_too_long`, `static_character_array_embedded_null_too_long`, `static_character_array_concatenated_string_too_long` |
| aggregateのmodifiable lvalue制約 | `struct_with_const_member_assignment`, `nested_struct_with_const_member_assignment`, `struct_with_const_array_member_assignment`, `union_with_const_member_assignment`, `typedef_const_member_assignment`, `anonymous_union_const_member_assignment`, `const_pointer_member_assignment` |
| address-ofのアドレス可能性制約 | `address_of_rvalue`, `address_of_assignment_result`, `address_of_comma_result`, `address_of_conditional_result`, `bitfield_addr`, `address_of_parenthesized_bitfield`, `address_of_register_parameter`, `address_of_register_array`, `address_of_register_struct_member` |
| `++` / `--` のmodifiable scalar制約 | `increment_array`, `decrement_function`, `increment_const_scalar`, `increment_const_bitfield`, `increment_complex`, `increment_void_pointer`, `increment_incomplete_pointer`, `increment_function_pointer` |
| compound assignmentのtarget・operand制約 | `compound_assign_const_scalar`, `compound_assign_array`, `compound_assign_struct`, `compound_assign_pointer_multiply`, `compound_assign_pointer_bitwise` |
| 間接参照・添字の完全型制約 | `incomplete_lvalue_conversion`, `incomplete_lvalue_conversion_void_cast`, `incomplete_lvalue_conversion_comma_left`, `subscript_void_pointer`, `subscript_incomplete_pointer`, `subscript_function_pointer` |
| 明示castのscalar category制約 | `cast_pointer_to_double`, `cast_double_to_pointer`, `cast_pointer_to_complex`, `cast_complex_to_pointer`, `cast_struct_to_integer`, `cast_void_expression_to_integer` |
| pointer算術・比較のcompatible object制約 | `add_two_pointers`, `void_pointer_arithmetic`, `function_pointer_arithmetic`, `incomplete_pointer_arithmetic`, `subtract_incompatible_pointers`, `subtract_atomic_plain_pointers`, `relational_incompatible_pointers`, `relational_void_object_pointers`, `relational_function_pointers` |

## 追加するときの基準

1. `cc -fsyntax-only` がエラーになる (実際に C 標準違反)。
2. `./build/ag_c` が受け入れて asm を出してしまう。
3. 1 ファイル 1 ケース、ファイル名は問題を表す snake_case。
4. ヘッダコメントで「期待: ag_c は X エラー」と意図を書く。

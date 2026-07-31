# `should_reject/` fixtures

ISO C11では不正であり、ag_cが必ず拒否すべきCソースを集めた回帰用fixtureです。
全fixtureはNativeコンパイラとWasm objectコンパイラの両方でgatingされます。

## 使い方

```sh
make check-should-reject
```

各 `.c` に対して以下を比較します:

- `cc -std=c11 -pedantic-errors -fsyntax-only -Werror=implicit-function-declaration` → rc != 0 なら標準 C 違反
- `./build/ag_c <file>` → rc != 0、かつ`E0006`ではないこと
- `./build/ag_c_wasm -c -o /dev/null <file>` → rc != 0、かつ`E0006`ではないこと

出力例:

```
should_reject summary: total=N host_accepted=0
native: rejected=N missed=0 internal=0
wasm:   rejected=N missed=0 internal=0
```

host compilerが受理するfixture、ag_cのどちらかのmodeが受理するfixture、
または`E0006`内部不変条件違反へ落ちるfixtureが1件でもあれば失敗します。
`make test`からも実行されます。個別の診断コードを固定する重要境界は、
これに加えて`test/test_e2e.c`のcompile-fail registryへ登録します。

## カバー範囲

| カテゴリ | fixture |
|---|---|
| tokenizerの不正token・literal・escape | `tokenizer_hex_missing_digits`, `tokenizer_octal_invalid_digit`, `tokenizer_binary_invalid_digit`, `tokenizer_int_suffix_duplicate`, `tokenizer_int_suffix_excess_long`, `tokenizer_integer_too_large`, `tokenizer_numeric_suffix_concatenated`, `tokenizer_numeric_multiple_decimal_points`, `tokenizer_unterminated_comment`, `tokenizer_unexpected_character`, `tokenizer_unterminated_string`, `tokenizer_unterminated_character`, `tokenizer_unterminated_empty_character`, `tokenizer_empty_character`, `tokenizer_invalid_escape`, `tokenizer_hex_escape_missing_digits_string`, `tokenizer_hex_escape_nonhex_string`, `tokenizer_hex_escape_missing_digits_character`, `tokenizer_ucn_short_string`, `tokenizer_ucn_out_of_range_string`, `tokenizer_ucn_surrogate_string`, `tokenizer_ucn_control_string`, `tokenizer_ucn_control_identifier` |
| parser/semanticの基本制約 | `parser_dot_on_scalar`, `parser_arrow_on_scalar_pointer`, `parser_unknown_member`, `parser_generic_no_matching_association`, `parser_generic_duplicate_default`, `parser_struct_control_condition`, `parser_cast_target_array`, `parser_function_type_member`, `parser_incomplete_type_member`, `parser_variadic_parameter_not_last`, `parser_alignof_expression`, `parser_atomic_missing_type`, `parser_alignas_missing_parenthesis`, `parser_invalid_type_specifier_combination` |
| 型不整合 (init/assign) | `assign_string_to_int`, `assign_int_to_ptr_implicit`, `assign_struct_to_int`, `assign_void_func_to_int`, `function_pointer_from_nonzero_void_pointer`, `function_pointer_from_object_pointer_cast` |
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
| bit-field の型・幅・operator制約 | `bitfield_floating_type`, `bitfield_pointer_type`, `bitfield_array_type`, `bitfield_function_type`, `bitfield_nonconstant_width`, `bool_bitfield_too_wide`, `named_zero_width_bitfield`, `sizeof_bitfield`, `sizeof_generic_selected_bitfield`, `incomplete_enum_bitfield`, `bitfield_derived_switch_duplicate_case` |
| 関数再宣言のcanonical parameter型 | `function_parameter_int_long_mismatch`, `function_parameter_int_unsigned_mismatch`, `function_parameter_char_signed_char_mismatch`, `function_parameter_float_double_mismatch`, `function_parameter_pointer_base_mismatch`, `function_parameter_pointee_qualifier_mismatch`, `function_parameter_nested_qualifier_mismatch` |
| 静的character arrayの文字列長 | `static_character_array_string_too_long`, `static_local_character_array_string_too_long`, `static_character_array_embedded_null_too_long`, `static_character_array_concatenated_string_too_long` |
| aggregateのmodifiable lvalue制約 | `struct_with_const_member_assignment`, `nested_struct_with_const_member_assignment`, `struct_with_const_array_member_assignment`, `union_with_const_member_assignment`, `typedef_const_member_assignment`, `anonymous_union_const_member_assignment`, `const_pointer_member_assignment` |
| address-ofのアドレス可能性制約 | `address_of_rvalue`, `address_of_assignment_result`, `address_of_comma_result`, `address_of_conditional_result`, `bitfield_addr`, `address_of_parenthesized_bitfield`, `address_generic_selected_bitfield`, `address_of_register_parameter`, `address_of_register_array`, `address_of_register_struct_member`, `address_generic_selected_register_object`, `address_generic_selected_register_member`, `address_generic_selected_register_array` |
| generic selectionの選択結果制約 | `address_generic_selected_bitfield`, `sizeof_generic_selected_bitfield`, `sizeof_generic_selected_function`, `assign_generic_selected_const`, `address_generic_selected_register_object`, `address_generic_selected_register_member`, `address_generic_selected_register_array` |
| `++` / `--` のmodifiable scalar制約 | `increment_array`, `decrement_function`, `increment_const_scalar`, `increment_const_bitfield`, `increment_complex`, `increment_void_pointer`, `increment_incomplete_pointer`, `increment_function_pointer` |
| compound assignmentのtarget・operand制約 | `compound_assign_const_scalar`, `compound_assign_array`, `compound_assign_struct`, `compound_assign_pointer_multiply`, `compound_assign_pointer_bitwise` |
| 間接参照・添字の完全型制約 | `incomplete_lvalue_conversion`, `incomplete_lvalue_conversion_void_cast`, `incomplete_lvalue_conversion_comma_left`, `subscript_void_pointer`, `subscript_incomplete_pointer`, `subscript_function_pointer` |
| 明示castのscalar category制約 | `cast_pointer_to_double`, `cast_double_to_pointer`, `cast_pointer_to_complex`, `cast_complex_to_pointer`, `cast_struct_to_integer`, `cast_void_expression_to_integer` |
| pointer算術・比較のcompatible object制約 | `add_two_pointers`, `void_pointer_arithmetic`, `function_pointer_arithmetic`, `incomplete_pointer_arithmetic`, `subtract_incompatible_pointers`, `subtract_atomic_plain_pointers`, `relational_incompatible_pointers`, `relational_void_object_pointers`, `relational_function_pointers` |

## 追加するときの基準

1. `cc -fsyntax-only` がエラーになる (実際に C 標準違反)。
2. Native/Wasm objectの両方が診断付きで拒否する。
3. 1ファイル1ケース、ファイル名は問題を表すsnake_case。
4. ヘッダコメントで期待する制約違反を説明する。

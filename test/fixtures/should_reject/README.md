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
| `_Generic` association型制約 | `generic_function_association_type`, `generic_void_association_type`, `generic_incomplete_association_type`, `generic_incomplete_array_association_type`, `generic_implicit_incomplete_tag_association_type`, `generic_vla_association_type` |
| `_Generic`未選択associationの意味制約 | `generic_unselected_undefined_identifier`, `generic_unselected_incomplete_compound_literal` |
| `_Generic` association互換型重複 | `generic_duplicate_typedef_association`, `generic_duplicate_positive_enum_compatible_type`, `generic_duplicate_negative_enum_compatible_type`, `generic_duplicate_qualified_parameter_function_pointer`, `generic_duplicate_array_adjusted_function_pointer`, `generic_duplicate_function_adjusted_parameter_pointer`, `generic_duplicate_nested_callback_parameter_pointer_qualifier`, `generic_duplicate_nested_callback_array_adjustment`, `generic_duplicate_nested_callback_function_adjustment`, `generic_duplicate_nested_callback_unprototyped_int`, `generic_duplicate_nested_callback_return_function_unprototyped_int`, `generic_duplicate_nested_callback_restrict_array_adjustment`, `generic_duplicate_nested_callback_atomic_array_adjustment`, `generic_duplicate_nested_callback_function_pointer_parameter_qualifier` |
| compound literal型制約 | `compound_literal_void_type`, `compound_literal_function_type`, `compound_literal_incomplete_record`, `compound_literal_vla` |
| `_Alignas`の値・適用対象・再宣言制約 | `alignas_weaker_than_natural`, `alignas_non_power_of_two`, `alignas_signed_overflow`, `alignas_typedef`, `alignas_function_declaration`, `alignas_function_definition`, `alignas_parameter`, `alignas_bitfield`, `alignas_register_object`, `alignas_redeclaration_missing_definition`, `alignas_redeclaration_conflict`, `alignas_zero_missing_definition`, `alignas_zero_conflicts_strict`, `alignas_aligned_then_plain_tentative`, `alignas_definition_then_aligned_extern` |
| `_Static_assert`の構文・message・ICE制約 | `static_assert_missing_message`, `static_assert_identifier_message`, `static_assert_non_string_message`, `static_assert_floating_condition`, `static_assert_comma_condition`, `static_assert_variable_condition`, `static_assert_floating_expression_cast`, `static_assert_signed_overflow`, `static_assert_sizeof_vla`, `logical_unselected_call_static_assert`, `logical_selected_comma_static_assert`, `static_assert_false_condition`, `static_assert_for_initializer_false`, `static_assert_for_initializer_nonconstant` |
| signed整数定数式のoverflow制約 | `signed_constant_add_overflow`, `signed_constant_add_underflow`, `signed_constant_sub_overflow`, `signed_constant_sub_negative_overflow`, `signed_constant_mul_overflow`, `signed_constant_mul_negative_rhs_overflow`, `signed_constant_mul_both_negative_overflow`, `signed_constant_negate_overflow`, `signed_constant_div_overflow`, `signed_constant_mod_overflow`, `signed_constant_shift_overflow`, `signed_constant_negative_shift`, `signed_long_long_add_overflow`, `signed_long_long_div_overflow`, `signed_long_long_shift_overflow` |
| unsigned long long定数式のoperator制約 | `unsigned_long_long_divzero_constant`, `unsigned_long_long_negative_shift_count`, `unsigned_long_long_shift_width` |
| initializer designator制約 | `array_designator_negative`, `array_designator_nonconstant`, `array_designator_out_of_bounds`, `struct_designator_unknown_member`, `scalar_array_designator` |
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
| bit-field の型・幅・operator制約 | `atomic_bitfield_qualifier`, `atomic_bitfield_specifier`, `atomic_bitfield_typedef`, `atomic_bitfield_unnamed`, `bitfield_floating_type`, `bitfield_pointer_type`, `bitfield_array_type`, `bitfield_function_type`, `bitfield_comma_width`, `bitfield_nonconstant_width`, `bitfield_negative_width`, `bitfield_width_signed_overflow`, `bool_bitfield_too_wide`, `named_zero_width_bitfield`, `sizeof_bitfield`, `sizeof_generic_selected_bitfield`, `incomplete_enum_bitfield`, `bitfield_derived_switch_duplicate_case` |
| 関数再宣言のcanonical parameter型 | `function_parameter_int_long_mismatch`, `function_parameter_int_unsigned_mismatch`, `function_parameter_char_signed_char_mismatch`, `function_parameter_float_double_mismatch`, `function_parameter_pointer_base_mismatch`, `function_parameter_pointee_qualifier_mismatch`, `function_parameter_nested_qualifier_mismatch` |
| 静的character arrayの文字列長 | `static_character_array_string_too_long`, `static_local_character_array_string_too_long`, `static_character_array_embedded_null_too_long`, `static_character_array_concatenated_string_too_long` |
| aggregateのmodifiable lvalue制約 | `struct_with_const_member_assignment`, `nested_struct_with_const_member_assignment`, `struct_with_const_array_member_assignment`, `union_with_const_member_assignment`, `typedef_const_member_assignment`, `anonymous_union_const_member_assignment`, `const_pointer_member_assignment` |
| address-ofのアドレス可能性制約 | `address_of_rvalue`, `address_of_assignment_result`, `address_of_comma_result`, `address_of_conditional_result`, `bitfield_addr`, `address_of_parenthesized_bitfield`, `address_generic_selected_bitfield`, `address_of_register_parameter`, `address_of_register_array`, `address_of_register_struct_member`, `address_generic_selected_register_object`, `address_generic_selected_register_member`, `address_generic_selected_register_array` |
| generic selectionの選択結果制約 | `address_generic_selected_bitfield`, `sizeof_generic_selected_bitfield`, `sizeof_generic_selected_function`, `assign_generic_selected_const`, `address_generic_selected_register_object`, `address_generic_selected_register_member`, `address_generic_selected_register_array` |
| `++` / `--` のmodifiable scalar制約 | `increment_array`, `decrement_function`, `increment_const_scalar`, `increment_const_bitfield`, `increment_complex`, `increment_void_pointer`, `increment_incomplete_pointer`, `increment_function_pointer` |
| compound assignmentのtarget・operand制約 | `compound_assign_const_scalar`, `compound_assign_array`, `compound_assign_struct`, `compound_assign_pointer_multiply`, `compound_assign_pointer_bitwise` |
| 間接参照・添字の完全型制約 | `incomplete_lvalue_conversion`, `incomplete_lvalue_conversion_void_cast`, `incomplete_lvalue_conversion_comma_left`, `subscript_void_pointer`, `subscript_incomplete_pointer`, `subscript_function_pointer` |
| 明示castのscalar category制約 | `cast_pointer_to_double`, `cast_double_to_pointer`, `cast_pointer_to_complex`, `cast_complex_to_pointer`, `cast_struct_to_integer`, `cast_void_expression_to_integer` |
| 整数専用・単項演算子のoperand制約 | `modulo_floating_operands`, `shift_floating_operand`, `bitwise_floating_operand`, `unary_plus_pointer`, `logical_not_struct` |
| pointer算術・比較のcompatible object制約 | `add_two_pointers`, `multiply_pointer`, `void_pointer_arithmetic`, `function_pointer_arithmetic`, `incomplete_pointer_arithmetic`, `subtract_incompatible_pointers`, `subtract_atomic_plain_pointers`, `relational_incompatible_pointers`, `relational_void_object_pointers`, `relational_function_pointers`, `equality_incompatible_pointers` |
| pointer暗黙変換の修飾・階層制約 | `add_const_through_double_pointer`, `discard_const_through_double_pointer`, `object_double_pointer_to_void_double_pointer`, `assign_discards_const_pointer`, `argument_discards_const_pointer`, `return_discards_const_pointer`, `assign_discards_volatile_pointer`, `assign_atomic_to_plain_pointer`, `assign_plain_to_atomic_pointer`, `conditional_atomic_plain_pointers` |

## 追加するときの基準

1. `cc -fsyntax-only` がエラーになる (実際に C 標準違反)。
2. Native/Wasm objectの両方が診断付きで拒否する。
3. 1ファイル1ケース、ファイル名は問題を表すsnake_case。
4. ヘッダコメントで期待する制約違反を説明する。

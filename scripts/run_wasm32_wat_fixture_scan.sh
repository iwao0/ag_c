#!/usr/bin/env bash
set -u

agc_wasm=${AG_C_WASM:-./build/ag_c_wasm}
out_dir=${WASM32_WAT_SCAN_DIR:-build/wasm32_wat_scan}
list_fail=0
verbose=0
validate=auto
fixture_source=all

usage() {
  cat <<'EOF'
usage: scripts/run_wasm32_wat_fixture_scan.sh [--list-fail] [--verbose] [--no-validate] [--e2e-fixtures]

Compiles test/fixtures/**/*.c with the Wasm WAT backend, excluding should_reject
and fixtures that require multi-TU linking. With --e2e-fixtures, compiles the
fixture paths registered in test/test_e2e.c. If wat2wasm is available, converts
WAT to a binary wasm module. If wasm-validate is available, validates the module.
Set AG_C_WASM to override the compiler path.
Set WASM32_WAT_SCAN_DIR to override the output directory.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --list-fail)
      list_fail=1
      ;;
    --verbose)
      verbose=1
      ;;
    --no-validate)
      validate=0
      ;;
    --e2e-fixtures)
      fixture_source=e2e
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

skip_reason() {
  case "$1" in
    test/fixtures/probes_found_bugs/static_internal_linkage_xtu_main.c|\
    test/fixtures/probes_found_bugs/static_internal_linkage_xtu_other.c|\
    test/fixtures/probes_found_bugs/inherited_static_linkage_xtu_main.c|\
    test/fixtures/probes_found_bugs/inherited_static_linkage_xtu_other.c|\
    test/fixtures/probes_found_bugs/extern_funcptr_xtu_main.c|\
    test/fixtures/probes_found_bugs/extern_funcptr_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_return_pointee_qualifier_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_return_pointee_qualifier_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_parameter_qualifier_adjustment_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_parameter_qualifier_adjustment_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_parameter_multidimensional_qualifier_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_parameter_multidimensional_qualifier_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_parameter_function_adjustment_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_parameter_function_adjustment_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_parameter_callback_return_qualifier_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_parameter_callback_return_qualifier_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_parameter_callback_return_array_qualifier_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_parameter_callback_return_array_qualifier_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_parameter_callback_return_function_pointer_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_parameter_callback_return_function_pointer_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_parameter_atomic_array_adjustment_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_parameter_atomic_array_adjustment_xtu_other.c|\
    test/fixtures/probes_found_bugs/function_parameter_nested_pointer_qualifier_xtu_main.c|\
    test/fixtures/probes_found_bugs/function_parameter_nested_pointer_qualifier_xtu_other.c|\
    test/fixtures/probes_found_bugs/enum_compatible_function_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/enum_compatible_function_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/incomplete_array_bound_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/incomplete_array_bound_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/incomplete_global_array_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/incomplete_global_array_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/incomplete_global_record_pointer_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/incomplete_global_record_pointer_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/incomplete_global_record_object_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/incomplete_global_record_object_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/named_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/named_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/packed_indirect_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/packed_indirect_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/packed_pointer_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/packed_pointer_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/packed_callback_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/packed_callback_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/packed_global_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/packed_global_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/packed_global_callback_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/packed_global_callback_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/incomplete_callback_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/incomplete_callback_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/record_member_alignment_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/record_member_alignment_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/union_member_order_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/union_member_order_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_funcptr_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_funcptr_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_address_then_direct_call_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_address_then_direct_call_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_direct_call_then_address_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_direct_call_then_address_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_repeated_direct_call_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_repeated_direct_call_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_void_parameter_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_void_parameter_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_void_zero_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_void_zero_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_global_callback_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_global_callback_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_funcptr_return_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_funcptr_return_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_enum_funcptr_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_enum_funcptr_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_atomic_funcptr_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_atomic_funcptr_xtu_other.c|\
    test/fixtures/probes_found_bugs/unprototyped_parameter_categories_xtu_main.c|\
    test/fixtures/probes_found_bugs/unprototyped_parameter_categories_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu_other.c|\
    test/fixtures/probes_found_bugs/nested_atomic_aggregate_callback_parameter_xtu_main.c|\
    test/fixtures/probes_found_bugs/nested_atomic_aggregate_callback_parameter_xtu_other.c|\
    test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_parameter_xtu_main.c|\
    test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_parameter_xtu_other.c|\
    test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_result_xtu_main.c|\
    test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_result_xtu_other.c|\
    test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu_main.c|\
    test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_union_complex_callback_factory_data_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_union_complex_callback_factory_data_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_union_complex_callback_factory_container_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_union_complex_callback_factory_container_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_callback_function_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_callback_function_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_aligned_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_aligned_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_anonymous_union_data_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_anonymous_union_data_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_array_data_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_array_data_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_callback_data_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_callback_data_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_callback_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_callback_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_container_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_container_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_flexible_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_flexible_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_function_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_function_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_incomplete_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_incomplete_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_mutual_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_mutual_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_packed_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_packed_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_pointer_to_array_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_pointer_to_array_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_record_data_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_record_data_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_record_function_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_record_function_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_self_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_self_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_enum_union_function_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_enum_union_function_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_function_pointer_data_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_function_pointer_data_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_function_pointer_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_function_pointer_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_incomplete_record_pointer_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_incomplete_record_pointer_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_incomplete_record_wrapper_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_incomplete_record_wrapper_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_mutual_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_mutual_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_self_referential_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_self_referential_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_function_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_function_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_function_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_function_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_return_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_return_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_to_array_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_to_array_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_to_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_to_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_array_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_array_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_multidimensional_array_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_multidimensional_array_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_global_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_pointer_global_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/atomic_record_member_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/atomic_record_member_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/anonymous_flexible_callback_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/anonymous_flexible_callback_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/anonymous_flexible_function_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/anonymous_flexible_function_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/anonymous_flexible_global_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/anonymous_flexible_global_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/anonymous_global_record_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/anonymous_global_record_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/anonymous_global_union_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/anonymous_global_union_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/nested_anonymous_global_union_signature_xtu_main.c|\
    test/fixtures/probes_found_bugs/nested_anonymous_global_union_signature_xtu_other.c|\
    test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries_main.c|\
    test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries_other.c|\
    test/fixtures/probes_found_bugs/thread_local_xtu_boundaries_main.c|\
    test/fixtures/probes_found_bugs/thread_local_xtu_boundaries_other.c|\
    test/fixtures/probes_found_bugs/aligned_global_definition_xtu_main.c|\
    test/fixtures/probes_found_bugs/aligned_global_definition_xtu_other.c|\
    test/fixtures/probes_found_bugs/aligned_global_data_reloc_xtu_main.c|\
    test/fixtures/probes_found_bugs/aligned_global_data_reloc_xtu_other.c|\
    test/fixtures/probes_found_bugs/global_callback_parameter_qualifier_xtu_main.c|\
    test/fixtures/probes_found_bugs/global_callback_parameter_qualifier_xtu_other.c|\
    test/fixtures/probes_found_bugs/global_enum_integer_compatible_xtu_main.c|\
    test/fixtures/probes_found_bugs/global_enum_integer_compatible_xtu_other.c|\
    test/fixtures/wasm32/atomic_aggregate_signature_mismatch_main.c|\
    test/fixtures/wasm32/atomic_aggregate_signature_mismatch_other.c|\
    test/fixtures/wasm32/anonymous_flexible_callback_member_type_mismatch_main.c|\
    test/fixtures/wasm32/anonymous_flexible_callback_member_type_mismatch_other.c|\
    test/fixtures/wasm32/anonymous_flexible_function_member_type_mismatch_main.c|\
    test/fixtures/wasm32/anonymous_flexible_function_member_type_mismatch_other.c|\
    test/fixtures/wasm32/anonymous_flexible_global_member_type_mismatch_main.c|\
    test/fixtures/wasm32/anonymous_flexible_global_member_type_mismatch_other.c|\
    test/fixtures/wasm32/anonymous_global_union_bitfield_width_mismatch_main.c|\
    test/fixtures/wasm32/anonymous_global_union_bitfield_width_mismatch_other.c|\
    test/fixtures/wasm32/anonymous_global_record_member_name_mismatch_main.c|\
    test/fixtures/wasm32/anonymous_global_record_member_name_mismatch_other.c|\
    test/fixtures/wasm32/anonymous_global_record_member_type_mismatch_main.c|\
    test/fixtures/wasm32/anonymous_global_record_member_type_mismatch_other.c|\
    test/fixtures/wasm32/anonymous_global_union_member_type_mismatch_main.c|\
    test/fixtures/wasm32/anonymous_global_union_member_type_mismatch_other.c|\
    test/fixtures/wasm32/anonymous_global_union_member_name_mismatch_main.c|\
    test/fixtures/wasm32/anonymous_global_union_member_name_mismatch_other.c|\
    test/fixtures/wasm32/global_anonymous_tagged_union_mismatch_main.c|\
    test/fixtures/wasm32/global_anonymous_tagged_union_mismatch_other.c|\
    test/fixtures/wasm32/nested_anonymous_global_union_member_type_mismatch_main.c|\
    test/fixtures/wasm32/nested_anonymous_global_union_member_type_mismatch_other.c|\
    test/fixtures/wasm32/array_bound_signature_mismatch_main.c|\
    test/fixtures/wasm32/array_bound_signature_mismatch_other.c|\
    test/fixtures/wasm32/enum_distinct_return_signature_mismatch_main.c|\
    test/fixtures/wasm32/enum_distinct_return_signature_mismatch_other.c|\
    test/fixtures/wasm32/enum_incompatible_return_signature_mismatch_main.c|\
    test/fixtures/wasm32/enum_incompatible_return_signature_mismatch_other.c|\
    test/fixtures/wasm32/function_return_pointee_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_return_pointee_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_return_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_return_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_return_pointer_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_return_pointer_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_pointee_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_pointee_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_array_element_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_array_element_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_signedness_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_signedness_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_parameter_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_parameter_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_aggregate_parameter_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_aggregate_parameter_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_union_parameter_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_union_parameter_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_small_union_parameter_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_small_union_parameter_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_complex_parameter_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_complex_parameter_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_float_complex_parameter_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_float_complex_parameter_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_union_result_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_union_result_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_small_union_result_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_small_union_result_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_complex_result_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_complex_result_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_float_complex_result_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_float_complex_result_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_union_result_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_union_result_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_complex_result_volatile_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_complex_result_volatile_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_factory_atomic_union_parameter_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_factory_atomic_union_parameter_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_factory_atomic_complex_result_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_factory_atomic_complex_result_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_result_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_atomic_result_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_return_pointee_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_return_pointee_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_return_array_bound_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_return_array_bound_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_return_array_element_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_return_array_element_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_return_function_parameter_signedness_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_return_function_parameter_signedness_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_callback_return_function_result_signedness_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_callback_return_function_result_signedness_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_nested_pointer_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_nested_pointer_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointee_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointee_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_distinct_enum_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_distinct_enum_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_callback_enum_parameter_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_callback_enum_parameter_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointee_distinct_enum_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointee_distinct_enum_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_function_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_function_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_to_array_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_to_array_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_to_enum_array_distinct_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_to_enum_array_distinct_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_pointer_to_atomic_array_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_pointer_to_atomic_array_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_pointer_to_atomic_enum_array_integer_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_pointer_to_atomic_enum_array_integer_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_record_member_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_record_member_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_record_distinct_enum_member_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_record_distinct_enum_member_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_union_distinct_enum_member_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_union_distinct_enum_member_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_flexible_distinct_enum_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_flexible_distinct_enum_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_packed_enum_layout_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_packed_enum_layout_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_aligned_enum_layout_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_aligned_enum_layout_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_record_layout_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_pointer_record_layout_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_self_record_atomic_pointer_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_self_record_atomic_pointer_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_self_record_atomic_member_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_self_record_atomic_member_type_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_self_record_distinct_enum_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_self_record_distinct_enum_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_mutual_record_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_mutual_record_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/function_parameter_atomic_array_element_type_mismatch_main.c|\
    test/fixtures/wasm32/function_parameter_atomic_array_element_type_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_type_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_type_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_callback_enum_result_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_callback_enum_result_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_pointer_record_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_pointer_record_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_union_pointee_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_union_pointee_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_pointer_flexible_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_pointer_flexible_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_pointee_type_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_pointee_type_mismatch_other.c|\
    test/fixtures/wasm32/function_return_atomic_function_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/function_return_atomic_function_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/record_member_signature_mismatch_main.c|\
    test/fixtures/wasm32/record_member_signature_mismatch_other.c|\
    test/fixtures/wasm32/record_member_alignment_presence_mismatch_main.c|\
    test/fixtures/wasm32/record_member_alignment_presence_mismatch_other.c|\
    test/fixtures/wasm32/record_member_alignment_value_mismatch_main.c|\
    test/fixtures/wasm32/record_member_alignment_value_mismatch_other.c|\
    test/fixtures/wasm32/indirect_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/indirect_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/nested_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/nested_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/pointer_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/pointer_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/nested_pointer_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/nested_pointer_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/incomplete_pointer_sibling_layout_mismatch_main.c|\
    test/fixtures/wasm32/incomplete_pointer_sibling_layout_mismatch_other.c|\
    test/fixtures/wasm32/callback_pointer_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/callback_pointer_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/callback_return_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/callback_return_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/incomplete_callback_sibling_layout_mismatch_main.c|\
    test/fixtures/wasm32/incomplete_callback_sibling_layout_mismatch_other.c|\
    test/fixtures/wasm32/global_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/global_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/global_pointer_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/global_pointer_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/global_callback_record_layout_signature_mismatch_main.c|\
    test/fixtures/wasm32/global_callback_record_layout_signature_mismatch_other.c|\
    test/fixtures/wasm32/global_scalar_type_signature_mismatch_main.c|\
    test/fixtures/wasm32/global_scalar_type_signature_mismatch_other.c|\
    test/fixtures/wasm32/global_array_bound_signature_mismatch_main.c|\
    test/fixtures/wasm32/global_array_bound_signature_mismatch_other.c|\
    test/fixtures/wasm32/global_unprototyped_callback_promotion_mismatch_main.c|\
    test/fixtures/wasm32/global_unprototyped_callback_promotion_mismatch_other.c|\
    test/fixtures/wasm32/global_thread_local_storage_mismatch_main.c|\
    test/fixtures/wasm32/global_thread_local_storage_mismatch_other.c|\
    test/fixtures/wasm32/global_alignment_requirement_mismatch_main.c|\
    test/fixtures/wasm32/global_alignment_requirement_mismatch_other.c|\
    test/fixtures/wasm32/global_alignment_value_mismatch_main.c|\
    test/fixtures/wasm32/global_alignment_value_mismatch_other.c|\
    test/fixtures/wasm32/global_alignment_data_reloc_mismatch_main.c|\
    test/fixtures/wasm32/global_alignment_data_reloc_mismatch_other.c|\
    test/fixtures/wasm32/global_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/global_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/global_volatile_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/global_volatile_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_anonymous_union_distinct_enum_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_anonymous_union_distinct_enum_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_flexible_distinct_enum_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_flexible_distinct_enum_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_packed_record_distinct_enum_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_packed_record_distinct_enum_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_aligned_record_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_aligned_record_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_anonymous_flexible_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_anonymous_flexible_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_anonymous_union_pointee_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_anonymous_union_pointee_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointee_type_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointee_type_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_function_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_function_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_function_pointer_callback_signature_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_function_pointer_callback_signature_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_to_record_type_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_to_record_type_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_record_pointee_type_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_record_pointee_type_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_record_pointee_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_record_pointee_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_record_member_type_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_record_member_type_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_record_distinct_enum_member_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_record_distinct_enum_member_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_pointer_record_layout_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_pointer_record_layout_mismatch_other.c|\
    test/fixtures/wasm32/global_mutual_atomic_record_member_type_mismatch_main.c|\
    test/fixtures/wasm32/global_mutual_atomic_record_member_type_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_self_record_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_self_record_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_mutual_record_distinct_enum_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_mutual_record_distinct_enum_mismatch_other.c|\
    test/fixtures/wasm32/global_mutual_atomic_record_layout_mismatch_main.c|\
    test/fixtures/wasm32/global_mutual_atomic_record_layout_mismatch_other.c|\
    test/fixtures/wasm32/global_array_atomic_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/global_array_atomic_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/global_array_atomic_pointee_type_mismatch_main.c|\
    test/fixtures/wasm32/global_array_atomic_pointee_type_mismatch_other.c|\
    test/fixtures/wasm32/global_multidimensional_array_atomic_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/global_multidimensional_array_atomic_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/global_multidimensional_array_atomic_pointee_type_mismatch_main.c|\
    test/fixtures/wasm32/global_multidimensional_array_atomic_pointee_type_mismatch_other.c|\
    test/fixtures/wasm32/global_multidimensional_atomic_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/global_multidimensional_atomic_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/record_member_atomic_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/record_member_atomic_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/record_member_atomic_pointee_type_mismatch_main.c|\
    test/fixtures/wasm32/record_member_atomic_pointee_type_mismatch_other.c|\
    test/fixtures/wasm32/record_member_atomic_pointer_to_enum_array_distinct_mismatch_main.c|\
    test/fixtures/wasm32/record_member_atomic_pointer_to_enum_array_distinct_mismatch_other.c|\
    test/fixtures/wasm32/global_pointee_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/global_pointee_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/global_restrict_pointer_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/global_restrict_pointer_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/global_callback_pointee_const_qualifier_mismatch_main.c|\
    test/fixtures/wasm32/global_callback_pointee_const_qualifier_mismatch_other.c|\
    test/fixtures/wasm32/global_void_pointer_type_mismatch_main.c|\
    test/fixtures/wasm32/global_void_pointer_type_mismatch_other.c|\
    test/fixtures/wasm32/global_pointer_array_shape_mismatch_main.c|\
    test/fixtures/wasm32/global_pointer_array_shape_mismatch_other.c|\
    test/fixtures/wasm32/global_function_object_pointer_kind_mismatch_main.c|\
    test/fixtures/wasm32/global_function_object_pointer_kind_mismatch_other.c|\
    test/fixtures/wasm32/global_distinct_enum_type_mismatch_main.c|\
    test/fixtures/wasm32/global_distinct_enum_type_mismatch_other.c|\
    test/fixtures/wasm32/global_record_atomic_enum_integer_mismatch_main.c|\
    test/fixtures/wasm32/global_record_atomic_enum_integer_mismatch_other.c|\
    test/fixtures/wasm32/global_atomic_callback_enum_parameter_mismatch_main.c|\
    test/fixtures/wasm32/global_atomic_callback_enum_parameter_mismatch_other.c|\
    test/fixtures/wasm32/global_callback_factory_atomic_union_parameter_mismatch_main.c|\
    test/fixtures/wasm32/global_callback_factory_atomic_union_parameter_mismatch_other.c|\
    test/fixtures/wasm32/global_callback_factory_atomic_complex_result_mismatch_main.c|\
    test/fixtures/wasm32/global_callback_factory_atomic_complex_result_mismatch_other.c|\
    test/fixtures/wasm32/record_member_callback_factory_atomic_union_parameter_mismatch_main.c|\
    test/fixtures/wasm32/record_member_callback_factory_atomic_union_parameter_mismatch_other.c|\
    test/fixtures/wasm32/global_array_callback_factory_atomic_complex_result_mismatch_main.c|\
    test/fixtures/wasm32/global_array_callback_factory_atomic_complex_result_mismatch_other.c|\
    test/fixtures/wasm32/record_member_atomic_callback_enum_result_mismatch_main.c|\
    test/fixtures/wasm32/record_member_atomic_callback_enum_result_mismatch_other.c|\
    test/fixtures/wasm32/struct_member_order_signature_mismatch_main.c|\
    test/fixtures/wasm32/struct_member_order_signature_mismatch_other.c|\
    test/fixtures/wasm32/union_member_type_signature_mismatch_main.c|\
    test/fixtures/wasm32/union_member_type_signature_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_bool_signature_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_bool_signature_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_narrow_signature_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_narrow_signature_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_promotion_signature_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_promotion_signature_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_address_direct_promotion_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_address_direct_promotion_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_direct_address_bool_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_direct_address_bool_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_void_promotion_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_void_promotion_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_return_signature_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_return_signature_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_variadic_signature_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_variadic_signature_mismatch_other.c)
      echo "multi-TU link fixture; WAT mode is single-module standalone"
      ;;
    test/fixtures/probes_found_bugs/gnu_attribute_parse.c|\
    test/fixtures/probes_found_bugs/gnu_statement_expression.c|\
    test/fixtures/probes_found_bugs/unsupported_gnu_extensions_warn_skip.c)
      echo "intentional strict-C rejection covered by wasm32 E2E reject cases"
      ;;
    test/fixtures/wasm32/setjmp_stub_ops.c|\
    test/fixtures/probes_found_bugs/setjmp_noreturn_cfg_termination_boundaries.c|\
    test/fixtures/probes_found_bugs/setjmp_noreturn_cfg_warning_boundaries.c)
      echo "intentional unsupported non-local control-flow rejection"
      ;;
    test/fixtures/wasm_continuation_basic.c)
      echo "host-provided game_running import requires continuation integration"
      ;;
    *)
      return 1
      ;;
  esac
}

if [ ! -x "$agc_wasm" ]; then
  echo "missing executable: $agc_wasm" >&2
  exit 2
fi

wat2wasm_available=0
if command -v wat2wasm >/dev/null 2>&1; then
  wat2wasm_available=1
fi

if [ "$validate" = "auto" ]; then
  if [ "$wat2wasm_available" -ne 0 ] && command -v wasm-validate >/dev/null 2>&1; then
    validate=1
  else
    validate=0
  fi
fi

mkdir -p "$out_dir"
failures="$out_dir/failures.txt"
: > "$failures"

scanned=0
failed=0
skipped=0

fixture_list="$out_dir/fixtures.txt"
if [ "$fixture_source" = "e2e" ]; then
  sed -n 's/.*"\(test\/fixtures\/[^"]*\.c\)".*/\1/p' test/test_e2e.c |
    LC_ALL=C sort -u > "$fixture_list"
else
  find test/fixtures -type f -name '*.c' | LC_ALL=C sort > "$fixture_list"
fi

while IFS= read -r src; do
  case "$src" in
    */should_reject/*)
      continue
      ;;
  esac

  if reason=$(skip_reason "$src"); then
    skipped=$((skipped + 1))
    if [ "$verbose" -ne 0 ]; then
      printf 'SKIP %s\t%s\n' "$src" "$reason"
    fi
    continue
  fi

  scanned=$((scanned + 1))
  rel=${src#test/fixtures/}
  safe=${rel//\//__}
  wat="$out_dir/${safe%.c}.wat"
  wasm="$out_dir/${safe%.c}.wasm"
  err="$out_dir/${safe%.c}.err"

  if ! "$agc_wasm" "$src" > "$wat" 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tcompile: %s\n' "$src" "$msg" >> "$failures"
    if [ "$verbose" -ne 0 ]; then
      printf 'FAIL %s\tcompile: %s\n' "$src" "$msg"
    fi
    continue
  fi

  if [ "$wat2wasm_available" -ne 0 ] && ! wat2wasm "$wat" -o "$wasm" 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\twat2wasm: %s\n' "$src" "$msg" >> "$failures"
    if [ "$verbose" -ne 0 ]; then
      printf 'FAIL %s\twat2wasm: %s\n' "$src" "$msg"
    fi
    continue
  fi

  if [ "$validate" -ne 0 ] && ! wasm-validate "$wasm" >/dev/null 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tvalidate: %s\n' "$src" "$msg" >> "$failures"
    if [ "$verbose" -ne 0 ]; then
      printf 'FAIL %s\tvalidate: %s\n' "$src" "$msg"
    fi
    continue
  fi

  if [ "$verbose" -ne 0 ]; then
    printf 'PASS %s\n' "$src"
  fi
done < "$fixture_list"

printf '==== wasm32 WAT fixture scan ====\n'
printf 'Source:   %s\n' "$fixture_source"
printf 'Total:    %d\n' "$scanned"
printf 'Pass:     %d\n' "$((scanned - failed))"
printf 'Fail:     %d\n' "$failed"
printf 'Skip:     %d\n' "$skipped"
printf 'Wat2wasm: %s\n' "$wat2wasm_available"
printf 'Validate: %s\n' "$validate"
printf 'Log:      %s\n' "$failures"

if [ "$failed" -ne 0 ]; then
  if [ "$list_fail" -ne 0 ]; then
    cat "$failures"
  else
    sed -n '1,20p' "$failures"
  fi
  exit 1
fi

exit 0

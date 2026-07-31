#!/usr/bin/env bash
set -u

agc_wasm=${AG_C_WASM:-./build/ag_c_wasm}
ag_wasm_link=${AG_WASM_LINK:-./build/ag_wasm_link}
out_dir=${WASM32_OBJECT_LINK_SCAN_DIR:-build/wasm32_obj_link_scan}
list_fail=0
verbose=0
fixture_source=e2e

usage() {
  cat <<'EOF'
usage: scripts/run_wasm32_object_link_fixture_scan.sh [--list-fail] [--verbose] [--all-fixtures]

Compiles fixtures in Wasm object mode, links each single-TU object or known
multi-TU fixture pair with ag_wasm_link, validates the linked wasm when
wasm-validate is available, and runs it when wasm-interp is available and the
linked wasm has no imports.
By default, scans fixture paths registered in test/test_e2e.c.
Set AG_C_WASM / AG_WASM_LINK to override tool paths.
Set WASM32_OBJECT_LINK_SCAN_DIR to override the output directory.
Set WASM32_OBJECT_LINK_SCAN_TIMEOUT_SEC to override the wasm-interp timeout.
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
    --all-fixtures)
      fixture_source=all
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

if [ ! -x "$agc_wasm" ]; then
  echo "missing executable: $agc_wasm" >&2
  exit 2
fi

if [ ! -x "$ag_wasm_link" ]; then
  echo "missing executable: $ag_wasm_link" >&2
  exit 2
fi

skip_reason() {
  case "$1" in
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
    test/fixtures/wasm32/stdio_file_state_ops.c)
      echo "WAT-only unavailable-file stub contract; object runtime has real in-memory files"
      ;;
    test/fixtures/probes_found_bugs/anonymous_flexible_callback_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/anonymous_flexible_function_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/anonymous_flexible_global_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/anonymous_global_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/anonymous_global_union_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/nested_anonymous_global_union_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/static_internal_linkage_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/inherited_static_linkage_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/extern_funcptr_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_return_pointee_qualifier_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_qualifier_adjustment_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_multidimensional_qualifier_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_function_adjustment_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_qualifier_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_array_qualifier_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_function_pointer_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_atomic_array_adjustment_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_nested_pointer_qualifier_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/enum_compatible_function_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/incomplete_array_bound_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/incomplete_global_array_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/incomplete_global_record_pointer_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/incomplete_global_record_object_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/aligned_global_definition_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/aligned_global_data_reloc_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/global_callback_parameter_qualifier_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/global_enum_integer_compatible_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/named_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/packed_indirect_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/packed_pointer_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/packed_callback_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/packed_global_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/packed_global_callback_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/incomplete_callback_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/record_member_alignment_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/union_member_order_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_funcptr_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_address_then_direct_call_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_direct_call_then_address_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_repeated_direct_call_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_global_callback_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_funcptr_return_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_enum_funcptr_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_atomic_funcptr_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_parameter_categories_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_callback_function_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_aligned_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_anonymous_union_data_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_array_data_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_callback_data_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_callback_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_container_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_flexible_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_function_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_incomplete_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_mutual_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_packed_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_pointer_to_array_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_record_data_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_record_function_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_self_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_union_function_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_function_pointer_data_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_function_pointer_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_incomplete_record_pointer_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_incomplete_record_wrapper_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_mutual_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_self_referential_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_function_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_function_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_return_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_to_array_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_to_record_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_array_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_multidimensional_array_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_global_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/atomic_record_member_signature_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/thread_local_xtu_boundaries_other.c)
      echo "multi-TU link fixture component without main"
      ;;
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
    test/fixtures/wasm32/unprototyped_return_signature_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_return_signature_mismatch_other.c|\
    test/fixtures/wasm32/unprototyped_variadic_signature_mismatch_main.c|\
    test/fixtures/wasm32/unprototyped_variadic_signature_mismatch_other.c)
      echo "intentional multi-TU C-signature mismatch covered by wasm32 object tests"
      ;;
    *)
      return 1
      ;;
  esac
}

link_companion() {
  case "$1" in
    test/fixtures/probes_found_bugs/anonymous_flexible_callback_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/anonymous_flexible_callback_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/anonymous_flexible_function_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/anonymous_flexible_function_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/anonymous_flexible_global_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/anonymous_flexible_global_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/anonymous_global_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/anonymous_global_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/anonymous_global_union_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/anonymous_global_union_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/nested_anonymous_global_union_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/nested_anonymous_global_union_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/static_internal_linkage_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/static_internal_linkage_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/inherited_static_linkage_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/inherited_static_linkage_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/extern_funcptr_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/extern_funcptr_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_return_pointee_qualifier_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_return_pointee_qualifier_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_qualifier_adjustment_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_parameter_qualifier_adjustment_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_multidimensional_qualifier_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_parameter_multidimensional_qualifier_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_function_adjustment_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_parameter_function_adjustment_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_qualifier_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_parameter_callback_return_qualifier_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_array_qualifier_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_parameter_callback_return_array_qualifier_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_function_pointer_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_parameter_callback_return_function_pointer_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_atomic_array_adjustment_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_parameter_atomic_array_adjustment_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/function_parameter_nested_pointer_qualifier_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/function_parameter_nested_pointer_qualifier_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/enum_compatible_function_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/enum_compatible_function_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/incomplete_array_bound_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/incomplete_array_bound_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/incomplete_global_array_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/incomplete_global_array_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/incomplete_global_record_pointer_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/incomplete_global_record_pointer_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/incomplete_global_record_object_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/incomplete_global_record_object_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/aligned_global_definition_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/aligned_global_definition_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/aligned_global_data_reloc_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/aligned_global_data_reloc_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/global_callback_parameter_qualifier_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/global_callback_parameter_qualifier_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/global_enum_integer_compatible_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/global_enum_integer_compatible_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/named_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/named_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/packed_indirect_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/packed_indirect_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/packed_pointer_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/packed_pointer_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/packed_callback_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/packed_callback_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/packed_global_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/packed_global_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/packed_global_callback_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/packed_global_callback_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/incomplete_callback_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/incomplete_callback_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/record_member_alignment_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/record_member_alignment_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/union_member_order_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/union_member_order_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_funcptr_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_funcptr_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_address_then_direct_call_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_address_then_direct_call_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_direct_call_then_address_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_direct_call_then_address_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_repeated_direct_call_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_repeated_direct_call_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_global_callback_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_global_callback_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_funcptr_return_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_funcptr_return_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_enum_funcptr_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_enum_funcptr_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_atomic_funcptr_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_atomic_funcptr_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/unprototyped_parameter_categories_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/unprototyped_parameter_categories_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_callback_function_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_callback_function_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_aligned_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_aligned_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_anonymous_union_data_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_anonymous_union_data_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_array_data_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_array_data_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_callback_data_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_callback_data_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_callback_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_callback_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_container_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_container_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_flexible_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_flexible_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_function_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_function_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_incomplete_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_incomplete_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_mutual_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_mutual_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_packed_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_packed_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_pointer_to_array_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_pointer_to_array_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_record_data_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_record_data_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_record_function_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_record_function_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_self_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_self_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_union_function_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_enum_union_function_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_function_pointer_data_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_function_pointer_data_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_function_pointer_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_function_pointer_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_incomplete_record_pointer_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_incomplete_record_pointer_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_incomplete_record_wrapper_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_incomplete_record_wrapper_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_mutual_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_mutual_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_self_referential_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_self_referential_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_function_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_function_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_function_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_pointer_function_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_return_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_pointer_return_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_to_array_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_pointer_to_array_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_to_record_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_pointer_to_record_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_array_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_pointer_array_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_multidimensional_array_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_pointer_multidimensional_array_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_global_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_pointer_global_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/atomic_record_member_signature_xtu_main.c)
      echo "test/fixtures/probes_found_bugs/atomic_record_member_signature_xtu_other.c"
      ;;
    test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries_main.c)
      echo "test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries_other.c"
      ;;
    test/fixtures/probes_found_bugs/thread_local_xtu_boundaries_main.c)
      echo "test/fixtures/probes_found_bugs/thread_local_xtu_boundaries_other.c"
      ;;
    *)
      return 1
      ;;
  esac
}

expected_result() {
  case "$1" in
    test/fixtures/probes_found_bugs/anonymous_flexible_callback_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/anonymous_flexible_function_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/anonymous_flexible_global_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/anonymous_global_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/anonymous_global_union_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/nested_anonymous_global_union_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/static_internal_linkage_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/inherited_static_linkage_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/extern_funcptr_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_return_pointee_qualifier_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_parameter_qualifier_adjustment_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_parameter_multidimensional_qualifier_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_parameter_function_adjustment_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_qualifier_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_array_qualifier_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_parameter_callback_return_function_pointer_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_parameter_atomic_array_adjustment_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/function_parameter_nested_pointer_qualifier_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/enum_compatible_function_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/incomplete_array_bound_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/named_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/unprototyped_funcptr_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/unprototyped_address_then_direct_call_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/unprototyped_direct_call_then_address_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/unprototyped_repeated_direct_call_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/unprototyped_funcptr_return_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/unprototyped_enum_funcptr_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/unprototyped_atomic_funcptr_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/unprototyped_parameter_categories_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_callback_function_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_function_pointer_data_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_array_data_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_aligned_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_anonymous_union_data_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_callback_data_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_callback_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_container_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_flexible_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_function_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_incomplete_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_mutual_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_packed_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_pointer_to_array_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_record_data_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_record_function_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_self_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_enum_union_function_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_function_pointer_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_incomplete_record_pointer_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_incomplete_record_wrapper_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_mutual_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_self_referential_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_function_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_function_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_return_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_to_array_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_to_record_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_array_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_multidimensional_array_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_pointer_global_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/atomic_record_member_signature_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/thread_local_xtu_boundaries_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/global_callback_parameter_qualifier_xtu_main.c)
      echo 42
      ;;
    test/fixtures/probes_found_bugs/global_enum_integer_compatible_xtu_main.c)
      echo 42
      ;;
    *)
      echo 0
      ;;
  esac
}

validate=0
if command -v wasm-validate >/dev/null 2>&1; then
  validate=1
fi

run=0
if command -v wasm-interp >/dev/null 2>&1 && command -v wasm-objdump >/dev/null 2>&1; then
  run=1
fi
interp_timeout_sec=${WASM32_OBJECT_LINK_SCAN_TIMEOUT_SEC:-5}

run_with_timeout() {
  local sec=$1
  shift
  perl -e '
    my $sec = shift @ARGV;
    my $pid = fork();
    die "fork failed: $!\n" unless defined $pid;
    if ($pid == 0) {
      exec @ARGV or die "exec failed: $!\n";
    }
    $SIG{ALRM} = sub {
      kill "TERM", $pid;
      select undef, undef, undef, 0.1;
      kill "KILL", $pid;
      exit 124;
    };
    alarm $sec;
    waitpid($pid, 0);
    my $st = $?;
    exit(($st & 127) ? 128 + ($st & 127) : ($st >> 8));
  ' "$sec" "$@"
}

mkdir -p "$out_dir"
failures="$out_dir/failures.txt"
: > "$failures"

fixture_list="$out_dir/fixtures.txt"
if [ "$fixture_source" = "e2e" ]; then
  sed -n 's/.*"\(test\/fixtures\/[^"]*\.c\)".*/\1/p' test/test_e2e.c |
    LC_ALL=C sort -u > "$fixture_list"
else
  find test/fixtures -type f -name '*.c' | LC_ALL=C sort > "$fixture_list"
fi

scanned=0
failed=0
validated=0
runnable=0
ran=0
skipped=0
skipped_run_imports=0
skipped_run_tools=0

while IFS= read -r src; do
  case "$src" in
    */should_reject/*)
      continue
      ;;
  esac

  if reason=$(skip_reason "$src"); then
    skipped=$((skipped + 1))
    [ "$verbose" -ne 0 ] && printf 'SKIP %s\t%s\n' "$src" "$reason"
    continue
  fi

  scanned=$((scanned + 1))
  rel=${src#test/fixtures/}
  safe=${rel//\//__}
  obj="$out_dir/${safe%.c}.o"
  wasm="$out_dir/${safe%.c}.wasm"
  err="$out_dir/${safe%.c}.err"
  interp="$out_dir/${safe%.c}.interp"
  dump="$out_dir/${safe%.c}.objdump"
  companion=
  extra_obj=
  expect=$(expected_result "$src")

  if ! "$agc_wasm" -c -o "$obj" "$src" >/dev/null 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tcompile: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tcompile: %s\n' "$src" "$msg"
    continue
  fi

  if companion=$(link_companion "$src"); then
    companion_rel=${companion#test/fixtures/}
    companion_safe=${companion_rel//\//__}
    extra_obj="$out_dir/${companion_safe%.c}.o"
    if ! "$agc_wasm" -c -o "$extra_obj" "$companion" >/dev/null 2>"$err"; then
      failed=$((failed + 1))
      msg=$(sed -n '1p' "$err")
      printf '%s\tcompile companion %s: %s\n' "$src" "$companion" "$msg" >> "$failures"
      [ "$verbose" -ne 0 ] && printf 'FAIL %s\tcompile companion %s: %s\n' "$src" "$companion" "$msg"
      continue
    fi
  fi

  if [ -n "$extra_obj" ]; then
    if ! "$ag_wasm_link" --no-entry --export=main -o "$wasm" "$obj" "$extra_obj" >/dev/null 2>"$err"; then
      failed=$((failed + 1))
      msg=$(sed -n '1p' "$err")
      printf '%s\tlink: %s\n' "$src" "$msg" >> "$failures"
      [ "$verbose" -ne 0 ] && printf 'FAIL %s\tlink: %s\n' "$src" "$msg"
      continue
    fi
  elif ! "$ag_wasm_link" --no-entry --export=main -o "$wasm" "$obj" >/dev/null 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tlink: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tlink: %s\n' "$src" "$msg"
    continue
  fi

  if [ "$validate" -ne 0 ]; then
    if ! wasm-validate "$wasm" >/dev/null 2>"$err"; then
      failed=$((failed + 1))
      msg=$(sed -n '1p' "$err")
      printf '%s\tvalidate: %s\n' "$src" "$msg" >> "$failures"
      [ "$verbose" -ne 0 ] && printf 'FAIL %s\tvalidate: %s\n' "$src" "$msg"
      continue
    fi
    validated=$((validated + 1))
  fi

  if [ "$run" -eq 0 ]; then
    skipped_run_tools=$((skipped_run_tools + 1))
    [ "$verbose" -ne 0 ] && printf 'PASS %s\tlink-only\n' "$src"
    continue
  fi

  if ! wasm-objdump -x "$wasm" > "$dump" 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tobjdump: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tobjdump: %s\n' "$src" "$msg"
    continue
  fi

  if grep -q '^Import\[' "$dump"; then
    skipped_run_imports=$((skipped_run_imports + 1))
    [ "$verbose" -ne 0 ] && printf 'PASS %s\tlink-only-imports\n' "$src"
    continue
  fi

  runnable=$((runnable + 1))
  interp_status=0
  run_with_timeout "$interp_timeout_sec" wasm-interp "$wasm" --run-all-exports > "$interp" 2>"$err" ||
    interp_status=$?
  if [ "$interp_status" -ne 0 ]; then
    failed=$((failed + 1))
    if [ "$interp_status" -eq 124 ]; then
      msg="wasm-interp timed out after ${interp_timeout_sec}s"
    else
      msg=$(sed -n '1p' "$err")
      [ -n "$msg" ] || msg="wasm-interp exited with status $interp_status"
    fi
    printf '%s\trun: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\trun: %s\n' "$src" "$msg"
    continue
  fi

  if ! grep -q "main() => i32:$expect" "$interp"; then
    failed=$((failed + 1))
    msg=$(tr '\n' ' ' < "$interp")
    printf '%s\tresult: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tresult: %s\n' "$src" "$msg"
    continue
  fi
  ran=$((ran + 1))
  [ "$verbose" -ne 0 ] && printf 'PASS %s\trun\n' "$src"
done < "$fixture_list"

printf '==== wasm32 object link fixture scan ====\n'
printf 'Source:           %s\n' "$fixture_source"
printf 'Total:            %d\n' "$scanned"
printf 'Pass:             %d\n' "$((scanned - failed))"
printf 'Fail:             %d\n' "$failed"
printf 'Skip:             %d\n' "$skipped"
printf 'Validate:         %s\n' "$validate"
printf 'Validated:        %d\n' "$validated"
printf 'Run tools:        %s\n' "$run"
printf 'Runnable:         %d\n' "$runnable"
printf 'Ran:              %d\n' "$ran"
printf 'Skip run imports: %d\n' "$skipped_run_imports"
printf 'Skip run tools:   %d\n' "$skipped_run_tools"
printf 'Log:              %s\n' "$failures"

if [ "$failed" -ne 0 ]; then
  if [ "$list_fail" -ne 0 ]; then
    cat "$failures"
  else
    sed -n '1,20p' "$failures"
  fi
  exit 1
fi

exit 0

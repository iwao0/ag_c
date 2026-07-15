#include "../src/parser/parser.h"
#include "../src/compilation_session_internal.h"
#include "../src/type_layout.h"
#include "../src/codegen_emit.h"
#include "../src/declaration_pipeline.h"
#include "../src/parser/arena.h"
#include "../src/parser/alignas_value.h"
#include "../src/parser/decl.h"
#include "../src/parser/declarator_shape_builder.h"
#include "../src/parser/lvar_internal.h"
#include "../src/parser/expr.h"
#include "../src/parser/function_parameter_syntax.h"
#include "../src/parser/function_public.h"
#include "../src/parser/gvar_public.h"
#include "../src/parser/global_registry.h"
#include "../src/parser/literal_public.h"
#include "../src/parser/lvar_public.h"
#include "../src/parser/local_registry.h"
#include "../src/parser/node_type_public.h"
#include "../src/parser/node_utils.h"
#include "../src/parser/node_vla_public.h"
#include "../src/parser/tag_public.h"
#include "../src/parser/semantic_ctx.h"
#include "../src/parser/runtime_context.h"
#include "../src/parser/stmt.h"
#include "../src/parser/symtab.h"
#include "../src/parser/type_builder.h"
#include "../src/parser/aggregate_member_syntax.h"
#include "../src/semantic/declaration_application.h"
#include "../src/semantic/declaration_registration.h"
#include "../src/frontend/function_definition.h"
#include "../src/frontend/local_declaration.h"
#include "../src/frontend/semantic_pipeline.h"
#include "../src/frontend/toplevel_declaration.h"
#include "../src/frontend/translation_unit.h"
#include "../src/preprocess/preprocess.h"
#include "../src/diag/diag.h"
#include "../src/semantic/aggregate_member_resolution.h"
#include "../src/semantic/constant_expression.h"
#include "../src/semantic/declaration_resolution.h"
#include "../src/semantic/enum_constant_resolution.h"
#include "../src/semantic/function_declaration_resolution.h"
#include "../src/semantic/function_parameter_resolution.h"
#include "../src/semantic/initializer_resolution.h"
#include "../src/semantic/identifier_resolution.h"
#include "../src/semantic/member_access_resolution.h"
#include "../src/semantic/expression_operand_resolution.h"
#include "../src/semantic/function_call_resolution.h"
#include "../src/semantic/generic_selection_resolution.h"
#include "../src/semantic/identifier_binding.h"
#include "../src/semantic/semantic_invariants.h"
#include "../src/semantic/semantic_pass.h"
#include "../src/semantic/type_query_resolution.h"
#include "../src/semantic/type_name_resolution.h"
#include "../src/semantic/local_declaration_plan.h"
#include "../src/semantic/local_declaration_resolution.h"
#include "../src/semantic/global_declaration_resolution.h"
#include "../src/semantic/parameter_declaration_plan.h"
#include "../src/semantic/parameter_declaration_resolution.h"
#include "../src/semantic/static_assert_resolution.h"
#include "../src/semantic/static_initializer_resolution.h"
#include "../src/semantic/tag_declaration_resolution.h"
#include "../src/semantic/typedef_declaration_resolution.h"
#include "../src/lowering/global_object_lowering.h"
#include "../src/lowering/abi_lowering.h"
#include "../src/lowering/expr_lowering.h"
#include "../src/lowering/subscript_lowering.h"
#include "../src/lowering/runtime_context.h"
#include "../src/lowering/local_storage.h"
#include "../src/lowering/local_object_lowering.h"
#include "../src/lowering/parameter_lowering.h"
#include "../src/lowering/vla_lowering.h"
#include "../src/lowering/static_data_initializer.h"
#include "../src/lowering/static_local_lowering.h"
#include "../src/lowering/translation_unit_data_lowering.h"
#include "../src/pragma_pack.h"
#include "../src/tokenizer/tokenizer.h"
#include "../src/tokenizer/allocator.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "test_common.h"

static node_t **parsed_code;
static ag_compilation_session_t *test_suite_session;

static ag_diagnostic_context_t *test_diagnostics(void) {
  return ag_compilation_session_diagnostic_context(test_suite_session);
}

static arena_context_t *test_arena_context(void) {
  return ag_compilation_session_arena_context(test_suite_session);
}

/* Test fixtures use the suite session explicitly without repeating it at
 * every constructor call. Production code is forbidden from these aliases. */
#define ps_type_new(...) \
  ps_type_new_in(test_arena_context(), __VA_ARGS__)
#define ps_type_new_integer(kind, legacy_size, is_unsigned) \
  ps_type_new_integer_in(test_arena_context(), kind, is_unsigned)
#define ps_type_new_enum(name, len, scope_depth_p1, legacy_size) \
  ps_type_new_enum_in(test_arena_context(), name, len, scope_depth_p1)
#define ps_type_new_float(kind, legacy_size) \
  ps_type_new_float_in(test_arena_context(), kind)
#define ps_type_new_pointer(...) \
  ps_type_new_pointer_in(test_arena_context(), __VA_ARGS__)
#define ps_type_new_function(...) \
  ps_type_new_function_in(test_arena_context(), __VA_ARGS__)
#define ps_type_new_array(base, len, legacy_size, is_vla) \
  ps_type_new_array_in(test_arena_context(), base, len, is_vla)
#define ps_type_clone(...) \
  ps_type_clone_in(test_arena_context(), __VA_ARGS__)
#define ps_type_new_tag(kind, name, len, scope_depth_p1, legacy_size) \
  ps_type_new_tag_in(test_arena_context(), kind, name, len, scope_depth_p1)
#define ps_type_set_function_params(...) \
  ps_type_set_function_params_in(test_arena_context(), __VA_ARGS__)
#define ps_type_wrap_array_dims(...) \
  ps_type_wrap_array_dims_in(test_arena_context(), __VA_ARGS__)
#define ps_type_apply_declarator_shape(...) \
  ps_type_apply_declarator_shape_in(test_arena_context(), __VA_ARGS__)
#define ps_type_adjust_parameter_type(...) \
  ps_type_adjust_parameter_type_in(test_arena_context(), __VA_ARGS__)
#define ps_type_usual_arithmetic_result(...) \
  ps_type_usual_arithmetic_result_for_target_in( \
      test_arena_context(), \
      ps_ctx_target_info(test_semantic_context()), __VA_ARGS__)
#define ps_type_binary_result(...) \
  ps_type_binary_result_for_target_in( \
      test_arena_context(), \
      ps_ctx_target_info(test_semantic_context()), __VA_ARGS__)
#define ps_type_conditional_result(...) \
  ps_type_conditional_result_for_target_in( \
      test_arena_context(), \
      ps_ctx_target_info(test_semantic_context()), __VA_ARGS__)
#define ps_type_format_canonical_signature(...) \
  test_type_format_canonical_signature(__VA_ARGS__)
#define ps_type_layout_of(...) test_type_layout_of(__VA_ARGS__)
#define ps_type_sizeof_for_target(...) \
  test_type_sizeof_for_target(__VA_ARGS__)
#define ps_type_alignof_for_target(...) \
  test_type_alignof_for_target(__VA_ARGS__)
static int test_type_layout_of(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_type_layout_t *out);
static int test_type_sizeof_for_target(
    const psx_type_t *type, const ag_target_info_t *target);
static int test_type_alignof_for_target(
    const psx_type_t *type, const ag_target_info_t *target);
#define ps_type_address_result(...) \
  ps_type_address_result_in(test_arena_context(), __VA_ARGS__)
#define ps_type_decay_array(...) \
  ps_type_decay_array_in(test_arena_context(), __VA_ARGS__)
#define ps_type_subscript_result(...) \
  ps_type_subscript_result_in(test_arena_context(), __VA_ARGS__)
#define ps_type_generic_control(...) \
  ps_type_generic_control_in(test_arena_context(), __VA_ARGS__)
#define ps_type_generic_select_index(...) \
  ps_type_generic_select_index_in(test_arena_context(), __VA_ARGS__)
#define ps_type_sizeof(...) test_type_sizeof(__VA_ARGS__)
#define ps_type_deref_size(...) test_type_deref_size(__VA_ARGS__)
#define ps_type_pointee_value_size(...) \
  test_type_pointee_value_size(__VA_ARGS__)
#define ps_type_array_scalar_element_size(...) \
  test_type_array_scalar_element_size(__VA_ARGS__)
#define ps_type_array_subscript_stride_bytes(...) \
  test_type_array_subscript_stride_bytes(__VA_ARGS__)
#define ps_type_subscript_static_stride(...) \
  test_type_subscript_static_stride(__VA_ARGS__)
#define ps_type_pointer_view_structural_base_deref_size(...) \
  test_type_pointer_view_structural_base_deref_size(__VA_ARGS__)
#define ps_type_pointer_view_structural_ptr_array_pointee_bytes(...) \
  test_type_pointer_view_structural_ptr_array_pointee_bytes(__VA_ARGS__)
#define ps_declarator_shape_append_pointer(...) \
  ps_declarator_shape_append_pointer_in(test_arena_context(), __VA_ARGS__)
#define ps_declarator_shape_append_array(...) \
  ps_declarator_shape_append_array_in(test_arena_context(), __VA_ARGS__)
#define ps_declarator_shape_append_array_ex(...) \
  ps_declarator_shape_append_array_ex_in(test_arena_context(), __VA_ARGS__)
#define ps_declarator_shape_append_vla_array(...) \
  ps_declarator_shape_append_vla_array_in( \
      test_arena_context(), __VA_ARGS__)
#define ps_declarator_shape_append_function(...) \
  ps_declarator_shape_append_function_in(test_arena_context(), __VA_ARGS__)
#define ps_declarator_op_set_function_params(...) \
  ps_declarator_op_set_function_params_in( \
      test_arena_context(), __VA_ARGS__)
#define ps_declarator_shape_copy(...) \
  ps_declarator_shape_copy_in(test_arena_context(), __VA_ARGS__)

#define ps_node_new_binary(...) \
  ps_node_new_binary_for_target_in( \
      test_arena_context(), \
      ps_ctx_target_info(test_semantic_context()), __VA_ARGS__)
#define ps_node_row_decay_pointer_arith_type(...) \
  ps_node_array_decay_pointer_arith_type_in( \
      test_arena_context(), __VA_ARGS__)
#define ps_node_type_size(...) test_node_type_size(__VA_ARGS__)
#define ps_node_storage_type_size(...) \
  test_node_type_size(__VA_ARGS__)
#define ps_node_deref_size(...) test_node_deref_size(__VA_ARGS__)
#define ps_node_aggregate_value_size(...) \
  test_node_aggregate_value_size(__VA_ARGS__)
#define ps_node_cast_i64_extension_info(...) \
  test_node_cast_i64_extension_info(__VA_ARGS__)
#define ps_node_i64_widen_source_is_unsigned(...) \
  test_node_i64_widen_source_is_unsigned(__VA_ARGS__)
#define ps_gvar_decl_sizeof(...) test_gvar_decl_sizeof(__VA_ARGS__)
#define ps_gvar_storage_size(...) test_gvar_decl_sizeof(__VA_ARGS__)
#define ps_gvar_array_element_size(...) \
  test_gvar_array_element_size(__VA_ARGS__)
#define ps_gvar_initializer_element_size(...) \
  test_gvar_initializer_element_size(__VA_ARGS__)
#define ps_gvar_initializer_element_count(...) \
  test_gvar_initializer_element_count(__VA_ARGS__)
#define ps_lvar_decl_sizeof(...) test_lvar_decl_sizeof(__VA_ARGS__)
#define ps_lvar_storage_size(...) test_lvar_storage_size(__VA_ARGS__)
#define ps_lvar_array_scalar_element_size(...) \
  test_lvar_array_scalar_element_size(__VA_ARGS__)
#define psx_node_new_raw_binary(...) \
  psx_node_new_raw_binary_in(test_arena_context(), __VA_ARGS__)
#define ps_node_compound_literal_array_size(...) \
  test_node_compound_literal_array_size(__VA_ARGS__)
#define ps_node_usual_arith_is_unsigned(...) \
  test_node_usual_arith_is_unsigned(__VA_ARGS__)
#define ps_node_new_num(...) \
  ps_node_new_num_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_lvar(...) \
  psx_node_new_lvar_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_lvar_typed(...) \
  ps_node_new_lvar_typed_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_lvar_typed_at_for(...) \
  test_node_new_lvar_typed_at_for(__VA_ARGS__)
#define ps_node_new_lvar_type_at_for(...) \
  ps_node_new_lvar_type_at_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_lvar_scalar_slot_at(...) \
  psx_node_new_lvar_scalar_slot_at_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_lvar_fp_slot_at(...) \
  psx_node_new_lvar_fp_slot_at_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_lvar_fp_slot_for(...) \
  ps_node_new_lvar_fp_slot_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_param_placeholder(...) \
  ps_node_new_param_placeholder_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_unsigned_lvar_typed(...) \
  ps_node_new_unsigned_lvar_typed_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_lvar_for(...) \
  psx_node_new_lvar_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_lvar_object_ref_for(...) \
  psx_node_new_lvar_object_ref_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_lvar_expr_ref_for(...) \
  ps_node_new_lvar_expr_ref_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_lvar_identifier_ref_for(...) \
  psx_node_new_lvar_identifier_ref_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_vla_decay_ref_for(...) \
  psx_node_new_vla_decay_ref_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_param_lvar_for(...) \
  ps_node_new_param_lvar_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_array_elem_lvar_for(...) \
  test_node_new_array_elem_lvar_for(__VA_ARGS__)
#define ps_node_new_fp_to_int_cast(...) \
  ps_node_new_fp_to_int_cast_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_int_to_fp_cast(...) \
  ps_node_new_int_to_fp_cast_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_integer_cast_result(...) \
  ps_node_new_integer_cast_result_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_integer_cast_result_ex(...) \
  ps_node_new_integer_cast_result_ex_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_i64_to_i32_trunc_cast(...) \
  ps_node_new_i64_to_i32_trunc_cast_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_pointer_cast_result(...) \
  ps_node_new_pointer_cast_result_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_aggregate_cast_result(...) \
  ps_node_new_aggregate_cast_result_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_void_cast_result(...) \
  ps_node_new_void_cast_result_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_source_cast(...) \
  psx_node_new_source_cast_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_gvar_array_addr_for(...) \
  ps_node_new_gvar_array_addr_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_static_local_array_addr_for(...) \
  psx_node_new_static_local_array_addr_for_in( \
      test_arena_context(), __VA_ARGS__)
#define ps_node_new_lvar_array_addr_for(...) \
  ps_node_new_lvar_array_addr_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_addr_value_for(...) \
  ps_node_new_addr_value_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_explicit_addr_value_for(...) \
  ps_node_new_explicit_addr_value_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_unary_addr_for(...) \
  ps_node_new_unary_addr_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_tag_member_deref_for(...) \
  ps_node_new_tag_member_deref_for_in( \
      test_arena_context(), \
      ps_ctx_target_info(test_semantic_context()), __VA_ARGS__)
#define ps_node_new_unary_deref_for(...) \
  ps_node_new_unary_deref_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_unary_deref_syntax_for(...) \
  psx_node_new_unary_deref_syntax_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_subscript_syntax_for(...) \
  psx_node_new_subscript_syntax_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_subscript_deref_for(...) \
  ps_node_new_subscript_deref_for_in( \
      test_arena_context(), \
      ps_ctx_target_info(test_semantic_context()), __VA_ARGS__)
#define ps_node_new_tag_member_lvar_ref_for(...) \
  ps_node_new_tag_member_lvar_ref_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_gvar_for(...) \
  ps_node_new_gvar_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_gvar_array_base_for(...) \
  psx_node_new_gvar_array_base_for_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_static_local_gvar_for(...) \
  psx_node_new_static_local_gvar_for_in(test_arena_context(), __VA_ARGS__)
#define ps_node_clone_lvalue_with_lhs(...) \
  ps_node_clone_lvalue_with_lhs_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_vla_alloc(...) \
  ps_node_new_vla_alloc_in(test_arena_context(), __VA_ARGS__)
#define ps_node_new_assign(...) \
  ps_node_new_assign_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_raw_assign(...) \
  psx_node_new_raw_assign_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_raw_decl_initializer(...) \
  psx_node_new_raw_decl_initializer_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_compound_literal(...) \
  psx_node_new_compound_literal_in(test_arena_context(), __VA_ARGS__)
#define psx_node_new_raw_decl_initializer_list(...) \
  psx_node_new_raw_decl_initializer_list_in( \
      test_arena_context(), __VA_ARGS__)
#define psx_node_new_initializer_list(...) \
  psx_node_new_initializer_list_in(test_arena_context(), __VA_ARGS__)

static psx_semantic_context_t *test_semantic_context(void) {
  return ag_compilation_session_semantic_context(test_suite_session);
}

static psx_global_registry_t *test_global_registry(void) {
  return ag_compilation_session_global_registry(test_suite_session);
}

static psx_local_registry_t *test_local_registry(void) {
  return ag_compilation_session_local_registry(test_suite_session);
}

static bool test_semantic_has_tag_type(
    token_kind_t kind, char *name, int len) {
  return ps_ctx_has_tag_type_in(
      test_semantic_context(), kind, name, len);
}

static void test_semantic_define_tag_type_with_layout(
    token_kind_t kind, char *name, int len,
    int member_count, int tag_size, int tag_align) {
  int is_complete = member_count > 0 || tag_size > 0 || tag_align > 0;
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      test_semantic_context(), test_local_registry(),
      kind, name, len, is_complete, member_count, tag_size, tag_align));
}

static int test_semantic_register_tag_type(
    token_kind_t kind, char *name, int len,
    int is_complete, int member_count, int tag_size, int tag_align) {
  return ps_ctx_register_tag_type_in_contexts(
      test_semantic_context(), test_local_registry(),
      kind, name, len, is_complete, member_count, tag_size, tag_align);
}

static int test_semantic_register_tag_member(
    token_kind_t kind, char *name, int len,
    const tag_member_info_t *member, int *out_created) {
  return psx_ctx_register_tag_member_in(
      test_semantic_context(), kind, name, len, member, out_created);
}

static int test_semantic_register_tag_members(
    token_kind_t kind, char *name, int len,
    const tag_member_info_t *members, int member_count,
    int *out_conflict_index) {
  return ps_ctx_register_tag_members_in(
      test_semantic_context(), kind, name, len, members, member_count,
      out_conflict_index);
}

static int test_semantic_define_enum_const(
    char *name, int len, long long value) {
  return ps_ctx_register_enum_const_in_contexts(
      test_semantic_context(), test_local_registry(),
      name, len, value, NULL);
}

static int test_semantic_define_typedef_name(
    char *name, int len, const psx_typedef_info_t *info) {
  return ps_ctx_register_typedef_name_in_contexts(
      test_semantic_context(), test_local_registry(),
      name, len, info, NULL, NULL);
}

static void test_semantic_define_function_name_with_ret(
    char *name, int len, int ret_struct_size) {
  psx_ctx_define_function_name_with_ret_in(
      test_semantic_context(), name, len, ret_struct_size);
}

static int test_semantic_track_function_type(
    char *name, int len, const psx_type_t *function_type) {
  return psx_ctx_track_function_type_in(
      test_semantic_context(), name, len, function_type);
}

static global_var_t *find_test_global_var(char *name, int len) {
  return ps_find_global_var_in(test_global_registry(), name, len);
}

static bool iter_test_float_literals(
    float_lit_visitor_t visitor, void *user) {
  return ps_iter_float_literals_in(
      test_global_registry(), visitor, user);
}

static void reset_test_global_registry_translation_unit(void) {
  ps_global_registry_reset_translation_unit_in(test_global_registry());
}

static ag_compilation_options_t *test_compilation_options(void) {
  return ag_compilation_session_options(test_suite_session);
}

static psx_lowering_context_t *test_lowering_context(void) {
  return ag_compilation_session_lowering_context(test_suite_session);
}

static psx_type_id_t intern_test_type_id(const psx_type_t *type) {
  return ps_ctx_intern_qual_type_in(
      test_semantic_context(), type).type_id;
}

static int define_test_record_decl(const psx_record_decl_t *record) {
  return psx_record_decl_table_define(
      (psx_record_decl_table_t *)ps_ctx_record_decl_table_in(
          test_semantic_context()),
      record);
}

static const psx_record_decl_t *test_record_decl_in(
    psx_semantic_context_t *semantic_context, const psx_type_t *type) {
  return ps_ctx_get_record_decl_in(
      semantic_context, ps_type_record_id(type));
}

static const psx_record_decl_t *test_record_decl(
    const psx_type_t *type) {
  return test_record_decl_in(test_semantic_context(), type);
}

static psx_record_decl_t *test_record_decl_mut(const psx_type_t *type) {
  return (psx_record_decl_t *)test_record_decl(type);
}

static int test_type_size_id(psx_type_id_t type_id) {
  return ps_type_sizeof_id_with_records(
      ps_ctx_semantic_type_table_in(test_semantic_context()),
      ps_ctx_record_layout_table_in(test_semantic_context()),
      type_id, ps_ctx_target_info(test_semantic_context()));
}

static int test_tag_member_decl_value_size(const tag_member_info_t *member) {
  return ps_ctx_type_sizeof_in(
      test_semantic_context(), ps_tag_member_decl_value_type(member));
}

static int test_tag_member_decl_storage_size(const tag_member_info_t *member) {
  return ps_ctx_type_sizeof_in(
      test_semantic_context(), ps_tag_member_decl_type(member));
}

static int test_node_atomic_pointer_info(
    node_t *pointer, const ag_target_info_t *target,
    int *width, int *is_unsigned) {
  if (!pointer || !target) return 0;
  const psx_type_t *pointee = ps_type_pointee_value_type(
      ps_node_get_type(pointer));
  int pointee_width = ps_type_sizeof_for_target(pointee, target);
  if (pointee_width != 1 && pointee_width != 2 &&
      pointee_width != 4 && pointee_width != 8) {
    pointee_width = 4;
  }
  if (width) *width = pointee_width;
  if (is_unsigned) {
    *is_unsigned = pointer->kind == ND_ADDR && pointer->lhs
                       ? ps_node_is_unsigned_type(pointer->lhs)
                       : ps_type_is_unsigned(pointee);
  }
  return 1;
}

static int test_node_type_size(node_t *node) {
  return node ? ps_type_sizeof_for_target(
                    ps_node_get_type(node),
                    ps_ctx_target_info(test_semantic_context()))
              : 0;
}

static int test_node_deref_size(node_t *node) {
  const psx_type_t *type = node ? ps_node_get_type(node) : NULL;
  if (!type || !type->base ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return 0;
  }
  return ps_type_sizeof_for_target(
      type->base, ps_ctx_target_info(test_semantic_context()));
}

static int test_node_aggregate_value_size(node_t *node) {
  const psx_type_t *type = node ? ps_node_get_type(node) : NULL;
  return ps_type_is_tag_aggregate(type) ? test_node_type_size(node) : 0;
}

static int test_node_cast_i64_extension_info(
    node_t *node, int *target_size,
    int *widen_zext_i64, int *needs_i64_extend) {
  if (target_size) *target_size = 0;
  if (widen_zext_i64) *widen_zext_i64 = 0;
  if (needs_i64_extend) *needs_i64_extend = 0;
  if (!node) return 0;
  int size = test_node_type_size(node);
  if (target_size) *target_size = size;
  if (widen_zext_i64) *widen_zext_i64 = node->widen_zext_i64 ? 1 : 0;
  if (needs_i64_extend) {
    *needs_i64_extend =
        !ps_node_value_is_pointer_like(node) && size >= 8;
  }
  return 1;
}

static int test_node_i64_widen_source_is_unsigned(node_t *node) {
  const psx_type_t *type = node ? ps_node_get_type(node) : NULL;
  if (!type || (type->kind != PSX_TYPE_BOOL &&
                type->kind != PSX_TYPE_INTEGER)) {
    return 0;
  }
  return test_node_type_size(node) >= 4 &&
         ps_node_conversion_value_is_unsigned(node);
}

static int test_gvar_decl_sizeof(
    const global_var_t *global, int fallback_size) {
  int size = ps_type_sizeof_for_target(
      ps_gvar_get_decl_type(global),
      ps_ctx_target_info(test_semantic_context()));
  return size > 0 ? size : fallback_size;
}

static int test_gvar_array_element_size(const global_var_t *global) {
  const psx_type_t *type = ps_gvar_get_decl_type(global);
  if (!type || type->kind != PSX_TYPE_ARRAY || !type->base) return 0;
  return ps_type_sizeof_for_target(
      type->base, ps_ctx_target_info(test_semantic_context()));
}

static int test_gvar_initializer_element_size(
    const global_var_t *global, int fallback_size) {
  const psx_type_t *type = ps_gvar_get_decl_type(global);
  if (!type || type->kind != PSX_TYPE_ARRAY) return fallback_size;
  return ps_type_sizeof_for_target(
      ps_type_array_leaf_type(type),
      ps_ctx_target_info(test_semantic_context()));
}

static int test_gvar_initializer_element_count(
    const global_var_t *global, int fallback_size) {
  const psx_type_t *type = ps_gvar_get_decl_type(global);
  if (!type || type->kind != PSX_TYPE_ARRAY) {
    return ps_gvar_has_explicit_initializer(global) ? 1 : 0;
  }
  if (ps_type_is_incomplete_array(type)) return 0;
  int element_size = test_gvar_initializer_element_size(
      global, fallback_size);
  int storage_size = test_gvar_decl_sizeof(global, fallback_size);
  return element_size > 0
             ? (storage_size + element_size - 1) / element_size
             : 0;
}

static int test_lvar_decl_sizeof(
    const lvar_t *var, int fallback_size) {
  int size = ps_type_sizeof_for_target(
      ps_lvar_get_decl_type(var),
      ps_ctx_target_info(test_semantic_context()));
  return size > 0 ? size : fallback_size;
}

static int test_lvar_storage_size(
    const lvar_t *var, int fallback_size) {
  int declaration_size = test_lvar_decl_sizeof(var, 0);
  int frame_size = ps_lvar_frame_storage_size(var);
  int size = frame_size > declaration_size ? frame_size : declaration_size;
  return size > 0 ? size : fallback_size;
}

static int test_lvar_array_scalar_element_size(const lvar_t *var) {
  const psx_type_t *type = ps_lvar_get_decl_type(var);
  const psx_type_t *element = type && type->kind == PSX_TYPE_ARRAY
                                  ? ps_type_array_leaf_type(type)
                                  : ps_type_pointee_value_type(type);
  return ps_type_sizeof_for_target(
      element, ps_ctx_target_info(test_semantic_context()));
}

static token_kind_t test_integer_kind_for_storage_size(int size) {
  if (size <= 1) return TK_CHAR;
  if (size == 2) return TK_SHORT;
  if (size >= 8) return TK_LONG;
  return TK_INT;
}

static const psx_type_t *test_array_element_type_for_size(
    const psx_type_t *type, int type_size) {
  while (type && type->kind == PSX_TYPE_ARRAY && type->base) {
    int element_size = ps_type_sizeof_for_target(
        type->base, ps_ctx_target_info(test_semantic_context()));
    if (element_size == type_size) return type->base;
    type = type->base;
  }
  return NULL;
}

static node_t *test_node_new_lvar_typed_at_for(
    lvar_t *owner, int offset, int type_size) {
  const psx_type_t *type = NULL;
  if (owner) {
    const psx_type_t *owner_type = ps_lvar_get_decl_type(owner);
    int relative_offset = offset - ps_lvar_offset(owner);
    int scalar_size = test_lvar_array_scalar_element_size(owner);
    if (relative_offset == 0 &&
        test_lvar_decl_sizeof(owner, 0) == type_size) {
      type = owner_type;
    } else if (relative_offset >= 0 && scalar_size > 0 &&
               relative_offset % scalar_size == 0) {
      type = test_array_element_type_for_size(owner_type, type_size);
    }
  }
  if (!type) {
    type = ps_type_new_integer(
        test_integer_kind_for_storage_size(
            type_size > 0 ? type_size : 8),
        type_size, 0);
  }
  return ps_node_new_lvar_type_at_for_in(
      test_arena_context(), owner, offset, type);
}

static node_t *test_node_new_array_elem_lvar_for(
    lvar_t *var, int index) {
  const psx_type_t *array_type = ps_lvar_get_decl_type(var);
  const psx_type_t *element = ps_type_array_leaf_type(array_type);
  int element_size = test_lvar_array_scalar_element_size(var);
  if (!element) element = ps_type_new_integer(TK_INT, 4, 0);
  return ps_node_new_lvar_type_at_for_in(
      test_arena_context(), var,
      ps_lvar_offset(var) + index * element_size, element);
}

static int test_node_compound_literal_array_size(node_t *node) {
  if (!node) return 0;
  if (node->kind == ND_COMMA) {
    return test_node_compound_literal_array_size(node->rhs);
  }
  if (node->kind != ND_ADDR || node->is_explicit_addr_expr || !node->lhs) {
    return 0;
  }
  const psx_type_t *object_type = ps_node_get_type(node->lhs);
  return object_type && object_type->kind == PSX_TYPE_ARRAY
             ? ps_type_sizeof_for_target(
                   object_type,
                   ps_ctx_target_info(test_semantic_context()))
             : 0;
}

static int plan_test_local_storage(
    const psx_type_t *type, psx_local_storage_plan_t *plan) {
  ag_target_info_t target = ag_target_info_host();
  return psx_plan_local_storage_for_type_id(
      ps_ctx_semantic_type_table_in(test_semantic_context()),
      ps_ctx_record_layout_table_in(test_semantic_context()),
      intern_test_type_id(type), &target, plan);
}

static int plan_test_parameter_storage(
    const psx_type_t *type, psx_parameter_storage_plan_t *plan) {
  ag_target_info_t target = ag_target_info_host();
  return psx_plan_parameter_storage_for_type_id(
      ps_ctx_semantic_type_table_in(test_semantic_context()),
      ps_ctx_record_layout_table_in(test_semantic_context()),
      intern_test_type_id(type), &target, plan);
}

static int test_tag_flat_slot_count(
    token_kind_t tag_kind, char *tag_name, int tag_len) {
  return ps_tag_flat_slot_count_in(
      test_semantic_context(), tag_kind, tag_name, tag_len);
}

static int test_tag_member_flat_slots(const tag_member_info_t *member) {
  return ps_tag_member_flat_slots_in(test_semantic_context(), member);
}

static int test_tag_member_elem_flat_slots(
    const tag_member_info_t *member) {
  return ps_tag_member_elem_flat_slots_in(
      test_semantic_context(), member);
}

static int test_tag_find_named_member(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    char *member_name, int member_len,
    tag_member_info_t *out, int *out_ordinal) {
  return ps_tag_find_named_member_in(
      test_semantic_context(), tag_kind, tag_name, tag_len,
      member_name, member_len, out, out_ordinal);
}

static int test_tag_select_union_member_for_init_slot(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const global_var_t *global, int index, tag_member_info_t *member) {
  return ps_tag_select_union_member_for_init_slot_in(
      test_semantic_context(), tag_kind, tag_name, tag_len,
      global, index, member);
}

static int test_tag_union_init_member_for_slot(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const global_var_t *global, int index, tag_member_info_t *out) {
  return ps_tag_union_init_member_for_slot_in(
      test_semantic_context(), tag_kind, tag_name, tag_len,
      global, index, out);
}

static int test_tag_first_named_member(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    tag_member_info_t *out, int *out_ordinal) {
  return ps_tag_first_named_member_in(
      test_semantic_context(), tag_kind, tag_name, tag_len,
      out, out_ordinal);
}

static int test_tag_next_named_member(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int *ordinal_inout, tag_member_info_t *out) {
  return ps_tag_next_named_member_in(
      test_semantic_context(), tag_kind, tag_name, tag_len,
      ordinal_inout, out);
}

static int test_tag_member_at_flat_slot(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int flat_slot, tag_member_info_t *out, int *out_ordinal) {
  return ps_tag_member_at_flat_slot_in(
      test_semantic_context(), tag_kind, tag_name, tag_len,
      flat_slot, out, out_ordinal);
}

static void test_tag_flat_cover_state_note(
    psx_tag_flat_cover_state_t *state,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const tag_member_info_t *member) {
  ps_tag_flat_cover_state_note_in(
      test_semantic_context(), state, tag_kind, tag_name, tag_len, member);
}

static int test_tag_member_designator_slot(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    char *member_name, int member_len, int *out_ordinal) {
  return ps_tag_member_designator_slot_in(
      test_semantic_context(), tag_kind, tag_name, tag_len,
      member_name, member_len, out_ordinal);
}

static int test_gvar_walk_aggregate_initializer(
    global_var_t *global, long long base_offset,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user) {
  return ps_gvar_walk_aggregate_initializer_in(
      test_semantic_context(), global, base_offset, ops, user);
}

static void reset_test_locals(void) {
  ps_decl_reset_locals_in(
      ag_compilation_session_local_registry(test_suite_session));
  local_storage_reset(test_lowering_context());
}

static void set_test_current_funcname(char *name, int len) {
  ps_decl_set_current_funcname_in(
      ag_compilation_session_local_registry(test_suite_session), name, len);
}

static void reset_test_translation_unit_state(void) {
  ASSERT_TRUE(psx_frontend_reset_translation_unit_state_in_session(
      test_suite_session));
}

static node_t **parse_test_program_from(token_t *start) {
  return psx_frontend_program_in_session(
      test_suite_session, NULL, start);
}

static node_t *parse_test_expression_from(token_t *start) {
  return ps_expr_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      NULL, NULL, start);
}

static void begin_test_parser_stream(
    psx_parser_stream_t *stream,
    tokenizer_context_t *tokenizer_context, token_t *start,
    const psx_toplevel_declaration_callbacks_t *toplevel_declarations) {
  ps_parser_stream_begin_in_contexts(
      stream,
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      tokenizer_context, start, toplevel_declarations);
}

static void parse_test_aggregate_body(psx_parsed_aggregate_body_t *body) {
  psx_parse_aggregate_body_with_options(
      body,
      &(psx_decl_specifier_syntax_options_t){
          .semantic_context =
              ag_compilation_session_semantic_context(test_suite_session),
          .global_registry =
              ag_compilation_session_global_registry(test_suite_session),
          .local_registry =
              ag_compilation_session_local_registry(test_suite_session),
          .runtime_context =
              ag_compilation_session_parser_runtime_context(test_suite_session),
      });
}

static int parse_test_toplevel_declaration_syntax(
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_toplevel_declaration_callbacks_t *callbacks) {
  return psx_parse_toplevel_declaration_syntax_in_contexts(
      declaration, callbacks,
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session));
}

static void parse_test_decl_specifier_syntax(
    psx_parsed_decl_specifier_t *specifier) {
  psx_parse_decl_specifier_syntax_ex(
      specifier,
      &(psx_decl_specifier_syntax_options_t){
          .semantic_context =
              ag_compilation_session_semantic_context(test_suite_session),
          .global_registry =
              ag_compilation_session_global_registry(test_suite_session),
          .local_registry =
              ag_compilation_session_local_registry(test_suite_session),
          .runtime_context =
              ag_compilation_session_parser_runtime_context(test_suite_session),
      });
}

static int parse_test_type_name_syntax_at(
    token_t *start, psx_parsed_type_name_t *type_name) {
  return psx_parse_type_name_syntax_at(
      start,
      &(psx_decl_specifier_syntax_options_t){
          .semantic_context =
              ag_compilation_session_semantic_context(test_suite_session),
          .global_registry =
              ag_compilation_session_global_registry(test_suite_session),
          .local_registry =
              ag_compilation_session_local_registry(test_suite_session),
          .runtime_context =
              ag_compilation_session_parser_runtime_context(test_suite_session),
      },
      type_name);
}

static psx_parsed_declarator_t parse_test_declarator_syntax_tree(void) {
  psx_parsed_declarator_t declarator;
  psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts(
      &declarator,
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      NULL, NULL);
  return declarator;
}

static void parse_test_runtime_declarator_expressions(
    psx_parsed_declarator_t *declarator) {
  ps_parse_runtime_declarator_expressions_in_contexts(
      declarator,
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      NULL);
}

static void prepare_test_constant_declarator_expressions(
    psx_parsed_declarator_t *declarator) {
  ps_prepare_constant_declarator_expressions_in_context(
      declarator,
      ag_compilation_session_semantic_context(test_suite_session));
}

static void prepare_test_decl_specifier_alignments(
    psx_parsed_decl_specifier_t *specifier) {
  ps_prepare_decl_specifier_alignments_in_context(
      specifier,
      ag_compilation_session_semantic_context(test_suite_session));
}

static node_t *analyze_test_expression(
    node_t *expression, const token_t *fallback_diag_tok) {
  return psx_frontend_analyze_expression_in_session(
      test_suite_session, expression, fallback_diag_tok);
}

static void analyze_test_function(
    node_t *function, const token_t *fallback_diag_tok) {
  psx_frontend_analyze_function_in_session(
      test_suite_session, function, fallback_diag_tok);
}

static node_t *bind_test_identifier_tree(
    node_t *node, const token_t *fallback_diag_tok) {
  return psx_bind_identifier_tree_in_session(
      test_suite_session, node, fallback_diag_tok);
}

static node_function_definition_t *apply_test_function_definition_header(
    psx_parsed_function_definition_t *definition) {
  return psx_apply_function_definition_header_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      test_lowering_context(),
      definition);
}

static void apply_test_toplevel_declaration(
    psx_parsed_toplevel_declaration_t *declaration) {
  psx_apply_toplevel_declaration_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      test_lowering_context(),
      ag_compilation_session_options_view(test_suite_session),
      declaration);
}

static void init_test_local_declaration_callbacks(
    psx_local_declaration_callbacks_t *callbacks) {
  psx_frontend_init_local_declaration_callbacks_in_contexts(
      callbacks,
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      test_lowering_context(),
      ag_compilation_session_options_view(test_suite_session));
}

static void init_test_toplevel_declaration_callbacks(
    psx_toplevel_declaration_callbacks_t *callbacks) {
  psx_frontend_init_toplevel_declaration_callbacks_in_contexts(
      callbacks,
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      test_lowering_context(),
      ag_compilation_session_options_view(test_suite_session));
}

static const psx_type_t *resolve_test_decl_specifier_syntax(
    const psx_parsed_decl_specifier_t *specifier) {
  return psx_resolve_decl_specifier_syntax_in_context(
      ag_compilation_session_semantic_context(test_suite_session),
      specifier);
}

static void resolve_test_generic_selection(
    node_generic_selection_t *selection,
    psx_generic_selection_resolution_t *resolution) {
  psx_resolve_generic_selection_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      selection, resolution);
}

static void resolve_test_sizeof_query(
    node_sizeof_query_t *query,
    psx_sizeof_query_resolution_t *resolution) {
  psx_resolve_sizeof_query_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      query, resolution);
}

static void resolve_test_alignof_query(node_alignof_query_t *query) {
  psx_resolve_alignof_query_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session), query);
}

static int apply_test_declaration_phase(
    psx_declaration_phase_t *phase, int standalone_tag) {
  return psx_apply_declaration_phase_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      phase, standalone_tag);
}

static int apply_test_parsed_aggregate_body_layout(
    psx_parsed_aggregate_body_t *body,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int *out_size, int *out_align) {
  return psx_apply_parsed_aggregate_body_layout_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      body, tag_kind, tag_name, tag_len, out_size, out_align);
}

static const psx_type_t *apply_test_parsed_type_name(
    const psx_parsed_type_name_t *type_name) {
  return psx_apply_parsed_type_name_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session), type_name);
}

static void apply_test_runtime_parsed_declarator(
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application) {
  psx_apply_runtime_parsed_declarator_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      declarator, application);
}

static void apply_test_parsed_declarator(
    const psx_parsed_declarator_t *declarator,
    psx_declarator_shape_t *shape, int *bit_width) {
  psx_apply_parsed_declarator_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      declarator, shape, bit_width);
}

static void parse_test_initializer_syntax_value(
    psx_parsed_initializer_t *initializer, token_t *assign_tok) {
  psx_parse_initializer_syntax_value_in_contexts(
      initializer, assign_tok,
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      NULL);
}

static node_t *parse_test_initializer_for_var(lvar_t *var) {
  return psx_decl_parse_initializer_for_var_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      NULL, var);
}

/* Test-only storage fixtures may start with a simple scalar/array type and
 * replace it with the exact type under test. Production registration accepts
 * only an explicit canonical type. */
static lvar_t *register_test_typed_storage_fixture(
    char *name, int len, int size, int align, const psx_type_t *type) {
  int offset = local_storage_allocate(
      test_lowering_context(), size, align);
  return ps_local_registry_create_storage_object_in(
      ag_compilation_session_local_registry(test_suite_session),
      name, len, offset, size, align, type, NULL);
}

static lvar_t *register_test_storage_fixture(
    char *name, int len, int size, int elem_size, int is_array) {
  int scalar_size = elem_size > 0 ? elem_size : size;
  if (scalar_size <= 0) scalar_size = 1;
  psx_type_t *type = ps_type_new_integer(
      scalar_size > 4 ? TK_LONG : TK_INT, scalar_size, 0);
  if (is_array) {
    int array_len = size > 0 && size % scalar_size == 0
                        ? size / scalar_size
                        : 0;
    type = ps_type_new_array(type, array_len, size, 0);
  }
  return register_test_typed_storage_fixture(
      name, len, size, 0, type);
}

static lvar_t *register_test_default_storage_fixture(char *name, int len) {
  return register_test_storage_fixture(name, len, 8, 8, 0);
}

static void set_test_storage_fixture_type(
    lvar_t *var, const psx_type_t *type) {
  ASSERT_TRUE(var != NULL);
  ASSERT_TRUE(type != NULL);
  var->decl_type = ps_type_clone_persistent(type);
  ASSERT_TRUE(var->decl_type != NULL);
}

static void find_long_double_float_literal(float_lit_t *lit, void *user) {
  bool *found = user;
  if (lit->float_suffix_kind == TK_FLOAT_SUFFIX_L) *found = true;
}

/* parse_expr_input は単体式パースのため、main 関数の宣言ブロックを通らない。
 * ag_c は未宣言識別子をエラー扱いするので、テストで多用する短い名前を
 * あらかじめローカル変数として登録しておく。 */
static void preregister_test_locals(void) {
  static char names[] = "abcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 26; i++) {
    register_test_default_storage_fixture(&names[i], 1);
  }
}

static void assert_semantic_tree_invariants(node_t *root) {
  psx_semantic_invariant_failure_t failure;
  if (psx_semantic_tree_has_canonical_expression_types(root, &failure)) return;
  fprintf(stderr, "semantic invariant failed: status=%d kind=%d\n",
          failure.status, failure.node ? failure.node->kind : -1);
  exit(1);
}

static node_t *parse_expr_input(const char *input) {
  reset_test_locals();
  preregister_test_locals();
  /* 単体式パースは関数本体内のコードを模す (ローカルを登録するのと同様)。
   * 複合リテラル `(int){3}` をローカル実体化経路 (ND_COMMA) で扱わせるため、
   * 現在関数名を非 NULL にしておく (本物のパースでは関数定義時に設定される)。 */
  set_test_current_funcname((char *)"__test__", 8);
  token_t *head = tk_tokenize((char *)input);
  node_t *expr = parse_test_expression_from(head);
  node_t *analyzed = analyze_test_expression(expr, head);
  assert_semantic_tree_invariants(analyzed);
  return analyzed;
}

static node_t *parse_expr_input_with_existing_locals(const char *input) {
  set_test_current_funcname((char *)"__test__", 8);
  token_t *head = tk_tokenize((char *)input);
  return parse_test_expression_from(head);
}

static node_t *parse_analyzed_expr_input_with_existing_locals(
    const char *input) {
  node_t *expr = parse_expr_input_with_existing_locals(input);
  node_t *analyzed =
      analyze_test_expression(expr, expr ? expr->tok : NULL);
  assert_semantic_tree_invariants(analyzed);
  return analyzed;
}

static node_t **parse_program_input(const char *input) {
  token_t *head = tk_tokenize((char *)input);
  node_t **program = parse_test_program_from(head);
  for (int i = 0; program && program[i]; i++)
    assert_semantic_tree_invariants(program[i]);
  return program;
}

static const psx_type_t *resolve_tag_base_for_test(
    token_kind_t kind, char *name, int name_len) {
  psx_parsed_decl_specifier_t specifier = {
      .source = PSX_PARSED_DECL_TYPE_TAG,
      .tag_action = {
          .kind = kind,
          .name = name,
          .name_len = name_len,
      },
  };
  return resolve_test_decl_specifier_syntax(&specifier);
}

static node_t *parse_raw_function_item(
    psx_parser_stream_t *stream, psx_parsed_toplevel_item_t *item) {
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item->kind);
  node_function_definition_t *header =
      apply_test_function_definition_header(
      &item->value.function_header);
  psx_local_declaration_callbacks_t local_declarations;
  init_test_local_declaration_callbacks(&local_declarations);
  node_t *function = ps_parse_function_definition_body(
      stream, header, &local_declarations);
  ps_dispose_function_definition_header_syntax(
      &item->value.function_header);
  return function;
}

static node_num_t *as_num(node_t *n) { return (node_num_t *)n; }
static node_lvar_t *as_lvar(node_t *n) { return (node_lvar_t *)n; }
static node_function_definition_t *as_function_definition(node_t *n) {
  ASSERT_TRUE(n != NULL);
  ASSERT_EQ(ND_FUNCDEF, n->kind);
  return (node_function_definition_t *)n;
}
static node_function_call_t *as_function_call(node_t *n) {
  ASSERT_TRUE(n != NULL);
  ASSERT_EQ(ND_FUNCALL, n->kind);
  return (node_function_call_t *)n;
}
static node_block_t *as_block(node_t *n) { return (node_block_t *)n; }
static node_ctrl_t *as_ctrl(node_t *n) { return (node_ctrl_t *)n; }
static node_string_t *as_string(node_t *n) { return (node_string_t *)n; }
static node_case_t *as_case(node_t *n) { return (node_case_t *)n; }

static int test_type_sizeof(const psx_type_t *type) {
  ag_target_info_t target = ag_target_info_host();
  return ps_type_sizeof_for_target(type, &target);
}

static int test_type_layout_of(
    const psx_type_t *type, const ag_target_info_t *target,
    psx_type_layout_t *out) {
  psx_semantic_type_table_t *types = psx_semantic_type_table_create();
  if (!types) return 0;
  psx_qual_type_t identity = psx_semantic_type_table_intern(types, type);
  int resolved = identity.type_id != PSX_TYPE_ID_INVALID &&
                 ps_type_layout_of_id(
                     types, identity.type_id, target, out);
  psx_semantic_type_table_destroy(types);
  return resolved;
}

static int test_type_sizeof_for_target(
    const psx_type_t *type, const ag_target_info_t *target) {
  psx_type_layout_t layout = {0};
  return test_type_layout_of(type, target, &layout) && layout.is_complete
             ? layout.size
             : 0;
}

static int test_type_alignof_for_target(
    const psx_type_t *type, const ag_target_info_t *target) {
  psx_type_layout_t layout = {0};
  return test_type_layout_of(type, target, &layout)
             ? layout.alignment
             : 0;
}

static int test_type_format_canonical_signature(
    const psx_type_t *type, char *out, size_t out_size) {
  return ps_type_format_canonical_signature_for_target(
      type, ps_ctx_target_info(test_semantic_context()), out, out_size);
}

static int test_type_deref_size(const psx_type_t *type) {
  if (!type ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return 0;
  }
  if (type->base && type->base->kind == PSX_TYPE_ARRAY &&
      ps_type_is_tag_aggregate(ps_type_array_leaf_type(type->base))) {
    return 0;
  }
  return test_type_sizeof(type->base);
}

static int test_type_pointee_value_size(const psx_type_t *type) {
  return test_type_sizeof(ps_type_pointee_value_type(type));
}

static int test_type_array_scalar_element_size(const psx_type_t *type) {
  if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  return test_type_sizeof(ps_type_array_leaf_type(type));
}

static int test_type_array_subscript_stride_bytes(
    const psx_type_t *type, int depth) {
  if (!type || type->kind != PSX_TYPE_ARRAY || depth < 0) return 0;
  while (depth-- > 0) {
    type = type->base;
    if (!type || type->kind != PSX_TYPE_ARRAY) return 0;
  }
  return test_type_sizeof(type->base);
}

static int test_type_subscript_static_stride(const psx_type_t *type) {
  if (!type || !type->base ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return 0;
  }
  return test_type_sizeof(type->base);
}

static int test_type_pointer_view_structural_base_deref_size(
    const psx_type_t *type) {
  if (!type ||
      (type->kind != PSX_TYPE_POINTER && type->kind != PSX_TYPE_ARRAY)) {
    return 0;
  }
  const psx_type_t *leaf = ps_type_derived_leaf_type(type);
  int size = leaf ? test_type_sizeof(leaf) : 0;
  return size > 0 ? size : 0;
}

static int test_type_pointer_view_structural_ptr_array_pointee_bytes(
    const psx_type_t *type) {
  while (type && type->kind == PSX_TYPE_ARRAY) type = type->base;
  if (!type || type->kind != PSX_TYPE_POINTER || !type->base ||
      type->base->kind != PSX_TYPE_ARRAY ||
      ps_type_is_tag_aggregate(ps_type_array_leaf_type(type->base))) {
    return 0;
  }
  int bytes = test_type_sizeof(type->base);
  if (bytes <= 0) bytes = test_type_deref_size(type);
  return bytes > 0 ? bytes : 0;
}

static int test_node_usual_arith_is_unsigned(node_t *node) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITXOR:
    case ND_BITOR:
    case ND_LT:
    case ND_LE:
    case ND_EQ:
    case ND_NE:
      return ps_type_usual_arithmetic_result_is_unsigned_for_target(
          ps_node_get_type(node->lhs), ps_node_get_type(node->rhs),
          ps_ctx_target_info(test_semantic_context()));
    default:
      return ps_type_is_unsigned(ps_node_get_type(node));
  }
}

static const psx_type_t *canonical_node_pointee_type(node_t *node) {
  return node ? ps_type_pointee_value_type(ps_node_get_type(node)) : NULL;
}

static int canonical_pointer_qual_levels(const psx_type_t *type) {
  return type && type->kind == PSX_TYPE_POINTER && type->base
             ? ps_type_pointer_depth(type)
             : 0;
}

static int canonical_node_pointer_qual_levels(node_t *node) {
  return node ? canonical_pointer_qual_levels(ps_node_get_type(node)) : 0;
}

static int canonical_node_base_deref_size(node_t *node) {
  return node ? ps_type_pointer_view_structural_base_deref_size(
                    ps_node_get_type(node))
              : 0;
}

static int canonical_node_ptr_array_pointee_bytes(node_t *node) {
  return node ? ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    ps_node_get_type(node))
              : 0;
}

static const psx_type_t *canonical_pointer_level(
    const psx_type_t *type, int level) {
  if (level < 0) return NULL;
  while (type && type->kind == PSX_TYPE_POINTER && level > 0) {
    type = type->base;
    level--;
  }
  return type && type->kind == PSX_TYPE_POINTER && level == 0
             ? type
             : NULL;
}

static void assert_pointer_qualifiers(
    const psx_type_t *type, const char *const_levels,
    const char *volatile_levels) {
  ASSERT_TRUE(const_levels != NULL);
  ASSERT_TRUE(volatile_levels != NULL);
  int levels = (int)strlen(const_levels);
  ASSERT_EQ(levels, (int)strlen(volatile_levels));
  ASSERT_EQ(levels, ps_type_pointer_depth(type));
  for (int i = 0; i < levels; i++) {
    const psx_type_t *pointer = canonical_pointer_level(type, i);
    ASSERT_TRUE(pointer != NULL);
    ASSERT_EQ(const_levels[i] == '1', ps_type_has_qualifier(pointer, PSX_TYPE_QUALIFIER_CONST));
    ASSERT_EQ(volatile_levels[i] == '1',
              ps_type_has_qualifier(pointer, PSX_TYPE_QUALIFIER_VOLATILE));
  }
}

static void assert_node_pointer_qualifiers(
    node_t *node, const char *const_levels,
    const char *volatile_levels) {
  assert_pointer_qualifiers(
      node ? ps_node_get_type(node) : NULL,
      const_levels, volatile_levels);
}

static int canonical_node_pointee_is_unsigned(node_t *node) {
  return ps_type_is_unsigned(canonical_node_pointee_type(node));
}

static int canonical_node_pointee_is_bool(node_t *node) {
  const psx_type_t *type = canonical_node_pointee_type(node);
  return type && type->kind == PSX_TYPE_BOOL;
}

static int canonical_node_pointee_is_void(node_t *node) {
  const psx_type_t *type = canonical_node_pointee_type(node);
  return type && type->kind == PSX_TYPE_VOID;
}

static int canonical_node_pointee_is_const_qualified(node_t *node) {
  const psx_type_t *type = canonical_node_pointee_type(node);
  return type && ps_type_has_qualifier(type, PSX_TYPE_QUALIFIER_CONST);
}

static int canonical_node_pointee_is_volatile_qualified(node_t *node) {
  const psx_type_t *type = canonical_node_pointee_type(node);
  return type && ps_type_has_qualifier(type, PSX_TYPE_QUALIFIER_VOLATILE);
}

static tk_float_kind_t canonical_node_pointee_fp_kind(node_t *node) {
  const psx_type_t *type = canonical_node_pointee_type(node);
  return type && (type->kind == PSX_TYPE_FLOAT ||
                  type->kind == PSX_TYPE_COMPLEX)
             ? type->fp_kind
             : TK_FLOAT_KIND_NONE;
}

static int canonical_node_array_subscript_stride_bytes(node_t *node,
                                                       int depth) {
  const psx_type_t *type = node ? ps_node_get_type(node) : NULL;
  while (type && type->kind == PSX_TYPE_POINTER) type = type->base;
  return ps_type_array_subscript_stride_bytes(type, depth);
}

static int canonical_lvar_pointer_qual_levels(lvar_t *var) {
  return var ? canonical_pointer_qual_levels(ps_lvar_get_decl_type(var)) : 0;
}

static psx_type_t *test_function_pointer(
    psx_type_t *return_type, const psx_type_t *const *param_types,
    int param_count, int is_variadic) {
  psx_type_t *function = ps_type_new_function(return_type);
  ps_type_set_function_params(
      function, param_types, param_count, is_variadic);
  return ps_type_new_pointer(function);
}

static void assert_canonical_type_signature(const psx_type_t *type,
                                            const char *expected) {
  char actual[512];
  int len = ps_type_format_canonical_signature(type, actual, sizeof(actual));
  ASSERT_TRUE(len >= 0);
  ASSERT_TRUE((size_t)len < sizeof(actual));
  if (strcmp(expected, actual) != 0) {
    fprintf(stderr, "canonical type mismatch: expected %s, got %s\n",
            expected, actual);
    exit(1);
  }
}

static lvar_t *find_func_lvar(
    node_function_definition_t *fn, const char *name) {
  int len = (int)strlen(name);
  for (lvar_t *v = fn ? fn->lvars : NULL; v; v = v->next_all) {
    if (v->len == len && strncmp(v->name, name, (size_t)len) == 0) return v;
  }
  return NULL;
}

static void test_local_declaration_frontend_boundary() {
  printf("test_local_declaration_frontend_boundary...\n");
  parsed_code = parse_program_input(
      "int main(void) { int block_fn(int); "
      "int self = sizeof self, a = 2, b = a + 3; "
      "typedef int T, U; T value = b; U other = 1; "
      "static int s = 4, t = 5; "
      "return block_fn(value) + self + other + s + t; }");
  node_function_definition_t *main_function =
      as_function_definition(parsed_code[0]);
  ASSERT_TRUE(find_func_lvar(main_function, "self") != NULL);
  ASSERT_TRUE(find_func_lvar(main_function, "b") != NULL);
  ASSERT_TRUE(find_func_lvar(main_function, "value") != NULL);
  ASSERT_TRUE(find_func_lvar(main_function, "s") != NULL);
  ASSERT_TRUE(ps_ctx_get_function_type_in(test_semantic_context(), (char *)"block_fn", 8) != NULL);
}

static void test_function_parameter_point_of_declaration_boundary() {
  printf("test_function_parameter_point_of_declaration_boundary...\n");
  reset_test_translation_unit_state();
  parsed_code = parse_program_input(
      "int __parameter_order(int m, int k, int t[][m][3][k]); "
      "int __parameter_order(int m, int k, int t[][m][3][k]) { "
      "return t[0][0][0][0]; }");
  node_function_definition_t *function =
      as_function_definition(parsed_code[0]);
  lvar_t *m = find_func_lvar(function, "m");
  lvar_t *k = find_func_lvar(function, "k");
  lvar_t *t = find_func_lvar(function, "t");
  ASSERT_TRUE(m != NULL);
  ASSERT_TRUE(k != NULL);
  ASSERT_TRUE(t != NULL);
  ASSERT_TRUE(t->decl_type != NULL);
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_count(t));
  ASSERT_EQ(m->offset,
            ps_lvar_vla_param_inner_dim_src_offset(t, 0));
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_const(t, 1));
  ASSERT_EQ(k->offset,
            ps_lvar_vla_param_inner_dim_src_offset(t, 2));

  parsed_code = parse_program_input(
      "int __deep_parameter_vla(int n, "
      "int t[][n][40000][n][n][n][n][n][n][n][n]) { return 0; }");
  function = as_function_definition(parsed_code[0]);
  lvar_t *n = find_func_lvar(function, "n");
  t = find_func_lvar(function, "t");
  ASSERT_TRUE(n != NULL);
  ASSERT_TRUE(t != NULL);
  ASSERT_EQ(10, ps_lvar_vla_param_inner_dim_count(t));
  ASSERT_EQ(n->offset,
            ps_lvar_vla_param_inner_dim_src_offset(t, 0));
  ASSERT_EQ(40000, ps_lvar_vla_param_inner_dim_const(t, 1));
  ASSERT_EQ(n->offset,
            ps_lvar_vla_param_inner_dim_src_offset(t, 9));
  ASSERT_EQ(0, ps_lvar_vla_param_inner_dim_const(t, 10));
}

static void assert_identifier_resolution_kind(
    char *name, int name_len, int is_call,
    psx_identifier_resolution_kind_t expected) {
  psx_identifier_resolution_t resolution;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = name,
          .name_len = name_len,
          .is_call = is_call,
      },
      &resolution);
  ASSERT_EQ(expected, resolution.kind);
}

static void test_identifier_resolution_boundary() {
  printf("test_identifier_resolution_boundary...\n");
  reset_test_translation_unit_state();
  parsed_code = parse_program_input(
      "enum __IdentifierEnum { __identifier_enum = 7 }; "
      "int __identifier_global; "
      "int __identifier_function(int value);");
  lvar_t *local = register_test_default_storage_fixture(
      (char *)"__identifier_local", 18);
  ASSERT_TRUE(local != NULL);

  assert_identifier_resolution_kind(
      (char *)"__identifier_local", 18, 0, PSX_IDENTIFIER_LOCAL);
  assert_identifier_resolution_kind(
      (char *)"__identifier_enum", 17, 0,
      PSX_IDENTIFIER_ENUM_CONSTANT);
  assert_identifier_resolution_kind(
      (char *)"__identifier_global", 19, 0,
      PSX_IDENTIFIER_GLOBAL_OBJECT);
  assert_identifier_resolution_kind(
      (char *)"__identifier_global", 19, 1,
      PSX_IDENTIFIER_GLOBAL_OBJECT);
  assert_identifier_resolution_kind(
      (char *)"__identifier_function", 21, 0,
      PSX_IDENTIFIER_FUNCTION);
  psx_identifier_resolution_t function_call;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = (char *)"__identifier_function",
          .name_len = 21,
          .is_call = 1,
      },
      &function_call);
  ASSERT_EQ(PSX_IDENTIFIER_FUNCTION, function_call.kind);
  ASSERT_TRUE(function_call.function != NULL);
  ASSERT_TRUE(function_call.function ==
              ps_ctx_find_function_symbol_in(test_semantic_context(),
                  (char *)"__identifier_function", 21));
  const psx_type_t *resolved_function_type =
      ps_function_symbol_type(function_call.function);
  ASSERT_TRUE(resolved_function_type != NULL);
  ASSERT_TRUE(resolved_function_type ==
              ps_ctx_get_function_type_in(test_semantic_context(),
                  (char *)"__identifier_function", 21));
  ASSERT_EQ(PSX_TYPE_FUNCTION, resolved_function_type->kind);
  ASSERT_EQ(1, resolved_function_type->param_count);
  ASSERT_TRUE(!resolved_function_type->is_variadic_function);
  assert_identifier_resolution_kind(
      (char *)"__identifier_missing", 20, 0,
      PSX_IDENTIFIER_UNDEFINED);
  assert_identifier_resolution_kind(
      (char *)"__identifier_missing", 20, 1,
      PSX_IDENTIFIER_UNDECLARED_CALL);
}

static void test_persistent_local_scope_lookup_boundary() {
  printf("test_persistent_local_scope_lookup_boundary...\n");
  reset_test_locals();

  lvar_t *outer = register_test_storage_fixture(
      (char *)"__scope_value", 13, 4, 4, 0);
  ASSERT_TRUE(outer != NULL);

  ps_decl_enter_scope_in(test_local_registry());
  psx_local_lookup_point_t before_inner =
      ps_local_registry_capture_lookup_point_in(test_local_registry());
  lvar_t *inner = register_test_storage_fixture(
      (char *)"__scope_value", 13, 4, 4, 0);
  ASSERT_TRUE(inner != NULL);
  psx_local_lookup_point_t after_inner =
      ps_local_registry_capture_lookup_point_in(test_local_registry());
  ASSERT_TRUE(ps_local_registry_find_visible_in(test_local_registry(),
                  (char *)"__scope_value", 13, before_inner) == outer);
  ASSERT_TRUE(ps_local_registry_find_visible_in(test_local_registry(),
                  (char *)"__scope_value", 13, after_inner) == inner);

  ps_decl_enter_scope_in(test_local_registry());
  psx_local_lookup_point_t nested =
      ps_local_registry_capture_lookup_point_in(test_local_registry());
  ASSERT_TRUE(ps_local_registry_find_visible_in(test_local_registry(),
                  (char *)"__scope_value", 13, nested) == inner);
  ps_decl_leave_scope_in(test_local_registry());
  ps_decl_leave_scope_in(test_local_registry());

  psx_identifier_resolution_t delayed_resolution;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = (char *)"__scope_value",
          .name_len = 13,
          .has_local_lookup_point = 1,
          .local_lookup_point = before_inner,
      },
      &delayed_resolution);
  ASSERT_EQ(PSX_IDENTIFIER_LOCAL, delayed_resolution.kind);
  ASSERT_TRUE(delayed_resolution.local == outer);
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = (char *)"__scope_value",
          .name_len = 13,
          .has_local_lookup_point = 1,
          .local_lookup_point = after_inner,
      },
      &delayed_resolution);
  ASSERT_EQ(PSX_IDENTIFIER_LOCAL, delayed_resolution.kind);
  ASSERT_TRUE(delayed_resolution.local == inner);

  ps_decl_enter_scope_in(test_local_registry());
  psx_local_lookup_point_t sibling_before_decl =
      ps_local_registry_capture_lookup_point_in(test_local_registry());
  lvar_t *sibling = register_test_storage_fixture(
      (char *)"__sibling_only", 14, 4, 4, 0);
  ASSERT_TRUE(sibling != NULL);
  ASSERT_TRUE(ps_local_registry_find_visible_in(test_local_registry(),
                  (char *)"__sibling_only", 14,
                  sibling_before_decl) == NULL);
  ASSERT_TRUE(ps_local_registry_find_visible_in(test_local_registry(),
                  (char *)"__scope_value", 13,
                  sibling_before_decl) == outer);
  ps_decl_leave_scope_in(test_local_registry());

  ps_decl_enter_scope_in(test_local_registry());
  psx_local_lookup_point_t other_sibling =
      ps_local_registry_capture_lookup_point_in(test_local_registry());
  ASSERT_TRUE(ps_local_registry_find_visible_in(test_local_registry(),
                  (char *)"__sibling_only", 14,
                  other_sibling) == NULL);
  ASSERT_TRUE(ps_local_registry_find_visible_in(test_local_registry(),
                  (char *)"__scope_value", 13,
                  other_sibling) == outer);
  ps_decl_leave_scope_in(test_local_registry());

  ps_ctx_enter_block_scope_in(test_semantic_context());
  ps_decl_enter_scope_in(test_local_registry());
  psx_local_lookup_point_t before_enum =
      ps_local_registry_capture_lookup_point_in(test_local_registry());
  ASSERT_TRUE(test_semantic_define_enum_const(
      (char *)"__scoped_enum", 13, 29));
  psx_local_lookup_point_t after_enum =
      ps_local_registry_capture_lookup_point_in(test_local_registry());
  ps_decl_leave_scope_in(test_local_registry());
  ps_ctx_leave_block_scope_in(test_semantic_context());
  long long enum_value = 0;
  ASSERT_TRUE(!ps_ctx_find_enum_const_at_in_contexts(test_semantic_context(), test_local_registry(),
      (char *)"__scoped_enum", 13, before_enum, &enum_value));
  ASSERT_TRUE(ps_ctx_find_enum_const_at_in_contexts(test_semantic_context(), test_local_registry(),
      (char *)"__scoped_enum", 13, after_enum, &enum_value));
  ASSERT_EQ(29, enum_value);

  ps_ctx_enter_block_scope_in(test_semantic_context());
  ps_decl_enter_scope_in(test_local_registry());
  psx_local_lookup_point_t enum_sibling =
      ps_local_registry_capture_lookup_point_in(test_local_registry());
  ASSERT_TRUE(!ps_ctx_find_enum_const_at_in_contexts(test_semantic_context(), test_local_registry(),
      (char *)"__scoped_enum", 13, enum_sibling, &enum_value));
  ps_decl_leave_scope_in(test_local_registry());
  ps_ctx_leave_block_scope_in(test_semantic_context());
}

static void test_member_access_resolution_boundary() {
  printf("test_member_access_resolution_boundary...\n");
  reset_test_translation_unit_state();
  parsed_code = parse_program_input(
      "struct __MemberBoundary { char prefix; int value; }; "
      "int __member_boundary_function(void) { "
      "struct __MemberBoundary object; int *pointer; return 0; }");
  lvar_t *object = find_func_lvar(as_function_definition(parsed_code[0]), "object");
  lvar_t *pointer = find_func_lvar(as_function_definition(parsed_code[0]), "pointer");
  ASSERT_TRUE(object != NULL);
  ASSERT_TRUE(pointer != NULL);
  node_t *base = psx_node_new_lvar_identifier_ref_for(object);
  psx_record_decl_t *member_record = test_record_decl_mut(
      ps_type_find_aggregate_object_type(ps_node_get_type(base)));
  ASSERT_TRUE(member_record != NULL);
  ASSERT_EQ(2, member_record->member_count);
  ((tag_member_info_t *)member_record->members)[1].offset = 77;
  psx_member_access_resolution_t resolution;
  psx_resolve_member_access(
      &(psx_member_access_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .base = base,
          .member_name = (char *)"value",
          .member_name_len = 5,
      },
      &resolution);
  ASSERT_EQ(PSX_MEMBER_ACCESS_OK, resolution.status);
  ASSERT_EQ(1, resolution.member_index);
  ASSERT_EQ(member_record->record_id, resolution.record_id);
  ASSERT_EQ(77, resolution.member.offset);
  ASSERT_EQ(4, test_tag_member_decl_value_size(&resolution.member));
  ASSERT_TRUE(resolution.base_object_type == ps_node_get_type(base));
  ASSERT_TRUE(ps_type_is_tag_aggregate(resolution.base_object_type));

  psx_resolve_member_access(
      &(psx_member_access_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .base = base,
          .member_name = (char *)"value",
          .member_name_len = 5,
          .from_pointer = 1,
      },
      &resolution);
  ASSERT_EQ(PSX_MEMBER_ACCESS_INVALID_BASE, resolution.status);
  psx_resolve_member_access(
      &(psx_member_access_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .base = base,
          .member_name = (char *)"missing",
          .member_name_len = 7,
      },
      &resolution);
  ASSERT_EQ(PSX_MEMBER_ACCESS_NOT_FOUND, resolution.status);

  psx_type_t *object_type = ps_type_clone(ps_lvar_get_decl_type(object));
  reset_test_locals();
  lvar_t *raw_object = register_test_storage_fixture(
      (char *)"object", 6, 8, 4, 0);
  set_test_storage_fixture_type(raw_object, object_type);
  node_t *raw_access = parse_expr_input_with_existing_locals(
      "object.value");
  ASSERT_EQ(ND_MEMBER_ACCESS, raw_access->kind);
  node_member_access_t *member_syntax =
      (node_member_access_t *)raw_access;
  ASSERT_TRUE(!member_syntax->from_pointer);
  ASSERT_EQ(5, member_syntax->member_name_len);
  ASSERT_TRUE(strncmp(member_syntax->member_name, "value", 5) == 0);
  ASSERT_TRUE(member_syntax->resolved_member == NULL);
  ASSERT_TRUE(raw_access->type == NULL);
  node_t *lowered_access = analyze_test_expression(
      raw_access, raw_access->tok);
  ASSERT_EQ(ND_DEREF, lowered_access->kind);
  ASSERT_TRUE(ps_type_is_tag_aggregate(
      ps_type_find_aggregate_object_type(
          ps_node_get_type(member_syntax->base.lhs))));
  ASSERT_TRUE(lowered_access->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, lowered_access->type->kind);
  ASSERT_EQ(4, ps_type_sizeof(lowered_access->type));
  ASSERT_EQ(ND_ADD, lowered_access->lhs->kind);
  ASSERT_EQ(4, as_num(lowered_access->lhs->rhs)->val);

  node_t *pointer_node = psx_node_new_lvar_identifier_ref_for(pointer);
  ASSERT_EQ(PSX_DEREF_OPERAND_OK,
            psx_resolve_deref_operand(pointer_node));
  ASSERT_EQ(PSX_DEREF_OPERAND_NOT_POINTER,
            psx_resolve_deref_operand(ps_node_new_num(3)));
  psx_subscript_operands_resolution_t subscript;
  psx_resolve_subscript_operands(
      ps_node_new_num(3), pointer_node, &subscript);
  ASSERT_EQ(PSX_SUBSCRIPT_OPERANDS_OK, subscript.status);
  ASSERT_TRUE(subscript.swapped);
  ASSERT_EQ(pointer_node, subscript.base);
  psx_resolve_subscript_operands(
      ps_node_new_num(3), ps_node_new_num(4), &subscript);
  ASSERT_EQ(PSX_SUBSCRIPT_OPERANDS_INVALID, subscript.status);
}

typedef struct {
  global_var_t *gv;
  int scalar_count;
  long long scalar_offsets[16];
  long long scalar_values[16];
  int scalar_sizes[16];
  psx_type_id_t scalar_type_ids[16];
  int padding_count;
  long long padding_offsets[16];
  int padding_sizes[16];
} aggregate_walk_trace_t;

static void aggregate_walk_trace_scalar(void *user, const tag_member_info_t *mi,
                                        psx_type_id_t value_type_id,
                                        int slot, long long offset) {
  aggregate_walk_trace_t *trace = user;
  if (!trace || trace->scalar_count >= 16) return;
  int idx = trace->scalar_count++;
  psx_gvar_init_member_value_t value =
      ps_gvar_init_member_value(
          trace->gv, slot, mi, test_type_size_id(value_type_id));
  trace->scalar_offsets[idx] = offset;
  trace->scalar_values[idx] = value.value;
  trace->scalar_sizes[idx] = value.size;
  trace->scalar_type_ids[idx] = value_type_id;
}

static void aggregate_walk_trace_padding(void *user, long long offset, int size) {
  aggregate_walk_trace_t *trace = user;
  if (!trace || trace->padding_count >= 16) return;
  int idx = trace->padding_count++;
  trace->padding_offsets[idx] = offset;
  trace->padding_sizes[idx] = size;
}

static void expect_parse_fail(const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    diag_reset_records_in(test_diagnostics());
    token_t *head = tk_tokenize((char *)input);
    parsed_code = parse_test_program_from(head);
    _exit(diag_has_error_records_in(test_diagnostics()) ? 1 : 0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    fprintf(stderr, "Expected parse failure: %s\n", input);
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void expect_parse_ok(const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    diag_reset_records_in(test_diagnostics());
    token_t *head = tk_tokenize((char *)input);
    parsed_code = parse_test_program_from(head);
    _exit(diag_has_error_records_in(test_diagnostics()) ? 1 : 0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
}

static void expect_const_assign_ok_for_node(node_t *node) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    ps_node_reject_const_assign_at_in(
        ag_compilation_session_diagnostic_context(test_suite_session),
        node, "=", NULL);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
}

static void expect_const_assign_fail_for_node(node_t *node) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    ps_node_reject_const_assign_at_in(
        ag_compilation_session_diagnostic_context(test_suite_session),
        node, "=", NULL);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void expect_const_qual_discard_fail_for_nodes(node_t *lhs, node_t *rhs) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    ps_node_reject_const_qual_discard_at_in(
        ag_compilation_session_diagnostic_context(test_suite_session),
        lhs, rhs, NULL);
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void expect_parse_fail_with_message(const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics());
    token_t *head = tk_tokenize((char *)input);
    parsed_code = parse_test_program_from(head);
    _exit(diag_has_error_records_in(test_diagnostics()) ? 1 : 0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
  if (!strstr(buf, needle)) {
    fprintf(stderr,
            "Expected diagnostic not found\ninput: %s\nexpected: %s\nactual: %s\n",
            input, needle, buf);
  }
  ASSERT_TRUE(strstr(buf, needle) != NULL);
}

static void expect_parse_fail_without_message(const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics());
    token_t *head = tk_tokenize((char *)input);
    parsed_code = parse_test_program_from(head);
    _exit(diag_has_error_records_in(test_diagnostics()) ? 1 : 0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
  ASSERT_TRUE(strstr(buf, needle) == NULL);
}

static void expect_semantic_invariant_internal_error(
    node_t *node, const token_t *fallback_diag_tok) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics());
    psx_require_semantic_tree_has_canonical_expression_types(
        ag_compilation_session_diagnostic_context(test_suite_session),
        node, fallback_diag_tok);
    _exit(0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
  ASSERT_TRUE(strstr(buf, "E0006") != NULL);
  ASSERT_TRUE(strstr(buf, "E3064") == NULL);
}

static void expect_parse_ok_without_message(const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics());
    token_t *head = tk_tokenize((char *)input);
    parsed_code = parse_test_program_from(head);
    _exit(diag_has_error_records_in(test_diagnostics()) ? 1 : 0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
  ASSERT_TRUE(strstr(buf, needle) == NULL);
}

static void expect_parse_ok_with_message(const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics());
    token_t *head = tk_tokenize((char *)input);
    parsed_code = parse_test_program_from(head);
    _exit(diag_has_error_records_in(test_diagnostics()) ? 1 : 0);
  }

  close(fds[1]);
  char buf[4096];
  size_t used = 0;
  for (;;) {
    ssize_t nread = read(fds[0], buf + used, sizeof(buf) - 1 - used);
    if (nread <= 0) break;
    used += (size_t)nread;
    if (used >= sizeof(buf) - 1) break;
  }
  buf[used] = '\0';
  close(fds[0]);

  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
  ASSERT_TRUE(strstr(buf, needle) != NULL);
}

static void test_expr_number() {
  printf("test_expr_number...\n");
  node_t *node = parse_expr_input("42");
  ASSERT_EQ(ND_NUM, node->kind);
  ASSERT_EQ(42, as_num(node)->val);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  node_t *long_node = parse_expr_input("0L");
  ASSERT_EQ(ND_NUM, long_node->kind);
  ASSERT_EQ(TK_LONG, ps_node_get_type(long_node)->scalar_kind);
  ASSERT_EQ(8, ps_node_type_size(long_node));

  node_t *unsigned_long_node = parse_expr_input("0UL");
  ASSERT_EQ(ND_NUM, unsigned_long_node->kind);
  ASSERT_EQ(TK_LONG, ps_node_get_type(unsigned_long_node)->scalar_kind);
  ASSERT_TRUE(ps_node_is_unsigned_type(unsigned_long_node));

  node_t *long_long_node = parse_expr_input("0LL");
  ASSERT_EQ(ND_NUM, long_long_node->kind);
  ASSERT_EQ(TK_LONG, ps_node_get_type(long_long_node)->scalar_kind);
  ASSERT_TRUE(ps_node_is_long_long_type(long_long_node));
}

static void test_expr_float() {
  printf("test_expr_float...\n");
    node_t *node = parse_expr_input("3.14 + 1.5f");

  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(ND_NUM, node->lhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(node->lhs));
  ASSERT_TRUE(as_num(node->lhs)->fval > 3.13 && as_num(node->lhs)->fval < 3.15);

  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, ps_node_value_fp_kind(node->rhs));
  ASSERT_TRUE(as_num(node->rhs)->fval > 1.49 && as_num(node->rhs)->fval < 1.51);
}

static void test_expr_long_double_suffix_metadata() {
  printf("test_expr_long_double_suffix_metadata...\n");
    node_t *node = parse_expr_input("4.0L");

  ASSERT_EQ(ND_NUM, node->kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, ps_node_value_fp_kind(node));
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num(node)->float_suffix_kind);
  ASSERT_TRUE(as_num(node)->fval > 3.9 && as_num(node)->fval < 4.1);

  bool found = false;
  iter_test_float_literals(find_long_double_float_literal, &found);
  ASSERT_TRUE(found);
}

static void test_expr_compound_literal() {
  printf("test_expr_compound_literal...\n");
  reset_test_locals();
  node_t *raw = parse_expr_input_with_existing_locals("(int){3}");
  ASSERT_EQ(ND_COMPOUND_LITERAL, raw->kind);
  node_compound_literal_t *compound = (node_compound_literal_t *)raw;
  ASSERT_TRUE(raw->type == NULL);
  ASSERT_TRUE(compound->type_name.syntax != NULL);
  ASSERT_TRUE(compound->type_name.bound_base_type == NULL);
  ASSERT_TRUE(compound->type_name.resolved_type == NULL);
  ASSERT_TRUE(ps_node_get_type(raw) == NULL);
  ASSERT_EQ(ND_INIT_LIST, raw->rhs->kind);
  node_t *lowered = analyze_test_expression(raw, raw->tok);
  ASSERT_TRUE(lowered != raw);
  ASSERT_TRUE(compound->type_name.resolved_type != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, raw->kind);
  ASSERT_EQ(ND_COMMA, lowered->kind);

  node_t *node = parse_expr_input("(int){3}");
  ASSERT_EQ(ND_COMMA, node->kind);
}

static void test_expr_compound_literal_array_subscript() {
  printf("test_expr_compound_literal_array_subscript...\n");
  node_t *array_literal = parse_expr_input("(int[3]){1,2,3}");
  ASSERT_EQ(ND_COMMA, array_literal->kind);
  ASSERT_EQ(12, ps_node_compound_literal_array_size(array_literal));
  ASSERT_EQ(ND_ADDR, array_literal->rhs->kind);
  ASSERT_EQ(12, ps_node_compound_literal_array_size(array_literal));

  // 配列型複合リテラルへの添字アクセス: ((int[2]){1,2})[1]
  node_t *node = parse_expr_input("((int[2]){1,2})[1]");
  ASSERT_EQ(ND_DEREF, node->kind);

  node_t *inferred = parse_expr_input("(int[]){1,2,3}");
  ASSERT_EQ(ND_COMMA, inferred->kind);
  ASSERT_EQ(12, ps_node_compound_literal_array_size(inferred));

  node_t *string_inferred = parse_expr_input("(char[]){\"abc\"}");
  ASSERT_EQ(ND_COMMA, string_inferred->kind);
  ASSERT_EQ(4, ps_node_compound_literal_array_size(string_inferred));

  node_t *pointer_inferred = parse_expr_input("(int *[]){0,0}");
  ASSERT_EQ(ND_COMMA, pointer_inferred->kind);
  ASSERT_EQ(16, ps_node_compound_literal_array_size(pointer_inferred));
}

static void test_expr_add_sub() {
  printf("test_expr_add_sub...\n");
    node_t *node = parse_expr_input("1 + 2 - 3"); // (1+2)-3

  ASSERT_EQ(ND_SUB, node->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_additive_semantic_lowering_boundary() {
  printf("test_additive_semantic_lowering_boundary...\n");
  reset_test_locals();
  lvar_t *pointer = register_test_storage_fixture((char *)"p", 1, 8, 4, 0);
  set_test_storage_fixture_type(
      pointer,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0)));
  lvar_t *integer = register_test_storage_fixture(
      (char *)"i", 1, 4, 4, 0);
  set_test_storage_fixture_type(
      integer, ps_type_new_integer(TK_INT, 4, 0));
  lvar_t *floating = register_test_storage_fixture(
      (char *)"d", 1, 8, 8, 0);
  set_test_storage_fixture_type(
      floating, ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8));

  node_t *node = parse_expr_input_with_existing_locals("p + 2");
  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(TK_PLUS, node->source_op);
  ASSERT_TRUE(node->type == NULL);
  ASSERT_TRUE(ps_node_get_type(node) == NULL);
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs)->val);

  node = analyze_test_expression(node, NULL);
  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(TK_EOF, node->source_op);
  ASSERT_TRUE(node->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, node->type->kind);
  ASSERT_EQ(ND_MUL, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs->lhs)->val);
  ASSERT_EQ(4, as_num(node->rhs->rhs)->val);

  node_t *mixed = parse_expr_input_with_existing_locals("i + d");
  ASSERT_EQ(ND_ADD, mixed->kind);
  ASSERT_TRUE(mixed->type == NULL);
  ASSERT_TRUE(ps_node_get_type(mixed) == NULL);
  mixed = analyze_test_expression(mixed, NULL);
  ASSERT_TRUE(mixed->type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, mixed->type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, mixed->type->fp_kind);

  node_t *difference = parse_expr_input_with_existing_locals("p - p");
  ASSERT_EQ(ND_SUB, difference->kind);
  ASSERT_TRUE(difference->type == NULL);
  difference = analyze_test_expression(difference, NULL);
  ASSERT_EQ(ND_DIV, difference->kind);
  ASSERT_EQ(8, ps_node_type_size(difference));
  ASSERT_TRUE(!ps_node_is_unsigned_type(difference));

  const psx_type_t *promoted = ps_type_usual_arithmetic_result(
      ps_type_new_integer(TK_CHAR, 1, 1),
      ps_type_new_integer(TK_SHORT, 2, 0),
      TK_FLOAT_KIND_NONE, 0);
  ASSERT_EQ(PSX_TYPE_INTEGER, promoted->kind);
  ASSERT_EQ(4, ps_type_sizeof(promoted));
  ASSERT_TRUE(!ps_type_is_unsigned(promoted));

  const psx_type_t *ranked = ps_type_usual_arithmetic_result(
      ps_type_new_integer(TK_UNSIGNED, 4, 1),
      ps_type_new_integer(TK_LONG, 8, 0),
      TK_FLOAT_KIND_NONE, 0);
  ASSERT_EQ(8, ps_type_sizeof(ranked));
  ASSERT_TRUE(!ps_type_is_unsigned(ranked));

  psx_type_t *complex_float = ps_type_new(PSX_TYPE_COMPLEX);
  complex_float->fp_kind = TK_FLOAT_KIND_FLOAT;
  const psx_type_t *complex_promoted = ps_type_usual_arithmetic_result(
      complex_float, ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
      TK_FLOAT_KIND_NONE, 0);
  ASSERT_EQ(PSX_TYPE_COMPLEX, complex_promoted->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, complex_promoted->fp_kind);
  ASSERT_EQ(16, ps_type_sizeof(complex_promoted));

  psx_type_t *pointer_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  const psx_type_t *pointer_sum = ps_type_binary_result(
      PSX_TYPE_BINARY_ADD, pointer_type,
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(PSX_TYPE_POINTER, pointer_sum->kind);
  ASSERT_TRUE(pointer_sum != pointer_type);
  const psx_type_t *pointer_difference = ps_type_binary_result(
      PSX_TYPE_BINARY_SUB, pointer_type, pointer_type);
  ASSERT_EQ(PSX_TYPE_INTEGER, pointer_difference->kind);
  ASSERT_EQ(8, ps_type_sizeof(pointer_difference));
  ASSERT_TRUE(!ps_type_is_unsigned(pointer_difference));

  psx_type_t *array_type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0);
  ASSERT_TRUE(!ps_type_is_pointer(array_type));
  ASSERT_TRUE(ps_type_is_pointer_like(array_type));
  const psx_type_t *array_sum = ps_type_binary_result(
      PSX_TYPE_BINARY_ADD, array_type,
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(PSX_TYPE_POINTER, array_sum->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, array_sum->base->kind);
  const psx_type_t *array_conditional = ps_type_conditional_result(
      array_type, pointer_type);
  ASSERT_EQ(PSX_TYPE_POINTER, array_conditional->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, array_conditional->base->kind);
  node_t array_operand = {.kind = ND_LVAR, .type = array_type};
  ASSERT_EQ(PSX_DEREF_OPERAND_OK,
            psx_resolve_deref_operand(&array_operand));
  ASSERT_TRUE(psx_resolve_incdec_result_type(
                  test_semantic_context(), &array_operand) == NULL);
  node_t pointer_operand = {.kind = ND_LVAR, .type = pointer_type};
  ASSERT_TRUE(psx_resolve_incdec_result_type(
                  test_semantic_context(), &pointer_operand) != NULL);

  psx_type_t *comma_rhs = ps_type_new_float(
      TK_FLOAT_KIND_DOUBLE, 8);
  const psx_type_t *comma_result = ps_type_binary_result(
      PSX_TYPE_BINARY_COMMA,
      ps_type_new_integer(TK_INT, 4, 0), comma_rhs);
  ASSERT_EQ(PSX_TYPE_FLOAT, comma_result->kind);
  ASSERT_TRUE(comma_result != comma_rhs);
  const psx_type_t *conditional_result = ps_type_conditional_result(
      ps_type_new_integer(TK_UNSIGNED, 4, 1),
      ps_type_new_integer(TK_LONG, 8, 0));
  ASSERT_EQ(8, ps_type_sizeof(conditional_result));
  ASSERT_TRUE(!ps_type_is_unsigned(conditional_result));
}

static void test_subscript_semantic_lowering_boundary() {
  printf("test_subscript_semantic_lowering_boundary...\n");
  reset_test_locals();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *row = ps_type_new_array(integer, 3, 12, 0);
  psx_type_t *matrix = ps_type_new_array(row, 2, 24, 0);
  const psx_type_t *decayed_matrix = ps_type_decay_array(matrix);
  ASSERT_TRUE(decayed_matrix != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, decayed_matrix->kind);
  ASSERT_TRUE(decayed_matrix->base == row);
  const psx_type_t *addressed_matrix = ps_type_address_result(matrix);
  ASSERT_TRUE(addressed_matrix != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, addressed_matrix->kind);
  ASSERT_TRUE(addressed_matrix->base == matrix);
  lvar_t *array = register_test_storage_fixture(
      (char *)"a", 1, 24, 12, 1);
  set_test_storage_fixture_type(array, matrix);
  ASSERT_EQ(12, ps_type_subscript_static_stride(matrix));
  const psx_type_t *matrix_row_type = ps_type_subscript_result(matrix);
  ASSERT_TRUE(matrix_row_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, matrix_row_type->kind);
  ASSERT_EQ(4, ps_type_subscript_static_stride(matrix_row_type));
  const psx_type_t *matrix_element_type =
      ps_type_subscript_result(matrix_row_type);
  ASSERT_TRUE(matrix_element_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, matrix_element_type->kind);

  psx_type_t *vla_cell = ps_type_new_array(integer, 0, 0, 1);
  psx_type_t *vla_row = ps_type_new_array(vla_cell, 0, 0, 1);
  psx_type_t *vla_matrix = ps_type_new_array(vla_row, 0, 0, 1);
  const psx_type_t *vla_result = ps_type_subscript_result(vla_matrix);
  ASSERT_TRUE(vla_result != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, vla_result->kind);
  ASSERT_TRUE(vla_result->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, vla_result->base->kind);
  psx_type_t *vla_scalar_pointer = ps_type_new_pointer(integer);
  const psx_type_t *vla_scalar_result =
      ps_type_subscript_result(vla_scalar_pointer);
  ASSERT_TRUE(vla_scalar_result != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, vla_scalar_result->kind);

  node_t *node = parse_expr_input_with_existing_locals("a[0][1]");
  ASSERT_EQ(ND_SUBSCRIPT, node->kind);
  ASSERT_EQ(ND_SUBSCRIPT, node->lhs->kind);
  ASSERT_TRUE(node->type == NULL);
  ASSERT_TRUE(node->lhs->type == NULL);
  ASSERT_TRUE(ps_node_get_type(node) == NULL);
  ASSERT_TRUE(ps_node_get_type(node->lhs) == NULL);

  node_t *subscript_syntax = node;
  node = analyze_test_expression(node, NULL);
  ASSERT_TRUE(node != subscript_syntax);
  ASSERT_EQ(ND_SUBSCRIPT, subscript_syntax->kind);
  ASSERT_EQ(ND_DEREF, node->kind);
  ASSERT_TRUE(node->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, node->type->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_TRUE(node->lhs->lhs->kind != ND_SUBSCRIPT);

  node_t *reversed = parse_expr_input_with_existing_locals("1[a]");
  ASSERT_EQ(ND_SUBSCRIPT, reversed->kind);
  ASSERT_EQ(ND_NUM, reversed->lhs->kind);
  ASSERT_EQ(ND_IDENTIFIER, reversed->rhs->kind);
  reversed = analyze_test_expression(reversed, NULL);
  ASSERT_EQ(ND_DEREF, reversed->kind);
}

static void test_unary_deref_semantic_lowering_boundary() {
  printf("test_unary_deref_semantic_lowering_boundary...\n");
  reset_test_locals();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *pointer_type = ps_type_new_pointer(integer);
  ASSERT_TRUE(ps_type_dereference_result(pointer_type) == integer);
  lvar_t *pointer = register_test_storage_fixture(
      (char *)"p", 1, 8, 4, 0);
  set_test_storage_fixture_type(pointer, pointer_type);
  lvar_t *pointer_pointer = register_test_storage_fixture(
      (char *)"q", 1, 8, 8, 0);
  set_test_storage_fixture_type(
      pointer_pointer, ps_type_new_pointer(pointer_type));

  node_t *node = parse_expr_input_with_existing_locals("**q");
  ASSERT_EQ(ND_UNARY_DEREF, node->kind);
  ASSERT_EQ(ND_UNARY_DEREF, node->lhs->kind);
  ASSERT_TRUE(node->type == NULL);
  ASSERT_TRUE(node->lhs->type == NULL);
  ASSERT_TRUE(ps_node_get_type(node) == NULL);
  ASSERT_TRUE(ps_node_get_type(node->lhs) == NULL);
  node_t *deref_syntax = node;
  node = analyze_test_expression(node, NULL);
  ASSERT_TRUE(node != deref_syntax);
  ASSERT_EQ(ND_UNARY_DEREF, deref_syntax->kind);
  ASSERT_EQ(ND_DEREF, node->kind);
  ASSERT_EQ(ND_DEREF, node->lhs->kind);
  ASSERT_TRUE(node->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, node->type->kind);

  node_t *assignment = parse_expr_input_with_existing_locals("*p = 7");
  ASSERT_EQ(ND_ASSIGN, assignment->kind);
  ASSERT_EQ(ND_UNARY_DEREF, assignment->lhs->kind);
  ASSERT_TRUE(assignment->type == NULL);
  ASSERT_TRUE(assignment->lhs->type == NULL);
  assignment = analyze_test_expression(assignment, NULL);
  ASSERT_EQ(ND_DEREF, assignment->lhs->kind);
  ASSERT_TRUE(assignment->type != NULL);

  node_t *subscript_address =
      parse_expr_input_with_existing_locals("&p[0]");
  ASSERT_EQ(ND_ADDR, subscript_address->kind);
  ASSERT_EQ(ND_SUBSCRIPT, subscript_address->lhs->kind);
  ASSERT_TRUE(subscript_address->type == NULL);
  subscript_address = analyze_test_expression(
      subscript_address, NULL);
  ASSERT_EQ(ND_ADDR, subscript_address->kind);
  ASSERT_TRUE(subscript_address->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, subscript_address->type->kind);
  ASSERT_TRUE(subscript_address->type->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, subscript_address->type->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(subscript_address->type));
}

static void test_unary_operator_semantic_lowering_boundary() {
  printf("test_unary_operator_semantic_lowering_boundary...\n");
  psx_type_t *stale_wide_char = ps_type_new_integer(TK_CHAR, 8, 0);
  node_t *stale_wide_char_value = ps_node_new_num(1);
  ps_node_bind_type(stale_wide_char_value, stale_wide_char);
  const psx_type_t *promoted_stale_char =
      psx_resolve_arithmetic_unary_result_type(
          test_semantic_context(), ND_UNARY_NEGATE,
          stale_wide_char_value);
  ASSERT_TRUE(promoted_stale_char != NULL);
  ASSERT_EQ(TK_INT, promoted_stale_char->scalar_kind);
  ASSERT_EQ(3, ps_type_integer_rank(promoted_stale_char));

  reset_test_locals();
  lvar_t *integer = register_test_storage_fixture(
      (char *)"i", 1, 4, 4, 0);
  set_test_storage_fixture_type(
      integer, ps_type_new_integer(TK_INT, 4, 0));
  lvar_t *floating = register_test_storage_fixture(
      (char *)"d", 1, 8, 8, 0);
  set_test_storage_fixture_type(
      floating, ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8));
  psx_type_t *complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  complex_type->fp_kind = TK_FLOAT_KIND_DOUBLE;
  lvar_t *complex_value = register_test_storage_fixture(
      (char *)"z", 1, 16, 8, 0);
  set_test_storage_fixture_type(complex_value, complex_type);

  node_t *raw_integer = parse_expr_input_with_existing_locals("-i");
  ASSERT_EQ(ND_UNARY_NEGATE, raw_integer->kind);
  ASSERT_TRUE(raw_integer->type == NULL);
  ASSERT_TRUE(ps_node_get_type(raw_integer) == NULL);
  node_t *lowered_integer =
      analyze_test_expression(raw_integer, NULL);
  ASSERT_EQ(ND_SUB, lowered_integer->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(lowered_integer)->kind);

  node_t *raw_floating = parse_expr_input_with_existing_locals("-d");
  ASSERT_EQ(ND_UNARY_NEGATE, raw_floating->kind);
  ASSERT_TRUE(raw_floating->type == NULL);
  ASSERT_TRUE(ps_node_get_type(raw_floating) == NULL);
  node_t *lowered_floating =
      analyze_test_expression(raw_floating, NULL);
  ASSERT_EQ(ND_FNEG, lowered_floating->kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, ps_node_get_type(lowered_floating)->kind);

  node_t *raw_real = parse_expr_input_with_existing_locals("__real__ z");
  ASSERT_EQ(ND_CREAL, raw_real->kind);
  ASSERT_TRUE(raw_real->type == NULL);
  ASSERT_TRUE(ps_node_get_type(raw_real) == NULL);
  node_t *lowered_real = analyze_test_expression(raw_real, NULL);
  ASSERT_EQ(ND_CREAL, lowered_real->kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, ps_node_get_type(lowered_real)->kind);

  node_t *raw_integer_imag =
      parse_expr_input_with_existing_locals("__imag__ i");
  ASSERT_EQ(ND_CIMAG, raw_integer_imag->kind);
  ASSERT_TRUE(raw_integer_imag->type == NULL);
  node_t *lowered_integer_imag =
      analyze_test_expression(raw_integer_imag, NULL);
  ASSERT_EQ(ND_CAST, lowered_integer_imag->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(lowered_integer_imag)->kind);
}

static void test_generic_selection_semantic_lowering_boundary() {
  printf("test_generic_selection_semantic_lowering_boundary...\n");
  node_t control = {
      .kind = ND_NUM,
      .type = ps_type_new_integer(TK_INT, 4, 0),
  };
  node_t integer_result = {
      .kind = ND_NUM,
      .type = ps_type_new_integer(TK_INT, 4, 0),
  };
  node_t float_result = {
      .kind = ND_NUM,
      .type = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
  };
  psx_generic_association_t associations[2] = {
      {
          .type_name = {
              .resolved_type = ps_type_new_integer(TK_INT, 4, 0),
          },
          .expression = &integer_result,
      },
      {
          .expression = &float_result,
          .is_default = 1,
      },
  };
  node_generic_selection_t direct_selection = {
      .base = {.kind = ND_GENERIC_SELECTION},
      .control = &control,
      .associations = associations,
      .association_count = 2,
      .selected_index = -1,
  };
  psx_generic_selection_resolution_t resolution;
  resolve_test_generic_selection(&direct_selection, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_OK, resolution.status);
  ASSERT_EQ(0, resolution.selected_index);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(
                direct_selection.associations[resolution.selected_index]
                    .expression)->kind);

  associations[0].is_default = 1;
  resolve_test_generic_selection(&direct_selection, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_DEFAULT,
            resolution.status);
  ASSERT_EQ(1, resolution.conflict_index);

  associations[0].is_default = 0;
  associations[1].is_default = 0;
  associations[1].type_name.resolved_type =
      ps_type_new_integer(TK_INT, 4, 0);
  resolve_test_generic_selection(&direct_selection, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE,
            resolution.status);
  ASSERT_EQ(1, resolution.conflict_index);

  direct_selection.association_count = 1;
  control.type = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  resolve_test_generic_selection(&direct_selection, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_NO_MATCH,
            resolution.status);

  control.type = ps_type_new_integer(TK_INT, 4, 0);
  integer_result.type = NULL;
  resolve_test_generic_selection(&direct_selection, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED,
            resolution.status);
  ASSERT_EQ(0, resolution.conflict_index);

  reset_test_locals();
  lvar_t *value = register_test_storage_fixture(
      (char *)"x", 1, 4, 4, 0);
  set_test_storage_fixture_type(
      value, ps_type_new_integer(TK_INT, 4, 0));

  node_t *raw = parse_expr_input_with_existing_locals(
      "_Generic(x, int: x + 1, default: x + 2)");
  ASSERT_EQ(ND_GENERIC_SELECTION, raw->kind);
  node_generic_selection_t *selection =
      (node_generic_selection_t *)raw;
  ASSERT_EQ(2, selection->association_count);
  ASSERT_EQ(-1, selection->selected_index);
  ASSERT_EQ(ND_IDENTIFIER, selection->control->kind);
  ASSERT_TRUE(!selection->control->records_lvar_usage);
  ASSERT_TRUE(selection->control->lvar_usage_unevaluated);
  ASSERT_TRUE(selection->associations[0].type_name.syntax != NULL);
  ASSERT_TRUE(selection->associations[0].type_name.resolved_type == NULL);
  ASSERT_EQ(ND_ADD, selection->associations[0].expression->kind);
  ASSERT_TRUE(selection->associations[1].is_default);
  ASSERT_TRUE(raw->type == NULL);
  ASSERT_TRUE(ps_node_get_type(raw) == NULL);

  node_t *lowered = analyze_test_expression(raw, NULL);
  ASSERT_TRUE(selection->associations[0].type_name.resolved_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            selection->associations[0].type_name.resolved_type->kind);
  ASSERT_EQ(0, selection->selected_index);
  ASSERT_TRUE(selection->base.type != NULL);
  ASSERT_TRUE(selection->base.type !=
              selection->associations[0].expression->type);
  ASSERT_TRUE(ps_type_shape_matches(
      selection->base.type,
      selection->associations[0].expression->type));
  ASSERT_EQ(ND_ADD, lowered->kind);
  ASSERT_EQ(1, as_num(lowered->rhs)->val);
}

static void test_sizeof_semantic_lowering_boundary() {
  printf("test_sizeof_semantic_lowering_boundary...\n");
  reset_test_locals();
  lvar_t *value = register_test_storage_fixture(
      (char *)"x", 1, 4, 4, 0);
  set_test_storage_fixture_type(
      value, ps_type_new_integer(TK_INT, 4, 0));

  node_t *raw_expr = parse_expr_input_with_existing_locals("sizeof(x)");
  ASSERT_EQ(ND_SIZEOF_QUERY, raw_expr->kind);
  node_sizeof_query_t *expr_query = (node_sizeof_query_t *)raw_expr;
  ASSERT_TRUE(!expr_query->is_type_name);
  ASSERT_EQ(ND_IDENTIFIER, expr_query->operand->kind);
  raw_expr = bind_test_identifier_tree(raw_expr, NULL);
  expr_query = (node_sizeof_query_t *)raw_expr;
  ASSERT_EQ(ND_LVAR, expr_query->operand->kind);
  ASSERT_TRUE(expr_query->operand->records_lvar_usage);
  ASSERT_TRUE(expr_query->operand->lvar_usage_unevaluated);
  psx_sizeof_query_resolution_t direct_resolution;
  resolve_test_sizeof_query(expr_query, &direct_resolution);
  ASSERT_EQ(PSX_TYPE_QUERY_RESOLUTION_OK, direct_resolution.status);
  ASSERT_TRUE(direct_resolution.usage_root == expr_query->operand);
  ASSERT_TRUE(ps_node_get_type(expr_query->operand) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(expr_query->operand)->kind);
  ASSERT_EQ(4, expr_query->resolved_size);
  node_t *lowered_expr = analyze_test_expression(raw_expr, NULL);
  ASSERT_EQ(ND_NUM, lowered_expr->kind);
  ASSERT_EQ(4, as_num(lowered_expr)->val);

  node_t *direct_type_raw =
      parse_expr_input_with_existing_locals("sizeof(int[3])");
  node_sizeof_query_t *direct_type_query =
      (node_sizeof_query_t *)direct_type_raw;
  resolve_test_sizeof_query(direct_type_query, &direct_resolution);
  ASSERT_EQ(PSX_TYPE_QUERY_RESOLUTION_OK, direct_resolution.status);
  ASSERT_TRUE(direct_type_query->type_name.resolved_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY,
            direct_type_query->type_name.resolved_type->kind);
  ASSERT_EQ(12, direct_type_query->resolved_size);

  node_sizeof_query_t *negative_type_query =
      (node_sizeof_query_t *)parse_expr_input_with_existing_locals(
          "sizeof(int[3])");
  node_t *negative_bound = negative_type_query->type_name.syntax->declarator
                               .array_bounds[0].expression.node;
  ASSERT_EQ(ND_NUM, negative_bound->kind);
  as_num(negative_bound)->val = -1;
  resolve_test_sizeof_query(negative_type_query, &direct_resolution);
  ASSERT_EQ(PSX_TYPE_QUERY_RESOLUTION_NEGATIVE_ARRAY_BOUND,
            direct_resolution.status);
  ASSERT_EQ(0, direct_resolution.issue_bound_index);

  node_sizeof_query_t *zero_type_query =
      (node_sizeof_query_t *)parse_expr_input_with_existing_locals(
          "sizeof(int[0])");
  resolve_test_sizeof_query(zero_type_query, &direct_resolution);
  ASSERT_EQ(PSX_TYPE_QUERY_RESOLUTION_OK, direct_resolution.status);
  ASSERT_EQ(1, direct_resolution.zero_length_bound_count);
  ASSERT_EQ(0, direct_resolution.zero_length_bound_indices[0]);

  node_t *raw_type =
      parse_expr_input_with_existing_locals("sizeof(int[3])");
  ASSERT_EQ(ND_SIZEOF_QUERY, raw_type->kind);
  node_sizeof_query_t *type_query = (node_sizeof_query_t *)raw_type;
  ASSERT_TRUE(type_query->is_type_name);
  ASSERT_TRUE(type_query->type_name.syntax != NULL);
  ASSERT_TRUE(type_query->type_name.resolved_type == NULL);
  ASSERT_EQ(1, type_query->type_name.syntax->declarator.declarator_shape.count);
  ASSERT_EQ(1, type_query->type_name.syntax->declarator.array_bound_count);
  ASSERT_EQ(ND_NUM,
            type_query->type_name.syntax->declarator
                .array_bounds[0].expression.node->kind);
  node_t *lowered_type = analyze_test_expression(raw_type, NULL);
  ASSERT_TRUE(type_query->type_name.resolved_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, type_query->type_name.resolved_type->kind);
  ASSERT_EQ(ND_NUM, lowered_type->kind);
  ASSERT_EQ(12, as_num(lowered_type)->val);

  psx_type_t *array_type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0);
  lvar_t *array = register_test_storage_fixture(
      (char *)"a", 1, 12, 4, 1);
  set_test_storage_fixture_type(array, array_type);
  node_t *raw_addr =
      parse_expr_input_with_existing_locals("sizeof(&a)");
  ASSERT_EQ(ND_SIZEOF_QUERY, raw_addr->kind);
  ASSERT_TRUE(((node_sizeof_query_t *)raw_addr)->operand->is_explicit_addr_expr);
  node_t *lowered_addr = analyze_test_expression(raw_addr, NULL);
  ASSERT_EQ(ND_NUM, lowered_addr->kind);
  ASSERT_EQ(8, as_num(lowered_addr)->val);

  node_t *raw_align =
      parse_expr_input_with_existing_locals("_Alignof(int[3])");
  ASSERT_EQ(ND_ALIGNOF_QUERY, raw_align->kind);
  node_alignof_query_t *align_query = (node_alignof_query_t *)raw_align;
  ASSERT_TRUE(align_query->type_name.syntax != NULL);
  ASSERT_TRUE(align_query->type_name.resolved_type == NULL);
  resolve_test_alignof_query(align_query);
  ASSERT_TRUE(align_query->type_name.resolved_type != NULL);
  ASSERT_EQ(4, align_query->resolved_alignment);
  node_t *lowered_align = analyze_test_expression(raw_align, NULL);
  ASSERT_TRUE(align_query->type_name.resolved_type != NULL);
  ASSERT_EQ(ND_NUM, lowered_align->kind);
  ASSERT_EQ(4, as_num(lowered_align)->val);
}

static void test_expression_type_materialization_boundary() {
  printf("test_expression_type_materialization_boundary...\n");
  reset_test_locals();
  node_t *ternary =
      parse_expr_input_with_existing_locals("(1, 2) ? 3 : 4");
  ASSERT_EQ(ND_TERNARY, ternary->kind);
  ASSERT_EQ(ND_COMMA, ternary->lhs->kind);
  ASSERT_TRUE(ternary->type == NULL);
  ASSERT_TRUE(ternary->lhs->type == NULL);
  ASSERT_TRUE(ps_node_get_type(ternary) == NULL);
  ASSERT_TRUE(ps_node_get_type(ternary->lhs) == NULL);

  analyze_test_expression(ternary, NULL);
  ASSERT_TRUE(ternary->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ternary->type->kind);
  ASSERT_TRUE(ternary->lhs->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ternary->lhs->type->kind);
  ASSERT_TRUE(ps_ctx_find_interned_qual_type_in(
                  test_semantic_context(), ternary->type).type_id !=
              PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_ctx_find_interned_qual_type_in(
                  test_semantic_context(), ternary->lhs->type).type_id !=
              PSX_TYPE_ID_INVALID);
}

static void test_function_call_type_binding_boundary() {
  printf("test_function_call_type_binding_boundary...\n");
  static char function_name[] = "__call_type_boundary";
  int function_name_len = (int)sizeof(function_name) - 1;
  psx_type_t *parameter = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *function = ps_type_new_function(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8));
  const psx_type_t *parameters[] = {parameter};
  ps_type_set_function_params(function, parameters, 1, 0);

  psx_function_call_resolution_t resolution;
  psx_resolve_function_call_type(function, NULL, 0, &resolution);
  ASSERT_EQ(PSX_FUNCTION_CALL_RESOLUTION_OK, resolution.status);
  ASSERT_TRUE(resolution.function_type == function);
  ASSERT_EQ(PSX_TYPE_FLOAT, resolution.function_type->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            resolution.function_type->base->fp_kind);

  psx_type_t *function_pointer = ps_type_new_pointer(
      ps_type_clone(function));
  ASSERT_TRUE(ps_type_callable_function(function) == function);
  ASSERT_TRUE(ps_type_callable_function(function_pointer) ==
              function_pointer->base);
  ASSERT_TRUE(ps_type_function_return_type(function_pointer) ==
              function_pointer->base->base);
  psx_resolve_function_call_type(
      NULL, function_pointer, 0, &resolution);
  ASSERT_EQ(PSX_FUNCTION_CALL_RESOLUTION_OK, resolution.status);
  ASSERT_EQ(PSX_TYPE_FUNCTION, resolution.function_type->kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, resolution.function_type->base->kind);

  psx_resolve_function_call_type(NULL, NULL, 1, &resolution);
  ASSERT_EQ(PSX_FUNCTION_CALL_RESOLUTION_OK, resolution.status);
  ASSERT_TRUE(resolution.function_type == NULL);

  psx_resolve_function_call_type(
      NULL, ps_type_new_integer(TK_INT, 4, 0), 0, &resolution);
  ASSERT_EQ(PSX_FUNCTION_CALL_RESOLUTION_NOT_CALLABLE,
            resolution.status);
  ASSERT_TRUE(resolution.function_type == NULL);

  psx_type_t *double_function_pointer =
      ps_type_new_pointer(ps_type_new_pointer(ps_type_clone(function)));
  ASSERT_TRUE(ps_type_derived_function(double_function_pointer) != NULL);
  ASSERT_TRUE(ps_type_callable_function(double_function_pointer) == NULL);
  ASSERT_TRUE(ps_type_function_return_type(double_function_pointer) == NULL);
  psx_resolve_function_call_type(
      NULL, double_function_pointer, 0, &resolution);
  ASSERT_EQ(PSX_FUNCTION_CALL_RESOLUTION_NOT_CALLABLE,
            resolution.status);
  ASSERT_TRUE(resolution.function_type == NULL);

  psx_type_t *function_pointer_array = ps_type_new_array(
      ps_type_clone(function_pointer), 2, 16, 0);
  ASSERT_TRUE(ps_type_derived_function(function_pointer_array) != NULL);
  ASSERT_TRUE(ps_type_callable_function(function_pointer_array) == NULL);
  ASSERT_TRUE(ps_type_function_return_type(function_pointer_array) == NULL);

  const psx_type_t *reference_type =
      psx_resolve_function_reference_type(
          test_semantic_context(), function);
  ASSERT_TRUE(reference_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, reference_type->kind);
  ASSERT_TRUE(ps_type_derived_function(reference_type) != NULL);
  ASSERT_TRUE(psx_resolve_function_reference_type(
                  test_semantic_context(), parameter) == NULL);

  test_semantic_define_function_name_with_ret(
      function_name, function_name_len, 0);
  ASSERT_TRUE(test_semantic_track_function_type(
      function_name, function_name_len, function));

  reset_test_locals();
  node_t *direct = parse_expr_input_with_existing_locals(
      "__call_type_boundary(3)");
  ASSERT_EQ(ND_FUNCALL, direct->kind);
  node_function_call_t *direct_call = (node_function_call_t *)direct;
  ASSERT_TRUE(direct_call->callee_type == NULL);
  ASSERT_TRUE(direct_call->callee != NULL);
  ASSERT_EQ(ND_IDENTIFIER, direct_call->callee->kind);
  ASSERT_TRUE(direct->type == NULL);
  direct = analyze_test_expression(direct, NULL);
  direct_call = (node_function_call_t *)direct;
  ASSERT_TRUE(direct_call->callee_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, direct_call->callee_type->kind);
  ASSERT_TRUE(direct->type != NULL);
  ASSERT_TRUE(direct->type == direct_call->callee_type->base);
  ASSERT_EQ(PSX_TYPE_FLOAT, direct->type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, direct->type->fp_kind);

  node_t *reference = parse_expr_input_with_existing_locals(
      "__call_type_boundary");
  ASSERT_EQ(ND_IDENTIFIER, reference->kind);
  ASSERT_TRUE(reference->type == NULL);
  reference = analyze_test_expression(reference, NULL);
  ASSERT_EQ(ND_FUNCREF, reference->kind);
  ASSERT_TRUE(reference->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, reference->type->kind);
  ASSERT_TRUE(ps_type_derived_function(reference->type) != NULL);

  reset_test_locals();
  lvar_t *fp = register_test_storage_fixture(
      (char *)"fp", 2, 8, 8, 0);
  set_test_storage_fixture_type(
      fp, ps_type_new_pointer(ps_type_clone(function)));
  node_t *indirect = parse_expr_input_with_existing_locals("fp(4)");
  ASSERT_EQ(ND_FUNCALL, indirect->kind);
  node_function_call_t *indirect_call = (node_function_call_t *)indirect;
  ASSERT_TRUE(indirect_call->callee != NULL);
  ASSERT_TRUE(indirect_call->callee_type == NULL);
  ASSERT_TRUE(indirect->type == NULL);
  indirect = analyze_test_expression(indirect, NULL);
  ASSERT_TRUE(indirect_call->callee_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, indirect_call->callee_type->kind);
  ASSERT_TRUE(indirect->type != NULL);
  ASSERT_TRUE(indirect->type == indirect_call->callee_type->base);
  ASSERT_EQ(PSX_TYPE_FLOAT, indirect->type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, indirect->type->fp_kind);

  reset_test_locals();
  node_t *implicit = parse_expr_input_with_existing_locals(
      "__implicit_call_type_boundary(5)");
  ASSERT_EQ(ND_FUNCALL, implicit->kind);
  node_function_call_t *implicit_call = (node_function_call_t *)implicit;
  ASSERT_TRUE(implicit_call->callee_type == NULL);
  ASSERT_TRUE(implicit->type == NULL);
  implicit = analyze_test_expression(implicit, NULL);
  implicit_call = (node_function_call_t *)implicit;
  ASSERT_TRUE(implicit_call->base.is_implicit_func_decl);
  ASSERT_TRUE(implicit_call->callee_type == NULL);
  ASSERT_TRUE(implicit->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, implicit->type->kind);
  ASSERT_EQ(TK_INT, implicit->type->scalar_kind);
  ASSERT_EQ(4, ps_type_sizeof(implicit->type));
}

static void test_cast_semantic_lowering_boundary() {
  printf("test_cast_semantic_lowering_boundary...\n");
  reset_test_locals();
  preregister_test_locals();
  node_t *node =
      parse_expr_input_with_existing_locals("(int)(unsigned long)a");
  ASSERT_EQ(ND_CAST, node->kind);
  ASSERT_TRUE(node->is_source_cast);
  node_source_cast_t *outer = (node_source_cast_t *)node;
  ASSERT_TRUE(outer->type_name.syntax != NULL);
  ASSERT_TRUE(outer->type_name.bound_base_type == NULL);
  ASSERT_TRUE(outer->type_name.resolved_type == NULL);
  ASSERT_TRUE(node->type == NULL);
  ASSERT_TRUE(ps_node_get_type(node) == NULL);
  ASSERT_EQ(ND_CAST, node->lhs->kind);
  ASSERT_TRUE(node->lhs->is_source_cast);
  node_source_cast_t *inner = (node_source_cast_t *)node->lhs;
  ASSERT_TRUE(inner->type_name.syntax != NULL);
  ASSERT_TRUE(inner->type_name.bound_base_type == NULL);
  ASSERT_TRUE(inner->type_name.resolved_type == NULL);

  node = analyze_test_expression(node, NULL);
  ASSERT_EQ(ND_CAST, node->kind);
  ASSERT_TRUE(!node->is_source_cast);
  ASSERT_EQ(ND_SHR, node->lhs->kind);
  ASSERT_EQ(ND_SHL, node->lhs->lhs->kind);
}

static void test_aggregate_cast_semantic_lowering_boundary() {
  printf("test_aggregate_cast_semantic_lowering_boundary...\n");
  node_t **program = parse_program_input(
      "int main(void) { struct S { int x; int y; }; "
      "return ((struct S)7).x; }");
  ASSERT_TRUE(program != NULL);
  ASSERT_TRUE(program[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, program[0]->kind);

  node_function_definition_t *fn = as_function_definition(program[0]);
  lvar_t *temp = NULL;
  for (lvar_t *var = fn->lvars; var; var = var->next_all) {
    if (var->len >= 17 &&
        strncmp(var->name, "__aggregate_cast_", 17) == 0) {
      temp = var;
      break;
    }
  }
  ASSERT_TRUE(temp != NULL);

  const psx_type_t *type = ps_lvar_get_decl_type(temp);
  ASSERT_TRUE(type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, type->kind);
  const psx_record_decl_t *record = test_record_decl(type);
  ASSERT_TRUE(record != NULL);
  ASSERT_EQ(2, record->member_count);
  ASSERT_EQ(1, record->members[0].len);
  ASSERT_TRUE(strncmp(record->members[0].name, "x", 1) == 0);
}

static void test_implicit_conversion_semantic_lowering_boundary() {
  printf("test_implicit_conversion_semantic_lowering_boundary...\n");
  node_t **program = parse_program_input(
      "double id(double x) { return x; } "
      "double retconv(int x) { return x; } "
      "double same(double x) { double y=id(x); return y; } "
      "int main(void) { int x=3; double d=x; d=x; return (int)id(x); }");

  node_block_t *retconv_body = as_block(as_function_definition(program[1])->base.rhs);
  ASSERT_EQ(ND_RETURN, retconv_body->body[0]->kind);
  ASSERT_EQ(ND_INT_TO_FP, retconv_body->body[0]->lhs->kind);

  node_block_t *same_body =
      as_block(as_function_definition(program[2])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, same_body->body[0]->kind);
  ASSERT_EQ(ND_FUNCALL, same_body->body[0]->rhs->kind);
  node_function_call_t *same_call =
      as_function_call(same_body->body[0]->rhs);
  ASSERT_TRUE(same_call->base.type == same_call->callee_type->base);

  node_block_t *main_body = as_block(as_function_definition(program[3])->base.rhs);
  node_t *decl_init = main_body->body[1];
  ASSERT_EQ(ND_ASSIGN, decl_init->kind);
  ASSERT_TRUE(decl_init->is_decl_initializer);
  ASSERT_EQ(ND_INT_TO_FP, decl_init->rhs->kind);

  node_t *source_assign = main_body->body[2];
  ASSERT_EQ(ND_ASSIGN, source_assign->kind);
  ASSERT_TRUE(source_assign->is_source_assignment);
  ASSERT_EQ(ND_INT_TO_FP, source_assign->rhs->kind);

  node_t *ret = main_body->body[3];
  ASSERT_EQ(ND_RETURN, ret->kind);
  node_t *return_value = ret->lhs->kind == ND_CAST
                             ? ret->lhs->lhs : ret->lhs;
  ASSERT_EQ(ND_FP_TO_INT, return_value->kind);
  ASSERT_EQ(ND_FUNCALL, return_value->lhs->kind);
  node_function_call_t *call = as_function_call(return_value->lhs);
  ASSERT_EQ(1, call->argument_count);
  ASSERT_EQ(ND_INT_TO_FP, call->arguments[0]->kind);
}

static void test_compound_assignment_semantic_lowering_boundary() {
  printf("test_compound_assignment_semantic_lowering_boundary...\n");
  reset_test_locals();
  lvar_t *value = register_test_storage_fixture((char *)"a", 1, 4, 4, 0);
  set_test_storage_fixture_type(
      value, ps_type_new_integer(TK_INT, 4, 0));
  node_t *node = parse_expr_input_with_existing_locals("a += 2");
  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_TRUE(node->is_source_compound_assignment);
  ASSERT_EQ(TK_PLUSEQ, node->source_op);
  ASSERT_EQ(ND_NUM, node->rhs->kind);

  node_t *assignment_syntax = node;
  node = analyze_test_expression(node, NULL);
  ASSERT_TRUE(node != assignment_syntax);
  ASSERT_TRUE(assignment_syntax->is_source_compound_assignment);
  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_TRUE(!node->is_source_compound_assignment);
  ASSERT_EQ(ND_ADD, node->rhs->kind);

  reset_test_locals();
  lvar_t *pointer = register_test_storage_fixture((char *)"p", 1, 8, 4, 0);
  set_test_storage_fixture_type(
      pointer,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0)));
  node = parse_expr_input_with_existing_locals("*p += 2");
  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_TRUE(node->is_source_compound_assignment);
  node = analyze_test_expression(node, NULL);
  ASSERT_EQ(ND_COMMA, node->kind);
  ASSERT_EQ(ND_ASSIGN, node->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, node->rhs->kind);
}

static void test_translation_unit_frontend_boundary() {
  printf("test_translation_unit_frontend_boundary...\n");
  const char *source =
      "int __frontend_boundary(int input) { "
      "int x=input; x += 2; return x; }";

  reset_test_translation_unit_state();
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(
      &stream, NULL, tk_tokenize((char *)source), NULL);
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item.kind);
  ASSERT_TRUE(ps_ctx_get_function_type_in(test_semantic_context(),
                  (char *)"__frontend_boundary", 19) == NULL);
  ASSERT_EQ(1, item.value.function_header.declarator
                   .function_suffixes[0].parameters->count);
  node_t *parsed = parse_raw_function_item(&stream, &item);
  ASSERT_TRUE(ps_ctx_get_function_type_in(test_semantic_context(),
                  (char *)"__frontend_boundary", 19) != NULL);
  ASSERT_TRUE(find_func_lvar(as_function_definition(parsed), "input") != NULL);
  node_block_t *parsed_body = as_block(as_function_definition(parsed)->base.rhs);
  ASSERT_EQ(ND_ASSIGN, parsed_body->body[1]->kind);
  ASSERT_TRUE(parsed_body->body[1]->is_source_compound_assignment);
  ASSERT_EQ(TK_PLUSEQ, parsed_body->body[1]->source_op);
  ps_parser_stream_end(&stream);

  reset_test_translation_unit_state();
  node_t **analyzed =
      parse_test_program_from(tk_tokenize((char *)source));
  ASSERT_TRUE(analyzed != NULL);
  ASSERT_TRUE(analyzed[0] != NULL);
  node_block_t *analyzed_body = as_block(as_function_definition(analyzed[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, analyzed_body->body[1]->kind);
  ASSERT_TRUE(!analyzed_body->body[1]->is_source_compound_assignment);
  ASSERT_EQ(TK_EOF, analyzed_body->body[1]->source_op);
  ASSERT_EQ(ND_ADD, analyzed_body->body[1]->rhs->kind);
}

static void test_toplevel_static_assert_frontend_boundary() {
  printf("test_toplevel_static_assert_frontend_boundary...\n");
  const char *source =
      "_Static_assert(0, \"deferred\"); "
      "int __after_static_assert(void) { return 0; }";

  reset_test_translation_unit_state();
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(
      &stream, NULL, tk_tokenize((char *)source), NULL);

  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_STATIC_ASSERT, item.kind);
  ASSERT_TRUE(item.value.static_assertion.condition != NULL);
  ASSERT_EQ(ND_NUM, item.value.static_assertion.condition->kind);
  ASSERT_EQ(0, as_num(item.value.static_assertion.condition)->val);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(&stream, &item) != NULL);
  ASSERT_TRUE(!ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_EOF, item.kind);
  ps_parser_stream_end(&stream);
}

static void test_toplevel_declaration_frontend_boundary() {
  printf("test_toplevel_declaration_frontend_boundary...\n");
  const char *source =
      "typedef unsigned long __PhaseWord; "
      "struct __PhaseTag { int value; }; "
      "__PhaseWord __phase_proto(__PhaseWord); "
      "__PhaseWord __phase_global; "
      "__PhaseWord __phase_initialized = 7; "
      "int __phase_function(void) { return 0; }";

  reset_test_translation_unit_state();
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(
      &stream, NULL, tk_tokenize((char *)source), NULL);
  psx_parsed_toplevel_item_t item;

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(item.value.declaration.is_typedef);
  ASSERT_EQ(1, item.value.declaration.declarator_count);
  psx_typedef_info_t typedef_info;
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(test_semantic_context(),
      (char *)"__PhaseWord", 11, &typedef_info));
  apply_test_toplevel_declaration(&item.value.declaration);
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(),
      (char *)"__PhaseWord", 11, &typedef_info));
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(item.value.declaration.is_standalone_tag);
  ASSERT_EQ(-1, ps_ctx_get_tag_member_count_in(test_semantic_context(),
                    TK_STRUCT, (char *)"__PhaseTag", 10));
  apply_test_toplevel_declaration(&item.value.declaration);
  ASSERT_EQ(1, ps_ctx_get_tag_member_count_in(test_semantic_context(),
                   TK_STRUCT, (char *)"__PhaseTag", 10));
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_EQ(1, item.value.declaration.declarator_count);
  ASSERT_TRUE(ps_ctx_get_function_type_in(test_semantic_context(),
                  (char *)"__phase_proto", 13) == NULL);
  apply_test_toplevel_declaration(&item.value.declaration);
  ASSERT_TRUE(ps_ctx_get_function_type_in(test_semantic_context(),
                  (char *)"__phase_proto", 13) != NULL);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(!item.value.declaration.is_typedef);
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            item.value.declaration.specifier.source);
  ASSERT_TRUE(find_test_global_var("__phase_global", 14) == NULL);
  apply_test_toplevel_declaration(&item.value.declaration);
  ASSERT_TRUE(find_test_global_var("__phase_global", 14) != NULL);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(item.value.declaration.initializers[0].has_initializer);
  ASSERT_EQ(PSX_DECL_INIT_EXPR,
            item.value.declaration.initializers[0].kind);
  ASSERT_EQ(ND_NUM, item.value.declaration.initializers[0].value->kind);
  ASSERT_TRUE(find_test_global_var("__phase_initialized", 19) == NULL);
  apply_test_toplevel_declaration(&item.value.declaration);
  ASSERT_TRUE(find_test_global_var("__phase_initialized", 19) != NULL);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(&stream, &item) != NULL);
  ps_parser_stream_end(&stream);
}

static void test_toplevel_callback_context_boundary() {
  printf("test_toplevel_callback_context_boundary...\n");
  ASSERT_TRUE(ag_compilation_session_is_active(test_suite_session));
  ASSERT_TRUE(tk_context_active() ==
              ag_compilation_session_tokenizer(test_suite_session));
  reset_test_translation_unit_state();
  tk_tokenize((char *)"int __callback_global = 23;");
  psx_parsed_toplevel_declaration_t declaration;
  psx_toplevel_declaration_callbacks_t callbacks;
  init_test_toplevel_declaration_callbacks(&callbacks);
  ASSERT_TRUE(parse_test_toplevel_declaration_syntax(
      &declaration, &callbacks));
  ASSERT_TRUE(declaration.applied_during_parse);
  global_var_t *global = find_test_global_var(
      (char *)"__callback_global", 17);
  ASSERT_TRUE(global != NULL);
  ASSERT_TRUE(global->has_init);
  ASSERT_EQ(23, global->init_val);
  ps_dispose_toplevel_declaration_syntax(&declaration);
}

static void test_toplevel_compound_initializer_frontend_boundary() {
  printf("test_toplevel_compound_initializer_frontend_boundary...\n");
  const char *source =
      "int *__phase_compound = (int[]){1,2,3}; "
      "int __phase_compound_function(void) { return 0; }";

  reset_test_translation_unit_state();
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(
      &stream, NULL, tk_tokenize((char *)source), NULL);
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  psx_parsed_initializer_t *initializer =
      &item.value.declaration.initializers[0];
  ASSERT_TRUE(initializer->has_initializer);
  ASSERT_EQ(PSX_DECL_INIT_EXPR, initializer->kind);
  ASSERT_EQ(ND_COMPOUND_LITERAL, initializer->value->kind);
  ASSERT_EQ(ND_INIT_LIST, initializer->value->rhs->kind);
  ASSERT_TRUE(find_test_global_var("__phase_compound", 16) == NULL);
  ASSERT_TRUE(find_test_global_var("__compound_lit_0", 16) == NULL);

  apply_test_toplevel_declaration(&item.value.declaration);
  ASSERT_TRUE(find_test_global_var("__phase_compound", 16) != NULL);
  ASSERT_TRUE(find_test_global_var("__compound_lit_0", 16) != NULL);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(&stream, &item) != NULL);
  ps_parser_stream_end(&stream);
}

static void test_toplevel_point_of_declaration_boundary() {
  printf("test_toplevel_point_of_declaration_boundary...\n");
  parsed_code = parse_program_input(
      "int __point_self = sizeof __point_self; "
      "int __point_first = sizeof __point_first, "
      "__point_second = sizeof __point_second; "
      "int main(void) { return 0; }");
  global_var_t *self = find_test_global_var("__point_self", 12);
  global_var_t *first = find_test_global_var("__point_first", 13);
  global_var_t *second = find_test_global_var("__point_second", 14);
  ASSERT_TRUE(self != NULL);
  ASSERT_TRUE(first != NULL);
  ASSERT_TRUE(second != NULL);
  ASSERT_EQ(4, self->init_val);
  ASSERT_EQ(4, first->init_val);
  ASSERT_EQ(4, second->init_val);
}

static void assert_toplevel_syntax_kind(
    const char *source, psx_toplevel_item_kind_t expected_kind,
    int expected_declarator_count) {
  reset_test_translation_unit_state();
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(
      &stream, NULL, tk_tokenize((char *)source), NULL);
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(expected_kind, item.kind);
  if (item.kind == PSX_TOPLEVEL_ITEM_DECLARATION) {
    ASSERT_EQ(expected_declarator_count,
              item.value.declaration.declarator_count);
    ps_dispose_toplevel_declaration_syntax(&item.value.declaration);
  } else if (item.kind == PSX_TOPLEVEL_ITEM_FUNCTION_HEADER) {
    ASSERT_TRUE(item.value.function_header.declarator.identifier != NULL);
    ps_dispose_function_definition_header_syntax(
        &item.value.function_header);
  }
  ps_parser_stream_end(&stream);
}

static void test_toplevel_single_parse_classification_boundary() {
  printf("test_toplevel_single_parse_classification_boundary...\n");
  assert_toplevel_syntax_kind(
      "int (*proto(void))(int);", PSX_TOPLEVEL_ITEM_DECLARATION, 1);
  assert_toplevel_syntax_kind(
      "int (*definition(void))(int) { return 0; }",
      PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, 0);
  assert_toplevel_syntax_kind(
      "struct R { int x; } (*tag_proto(void))[3];",
      PSX_TOPLEVEL_ITEM_DECLARATION, 1);
  assert_toplevel_syntax_kind(
      "struct R { int x; } (*tag_definition(void))[3] { return 0; }",
      PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, 0);
  assert_toplevel_syntax_kind(
      "int (parenthesized)(void) { return 0; }",
      PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, 0);
  assert_toplevel_syntax_kind(
      "int first(int), second(int), object;",
      PSX_TOPLEVEL_ITEM_DECLARATION, 3);
}

static void test_frontend_stream_lifecycle_boundary() {
  printf("test_frontend_stream_lifecycle_boundary...\n");
  reset_test_translation_unit_state();
  node_t **program = parse_test_program_from(tk_tokenize(
      (char *)"int __stream_previous(void) { return 0; }"));
  ASSERT_TRUE(program != NULL);
  ASSERT_TRUE(ps_ctx_get_function_type_in(test_semantic_context(),
                  (char *)"__stream_previous", 17) != NULL);

  psx_parser_stream_t parser_stream = {0};
  begin_test_parser_stream(
      &parser_stream, NULL, tk_tokenize((char *)""), NULL);
  ASSERT_TRUE(ps_ctx_get_function_type_in(test_semantic_context(),
                  (char *)"__stream_previous", 17) != NULL);
  ps_parser_stream_end(&parser_stream);

  ag_compilation_session_t session_context;
  ASSERT_TRUE(ag_compilation_session_init(&session_context, NULL));
  ag_compilation_session_t *outer_session = test_suite_session;
  ASSERT_TRUE(ag_compilation_session_is_active(outer_session));
  psx_frontend_stream_t frontend_stream = {0};
  ASSERT_TRUE(!psx_frontend_stream_begin(
      &frontend_stream, NULL, NULL, tk_tokenize((char *)"")));
  ASSERT_TRUE(psx_frontend_next_function(&frontend_stream) == NULL);
  psx_frontend_stream_end(&frontend_stream);

  ag_diagnostic_context_t *session_diagnostic_context =
      session_context.diagnostic_context;
  session_context.diagnostic_context = NULL;
  ASSERT_TRUE(!ag_compilation_session_is_complete(&session_context));
  ASSERT_TRUE(ag_compilation_session_semantic_context(
                  &session_context) == NULL);
  ASSERT_TRUE(ag_compilation_session_diagnostic_context(
                  &session_context) == NULL);
  ASSERT_TRUE(!psx_frontend_stream_begin(
      &frontend_stream, &session_context, NULL,
      tk_tokenize((char *)"int __incomplete_session(void);")));
  session_context.diagnostic_context = session_diagnostic_context;
  ASSERT_TRUE(ag_compilation_session_is_complete(&session_context));

  ASSERT_TRUE(psx_frontend_stream_begin(
      &frontend_stream, &session_context, NULL,
      tk_tokenize((char *)
          "int __stream_explicit(void) { return 0; }")));
  ASSERT_TRUE(ag_compilation_session_is_active(&session_context));
  ASSERT_TRUE(frontend_stream.owns_session_activation);
  ASSERT_TRUE(psx_frontend_next_function(&frontend_stream) != NULL);
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
                  session_context.semantic_context,
                  (char *)"__stream_explicit", 17) != NULL);
  ASSERT_TRUE(ps_ctx_get_function_type_in(session_context.semantic_context,
                  (char *)"__stream_explicit", 17) != NULL);
  ASSERT_TRUE(ps_ctx_get_function_type_in(session_context.semantic_context,
                  (char *)"__stream_previous", 17) == NULL);
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
                  outer_session->semantic_context,
                  (char *)"__stream_previous", 17) != NULL);
  ag_compilation_session_t nested_context;
  ASSERT_TRUE(ag_compilation_session_init(&nested_context, NULL));
  ASSERT_TRUE(ag_compilation_session_activate(&nested_context));
  ASSERT_TRUE(!psx_frontend_stream_end(&frontend_stream));
  ASSERT_TRUE(ag_compilation_session_is_active(&nested_context));
  ASSERT_TRUE(ag_compilation_session_deactivate(&nested_context));
  ASSERT_TRUE(psx_frontend_stream_end(&frontend_stream));
  ASSERT_TRUE(ag_compilation_session_is_active(outer_session));
  ASSERT_TRUE(!frontend_stream.owns_session_activation);
  ASSERT_TRUE(psx_frontend_next_function(&frontend_stream) == NULL);
  ASSERT_TRUE(ag_compilation_session_dispose(&nested_context));
  ASSERT_TRUE(ag_compilation_session_dispose(&session_context));
}

static void test_complex_initializer_semantic_lowering_boundary() {
  printf("test_complex_initializer_semantic_lowering_boundary...\n");
  reset_test_locals();
  lvar_t *value = register_test_storage_fixture((char *)"z", 1, 16, 16, 0);
  psx_type_t *complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  complex_type->fp_kind = TK_FLOAT_KIND_DOUBLE;
  set_test_storage_fixture_type(value, complex_type);

  psx_initializer_entry_t *complex_entries =
      calloc(2, sizeof(*complex_entries));
  complex_entries[0].value = ps_node_new_num(3);
  complex_entries[1].value = ps_node_new_num(4);
  node_t *raw = psx_node_new_raw_decl_initializer_list(
      ps_node_new_lvar_expr_ref_for(value),
      PSX_DECL_INIT_LIST, complex_entries, 2, NULL);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_TRUE(raw->lhs->type != NULL);
  ASSERT_TRUE(raw->type == NULL);
  ASSERT_TRUE(ps_node_get_type(raw) == NULL);

  node_t *initializer_syntax = raw;
  raw = analyze_test_expression(raw, NULL);
  ASSERT_TRUE(raw != initializer_syntax);
  ASSERT_EQ(ND_DECL_INIT, initializer_syntax->kind);
  ASSERT_EQ(ND_COMMA, raw->kind);
  ASSERT_EQ(ND_ASSIGN, raw->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, raw->rhs->kind);
  ASSERT_TRUE(raw->lhs->is_decl_initializer);
  ASSERT_TRUE(raw->rhs->is_decl_initializer);
  ASSERT_EQ(ND_INT_TO_FP, raw->lhs->rhs->kind);
  ASSERT_EQ(ND_INT_TO_FP, raw->rhs->rhs->kind);

  lvar_t *float_value = register_test_storage_fixture((char *)"f", 1, 8, 8, 0);
  psx_type_t *float_complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  float_complex_type->fp_kind = TK_FLOAT_KIND_FLOAT;
  set_test_storage_fixture_type(float_value, float_complex_type);
  complex_entries = calloc(2, sizeof(*complex_entries));
  complex_entries[0].value = ps_node_new_num(1);
  complex_entries[1].value = ps_node_new_num(2);
  raw = psx_node_new_raw_decl_initializer_list(
      ps_node_new_lvar_expr_ref_for(float_value),
      PSX_DECL_INIT_LIST, complex_entries, 2, NULL);
  raw = analyze_test_expression(raw, NULL);
  ASSERT_EQ(4, ((node_lvar_t *)raw->rhs->lhs)->offset -
                   ((node_lvar_t *)raw->lhs->lhs)->offset);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, ps_node_value_fp_kind(raw->lhs->lhs));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, ps_node_value_fp_kind(raw->rhs->lhs));

  reset_test_locals();
  lvar_t *array = register_test_storage_fixture((char *)"a", 1, 12, 4, 1);
  set_test_storage_fixture_type(
      array,
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0));
  raw = psx_node_new_raw_decl_initializer(
      psx_node_new_lvar_object_ref_for(array), ps_node_new_num(7),
      PSX_DECL_INIT_EXPR, NULL);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_TRUE(raw->lhs->type != NULL);
  ASSERT_TRUE(raw->type == NULL);
  ASSERT_TRUE(ps_node_get_type(raw) == NULL);

  raw = analyze_test_expression(raw, NULL);
  ASSERT_EQ(ND_COMMA, raw->kind);
  ASSERT_EQ(ND_COMMA, raw->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, raw->lhs->lhs->kind);
  ASSERT_EQ(7, as_num(raw->lhs->lhs->rhs)->val);
  ASSERT_EQ(ND_ASSIGN, raw->lhs->rhs->kind);
  ASSERT_EQ(0, as_num(raw->lhs->rhs->rhs)->val);
  ASSERT_EQ(ND_ASSIGN, raw->rhs->kind);
  ASSERT_EQ(0, as_num(raw->rhs->rhs)->val);

  psx_initializer_entry_t *entries = calloc(2, sizeof(*entries));
  entries[0].value = ps_node_new_num(8);
  entries[0].index_exprs[0] = ps_node_new_num(1);
  entries[0].index_expr_count = 1;
  entries[0].has_index = 1;
  entries[1].value = ps_node_new_num(9);
  raw = psx_node_new_raw_decl_initializer_list(
      psx_node_new_lvar_object_ref_for(array), PSX_DECL_INIT_LIST,
      entries, 2, NULL);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(ND_INIT_LIST, raw->rhs->kind);
  ASSERT_EQ(2, ((node_init_list_t *)raw->rhs)->entry_count);

  raw = analyze_test_expression(raw, NULL);
  ASSERT_EQ(ND_COMMA, raw->kind);
  ASSERT_EQ(ND_COMMA, raw->lhs->kind);
  ASSERT_EQ(8, as_num(raw->lhs->lhs->rhs)->val);
  ASSERT_EQ(ps_lvar_offset(array) + 4,
            ((node_lvar_t *)raw->lhs->lhs->lhs)->offset);
  ASSERT_EQ(9, as_num(raw->lhs->rhs->rhs)->val);
  ASSERT_EQ(ps_lvar_offset(array) + 8,
            ((node_lvar_t *)raw->lhs->rhs->lhs)->offset);
  ASSERT_EQ(0, as_num(raw->rhs->rhs)->val);
  ASSERT_EQ(ps_lvar_offset(array), ((node_lvar_t *)raw->rhs->lhs)->offset);
}

static void test_local_declaration_storage_plan_boundary() {
  printf("test_local_declaration_storage_plan_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *row = ps_type_new_array(integer, 3, 12, 0);
  psx_type_t *matrix = ps_type_new_array(row, 2, 24, 0);
  psx_local_storage_plan_t plan = {0};
  ASSERT_TRUE(plan_test_local_storage(matrix, &plan));
  ASSERT_EQ(24, plan.storage_size);
  ASSERT_EQ(4, plan.alignment);

  psx_type_t *pointer = ps_type_new_pointer(integer);
  psx_type_t *pointers = ps_type_new_array(pointer, 3, 24, 0);
  ASSERT_TRUE(plan_test_local_storage(pointers, &plan));
  ASSERT_EQ(24, plan.storage_size);

  psx_type_t *incomplete = ps_type_new_array(integer, 0, 0, 0);
  ASSERT_TRUE(!plan_test_local_storage(incomplete, &plan));
  ASSERT_TRUE(psx_resolve_incomplete_array_type(
      test_semantic_context(), incomplete,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 5,
      }));
  ASSERT_EQ(5, incomplete->array_len);
  ASSERT_EQ(20, ps_type_sizeof(incomplete));
  ASSERT_TRUE(plan_test_local_storage(incomplete, &plan));

  psx_type_t *partial_flat_matrix = ps_type_new_array(row, 0, 0, 0);
  ASSERT_TRUE(psx_resolve_incomplete_array_type(
      test_semantic_context(), partial_flat_matrix,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 5,
          .entries_initialize_outer_elements = 0,
      }));
  ASSERT_EQ(2, partial_flat_matrix->array_len);
  ASSERT_EQ(24, ps_type_sizeof(partial_flat_matrix));

  psx_type_t *nested_matrix = ps_type_new_array(row, 0, 0, 0);
  ASSERT_TRUE(psx_resolve_incomplete_array_type(
      test_semantic_context(), nested_matrix,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 2,
          .entries_initialize_outer_elements = 1,
      }));
  ASSERT_EQ(2, nested_matrix->array_len);
  ASSERT_EQ(24, ps_type_sizeof(nested_matrix));
  psx_type_t *vla = ps_type_new_array(integer, 0, 0, 1);
  ASSERT_TRUE(!plan_test_local_storage(vla, &plan));

  ASSERT_TRUE(plan_test_local_storage(pointer, &plan));
  ASSERT_EQ(8, plan.storage_size);
  ASSERT_EQ(8, plan.alignment);
  ASSERT_TRUE(plan_test_local_storage(integer, &plan));
  ASSERT_EQ(4, plan.storage_size);
  ASSERT_TRUE(!plan_test_local_storage(vla, &plan));

  reset_test_locals();
  lvar_t *lowered = lower_complete_local_object(
      &(psx_local_object_request_t){
          .local_registry = test_local_registry(),
          .lowering_context = test_lowering_context(),
          .name = (char *)"matrix",
          .name_len = 6,
          .type = matrix,
          .requested_alignment = 32,
      });
  ASSERT_TRUE(lowered != NULL);
  ASSERT_EQ(24, ps_lvar_storage_size(lowered, 0));
  ASSERT_TRUE(ps_lvar_is_array(lowered));
  ASSERT_EQ(0, ps_lvar_offset(lowered) % 32);
  const psx_type_t *stored_type = ps_lvar_get_decl_type(lowered);
  ASSERT_TRUE(stored_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, stored_type->kind);
  ASSERT_EQ(24, ps_type_sizeof(stored_type));
  ASSERT_EQ(2, stored_type->array_len);
  ASSERT_TRUE(stored_type->base != NULL);
  ASSERT_EQ(3, stored_type->base->array_len);
  ASSERT_TRUE(ps_lvar_decl_type_id(lowered) != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(24, ps_type_sizeof_id_for_target(
                    ps_ctx_semantic_type_table_in(test_semantic_context()),
                    ps_lvar_decl_type_id(lowered),
                    ps_ctx_target_info(test_semantic_context())));
  ASSERT_EQ(lowered, ps_decl_find_lvar_in(test_local_registry(), (char *)"matrix", 6));

  reset_test_locals();
  psx_type_t *deferred_type =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 0, 0, 0);
  psx_type_id_t incomplete_type_id = intern_test_type_id(deferred_type);
  ASSERT_TRUE(incomplete_type_id != PSX_TYPE_ID_INVALID);
  lvar_t *declared = declare_incomplete_local_object(
      &(psx_local_object_request_t){
          .local_registry = test_local_registry(),
          .lowering_context = test_lowering_context(),
          .name = (char *)"deferred",
          .name_len = 8,
          .type = deferred_type,
      });
  ASSERT_TRUE(declared != NULL);
  ASSERT_EQ(incomplete_type_id, ps_lvar_decl_type_id(declared));
  ASSERT_EQ(0, ps_lvar_storage_size(declared, 0));
  ASSERT_EQ(declared,
            ps_decl_find_lvar_in(test_local_registry(), (char *)"deferred", 8));
  ASSERT_TRUE(!ps_local_registry_complete_array_type(
      test_local_registry(), declared,
      ps_type_new_integer(TK_INT, 4, 0)));
  ASSERT_TRUE(!ps_local_registry_complete_array_type(
      test_local_registry(), declared,
      ps_type_new_array(
          ps_type_new_float(TK_FLOAT_KIND_FLOAT, 4), 3, 12, 0)));
  ASSERT_TRUE(psx_resolve_incomplete_array_type(
      test_semantic_context(), deferred_type,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 3,
      }));
  psx_type_id_t complete_type_id = intern_test_type_id(deferred_type);
  ASSERT_TRUE(complete_type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(complete_type_id != incomplete_type_id);
  ASSERT_TRUE(complete_declared_local_object(
      declared,
      &(psx_local_object_request_t){
          .local_registry = test_local_registry(),
          .lowering_context = test_lowering_context(),
          .name = (char *)"deferred",
          .name_len = 8,
          .type = deferred_type,
      }));
  ASSERT_EQ(12, ps_lvar_storage_size(declared, 0));
  ASSERT_EQ(3, ps_lvar_get_decl_type(declared)->array_len);
  ASSERT_EQ(complete_type_id, ps_lvar_decl_type_id(declared));
  ASSERT_TRUE(!ps_local_registry_complete_array_type(
      test_local_registry(), declared, deferred_type));

  reset_test_locals();
  psx_type_t *pipeline_input =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 0, 0, 0);
  psx_runtime_declarator_application_t pipeline_application = {0};
  psx_initializer_entry_t pipeline_entries[3] = {
      {.value = ps_node_new_num(1)},
      {.value = ps_node_new_num(2)},
      {.value = ps_node_new_num(3)},
  };
  node_init_list_t pipeline_list = {0};
  pipeline_list.base.kind = ND_INIT_LIST;
  pipeline_list.entries = pipeline_entries;
  pipeline_list.entry_count = 3;
  psx_parsed_initializer_t pipeline_initializer = {
      .has_initializer = 1,
      .kind = PSX_DECL_INIT_LIST,
      .value = (node_t *)&pipeline_list,
  };
  psx_automatic_local_declaration_pipeline_result_t pipeline_result = {0};
  ASSERT_TRUE(psx_apply_automatic_local_declaration_pipeline(
      &(psx_automatic_local_declaration_pipeline_request_t){
          .semantic_context = test_semantic_context(),
          .local_registry = test_local_registry(),
          .lowering_context = test_lowering_context(),
          .name = (char *)"pipeline_deferred",
          .name_len = 17,
          .type = pipeline_input,
          .application = &pipeline_application,
          .initializer = &pipeline_initializer,
      },
      &pipeline_result));
  ASSERT_TRUE(pipeline_result.var != NULL);
  ASSERT_EQ(0, pipeline_input->array_len);
  ASSERT_EQ(0, ps_type_sizeof(pipeline_input));
  ASSERT_EQ(3, ps_lvar_get_decl_type(pipeline_result.var)->array_len);
  ASSERT_EQ(12, ps_lvar_storage_size(pipeline_result.var, 0));

  expect_parse_ok(
      "int main(void){ int *a[]={(int *)a}; return sizeof(a)==8; }");

}

static void test_target_type_layout_boundary() {
  printf("test_target_type_layout_boundary...\n");
  ag_target_info_t host = ag_target_info_host();
  ag_target_info_t wasm = ag_target_info_wasm32();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *stale_integer = ps_type_new_integer(TK_INT, 1, 0);
  psx_type_t *float_complex = ps_type_new(PSX_TYPE_COMPLEX);
  float_complex->fp_kind = TK_FLOAT_KIND_FLOAT;
  psx_type_t *pointer = ps_type_new_pointer(integer);
  psx_type_t *pointer_array = ps_type_new_array(pointer, 3, 24, 0);
  psx_type_layout_t layout = {0};

  ASSERT_TRUE(ps_type_layout_of(pointer, &host, &layout));
  ASSERT_TRUE(layout.is_complete);
  ASSERT_EQ(8, layout.size);
  ASSERT_EQ(8, layout.alignment);
  ASSERT_EQ(4, ps_type_sizeof_for_target(pointer, &wasm));
  ASSERT_EQ(4, ps_type_alignof_for_target(pointer, &wasm));
  ASSERT_EQ(24, ps_type_sizeof_for_target(pointer_array, &host));
  ASSERT_EQ(12, ps_type_sizeof_for_target(pointer_array, &wasm));
  ASSERT_EQ(4, ps_type_sizeof_for_target(stale_integer, &host));
  ASSERT_TRUE(ps_type_shape_matches(integer, stale_integer));
  ag_target_info_t narrow_int_target = host;
  narrow_int_target.scalar[AG_TARGET_SCALAR_INT] =
      (ag_target_scalar_layout_t){2, 2};
  ASSERT_TRUE(ag_target_info_equal(&host, &host));
  ASSERT_TRUE(!ag_target_info_equal(&host, &wasm));
  ASSERT_TRUE(!ag_target_info_equal(&host, &narrow_int_target));
  ASSERT_EQ(2, ps_type_sizeof_for_target(integer, &narrow_int_target));
  ASSERT_EQ(2, ps_type_alignof_for_target(integer, &narrow_int_target));
  char target_signature[16];
  ASSERT_EQ(3, ps_type_format_canonical_signature_for_target(
                   stale_integer, &host,
                   target_signature, sizeof(target_signature)));
  ASSERT_TRUE(strcmp("i32", target_signature) == 0);
  ASSERT_EQ(3, ps_type_format_canonical_signature_for_target(
                   stale_integer, &narrow_int_target,
                   target_signature, sizeof(target_signature)));
  ASSERT_TRUE(strcmp("i16", target_signature) == 0);
  ASSERT_EQ(8, ps_type_sizeof_for_target(float_complex, &host));
  ASSERT_EQ(8, ps_type_alignof_for_target(float_complex, &host));
  ag_target_info_t packed_complex_target = host;
  packed_complex_target.scalar[AG_TARGET_SCALAR_FLOAT_COMPLEX] =
      (ag_target_scalar_layout_t){8, 2};
  ASSERT_TRUE(!ag_target_info_equal(&host, &packed_complex_target));
  ASSERT_EQ(8, ps_type_sizeof_for_target(
                   float_complex, &packed_complex_target));
  ASSERT_EQ(2, ps_type_alignof_for_target(
                   float_complex, &packed_complex_target));

  psx_type_t *stale_signed_long =
      ps_type_new_integer(TK_LONG, 1, 0);
  psx_type_t *stale_unsigned_int =
      ps_type_new_integer(TK_INT, 64, 1);
  const psx_type_t *host_conversion =
      ps_type_usual_arithmetic_result_for_target_in(
          test_arena_context(), &host,
          stale_signed_long, stale_unsigned_int,
          TK_FLOAT_KIND_NONE, 0);
  ASSERT_EQ(TK_LONG, host_conversion->scalar_kind);
  ASSERT_EQ(8, ps_type_sizeof_for_target(host_conversion, &host));
  ASSERT_TRUE(!ps_type_is_unsigned(host_conversion));

  ag_target_info_t equal_width_integer_target = host;
  equal_width_integer_target.scalar[AG_TARGET_SCALAR_LONG] =
      (ag_target_scalar_layout_t){4, 4};
  const psx_type_t *equal_width_conversion =
      ps_type_usual_arithmetic_result_for_target_in(
          test_arena_context(), &equal_width_integer_target,
          stale_signed_long, stale_unsigned_int,
          TK_FLOAT_KIND_NONE, 0);
  ASSERT_EQ(TK_LONG, equal_width_conversion->scalar_kind);
  ASSERT_EQ(4, ps_type_sizeof_for_target(
                   equal_width_conversion, &equal_width_integer_target));
  ASSERT_TRUE(ps_type_is_unsigned(equal_width_conversion));

  ag_target_info_t wide_short_target = host;
  wide_short_target.scalar[AG_TARGET_SCALAR_SHORT] =
      (ag_target_scalar_layout_t){4, 4};
  psx_type_t *stale_unsigned_short =
      ps_type_new_integer(TK_SHORT, 1, 1);
  ASSERT_TRUE(ps_type_integer_promotion_is_unsigned_for_target(
      stale_unsigned_short, &wide_short_target));
  ASSERT_TRUE(!ps_type_integer_promotion_is_unsigned_for_target(
      stale_unsigned_short, &host));

  psx_qual_type_t pointer_identity =
      ps_ctx_intern_qual_type_in(test_semantic_context(), pointer);
  psx_qual_type_t pointer_array_identity =
      ps_ctx_intern_qual_type_in(test_semantic_context(), pointer_array);
  psx_qual_type_t integer_identity =
      ps_ctx_intern_qual_type_in(test_semantic_context(), integer);
  psx_qual_type_t stale_integer_identity =
      ps_ctx_intern_qual_type_in(test_semantic_context(), stale_integer);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context());
  ASSERT_TRUE(types != NULL);
  ASSERT_EQ(integer_identity.type_id, stale_integer_identity.type_id);
  const psx_type_t *canonical_pointer_array =
      psx_semantic_type_table_lookup(types, pointer_array_identity.type_id);
  ASSERT_TRUE(canonical_pointer_array != NULL);
  ASSERT_EQ(8, ps_type_sizeof_id_for_target(
                   types, pointer_identity.type_id, &host));
  ASSERT_EQ(4, ps_type_sizeof_id_for_target(
                   types, pointer_identity.type_id, &wasm));
  ASSERT_EQ(8, ps_type_alignof_id_for_target(
                   types, pointer_identity.type_id, &host));
  ASSERT_EQ(4, ps_type_alignof_id_for_target(
                   types, pointer_identity.type_id, &wasm));
  ASSERT_EQ(24, ps_type_sizeof_id_for_target(
                    types, pointer_array_identity.type_id, &host));
  ASSERT_EQ(12, ps_type_sizeof_id_for_target(
                    types, pointer_array_identity.type_id, &wasm));
  psx_semantic_context_t *semantic_context = test_semantic_context();
  token_t *pointer_type_tokens = tk_tokenize((char *)"int *)");
  token_t *pointer_type_end = pointer_type_tokens;
  while (pointer_type_end && pointer_type_end->kind != TK_RPAREN)
    pointer_type_end = pointer_type_end->next;
  ASSERT_TRUE(pointer_type_end != NULL);
  ps_ctx_bind_target_info(semantic_context, &wasm);
  ASSERT_EQ(12, ps_ctx_type_sizeof_in(semantic_context, pointer_array));
  ASSERT_EQ(4, ps_ctx_type_alignof_in(semantic_context, pointer_array));
  ASSERT_EQ(4, psx_eval_parsed_alignas_value_in_context(
                   semantic_context, pointer_type_tokens,
                   pointer_type_end));
  ps_ctx_bind_target_info(semantic_context, &host);
  ASSERT_EQ(24, ps_ctx_type_sizeof_in(semantic_context, pointer_array));
  ASSERT_EQ(8, ps_ctx_type_alignof_in(semantic_context, pointer_array));
  ASSERT_EQ(8, psx_eval_parsed_alignas_value_in_context(
                   semantic_context, pointer_type_tokens,
                   pointer_type_end));
  psx_record_decl_t *record = arena_alloc_in(
      test_arena_context(), sizeof(*record));
  memset(record, 0, sizeof(*record));
  record->record_id = 0xfaceu;
  record->tag_kind = TK_STRUCT;
  record->is_complete = 1;
  psx_type_t *record_type = ps_type_new_tag(
      TK_STRUCT, (char *)"__TargetRecord", 14, 1, 64);
  record_type->record_id = record->record_id;
  record_type->aggregate_definition = record;
  psx_qual_type_t record_identity = ps_ctx_intern_qual_type_in(
      test_semantic_context(), record_type);
  ASSERT_TRUE(record_identity.type_id != PSX_TYPE_ID_INVALID);

  psx_record_layout_table_t *record_layouts =
      psx_record_layout_table_create();
  ASSERT_TRUE(record_layouts != NULL);
  const psx_record_member_layout_t host_members[] = {
      {.offset = 0}, {.offset = 8},
  };
  const psx_record_member_layout_t wasm_members[] = {
      {.offset = 0}, {.offset = 4},
  };
  ASSERT_TRUE(psx_record_layout_table_define(
      record_layouts, record->record_id, &host, 16, 8,
      host_members, 2));
  ASSERT_TRUE(psx_record_layout_table_define(
      record_layouts, record->record_id, &wasm, 8, 4,
      wasm_members, 2));
  ASSERT_EQ(0, ps_type_sizeof_id_for_target(
                    types, record_identity.type_id, &host));
  ASSERT_EQ(16, ps_type_sizeof_id_with_records(
                    types, record_layouts, record_identity.type_id, &host));
  ASSERT_EQ(8, ps_type_alignof_id_with_records(
                   types, record_layouts, record_identity.type_id, &host));
  ASSERT_EQ(8, ps_type_sizeof_id_with_records(
                   types, record_layouts, record_identity.type_id, &wasm));
  ir_abi_type_context_t host_abi = {
      .semantic_types = types,
      .record_layouts = record_layouts,
      .target = &host,
  };
  ir_abi_type_context_t wasm_abi = host_abi;
  wasm_abi.target = &wasm;
  ir_abi_param_info_t host_record_abi = ir_abi_classify_type_id(
      &host_abi, record_identity.type_id);
  ir_abi_param_info_t wasm_record_abi = ir_abi_classify_type_id(
      &wasm_abi, record_identity.type_id);
  ASSERT_EQ(IR_ABI_PARAM_AGGREGATE, host_record_abi.param_class);
  ASSERT_EQ(16, host_record_abi.source_size);
  ASSERT_EQ(IR_TY_PTR, host_record_abi.type);
  ASSERT_EQ(IR_ABI_PARAM_AGGREGATE, wasm_record_abi.param_class);
  ASSERT_EQ(8, wasm_record_abi.source_size);
  ASSERT_EQ(IR_TY_I64, wasm_record_abi.type);
  ASSERT_EQ(4, ps_type_alignof_id_with_records(
                   types, record_layouts, record_identity.type_id, &wasm));
  const psx_record_layout_t *host_record_layout =
      psx_record_layout_table_lookup(
          record_layouts, record->record_id, &host);
  const psx_record_layout_t *wasm_record_layout =
      psx_record_layout_table_lookup(
          record_layouts, record->record_id, &wasm);
  ASSERT_TRUE(host_record_layout != NULL);
  ASSERT_TRUE(wasm_record_layout != NULL);
  ASSERT_EQ(8, psx_record_layout_member(host_record_layout, 1)->offset);
  ASSERT_EQ(4, psx_record_layout_member(wasm_record_layout, 1)->offset);
  psx_local_storage_plan_t local = {0};
  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts, pointer_array_identity.type_id, &wasm, &local));
  ASSERT_EQ(12, local.storage_size);
  ASSERT_EQ(4, local.alignment);

  psx_parameter_storage_plan_t parameter = {0};
  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, pointer_identity.type_id, &wasm, &parameter));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_POINTER, parameter.kind);
  ASSERT_EQ(4, parameter.storage_size);
  ASSERT_EQ(4, parameter.alignment);

  psx_local_storage_plan_t host_record_local = {0};
  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts, record_identity.type_id, &host,
      &host_record_local));
  ASSERT_EQ(16, host_record_local.storage_size);
  ASSERT_EQ(8, host_record_local.alignment);

  psx_local_storage_plan_t wasm_record_local = {0};
  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts, record_identity.type_id, &wasm,
      &wasm_record_local));
  ASSERT_EQ(8, wasm_record_local.storage_size);
  ASSERT_EQ(4, wasm_record_local.alignment);

  psx_parameter_storage_plan_t host_record_parameter = {0};
  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, record_identity.type_id, &host,
      &host_record_parameter));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_AGGREGATE_VALUE,
            host_record_parameter.kind);
  ASSERT_EQ(16, host_record_parameter.storage_size);
  ASSERT_EQ(8, host_record_parameter.alignment);

  psx_parameter_storage_plan_t wasm_record_parameter = {0};
  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, record_identity.type_id, &wasm,
      &wasm_record_parameter));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_AGGREGATE_VALUE,
            wasm_record_parameter.kind);
  ASSERT_EQ(8, wasm_record_parameter.storage_size);
  ASSERT_EQ(4, wasm_record_parameter.alignment);

  psx_lowering_context_t *lowering = test_lowering_context();
  const psx_record_layout_table_t *session_record_layouts =
      ps_lowering_record_layouts(lowering);
  ps_lowering_context_bind_record_layouts(lowering, record_layouts);
  ps_lowering_context_bind_target(lowering, &wasm);
  ASSERT_EQ(8, ps_lowering_type_size(lowering, record_type));
  ASSERT_EQ(4, ps_lowering_type_alignment(lowering, record_type));
  ASSERT_EQ(4, ps_lowering_type_deref_size(lowering, pointer_array));
  node_t *pointer_value = ps_node_new_num(0);
  ps_node_bind_type(pointer_value, ps_type_new_pointer(pointer));
  node_t *pointer_add = lower_additive_expression(
      lowering, ND_ADD, pointer_value, ps_node_new_num(2));
  ASSERT_EQ(ND_MUL, pointer_add->rhs->kind);
  ASSERT_EQ(4, as_num(pointer_add->rhs->rhs)->val);

  psx_type_t *record_pointer = ps_type_new_pointer(record_type);
  ASSERT_TRUE(ps_ctx_intern_qual_type_in(
                  test_semantic_context(), record_pointer).type_id !=
              PSX_TYPE_ID_INVALID);
  node_t *wasm_record_pointer = ps_node_new_num(0);
  ps_node_bind_type(wasm_record_pointer, record_pointer);
  node_t *wasm_record_add = lower_additive_expression(
      lowering, ND_ADD, wasm_record_pointer, ps_node_new_num(1));
  ASSERT_EQ(ND_MUL, wasm_record_add->rhs->kind);
  ASSERT_EQ(8, as_num(wasm_record_add->rhs->rhs)->val);
  node_t *wasm_record_subscript = lower_subscript_expression(
      lowering, psx_node_new_subscript_syntax_for_in(
                    test_arena_context(), wasm_record_pointer,
                    ps_node_new_num(1)));
  ASSERT_EQ(ND_DEREF, wasm_record_subscript->kind);
  ASSERT_EQ(ND_MUL, wasm_record_subscript->lhs->rhs->kind);
  ASSERT_EQ(8, as_num(wasm_record_subscript->lhs->rhs->rhs)->val);

  psx_type_t *record_vla_type = ps_type_new_array(
      record_type, 0, 0, 1);
  ASSERT_TRUE(ps_ctx_intern_qual_type_in(
                  test_semantic_context(), record_vla_type).type_id !=
              PSX_TYPE_ID_INVALID);
  node_t *record_vla_dimensions[1] = {ps_node_new_num(3)};
  long long record_vla_constants[1] = {0};
  unsigned char record_vla_is_constant[1] = {0};
  psx_vla_lowering_request_t record_vla_request = {
      .local_registry = test_local_registry(),
      .lowering_context = lowering,
      .name = (char *)"target_vla",
      .name_len = 10,
      .dimensions = record_vla_dimensions,
      .const_values = record_vla_constants,
      .is_const = record_vla_is_constant,
      .dimension_count = 1,
      .type = record_vla_type,
      .requested_alignment = 8,
  };
  reset_test_locals();
  psx_vla_lowering_result_t wasm_record_vla = lower_vla_declaration(
      &record_vla_request);
  ASSERT_TRUE(wasm_record_vla.var != NULL);
  ASSERT_EQ(ND_VLA_ALLOC, wasm_record_vla.init->kind);
  ASSERT_EQ(ND_MUL, wasm_record_vla.init->lhs->kind);
  ASSERT_EQ(8, as_num(wasm_record_vla.init->lhs->rhs)->val);

  ps_lowering_context_bind_target(lowering, &host);
  ASSERT_EQ(16, ps_lowering_type_size(lowering, record_type));
  ASSERT_EQ(8, ps_lowering_type_alignment(lowering, record_type));
  ASSERT_EQ(8, ps_lowering_type_deref_size(lowering, pointer_array));
  node_t *host_record_pointer = ps_node_new_num(0);
  ps_node_bind_type(host_record_pointer, record_pointer);
  node_t *host_record_add = lower_additive_expression(
      lowering, ND_ADD, host_record_pointer, ps_node_new_num(1));
  ASSERT_EQ(ND_MUL, host_record_add->rhs->kind);
  ASSERT_EQ(16, as_num(host_record_add->rhs->rhs)->val);
  node_t *host_record_subscript = lower_subscript_expression(
      lowering, psx_node_new_subscript_syntax_for_in(
                    test_arena_context(), host_record_pointer,
                    ps_node_new_num(1)));
  ASSERT_EQ(ND_DEREF, host_record_subscript->kind);
  ASSERT_EQ(ND_MUL, host_record_subscript->lhs->rhs->kind);
  ASSERT_EQ(16, as_num(host_record_subscript->lhs->rhs->rhs)->val);
  reset_test_locals();
  psx_vla_lowering_result_t host_record_vla = lower_vla_declaration(
      &record_vla_request);
  ASSERT_TRUE(host_record_vla.var != NULL);
  ASSERT_EQ(ND_VLA_ALLOC, host_record_vla.init->kind);
  ASSERT_EQ(ND_MUL, host_record_vla.init->lhs->kind);
  ASSERT_EQ(16, as_num(host_record_vla.init->lhs->rhs)->val);
  ps_lowering_context_bind_record_layouts(
      lowering, session_record_layouts);
  psx_record_layout_table_destroy(record_layouts);
}

static void test_wasm_target_global_pointer_data_layout() {
  printf("test_wasm_target_global_pointer_data_layout...\n");
  ag_target_info_t wasm_target = ag_target_info_wasm32();
  ag_compilation_session_t session;
  ASSERT_TRUE(ag_compilation_session_init(&session, &wasm_target));

  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *pointer = ps_type_new_pointer(integer);
  psx_type_t *array = ps_type_new_array(pointer, 2, 16, 0);
  global_var_t first = {
      .name = (char *)"layout_first",
      .name_len = 12,
      .decl_type = integer,
  };
  global_var_t second = {
      .name = (char *)"layout_second",
      .name_len = 13,
      .decl_type = integer,
  };
  global_var_t pointers = {
      .name = (char *)"layout_pointers",
      .name_len = 15,
      .decl_type = array,
      .has_init = 1,
      .init_count = 2,
  };
  ps_gvar_init_slots_alloc(&pointers, 2, 0);
  ps_gvar_init_slot_write(
      &pointers, 0, 0, 0.0, first.name, first.name_len);
  ps_gvar_init_slot_write(
      &pointers, 1, 0, 0.0, second.name, second.name_len);
  first.decl_type_id = ps_ctx_intern_qual_type_in(
      session.semantic_context, integer).type_id;
  ASSERT_TRUE(first.decl_type_id != PSX_TYPE_ID_INVALID);
  second.decl_type_id = first.decl_type_id;
  pointers.decl_type_id = ps_ctx_intern_qual_type_in(
      session.semantic_context, array).type_id;
  ASSERT_TRUE(pointers.decl_type_id != PSX_TYPE_ID_INVALID);
  ps_register_global_var_in(session.global_registry, &first);
  ps_register_global_var_in(session.global_registry, &second);
  ps_register_global_var_in(session.global_registry, &pointers);

  ir_data_module_t *module =
      lower_ir_translation_unit_data_in_session(&session);
  ASSERT_TRUE(module != NULL);
  ir_data_object_t *object = ir_data_module_find_object(
      module, pointers.name, pointers.name_len);
  ASSERT_TRUE(object != NULL);
  ASSERT_EQ(8, object->byte_size);
  ASSERT_EQ(4, object->alignment);
  ASSERT_TRUE(object->relocs != NULL);
  ASSERT_EQ(0, object->relocs->offset);
  ASSERT_EQ(4, object->relocs->width);
  ASSERT_TRUE(object->relocs->next != NULL);
  ASSERT_EQ(4, object->relocs->next->offset);
  ASSERT_EQ(4, object->relocs->next->width);

  ir_data_module_free(module);
  free(pointers.init_values);
  free(pointers.init_value_symbols);
  free(pointers.init_value_symbol_lens);
  free(pointers.init_union_ordinals);
  ASSERT_TRUE(ag_compilation_session_dispose(&session));
}

static void test_vla_lowering_request_boundary() {
  printf("test_vla_lowering_request_boundary...\n");
  reset_test_locals();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *vla_type = ps_type_new_array(integer, 0, 0, 1);
  node_t *request_dimensions[3] = {0};
  long long request_const_values[3] = {0};
  unsigned char request_is_const[3] = {0};
  psx_vla_lowering_request_t request = {
      .local_registry = test_local_registry(),
      .lowering_context = test_lowering_context(),
      .dimensions = request_dimensions,
      .const_values = request_const_values,
      .is_const = request_is_const,
  };
  request.name = (char *)"v";
  request.name_len = 1;
  request.dimensions[0] = ps_node_new_num(3);
  request.dimension_count = 1;
  request.type = vla_type;
  request.requested_alignment = 16;
  psx_vla_lowering_result_t result = lower_vla_declaration(&request);
  ASSERT_TRUE(result.var != NULL);
  ASSERT_EQ(16, ps_lvar_storage_size(result.var, 0));
  ASSERT_EQ(ND_VLA_ALLOC, result.init->kind);
  ASSERT_EQ(4, ps_lvar_array_scalar_element_size(result.var));
  ASSERT_EQ(0, ps_lvar_offset(result.var) % 16);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_lvar_get_decl_type(result.var)->kind);
  ASSERT_TRUE(ps_lvar_get_decl_type(result.var)->is_vla);

  reset_test_locals();
  request.name = (char *)"vp";
  request.name_len = 2;
  request.dimensions[0] = ps_node_new_num(3);
  request.dimension_count = 1;
  request.type = ps_type_new_array(
      ps_type_new_pointer(ps_type_clone(integer)), 0, 0, 1);
  result = lower_vla_declaration(&request);
  ASSERT_TRUE(result.var != NULL);
  ASSERT_EQ(8, ps_type_pointee_value_size(request.type));
  ASSERT_EQ(ND_VLA_ALLOC, result.init->kind);
  ASSERT_EQ(ND_MUL, result.init->lhs->kind);
  ASSERT_EQ(8, as_num(result.init->lhs->rhs)->val);

  reset_test_locals();
  request.name = (char *)"m";
  request.name_len = 1;
  request.dimensions[0] = ps_node_new_num(2);
  request.dimensions[1] = ps_node_new_num(3);
  request.dimensions[2] = ps_node_new_num(4);
  request.dimension_count = 3;
  request.type = ps_type_new_array(
      ps_type_new_array(
          ps_type_new_array(integer, 0, 0, 1), 0, 0, 1),
      0, 0, 1);
  request.requested_alignment = 0;
  result = lower_vla_declaration(&request);
  ASSERT_EQ(32, ps_lvar_storage_size(result.var, 0));
  ASSERT_EQ(ND_COMMA, result.init->kind);
  ASSERT_EQ(ND_VLA_ALLOC, result.init->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, result.init->rhs->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_lvar_get_decl_type(result.var)->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY,
            ps_lvar_get_decl_type(result.var)->base->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY,
            ps_lvar_get_decl_type(result.var)->base->base->kind);
  ASSERT_TRUE(ps_lvar_vla_row_stride_frame_off(result.var) > 0);

  reset_test_locals();
  node_t *row_dimension = ps_node_new_num(5);
  psx_type_t *row_type = ps_type_new_array(integer, 0, 0, 1);
  psx_type_t *pointer_type = ps_type_new_pointer(row_type);
  result = lower_pointer_to_vla_declaration(
      &(psx_pointer_vla_lowering_request_t){
          .local_registry = test_local_registry(),
          .lowering_context = test_lowering_context(),
          .name = (char *)"p",
          .name_len = 1,
          .row_dimension = row_dimension,
          .type = pointer_type,
          .requested_alignment = 32,
      });
  ASSERT_TRUE(result.var != NULL);
  ASSERT_EQ(16, ps_lvar_storage_size(result.var, 0));
  ASSERT_EQ(8, ps_lvar_decl_sizeof(result.var, 0));
  ASSERT_EQ(0, ps_lvar_offset(result.var) % 32);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_lvar_get_decl_type(result.var)->kind);
  ASSERT_EQ(ps_lvar_offset(result.var) + 8,
            ps_lvar_vla_row_stride_frame_off(result.var));
  node_t *vla_symbol_ref = psx_node_new_lvar_identifier_ref_for(result.var);
  ASSERT_EQ(ps_lvar_vla_row_stride_frame_off(result.var),
            ps_node_vla_row_stride_frame_off(vla_symbol_ref));
  node_t *detached_type_ref = ps_node_new_param_placeholder(
      ps_type_clone(ps_lvar_get_decl_type(result.var)));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(detached_type_ref));
  ASSERT_EQ(ND_ASSIGN, result.init->kind);
  ASSERT_EQ(ps_lvar_vla_row_stride_frame_off(result.var),
            as_lvar(result.init->lhs)->offset);
  ASSERT_EQ(ND_MUL, result.init->rhs->kind);
  ASSERT_EQ(row_dimension, result.init->rhs->lhs);
  ASSERT_EQ(4, as_num(result.init->rhs->rhs)->val);

  reset_test_locals();
  lvar_t *n = register_test_storage_fixture(
      (char *)"n", 1, 4, 4, 0);
  lvar_t *k = register_test_storage_fixture(
      (char *)"k", 1, 4, 4, 0);
  n->is_param = 1;
  k->is_param = 1;
  psx_type_t *parameter_type = ps_type_new_pointer(
      ps_type_new_array(integer, 0, 0, 1));
  psx_parameter_vla_dimension_t parameter_dimensions[3] = {0};
  psx_parameter_vla_lowering_request_t parameter_request = {
      .local_registry = test_local_registry(),
      .lowering_context = test_lowering_context(),
      .name = (char *)"tensor",
      .name_len = 6,
      .inner_dimensions = parameter_dimensions,
      .inner_dimension_count = 3,
      .type = parameter_type,
  };
  parameter_request.inner_dimensions[0].source_name = (char *)"n";
  parameter_request.inner_dimensions[0].source_name_len = 1;
  parameter_request.inner_dimensions[1].constant = 3;
  parameter_request.inner_dimensions[2].source_name = (char *)"k";
  parameter_request.inner_dimensions[2].source_name_len = 1;
  psx_parameter_vla_lowering_result_t parameter_result =
      lower_parameter_vla_declaration(&parameter_request);
  ASSERT_TRUE(parameter_result.var != NULL);
  ASSERT_TRUE(parameter_result.stride_storage != NULL);
  ASSERT_TRUE(parameter_result.var->is_param);
  ASSERT_EQ(8, ps_lvar_storage_size(parameter_result.var, 0));
  ASSERT_EQ(24, ps_lvar_storage_size(
                    parameter_result.stride_storage, 0));
  ASSERT_EQ(parameter_result.stride_storage,
            ps_decl_find_lvar_in(test_local_registry(), (char *)"__rs_tensor", 11));
  ASSERT_EQ(parameter_result.stride_storage->offset,
            ps_lvar_vla_row_stride_frame_off(parameter_result.var));
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_count(
                   parameter_result.var));
  ASSERT_EQ(n->offset, ps_lvar_vla_param_inner_dim_src_offset(
                           parameter_result.var, 0));
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_const(
                   parameter_result.var, 1));
  ASSERT_EQ(k->offset, ps_lvar_vla_param_inner_dim_src_offset(
                           parameter_result.var, 2));
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_lvar_get_decl_type(parameter_result.var)->kind);
}

static void test_parameter_declaration_storage_plan_boundary() {
  printf("test_parameter_declaration_storage_plan_boundary...\n");
  reset_test_translation_unit_state();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_parameter_storage_plan_t plan = {0};
  ASSERT_TRUE(plan_test_parameter_storage(integer, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_SCALAR, plan.kind);
  ASSERT_EQ(4, plan.storage_size);

  psx_type_t *pointer = ps_type_new_pointer(integer);
  ASSERT_TRUE(plan_test_parameter_storage(pointer, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_POINTER, plan.kind);
  ASSERT_EQ(8, plan.storage_size);

  char small_parameter_tag[] = "SmallParam";
  test_semantic_define_tag_type_with_layout(
      TK_STRUCT, small_parameter_tag, 10, 0, 12, 8);
  psx_type_t *small_aggregate = ps_ctx_clone_tag_type_at_in_contexts(
      test_semantic_context(), test_local_registry(),
      TK_STRUCT, small_parameter_tag, 10,
      ps_local_registry_capture_lookup_point_in(test_local_registry()));
  ASSERT_TRUE(small_aggregate != NULL);
  ASSERT_TRUE(plan_test_parameter_storage(small_aggregate, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_AGGREGATE_VALUE, plan.kind);
  ASSERT_EQ(12, plan.storage_size);
  ASSERT_EQ(8, plan.alignment);
  ASSERT_TRUE(!plan.is_byref);

  char large_parameter_tag[] = "LargeParam";
  test_semantic_define_tag_type_with_layout(
      TK_STRUCT, large_parameter_tag, 10, 0, 24, 8);
  psx_type_t *large_aggregate = ps_ctx_clone_tag_type_at_in_contexts(
      test_semantic_context(), test_local_registry(),
      TK_STRUCT, large_parameter_tag, 10,
      ps_local_registry_capture_lookup_point_in(test_local_registry()));
  ASSERT_TRUE(large_aggregate != NULL);
  ASSERT_EQ(0, ps_type_sizeof(large_aggregate));
  ASSERT_EQ(24, ps_ctx_get_tag_size_in(
                    test_semantic_context(), TK_STRUCT,
                    large_parameter_tag, 10));
  ASSERT_EQ(8, ps_ctx_get_tag_align_in(
                   test_semantic_context(), TK_STRUCT,
                   large_parameter_tag, 10));
  ASSERT_TRUE(plan_test_parameter_storage(large_aggregate, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_AGGREGATE_BYREF, plan.kind);
  ASSERT_EQ(8, plan.storage_size);
  ASSERT_TRUE(plan.is_byref);

  psx_type_t *complex = ps_type_new(PSX_TYPE_COMPLEX);
  complex->fp_kind = TK_FLOAT_KIND_DOUBLE;
  ASSERT_TRUE(plan_test_parameter_storage(complex, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_COMPLEX, plan.kind);
  ASSERT_EQ(16, plan.storage_size);
  ASSERT_EQ(8, plan.alignment);

  reset_test_locals();
  lvar_t *lowered = lower_parameter_declaration(
      &(psx_parameter_lowering_request_t){
          .local_registry = test_local_registry(),
          .lowering_context = test_lowering_context(),
          .name = (char *)"value",
          .name_len = 5,
          .type = large_aggregate,
      });
  ASSERT_TRUE(lowered != NULL);
  ASSERT_TRUE(lowered->is_param);
  ASSERT_TRUE(lowered->is_byref_param);
  ASSERT_EQ(8, lowered->size);
  ASSERT_EQ(0, ps_lvar_decl_sizeof(lowered, 0));
  ASSERT_EQ(24, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    ps_lvar_get_decl_type(lowered)));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_lvar_get_decl_type(lowered)->kind);

  psx_declarator_shape_t vla_parameter_shape;
  ps_declarator_shape_init(&vla_parameter_shape);
  ASSERT_TRUE(ps_declarator_shape_append_vla_array(
      &vla_parameter_shape));
  psx_parameter_dimension_t parameter_dimensions[1] = {0};
  psx_parameter_declaration_resolution_request_t parameter_request = {
      .type = {
          .semantic_context = test_semantic_context(),
          .base_type = ps_type_new_integer(TK_INT, 4, 0),
          .declarator_shape = &vla_parameter_shape,
      },
      .inner_dimensions = parameter_dimensions,
      .inner_dimension_count = 1,
  };
  parameter_request.inner_dimensions[0].source_name = (char *)"n";
  parameter_request.inner_dimensions[0].source_name_len = 1;
  psx_parameter_declaration_resolution_t parameter_resolution;
  ASSERT_TRUE(psx_resolve_parameter_declaration(
      &parameter_request, &parameter_resolution));
  ASSERT_EQ(PSX_PARAMETER_LOWER_VLA,
            parameter_resolution.lowering_kind);
  ASSERT_EQ(PSX_PARAMETER_STORAGE_POINTER,
            parameter_resolution.storage.kind);
  ASSERT_EQ(4,
            ps_type_pointee_value_size(parameter_resolution.type));
  ASSERT_EQ(PSX_TYPE_POINTER, parameter_resolution.type->kind);

  reset_test_locals();
  lvar_t *dimension = register_test_storage_fixture(
      (char *)"n", 1, 4, 4, 0);
  dimension->is_param = 1;
  lvar_t *resolved_lowered = lower_resolved_parameter_declaration(
      &(psx_resolved_parameter_lowering_request_t){
          .local_registry = test_local_registry(),
          .lowering_context = test_lowering_context(),
          .name = (char *)"values",
          .name_len = 6,
          .resolution = &parameter_resolution,
      });
  ASSERT_TRUE(resolved_lowered != NULL);
  ASSERT_TRUE(resolved_lowered->is_param);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_lvar_get_decl_type(resolved_lowered)->kind);
  ASSERT_EQ(dimension->offset,
            ps_lvar_vla_row_stride_src_offset(resolved_lowered));

  const psx_type_t *parameter_types[2] = {integer, pointer};
  psx_type_t *function_input = ps_type_new_function(ps_type_clone(pointer));
  ps_type_set_function_params(function_input, parameter_types, 2, 1);
  static char planned_function_name[] = "__planned_function";
  psx_function_declaration_resolution_t planned_function = {0};
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = planned_function_name,
          .name_len = (int)sizeof(planned_function_name) - 1,
          .function_type = function_input,
      },
      &planned_function);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, planned_function.status);
  ASSERT_TRUE(planned_function.function != NULL);
  const psx_type_t *planned_function_type =
      ps_function_symbol_type(planned_function.function);
  ASSERT_EQ(PSX_TYPE_FUNCTION, planned_function_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER,
            planned_function_type->base->kind);
  ASSERT_EQ(2, planned_function_type->param_count);
  ASSERT_TRUE(planned_function_type->is_variadic_function);
  ASSERT_TRUE(ps_type_shape_matches(
      planned_function_type->param_types[0], integer));
  ASSERT_TRUE(ps_type_shape_matches(
      planned_function_type->param_types[1], pointer));
  psx_type_t *cyclic_function_type = ps_type_new_function(NULL);
  cyclic_function_type->base = cyclic_function_type;
  ASSERT_TRUE(!ps_type_is_well_formed(cyclic_function_type));
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__cyclic_function",
          .name_len = 17,
          .function_type = cyclic_function_type,
      },
      &planned_function);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_INVALID, planned_function.status);

  psx_declarator_shape_t returned_funcptr_shape;
  ps_declarator_shape_init(&returned_funcptr_shape);
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &returned_funcptr_shape, 0, 0));
  ASSERT_TRUE(ps_declarator_shape_append_function(
      &returned_funcptr_shape));
  const psx_type_t *returned_callable_params[] = {integer};
  psx_set_resolved_function_parameter_types(
      test_arena_context(),
      &returned_funcptr_shape.ops[returned_funcptr_shape.count - 1],
      returned_callable_params, 1, 0);
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &returned_funcptr_shape, 0, 0));
  ASSERT_EQ(2, ps_declarator_shape_count_ops(
                   &returned_funcptr_shape, PSX_DECL_OP_POINTER));
  ASSERT_EQ(1, ps_declarator_shape_count_ops(
                   &returned_funcptr_shape, PSX_DECL_OP_FUNCTION));
  ASSERT_EQ(0, ps_declarator_shape_count_ops(
                   &returned_funcptr_shape, PSX_DECL_OP_ARRAY));
  psx_type_t *returned_funcptr = ps_type_apply_declarator_shape(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
      &returned_funcptr_shape);
  psx_type_t *funcptr_function_input =
      ps_type_new_function(ps_type_clone(returned_funcptr));
  static char funcptr_function_name[] = "__planned_funcptr";
  psx_function_declaration_resolution_t funcptr_resolution = {0};
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = funcptr_function_name,
          .name_len = (int)sizeof(funcptr_function_name) - 1,
          .function_type = funcptr_function_input,
      },
      &funcptr_resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, funcptr_resolution.status);
  ASSERT_TRUE(funcptr_resolution.function != NULL);
  const psx_type_t *planned_return =
      ps_type_function_return_type(
          ps_function_symbol_type(funcptr_resolution.function));
  const psx_type_t *planned_callable = ps_type_derived_function(planned_return);
  ASSERT_TRUE(planned_callable != NULL);
  ASSERT_EQ(1, planned_callable->param_count);
  ASSERT_TRUE(ps_type_shape_matches(
      planned_callable->param_types[0], integer));
  ASSERT_EQ(PSX_TYPE_POINTER, planned_callable->base->kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, planned_callable->base->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, planned_callable->base->base->fp_kind);

  reset_test_translation_unit_state();
  integer = ps_type_new_integer(TK_INT, 4, 0);
  pointer = ps_type_new_pointer(integer);
  parameter_types[0] = integer;
  parameter_types[1] = pointer;
  psx_type_t *resolution_function_type =
      ps_type_new_function(ps_type_clone(integer));
  ps_type_set_function_params(
      resolution_function_type, parameter_types, 2, 1);
  psx_function_declaration_resolution_request_t resolution_request = {
      .semantic_context = test_semantic_context(),
      .global_registry = test_global_registry(),
      .name = (char *)"__resolution_fn",
      .name_len = 15,
      .function_type = integer,
  };
  psx_function_declaration_resolution_t resolution = {0};
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_INVALID, resolution.status);

  resolution_request.function_type = resolution_function_type;
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(ps_ctx_has_function_name_in(test_semantic_context(), "__resolution_fn", 15));
  ASSERT_TRUE(ps_ctx_get_function_type_in(test_semantic_context(), "__resolution_fn", 15) != NULL);

  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, resolution.status);
  ASSERT_EQ(0, ps_ctx_is_function_defined_in(test_semantic_context(), "__resolution_fn", 15));
  psx_type_t *pointer_return_function_type =
      ps_type_new_function(ps_type_clone(pointer));
  ps_type_set_function_params(
      pointer_return_function_type, parameter_types, 2, 1);
  resolution_request.function_type = pointer_return_function_type;
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_TYPE_CONFLICT, resolution.status);

  resolution_request.function_type = resolution_function_type;
  resolution_request.is_definition = 1;
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(ps_ctx_is_function_defined_in(test_semantic_context(), "__resolution_fn", 15));
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_DUPLICATE_DEFINITION,
            resolution.status);

  parsed_code = parse_program_input("int __resolution_object;");
  ASSERT_TRUE(find_test_global_var("__resolution_object", 19) != NULL);
  resolution_request.name = (char *)"__resolution_object";
  resolution_request.name_len = 19;
  resolution_request.function_type = resolution_function_type;
  psx_resolve_function_declaration(&resolution_request, &resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OBJECT_NAME_CONFLICT,
            resolution.status);
}

static void test_global_declaration_resolution_boundary() {
  printf("test_global_declaration_resolution_boundary...\n");
  reset_test_global_registry_translation_unit();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);

  char *incomplete_tag_name = (char *)"__BoundaryIncompleteRecord";
  int incomplete_tag_len = 26;
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      test_semantic_context(), test_local_registry(), TK_STRUCT,
      incomplete_tag_name, incomplete_tag_len, 0, 0, 0, 1));
  const psx_record_decl_t *incomplete_record =
      ps_ctx_get_tag_definition_in(
          test_semantic_context(), TK_STRUCT,
          incomplete_tag_name, incomplete_tag_len);
  ASSERT_TRUE(incomplete_record != NULL);
  ASSERT_TRUE(!incomplete_record->is_complete);
  psx_type_t *stale_complete_view = ps_type_new_tag(
      TK_STRUCT, incomplete_tag_name, incomplete_tag_len, 1, 64);
  stale_complete_view->record_id = incomplete_record->record_id;
  stale_complete_view->aggregate_definition = incomplete_record;
  psx_global_declaration_resolution_t record_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_stale_complete_record",
          .name_len = 32,
          .type = stale_complete_view,
      },
      &record_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT,
            record_resolution.status);

  char *complete_tag_name = (char *)"__BoundaryCompleteRecord";
  int complete_tag_len = 24;
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      test_semantic_context(), test_local_registry(), TK_STRUCT,
      complete_tag_name, complete_tag_len, 1, 0, 16, 8));
  const psx_record_decl_t *complete_record = ps_ctx_get_tag_definition_in(
      test_semantic_context(), TK_STRUCT,
      complete_tag_name, complete_tag_len);
  ASSERT_TRUE(complete_record != NULL);
  ASSERT_TRUE(complete_record->is_complete);
  psx_type_t *stale_incomplete_view = ps_type_new_tag(
      TK_STRUCT, complete_tag_name, complete_tag_len, 1, 0);
  stale_incomplete_view->record_id = complete_record->record_id;
  stale_incomplete_view->aggregate_definition = complete_record;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_stale_incomplete_record",
          .name_len = 34,
          .type = stale_incomplete_view,
      },
      &record_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK, record_resolution.status);

  psx_type_t *incomplete = ps_type_new_array(
      integer, 0, 0, 0);
  psx_global_declaration_resolution_t first_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = incomplete,
          .is_extern_decl = 1,
      },
      &first_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK, first_resolution.status);
  ASSERT_TRUE(first_resolution.existing == NULL);
  psx_global_object_result_t first = {0};
  ASSERT_TRUE(lower_resolved_global_object_declaration(
      &(psx_resolved_global_object_request_t){
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = incomplete,
          .is_extern_decl = 1,
          .resolution = &first_resolution,
      },
      &first));
  ASSERT_TRUE(first.global != NULL);
  ASSERT_TRUE(first.created);
  ASSERT_TRUE(first.global->is_extern_decl);
  ASSERT_EQ(0, ps_gvar_decl_sizeof(first.global, 0));

  psx_type_t *complete = ps_type_new_array(
      integer, 3, 12, 0);
  psx_global_declaration_resolution_t merged_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = complete,
      },
      &merged_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK, merged_resolution.status);
  ASSERT_EQ(first.global, merged_resolution.existing);
  ASSERT_TRUE(merged_resolution.complete_existing_array);
  ASSERT_TRUE(merged_resolution.clear_existing_extern);
  psx_global_object_result_t merged = {0};
  ASSERT_TRUE(lower_resolved_global_object_declaration(
      &(psx_resolved_global_object_request_t){
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = complete,
          .resolution = &merged_resolution,
      },
      &merged));
  ASSERT_EQ(first.global, merged.global);
  ASSERT_TRUE(!merged.created);
  ASSERT_TRUE(!merged.global->is_extern_decl);
  ASSERT_EQ(12, ps_gvar_decl_sizeof(merged.global, 0));
  ASSERT_EQ(3, ps_gvar_get_decl_type(merged.global)->array_len);
  const psx_type_t *merged_type = ps_gvar_get_decl_type(merged.global);

  psx_global_declaration_resolution_t repeated_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = complete,
      },
      &repeated_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK, repeated_resolution.status);
  ASSERT_TRUE(!repeated_resolution.complete_existing_array);
  psx_global_object_result_t repeated = {0};
  ASSERT_TRUE(lower_resolved_global_object_declaration(
      &(psx_resolved_global_object_request_t){
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = complete,
          .resolution = &repeated_resolution,
      },
      &repeated));
  ASSERT_TRUE(ps_gvar_get_decl_type(repeated.global) == merged_type);

  psx_global_declaration_resolution_t rejected_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_incomplete",
          .name_len = 21,
          .type = incomplete,
      },
      &rejected_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_INCOMPLETE_OBJECT,
            rejected_resolution.status);
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_incomplete",
          .name_len = 21,
          .type = incomplete,
          .has_initializer = 1,
      },
      &rejected_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK, rejected_resolution.status);

  psx_type_t *pointer = ps_type_new_pointer(integer);
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_global",
          .name_len = 17,
          .type = pointer,
      },
      &rejected_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_TYPE_CONFLICT,
            rejected_resolution.status);
  psx_global_object_result_t internal = {0};
  ASSERT_TRUE(lower_global_object_declaration(
      &(psx_global_object_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__boundary_static",
          .name_len = 17,
          .type = pointer,
          .is_static = 1,
      },
      &internal));
  ASSERT_TRUE(internal.global->is_static);
  ASSERT_EQ(8, ps_gvar_decl_sizeof(internal.global, 0));
}

static void test_declaration_pipeline_order_boundary() {
  printf("test_declaration_pipeline_order_boundary...\n");
  reset_test_translation_unit_state();
  token_t *tokens = tk_tokenize((char *)"= 37");
  tk_set_current_token(tokens);
  psx_parsed_initializer_t initializer;
  psx_prepare_optional_initializer_syntax(
      &initializer,
      ag_compilation_session_parser_runtime_context(test_suite_session));
  char *name = (char *)"__pipeline_object";
  int name_len = 17;
  psx_global_declaration_pipeline_request_t request = {
      .semantic_context = test_semantic_context(),
      .global_registry = test_global_registry(),
      .local_registry = test_local_registry(),
      .lowering_context = test_lowering_context(),
      .options = test_compilation_options(),
      .name = name,
      .name_len = name_len,
      .type = ps_type_new_integer(TK_INT, 4, 0),
      .initializer = &initializer,
      .diag_tok = tokens,
  };
  psx_global_declaration_pipeline_result_t result;
  ASSERT_TRUE(psx_begin_global_declaration_pipeline(&request, &result));
  ASSERT_TRUE(find_test_global_var(name, name_len) != NULL);
  token_t *assign_tok = tk_get_current_token();
  ASSERT_EQ(TK_ASSIGN, assign_tok->kind);
  tk_set_current_token(assign_tok->next);
  parse_test_initializer_syntax_value(&initializer, assign_tok);
  ASSERT_TRUE(psx_finish_global_declaration_pipeline(&request, &result));
  ASSERT_TRUE(result.global != NULL);
  ASSERT_TRUE(result.initialized);
  ASSERT_EQ(37, result.global->init_val);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);
}

static void test_tag_declaration_resolution_boundary() {
  printf("test_tag_declaration_resolution_boundary...\n");
  reset_test_translation_unit_state();
  psx_tag_declaration_resolution_request_t request = {
      .semantic_context = test_semantic_context(),
      .local_registry = test_local_registry(),
      .kind = TK_STRUCT,
      .name = (char *)"__TagBoundary",
      .name_len = 13,
      .mode = PSX_TAG_DECLARATION_FORWARD,
  };
  psx_tag_declaration_resolution_t resolution;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.registered);
  ASSERT_EQ(0, resolution.scope_depth);

  request.mode = PSX_TAG_DECLARATION_REFERENCE;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(!resolution.registered);
  const psx_aggregate_definition_t *cached_definition =
      ps_ctx_get_tag_definition_in(test_semantic_context(),
          TK_STRUCT, (char *)"__TagBoundary", 13);
  ASSERT_TRUE(cached_definition != NULL);
  ASSERT_TRUE(cached_definition->record_id != PSX_RECORD_ID_INVALID);
  ASSERT_TRUE(!cached_definition->is_complete);
  psx_record_id_t outer_record_id = cached_definition->record_id;
  ASSERT_TRUE(psx_record_layout_table_lookup(
                  ps_ctx_record_layout_table_in(test_semantic_context()),
                  outer_record_id,
                  ps_ctx_target_info(test_semantic_context())) == NULL);

  request.mode = PSX_TAG_DECLARATION_DEFINITION;
  request.alignment = 1;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.registered);
  ASSERT_EQ(cached_definition,
            ps_ctx_get_tag_definition_in(test_semantic_context(),
                TK_STRUCT, (char *)"__TagBoundary", 13));
  ASSERT_TRUE(cached_definition->is_complete);
  ASSERT_EQ(outer_record_id, cached_definition->record_id);
  const psx_record_layout_t *outer_layout =
      psx_record_layout_table_lookup(
          ps_ctx_record_layout_table_in(test_semantic_context()),
          outer_record_id,
          ps_ctx_target_info(test_semantic_context()));
  ASSERT_TRUE(outer_layout != NULL);
  ASSERT_EQ(1, outer_layout->alignment);
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_REDEFINITION, resolution.status);

  request.kind = TK_UNION;
  request.mode = PSX_TAG_DECLARATION_FORWARD;
  request.alignment = 0;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_KIND_CONFLICT, resolution.status);

  ps_ctx_enter_block_scope_in(test_semantic_context());
  request.kind = TK_STRUCT;
  request.mode = PSX_TAG_DECLARATION_FORWARD;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.registered);
  ASSERT_EQ(1, resolution.scope_depth);
  const psx_aggregate_definition_t *shadow_definition =
      ps_ctx_get_tag_definition_in(test_semantic_context(),
          TK_STRUCT, (char *)"__TagBoundary", 13);
  ASSERT_TRUE(shadow_definition != NULL);
  ASSERT_TRUE(shadow_definition->record_id != outer_record_id);
  request.mode = PSX_TAG_DECLARATION_DEFINITION;
  request.alignment = 1;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_EQ(1, resolution.scope_depth);
  ps_ctx_leave_block_scope_in(test_semantic_context());
  ASSERT_EQ(0, ps_ctx_get_tag_scope_depth_in(test_semantic_context(),
                   TK_STRUCT, (char *)"__TagBoundary", 13));
}

static void test_aggregate_definition_ownership_boundary() {
  printf("test_aggregate_definition_ownership_boundary...\n");
  reset_test_translation_unit_state();

  char tag_name[] = "__DefinitionOwner";
  int tag_name_len = (int)(sizeof(tag_name) - 1);
  ASSERT_TRUE(test_semantic_register_tag_type(
      TK_STRUCT, tag_name, tag_name_len, 0, 0, 0, 0));
  const psx_aggregate_definition_t *first =
      ps_ctx_get_tag_definition_in(test_semantic_context(), TK_STRUCT, tag_name, tag_name_len);
  ASSERT_TRUE(first != NULL);
  ASSERT_TRUE(first->record_id != PSX_RECORD_ID_INVALID);
  ASSERT_EQ(first, ps_ctx_get_record_decl_in(
                       test_semantic_context(), first->record_id));
  ASSERT_EQ(first, psx_record_decl_table_lookup(
                       ps_ctx_record_decl_table_in(test_semantic_context()),
                       first->record_id));
  ASSERT_TRUE(!first->is_complete);
  ASSERT_EQ(0, first->member_count);

  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  tag_member_info_t member = {
      .name = (char *)"value",
      .len = 5,
      .offset = 0,
      .decl_type = integer,
  };
  ASSERT_TRUE(ps_ctx_register_record_members_in(
      test_semantic_context(), first->record_id, &member, 1, NULL));
  ASSERT_TRUE(test_semantic_register_tag_type(
      TK_STRUCT, tag_name, tag_name_len, 1, 1, 4, 4));
  ASSERT_TRUE(first ==
              ps_ctx_get_tag_definition_in(test_semantic_context(),
                  TK_STRUCT, tag_name, tag_name_len));
  ASSERT_EQ(1, first->member_count);
  ASSERT_TRUE(first->is_complete);
  ASSERT_TRUE(first->members != NULL);
  ASSERT_EQ(5, first->members[0].len);
  ASSERT_EQ(4, ps_type_sizeof(first->members[0].decl_type));
  const tag_member_info_t *first_members = first->members;

  ps_ctx_reset_tag_diag_state_in(test_semantic_context());
  ASSERT_TRUE(first->members == first_members);
  ASSERT_EQ(5, first->members[0].len);

  ASSERT_TRUE(test_semantic_register_tag_members(
      TK_STRUCT, tag_name, tag_name_len, &member, 1, NULL));
  ASSERT_TRUE(test_semantic_register_tag_type(
      TK_STRUCT, tag_name, tag_name_len, 1, 1, 4, 4));
  const psx_aggregate_definition_t *second =
      ps_ctx_get_tag_definition_in(test_semantic_context(), TK_STRUCT, tag_name, tag_name_len);
  ASSERT_TRUE(second != NULL);
  ASSERT_TRUE(second != first);
  ASSERT_TRUE(second->record_id != first->record_id);
  ASSERT_EQ(second, ps_ctx_get_record_decl_in(
                        test_semantic_context(), second->record_id));
  ASSERT_EQ(second, psx_record_decl_table_lookup(
                        ps_ctx_record_decl_table_in(test_semantic_context()),
                        second->record_id));
  psx_type_t first_type = {
      .kind = PSX_TYPE_STRUCT,
      .tag_kind = TK_STRUCT,
      .tag_name = tag_name,
      .tag_len = tag_name_len,
      .record_id = first->record_id,
      .aggregate_definition = first,
  };
  psx_type_t second_type = first_type;
  second_type.record_id = second->record_id;
  second_type.aggregate_definition = second;
  ASSERT_TRUE(!ps_type_tag_identity_matches(&first_type, &second_type));
  ASSERT_EQ(1, second->member_count);
  ASSERT_EQ(5, second->members[0].len);
  ASSERT_TRUE(first->members == first_members);
  ASSERT_EQ(5, first->members[0].len);

  psx_type_t *return_record = ps_type_new_tag(
      TK_STRUCT, tag_name, tag_name_len, 1, 4);
  return_record->aggregate_definition = first;
  psx_type_t *parameter_record = ps_type_new_tag(
      TK_STRUCT, tag_name, tag_name_len, 1, 4);
  parameter_record->aggregate_definition = second;
  psx_type_t *function_type = ps_type_new_function(
      ps_type_new_pointer(return_record));
  const psx_type_t *parameter_types[] = {
      ps_type_new_pointer(parameter_record),
  };
  ps_type_set_function_params(function_type, parameter_types, 1, 0);
  ps_ctx_bind_record_ids_in(test_semantic_context(), function_type);
  ASSERT_EQ(first->record_id,
            ps_type_record_id(function_type->base->base));
  ASSERT_TRUE(function_type->base->base->aggregate_definition == NULL);
  ASSERT_EQ(second->record_id,
            ps_type_record_id(function_type->param_types[0]->base));
  ASSERT_TRUE(
      function_type->param_types[0]->base->aggregate_definition == NULL);
}

static int register_boundary_tag_member(
    token_kind_t tag_kind, char *tag_name, int tag_name_len,
    char *member_name, int member_name_len, int offset,
    const psx_type_t *type,
    int bit_width, int bit_offset, int bit_is_signed) {
  tag_member_info_t member = {
      .name = member_name,
      .len = member_name_len,
      .offset = offset,
      .bit_width = bit_width,
      .bit_offset = bit_offset,
      .bit_is_signed = bit_is_signed,
      .decl_type = type,
  };
  int created = 0;
  return test_semantic_register_tag_member(
             tag_kind, tag_name, tag_name_len, &member, &created) &&
         created;
}

static int register_test_tag_member(
    token_kind_t tag_kind, char *tag_name, int tag_name_len,
    const tag_member_info_t *member) {
  int created = 0;
  return test_semantic_register_tag_member(
             tag_kind, tag_name, tag_name_len, member, &created) &&
         created;
}

static void test_aggregate_body_phase_boundary() {
  printf("test_aggregate_body_phase_boundary...\n");
  reset_test_translation_unit_state();

  psx_tag_declaration_resolution_t tag;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .local_registry = test_local_registry(),
          .kind = TK_STRUCT,
          .name = (char *)"__ParsedBody",
          .name_len = 12,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag.status);

  token_t *tokens = tk_tokenize(
      (char *)"int a, *b; _Static_assert(1, \"ok\"); char c; "
               "struct PhaseInnerTag { int x; } inner; "
               "enum PhaseEnumTag { PhaseEnumZero = 3, "
               "PhaseEnumNext = PhaseEnumZero + 2 } e; "
               "int arr[PhaseEnumNext]; "
               "unsigned flags:PhaseEnumZero; "
               "_Alignas(PhaseEnumNext + 3) char aligned; "
               "DeferredAlias late; "
               "int (*callback)(DeferredParam, int *, "
               "struct PrototypeOnly *, ...); }");
  tk_set_current_token(tokens);
  psx_parsed_aggregate_body_t body;
  parse_test_aggregate_body(&body);
  ASSERT_EQ(10, body.item_count);
  ASSERT_EQ(PSX_PARSED_AGGREGATE_MEMBER_DECLARATION,
            body.items[0].kind);
  ASSERT_EQ(2, body.items[0].value.member_declaration.declarator_count);
  ASSERT_EQ(PSX_PARSED_AGGREGATE_STATIC_ASSERT, body.items[1].kind);
  ASSERT_TRUE(body.items[1].value.static_assertion.condition != NULL);
  ASSERT_EQ(PSX_PARSED_AGGREGATE_MEMBER_DECLARATION,
            body.items[2].kind);
  ASSERT_EQ(PSX_PARSED_TAG_DEFINITION,
            body.items[3].value.member_declaration.specifier
                .tag_action.action);
  ASSERT_TRUE(body.items[3].value.member_declaration.specifier
                  .tag_action.aggregate_body != NULL);
  ASSERT_EQ(PSX_PARSED_TAG_DEFINITION,
            body.items[4].value.member_declaration.specifier
                .tag_action.action);
  ASSERT_TRUE(body.items[4].value.member_declaration.specifier
                  .tag_action.enum_body != NULL);
  psx_parsed_enum_body_t *phase_enum_body =
      body.items[4].value.member_declaration.specifier
          .tag_action.enum_body;
  ASSERT_EQ(2, phase_enum_body->member_count);
  ASSERT_TRUE(phase_enum_body->members[1].initializer != NULL);
  ASSERT_EQ(PSX_ENUM_EXPR_BINARY,
            phase_enum_body->members[1].initializer->kind);
  ASSERT_EQ(TK_PLUS, phase_enum_body->members[1].initializer->op);
  ASSERT_EQ(PSX_ENUM_EXPR_IDENTIFIER,
            phase_enum_body->members[1].initializer->lhs->kind);
  ASSERT_EQ(13,
            phase_enum_body->members[1].initializer->lhs
                ->identifier_len);
  ASSERT_TRUE(strncmp(
                  phase_enum_body->members[1].initializer->lhs
                      ->identifier,
                  "PhaseEnumZero", 13) == 0);
  ASSERT_EQ(PSX_ENUM_EXPR_VALUE,
            phase_enum_body->members[1].initializer->rhs->kind);
  ASSERT_EQ(2, phase_enum_body->members[1].initializer->rhs->value);
  ASSERT_EQ(1, body.items[5].value.member_declaration.declarators[0]
                   .array_bound_count);
  ASSERT_TRUE(!body.items[5].value.member_declaration.declarators[0]
                   .array_bounds[0].expression.has_constant_value);
  ASSERT_TRUE(body.items[6].value.member_declaration.declarators[0]
                  .bit_width_expression.start != NULL);
  ASSERT_TRUE(!body.items[6].value.member_declaration.declarators[0]
                   .bit_width_expression.has_constant_value);
  ASSERT_EQ(1, body.items[7].value.member_declaration.specifier
                   .alignas_expression_count);
  ASSERT_TRUE(!body.items[7].value.member_declaration.specifier
                   .alignas_expressions[0].has_constant_value);
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            body.items[8].value.member_declaration.specifier.source);
  ASSERT_EQ(13, body.items[8].value.member_declaration.specifier
                    .typedef_name->len);
  ASSERT_EQ(1, body.items[9].value.member_declaration.declarators[0]
                   .function_suffix_count);
  psx_parsed_function_parameters_t *callback_parameters =
      body.items[9].value.member_declaration.declarators[0]
          .function_suffixes[0].parameters;
  ASSERT_TRUE(callback_parameters != NULL);
  ASSERT_EQ(3, callback_parameters->count);
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            callback_parameters->items[0].specifier.source);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  tag_member_info_t member = {0};
  ASSERT_TRUE(!ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"a", 1, &member));
  ASSERT_TRUE(!test_semantic_has_tag_type(
      TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_TRUE(!test_semantic_has_tag_type(
      TK_ENUM, (char *)"PhaseEnumTag", 12));
  long long enum_value = 0;
  ASSERT_TRUE(!ps_ctx_find_enum_const_in(test_semantic_context(),
      (char *)"PhaseEnumZero", 13, &enum_value));
  const psx_type_t *deferred_alias_type = NULL;
  ASSERT_TRUE(!ps_ctx_find_typedef_decl_type_in(test_semantic_context(),
      (char *)"DeferredAlias", 13, &deferred_alias_type));

  psx_typedef_info_t deferred_alias = {0};
  deferred_alias.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(test_semantic_define_typedef_name(
      (char *)"DeferredAlias", 13, &deferred_alias));
  psx_typedef_info_t deferred_parameter = {0};
  deferred_parameter.decl_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  ASSERT_TRUE(test_semantic_define_typedef_name(
      (char *)"DeferredParam", 13, &deferred_parameter));

  int size = 0;
  int alignment = 0;
  ASSERT_EQ(10, apply_test_parsed_aggregate_body_layout(
                   &body, TK_STRUCT, (char *)"__ParsedBody", 12,
                   &size, &alignment));
  ASSERT_EQ(72, size);
  ASSERT_EQ(8, alignment);
  ASSERT_EQ(5, body.items[5].value.member_declaration.declarators[0]
                   .array_bounds[0].expression.constant_value);
  ASSERT_TRUE(body.items[5].value.member_declaration.declarators[0]
                  .array_bounds[0].expression.has_constant_value);
  ASSERT_EQ(3, body.items[6].value.member_declaration.declarators[0]
                   .bit_width_expression.constant_value);
  ASSERT_TRUE(body.items[6].value.member_declaration.declarators[0]
                  .bit_width_expression.has_constant_value);
  ASSERT_EQ(8, body.items[7].value.member_declaration.specifier
                   .alignas_expressions[0].constant_value);
  ASSERT_TRUE(body.items[7].value.member_declaration.specifier
                  .alignas_expressions[0].has_constant_value);
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"a", 1, &member));
  ASSERT_EQ(0, member.offset);
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"b", 1, &member));
  ASSERT_EQ(8, member.offset);
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"c", 1, &member));
  ASSERT_EQ(16, member.offset);
  ASSERT_TRUE(test_semantic_has_tag_type(
      TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_EQ(4, ps_ctx_get_tag_size_in(test_semantic_context(),
                   TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_TRUE(test_semantic_has_tag_type(
      TK_ENUM, (char *)"PhaseEnumTag", 12));
  ASSERT_TRUE(ps_ctx_find_enum_const_in(test_semantic_context(),
      (char *)"PhaseEnumZero", 13, &enum_value));
  ASSERT_EQ(3, enum_value);
  ASSERT_TRUE(ps_ctx_find_enum_const_in(test_semantic_context(),
      (char *)"PhaseEnumNext", 13, &enum_value));
  ASSERT_EQ(5, enum_value);
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"arr", 3, &member));
  ASSERT_EQ(28, member.offset);
  ASSERT_TRUE(member.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, member.decl_type->kind);
  ASSERT_EQ(5, member.decl_type->array_len);
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"flags", 5, &member));
  ASSERT_EQ(48, member.offset);
  ASSERT_EQ(3, member.bit_width);
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"aligned", 7, &member));
  ASSERT_EQ(56, member.offset);
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"late", 4, &member));
  ASSERT_EQ(60, member.offset);
  ASSERT_EQ(PSX_TYPE_INTEGER, member.decl_type->kind);
  ASSERT_EQ(4, ps_ctx_type_sizeof_in(
                   test_semantic_context(), member.decl_type));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"callback", 8, &member));
  ASSERT_EQ(64, member.offset);
  const psx_type_t *callback_type =
      ps_type_derived_function(member.decl_type);
  ASSERT_TRUE(callback_type != NULL);
  ASSERT_EQ(3, callback_type->param_count);
  ASSERT_TRUE(callback_type->is_variadic_function);
  ASSERT_EQ(PSX_TYPE_FLOAT, callback_type->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, callback_type->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER, callback_type->param_types[1]->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, callback_type->param_types[1]->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, callback_type->param_types[2]->kind);
  ASSERT_EQ(PSX_TYPE_STRUCT, callback_type->param_types[2]->base->kind);
  ASSERT_TRUE(!test_semantic_has_tag_type(
      TK_STRUCT, (char *)"PrototypeOnly", 13));
  psx_dispose_parsed_aggregate_body(&body);
}

static void test_declaration_phase_boundary() {
  printf("test_declaration_phase_boundary...\n");
  reset_test_translation_unit_state();

  token_t *tokens = tk_tokenize(
      (char *)"struct __PhaseObject { int value; }");
  tk_set_current_token(tokens);
  psx_declaration_phase_t phase;
  psx_parsed_decl_specifier_t syntax;
  parse_test_decl_specifier_syntax(&syntax);
  prepare_test_decl_specifier_alignments(&syntax);
  psx_begin_declaration_phase(&phase, &syntax);
  ASSERT_EQ(PSX_PARSED_DECL_TYPE_NONE, syntax.source);
  ASSERT_EQ(PSX_DECLARATION_PHASE_SYNTAX, phase.state);
  ASSERT_TRUE(phase.base_type == NULL);
  ASSERT_EQ(-1, ps_ctx_get_tag_member_count_in(test_semantic_context(),
                    TK_STRUCT, (char *)"__PhaseObject", 13));

  const psx_type_t *unapplied_type =
      resolve_test_decl_specifier_syntax(&phase.syntax);
  ASSERT_TRUE(unapplied_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, unapplied_type->kind);
  ASSERT_TRUE(unapplied_type->aggregate_definition == NULL);
  ASSERT_EQ(-1, ps_ctx_get_tag_member_count_in(test_semantic_context(),
                    TK_STRUCT, (char *)"__PhaseObject", 13));

  ASSERT_TRUE(apply_test_declaration_phase(&phase, 0));
  ASSERT_EQ(PSX_DECLARATION_PHASE_RESOLVED_TYPE, phase.state);
  ASSERT_TRUE(phase.base_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, phase.base_type->kind);
  const psx_record_decl_t *phase_record = test_record_decl(phase.base_type);
  ASSERT_TRUE(phase_record != NULL);
  ASSERT_EQ(1, phase_record->member_count);
  ASSERT_EQ(1, ps_ctx_get_tag_member_count_in(test_semantic_context(),
                   TK_STRUCT, (char *)"__PhaseObject", 13));
  psx_dispose_declaration_phase(&phase);

  tokens = tk_tokenize((char *)"_Alignas(16) int");
  tk_set_current_token(tokens);
  parse_test_decl_specifier_syntax(&syntax);
  prepare_test_decl_specifier_alignments(&syntax);
  psx_begin_declaration_phase(&phase, &syntax);
  ASSERT_EQ(0, phase.requested_alignment);
  ASSERT_TRUE(apply_test_declaration_phase(&phase, 0));
  ASSERT_EQ(16, phase.requested_alignment);
  ASSERT_EQ(PSX_TYPE_INTEGER, phase.base_type->kind);
  psx_dispose_declaration_phase(&phase);
}

static void test_type_name_phase_boundary() {
  printf("test_type_name_phase_boundary...\n");
  reset_test_translation_unit_state();
  token_t *tokens = tk_tokenize((char *)"int (*)(double),");
  tk_set_current_token(tokens);
  psx_parsed_type_name_t syntax;
  ASSERT_TRUE(parse_test_type_name_syntax_at(tokens, &syntax));
  ASSERT_TRUE(tk_get_current_token() == tokens);
  ASSERT_TRUE(syntax.end != NULL);
  ASSERT_EQ(TK_COMMA, syntax.end->kind);
  ASSERT_TRUE(syntax.atomic_inner == NULL);

  const psx_type_t *type = apply_test_parsed_type_name(&syntax);
  ASSERT_TRUE(type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, type->kind);
  ASSERT_TRUE(type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, type->base->kind);
  ASSERT_EQ(1, type->base->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT, type->base->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            type->base->param_types[0]->fp_kind);
  psx_dispose_type_name_syntax(&syntax);
}

static void test_toplevel_declarator_phase_boundary() {
  printf("test_toplevel_declarator_phase_boundary...\n");
  reset_test_translation_unit_state();

  parse_program_input("int __phase_fn(int), __phase_object;");

  const psx_type_t *function =
      ps_ctx_get_function_type_in(test_semantic_context(), (char *)"__phase_fn", 10);
  ASSERT_TRUE(function != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function->kind);
  ASSERT_EQ(1, function->param_count);
  ASSERT_EQ(PSX_TYPE_INTEGER, function->param_types[0]->kind);

  global_var_t *object =
      find_test_global_var((char *)"__phase_object", 14);
  ASSERT_TRUE(object != NULL);
  ASSERT_TRUE(ps_gvar_get_decl_type(object) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_gvar_get_decl_type(object)->kind);
}

static void test_local_declarator_application_boundary() {
  printf("test_local_declarator_application_boundary...\n");
  reset_test_translation_unit_state();
  register_test_default_storage_fixture((char *)"n", 1);

  token_t *tokens = tk_tokenize((char *)"matrix[n][4]");
  tk_set_current_token(tokens);
  psx_parsed_declarator_t syntax =
      parse_test_declarator_syntax_tree();
  parse_test_runtime_declarator_expressions(&syntax);
  ASSERT_TRUE(syntax.identifier != NULL);
  ASSERT_EQ(2, syntax.array_bound_count);
  ASSERT_EQ(2, syntax.declarator_shape.count);
  ASSERT_EQ(0, syntax.declarator_shape.ops[0].array_len);
  ASSERT_TRUE(!syntax.declarator_shape.ops[0].is_vla_array);
  ASSERT_EQ(0, syntax.declarator_shape.ops[1].array_len);
  ASSERT_TRUE(!syntax.declarator_shape.ops[1].is_vla_array);
  ASSERT_TRUE(syntax.array_bounds[0].expression.node != NULL);
  ASSERT_TRUE(syntax.array_bounds[1].expression.node != NULL);
  ASSERT_TRUE(syntax.array_bounds[0].expression.identifier_name != NULL);
  ASSERT_EQ(1, syntax.array_bounds[0].expression.identifier_name_len);
  ASSERT_TRUE(strncmp(
      syntax.array_bounds[0].expression.identifier_name, "n", 1) == 0);
  ASSERT_TRUE(syntax.array_bounds[1].expression.identifier_name == NULL);
  token_t *after_syntax = tk_get_current_token();

  psx_runtime_declarator_application_t applied;
  apply_test_runtime_parsed_declarator(&syntax, &applied);
  ASSERT_TRUE(tk_get_current_token() == after_syntax);
  ASSERT_EQ(2, applied.array_bound_count);
  ASSERT_TRUE(applied.shape.ops[0].is_vla_array);
  ASSERT_EQ(0, applied.shape.ops[0].array_len);
  ASSERT_TRUE(!applied.array_bounds[0].is_constant);
  ASSERT_TRUE(applied.array_bounds[0].expression_id !=
              PSX_SEMANTIC_EXPR_ID_INVALID);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  test_semantic_context(),
                  applied.array_bounds[0].expression_id) != NULL);
  ASSERT_TRUE(!applied.shape.ops[1].is_vla_array);
  ASSERT_EQ(4, applied.shape.ops[1].array_len);
  ASSERT_TRUE(applied.array_bounds[1].is_constant);
  ASSERT_EQ(4, applied.array_bounds[1].constant_value);
  psx_dispose_declarator_syntax(&syntax);
}

static void test_local_declaration_resolution_boundary() {
  printf("test_local_declaration_resolution_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_runtime_declarator_application_t application = {0};
  psx_local_declaration_resolution_t resolution;

  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .arena_context =
              ag_compilation_session_arena_context(test_suite_session),
          .semantic_types = ps_ctx_semantic_type_table_in(
              test_semantic_context()),
          .record_layouts = ps_ctx_record_layout_table_in(
              test_semantic_context()),
          .type_id = intern_test_type_id(integer),
          .target = ag_compilation_session_target(test_suite_session),
          .application = &application,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_COMPLETE, resolution.storage_kind);

  psx_type_t *incomplete = ps_type_new_array(integer, 0, 0, 0);
  ps_declarator_shape_init(&application.shape);
  ASSERT_TRUE(ps_declarator_shape_append_array_ex(
      &application.shape, 0, 1));
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .arena_context =
              ag_compilation_session_arena_context(test_suite_session),
          .semantic_types = ps_ctx_semantic_type_table_in(
              test_semantic_context()),
          .record_layouts = ps_ctx_record_layout_table_in(
              test_semantic_context()),
          .type_id = intern_test_type_id(incomplete),
          .target = ag_compilation_session_target(test_suite_session),
          .application = &application,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_INCOMPLETE_ARRAY_NEEDS_INITIALIZER,
            resolution.status);
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .arena_context =
              ag_compilation_session_arena_context(test_suite_session),
          .semantic_types = ps_ctx_semantic_type_table_in(
              test_semantic_context()),
          .record_layouts = ps_ctx_record_layout_table_in(
              test_semantic_context()),
          .type_id = intern_test_type_id(incomplete),
          .target = ag_compilation_session_target(test_suite_session),
          .application = &application,
          .has_initializer = 1,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_INCOMPLETE_ARRAY,
            resolution.storage_kind);

  node_t *runtime_bound = ps_node_new_num(7);
  psx_semantic_expr_id_t runtime_bound_id =
      ps_ctx_register_semantic_expression_in(
          test_semantic_context(), runtime_bound);
  ASSERT_TRUE(runtime_bound_id != PSX_SEMANTIC_EXPR_ID_INVALID);
  ASSERT_EQ(runtime_bound_id,
            ps_ctx_register_semantic_expression_in(
                test_semantic_context(), runtime_bound));
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  test_semantic_context(),
                  PSX_SEMANTIC_EXPR_ID_INVALID) == NULL);
  psx_type_t *vla = ps_type_new_array(integer, 0, 0, 1);
  application = (psx_runtime_declarator_application_t){0};
  ps_declarator_shape_init(&application.shape);
  ASSERT_TRUE(ps_declarator_shape_append_vla_array(
      &application.shape));
  psx_runtime_array_bound_t vla_bounds[1] = {{0}};
  application.array_bounds = vla_bounds;
  application.array_bound_count = 1;
  application.array_bounds[0] = (psx_runtime_array_bound_t){
      .declarator_op_index = 0,
      .expression_id = runtime_bound_id,
  };
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .arena_context =
              ag_compilation_session_arena_context(test_suite_session),
          .semantic_types = ps_ctx_semantic_type_table_in(
              test_semantic_context()),
          .record_layouts = ps_ctx_record_layout_table_in(
              test_semantic_context()),
          .type_id = intern_test_type_id(vla),
          .target = ag_compilation_session_target(test_suite_session),
          .application = &application,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_VLA_OBJECT, resolution.storage_kind);
  ASSERT_EQ(1, resolution.dimension_count);
  ASSERT_TRUE(resolution.dimensions[0].expression_id !=
              PSX_SEMANTIC_EXPR_ID_INVALID);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  test_semantic_context(),
                  resolution.dimensions[0].expression_id) == runtime_bound);

  psx_type_t *pointer_to_vla = ps_type_new_pointer(vla);
  application = (psx_runtime_declarator_application_t){0};
  ps_declarator_shape_init(&application.shape);
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &application.shape, 0, 0));
  ASSERT_TRUE(ps_declarator_shape_append_vla_array(
      &application.shape));
  psx_runtime_array_bound_t pointer_vla_bounds[1] = {{0}};
  application.array_bounds = pointer_vla_bounds;
  application.array_bound_count = 1;
  application.array_bounds[0] = (psx_runtime_array_bound_t){
      .declarator_op_index = 1,
      .expression_id = runtime_bound_id,
  };
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .arena_context =
              ag_compilation_session_arena_context(test_suite_session),
          .semantic_types = ps_ctx_semantic_type_table_in(
              test_semantic_context()),
          .record_layouts = ps_ctx_record_layout_table_in(
              test_semantic_context()),
          .type_id = intern_test_type_id(pointer_to_vla),
          .target = ag_compilation_session_target(test_suite_session),
          .application = &application,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_POINTER_TO_VLA, resolution.storage_kind);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  test_semantic_context(),
                  resolution.pointer_row_dimension_id) == runtime_bound);

  char record_element_tag[] = "LocalRecordElement";
  test_semantic_define_tag_type_with_layout(
      TK_STRUCT, record_element_tag, 18, 0, 8, 4);
  psx_type_t *record_element = ps_ctx_clone_tag_type_at_in_contexts(
      test_semantic_context(), test_local_registry(),
      TK_STRUCT, record_element_tag, 18,
      ps_local_registry_capture_lookup_point_in(test_local_registry()));
  ASSERT_TRUE(record_element != NULL);
  ASSERT_TRUE(test_record_decl(record_element) != NULL);
  psx_type_t *incomplete_record_array = ps_type_new_array(
      record_element, 0, 0, 0);
  application = (psx_runtime_declarator_application_t){0};
  ps_declarator_shape_init(&application.shape);
  ASSERT_TRUE(ps_declarator_shape_append_array_ex(
      &application.shape, 0, 1));
  psx_resolve_local_declaration(
      &(psx_local_declaration_resolution_request_t){
          .arena_context =
              ag_compilation_session_arena_context(test_suite_session),
          .semantic_types = ps_ctx_semantic_type_table_in(
              test_semantic_context()),
          .record_layouts = ps_ctx_record_layout_table_in(
              test_semantic_context()),
          .type_id = intern_test_type_id(incomplete_record_array),
          .target = ag_compilation_session_target(test_suite_session),
          .application = &application,
          .has_initializer = 1,
      },
      &resolution);
  ASSERT_EQ(PSX_LOCAL_DECLARATION_OK, resolution.status);
  ASSERT_EQ(PSX_LOCAL_STORAGE_INCOMPLETE_ARRAY,
            resolution.storage_kind);
}

static void test_aggregate_member_resolution_boundary() {
  printf("test_aggregate_member_resolution_boundary...\n");
  reset_test_translation_unit_state();

  psx_type_t *scoped_tag = ps_type_new_tag(
      TK_STRUCT, (char *)"Scoped", 6, 1, 4);
  ASSERT_TRUE(ps_type_tag_identity_matches(
      scoped_tag, ps_type_clone(scoped_tag)));
  ASSERT_TRUE(!ps_type_tag_identity_matches(
      scoped_tag,
      ps_type_new_tag(TK_STRUCT, (char *)"Scoped", 6, 2, 4)));
  psx_aggregate_definition_t anonymous_definition = {
      .tag_kind = TK_STRUCT,
  };
  psx_type_t *anonymous_tag = ps_type_new_tag(
      TK_STRUCT, NULL, 0, 1, 4);
  anonymous_tag->aggregate_definition = &anonymous_definition;
  ASSERT_TRUE(ps_type_tag_identity_matches(
      anonymous_tag, ps_type_clone(anonymous_tag)));
  psx_aggregate_definition_t other_anonymous_definition = {
      .tag_kind = TK_STRUCT,
  };
  psx_type_t *other_anonymous_tag = ps_type_new_tag(
      TK_STRUCT, NULL, 0, 1, 4);
  other_anonymous_tag->aggregate_definition =
      &other_anonymous_definition;
  ASSERT_TRUE(!ps_type_tag_identity_matches(
      anonymous_tag, other_anonymous_tag));

  psx_aggregate_layout_state_t layout;
  psx_aggregate_layout_init(
      &layout, TK_STRUCT, PSX_RECORD_ID_INVALID);
  psx_declarator_shape_t boundary_shape;
  ps_declarator_shape_init(&boundary_shape);
  psx_aggregate_member_declaration_resolution_t boundary;
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = ps_type_new_integer(TK_CHAR, 1, 0),
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"c",
          .member_name_len = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(0, boundary.offset);
  ASSERT_TRUE(boundary.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(1, ps_type_sizeof_id_for_target(
                   ps_ctx_semantic_type_table_in(test_semantic_context()),
                   boundary.type_id,
                   ps_ctx_target_info(test_semantic_context())));

  psx_type_t *bitfield_integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = bitfield_integer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"a",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 20,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(0, boundary.offset);
  ASSERT_EQ(8, boundary.bit_offset);
  ASSERT_TRUE(boundary.bit_is_signed);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = bitfield_integer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"b",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 16,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(4, boundary.offset);
  ASSERT_EQ(0, boundary.bit_offset);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = bitfield_integer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"overflow",
          .member_name_len = 8,
          .has_bitfield = 1,
          .bit_width = 33,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE,
            boundary.status);
  ASSERT_EQ(4, boundary.storage_size);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = ps_type_new_pointer(bitfield_integer),
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"pointer",
          .member_name_len = 7,
          .has_bitfield = 1,
          .bit_width = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE,
            boundary.status);

  psx_aggregate_layout_state_t bitfield_sign_layout;
  psx_aggregate_layout_init(
      &bitfield_sign_layout, TK_STRUCT, PSX_RECORD_ID_INVALID);
  const psx_type_t *canonical_enum = resolve_tag_base_for_test(
      TK_ENUM, (char *)"E", 1);
  ASSERT_TRUE(canonical_enum != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_enum->kind);
  ASSERT_EQ(TK_ENUM, canonical_enum->scalar_kind);
  ASSERT_EQ(TK_ENUM, canonical_enum->tag_kind);
  ASSERT_TRUE(ps_type_shape_matches(
      canonical_enum, ps_type_clone(canonical_enum)));
  ASSERT_TRUE(!ps_type_shape_matches(
      canonical_enum,
      ps_type_new_enum((char *)"F", 1, 1, 4)));
  ASSERT_TRUE(!ps_type_shape_matches(canonical_enum, bitfield_integer));
  psx_resolve_aggregate_member_declaration(
      &bitfield_sign_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__SignBoundary",
          .target_tag_name_len = 14,
          .base_type = canonical_enum,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"e",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 2,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_TRUE(!boundary.bit_is_signed);
  psx_resolve_aggregate_member_declaration(
      &bitfield_sign_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__SignBoundary",
          .target_tag_name_len = 14,
          .base_type = ps_type_new_integer(TK_BOOL, 1, 1),
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"b",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_TRUE(!boundary.bit_is_signed);

  psx_type_t *packed_member = ps_type_new_integer(TK_SHORT, 2, 0);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__LayoutBoundary",
          .target_tag_name_len = 16,
          .base_type = packed_member,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"tail",
          .member_name_len = 4,
          .pack_alignment = 2,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(8, boundary.offset);
  ASSERT_EQ(12, psx_aggregate_layout_size(&layout));
  ASSERT_EQ(4, psx_aggregate_layout_alignment(&layout));

  psx_aggregate_layout_init(
      &layout, TK_UNION, PSX_RECORD_ID_INVALID);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_UNION,
          .target_tag_name = (char *)"__UnionBoundary",
          .target_tag_name_len = 15,
          .base_type = bitfield_integer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"i",
          .member_name_len = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(0, boundary.offset);
  psx_resolve_aggregate_member_declaration(
      &layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_UNION,
          .target_tag_name = (char *)"__UnionBoundary",
          .target_tag_name_len = 15,
          .base_type = ps_type_new_integer(TK_LONG, 8, 0),
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"l",
          .member_name_len = 1,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);
  ASSERT_EQ(0, boundary.offset);
  ASSERT_EQ(8, psx_aggregate_layout_size(&layout));
  ASSERT_EQ(8, psx_aggregate_layout_alignment(&layout));

  psx_declarator_shape_t member_shape;
  ps_declarator_shape_init(&member_shape);
  ps_declarator_shape_append_pointer(&member_shape, 0, 0);
  ps_declarator_shape_append_array_ex(&member_shape, 3, 0);
  const psx_type_t *member_type = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = test_semantic_context(),
          .base_type = ps_type_new_integer(TK_INT, 4, 0),
          .declarator_shape = &member_shape,
      });
  ASSERT_TRUE(member_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_type->kind);
  ASSERT_TRUE(member_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, member_type->base->kind);
  ASSERT_EQ(3, member_type->base->array_len);
  ASSERT_TRUE(member_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, member_type->base->base->kind);

  ASSERT_EQ(12,
            ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                member_type));
  ASSERT_EQ(8, ps_type_sizeof(member_type));
  ASSERT_EQ(8, ps_type_alignof_for_target(
                   member_type, ps_ctx_target_info(test_semantic_context())));
  ASSERT_EQ(1, ps_type_pointer_depth(member_type));
  ASSERT_EQ(12, ps_type_sizeof(member_type->base));
  ASSERT_EQ(4, ps_type_sizeof(member_type->base->base));

  psx_declarator_shape_t pointer_array_shape;
  ps_declarator_shape_init(&pointer_array_shape);
  ps_declarator_shape_append_array_ex(&pointer_array_shape, 2, 0);
  ps_declarator_shape_append_pointer(
      &pointer_array_shape, 0, 0);
  ps_declarator_shape_append_array_ex(&pointer_array_shape, 3, 0);
  const psx_type_t *pointer_array_member = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = test_semantic_context(),
          .base_type = ps_type_new_integer(TK_INT, 4, 0),
          .declarator_shape = &pointer_array_shape,
      });
  ASSERT_TRUE(pointer_array_member != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, pointer_array_member->kind);
  ASSERT_EQ(2, pointer_array_member->array_len);
  ASSERT_EQ(16, ps_type_sizeof(pointer_array_member));
  ASSERT_EQ(PSX_TYPE_POINTER, pointer_array_member->base->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, pointer_array_member->base->base->kind);
  ASSERT_EQ(3, pointer_array_member->base->base->array_len);
  ASSERT_EQ(12, ps_type_sizeof(pointer_array_member->base->base));

  psx_tag_declaration_resolution_t tag_resolution;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .local_registry = test_local_registry(),
          .kind = TK_STRUCT,
          .name = (char *)"__AlignedMember",
          .name_len = 15,
          .mode = PSX_TAG_DECLARATION_DEFINITION,
          .size = 12,
          .alignment = 4,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  const psx_type_t *aligned_member = resolve_tag_base_for_test(
      TK_STRUCT, (char *)"__AlignedMember", 15);
  ASSERT_TRUE(aligned_member != NULL);
  ASSERT_EQ(0, ps_type_sizeof(aligned_member));
  ASSERT_EQ(12, ps_ctx_type_sizeof_in(
                    test_semantic_context(), aligned_member));
  ASSERT_EQ(4, ps_ctx_type_alignof_in(
                   test_semantic_context(), aligned_member));

  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .local_registry = test_local_registry(),
          .kind = TK_STRUCT,
          .name = (char *)"__IncompleteMember",
          .name_len = 18,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  const psx_type_t *incomplete_base = resolve_tag_base_for_test(
      TK_STRUCT, (char *)"__IncompleteMember", 18);
  ASSERT_TRUE(incomplete_base != NULL);
  psx_aggregate_layout_state_t constraint_layout;
  psx_aggregate_layout_init(
      &constraint_layout, TK_STRUCT, PSX_RECORD_ID_INVALID);
  psx_resolve_aggregate_member_declaration(
      &constraint_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__ConstraintBoundary",
          .target_tag_name_len = 20,
          .base_type = incomplete_base,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"incomplete",
          .member_name_len = 10,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE, boundary.status);
  psx_declarator_shape_t incomplete_pointer_shape;
  ps_declarator_shape_init(&incomplete_pointer_shape);
  ps_declarator_shape_append_pointer(
      &incomplete_pointer_shape, 0, 0);
  const psx_type_t *incomplete_pointer = psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .semantic_context = test_semantic_context(),
          .base_type = incomplete_base,
          .declarator_shape = &incomplete_pointer_shape,
      });
  psx_resolve_aggregate_member_declaration(
      &constraint_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__ConstraintBoundary",
          .target_tag_name_len = 20,
          .base_type = incomplete_pointer,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"pointer",
          .member_name_len = 7,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, boundary.status);

  psx_type_t *function_member = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0));
  psx_resolve_aggregate_member_declaration(
      &constraint_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"__ConstraintBoundary",
          .target_tag_name_len = 20,
          .base_type = function_member,
          .declarator_shape = &boundary_shape,
          .member_name = (char *)"function",
          .member_name_len = 8,
      },
      &boundary);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_FUNCTION_TYPE, boundary.status);

  psx_type_t *bool_base = ps_type_new_integer(TK_BOOL, 1, 1);
  ps_type_add_qualifiers(bool_base, PSX_TYPE_QUALIFIER_ATOMIC);
  ASSERT_TRUE(bool_base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_base->kind);
  ASSERT_TRUE(ps_type_is_unsigned(bool_base));
  ASSERT_TRUE(ps_type_has_qualifier(bool_base, PSX_TYPE_QUALIFIER_ATOMIC));
  ASSERT_EQ(TK_BOOL, bool_base->scalar_kind);

  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_declarator_shape_t transaction_shape;
  ps_declarator_shape_init(&transaction_shape);
  psx_aggregate_layout_state_t transaction_layout;
  psx_aggregate_layout_init(
      &transaction_layout, TK_STRUCT, PSX_RECORD_ID_INVALID);
  psx_aggregate_member_declaration_resolution_t transaction;
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = integer,
          .declarator_shape = &transaction_shape,
          .member_name = (char *)"a",
          .member_name_len = 1,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, transaction.status);
  ASSERT_EQ(0, transaction.offset);
  ASSERT_EQ(4, transaction.storage_size);
  ASSERT_EQ(1, transaction.registered_member_count);
  ASSERT_TRUE(transaction.type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, transaction.type->kind);
  tag_member_info_t transaction_member_before = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_at_scope_in(test_semantic_context(),
      TK_STRUCT, (char *)"Txn", 3, 0, (char *)"a", 1,
      &transaction_member_before));
  const psx_type_t *transaction_type_before =
      ps_tag_member_decl_type(&transaction_member_before);
  tag_member_info_t replacement_member = {
      .name = (char *)"a",
      .len = 1,
      .offset = 99,
      .bit_width = 7,
      .bit_offset = 3,
      .decl_type = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
  };
  int replacement_created = 1;
  ASSERT_TRUE(!test_semantic_register_tag_member(
      TK_STRUCT, (char *)"Txn", 3, &replacement_member,
      &replacement_created));
  ASSERT_EQ(0, replacement_created);
  tag_member_info_t transaction_member_after = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_at_scope_in(test_semantic_context(),
      TK_STRUCT, (char *)"Txn", 3, 0, (char *)"a", 1,
      &transaction_member_after));
  ASSERT_TRUE(ps_tag_member_decl_type(&transaction_member_after) ==
              transaction_type_before);
  ASSERT_EQ(0, transaction_member_after.offset);
  ASSERT_EQ(0, transaction_member_after.bit_width);
  ASSERT_EQ(0, transaction_member_after.bit_offset);
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = integer,
          .declarator_shape = &transaction_shape,
          .member_name = (char *)"b",
          .member_name_len = 1,
          .has_bitfield = 1,
          .bit_width = 3,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, transaction.status);
  ASSERT_EQ(4, transaction.offset);
  ASSERT_EQ(0, transaction.bit_offset);
  ASSERT_TRUE(transaction.bit_is_signed);
  ASSERT_EQ(1, transaction.registered_member_count);
  int transaction_size_before_duplicate =
      psx_aggregate_layout_size(&transaction_layout);
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = integer,
          .declarator_shape = &transaction_shape,
          .member_name = (char *)"a",
          .member_name_len = 1,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_DUPLICATE, transaction.status);
  ASSERT_EQ(1, transaction.conflicting_name_len);
  ASSERT_EQ(transaction_size_before_duplicate,
            psx_aggregate_layout_size(&transaction_layout));
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = integer,
          .declarator_shape = &transaction_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_MISSING_NAME, transaction.status);

  psx_declarator_shape_t unnamed_pointer_shape;
  ps_declarator_shape_init(&unnamed_pointer_shape);
  ps_declarator_shape_append_pointer(
      &unnamed_pointer_shape, 0, 0);
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = aligned_member,
          .declarator_shape = &unnamed_pointer_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_MISSING_NAME, transaction.status);
  ASSERT_EQ(transaction_size_before_duplicate,
            psx_aggregate_layout_size(&transaction_layout));

  psx_declarator_shape_t unnamed_array_shape;
  ps_declarator_shape_init(&unnamed_array_shape);
  ps_declarator_shape_append_array_ex(&unnamed_array_shape, 2, 0);
  psx_resolve_aggregate_member_declaration(
      &transaction_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"Txn",
          .target_tag_name_len = 3,
          .base_type = aligned_member,
          .declarator_shape = &unnamed_array_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_MISSING_NAME, transaction.status);
  ASSERT_EQ(transaction_size_before_duplicate,
            psx_aggregate_layout_size(&transaction_layout));

  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"__MemberBoundary", 16,
      (char *)"x", 1, 4, integer, 3, 5, 1));
  tag_member_info_t resolved_named = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_at_scope_in(test_semantic_context(),
      TK_STRUCT, (char *)"__MemberBoundary", 16, 0,
      (char *)"x", 1, &resolved_named));
  ASSERT_EQ(4, resolved_named.offset);
  ASSERT_EQ(3, resolved_named.bit_width);
  ASSERT_EQ(5, resolved_named.bit_offset);
  ASSERT_TRUE(resolved_named.bit_is_signed);
  ASSERT_TRUE(resolved_named.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, resolved_named.decl_type->kind);
  ASSERT_TRUE(!register_boundary_tag_member(
      TK_STRUCT, (char *)"__MemberBoundary", 16,
      (char *)"x", 1, 4, integer, 0, 0, 0));

  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"__MemberBoundary", 16,
      (char *)"p", 1, 8, member_type, 0, 0, 0));
  tag_member_info_t resolved_pointer = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_at_scope_in(test_semantic_context(),
      TK_STRUCT, (char *)"__MemberBoundary", 16, 0,
      (char *)"p", 1, &resolved_pointer));
  const psx_type_t *resolved_pointer_type =
      ps_tag_member_decl_type(&resolved_pointer);
  ASSERT_EQ(12, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    resolved_pointer_type));
  ASSERT_EQ(12, ps_type_subscript_static_stride(resolved_pointer_type));
  ASSERT_EQ(4, ps_type_pointer_view_structural_base_deref_size(
                   resolved_pointer_type));

  for (int i = 0; i < 2; i++) {
    ASSERT_TRUE(register_boundary_tag_member(
        TK_STRUCT, (char *)"__MemberBoundary", 16,
        (char *)"", 0, 4 + i * 4, integer, 0, 0, 0));
  }

  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .local_registry = test_local_registry(),
          .kind = TK_STRUCT,
          .name = (char *)"PromSrc",
          .name_len = 7,
          .mode = PSX_TAG_DECLARATION_DEFINITION,
          .member_count = 1,
          .size = 8,
          .alignment = 4,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .local_registry = test_local_registry(),
          .kind = TK_STRUCT,
          .name = (char *)"PromDst",
          .name_len = 7,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"PromSrc", 7,
      (char *)"b", 1, 4, integer, 3, 2, 1));
  psx_aggregate_layout_state_t promoted_layout;
  psx_aggregate_layout_init(
      &promoted_layout, TK_STRUCT, PSX_RECORD_ID_INVALID);
  psx_declarator_shape_t promoted_shape;
  ps_declarator_shape_init(&promoted_shape);
  psx_resolve_aggregate_member_declaration(
      &promoted_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"PromDst",
          .target_tag_name_len = 7,
          .base_type = ps_type_new_integer(TK_LONG, 8, 0),
          .declarator_shape = &promoted_shape,
          .member_name = (char *)"prefix",
          .member_name_len = 6,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, transaction.status);
  psx_resolve_aggregate_member_declaration(
      &promoted_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"PromDst",
          .target_tag_name_len = 7,
          .base_type = ps_type_new_tag(
              TK_STRUCT, (char *)"PromSrc", 7, 1, 8),
          .declarator_shape = &promoted_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_OK, transaction.status);
  ASSERT_EQ(2, transaction.registered_member_count);
  tag_member_info_t promoted = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"PromDst", 7, (char *)"b", 1, &promoted));
  ASSERT_EQ(12, promoted.offset);
  ASSERT_EQ(3, promoted.bit_width);
  ASSERT_EQ(2, promoted.bit_offset);
  ASSERT_TRUE(promoted.bit_is_signed);
  ASSERT_TRUE(promoted.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, promoted.decl_type->kind);

  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .local_registry = test_local_registry(),
          .kind = TK_STRUCT,
          .name = (char *)"BatchSrc",
          .name_len = 8,
          .mode = PSX_TAG_DECLARATION_DEFINITION,
          .member_count = 2,
          .size = 8,
          .alignment = 4,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .local_registry = test_local_registry(),
          .kind = TK_STRUCT,
          .name = (char *)"BatchDst",
          .name_len = 8,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag_resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag_resolution.status);
  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"BatchSrc", 8,
      (char *)"a", 1, 0, integer, 0, 0, 0));
  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"BatchSrc", 8,
      (char *)"b", 1, 4, integer, 0, 0, 0));
  ASSERT_TRUE(register_boundary_tag_member(
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"b", 1, 0, integer, 0, 0, 0));

  tag_member_info_t absent_promoted = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"b", 1, &absent_promoted));
  ASSERT_TRUE(!ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"a", 1, &absent_promoted));

  psx_aggregate_layout_state_t anonymous_layout;
  psx_aggregate_layout_init(
      &anonymous_layout, TK_STRUCT, PSX_RECORD_ID_INVALID);
  psx_declarator_shape_t anonymous_shape;
  ps_declarator_shape_init(&anonymous_shape);
  psx_type_t *batch_source_type =
      ps_type_new_tag(TK_STRUCT, (char *)"BatchSrc", 8, 1, 8);
  psx_resolve_aggregate_member_declaration(
      &anonymous_layout,
      &(psx_aggregate_member_declaration_request_t){
          .semantic_context = test_semantic_context(),
          .target_tag_kind = TK_STRUCT,
          .target_tag_name = (char *)"BatchDst",
          .target_tag_name_len = 8,
          .base_type = batch_source_type,
          .declarator_shape = &anonymous_shape,
      },
      &transaction);
  ASSERT_EQ(PSX_AGGREGATE_MEMBER_DUPLICATE, transaction.status);
  ASSERT_EQ(1, transaction.conflicting_name_len);
  ASSERT_EQ(0, psx_aggregate_layout_size(&anonymous_layout));
  ASSERT_TRUE(!ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"a", 1, &absent_promoted));
  ASSERT_TRUE(!ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)"BatchDst", 8,
      (char *)"", 0, &absent_promoted));
}

static void test_static_assert_resolution_boundary() {
  printf("test_static_assert_resolution_boundary...\n");
  psx_static_assert_resolution_t resolution;
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){.is_constant = 1, .value = 1},
      &resolution);
  ASSERT_EQ(PSX_STATIC_ASSERT_OK, resolution.status);
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){.is_constant = 1, .value = 0},
      &resolution);
  ASSERT_EQ(PSX_STATIC_ASSERT_FAILED, resolution.status);
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){.is_constant = 0, .value = 1},
      &resolution);
  ASSERT_EQ(PSX_STATIC_ASSERT_NOT_CONSTANT, resolution.status);
}

static void test_typedef_declaration_resolution_boundary() {
  printf("test_typedef_declaration_resolution_boundary...\n");
  reset_test_translation_unit_state();
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_typedef_declaration_resolution_request_t request = {
      .semantic_context = test_semantic_context(),
      .global_registry = test_global_registry(),
      .local_registry = test_local_registry(),
      .name = (char *)"__TypeBoundary",
      .name_len = 14,
      .type = integer,
  };
  psx_typedef_declaration_resolution_t resolution;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.created);
  ASSERT_EQ(0, resolution.scope_depth);
  const psx_type_t *canonical_typedef_type = NULL;
  ASSERT_TRUE(ps_ctx_find_typedef_decl_type_in(test_semantic_context(),
      request.name, request.name_len, &canonical_typedef_type));
  ASSERT_TRUE(canonical_typedef_type != NULL);

  request.type = ps_type_new_integer(TK_INT, 4, 0);
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.redeclared);
  const psx_type_t *redeclared_typedef_type = NULL;
  ASSERT_TRUE(ps_ctx_find_typedef_decl_type_in(test_semantic_context(),
      request.name, request.name_len, &redeclared_typedef_type));
  ASSERT_TRUE(redeclared_typedef_type == canonical_typedef_type);

  psx_type_t *long_type = ps_type_new_integer(TK_LONG, 8, 0);
  request.type = long_type;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT, resolution.status);

  ps_ctx_enter_block_scope_in(test_semantic_context());
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.created);
  ASSERT_EQ(1, resolution.scope_depth);
  ps_ctx_leave_block_scope_in(test_semantic_context());

  psx_global_object_result_t object;
  ASSERT_TRUE(lower_global_object_declaration(
      &(psx_global_object_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__TypeObject",
          .name_len = 12,
          .type = integer,
      },
      &object));
  request.name = (char *)"__TypeObject";
  request.name_len = 12;
  request.type = integer;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT,
            resolution.status);

  psx_function_declaration_resolution_t function_resolution;
  psx_type_t *function_type =
      ps_type_new_function(ps_type_clone(integer));
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__TypeFunction",
          .name_len = 14,
          .function_type = function_type,
      },
      &function_resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, function_resolution.status);
  request.name = (char *)"__TypeFunction";
  request.name_len = 14;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT,
            resolution.status);

  ASSERT_TRUE(test_semantic_define_enum_const(
      (char *)"__TypeEnum", 10, 1));
  request.name = (char *)"__TypeEnum";
  request.name_len = 10;
  psx_resolve_typedef_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT,
            resolution.status);
}

static void test_enum_constant_resolution_boundary() {
  printf("test_enum_constant_resolution_boundary...\n");
  reset_test_translation_unit_state();
  psx_enum_constant_resolution_request_t request = {
      .semantic_context = test_semantic_context(),
      .global_registry = test_global_registry(),
      .local_registry = test_local_registry(),
      .name = (char *)"__EnumBoundary",
      .name_len = 14,
      .value = 7,
  };
  psx_enum_constant_resolution_t resolution;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OK, resolution.status);
  ASSERT_TRUE(resolution.created);
  ASSERT_EQ(0, resolution.scope_depth);
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_DUPLICATE, resolution.status);

  ps_ctx_enter_block_scope_in(test_semantic_context());
  request.value = 11;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OK, resolution.status);
  ASSERT_TRUE(resolution.created);
  ASSERT_EQ(1, resolution.scope_depth);
  long long value = 0;
  ASSERT_TRUE(ps_ctx_find_enum_const_in(test_semantic_context(),
      (char *)"__EnumBoundary", 14, &value));
  ASSERT_EQ(11, value);
  ps_ctx_leave_block_scope_in(test_semantic_context());
  ASSERT_TRUE(ps_ctx_find_enum_const_in(test_semantic_context(),
      (char *)"__EnumBoundary", 14, &value));
  ASSERT_EQ(7, value);

  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_global_object_result_t object;
  ASSERT_TRUE(lower_global_object_declaration(
      &(psx_global_object_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__EnumObject",
          .name_len = 12,
          .type = integer,
      },
      &object));
  request.name = (char *)"__EnumObject";
  request.name_len = 12;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT, resolution.status);

  psx_function_declaration_resolution_t function_resolution;
  psx_type_t *function_type =
      ps_type_new_function(ps_type_clone(integer));
  psx_resolve_function_declaration(
      &(psx_function_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .name = (char *)"__EnumFunction",
          .name_len = 14,
          .function_type = function_type,
      },
      &function_resolution);
  ASSERT_EQ(PSX_FUNCTION_DECLARATION_OK, function_resolution.status);
  request.name = (char *)"__EnumFunction";
  request.name_len = 14;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT, resolution.status);

  psx_typedef_declaration_resolution_t typedef_resolution;
  psx_resolve_typedef_declaration(
      &(psx_typedef_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = (char *)"__EnumType",
          .name_len = 10,
          .type = integer,
      },
      &typedef_resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK, typedef_resolution.status);
  request.name = (char *)"__EnumType";
  request.name_len = 10;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT, resolution.status);

  ps_ctx_enter_block_scope_in(test_semantic_context());
  ps_decl_enter_scope_in(test_local_registry());
  ASSERT_TRUE(register_test_storage_fixture(
      (char *)"__EnumLocal", 11, 4, 4, 0) != NULL);
  request.name = (char *)"__EnumLocal";
  request.name_len = 11;
  psx_resolve_enum_constant(&request, &resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT, resolution.status);
  ps_decl_leave_scope_in(test_local_registry());
  ps_ctx_leave_block_scope_in(test_semantic_context());
}

static void test_initializer_resolution_boundary() {
  printf("test_initializer_resolution_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *array = ps_type_new_array(integer, 2, 8, 0);
  tag_member_info_t members[2] = {
      {.name = (char *)"x", .len = 1, .offset = 40,
       .decl_type = integer},
      {.name = (char *)"a", .len = 1, .offset = 44,
       .decl_type = array},
  };
  psx_aggregate_definition_t definition = {
      .record_id = 0x1a11u,
      .tag_kind = TK_STRUCT,
      .tag_name = (char *)"InitBoundary",
      .tag_len = 12,
      .member_count = 2,
      .members = members,
  };
  psx_type_t *aggregate = ps_type_new_tag(
      TK_STRUCT, definition.tag_name, definition.tag_len, 0, 12);
  aggregate->record_id = definition.record_id;
  aggregate->aggregate_definition = &definition;
  ASSERT_TRUE(define_test_record_decl(&definition));
  psx_type_id_t aggregate_type_id = ps_ctx_intern_qual_type_in(
      test_semantic_context(), aggregate).type_id;

  psx_record_layout_table_t *record_layouts =
      psx_record_layout_table_create();
  ASSERT_TRUE(record_layouts != NULL);
  const psx_record_member_layout_t aggregate_members[2] = {
      {.offset = 0}, {.offset = 4},
  };
  ASSERT_TRUE(psx_record_layout_table_define(
      record_layouts, definition.record_id,
      ps_ctx_target_info(test_semantic_context()), 12, 4,
      aggregate_members, 2));

  psx_initializer_scalar_leaf_list_t leaves = {0};
  ASSERT_TRUE(psx_collect_initializer_scalar_leaves_with_records(
      ps_ctx_semantic_type_table_in(test_semantic_context()),
      ps_ctx_record_decl_table_in(test_semantic_context()),
      record_layouts,
      ps_ctx_target_info(test_semantic_context()),
      aggregate_type_id, 0, &leaves));
  ASSERT_EQ(3, leaves.count);
  ASSERT_EQ(0, leaves.items[0].relative_offset);
  ASSERT_EQ(4, leaves.items[1].relative_offset);
  ASSERT_EQ(8, leaves.items[2].relative_offset);
  ASSERT_TRUE(ps_type_unqualified_semantic_matches(
      integer, leaves.items[2].type));
  ASSERT_TRUE(leaves.items[2].type_id != PSX_TYPE_ID_INVALID);

  node_num_t index = {0};
  index.base.kind = ND_NUM;
  index.val = 1;
  psx_initializer_entry_t entry = {0};
  entry.designator_count = 2;
  entry.designators[0] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_MEMBER,
      .member_name = (char *)"a",
      .member_len = 1,
  };
  entry.designators[1] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_INDEX,
      .index_expr = (node_t *)&index,
  };
  psx_initializer_target_t target =
      psx_resolve_initializer_designator_path_with_records(
          ps_ctx_diagnostics(test_semantic_context()),
          ps_ctx_semantic_type_table_in(test_semantic_context()),
          ps_ctx_record_decl_table_in(test_semantic_context()),
          record_layouts,
          ps_ctx_target_info(test_semantic_context()),
          &entry, aggregate_type_id, 0, NULL);
  ASSERT_TRUE(ps_type_unqualified_semantic_matches(integer, target.type));
  ASSERT_EQ(8, target.relative_offset);
  ASSERT_EQ(1, target.first_member_index);
  ASSERT_EQ(1, target.first_array_index);
  ASSERT_EQ(leaves.items[2].type_id, target.type_id);
  ASSERT_EQ(3, psx_initializer_leaf_cursor_after_target_with_records(
                   ps_ctx_semantic_type_table_in(test_semantic_context()),
                   record_layouts,
                   ps_ctx_target_info(test_semantic_context()),
                   &leaves, &target));
  psx_initializer_scalar_leaf_list_dispose(&leaves);

  psx_type_t *recursive = ps_type_new_tag(
      TK_STRUCT, (char *)"RecursiveInit", 13, 0, 16);
  tag_member_info_t recursive_members[2] = {
      {.name = (char *)"next", .len = 4, .offset = 0,
       .decl_type = ps_type_new_pointer(recursive)},
      {.name = (char *)"value", .len = 5, .offset = 8,
       .decl_type = integer},
  };
  psx_aggregate_definition_t recursive_definition = {
      .record_id = 0x1a12u,
      .tag_kind = TK_STRUCT,
      .tag_name = (char *)"RecursiveInit",
      .tag_len = 13,
      .member_count = 2,
      .members = recursive_members,
  };
  recursive->record_id = recursive_definition.record_id;
  recursive->aggregate_definition = &recursive_definition;
  ASSERT_TRUE(define_test_record_decl(&recursive_definition));
  psx_type_id_t recursive_type_id = ps_ctx_intern_qual_type_in(
      test_semantic_context(), recursive).type_id;
  const psx_record_member_layout_t recursive_layout_members[2] = {
      {.offset = 0}, {.offset = 8},
  };
  ASSERT_TRUE(psx_record_layout_table_define(
      record_layouts, recursive_definition.record_id,
      ps_ctx_target_info(test_semantic_context()), 16, 8,
      recursive_layout_members, 2));
  ASSERT_TRUE(psx_collect_initializer_scalar_leaves_with_records(
      ps_ctx_semantic_type_table_in(test_semantic_context()),
      ps_ctx_record_decl_table_in(test_semantic_context()),
      record_layouts,
      ps_ctx_target_info(test_semantic_context()),
      recursive_type_id, 0, &leaves));
  ASSERT_EQ(2, leaves.count);
  ASSERT_EQ(PSX_TYPE_POINTER, leaves.items[0].type->kind);
  ASSERT_EQ(8, leaves.items[1].relative_offset);
  psx_initializer_scalar_leaf_list_dispose(&leaves);
  psx_record_layout_table_destroy(record_layouts);
}

static void test_local_initializer_parse_lowering_boundary() {
  printf("test_local_initializer_parse_lowering_boundary...\n");
  reset_test_locals();

  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *function_pointer =
      test_function_pointer(integer, NULL, 0, 0);
  tag_member_info_t members[2] = {
      {.name = (char *)"fn", .len = 2, .offset = 0,
       .decl_type = function_pointer},
      {.name = (char *)"value", .len = 5, .offset = 8,
       .decl_type = integer},
  };
  psx_aggregate_definition_t definition = {
      .record_id = 0x1a20u,
      .tag_kind = TK_STRUCT,
      .tag_name = (char *)"LocalInitBoundary",
      .tag_len = 17,
      .is_complete = 1,
      .member_count = 2,
      .members = members,
  };
  psx_type_t *aggregate = ps_type_new_tag(
      TK_STRUCT, definition.tag_name, definition.tag_len, 0, 16);
  aggregate->aggregate_definition = &definition;
  aggregate->record_id = definition.record_id;
  ASSERT_TRUE(define_test_record_decl(&definition));
  const psx_record_member_layout_t aggregate_layout_members[2] = {
      {.offset = 0}, {.offset = 8},
  };
  ASSERT_TRUE(psx_record_layout_table_define(
      (psx_record_layout_table_t *)ps_ctx_record_layout_table_in(
          test_semantic_context()),
      definition.record_id, ps_ctx_target_info(test_semantic_context()),
      16, 8, aggregate_layout_members, 2));

  lvar_t *object = register_test_storage_fixture(
      (char *)"object", 6, 16, 16, 0);
  set_test_storage_fixture_type(object, aggregate);
  token_t *tokens = tk_tokenize((char *)"{0, 7}");
  tk_set_current_token(tokens);

  node_t *raw = parse_test_initializer_for_var(object);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_LIST,
            ((node_decl_init_t *)raw)->init_kind);
  ASSERT_EQ(ND_INIT_LIST, raw->rhs->kind);
  ASSERT_EQ(2, ((node_init_list_t *)raw->rhs)->entry_count);
  ASSERT_EQ(TK_EOF, tk_get_current_token()->kind);

  raw = analyze_test_expression(raw, NULL);
  ASSERT_TRUE(raw->kind != ND_DECL_INIT);

  lvar_t *source = register_test_storage_fixture(
      (char *)"source", 6, 16, 16, 0);
  set_test_storage_fixture_type(source, aggregate);
  tokens = tk_tokenize((char *)"source");
  tk_set_current_token(tokens);
  raw = parse_test_initializer_for_var(object);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_EXPR,
            ((node_decl_init_t *)raw)->init_kind);
  ASSERT_EQ(ND_IDENTIFIER, raw->rhs->kind);
  raw = analyze_test_expression(raw, NULL);
  ASSERT_EQ(ND_ASSIGN, raw->kind);
  ASSERT_TRUE(raw->is_decl_initializer);

  tag_member_info_t union_members[1] = {
      {.name = (char *)"value", .len = 5, .offset = 0,
       .decl_type = integer},
  };
  psx_aggregate_definition_t union_definition = {
      .record_id = 0x1a21u,
      .tag_kind = TK_UNION,
      .tag_name = (char *)"LocalUnionBoundary",
      .tag_len = 18,
      .is_complete = 1,
      .member_count = 1,
      .members = union_members,
  };
  psx_type_t *union_type = ps_type_new_tag(
      TK_UNION, union_definition.tag_name, union_definition.tag_len, 0, 4);
  union_type->aggregate_definition = &union_definition;
  union_type->record_id = union_definition.record_id;
  ASSERT_TRUE(define_test_record_decl(&union_definition));
  const psx_record_member_layout_t union_layout_members[1] = {
      {.offset = 0},
  };
  ASSERT_TRUE(psx_record_layout_table_define(
      (psx_record_layout_table_t *)ps_ctx_record_layout_table_in(
          test_semantic_context()),
      union_definition.record_id,
      ps_ctx_target_info(test_semantic_context()), 4, 4,
      union_layout_members, 1));
  lvar_t *union_object = register_test_storage_fixture(
      (char *)"u", 1, 4, 4, 0);
  set_test_storage_fixture_type(union_object, union_type);
  tokens = tk_tokenize((char *)"9");
  tk_set_current_token(tokens);
  raw = parse_test_initializer_for_var(union_object);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_EXPR,
            ((node_decl_init_t *)raw)->init_kind);
  raw = analyze_test_expression(raw, NULL);
  ASSERT_EQ(ND_ASSIGN, raw->kind);
  ASSERT_TRUE(raw->is_decl_initializer);
  ASSERT_EQ(9, as_num(raw->rhs)->val);

  lvar_t *scalar = register_test_storage_fixture(
      (char *)"scalar", 6, 4, 4, 0);
  set_test_storage_fixture_type(scalar, integer);
  tokens = tk_tokenize((char *)"{7,}");
  tk_set_current_token(tokens);
  raw = parse_test_initializer_for_var(scalar);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_LIST,
            ((node_decl_init_t *)raw)->init_kind);
  ASSERT_EQ(1, ((node_init_list_t *)raw->rhs)->entry_count);
  raw = analyze_test_expression(raw, NULL);
  ASSERT_EQ(ND_ASSIGN, raw->kind);
  ASSERT_EQ(7, as_num(raw->rhs)->val);

  psx_type_t *complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  complex_type->fp_kind = TK_FLOAT_KIND_DOUBLE;
  lvar_t *complex_value = register_test_storage_fixture(
      (char *)"complex", 7, 16, 16, 0);
  set_test_storage_fixture_type(complex_value, complex_type);
  tokens = tk_tokenize((char *)"{3, 4}");
  tk_set_current_token(tokens);
  raw = parse_test_initializer_for_var(complex_value);
  ASSERT_EQ(ND_DECL_INIT, raw->kind);
  ASSERT_EQ(PSX_DECL_INIT_LIST,
            ((node_decl_init_t *)raw)->init_kind);
  ASSERT_EQ(2, ((node_init_list_t *)raw->rhs)->entry_count);
  raw = analyze_test_expression(raw, NULL);
  ASSERT_EQ(ND_COMMA, raw->kind);
  ASSERT_EQ(ND_ASSIGN, raw->lhs->kind);
  ASSERT_EQ(ND_ASSIGN, raw->rhs->kind);
}

static void test_static_data_initializer_boundary() {
  printf("test_static_data_initializer_boundary...\n");
  psx_type_t *integer = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *array = ps_type_new_array(integer, 3, 12, 0);
  node_num_t one = {0};
  one.base.kind = ND_NUM;
  one.val = 1;
  node_num_t two = {0};
  two.base.kind = ND_NUM;
  two.val = 2;
  node_num_t seven = {0};
  seven.base.kind = ND_NUM;
  seven.val = 7;
  psx_initializer_entry_t entries[2] = {
      {.value = (node_t *)&one},
      {.value = (node_t *)&seven, .designator_count = 1},
  };
  entries[1].designators[0] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_INDEX,
      .index_expr = (node_t *)&two,
  };
  node_init_list_t list = {0};
  list.base.kind = ND_INIT_LIST;
  list.entries = entries;
  list.entry_count = 2;
  global_var_t global = {0};
  psx_type_id_t array_type_id = intern_test_type_id(array);
  ASSERT_TRUE(array_type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &global, array));
  ASSERT_EQ(array_type_id, ps_gvar_decl_type_id(&global));
  ASSERT_TRUE(lower_static_scalar_array_initializer(
      test_lowering_context(), &global, array, &list, NULL));
  ASSERT_EQ(3, global.init_count);
  ASSERT_EQ(1, ps_gvar_init_slot_view(&global, 0).value);
  ASSERT_EQ(0, ps_gvar_init_slot_view(&global, 1).value);
  ASSERT_EQ(7, ps_gvar_init_slot_view(&global, 2).value);

  psx_type_t *wide = ps_type_new_integer(TK_LONG, 8, 0);
  psx_type_t *pair = ps_type_new_array(integer, 2, 8, 0);
  tag_member_info_t union_members[2] = {
      {.name = (char *)"raw", .len = 3, .offset = 0,
       .decl_type = wide},
      {.name = (char *)"a", .len = 1, .offset = 0,
       .decl_type = pair},
  };
  psx_aggregate_definition_t union_definition = {
      .record_id = 0xfacdu,
      .tag_kind = TK_UNION,
      .tag_name = (char *)"InitUnion",
      .tag_len = 9,
      .member_count = 2,
      .members = union_members,
  };
  psx_type_t *union_type = ps_type_new_tag(
      TK_UNION, union_definition.tag_name, union_definition.tag_len, 0, 8);
  union_type->aggregate_definition = &union_definition;
  union_type->record_id = union_definition.record_id;
  ASSERT_TRUE(define_test_record_decl(&union_definition));
  const psx_record_member_layout_t union_layout_members[2] = {
      {.offset = 0}, {.offset = 0},
  };
  ASSERT_TRUE(psx_record_layout_table_define(
      (psx_record_layout_table_t *)ps_ctx_record_layout_table_in(
          test_semantic_context()),
      union_definition.record_id,
      ps_ctx_target_info(test_semantic_context()), 8, 8,
      union_layout_members, 2));
  node_num_t union_index = {0};
  union_index.base.kind = ND_NUM;
  union_index.val = 1;
  node_num_t union_value = {0};
  union_value.base.kind = ND_NUM;
  union_value.val = 7;
  psx_initializer_entry_t union_entry = {
      .value = (node_t *)&union_value,
      .designator_count = 2,
  };
  union_entry.designators[0] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_MEMBER,
      .member_name = (char *)"a",
      .member_len = 1,
  };
  union_entry.designators[1] = (psx_initializer_designator_t){
      .kind = PSX_INIT_DESIGNATOR_INDEX,
      .index_expr = (node_t *)&union_index,
  };
  node_init_list_t union_list = {0};
  union_list.base.kind = ND_INIT_LIST;
  union_list.entries = &union_entry;
  union_list.entry_count = 1;
  global_var_t union_global = {0};
  psx_type_id_t union_type_id = intern_test_type_id(union_type);
  ASSERT_TRUE(union_type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &union_global, union_type));
  ASSERT_EQ(union_type_id, ps_gvar_decl_type_id(&union_global));
  ASSERT_TRUE(lower_static_object_initializer(
      test_lowering_context(), &union_global,
      union_type, &union_list, NULL));
  ASSERT_EQ(2, union_global.init_count);
  ASSERT_EQ(0, ps_gvar_init_slot_view(&union_global, 0).value);
  ASSERT_EQ(7, ps_gvar_init_slot_view(&union_global, 1).value);
  ASSERT_EQ(1, ps_gvar_union_init_slot_ordinal(&union_global, 0));

  psx_type_t *incomplete = ps_type_new_array(integer, 0, 0, 0);
  global_var_t inferred_global = {0};
  ASSERT_TRUE(intern_test_type_id(incomplete) != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &inferred_global, incomplete));
  const psx_type_t *inferred_type = ps_gvar_get_decl_type(&inferred_global);
  psx_initializer_entry_t inferred_entries[3] = {
      {.value = (node_t *)&one},
      {.value = (node_t *)&two},
      {.value = (node_t *)&seven},
  };
  node_init_list_t inferred_list = {0};
  inferred_list.base.kind = ND_INIT_LIST;
  inferred_list.entries = inferred_entries;
  inferred_list.entry_count = 3;
  psx_static_initializer_resolution_t inferred_resolution;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .type = inferred_type,
          .kind = PSX_DECL_INIT_LIST,
          .initializer = (node_t *)&inferred_list,
      },
      &inferred_resolution);
  ASSERT_EQ(PSX_STATIC_INITIALIZER_OK, inferred_resolution.status);
  ASSERT_EQ(1, inferred_resolution.type_completed);
  ASSERT_EQ(0, inferred_type->array_len);
  ASSERT_TRUE(inferred_resolution.type != inferred_type);
  ASSERT_EQ(3, inferred_resolution.type->array_len);
  ASSERT_EQ(12, ps_type_sizeof(inferred_resolution.type));
  ASSERT_EQ(0, ps_gvar_decl_sizeof(&inferred_global, 0));

  psx_static_declaration_initializer_result_t inferred_result = {0};
  ASSERT_TRUE(lower_resolved_static_initializer(
      test_global_registry(), test_lowering_context(), &inferred_global,
      &inferred_resolution, NULL,
      &inferred_result));
  ASSERT_EQ(1, inferred_result.type_completed);
  ASSERT_EQ(1, inferred_result.initialized);
  ASSERT_EQ(12, ps_gvar_decl_sizeof(&inferred_global, 0));
  ASSERT_EQ(3, ps_gvar_get_decl_type(&inferred_global)->array_len);
  ASSERT_EQ(3, inferred_global.init_count);
  ASSERT_EQ(7, ps_gvar_init_slot_view(&inferred_global, 2).value);

  psx_type_t *char_type = ps_type_new_integer(TK_CHAR, 1, 0);
  psx_type_t *char_pointer = ps_type_new_pointer(char_type);
  psx_type_t *pointer_array = ps_type_new_array(char_pointer, 0, 0, 0);
  global_var_t pointer_array_global = {0};
  ASSERT_TRUE(intern_test_type_id(pointer_array) != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &pointer_array_global, pointer_array));
  const psx_type_t *pointer_array_type =
      ps_gvar_get_decl_type(&pointer_array_global);
  node_string_t string = {0};
  string.base.kind = ND_STRING;
  string.string_label = (char *)".Lboundary";
  string.char_width = TK_CHAR_WIDTH_CHAR;
  psx_initializer_entry_t pointer_entry = {
      .value = (node_t *)&string,
  };
  node_init_list_t pointer_list = {0};
  pointer_list.base.kind = ND_INIT_LIST;
  pointer_list.entries = &pointer_entry;
  pointer_list.entry_count = 1;
  psx_static_initializer_resolution_t pointer_resolution;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .type = pointer_array_type,
          .kind = PSX_DECL_INIT_LIST,
          .initializer = (node_t *)&pointer_list,
      },
      &pointer_resolution);
  ASSERT_EQ(PSX_STATIC_INITIALIZER_OK, pointer_resolution.status);
  ASSERT_TRUE(lower_resolved_static_initializer(
      test_global_registry(), test_lowering_context(), &pointer_array_global,
      &pointer_resolution, NULL, NULL));
  ASSERT_EQ(0, pointer_array_type->array_len);
  ASSERT_EQ(1, pointer_resolution.type->array_len);
  ASSERT_EQ(1, ps_gvar_get_decl_type(&pointer_array_global)->array_len);
  ASSERT_EQ(8, ps_gvar_decl_sizeof(&pointer_array_global, 0));
  ASSERT_EQ(1, pointer_array_global.init_count);
  psx_gvar_init_slot_t pointer_slot =
      ps_gvar_init_slot_view(&pointer_array_global, 0);
  ASSERT_TRUE(pointer_slot.symbol == string.string_label);
  ASSERT_EQ(-1, pointer_slot.symbol_len);

  reset_test_global_registry_translation_unit();
  reset_test_locals();
  psx_static_local_lowering_reset_in(test_lowering_context());
  psx_type_t *static_incomplete =
      ps_type_new_array(integer, 0, 0, 0);
  psx_static_initializer_resolution_t static_initializer_resolution;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .semantic_context = test_semantic_context(),
          .type = static_incomplete,
          .kind = PSX_DECL_INIT_LIST,
          .initializer = (node_t *)&inferred_list,
      },
      &static_initializer_resolution);
  ASSERT_EQ(PSX_STATIC_INITIALIZER_OK,
            static_initializer_resolution.status);
  psx_static_local_declaration_result_t static_result = {0};
  ASSERT_TRUE(lower_static_local_declaration(
      &(psx_static_local_declaration_request_t){
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .lowering_context = test_lowering_context(),
          .kind = PSX_STATIC_LOCAL_ARRAY,
          .function_name = (char *)"boundary",
          .function_name_len = 8,
          .name = (char *)"values",
          .name_len = 6,
          .type = static_incomplete,
          .initializer_resolution = &static_initializer_resolution,
      },
      &static_result));
  ASSERT_TRUE(static_result.global != NULL);
  ASSERT_TRUE(static_result.alias != NULL);
  ASSERT_EQ(1, static_result.type_completed);
  ASSERT_TRUE(find_test_global_var(
      static_result.alias->static_global_name,
      static_result.alias->static_global_name_len) == static_result.global);
  const psx_type_t *static_global_type =
      ps_gvar_get_decl_type(static_result.global);
  const psx_type_t *static_alias_type =
      ps_lvar_get_decl_type(static_result.alias);
  ASSERT_EQ(0, static_result.alias->size);
  ASSERT_EQ(3, static_global_type->array_len);
  ASSERT_EQ(12, ps_type_sizeof(static_global_type));
  ASSERT_EQ(3, static_alias_type->array_len);
  ASSERT_EQ(12, ps_lvar_decl_sizeof(static_result.alias, 0));
}

static void test_expr_mul_div() {
  printf("test_expr_mul_div...\n");
    node_t *node = parse_expr_input("1 * 2 / 3"); // (1*2)/3

  ASSERT_EQ(ND_DIV, node->kind);
  ASSERT_EQ(ND_MUL, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_mod() {
  printf("test_expr_mod...\n");
    node_t *node = parse_expr_input("10 % 3");

  ASSERT_EQ(ND_MOD, node->kind);
  ASSERT_EQ(10, as_num(node->lhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_precedence() {
  printf("test_expr_precedence...\n");
    node_t *node = parse_expr_input("1 + 2 * 3"); // 1+(2*3)

  ASSERT_EQ(ND_ADD, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(ND_MUL, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs->lhs)->val);
  ASSERT_EQ(3, as_num(node->rhs->rhs)->val);
}

static void test_expr_parentheses() {
  printf("test_expr_parentheses...\n");
    node_t *node = parse_expr_input("(1 + 2) * 3"); // (1+2)*3

  ASSERT_EQ(ND_MUL, node->kind);
  ASSERT_EQ(ND_ADD, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_eq_neq() {
  printf("test_expr_eq_neq...\n");
    node_t *node = parse_expr_input("1 == 2 != 3"); // (1==2)!=3

  ASSERT_EQ(ND_NE, node->kind);
  ASSERT_EQ(ND_EQ, node->lhs->kind);
  ASSERT_EQ(1, as_num(node->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_relational() {
  printf("test_expr_relational...\n");
    node_t *node = parse_expr_input("1 < 2 <= 3 > 4 >= 5");

  // ルートは ND_LE (>= が反転)
  ASSERT_EQ(ND_LE, node->kind);
  ASSERT_EQ(5, as_num(node->lhs)->val); // 5が左辺
  // > が反転 → ND_LT
  ASSERT_EQ(ND_LT, node->rhs->kind);
  ASSERT_EQ(4, as_num(node->rhs->lhs)->val); // 4が左辺
  // <=
  ASSERT_EQ(ND_LE, node->rhs->rhs->kind);
  ASSERT_EQ(3, as_num(node->rhs->rhs->rhs)->val);
  // <
  ASSERT_EQ(ND_LT, node->rhs->rhs->lhs->kind);
  ASSERT_EQ(1, as_num(node->rhs->rhs->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(node->rhs->rhs->lhs->rhs)->val);
}

static void test_expr_logical_and_or() {
  printf("test_expr_logical_and_or...\n");

    node_t *node = parse_expr_input("1 && 0 || 3");
  ASSERT_EQ(ND_LOGOR, node->kind);
  ASSERT_EQ(ND_LOGAND, node->lhs->kind);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_bitwise() {
  printf("test_expr_bitwise...\n");
    node_t *node = parse_expr_input("1 | 2 ^ 3 & 4");

  ASSERT_EQ(ND_BITOR, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(ND_BITXOR, node->rhs->kind);
  ASSERT_EQ(2, as_num(node->rhs->lhs)->val);
  ASSERT_EQ(ND_BITAND, node->rhs->rhs->kind);
}

static void test_expr_shift() {
  printf("test_expr_shift...\n");
    node_t *node = parse_expr_input("1 + 2 << 3 >> 1");

  ASSERT_EQ(ND_SHR, node->kind);
  ASSERT_EQ(ND_SHL, node->lhs->kind);
  ASSERT_EQ(ND_ADD, node->lhs->lhs->kind);
  ASSERT_EQ(1, as_num(node->rhs)->val);

    node_t *promoted_signed_shift = parse_expr_input("(unsigned char)a >> 1");
  ASSERT_EQ(ND_SHR, promoted_signed_shift->kind);
  ASSERT_TRUE(!ps_type_integer_promotion_is_unsigned_for_target(
      ps_node_get_type(promoted_signed_shift->lhs),
      ps_ctx_target_info(test_semantic_context())));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(promoted_signed_shift));

    node_t *promoted_unsigned_shift = parse_expr_input("(unsigned int)a >> 1");
  ASSERT_EQ(ND_SHR, promoted_unsigned_shift->kind);
  ASSERT_TRUE(ps_type_integer_promotion_is_unsigned_for_target(
      ps_node_get_type(promoted_unsigned_shift->lhs),
      ps_ctx_target_info(test_semantic_context())));
  ASSERT_TRUE(ps_node_shift_operation_is_unsigned(promoted_unsigned_shift));

    node_t *forced_signed_shift = parse_expr_input("(int)(unsigned long)a");
  ASSERT_EQ(ND_CAST, forced_signed_shift->kind);
  ASSERT_EQ(ND_SHR, forced_signed_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_signed_shift->lhs->lhs->kind);
  ASSERT_TRUE(!ps_type_is_unsigned(
      ps_node_get_type(forced_signed_shift->lhs->lhs)));
  ASSERT_TRUE(!ps_type_is_unsigned(
      ps_node_get_type(forced_signed_shift->lhs)));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(forced_signed_shift->lhs->lhs));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(forced_signed_shift->lhs));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(forced_signed_shift));

    node_t *forced_signed_keyword_shift = parse_expr_input("(signed)(unsigned long)a");
  ASSERT_EQ(ND_CAST, forced_signed_keyword_shift->kind);
  ASSERT_EQ(ND_SHR, forced_signed_keyword_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_signed_keyword_shift->lhs->lhs->kind);
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(forced_signed_keyword_shift->lhs->lhs));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(forced_signed_keyword_shift->lhs));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(forced_signed_keyword_shift));

    node_t *forced_unsigned_shift = parse_expr_input("(unsigned)(long)a");
  ASSERT_EQ(ND_CAST, forced_unsigned_shift->kind);
  ASSERT_EQ(ND_SHR, forced_unsigned_shift->lhs->kind);
  ASSERT_EQ(ND_SHL, forced_unsigned_shift->lhs->lhs->kind);
  ASSERT_TRUE(ps_type_is_unsigned(
      ps_node_get_type(forced_unsigned_shift->lhs->lhs)));
  ASSERT_TRUE(ps_type_is_unsigned(
      ps_node_get_type(forced_unsigned_shift->lhs)));
  ASSERT_TRUE(ps_node_shift_operation_is_unsigned(forced_unsigned_shift->lhs->lhs));
  ASSERT_TRUE(ps_node_shift_operation_is_unsigned(forced_unsigned_shift->lhs));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(forced_unsigned_shift));
}

static void test_expr_ternary() {
  printf("test_expr_ternary...\n");
    node_t *node = parse_expr_input("1 ? 2 : 3 ? 4 : 5");

  ASSERT_EQ(ND_TERNARY, node->kind);
  ASSERT_EQ(1, as_num(node->lhs)->val);
  ASSERT_EQ(2, as_num(node->rhs)->val);
  ASSERT_EQ(ND_TERNARY, as_ctrl(node)->els->kind); // 右結合
}

static void test_expr_unary_ops() {
  printf("test_expr_unary_ops...\n");

    node_t *pos = parse_expr_input("+42");
  ASSERT_EQ(ND_NUM, pos->kind);
  ASSERT_EQ(42, as_num(pos)->val);

    node_t *neg = parse_expr_input("-42");
  ASSERT_EQ(ND_SUB, neg->kind);
  ASSERT_EQ(ND_NUM, neg->lhs->kind);
  ASSERT_EQ(0, as_num(neg->lhs)->val);
  ASSERT_EQ(ND_NUM, neg->rhs->kind);
  ASSERT_EQ(42, as_num(neg->rhs)->val);

    node_t *not0 = parse_expr_input("!0");
  ASSERT_EQ(ND_EQ, not0->kind);
  ASSERT_EQ(ND_NUM, not0->lhs->kind);
  ASSERT_EQ(0, as_num(not0->lhs)->val);
  ASSERT_EQ(ND_NUM, not0->rhs->kind);
  ASSERT_EQ(0, as_num(not0->rhs)->val);

  node_t *bitnot = parse_expr_input("~5");
  ASSERT_EQ(ND_SUB, bitnot->kind);               // (~5) == ((0-5)-1)
  ASSERT_EQ(ND_SUB, bitnot->lhs->kind);
  ASSERT_EQ(1, as_num(bitnot->rhs)->val);

  node_t *voidcast = parse_expr_input("(void)1");
  ASSERT_EQ(ND_CAST, voidcast->kind);
  ASSERT_TRUE(ps_node_get_type(voidcast)->kind == PSX_TYPE_VOID);
  ASSERT_EQ(ND_NUM, voidcast->lhs->kind);
  ASSERT_EQ(1, as_num(voidcast->lhs)->val);

  node_t *ptr_const_cast = parse_expr_input("(int *)0x1000");
  ASSERT_EQ(ND_CAST, ptr_const_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptr_const_cast));
  ASSERT_EQ(ND_NUM, ptr_const_cast->lhs->kind);
  ASSERT_EQ(0x1000, as_num(ptr_const_cast->lhs)->val);

  node_t *ptrarr_cast = parse_expr_input("(double (*)[2])0");
  ASSERT_EQ(ND_CAST, ptrarr_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptrarr_cast));
  ASSERT_EQ(8, ps_node_type_size(ptrarr_cast));
  ASSERT_EQ(16, ps_node_deref_size(ptrarr_cast));
  ASSERT_EQ(8, canonical_node_base_deref_size(ptrarr_cast));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, canonical_node_pointee_fp_kind(ptrarr_cast));
  ASSERT_TRUE(ps_node_get_type(ptrarr_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptrarr_cast)->kind);
  ASSERT_TRUE(ps_node_get_type(ptrarr_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr_cast)->base->kind);

  node_t *ptrptr_cast = parse_expr_input("(int **)0");
  ASSERT_EQ(ND_CAST, ptrptr_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptrptr_cast));
  ASSERT_EQ(8, ps_node_deref_size(ptrptr_cast));
  ASSERT_TRUE(ps_node_get_type(ptrptr_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptrptr_cast)->kind);
  ASSERT_EQ(2, canonical_node_pointer_qual_levels(ptrptr_cast));
  ASSERT_TRUE(ps_node_get_type(ptrptr_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptrptr_cast)->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(ps_node_get_type(ptrptr_cast)->base));
  ASSERT_TRUE(ps_node_get_type(ptrptr_cast)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(ptrptr_cast)->base->base->kind);

  node_t *long_ptrarr_cast = parse_expr_input("(long (*)[2])0");
  ASSERT_EQ(ND_CAST, long_ptrarr_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(long_ptrarr_cast));
  ASSERT_EQ(16, ps_node_deref_size(long_ptrarr_cast));
  ASSERT_TRUE(ps_node_get_type(long_ptrarr_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(long_ptrarr_cast)->kind);
  ASSERT_TRUE(ps_node_get_type(long_ptrarr_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(long_ptrarr_cast)->base->kind);
  ASSERT_TRUE(ps_node_get_type(long_ptrarr_cast)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(long_ptrarr_cast)->base->base->kind);

  node_t *long_ptr_elem_cast = parse_expr_input("(long * (*)[2])0");
  ASSERT_EQ(ND_CAST, long_ptr_elem_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(long_ptr_elem_cast));
  ASSERT_EQ(16, ps_node_deref_size(long_ptr_elem_cast));
  ASSERT_TRUE(ps_node_get_type(long_ptr_elem_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(long_ptr_elem_cast)->kind);
  ASSERT_TRUE(ps_node_get_type(long_ptr_elem_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(long_ptr_elem_cast)->base->kind);
  ASSERT_TRUE(ps_node_get_type(long_ptr_elem_cast)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(long_ptr_elem_cast)->base->base->kind);

  node_t *ptr_elem_2d_cast = parse_expr_input("(int * (*)[2][3])0");
  ASSERT_EQ(ND_CAST, ptr_elem_2d_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptr_elem_2d_cast));
  ASSERT_EQ(8, ps_node_type_size(ptr_elem_2d_cast));
  ASSERT_EQ(48, ps_node_deref_size(ptr_elem_2d_cast));
  ASSERT_EQ(4, canonical_node_base_deref_size(ptr_elem_2d_cast));
  ASSERT_TRUE(ps_node_get_type(ptr_elem_2d_cast) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptr_elem_2d_cast)->kind);
  ASSERT_TRUE(ps_node_get_type(ptr_elem_2d_cast)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptr_elem_2d_cast)->base->kind);
  ASSERT_TRUE(ps_node_get_type(ptr_elem_2d_cast)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptr_elem_2d_cast)->base->base->kind);
  ASSERT_TRUE(ps_node_get_type(ptr_elem_2d_cast)->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_node_get_type(ptr_elem_2d_cast)->base->base->base->kind);
  int ptr_elem_2d_inner =
      canonical_node_array_subscript_stride_bytes(ptr_elem_2d_cast, 0);
  int ptr_elem_2d_next =
      canonical_node_array_subscript_stride_bytes(ptr_elem_2d_cast, 1);
  ASSERT_EQ(24, ptr_elem_2d_inner);
  ASSERT_EQ(8, ptr_elem_2d_next);

  node_t *uchar_ptrarr_cast = parse_expr_input("(unsigned char (*)[3])0");
  ASSERT_EQ(ND_CAST, uchar_ptrarr_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(uchar_ptrarr_cast));
  ASSERT_EQ(3, ps_node_deref_size(uchar_ptrarr_cast));
  ASSERT_EQ(1, canonical_node_base_deref_size(uchar_ptrarr_cast));
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(uchar_ptrarr_cast));

  node_t *bool_ptrarr_cast = parse_expr_input("(_Bool (*)[2])0");
  ASSERT_EQ(ND_CAST, bool_ptrarr_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(bool_ptrarr_cast));
  ASSERT_EQ(2, ps_node_deref_size(bool_ptrarr_cast));
  ASSERT_EQ(1, canonical_node_base_deref_size(bool_ptrarr_cast));
  ASSERT_TRUE(canonical_node_pointee_is_bool(bool_ptrarr_cast));

  node_t *bool_ptr_cast = parse_expr_input("(_Bool *)0");
  ASSERT_EQ(ND_CAST, bool_ptr_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(bool_ptr_cast));
  ASSERT_TRUE(canonical_node_pointee_is_bool(bool_ptr_cast));
  ASSERT_EQ(1, ps_node_deref_size(bool_ptr_cast));

  node_t *boolcast = parse_expr_input("(_Bool)3");
  ASSERT_EQ(ND_NE, boolcast->kind);
  ASSERT_EQ(ND_NUM, boolcast->rhs->kind);
  ASSERT_EQ(0, as_num(boolcast->rhs)->val);

    node_t *const_cast = parse_expr_input("(const int)7");
  ASSERT_EQ(ND_NUM, const_cast->kind);
  ASSERT_EQ(7, as_num(const_cast)->val);

    node_t *volatile_cast = parse_expr_input("(volatile int)8");
  ASSERT_EQ(ND_NUM, volatile_cast->kind);
  ASSERT_EQ(8, as_num(volatile_cast)->val);

    node_t *post_const_cast = parse_expr_input("(int const)12");
  ASSERT_EQ(ND_NUM, post_const_cast->kind);
  ASSERT_EQ(12, as_num(post_const_cast)->val);

    node_t *post_dup_const_cast = parse_expr_input("(int const const)21");
  ASSERT_EQ(ND_NUM, post_dup_const_cast->kind);
  ASSERT_EQ(21, as_num(post_dup_const_cast)->val);

    node_t *multi_ptr_qual_cast = parse_expr_input("(int const * volatile * restrict)0");
  ASSERT_EQ(ND_CAST, multi_ptr_qual_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(multi_ptr_qual_cast));
  ASSERT_EQ(ND_NUM, multi_ptr_qual_cast->lhs->kind);
  ASSERT_EQ(0, as_num(multi_ptr_qual_cast->lhs)->val);

    node_t *unsigned_int_const_cast = parse_expr_input("(unsigned int const)13");
  ASSERT_EQ(ND_NUM, unsigned_int_const_cast->kind);
  ASSERT_EQ(13, as_num(unsigned_int_const_cast)->val);

    node_t *funcptr_const_cast = parse_expr_input("(int (*const)(int))0");
  ASSERT_EQ(ND_CAST, funcptr_const_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(funcptr_const_cast));
  ASSERT_EQ(ND_NUM, funcptr_const_cast->lhs->kind);
  ASSERT_EQ(0, as_num(funcptr_const_cast->lhs)->val);

    node_t *long_long_cast = parse_expr_input("(long long)14");
  ASSERT_EQ(ND_NUM, long_long_cast->kind);
  ASSERT_EQ(14, as_num(long_long_cast)->val);

    node_t *unsigned_long_cast = parse_expr_input("(unsigned long)15");
  ASSERT_EQ(ND_NUM, unsigned_long_cast->kind);
  ASSERT_EQ(15, as_num(unsigned_long_cast)->val);

  node_t *long_unsigned_int_cast = parse_expr_input("(long)(unsigned int)a");
  ASSERT_EQ(ND_CAST, long_unsigned_int_cast->kind);
  ASSERT_TRUE(long_unsigned_int_cast->widen_zext_i64);
  ASSERT_TRUE(ps_node_i64_widen_source_is_unsigned(long_unsigned_int_cast->lhs));

  node_t *long_signed_int_cast = parse_expr_input("(long)(int)a");
  ASSERT_EQ(ND_CAST, long_signed_int_cast->kind);
  ASSERT_TRUE(!long_signed_int_cast->widen_zext_i64);
  ASSERT_TRUE(!ps_node_i64_widen_source_is_unsigned(long_signed_int_cast->lhs));

    // 定数の short/char キャストは目的幅へ切り詰めて ND_NUM へ定数畳み込みする
    // (16/17/18 は範囲内なので値は不変)。
    node_t *unsigned_short_int_cast = parse_expr_input("(unsigned short int)16");
  ASSERT_EQ(ND_NUM, unsigned_short_int_cast->kind);
  ASSERT_EQ(16, as_num(unsigned_short_int_cast)->val);

    node_t *signed_char_cast = parse_expr_input("(signed char)17");
  ASSERT_EQ(ND_NUM, signed_char_cast->kind);
  ASSERT_EQ(17, as_num(signed_char_cast)->val);

    node_t *unsigned_char_cast = parse_expr_input("(unsigned char)18");
  ASSERT_EQ(ND_NUM, unsigned_char_cast->kind);
  ASSERT_EQ(18, as_num(unsigned_char_cast)->val);

  node_t *long_unsigned_char_cast = parse_expr_input("(long)(unsigned char)a");
  ASSERT_EQ(ND_CAST, long_unsigned_char_cast->kind);
  ASSERT_TRUE(long_unsigned_char_cast->widen_zext_i64);
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(long_unsigned_char_cast->lhs));

  node_t *long_unsigned_short_cast = parse_expr_input("(long)(unsigned short)a");
  ASSERT_EQ(ND_CAST, long_unsigned_short_cast->kind);
  ASSERT_TRUE(long_unsigned_short_cast->widen_zext_i64);
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(long_unsigned_short_cast->lhs));

  node_t *unsigned_signed_short_cast =
      parse_expr_input("(unsigned)(short)a");
  ASSERT_EQ(ND_CAST, unsigned_signed_short_cast->kind);
  ASSERT_EQ(ND_BITAND, unsigned_signed_short_cast->lhs->kind);
  ASSERT_TRUE(!ps_type_is_unsigned(
      ps_node_get_type(unsigned_signed_short_cast->lhs)));
  ASSERT_TRUE(ps_type_is_unsigned(
      ps_node_get_type(unsigned_signed_short_cast)));
  ASSERT_TRUE(ps_node_usual_arith_is_unsigned(
      unsigned_signed_short_cast));

  parsed_code = parse_program_input(
      "int cast_unsigned_short_compare(short s) { return (unsigned)s > 5; }");
  node_t *unsigned_short_return =
      as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, unsigned_short_return->kind);
  ASSERT_EQ(ND_LT, unsigned_short_return->lhs->kind);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      unsigned_short_return->lhs->rhs));
  ASSERT_TRUE(ps_node_is_unsigned_type(
      unsigned_short_return->lhs->rhs));
  ASSERT_TRUE(ps_node_usual_arith_is_unsigned(
      unsigned_short_return->lhs));

  node_t *long_signed_short_cast = parse_expr_input("(long)(short)a");
  ASSERT_EQ(ND_CAST, long_signed_short_cast->kind);
  ASSERT_TRUE(!long_signed_short_cast->widen_zext_i64);
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(long_signed_short_cast->lhs));

    node_t *restrict_ptr_cast = parse_expr_input("(restrict int*)0");
  ASSERT_EQ(ND_CAST, restrict_ptr_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(restrict_ptr_cast));
  ASSERT_EQ(ND_NUM, restrict_ptr_cast->lhs->kind);
  ASSERT_EQ(0, as_num(restrict_ptr_cast->lhs)->val);

    node_t *dup_restrict_ptr_cast = parse_expr_input("(restrict restrict int*)0");
  ASSERT_EQ(ND_CAST, dup_restrict_ptr_cast->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(dup_restrict_ptr_cast));
  ASSERT_EQ(ND_NUM, dup_restrict_ptr_cast->lhs->kind);
  ASSERT_EQ(0, as_num(dup_restrict_ptr_cast->lhs)->val);

    node_t *atomic_cast = parse_expr_input("(_Atomic int)9");
  ASSERT_EQ(ND_NUM, atomic_cast->kind);
  ASSERT_EQ(9, as_num(atomic_cast)->val);

    node_t *atomic_const_cast = parse_expr_input("(_Atomic const int)10");
  ASSERT_EQ(ND_NUM, atomic_const_cast->kind);
  ASSERT_EQ(10, as_num(atomic_const_cast)->val);

    node_t *nested_atomic_cast = parse_expr_input("(_Atomic(_Atomic(int)))11");
  ASSERT_EQ(ND_NUM, nested_atomic_cast->kind);
  ASSERT_EQ(11, as_num(nested_atomic_cast)->val);

  parsed_code = parse_program_input("int main() { struct S { int x; }; struct S a={1}, b={2}; int c=1; struct S s=c?a:(struct S){3}; return s.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; }; struct S a={1}, b={2}; int c=1; struct S s=(c?(struct S){3}:b); return s.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; }; struct S a={1}; struct S s=(a,(struct S){9}); return s.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; }; struct S a={1}, b={2}; int c=1; struct S t=(struct S)(c?a:b); return t.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U a={.x=1}, b={.x=2}; int c=1; union U t=(union U)(c?a:b); return t.x; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
}

static void test_expr_generic() {
  printf("test_expr_generic...\n");

  parsed_code = parse_program_input(
      "typedef double ExprCanonicalParam; "
      "int main(void){ return _Generic("
      "(int (*)(ExprCanonicalParam, int *, ...))0, "
      "int (*)(ExprCanonicalParam, int *, ...): 53, default: 7); }");
  node_t *canonical_generic_return =
      as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, canonical_generic_return->kind);
  ASSERT_EQ(53, as_num(canonical_generic_return->lhs)->val);
  node_t *canonical_expr_funcptr = parse_expr_input_with_existing_locals(
      "(int (*)(ExprCanonicalParam, int *, ...))0");
  ASSERT_EQ(ND_CAST, canonical_expr_funcptr->kind);
  ASSERT_TRUE(canonical_expr_funcptr->type == NULL);
  ASSERT_TRUE(ps_node_get_type(canonical_expr_funcptr) == NULL);
  canonical_expr_funcptr = analyze_test_expression(
      canonical_expr_funcptr, NULL);
  const psx_type_t *canonical_expr_function =
      ps_type_derived_function(ps_node_get_type(canonical_expr_funcptr));
  ASSERT_TRUE(canonical_expr_function != NULL);
  ASSERT_EQ(2, canonical_expr_function->param_count);
  ASSERT_TRUE(canonical_expr_function->is_variadic_function);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            canonical_expr_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            canonical_expr_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER,
            canonical_expr_function->param_types[1]->kind);

  parsed_code = parse_program_input(
      "typedef int (*unary_fn)(int); int generic_id(int x){return x;} "
      "int main(){return 0;}");
  psx_typedef_info_t unary_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), (char *)"unary_fn", 8, &unary_info));
  const psx_type_t *generic_id_type =
      ps_ctx_get_function_type_in(test_semantic_context(), (char *)"generic_id", 10);
  ASSERT_TRUE(generic_id_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, generic_id_type->kind);
  psx_type_t *generic_id_pointer =
      ps_type_new_pointer(ps_type_clone(generic_id_type));
  const psx_type_t *unary_type = ps_ctx_typedef_decl_type(&unary_info);
  ASSERT_TRUE(unary_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, unary_type->kind);
  ASSERT_TRUE(ps_type_derived_function(unary_type) != NULL);
  assert_canonical_type_signature(generic_id_pointer, "p<i32(i32)>");
  assert_canonical_type_signature(unary_type, "p<i32(i32)>");
  ASSERT_TRUE(ps_type_generic_matches(generic_id_pointer, unary_type));
  node_t *generic_id_ref = parse_expr_input("generic_id");
  const psx_type_t *generic_id_ref_type = ps_node_get_type(generic_id_ref);
  ASSERT_TRUE(generic_id_ref_type != NULL);
  assert_canonical_type_signature(generic_id_ref_type, "p<i32(i32)>");
  ASSERT_TRUE(ps_type_generic_matches(generic_id_ref_type, unary_type));
  parsed_code = parse_program_input(
      "typedef int (*unary_fn)(int); int id(int x){return x;} "
      "int main(){return _Generic(id, unary_fn:9, int:4, default:5);}");
  node_t *typedef_funcref_return =
      as_block(as_function_definition(parsed_code[1])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, typedef_funcref_return->kind);
  ASSERT_EQ(ND_NUM, typedef_funcref_return->lhs->kind);
  ASSERT_EQ(9, as_num(typedef_funcref_return->lhs)->val);

    node_t *g1 = parse_expr_input("_Generic(1, int: 11, default: 22)");
  ASSERT_EQ(ND_NUM, g1->kind);
  ASSERT_EQ(11, as_num(g1)->val);

    node_t *g2 = parse_expr_input("_Generic(1.0, float: 11, double: 33, default: 22)");
  ASSERT_EQ(ND_NUM, g2->kind);
  ASSERT_EQ(33, as_num(g2)->val);

  node_t *generic_atomic_scalar =
      parse_expr_input("_Generic(1, _Atomic(int): 41, default: 42)");
  ASSERT_EQ(ND_NUM, generic_atomic_scalar->kind);
  expect_parse_ok(
      "int main(){ int *p=0; return _Generic(p, _Atomic(int *):1, default:2); }");
  expect_parse_ok(
      "int main(){ return _Generic(1, _Atomic(_Atomic(int)):1, default:2); }");

  parsed_code = parse_program_input("int main() { int *p=0; return _Generic(p, int*: 3, default: 7); }");
  node_t *ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(3, as_num(ret->lhs)->val);

  parsed_code = parse_program_input(
      "typedef int (*fp_t)(int); "
      "int f(int x){ return x; } "
      "int main(){ fp_t p=f; return _Generic(p, int (*)(int): 13, default: 7); }");
  node_t *ret_fp = as_block(as_function_definition(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_fp->kind);
  ASSERT_EQ(ND_NUM, ret_fp->lhs->kind);
  ASSERT_EQ(13, as_num(ret_fp->lhs)->val);

  parsed_code = parse_program_input(
      "double fd(double x){ return x; } "
      "int main(){ return _Generic(fd, double (*)(double): 17, default: 7); }");
  node_t *ret_func_designator = as_block(as_function_definition(parsed_code[1])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_func_designator->kind);
  ASSERT_EQ(ND_NUM, ret_func_designator->lhs->kind);
  ASSERT_EQ(17, as_num(ret_func_designator->lhs)->val);

  parsed_code = parse_program_input(
      "int fg(int x){ return x; } "
      "int main(){ int (*p)(int)=fg; return _Generic((p), int (*)(int): 19, default: 7); }");
  node_t *ret_parenthesized_fp = as_block(as_function_definition(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_parenthesized_fp->kind);
  ASSERT_EQ(ND_NUM, ret_parenthesized_fp->lhs->kind);
  ASSERT_EQ(19, as_num(ret_parenthesized_fp->lhs)->val);

  parsed_code = parse_program_input(
      "int (*__tm_gen_rowfn(void))[3] { return 0; } "
      "int main(){ int (*(*p)(void))[3]=__tm_gen_rowfn; "
      "return _Generic((p), int (*(*)(void))[3]: 23, default: 7); }");
  node_t *ret_parenthesized_nested_fp =
      as_block(as_function_definition(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_parenthesized_nested_fp->kind);
  ASSERT_EQ(ND_NUM, ret_parenthesized_nested_fp->lhs->kind);
  ASSERT_EQ(23, as_num(ret_parenthesized_nested_fp->lhs)->val);

  parsed_code = parse_program_input(
      "int (*__tm_gen_growfn(void))[3] { return 0; } "
      "int (*(*__tm_gen_gfp)(void))[3]; "
      "int main(){ return _Generic((__tm_gen_gfp), int (*(*)(void))[3]: 29, default: 7); }");
  node_t *ret_parenthesized_global_nested_fp =
      as_block(as_function_definition(parsed_code[1])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_parenthesized_global_nested_fp->kind);
  ASSERT_EQ(ND_NUM, ret_parenthesized_global_nested_fp->lhs->kind);
  ASSERT_EQ(29, as_num(ret_parenthesized_global_nested_fp->lhs)->val);

  reset_test_locals();
  char synthetic_nested_name[] = "p";
  psx_type_t *synthetic_nested_array = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0);
  psx_type_t *synthetic_nested_return =
      ps_type_new_pointer(synthetic_nested_array);
  psx_type_t *synthetic_nested_type =
      test_function_pointer(synthetic_nested_return, NULL, 0, 0);
  lvar_t *synthetic_nested = register_test_typed_storage_fixture(
      synthetic_nested_name, 1, 8, 0, synthetic_nested_type);
  ASSERT_TRUE(synthetic_nested != NULL);
  node_t *ret_structural_nested_fp = parse_analyzed_expr_input_with_existing_locals(
      "_Generic(p, int (*(*)(void))[3]: 31, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_nested_fp->kind);
  ASSERT_EQ(31, as_num(ret_structural_nested_fp)->val);

  reset_test_locals();
  char synthetic_ret_funcptr_name[] = "q";
  psx_type_t *synthetic_ret_funcptr_param =
      ps_type_new_integer(TK_INT, 4, 0);
  const psx_type_t *synthetic_ret_funcptr_params[] = {
      synthetic_ret_funcptr_param};
  psx_type_t *synthetic_returned_funcptr = test_function_pointer(
      ps_type_new_integer(TK_INT, 4, 0), synthetic_ret_funcptr_params, 1, 0);
  psx_type_t *synthetic_ret_funcptr_type =
      test_function_pointer(synthetic_returned_funcptr, NULL, 0, 0);
  lvar_t *synthetic_ret_funcptr = register_test_typed_storage_fixture(
      synthetic_ret_funcptr_name, 1, 8, 0,
      synthetic_ret_funcptr_type);
  const psx_type_t *synthetic_ret_funcptr_ty =
      ps_lvar_get_decl_type(synthetic_ret_funcptr);
  ASSERT_TRUE(synthetic_ret_funcptr_ty != NULL);
  assert_canonical_type_signature(
      synthetic_ret_funcptr_ty, "p<p<i32(i32)>()>");
  node_t *synthetic_ret_funcptr_ref =
      psx_node_new_lvar_identifier_ref_for(synthetic_ret_funcptr);
  assert_canonical_type_signature(
      ps_node_get_type(synthetic_ret_funcptr_ref), "p<p<i32(i32)>()>");
  node_t *ret_structural_ret_funcptr = parse_analyzed_expr_input_with_existing_locals(
      "_Generic(q, int (*(*)(void))(int): 37, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_ret_funcptr->kind);
  ASSERT_EQ(37, as_num(ret_structural_ret_funcptr)->val);
  node_t *ret_structural_ret_funcptr_nomatch = parse_analyzed_expr_input_with_existing_locals(
      "_Generic(q, int (*(*)(void))(double): 41, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_ret_funcptr_nomatch->kind);
  ASSERT_EQ(7, as_num(ret_structural_ret_funcptr_nomatch)->val);
  node_t *ret_structural_ret_funcptr_ret_nomatch = parse_analyzed_expr_input_with_existing_locals(
      "_Generic(q, double (*(*)(void))(int): 43, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_ret_funcptr_ret_nomatch->kind);
  ASSERT_EQ(7, as_num(ret_structural_ret_funcptr_ret_nomatch)->val);

  reset_test_locals();
  char synthetic_double_ret_funcptr_name[] = "r";
  psx_type_t *synthetic_double_ret_funcptr_param =
      ps_type_new_integer(TK_INT, 4, 0);
  const psx_type_t *synthetic_double_ret_funcptr_params[] = {
      synthetic_double_ret_funcptr_param};
  psx_type_t *synthetic_double_returned_funcptr = test_function_pointer(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
      synthetic_double_ret_funcptr_params, 1, 0);
  psx_type_t *synthetic_double_ret_funcptr_type =
      test_function_pointer(synthetic_double_returned_funcptr, NULL, 0, 0);
  lvar_t *synthetic_double_ret_funcptr =
      register_test_typed_storage_fixture(
          synthetic_double_ret_funcptr_name, 1, 8, 0,
          synthetic_double_ret_funcptr_type);
  const psx_type_t *synthetic_double_ret_funcptr_ty =
      ps_lvar_get_decl_type(synthetic_double_ret_funcptr);
  ASSERT_TRUE(synthetic_double_ret_funcptr_ty != NULL);
  assert_canonical_type_signature(
      synthetic_double_ret_funcptr_ty, "p<p<f64(i32)>()>");
  node_t *ret_structural_double_ret_funcptr = parse_analyzed_expr_input_with_existing_locals(
      "_Generic(r, double (*(*)(void))(int): 47, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_double_ret_funcptr->kind);
  ASSERT_EQ(47, as_num(ret_structural_double_ret_funcptr)->val);
  node_t *ret_structural_double_ret_funcptr_nomatch = parse_analyzed_expr_input_with_existing_locals(
      "_Generic(r, int (*(*)(void))(int): 49, default: 7)");
  ASSERT_EQ(ND_NUM, ret_structural_double_ret_funcptr_nomatch->kind);
  ASSERT_EQ(7, as_num(ret_structural_double_ret_funcptr_nomatch)->val);

  expect_parse_ok(
      "int main(){ struct S{int x;}; return _Generic((struct S){1}, struct S: 1, default: 2); }");
  expect_parse_ok(
      "int main(){ union U{int x;}; return _Generic((union U){.x=1}, union U: 1, default: 2); }");
  parsed_code = parse_program_input(
      "int main(){ struct S{int x;}; return _Generic((struct S){1}, struct S: 1, default: 2); }");
  node_t *ret_struct = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_struct->kind);
  ASSERT_EQ(ND_NUM, ret_struct->lhs->kind);
  ASSERT_EQ(1, as_num(ret_struct->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ struct S{int x;}; struct T{int x;}; return _Generic((struct S){1}, struct T: 1, default: 2); }");
  node_t *ret_struct_nomatch = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_struct_nomatch->kind);
  ASSERT_EQ(ND_NUM, ret_struct_nomatch->lhs->kind);
  ASSERT_EQ(2, as_num(ret_struct_nomatch->lhs)->val);
  expect_parse_ok(
      "int main(){ int *p=0; return _Generic(p, int[3]: 1, default: 2); }");
  expect_parse_ok(
      "int main(){ double d=1.0; double *p=&d; return _Generic(*p, double:42, default:99); }");
  expect_parse_ok(
      "int main(){ float f=1.0f; float *p=&f; return _Generic(*p, float:11, default:99); }");
  expect_parse_ok(
      "int main(){ double a[1]={1.0}; double *p=a; return _Generic(p[0], double:42, default:99); }");
  parsed_code = parse_program_input(
      "int main(){ int x=0; char c=0; int *pi=&x; char *pc=&c; return _Generic(pc, int*:1, char*:2, default:3); }");
  node_t *ret_ptr_kind = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[4];
  ASSERT_EQ(ND_RETURN, ret_ptr_kind->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_kind->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_kind->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ double d=1.0; double *pd=&d; return _Generic(pd, int*:1, double*:2, default:3); }");
  node_t *ret_ptr_fp = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_ptr_fp->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_fp->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_fp->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ struct S{int x;}; struct T{int x;}; struct S s={1}; struct S *ps=&s; return _Generic(ps, struct T*:1, struct S*:2, default:3); }");
  node_t *ret_ptr_tag = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[4];
  ASSERT_EQ(ND_RETURN, ret_ptr_tag->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_tag->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_tag->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ int x=0; const int *p=&x; return _Generic(p, int*:1, const int*:2, default:3); }");
  node_t *ret_ptr_const = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_ptr_const->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_const->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_const->lhs)->val);

  parsed_code = parse_program_input(
      "typedef const int *cip_t; int main(){ int x=0; cip_t p=&x; return _Generic(p, int*:1, const int*:2, default:3); }");
  node_t *ret_typedef_const_ptr = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_typedef_const_ptr->kind);
  ASSERT_EQ(ND_NUM, ret_typedef_const_ptr->lhs->kind);
  ASSERT_EQ(2, as_num(ret_typedef_const_ptr->lhs)->val);

  parsed_code = parse_program_input(
      "typedef volatile int *vip_t; int main(){ int x=0; vip_t p=&x; return _Generic(p, volatile int*:2, int*:1, default:3); }");
  node_t *ret_typedef_volatile_ptr = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_typedef_volatile_ptr->kind);
  ASSERT_EQ(ND_NUM, ret_typedef_volatile_ptr->lhs->kind);
  ASSERT_EQ(2, as_num(ret_typedef_volatile_ptr->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ int x=0; char c=0; int *pi=&x; char *pc=&c; int **ppi=&pi; return _Generic(ppi, char**:1, int**:2, default:3); }");
  node_t *ret_ptr_ptr_kind = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[5];
  ASSERT_EQ(ND_RETURN, ret_ptr_ptr_kind->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_ptr_kind->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_ptr_kind->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ int x=0; unsigned int u=0; unsigned int *pu=&u; return _Generic(pu, int*:1, unsigned int*:2, default:3); }");
  node_t *ret_ptr_unsigned = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[3];
  ASSERT_EQ(ND_RETURN, ret_ptr_unsigned->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_unsigned->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_unsigned->lhs)->val);

  parsed_code = parse_program_input(
      "typedef unsigned int *uip_t; int main(){ unsigned int u=0; uip_t pu=&u; return _Generic(pu, int*:1, unsigned int*:2, default:3); }");
  node_t *ret_ptr_unsigned_typedef = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_ptr_unsigned_typedef->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_unsigned_typedef->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_unsigned_typedef->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ int x=0; int *p=&x; int * const *pp=&p; return _Generic(pp, int**:1, int * const *:2, default:3); }");
  node_t *ret_ptr_level_const = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[3];
  ASSERT_EQ(ND_RETURN, ret_ptr_level_const->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_level_const->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_level_const->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ int x=0; int *p=&x; int * volatile *pp=&p; return _Generic(pp, int**:1, int * volatile *:2, default:3); }");
  node_t *ret_ptr_level_volatile = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[3];
  ASSERT_EQ(ND_RETURN, ret_ptr_level_volatile->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_level_volatile->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_level_volatile->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ unsigned long ul=1; return _Generic(ul, unsigned long:2, unsigned int:1, default:3); }");
  node_t *ret_unsigned_long = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_unsigned_long->kind);
  ASSERT_EQ(ND_NUM, ret_unsigned_long->lhs->kind);
  ASSERT_EQ(2, as_num(ret_unsigned_long->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ long l=1; return _Generic(l, unsigned long:1, long:2, default:3); }");
  node_t *ret_long_signed = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_long_signed->kind);
  ASSERT_EQ(ND_NUM, ret_long_signed->lhs->kind);
  ASSERT_EQ(2, as_num(ret_long_signed->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ return _Generic((1 ? (char)1 : (char)2), char:1, int:2, default:3); }");
  node_t *ret_ternary_promoted_char = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_ternary_promoted_char->kind);
  ASSERT_EQ(ND_NUM, ret_ternary_promoted_char->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ternary_promoted_char->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ return _Generic((1 ? (long double)1.0 : (double)2.0), long double:4, double:5, default:6); }");
  node_t *ret_ternary_long_double = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_ternary_long_double->kind);
  ASSERT_EQ(ND_NUM, ret_ternary_long_double->lhs->kind);
  ASSERT_EQ(4, as_num(ret_ternary_long_double->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ return _Generic((long double){1.0}, long double:4, double:5, default:6); }");
  node_t *ret_compound_long_double = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_compound_long_double->kind);
  ASSERT_EQ(ND_NUM, ret_compound_long_double->lhs->kind);
  ASSERT_EQ(4, as_num(ret_compound_long_double->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ return _Generic((_Complex double)1, double:5, _Complex double:4, default:6); }");
  node_t *ret_complex_cast = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_complex_cast->kind);
  ASSERT_EQ(ND_NUM, ret_complex_cast->lhs->kind);
  ASSERT_EQ(4, as_num(ret_complex_cast->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ _Complex double z=1; return _Generic(z, double:5, _Complex double:4, default:6); }");
  node_t *ret_complex_lvar = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret_complex_lvar->kind);
  ASSERT_EQ(ND_NUM, ret_complex_lvar->lhs->kind);
  ASSERT_EQ(4, as_num(ret_complex_lvar->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ return _Generic(1, int const:2, default:3); }");
  node_t *ret_int_post_const = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret_int_post_const->kind);
  ASSERT_EQ(ND_NUM, ret_int_post_const->lhs->kind);
  ASSERT_EQ(2, as_num(ret_int_post_const->lhs)->val);

  parsed_code = parse_program_input(
      "int main(){ int x=0; int const *p=&x; return _Generic(p, int const *:2, int *:1, default:3); }");
  node_t *ret_ptr_post_const = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[2];
  ASSERT_EQ(ND_RETURN, ret_ptr_post_const->kind);
  ASSERT_EQ(ND_NUM, ret_ptr_post_const->lhs->kind);
  ASSERT_EQ(2, as_num(ret_ptr_post_const->lhs)->val);
}

static void test_expr_sizeof() {
  printf("test_expr_sizeof...\n");

    node_t *n1 = parse_expr_input("sizeof(int)");
  ASSERT_EQ(ND_NUM, n1->kind);
  ASSERT_EQ(4, as_num(n1)->val);

    node_t *n0 = parse_expr_input("sizeof(void)");
  ASSERT_EQ(ND_NUM, n0->kind);
  ASSERT_EQ(1, as_num(n0)->val);

    node_t *n2 = parse_expr_input("sizeof(int*)");
  ASSERT_EQ(ND_NUM, n2->kind);
  ASSERT_EQ(8, as_num(n2)->val);

    node_t *n2q1 = parse_expr_input("sizeof(int * const)");
  ASSERT_EQ(ND_NUM, n2q1->kind);
  ASSERT_EQ(8, as_num(n2q1)->val);

    node_t *n2q2 = parse_expr_input("sizeof(int * volatile)");
  ASSERT_EQ(ND_NUM, n2q2->kind);
  ASSERT_EQ(8, as_num(n2q2)->val);

    node_t *n2q3 = parse_expr_input("sizeof(int * restrict)");
  ASSERT_EQ(ND_NUM, n2q3->kind);
  ASSERT_EQ(8, as_num(n2q3)->val);

    node_t *n2a = parse_expr_input("sizeof(int[10])");
  ASSERT_EQ(ND_NUM, n2a->kind);
  ASSERT_EQ(40, as_num(n2a)->val);

    node_t *n2b = parse_expr_input("sizeof(int (*)[3])");
  ASSERT_EQ(ND_NUM, n2b->kind);
  ASSERT_EQ(8, as_num(n2b)->val);

    node_t *n2c = parse_expr_input("sizeof((int[3]))");
  ASSERT_EQ(ND_NUM, n2c->kind);
  ASSERT_EQ(12, as_num(n2c)->val);

    node_t *n3 = parse_expr_input("sizeof(int (*)(int))");
  ASSERT_EQ(ND_NUM, n3->kind);
  ASSERT_EQ(8, as_num(n3)->val);

    node_t *n4 = parse_expr_input("sizeof(_Complex double)");
  ASSERT_EQ(ND_NUM, n4->kind);
  ASSERT_EQ(16, as_num(n4)->val);

    node_t *n5 = parse_expr_input("sizeof(float _Imaginary)");
  ASSERT_EQ(ND_NUM, n5->kind);
  ASSERT_EQ(8, as_num(n5)->val);

    node_t *a1 = parse_expr_input("_Alignof(int)");
  ASSERT_EQ(ND_NUM, a1->kind);
  ASSERT_EQ(4, as_num(a1)->val);

    node_t *a2 = parse_expr_input("_Alignof(int*)");
  ASSERT_EQ(ND_NUM, a2->kind);
  ASSERT_EQ(8, as_num(a2)->val);

    node_t *a2q1 = parse_expr_input("_Alignof(int * const)");
  ASSERT_EQ(ND_NUM, a2q1->kind);
  ASSERT_EQ(8, as_num(a2q1)->val);

    node_t *a2q2 = parse_expr_input("_Alignof(int * volatile)");
  ASSERT_EQ(ND_NUM, a2q2->kind);
  ASSERT_EQ(8, as_num(a2q2)->val);

    node_t *a2q3 = parse_expr_input("_Alignof(int * restrict)");
  ASSERT_EQ(ND_NUM, a2q3->kind);
  ASSERT_EQ(8, as_num(a2q3)->val);

    node_t *a2a = parse_expr_input("_Alignof(int[10])");
  ASSERT_EQ(ND_NUM, a2a->kind);
  /* 配列のアラインメントは要素のアラインメント (= 4)。sizeof (40) ではない。 */
  ASSERT_EQ(4, as_num(a2a)->val);

    node_t *a2b = parse_expr_input("_Alignof(int (*)[3])");
  ASSERT_EQ(ND_NUM, a2b->kind);
  ASSERT_EQ(8, as_num(a2b)->val);

    node_t *a2c = parse_expr_input("_Alignof((int[3]))");
  ASSERT_EQ(ND_NUM, a2c->kind);
  /* 配列のアラインメントは要素のアラインメント (= 4)、sizeof (12) ではない。 */
  ASSERT_EQ(4, as_num(a2c)->val);

  node_t *a3 = parse_expr_input("_Alignof(_Imaginary double)");
  ASSERT_EQ(ND_NUM, a3->kind);
  ASSERT_EQ(8, as_num(a3)->val);

  parsed_code = parse_program_input("int main() { int x; return sizeof(x); }");
  node_t *ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(4, as_num(ret->lhs)->val);
  expect_parse_ok_without_message("int main(void){ int x; return sizeof(x); }", "W3004");
  expect_parse_ok_without_message("int main(void){ int a[3]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message("int main(void){ int n=3; int a[n]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message("int main(void){ int n=2,m=4; int v[n][m]; int idx=1; return sizeof(v[idx]); }", "W3003");
  expect_parse_ok_without_message("int main(void){ static int a[3]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message("int main(void){ int (*p)[3][4]; return sizeof(*p); }", "W3004");

  parsed_code = parse_program_input("int main() { struct S { int x; }; return sizeof(struct S); }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(4, as_num(ret->lhs)->val);

  parsed_code = parse_program_input(
      "typedef struct Forward Forward; "
      "struct Forward { void *items; int count; int capacity; }; "
      "int main(void) { Forward *body = 0; return sizeof(*body); }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(16, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("int main() { struct S { int x; }; return _Alignof(struct S); }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(4, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("int main() { struct S { int x; }; return sizeof(struct S (*)[3]); }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(8, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("typedef int A3[3]; int main() { return sizeof(A3 (*)[2]); }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(8, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("int main() { struct S { int x; }; return sizeof(struct S[3]); }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(12, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("typedef int A3[3]; int main() { return sizeof(A3[2]); }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(24, as_num(ret->lhs)->val);

    // (char)300: signed char へ切り詰めて ND_NUM へ畳み込む (300 → 44)。
    node_t *c1 = parse_expr_input("(char)300");
  ASSERT_EQ(ND_NUM, c1->kind);
  ASSERT_EQ(44, as_num(c1)->val);

    // 整数リテラルの fp キャストは ND_INT_TO_FP でラップされ、codegen が I2F を発行する。
    node_t *c2 = parse_expr_input("(_Complex double)1");
  ASSERT_EQ(ND_INT_TO_FP, c2->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(c2));
  ASSERT_EQ(ND_NUM, c2->lhs->kind);
  const psx_type_t *c2_ty = ps_node_get_type(c2);
  ASSERT_TRUE(c2_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, c2_ty->kind);
  ASSERT_EQ(16, ps_type_sizeof(c2_ty));
  ASSERT_EQ(16, ps_node_type_size(c2));

    node_t *c3 = parse_expr_input("(float _Imaginary)1");
  ASSERT_EQ(ND_INT_TO_FP, c3->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, ps_node_value_fp_kind(c3));
  ASSERT_EQ(ND_NUM, c3->lhs->kind);
  const psx_type_t *c3_ty = ps_node_get_type(c3);
  ASSERT_TRUE(c3_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, c3_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(c3_ty));
  ASSERT_EQ(8, ps_node_type_size(c3));

    node_t *c4 = parse_expr_input("(long double)1");
  ASSERT_EQ(ND_INT_TO_FP, c4->kind);
  ASSERT_EQ(TK_FLOAT_KIND_LONG_DOUBLE, ps_node_value_fp_kind(c4));
  ASSERT_EQ(ND_NUM, c4->lhs->kind);

    node_t *c5 = parse_expr_input("(_Atomic(int))1");
  ASSERT_EQ(ND_NUM, c5->kind);

    node_t *c6 = parse_expr_input("(_Atomic(int*))0");
  ASSERT_EQ(ND_CAST, c6->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(c6));
  ASSERT_EQ(ND_NUM, c6->lhs->kind);

    node_t *ci = parse_expr_input("(int)a");
  const psx_type_t *ci_ty = ps_node_get_type(ci);
  ASSERT_TRUE(ci_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ci_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(ci_ty));
  ASSERT_TRUE(!ps_type_is_unsigned(ci_ty));
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(ci));

    node_t *cus = parse_expr_input("(unsigned short)a");
  const psx_type_t *cus_ty = ps_node_get_type(cus);
  ASSERT_TRUE(cus_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, cus_ty->kind);
  ASSERT_EQ(2, ps_type_sizeof(cus_ty));
  ASSERT_EQ(2, ps_node_type_size(cus));
  ASSERT_TRUE(ps_type_is_unsigned(cus_ty));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(cus));

    node_t *cul = parse_expr_input("(unsigned long)a");
  const psx_type_t *cul_ty = ps_node_get_type(cul);
  ASSERT_TRUE(cul_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, cul_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(cul_ty));
  ASSERT_EQ(8, ps_node_type_size(cul));
  ASSERT_TRUE(ps_type_is_unsigned(cul_ty));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(cul));

    node_t *cf = parse_expr_input("(float)a");
  const psx_type_t *cf_ty = ps_node_get_type(cf);
  ASSERT_TRUE(cf_ty != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, cf_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(cf_ty));

    node_t *cp = parse_expr_input("(double*)a");
  const psx_type_t *cp_ty = ps_node_get_type(cp);
  ASSERT_TRUE(cp_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, cp_ty->kind);
  ASSERT_EQ(8, ps_node_type_size(cp));
  ASSERT_EQ(8, ps_type_deref_size(cp_ty));
  ASSERT_EQ(8, ps_node_deref_size(cp));
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(cp));
  ASSERT_EQ(8, canonical_node_base_deref_size(cp));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, canonical_node_pointee_fp_kind(cp));
  ASSERT_TRUE(ps_node_value_is_pointer_like(cp));

    node_t *uac_promote = parse_expr_input("(unsigned char)1 + (short)2");
  const psx_type_t *uac_promote_ty = ps_node_get_type(uac_promote);
  ASSERT_TRUE(uac_promote_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_promote_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(uac_promote_ty));
  ASSERT_TRUE(!ps_type_is_unsigned(uac_promote_ty));
  ASSERT_EQ(4, ps_node_type_size(uac_promote));
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(uac_promote));
  ASSERT_TRUE(!ps_node_usual_arith_is_unsigned(uac_promote));

    node_t *uac_signed_wider = parse_expr_input("(unsigned int)1 + (long)-1");
  const psx_type_t *uac_signed_wider_ty = ps_node_get_type(uac_signed_wider);
  ASSERT_TRUE(uac_signed_wider_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_signed_wider_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(uac_signed_wider_ty));
  ASSERT_TRUE(!ps_type_is_unsigned(uac_signed_wider_ty));
  ASSERT_EQ(8, ps_node_type_size(uac_signed_wider));
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(uac_signed_wider));
  ASSERT_TRUE(!ps_node_usual_arith_is_unsigned(uac_signed_wider));

    node_t *uac_unsigned_same_width = parse_expr_input("(unsigned long)1 + (long)-1");
  const psx_type_t *uac_unsigned_same_width_ty =
      ps_node_get_type(uac_unsigned_same_width);
  ASSERT_TRUE(uac_unsigned_same_width_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_unsigned_same_width_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(uac_unsigned_same_width_ty));
  ASSERT_TRUE(ps_type_is_unsigned(uac_unsigned_same_width_ty));
  ASSERT_EQ(8, ps_node_type_size(uac_unsigned_same_width));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(uac_unsigned_same_width));
  ASSERT_TRUE(ps_node_usual_arith_is_unsigned(uac_unsigned_same_width));

    node_t *uac_long_long = parse_expr_input("((unsigned long long)9ULL) ^ ((unsigned short)3)");
  const psx_type_t *uac_long_long_ty = ps_node_get_type(uac_long_long);
  ASSERT_TRUE(uac_long_long_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, uac_long_long_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(uac_long_long_ty));
  ASSERT_TRUE(ps_type_is_unsigned(uac_long_long_ty));
  ASSERT_TRUE(uac_long_long_ty->is_long_long);
  ASSERT_EQ(8, ps_node_type_size(uac_long_long));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(uac_long_long));
  ASSERT_TRUE(ps_node_usual_arith_is_unsigned(uac_long_long));

    node_t *ternary_uac = parse_expr_input("1 ? (unsigned int)1 : (long)-1");
  const psx_type_t *ternary_uac_ty = ps_node_get_type(ternary_uac);
  ASSERT_TRUE(ternary_uac_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ternary_uac_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(ternary_uac_ty));
  ASSERT_TRUE(!ps_type_is_unsigned(ternary_uac_ty));
  ASSERT_EQ(8, ps_node_type_size(ternary_uac));
  ASSERT_TRUE(!ps_node_integer_value_is_unsigned(ternary_uac));
  ASSERT_TRUE(!ps_node_usual_arith_is_unsigned(ternary_uac));

    node_t *cmp_uac = parse_expr_input("(unsigned int)1 < (long)-1");
  ASSERT_TRUE(!ps_node_usual_arith_is_unsigned(cmp_uac));
}

static void test_expr_inc_dec() {
  printf("test_expr_inc_dec...\n");
  reset_test_locals();
  lvar_t *integer = register_test_storage_fixture(
      (char *)"a", 1, 4, 4, 0);
  set_test_storage_fixture_type(
      integer, ps_type_new_integer(TK_INT, 4, 0));

  node_t *prei = parse_expr_input_with_existing_locals("++a");
  ASSERT_EQ(ND_PRE_INC, prei->kind);
  ASSERT_EQ(ND_IDENTIFIER, prei->lhs->kind);
  ASSERT_TRUE(prei->tok != NULL);
  ASSERT_TRUE(prei->type == NULL);
  ASSERT_TRUE(ps_node_get_type(prei) == NULL);
  analyze_test_expression(prei, NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, prei->type->kind);

  node_t *pred = parse_expr_input_with_existing_locals("--a");
  ASSERT_EQ(ND_PRE_DEC, pred->kind);
  ASSERT_EQ(ND_IDENTIFIER, pred->lhs->kind);
  ASSERT_TRUE(pred->type == NULL);
  ASSERT_TRUE(ps_node_get_type(pred) == NULL);
  analyze_test_expression(pred, NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, pred->type->kind);

  node_t *posti = parse_expr_input_with_existing_locals("a++");
  ASSERT_EQ(ND_POST_INC, posti->kind);
  ASSERT_EQ(ND_IDENTIFIER, posti->lhs->kind);
  ASSERT_TRUE(posti->tok != NULL);
  ASSERT_TRUE(posti->type == NULL);
  ASSERT_TRUE(ps_node_get_type(posti) == NULL);
  analyze_test_expression(posti, NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, posti->type->kind);

  node_t *postd = parse_expr_input_with_existing_locals("a--");
  ASSERT_EQ(ND_POST_DEC, postd->kind);
  ASSERT_EQ(ND_IDENTIFIER, postd->lhs->kind);
  ASSERT_TRUE(postd->type == NULL);
  ASSERT_TRUE(ps_node_get_type(postd) == NULL);
  analyze_test_expression(postd, NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, postd->type->kind);
}

static void test_expr_assign() {
  printf("test_expr_assign...\n");
    node_t *node = parse_expr_input("a = 3");

  ASSERT_EQ(ND_ASSIGN, node->kind);
  ASSERT_EQ(ND_LVAR, node->lhs->kind);
  ASSERT_TRUE(as_lvar(node->lhs)->offset >= 0);
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  ASSERT_EQ(3, as_num(node->rhs)->val);
}

static void test_expr_compound_assign() {
  printf("test_expr_compound_assign...\n");

    node_t *add = parse_expr_input("a += 3");
  ASSERT_EQ(ND_ASSIGN, add->kind);
  ASSERT_EQ(ND_ADD, add->rhs->kind);

    node_t *sub = parse_expr_input("a -= 3");
  ASSERT_EQ(ND_ASSIGN, sub->kind);
  ASSERT_EQ(ND_SUB, sub->rhs->kind);

    node_t *mul = parse_expr_input("a *= 3");
  ASSERT_EQ(ND_ASSIGN, mul->kind);
  ASSERT_EQ(ND_MUL, mul->rhs->kind);

    node_t *div = parse_expr_input("a /= 3");
  ASSERT_EQ(ND_ASSIGN, div->kind);
  ASSERT_EQ(ND_DIV, div->rhs->kind);

    node_t *mod = parse_expr_input("a %= 3");
  ASSERT_EQ(ND_ASSIGN, mod->kind);
  ASSERT_EQ(ND_MOD, mod->rhs->kind);

    node_t *shl = parse_expr_input("a <<= 3");
  ASSERT_EQ(ND_ASSIGN, shl->kind);
  ASSERT_EQ(ND_SHL, shl->rhs->kind);

    node_t *shr = parse_expr_input("a >>= 3");
  ASSERT_EQ(ND_ASSIGN, shr->kind);
  ASSERT_EQ(ND_SHR, shr->rhs->kind);

    node_t *band = parse_expr_input("a &= 3");
  ASSERT_EQ(ND_ASSIGN, band->kind);
  ASSERT_EQ(ND_BITAND, band->rhs->kind);

    node_t *bxor = parse_expr_input("a ^= 3");
  ASSERT_EQ(ND_ASSIGN, bxor->kind);
  ASSERT_EQ(ND_BITXOR, bxor->rhs->kind);

    node_t *bor = parse_expr_input("a |= 3");
  ASSERT_EQ(ND_ASSIGN, bor->kind);
  ASSERT_EQ(ND_BITOR, bor->rhs->kind);
}

static void test_expr_comma() {
  printf("test_expr_comma...\n");

    node_t *node = parse_expr_input("a=1, b=2, a+b");
  ASSERT_EQ(ND_COMMA, node->kind);
  ASSERT_EQ(ND_COMMA, node->lhs->kind);
  ASSERT_EQ(ND_ADD, node->rhs->kind);
}

static void test_program_funcdef() {
  printf("test_program_funcdef...\n");
  parsed_code = parse_program_input("int main(void) { int a=1; int b=2; a+b; }");

  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(0, as_function_definition(parsed_code[0])->parameter_count);

  node_t *body = as_function_definition(parsed_code[0])->base.rhs;
  ASSERT_EQ(ND_BLOCK, body->kind);
  ASSERT_EQ(ND_ASSIGN, as_block(body)->body[0]->kind);
  ASSERT_EQ(0, as_lvar(as_block(body)->body[0]->lhs)->offset);
  ASSERT_EQ(ND_ASSIGN, as_block(body)->body[1]->kind);
  ASSERT_EQ(4, as_lvar(as_block(body)->body[1]->lhs)->offset);
  ASSERT_EQ(ND_ADD, as_block(body)->body[2]->kind);
  ASSERT_TRUE(as_block(body)->body[3] == NULL);
  ASSERT_TRUE(parsed_code[1] == NULL);
}

static void test_funcall() {
  printf("test_funcall...\n");
    node_t *node = parse_expr_input("add(1, 2)");

  ASSERT_EQ(ND_FUNCALL, node->kind);
  ASSERT_EQ(2, as_function_call(node)->argument_count);
  ASSERT_EQ(1, as_num(as_function_call(node)->arguments[0])->val);
  ASSERT_EQ(2, as_num(as_function_call(node)->arguments[1])->val);

  node= parse_expr_input("foo((1,2), 3)");
  ASSERT_EQ(ND_FUNCALL, node->kind);
  ASSERT_EQ(2, as_function_call(node)->argument_count);
  ASSERT_EQ(ND_COMMA, as_function_call(node)->arguments[0]->kind);
  ASSERT_EQ(ND_NUM, as_function_call(node)->arguments[1]->kind);
  ASSERT_EQ(3, as_num(as_function_call(node)->arguments[1])->val);

  parsed_code = parse_program_input(
      "int main() { int (*fp)(int); fp(1); }");
  node_t *stmt = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_FUNCALL, stmt->kind);
  ASSERT_EQ(ND_LVAR, as_function_call(stmt)->callee->kind);
  ASSERT_EQ(1, as_function_call(stmt)->argument_count);

  parsed_code = parse_program_input(
      "int inc(int x){ return x + 1; } "
      "int main(void){ int (*fp)(int)=inc; (*(int (*)(int))fp)(1); }");
  node_t *cast_deref_call = as_block(as_function_definition(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_FUNCALL, cast_deref_call->kind);
  ASSERT_EQ(ND_CAST, as_function_call(cast_deref_call)->callee->kind);
  ASSERT_TRUE(ps_type_derived_function(
      ps_node_get_type(as_function_call(cast_deref_call)->callee)) != NULL);
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(as_function_call(cast_deref_call)->callee));

  parsed_code = parse_program_input(
      "int inc(int x){ return x + 1; } "
      "typedef int (*Fn)(int); "
      "int main(void){ int (*fp)(int)=inc; (*(Fn)fp)(1); }");
  node_t *typedef_cast_deref_call = as_block(as_function_definition(parsed_code[1])->base.rhs)->body[1];
  ASSERT_EQ(ND_FUNCALL, typedef_cast_deref_call->kind);
  ASSERT_EQ(ND_CAST, as_function_call(typedef_cast_deref_call)->callee->kind);
  ASSERT_TRUE(ps_type_derived_function(
      ps_node_get_type(as_function_call(typedef_cast_deref_call)->callee)) != NULL);
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(as_function_call(typedef_cast_deref_call)->callee));
}

// --- ここから追加テスト ---

static void test_funcdef_with_params() {
  printf("test_funcdef_with_params...\n");
  parsed_code = parse_program_input("int add(int a, int b) { return a+b; }");

  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(2, as_function_definition(parsed_code[0])->parameter_count);
  ASSERT_EQ(ND_LVAR, as_function_definition(parsed_code[0])->parameters[0]->kind);
  ASSERT_EQ(ND_LVAR, as_function_definition(parsed_code[0])->parameters[1]->kind);

  parsed_code = parse_program_input("int apply(int (*fp)(int), int x) { return x; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(2, as_function_definition(parsed_code[0])->parameter_count);

  parsed_code = parse_program_input("int sum(int a[], int n) { return n; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(2, as_function_definition(parsed_code[0])->parameter_count);

  parsed_code = parse_program_input(
      "int sum_variadic(int first, ...) { return first; }");
  node_function_definition_t *variadic = as_function_definition(parsed_code[0]);
  ASSERT_EQ(ND_FUNCDEF, variadic->base.kind);
  ASSERT_TRUE(variadic->signature != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, variadic->signature->kind);
  ASSERT_TRUE(variadic->signature->is_variadic_function);

  // プロトタイプ宣言では名前なし仮引数を許容
  parsed_code = parse_program_input("int proto(int); int main() { return 0; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(0, as_function_definition(parsed_code[0])->parameter_count);

  parsed_code = parse_program_input(
      "typedef void VoidAlias; int no_args(VoidAlias); "
      "int main(void) { return 0; }");
  const psx_type_t *no_args_type =
      ps_ctx_get_function_type_in(test_semantic_context(), (char *)"no_args", 7);
  ASSERT_TRUE(no_args_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, no_args_type->kind);
  ASSERT_EQ(0, no_args_type->param_count);

  parsed_code = parse_program_input(
      "typedef int (*F0)(); typedef int (*F1)(F0); "
      "int nested(int (int()), F0); "
      "int nested(F1 fp, F0 arg) { return fp(arg); }");
  const psx_type_t *nested_type =
      ps_ctx_get_function_type_in(test_semantic_context(), (char *)"nested", 6);
  ASSERT_TRUE(nested_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, nested_type->kind);
  ASSERT_EQ(2, nested_type->param_count);

  expect_parse_ok(
      "typedef union __attribute__((packed)) AttrUnion { int value; } "
      "AttrUnion; int attr_fn(void) { AttrUnion value; return 0; }");
}

static void test_stmt_if() {
  printf("test_stmt_if...\n");
  parsed_code = parse_program_input("int main() { if (1) 2; }");
  node_t *body = as_function_definition(parsed_code[0])->base.rhs;
  node_t *if_node = as_block(body)->body[0];

  ASSERT_EQ(ND_IF, if_node->kind);
  ASSERT_EQ(ND_NUM, if_node->lhs->kind);  // 条件: 1
  ASSERT_EQ(1, as_num(if_node->lhs)->val);
  ASSERT_EQ(ND_NUM, if_node->rhs->kind);  // then: 2
  ASSERT_EQ(2, as_num(if_node->rhs)->val);
  ASSERT_TRUE(as_ctrl(if_node)->els == NULL);       // else なし
}

static void test_stmt_if_else() {
  printf("test_stmt_if_else...\n");
  parsed_code = parse_program_input("int main() { if (1) 2; else 3; }");
  node_t *if_node = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_IF, if_node->kind);
  ASSERT_EQ(1, as_num(if_node->lhs)->val);        // 条件
  ASSERT_EQ(2, as_num(if_node->rhs)->val);        // then
  ASSERT_EQ(ND_NUM, as_ctrl(if_node)->els->kind);  // else
  ASSERT_EQ(3, as_num(as_ctrl(if_node)->els)->val);
}

static void test_stmt_while() {
  printf("test_stmt_while...\n");
  parsed_code = parse_program_input("int main() { while (1) 2; }");
  node_t *wh = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_WHILE, wh->kind);
  ASSERT_EQ(1, as_num(wh->lhs)->val);   // 条件
  ASSERT_EQ(2, as_num(wh->rhs)->val);   // ループ本体
}

static void test_stmt_do_while() {
  printf("test_stmt_do_while...\n");
  parsed_code = parse_program_input("int main(void) { int a = 0; do a=a+1; while (a<3); }");
  /* body[0] は int a=0 の初期化代入、body[1] が do-while */
  node_t *dw = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];

  ASSERT_EQ(ND_DO_WHILE, dw->kind);
  ASSERT_EQ(ND_ASSIGN, dw->rhs->kind);  // 本体: a=a+1
  ASSERT_EQ(ND_LT, dw->lhs->kind);      // 条件: a<3
}

static void test_stmt_break_continue() {
  printf("test_stmt_break_continue...\n");
  parsed_code = parse_program_input("int main() { while (1) { continue; break; } }");
  node_t *wh = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  node_t *body = wh->rhs;

  ASSERT_EQ(ND_WHILE, wh->kind);
  ASSERT_EQ(ND_BLOCK, body->kind);
  ASSERT_EQ(ND_CONTINUE, as_block(body)->body[0]->kind);
  ASSERT_EQ(ND_BREAK, as_block(body)->body[1]->kind);
}

static void test_stmt_switch_case_default() {
  printf("test_stmt_switch_case_default...\n");
  parsed_code = parse_program_input("int main(void) { int a = 0; switch (a) { case 1: a=2; break; default: a=3; } }");
  /* body[0] は int a = 0、body[1] が switch */
  node_t *sw = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];

  ASSERT_EQ(ND_SWITCH, sw->kind);
  ASSERT_EQ(ND_LVAR, sw->lhs->kind);
  ASSERT_EQ(ND_BLOCK, sw->rhs->kind);
  ASSERT_EQ(ND_CASE, as_block(sw->rhs)->body[0]->kind);
  ASSERT_EQ(1, as_case(as_block(sw->rhs)->body[0])->val);
  ASSERT_EQ(ND_BREAK, as_block(sw->rhs)->body[1]->kind);
  ASSERT_EQ(ND_DEFAULT, as_block(sw->rhs)->body[2]->kind);
}

static void test_stmt_for() {
  printf("test_stmt_for...\n");
  parsed_code = parse_program_input("int main(void) { int a; for (a=0; a<10; a=a+1) a; }");
  /* body[0] は int a; (宣言のみで初期化なし → ND_NUM ダミー)、body[1] が for */
  node_t *fr = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];

  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->init->kind);  // init: a=0
  ASSERT_EQ(ND_LT, fr->lhs->kind);      // 条件: a<10
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->inc->kind);   // inc: a=a+1
  ASSERT_EQ(ND_LVAR, fr->rhs->kind);     // 本体: a
}

static void test_stmt_for_with_decl_init() {
  printf("test_stmt_for_with_decl_init...\n");
  parsed_code = parse_program_input("int main() { for (int i=0; i<3; i=i+1) i; }");
  node_t *fr = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->init->kind);  // init: int i=0
  ASSERT_EQ(ND_LT, fr->lhs->kind);                // 条件: i<3
  ASSERT_EQ(ND_ASSIGN, as_ctrl(fr)->inc->kind);   // inc: i=i+1
  ASSERT_EQ(ND_LVAR, fr->rhs->kind);              // 本体: i

  parsed_code = parse_program_input("int main() { for (int i=0, j=2; i<j; i=i+1) i; }");
  fr = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_FOR, fr->kind);
  ASSERT_EQ(ND_COMMA, as_ctrl(fr)->init->kind);   // init: int i=0, j=2
}

static void test_stmt_return() {
  printf("test_stmt_return...\n");
  parsed_code = parse_program_input("int main() { return 42; }");
  node_t *ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->kind);
  ASSERT_EQ(42, as_num(ret->lhs)->val);

  parsed_code = parse_program_input("void noop() { return; }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_TRUE(ret->lhs == NULL);

  parsed_code = parse_program_input("_Bool flag(void) { return 200; }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_NE, ret->lhs->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->rhs->kind);
  ASSERT_EQ(0, as_num(ret->lhs->rhs)->val);

  parsed_code = parse_program_input("char narrow(int x) { return x; }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_EQ(ND_SHR, ret->lhs->lhs->kind);
  ASSERT_EQ(ND_SHL, ret->lhs->lhs->lhs->kind);
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(ret->lhs->lhs->lhs));
  ASSERT_TRUE(!ps_node_shift_operation_is_unsigned(ret->lhs->lhs));

  parsed_code = parse_program_input("int cast_unsigned_local(void) { unsigned u; return (int)u; }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(ret->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(ret->lhs->lhs));

  parsed_code = parse_program_input("int cast_pointer_int(int *p) { return (int)p; }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(ret->lhs));
  ASSERT_EQ(4, ps_node_type_size(ret->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ret->lhs->lhs));

  parsed_code = parse_program_input("long cast_pointer_long(int *p) { return (long)p; }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(ret->lhs));
  ASSERT_EQ(8, ps_node_type_size(ret->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ret->lhs->lhs));

  parsed_code = parse_program_input("int deref_intptr_cast(long addr) { return *(int *)addr; }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_DEREF, ret->lhs->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ret->lhs->lhs));
  ASSERT_EQ(4, ps_node_deref_size(ret->lhs->lhs));
  ASSERT_EQ(ND_LVAR, ret->lhs->lhs->lhs->kind);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(ret->lhs->lhs->lhs));

  parsed_code = parse_program_input("double void_cast_keeps_operand_fp(double d) { (void)d; return d; }");
  node_block_t *body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_CAST, body->body[0]->kind);
  ASSERT_TRUE(ps_node_get_type(body->body[0])->kind == PSX_TYPE_VOID);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_value_fp_kind(body->body[0]));
  ASSERT_EQ(ND_LVAR, body->body[0]->lhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_value_fp_kind(body->body[0]->lhs));
  ret = body->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_LVAR, ret->lhs->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(ret->lhs));

  parsed_code = parse_program_input("unsigned char unarrow(int x) { return x; }");
  ret = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_EQ(ND_BITAND, ret->lhs->lhs->kind);
  ASSERT_EQ(ND_NUM, ret->lhs->lhs->rhs->kind);
  ASSERT_EQ(0xff, as_num(ret->lhs->lhs->rhs)->val);

  parsed_code = parse_program_input(
      "struct __ret_meta_s { int a; int b; } __ret_meta_struct(void) { "
      "struct __ret_meta_s r; return r; }");
  node_function_definition_t *ret_meta_fn = as_function_definition(parsed_code[0]);
  ret = as_block(ret_meta_fn->base.rhs)->body[1];
  ASSERT_EQ(ND_RETURN, ret->kind);
  const psx_type_t *ret_meta_type =
      ps_function_definition_return_type(ret_meta_fn);
  ASSERT_TRUE(ret_meta_fn->base.type == NULL);
  ASSERT_TRUE(ret_meta_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, ret_meta_type->kind);
  ASSERT_EQ(0, ps_type_sizeof(ret_meta_type));
  ASSERT_EQ(8, ps_ctx_type_sizeof_in(
                   test_semantic_context(), ret_meta_type));
  analyze_test_function((node_t *)ret_meta_fn, NULL);
  ASSERT_TRUE(ret_meta_fn->base.type == NULL);
  ASSERT_TRUE(ret_meta_type ==
              ps_function_definition_return_type(ret_meta_fn));

  parsed_code =
      parse_program_input("_Complex double __ret_meta_complex(void) { return 1; }");
  ret_meta_fn = as_function_definition(parsed_code[0]);
  ret = as_block(ret_meta_fn->base.rhs)->body[0];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ret_meta_type = ps_function_definition_return_type(ret_meta_fn);
  ASSERT_TRUE(ret_meta_fn->base.type == NULL);
  ASSERT_TRUE(ret_meta_type != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, ret_meta_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ret_meta_type->fp_kind);
  analyze_test_function((node_t *)ret_meta_fn, NULL);
  ASSERT_TRUE(ret_meta_fn->base.type == NULL);
  ASSERT_TRUE(ret_meta_type ==
              ps_function_definition_return_type(ret_meta_fn));
}

static void test_stmt_block() {
  printf("test_stmt_block...\n");
  parsed_code = parse_program_input("int main() { { 1; 2; } }");
  node_t *blk = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];

  ASSERT_EQ(ND_BLOCK, blk->kind);
  ASSERT_EQ(ND_NUM, as_block(blk)->body[0]->kind);
  ASSERT_EQ(1, as_num(as_block(blk)->body[0])->val);
  ASSERT_EQ(ND_NUM, as_block(blk)->body[1]->kind);
  ASSERT_EQ(2, as_num(as_block(blk)->body[1])->val);
  ASSERT_TRUE(as_block(blk)->body[2] == NULL);
}

static void test_stmt_goto_label() {
  printf("test_stmt_goto_label...\n");
  parsed_code = parse_program_input("int main() { goto L1; L1: return 42; }");
  node_block_t *body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_GOTO, body->body[0]->kind);
  ASSERT_EQ(ND_LABEL, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->rhs->kind);
}

static void test_expr_deref_addr() {
  printf("test_expr_deref_addr...\n");
  // &a
    node_t *addr = parse_expr_input("&a");
  ASSERT_EQ(ND_ADDR, addr->kind);
  ASSERT_EQ(ND_LVAR, addr->lhs->kind);

  // *a (a は実際に pointer 型として登録する)
  reset_test_locals();
  lvar_t *pointer = register_test_storage_fixture(
      (char *)"a", 1, 8, 4, 0);
  set_test_storage_fixture_type(
      pointer,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0)));
  node_t *deref = parse_expr_input_with_existing_locals("*a");
  ASSERT_EQ(ND_UNARY_DEREF, deref->kind);
  deref = analyze_test_expression(deref, NULL);
  ASSERT_EQ(ND_DEREF, deref->kind);
  ASSERT_EQ(ND_LVAR, deref->lhs->kind);
}

static void test_expr_member_access() {
  printf("test_expr_member_access...\n");
  parsed_code = parse_program_input("int main() { struct S { int a; int b; }; struct S s; s.b = 3; return s.b; }");
  node_block_t *body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  node_t *assign = body->body[2];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  ASSERT_EQ(ND_DEREF, assign->lhs->kind);
  ASSERT_EQ(4, ps_node_type_size(assign->lhs));
  ASSERT_EQ(ND_ADD, assign->lhs->lhs->kind);

  node_t *ret = body->body[3];
  ASSERT_EQ(ND_RETURN, ret->kind);
  ASSERT_EQ(ND_DEREF, ret->lhs->kind);

  parsed_code = parse_program_input(
      "int f(void) { static struct { int n; int m; } s = {3, 4}; "
      "s.n += 1; return s.n + s.m; }");
  node_function_definition_t *fn = as_function_definition(parsed_code[0]);
  lvar_t *anonymous_static = find_func_lvar(fn, "s");
  ASSERT_TRUE(anonymous_static != NULL);
  ASSERT_TRUE(anonymous_static->decl_type != NULL);
  const psx_record_decl_t *anonymous_record = ps_ctx_get_record_decl_in(
      test_semantic_context(),
      ps_type_record_id(anonymous_static->decl_type));
  ASSERT_TRUE(anonymous_record != NULL);
  ASSERT_TRUE(anonymous_record->member_count == 2);
  ASSERT_TRUE(anonymous_record->members[0].name != NULL);
  ASSERT_TRUE(anonymous_record->members[0].len == 1);
  ASSERT_TRUE(memcmp(anonymous_record->members[0].name, "n", 1) == 0);

  parsed_code = parse_program_input(
      "typedef unsigned char u8; "
      "int main() { struct S { u8 a; }; struct S s; return s.a; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      ret = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(ret != NULL);
  ASSERT_EQ(ND_CAST, ret->lhs->kind);
  ASSERT_EQ(ND_DEREF, ret->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(ret->lhs->lhs));

  parsed_code = parse_program_input(
      "typedef unsigned char u8; "
      "int main() { struct S { u8 a; }; struct S s; return (int)s.a; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      ret = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(ret != NULL);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      ret->lhs->kind == ND_CAST ? ret->lhs->lhs : ret->lhs));

  parsed_code = parse_program_input(
      "typedef unsigned char u8; "
      "int main() { struct S { u8 a; }; struct S s; return (signed)s.a; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      ret = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(ret != NULL);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      ret->lhs->kind == ND_CAST ? ret->lhs->lhs : ret->lhs));

  parsed_code = parse_program_input("int main() { struct S { int a; int b; }; struct S s; struct S *p; p=&s; p->b=5; return p->b; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  assign = body->body[4];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  ASSERT_EQ(ND_DEREF, assign->lhs->kind);
  ASSERT_EQ(4, ps_node_type_size(assign->lhs));

  parsed_code = parse_program_input("int main() { struct S { int a[2]; }; struct S s={{1,2}}; return s.a[0]; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_ASSIGN || body->body[1]->kind == ND_COMMA);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input(
      "int first(char *p) { return p[0]; } "
      "int main() { struct C { char x[1]; }; "
      "struct C c = {\"Z\"}; return first(c.x); }");
  body = as_block(as_function_definition(parsed_code[1])->base.rhs);
  ret = body->body[2];
  ASSERT_EQ(ND_RETURN, ret->kind);
  node_t *call = ret->lhs;
  ASSERT_EQ(ND_FUNCALL, call->kind);
  ASSERT_EQ(1, as_function_call(call)->argument_count);
  node_t *array_member = as_function_call(call)->arguments[0];
  ASSERT_EQ(ND_DEREF, array_member->kind);
  ASSERT_TRUE(ps_node_deref_decays_to_address(array_member));
  ASSERT_EQ(1, ps_node_type_size(array_member));
  ASSERT_EQ(1, ps_node_deref_size(array_member));

  parsed_code = parse_program_input(
      "int main(void) { struct B { signed int x:3; }; "
      "struct B b; return b.x; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) ret = body->body[i];
  }
  ASSERT_TRUE(ret != NULL);
  node_t *bitfield = ret->lhs;
  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  ASSERT_TRUE(ps_node_bitfield_info(bitfield, &bit_width, &bit_offset,
                                     &bit_is_signed));
  ASSERT_EQ(3, bit_width);
  ASSERT_TRUE(ps_node_bitfield_info(bitfield, &bit_width, &bit_offset,
                                     &bit_is_signed));
  ASSERT_EQ(3, bit_width);
  ASSERT_EQ(1, bit_is_signed);

  parsed_code = parse_program_input(
      "enum CanonicalBitfieldEnum { CanonicalBitfieldValue }; "
      "typedef enum CanonicalBitfieldEnum CanonicalBitfieldEnumType; "
      "int main(void) { struct B { CanonicalBitfieldEnumType x:3; }; "
      "struct B b; return b.x; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) ret = body->body[i];
  }
  ASSERT_TRUE(ret != NULL);
  bitfield = ret->lhs;
  ASSERT_TRUE(ps_node_bitfield_info(bitfield, &bit_width, &bit_offset,
                                     &bit_is_signed));
  ASSERT_EQ(3, bit_width);
  ASSERT_EQ(0, bit_is_signed);
}

static void test_expr_string() {
  printf("test_expr_string...\n");
    node_t *node = parse_expr_input("\"hello\"");

  ASSERT_EQ(ND_STRING, node->kind);
  ASSERT_TRUE(as_string(node)->string_label != NULL);
  ASSERT_TRUE(node->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, node->type->kind);
  ASSERT_TRUE(node->type->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, node->type->base->kind);
  ASSERT_EQ(TK_CHAR, node->type->base->scalar_kind);
  node_string_t *string = as_string(node);
  ASSERT_EQ(5, string->literal_length);
  ASSERT_TRUE(strncmp(string->literal_contents, "hello", 5) == 0);
  string_lit_t *lit = ps_find_string_lit_by_label_in(
      ag_compilation_session_global_registry(test_suite_session),
      string->string_label);
  ASSERT_TRUE(lit != NULL);
  ASSERT_EQ(5, lit->len);
  ASSERT_TRUE(strncmp(lit->str, "hello", 5) == 0);
}

static void test_expr_concat_string() {
  printf("test_expr_concat_string...\n");
    node_t *node = parse_expr_input("\"he\" \"llo\"");

  ASSERT_EQ(ND_STRING, node->kind);
  node_string_t *string = as_string(node);
  ASSERT_EQ(5, string->literal_length);
  ASSERT_TRUE(strncmp(string->literal_contents, "hello", 5) == 0);
}

static void test_type_decl() {
  printf("test_type_decl...\n");
  // int x = 5; → ND_ASSIGN
  parsed_code = parse_program_input("int main() { int x = 5; }");
  node_t *stmt = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_ASSIGN, stmt->kind);
  ASSERT_EQ(ND_LVAR, stmt->lhs->kind);
  ASSERT_EQ(5, as_num(stmt->rhs)->val);

  // int x; → ND_NUM(0) ダミー
  parsed_code = parse_program_input("int main() { int x; }");
  stmt = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_NUM, stmt->kind);
  ASSERT_EQ(0, as_num(stmt)->val);

  // int a, b=1; → 初期化のある宣言子のみ式木に残る
  parsed_code = parse_program_input("int main() { int a, b=1; }");
  stmt = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_ASSIGN, stmt->kind);
  ASSERT_EQ(ND_NUM, stmt->rhs->kind);
  ASSERT_EQ(1, as_num(stmt->rhs)->val);

  parsed_code = parse_program_input("int main() { int a=1, b=2; }");
  stmt = as_block(as_function_definition(parsed_code[0])->base.rhs)->body[0];
  ASSERT_EQ(ND_COMMA, stmt->kind);

  parsed_code = parse_program_input("int main() { struct S; union U; enum E; return 0; }");
  node_block_t *body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_NUM, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("int main() { struct S; struct S *p; p=0; return p==0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; }; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; } *p; p=0; return p==0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; enum E { A=1, B=2 }; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { enum E { A=1, B, C=10 }; return A+B+C; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { enum E { A=1, B=A+2, C=(B*2)-1 }; return C; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { enum E { A=1, B=~A, C=(A<<3)|2, D=(C&10)^1 }; return D; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { enum E { A=1, B=(A<2), C=(A==1)&&(B||0), D=C?7:9 }; return D; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { unsigned u = 3; _Bool b = 1; signed s = 2; return u+b+s; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("typedef int myint; int main() { myint x = 3; return x; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("typedef int *intptr; int main() { int a=7; intptr p=&a; return *p; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("typedef int (*fp_t)(int); int f(int x){ return x+1; } int main() { fp_t p; return 0; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);

  parsed_code = parse_program_input("typedef int (((*fp_t)))(int); int f(int x){ return x+1; } int main() { fp_t p; return 0; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);

  parsed_code = parse_program_input("extern int a[]; int main(){ return 0; }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  {
    global_var_t *gv = find_test_global_var("a", 1);
    ASSERT_TRUE(gv != NULL);
    ASSERT_EQ(1, gv->is_extern_decl);
    ASSERT_EQ(1, ps_gvar_is_array(gv));
    ASSERT_EQ(0, ps_gvar_decl_sizeof(gv, 0));
  }

  parsed_code = parse_program_input("int main() { unsigned long long v = 13; signed char c = 7; return v+c; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("typedef unsigned long long ull; int main() { ull v = 5; return v; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  /* static ローカル変数はグローバル変数として lowering されるため、
   * 関数本体にはダミーの ND_NUM(0) だけが残る (init はデータセクションで行う)。
   * 続く register/auto/restrict 宣言は通常どおり ND_ASSIGN を生成。 */
  parsed_code = parse_program_input("int main() { static int x=3; register int r=2; auto int a=1; int *restrict p=0; return a+r+x+(p==0); }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[3]->kind);
  ASSERT_EQ(ND_RETURN, body->body[4]->kind);

  parsed_code = parse_program_input(
      "int main() { const const int x=3; volatile volatile int y=4; int *restrict restrict p=0; return x+y+(p==0); }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input(
      "int sumq(const const int a, volatile volatile int b, int *restrict restrict p){ return a+b+(p==0); }"
      "int main(){ return sumq(3,4,0); }");
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);

  parsed_code = parse_program_input("int main() { _Alignas(16) int x=3; _Atomic int y=4; return x+y; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { _Atomic(int) z=5; return z; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { _Atomic(int*) p=0; _Atomic int q=1; return q + (p==0); }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { _Complex double cx=1.0; _Imaginary float iy=2.0f; return (cx!=0)+(iy!=0); }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { _Complex double a=1.0; _Complex double b=2.0; _Complex double c=a+b; return c!=0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("int main() { int a[1+2]; a[0]=3; return a[0]; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { int a[2][3]; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x:3; int y; }; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { struct S { struct { int x; }; int y; }; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { struct S { union { int x; char c; }; int y; }; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { _Static_assert(1, \"ok\"); int x=3; return x; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { int x={3}; return x; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_TRUE(!ps_type_has_qualifier(
      ps_node_get_type(body->body[0]->lhs), PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(!ps_type_has_qualifier(
      ps_node_get_type(body->body[0]->lhs), PSX_TYPE_QUALIFIER_VOLATILE));
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { enum E { A=1 }; return (enum E)42; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { const int cx=1; volatile int vx=2; return cx+vx; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(body->body[0]->lhs), PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(!ps_type_has_qualifier(
      ps_node_get_type(body->body[0]->lhs), PSX_TYPE_QUALIFIER_VOLATILE));
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_TRUE(!ps_type_has_qualifier(
      ps_node_get_type(body->body[1]->lhs), PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(body->body[1]->lhs), PSX_TYPE_QUALIFIER_VOLATILE));
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { int *const pc=0; int *volatile pv=0; return (pc==0)+(pv==0); }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  assert_node_pointer_qualifiers(body->body[0]->lhs, "1", "0");
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  assert_node_pointer_qualifiers(body->body[1]->lhs, "0", "1");
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { int *const *volatile pp=0; return pp==0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  const psx_type_t *qualified_pp_type =
      ps_node_get_type(body->body[0]->lhs);
  ASSERT_TRUE(qualified_pp_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, qualified_pp_type->kind);
  ASSERT_TRUE(!ps_type_has_qualifier(qualified_pp_type, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(ps_type_has_qualifier(qualified_pp_type, PSX_TYPE_QUALIFIER_VOLATILE));
  ASSERT_TRUE(qualified_pp_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, qualified_pp_type->base->kind);
  ASSERT_TRUE(ps_type_has_qualifier(qualified_pp_type->base, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(!ps_type_has_qualifier(qualified_pp_type->base, PSX_TYPE_QUALIFIER_VOLATILE));
  ASSERT_EQ(2, canonical_node_pointer_qual_levels(body->body[0]->lhs));
  assert_node_pointer_qualifiers(body->body[0]->lhs, "01", "10");
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { int x=42; int *p=&x; int **pp=&p; return **pp; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_ASSIGN, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);
  ASSERT_EQ(ND_DEREF, body->body[3]->lhs->kind);
  ASSERT_EQ(ND_DEREF, body->body[3]->lhs->lhs->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(body->body[3]->lhs->lhs));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(body->body[3]->lhs));
  ASSERT_EQ(8, ps_node_storage_type_size(body->body[3]->lhs->lhs));
  ASSERT_EQ(4, ps_node_storage_type_size(body->body[3]->lhs));

  parsed_code = parse_program_input("int main() { int a[3]={1,2,3}; return a[2]; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { int a[2]=1; return a[0]+a[1]; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { char s[4]=\"abc\"; return s[2]; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; int y; }; struct S s={1,2}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; int y; }; struct S t={1,2}; struct S s=t; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; int y; }; struct S a={1,2}; struct S b={3,4}; struct S s=(0?a:b); return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[3]->kind);
  ASSERT_EQ(ND_TERNARY, body->body[3]->rhs->kind);
  ASSERT_EQ(ND_RETURN, body->body[4]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; int y; }; struct S t={1,2}; struct S s=(t.y=9,t); return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->rhs->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("int main() { struct S { int a[2]; int z; }; struct S t={{1,2},3}; struct S s=t; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("int main() { struct I { int x; int y; }; struct S { struct I i; int z; }; struct S t={{1,2},3}; struct S s=t; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_NUM, body->body[1]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[3]->kind);
  ASSERT_EQ(ND_RETURN, body->body[4]->kind);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U u={7}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U u=7; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U v={7}; union U u=v; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U v={7}; union U u=(v.x=9,v); return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_ASSIGN, body->body[2]->kind);
  ASSERT_EQ(ND_COMMA, body->body[2]->rhs->kind);
  ASSERT_EQ(ND_RETURN, body->body[3]->kind);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U u={.x=7}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U u={.x=7,.y=2}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; int y; }; struct S s={.y=2,.x=1}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { struct S { int x; }; struct S s=(struct S)(struct S){1}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  bool same_tag_cast_has_return = false;
  for (int i = 1; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      same_tag_cast_has_return = true;
      break;
    }
  }
  ASSERT_TRUE(same_tag_cast_has_return);

  parsed_code = parse_program_input("int main() { struct A { int x; }; struct B { int x; }; struct A a={1}; struct B b=(struct B)a; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  bool has_return = false;
  for (int i = 1; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      has_return = true;
      break;
    }
  }
  ASSERT_TRUE(has_return);

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U u=(union U)(union U){.x=7}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_ASSIGN || body->body[1]->kind == ND_COMMA);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { union A { int x; }; union B { int x; }; union A a={.x=1}; union B b=(union B)a; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  has_return = false;
  for (int i = 1; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      has_return = true;
      break;
    }
  }
  ASSERT_TRUE(has_return);

  parsed_code = parse_program_input("int main() { struct I { int x; int y; }; struct O { struct I i; int z; }; struct O o={{1,2},3}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_NUM || body->body[1]->kind == ND_COMMA || body->body[1]->kind == ND_ASSIGN);
  ASSERT_TRUE((body->body[2] && body->body[2]->kind == ND_RETURN) ||
              (body->body[3] && body->body[3]->kind == ND_RETURN));

  parsed_code = parse_program_input("int main() { struct I { int x; int y; }; union U { struct I i; int raw; }; union U u={{4,5}}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_NUM || body->body[1]->kind == ND_ASSIGN);
  ASSERT_TRUE((body->body[2] && body->body[2]->kind == ND_RETURN) ||
              (body->body[3] && body->body[3]->kind == ND_RETURN));

  parsed_code = parse_program_input("int main() { union U { int x; char y; }; union U u={.x=1,.y=2}; return u.x; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_COMMA || body->body[1]->kind == ND_ASSIGN);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { struct S { int a[2]; int z; }; struct S s={{1,2},3}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { struct S { int a[2]; int z; }; struct S s={1,2,3}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { int src[2]={5,6}; struct S { int a[2]; int z; }; struct S s={src,7}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_TRUE(body->body[1]->kind == ND_NUM || body->body[1]->kind == ND_COMMA || body->body[1]->kind == ND_ASSIGN);
  ASSERT_TRUE((body->body[2] && body->body[2]->kind == ND_RETURN) ||
              (body->body[3] && body->body[3]->kind == ND_RETURN));

  parsed_code = parse_program_input("int main() { struct S { char a[4]; int z; }; struct S s={\"ab\",7}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  parsed_code = parse_program_input("int main() { int a[4]={[2]=7,[0]=1}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_COMMA, body->body[0]->kind);
  ASSERT_EQ(ND_RETURN, body->body[1]->kind);

  // 入れ子 designator: struct の配列メンバへの .member[idx]=val
  parsed_code = parse_program_input("int main() { struct S { int a[2]; }; struct S s={.a[1]=3}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  // brace init 開始で struct 全体を 0 で埋める処理が入り、init_chain は
  // ND_COMMA チェイン (zero-fills → 明示 .a[1]=3) になる。
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  // 入れ子 designator: struct の配列メンバへの複数指定
  parsed_code = parse_program_input("int main() { struct S { int a[2]; }; struct S s={.a[0]=1,.a[1]=2}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);

  // 入れ子 designator: union の配列メンバへの .member[idx]=val
  parsed_code = parse_program_input("int main() { union U { int a[2]; int z; }; union U u={.a[1]=3}; return 0; }");
  body = as_block(as_function_definition(parsed_code[0])->base.rhs);
  ASSERT_EQ(ND_NUM, body->body[0]->kind);
  ASSERT_EQ(ND_COMMA, body->body[1]->kind);
  ASSERT_EQ(ND_RETURN, body->body[2]->kind);
}

static void test_type_metadata_bridge() {
  printf("test_type_metadata_bridge...\n");

  psx_type_t *qualified = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_NONE, ps_type_qualifiers(qualified));
  ps_type_add_qualifiers(
      qualified,
      PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_ATOMIC);
  ASSERT_TRUE(ps_type_has_qualifier(
      qualified, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(ps_type_has_qualifier(
      qualified, PSX_TYPE_QUALIFIER_ATOMIC));
  ASSERT_TRUE(!ps_type_has_qualifier(
      qualified, PSX_TYPE_QUALIFIER_VOLATILE));
  ps_type_remove_qualifiers(qualified, PSX_TYPE_QUALIFIER_CONST);
  ASSERT_TRUE(!ps_type_has_qualifier(
      qualified, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(ps_type_has_qualifier(
      qualified, PSX_TYPE_QUALIFIER_ATOMIC));

  psx_type_t *deep_array_leaf = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *deep_array = deep_array_leaf;
  for (int i = 0; i < 10; i++) {
    deep_array = ps_type_new_array(deep_array, 2, 0, 0);
  }
  ASSERT_EQ(10, ps_type_array_rank(deep_array));
  ASSERT_EQ(2, ps_type_array_dimension(deep_array, 9));
  ASSERT_EQ(0, ps_type_array_dimension(deep_array, 10));
  ASSERT_EQ(1024, ps_type_array_flat_element_count(deep_array));
  ASSERT_TRUE(ps_type_array_leaf_type(deep_array) == deep_array_leaf);
  ASSERT_EQ(4, ps_type_array_scalar_element_size(deep_array));
  ASSERT_EQ(512, ps_type_array_subscript_stride_elements(deep_array, 0));
  ASSERT_EQ(2, ps_type_array_subscript_stride_elements(deep_array, 8));
  ASSERT_EQ(1, ps_type_array_subscript_stride_elements(deep_array, 9));
  ASSERT_EQ(0, ps_type_array_subscript_stride_elements(deep_array, 10));
  ASSERT_EQ(2048, ps_type_array_subscript_stride_bytes(deep_array, 0));
  ASSERT_EQ(8, ps_type_array_subscript_stride_bytes(deep_array, 8));
  ASSERT_EQ(4, ps_type_array_subscript_stride_bytes(deep_array, 9));
  ASSERT_EQ(0, ps_type_array_subscript_stride_bytes(deep_array, 10));

  psx_type_t *incomplete_deep_array =
      ps_type_new_array(deep_array, 0, 0, 0);
  ASSERT_TRUE(ps_type_is_incomplete_array(incomplete_deep_array));
  ASSERT_TRUE(!ps_type_is_incomplete_array(deep_array));
  ASSERT_TRUE(!ps_type_is_incomplete_array(
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 0, 0, 1)));
  ASSERT_EQ(11, ps_type_array_rank(incomplete_deep_array));
  ASSERT_EQ(0, ps_type_array_dimension(incomplete_deep_array, 0));
  ASSERT_EQ(0, ps_type_array_flat_element_count(incomplete_deep_array));
  ASSERT_TRUE(ps_type_array_leaf_type(incomplete_deep_array) ==
              deep_array_leaf);

  psx_type_t *callback_type =
      ps_type_new_function(ps_type_new_integer(TK_INT, 4, 0));
  psx_type_t *callback_pointer = ps_type_new_pointer(callback_type);
  psx_type_t *callback_array =
      ps_type_new_array(callback_pointer, 3, 24, 0);
  ASSERT_EQ(1, ps_type_array_rank(callback_array));
  ASSERT_EQ(3, ps_type_array_flat_element_count(callback_array));
  ASSERT_TRUE(ps_type_array_leaf_type(callback_array) == callback_pointer);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_type_array_leaf_type(callback_array)->kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            ps_type_array_leaf_type(callback_array)->base->kind);

  psx_type_t *function_row_leaf = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *function_row =
      ps_type_new_array(function_row_leaf, 3, 12, 0);
  psx_type_t *function_row_pointer = ps_type_new_pointer(function_row);
  psx_type_t *function_returning_row_pointer =
      ps_type_new_function(function_row_pointer);
  ASSERT_TRUE(ps_type_derived_leaf_type(function_returning_row_pointer) ==
              function_returning_row_pointer);
  ASSERT_TRUE(ps_type_function_return_type(function_returning_row_pointer) ==
              function_row_pointer);
  ASSERT_TRUE(ps_type_pointee_value_type(function_row_pointer) ==
              function_row_leaf);

  psx_type_t *unsigned_pointer_leaf =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  psx_type_t *unsigned_pointer = ps_type_new_pointer(unsigned_pointer_leaf);
  node_t unsigned_pointer_pointer = {0};
  unsigned_pointer_pointer.kind = ND_LVAR;
  unsigned_pointer_pointer.type = ps_type_new_pointer(unsigned_pointer);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_type_pointee_value_type(unsigned_pointer_pointer.type)->kind);
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(&unsigned_pointer_pointer));
  ASSERT_TRUE(ps_type_is_unsigned(
      ps_type_derived_leaf_type(unsigned_pointer_pointer.type)));
  node_t *unsigned_pointer_value =
      ps_node_new_unary_deref_for(&unsigned_pointer_pointer);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(unsigned_pointer_value));

  parsed_code = parse_program_input("int main() { unsigned int x=1; return x; }");
  node_function_definition_t *fn = as_function_definition(parsed_code[0]);
  node_block_t *body = as_block(fn->base.rhs);
  node_t *assign = body->body[0];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  const psx_type_t *unsigned_ty = ps_node_get_type(assign->lhs);
  ASSERT_TRUE(unsigned_ty != NULL);
  ASSERT_TRUE(assign->lhs->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, unsigned_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(unsigned_ty));
  ASSERT_TRUE(ps_type_is_unsigned(unsigned_ty));
  lvar_t *x_lvar = find_func_lvar(fn, "x");
  ASSERT_TRUE(x_lvar != NULL);
  ASSERT_TRUE(ps_node_lvar_symbol(assign->lhs) == x_lvar);
  ASSERT_TRUE(x_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, x_lvar->decl_type->kind);
  ASSERT_EQ(4, ps_type_sizeof(x_lvar->decl_type));
  ASSERT_TRUE(ps_type_is_unsigned(x_lvar->decl_type));
  node_t *x_lvar_ref = psx_node_new_lvar_identifier_ref_for(x_lvar);
  ASSERT_TRUE(ps_node_is_unsigned_type(x_lvar_ref));
  node_t *x_lvar_direct_ref = psx_node_new_lvar_for(x_lvar);
  ASSERT_TRUE(ps_node_is_unsigned_type(x_lvar_direct_ref));
  const psx_type_t *x_decl_a = ps_lvar_get_decl_type(x_lvar);
  ASSERT_TRUE(x_decl_a != NULL);
  x_lvar->decl_type = NULL;
  ASSERT_TRUE(ps_lvar_get_decl_type(x_lvar) == NULL);
  x_lvar->decl_type = x_decl_a;

  parsed_code = parse_program_input("int __tm_local_bool_ref(void) { _Bool b=1; return b; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *bool_ref_lvar = find_func_lvar(fn, "b");
  ASSERT_TRUE(bool_ref_lvar != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(bool_ref_lvar) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_ref_lvar->decl_type->kind);
  node_t *bool_ref_node = psx_node_new_lvar_identifier_ref_for(bool_ref_lvar);
  ASSERT_TRUE(ps_node_get_type(bool_ref_node) == bool_ref_lvar->decl_type);
  node_t *bool_direct_ref = psx_node_new_lvar_for(bool_ref_lvar);
  ASSERT_TRUE(ps_node_get_type(bool_direct_ref) == bool_ref_lvar->decl_type);

  parsed_code = parse_program_input("int __tm_local_atomic_ref(void) { _Atomic int a=1; return a; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *atomic_ref_lvar = find_func_lvar(fn, "a");
  ASSERT_TRUE(atomic_ref_lvar != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(atomic_ref_lvar) != NULL);
  ASSERT_TRUE(ps_type_has_qualifier(atomic_ref_lvar->decl_type, PSX_TYPE_QUALIFIER_ATOMIC));
  node_t *atomic_ref_node = psx_node_new_lvar_for(atomic_ref_lvar);
  ASSERT_TRUE(ps_node_get_type(atomic_ref_node) == atomic_ref_lvar->decl_type);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(atomic_ref_node), PSX_TYPE_QUALIFIER_ATOMIC));

  parsed_code = parse_program_input(
      "int __tm_param_identity(const long long ll, volatile char ch, "
      "_Atomic int ai) { return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *param_ll = find_func_lvar(fn, "ll");
  lvar_t *param_ch = find_func_lvar(fn, "ch");
  lvar_t *param_ai = find_func_lvar(fn, "ai");
  ASSERT_TRUE(param_ll != NULL && param_ll->decl_type != NULL);
  ASSERT_TRUE(param_ch != NULL && param_ch->decl_type != NULL);
  ASSERT_TRUE(param_ai != NULL && param_ai->decl_type != NULL);
  ASSERT_TRUE(ps_type_has_qualifier(param_ll->decl_type, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(param_ll->decl_type->is_long_long);
  ASSERT_TRUE(ps_type_has_qualifier(param_ch->decl_type, PSX_TYPE_QUALIFIER_VOLATILE));
  ASSERT_TRUE(param_ch->decl_type->is_plain_char);
  ASSERT_TRUE(ps_type_has_qualifier(param_ai->decl_type, PSX_TYPE_QUALIFIER_ATOMIC));

  parsed_code = parse_program_input(
      "typedef int *__tm_qualified_ptr_alias; "
      "int __tm_qualified_ptr_alias_fn(void) { int x=0; "
      "const __tm_qualified_ptr_alias p=&x; *p=7; return x; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *qualified_ptr_alias = find_func_lvar(fn, "p");
  ASSERT_TRUE(qualified_ptr_alias != NULL);
  const psx_type_t *qualified_ptr_alias_type =
      ps_lvar_get_decl_type(qualified_ptr_alias);
  ASSERT_TRUE(qualified_ptr_alias_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, qualified_ptr_alias_type->kind);
  ASSERT_TRUE(ps_type_has_qualifier(qualified_ptr_alias_type, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(qualified_ptr_alias_type->base != NULL);
  ASSERT_TRUE(!ps_type_has_qualifier(qualified_ptr_alias_type->base, PSX_TYPE_QUALIFIER_CONST));

  parsed_code = parse_program_input(
      "typedef const int __tm_const_array_alias[2]; "
      "int __tm_const_array_alias_fn(void) { "
      "volatile __tm_const_array_alias values={1,2}; return values[0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *qualified_array_alias = find_func_lvar(fn, "values");
  ASSERT_TRUE(qualified_array_alias != NULL);
  const psx_type_t *qualified_array_alias_type =
      ps_lvar_get_decl_type(qualified_array_alias);
  ASSERT_TRUE(qualified_array_alias_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, qualified_array_alias_type->kind);
  ASSERT_TRUE(qualified_array_alias_type->base != NULL);
  ASSERT_TRUE(ps_type_has_qualifier(qualified_array_alias_type->base, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(ps_type_has_qualifier(
      qualified_array_alias_type->base, PSX_TYPE_QUALIFIER_VOLATILE));

  psx_type_t *typed_atomic_int = ps_type_new_integer(TK_INT, 4, 0);
  ps_type_add_qualifiers(typed_atomic_int, PSX_TYPE_QUALIFIER_ATOMIC);
  node_t typed_atomic_ptr_mem = {0};
  typed_atomic_ptr_mem.kind = ND_DEREF;
  typed_atomic_ptr_mem.type = ps_type_new_pointer(typed_atomic_int);
  node_t *typed_atomic_deref =
      ps_node_new_unary_deref_for(&typed_atomic_ptr_mem);
  ASSERT_TRUE(ps_node_get_type(typed_atomic_deref) == typed_atomic_int);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(typed_atomic_deref), PSX_TYPE_QUALIFIER_ATOMIC));

  lvar_t tmp_lvar = {0};
  tmp_lvar.size = 4;
  set_test_storage_fixture_type(
      &tmp_lvar, ps_type_new_integer(TK_INT, 4, 0));
  const psx_type_t *tmp_lvar_int = ps_lvar_get_decl_type(&tmp_lvar);
  ASSERT_TRUE(tmp_lvar_int != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tmp_lvar_int->kind);
  ASSERT_TRUE(!ps_lvar_value_is_pointer_like(&tmp_lvar));
  tmp_lvar.size = 8;
  set_test_storage_fixture_type(
      &tmp_lvar,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0)));
  ASSERT_TRUE(ps_lvar_value_is_pointer_like(&tmp_lvar));
  ASSERT_TRUE(tmp_lvar.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar.decl_type->kind);
  const psx_type_t *tmp_lvar_ptr = tmp_lvar.decl_type;
  ASSERT_TRUE(tmp_lvar_ptr != NULL);
  ASSERT_TRUE(tmp_lvar_ptr != tmp_lvar_int);
  ASSERT_TRUE(tmp_lvar.decl_type == tmp_lvar_ptr);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar_ptr->kind);
  ASSERT_EQ(8, ps_lvar_decl_sizeof(&tmp_lvar, 99));
  ASSERT_EQ(8, ps_lvar_storage_size(&tmp_lvar, 99));

  const char nested_tag_ptr_array_name[] = "__tm_nested_tag_ptr_array";
  lvar_t tmp_lvar_nested_tag_ptr_array = {0};
  tmp_lvar_nested_tag_ptr_array.size = 16;
  psx_type_t *tmp_nested_tag = ps_type_new_tag(
      TK_STRUCT, (char *)nested_tag_ptr_array_name,
      (int)sizeof(nested_tag_ptr_array_name) - 1, 0, 8);
  psx_type_t *tmp_nested_inner_ptr = ps_type_new_pointer(tmp_nested_tag);
  ps_type_add_qualifiers(tmp_nested_inner_ptr, PSX_TYPE_QUALIFIER_CONST);
  ps_type_add_qualifiers(tmp_nested_inner_ptr, PSX_TYPE_QUALIFIER_VOLATILE);
  psx_type_t *tmp_nested_ptr = ps_type_new_pointer(tmp_nested_inner_ptr);
  ps_type_add_qualifiers(tmp_nested_ptr, PSX_TYPE_QUALIFIER_CONST);
  psx_type_t *tmp_nested_array = ps_type_new_array(
      tmp_nested_ptr, 2, 16, 0);
  set_test_storage_fixture_type(&tmp_lvar_nested_tag_ptr_array,
                              tmp_nested_array);
  const psx_type_t *tmp_lvar_nested_tag_ptr_array_type =
      ps_lvar_get_decl_type(&tmp_lvar_nested_tag_ptr_array);
  ASSERT_TRUE(tmp_lvar_nested_tag_ptr_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tmp_lvar_nested_tag_ptr_array_type->kind);
  ASSERT_EQ(2, tmp_lvar_nested_tag_ptr_array_type->array_len);
  const psx_type_t *tmp_lvar_nested_tag_ptr_array_elem =
      tmp_lvar_nested_tag_ptr_array_type->base;
  ASSERT_TRUE(tmp_lvar_nested_tag_ptr_array_elem != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar_nested_tag_ptr_array_elem->kind);
  assert_pointer_qualifiers(
      tmp_lvar_nested_tag_ptr_array_elem, "11", "01");
  ASSERT_TRUE(tmp_lvar_nested_tag_ptr_array_elem->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_lvar_nested_tag_ptr_array_elem->base->kind);
  assert_pointer_qualifiers(
      tmp_lvar_nested_tag_ptr_array_elem->base, "1", "1");
  ASSERT_TRUE(tmp_lvar_nested_tag_ptr_array_elem->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT,
            tmp_lvar_nested_tag_ptr_array_elem->base->base->kind);

  lvar_t tmp_lvar_decl_type_wins = {0};
  tmp_lvar_decl_type_wins.size = 4;
  set_test_storage_fixture_type(
      &tmp_lvar_decl_type_wins,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0)));
  node_t *tmp_lvar_decl_type_ref =
      psx_node_new_lvar_object_ref_for(&tmp_lvar_decl_type_wins);
  ASSERT_EQ(8, ps_node_type_size(tmp_lvar_decl_type_ref));
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_decl_type_ref) ==
              tmp_lvar_decl_type_wins.decl_type);
  ASSERT_TRUE(ps_node_value_is_pointer_like(tmp_lvar_decl_type_ref));
  ASSERT_EQ(4, ps_node_deref_size(tmp_lvar_decl_type_ref));

  lvar_t tmp_lvar_scalar_decl_type_wins = {0};
  psx_type_t *tmp_lvar_scalar_canonical =
      ps_type_new_integer(TK_INT, 4, 0);
  tmp_lvar_scalar_decl_type_wins.size = 8;
  tmp_lvar_scalar_decl_type_wins.decl_type = tmp_lvar_scalar_canonical;
  ASSERT_TRUE(ps_lvar_get_decl_type(&tmp_lvar_scalar_decl_type_wins) ==
              tmp_lvar_scalar_canonical);
  ASSERT_TRUE(!ps_lvar_is_array(&tmp_lvar_scalar_decl_type_wins));
  ASSERT_TRUE(!ps_lvar_is_tag_aggregate(&tmp_lvar_scalar_decl_type_wins));
  ASSERT_TRUE(!ps_lvar_value_is_pointer_like(
      &tmp_lvar_scalar_decl_type_wins));
  node_t *tmp_lvar_scalar_decl_type_ref =
      psx_node_new_lvar_for(&tmp_lvar_scalar_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_scalar_decl_type_ref) ==
              tmp_lvar_scalar_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_scalar_decl_type_ref));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_lvar_scalar_decl_type_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_scalar_decl_type_ref));
  node_t *tmp_lvar_scalar_funcptr_ref =
      psx_node_new_lvar_for(&tmp_lvar_scalar_decl_type_wins);
  ASSERT_TRUE(ps_type_derived_function(
      ps_node_get_type(tmp_lvar_scalar_funcptr_ref)) == NULL);

  lvar_t tmp_lvar_ptr_array_cache_decl_type_wins = {0};
  psx_type_t *tmp_lvar_ptr_array_cache_canonical =
      ps_type_new_integer(TK_INT, 4, 0);
  tmp_lvar_ptr_array_cache_decl_type_wins.size = 8;
  tmp_lvar_ptr_array_cache_decl_type_wins.decl_type =
      tmp_lvar_ptr_array_cache_canonical;
  ASSERT_TRUE(ps_lvar_get_decl_type(
                  &tmp_lvar_ptr_array_cache_decl_type_wins) ==
              tmp_lvar_ptr_array_cache_canonical);
  ASSERT_TRUE(!ps_lvar_value_is_pointer_like(
      &tmp_lvar_ptr_array_cache_decl_type_wins));
  node_t *tmp_lvar_ptr_array_cache_ref =
      psx_node_new_lvar_for(&tmp_lvar_ptr_array_cache_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_array_cache_ref) ==
              tmp_lvar_ptr_array_cache_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_ptr_array_cache_ref));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_lvar_ptr_array_cache_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_ptr_array_cache_ref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_array_cache_ref));
  node_t *tmp_lvar_ptr_array_cache_expr_ref =
      ps_node_new_lvar_expr_ref_for(&tmp_lvar_ptr_array_cache_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_array_cache_expr_ref) ==
              tmp_lvar_ptr_array_cache_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_ptr_array_cache_expr_ref));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_lvar_ptr_array_cache_expr_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_ptr_array_cache_expr_ref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_array_cache_expr_ref));
  node_t *tmp_lvar_ptr_array_cache_param_ref =
      ps_node_new_param_lvar_for(&tmp_lvar_ptr_array_cache_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_array_cache_param_ref) ==
              tmp_lvar_ptr_array_cache_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_ptr_array_cache_param_ref));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_lvar_ptr_array_cache_param_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_ptr_array_cache_param_ref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_array_cache_param_ref));

  lvar_t tmp_lvar_ptr_decl_type_wins = {0};
  tmp_lvar_ptr_decl_type_wins.size = 8;
  tmp_lvar_ptr_decl_type_wins.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_t *tmp_lvar_ptr_decl_type_ref =
      psx_node_new_lvar_identifier_ref_for(&tmp_lvar_ptr_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_decl_type_ref) ==
              tmp_lvar_ptr_decl_type_wins.decl_type);
  ASSERT_TRUE(ps_node_value_is_pointer_like(tmp_lvar_ptr_decl_type_ref));
  ASSERT_EQ(4, ps_node_deref_size(tmp_lvar_ptr_decl_type_ref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_decl_type_ref));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   tmp_lvar_ptr_decl_type_ref, 0));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(
                   tmp_lvar_ptr_decl_type_ref));
  node_t *tmp_lvar_ptr_decl_type_deref =
      ps_node_new_unary_deref_for(tmp_lvar_ptr_decl_type_ref);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_decl_type_deref) ==
              tmp_lvar_ptr_decl_type_wins.decl_type->base);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_lvar_ptr_decl_type_deref));
  ASSERT_EQ(4, ps_node_type_size(tmp_lvar_ptr_decl_type_deref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_ptr_decl_type_deref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_decl_type_deref));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   tmp_lvar_ptr_decl_type_deref, 0));
  node_t *tmp_lvar_ptr_decl_type_addr =
      ps_node_new_lvar_array_addr_for(&tmp_lvar_ptr_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_ptr_decl_type_addr) ==
              tmp_lvar_ptr_decl_type_wins.decl_type);
  ASSERT_TRUE(ps_node_value_is_pointer_like(tmp_lvar_ptr_decl_type_addr));
  ASSERT_EQ(4, ps_node_deref_size(tmp_lvar_ptr_decl_type_addr));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_lvar_ptr_decl_type_addr));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   tmp_lvar_ptr_decl_type_addr, 0));

  lvar_t explicit_array_addr_var = {0};
  explicit_array_addr_var.decl_type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0);
  node_t *decayed_array_addr =
      ps_node_new_lvar_array_addr_for(&explicit_array_addr_var);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(decayed_array_addr)->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(decayed_array_addr)->base->kind);
  node_t *explicit_array_addr =
      ps_node_new_explicit_addr_value_for(decayed_array_addr);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(explicit_array_addr)->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(explicit_array_addr)->base->kind);
  ASSERT_EQ(12, ps_node_deref_size(explicit_array_addr));
  ASSERT_EQ(0, ps_node_compound_literal_array_size(explicit_array_addr));

  lvar_t tmp_lvar_flat_ptr_mismatch_decl_type = {0};
  tmp_lvar_flat_ptr_mismatch_decl_type.size = 8;
  tmp_lvar_flat_ptr_mismatch_decl_type.decl_type =
      ps_type_new_pointer(NULL);
  ASSERT_EQ(0, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                   tmp_lvar_flat_ptr_mismatch_decl_type.decl_type));
  psx_type_t *tmp_structural_ptrarr_type = ps_type_new_pointer(
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 0));
  ASSERT_EQ(16, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    tmp_structural_ptrarr_type));
  node_t *tmp_lvar_flat_ptr_mismatch_ref =
      psx_node_new_lvar_identifier_ref_for(
          &tmp_lvar_flat_ptr_mismatch_decl_type);
  ASSERT_TRUE(ps_node_get_type(tmp_lvar_flat_ptr_mismatch_ref) ==
              tmp_lvar_flat_ptr_mismatch_decl_type.decl_type);
  ASSERT_TRUE(ps_node_value_is_pointer_like(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, canonical_node_pointer_qual_levels(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, canonical_node_base_deref_size(tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_lvar_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   tmp_lvar_flat_ptr_mismatch_ref, 0));

  lvar_t tmp_static_local_fallback_decl_type_wins = {0};
  char *tmp_static_missing_backing_name = "__tm_missing_static_backing";
  tmp_static_local_fallback_decl_type_wins.is_static_local = 1;
  tmp_static_local_fallback_decl_type_wins.static_global_name =
      tmp_static_missing_backing_name;
  tmp_static_local_fallback_decl_type_wins.static_global_name_len =
      (int)strlen(tmp_static_missing_backing_name);
  tmp_static_local_fallback_decl_type_wins.size = 8;
  tmp_static_local_fallback_decl_type_wins.decl_type =
      ps_type_new_integer(TK_INT, 4, 0);
  node_t *tmp_static_local_fallback_ref =
      psx_node_new_static_local_gvar_for(
          &tmp_static_local_fallback_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_static_local_fallback_ref) ==
              tmp_static_local_fallback_decl_type_wins.decl_type);
  ASSERT_EQ(4, ps_node_type_size(tmp_static_local_fallback_ref));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_static_local_fallback_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_static_local_fallback_ref));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(tmp_static_local_fallback_ref)->kind);
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_static_local_fallback_ref));

  parsed_code = parse_program_input(
      "int __tm_sig_f(int x){ return x; } "
      "int __tm_sig_local(void) { "
      "struct LocalTag { int value; }; "
      "_Alignas(16) struct LocalTag aligned_value; "
      "typedef const struct LocalTag LocalAlias; LocalAlias alias_value; "
      "typedef double LocalSigParam; "
      "int (*p)(LocalSigParam, int *, ...); return 0; }");
  fn = as_function_definition(parsed_code[1]);
  lvar_t *sig_lvar = find_func_lvar(fn, "p");
  lvar_t *aligned_value = find_func_lvar(fn, "aligned_value");
  lvar_t *alias_value = find_func_lvar(fn, "alias_value");
  ASSERT_TRUE(aligned_value != NULL);
  ASSERT_TRUE(alias_value != NULL);
  ASSERT_EQ(0, ps_lvar_offset(aligned_value) % 16);
  ASSERT_EQ(16, aligned_value->align_bytes);
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_lvar_get_decl_type(aligned_value)->kind);
  ASSERT_EQ(0, ps_type_sizeof(ps_lvar_get_decl_type(aligned_value)));
  ASSERT_EQ(4, ps_ctx_type_sizeof_in(
                   test_semantic_context(),
                   ps_lvar_get_decl_type(aligned_value)));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_lvar_get_decl_type(alias_value)->kind);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_lvar_get_decl_type(alias_value), PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(sig_lvar != NULL);
  const psx_type_t *sig_lvar_type = ps_lvar_get_decl_type(sig_lvar);
  ASSERT_TRUE(sig_lvar_type != NULL);
  const psx_type_t *sig_lvar_function =
      ps_type_derived_function(sig_lvar_type);
  ASSERT_TRUE(sig_lvar_function != NULL);
  ASSERT_EQ(2, sig_lvar_function->param_count);
  ASSERT_TRUE(sig_lvar_function->is_variadic_function);
  ASSERT_EQ(PSX_TYPE_FLOAT, sig_lvar_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            sig_lvar_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER, sig_lvar_function->param_types[1]->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            sig_lvar_function->param_types[1]->base->kind);
  sig_lvar->decl_type = NULL;
  ASSERT_TRUE(ps_lvar_get_decl_type(sig_lvar) == NULL);
  sig_lvar->decl_type = sig_lvar_type;

  reset_test_locals();
  char funcptr_sync_lvar_name[] = "__tm_funcptr_sync_lvar";
  lvar_t *funcptr_sync_lvar =
      register_test_storage_fixture(funcptr_sync_lvar_name,
                                   (int)strlen(funcptr_sync_lvar_name), 8, 4, 0);
  psx_type_t *funcptr_sync_lvar_param =
      ps_type_new_integer(TK_INT, 4, 0);
  const psx_type_t *funcptr_sync_lvar_params[] = {
      funcptr_sync_lvar_param};
  const psx_type_t *funcptr_sync_lvar_type = test_function_pointer(
      ps_type_new_integer(TK_INT, 4, 0),
      funcptr_sync_lvar_params, 1, 0);
  set_test_storage_fixture_type(funcptr_sync_lvar, funcptr_sync_lvar_type);
  funcptr_sync_lvar_type = ps_lvar_get_decl_type(funcptr_sync_lvar);
  ASSERT_TRUE(funcptr_sync_lvar_type != NULL);
  assert_canonical_type_signature(funcptr_sync_lvar_type, "p<i32(i32)>");

  global_var_t funcptr_sync_gvar = {0};
  psx_type_t *funcptr_sync_gvar_param =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  const psx_type_t *funcptr_sync_gvar_params[] = {
      funcptr_sync_gvar_param};
  const psx_type_t *funcptr_sync_gvar_type = test_function_pointer(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
      funcptr_sync_gvar_params, 1, 0);
  ASSERT_TRUE(intern_test_type_id(funcptr_sync_gvar_type) !=
              PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &funcptr_sync_gvar, funcptr_sync_gvar_type));
  funcptr_sync_gvar_type = ps_gvar_get_decl_type(&funcptr_sync_gvar);
  ASSERT_TRUE(funcptr_sync_gvar_type != NULL);
  assert_canonical_type_signature(funcptr_sync_gvar_type, "p<f64(f64)>");

  parsed_code = parse_program_input(
      "int __tm_sig_nested_local(void) { "
      "int (*(*q)(void))(int); return _Generic(q, int (*(*)(void))(int): 37, default: 7); }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *sig_nested_lvar = find_func_lvar(fn, "q");
  ASSERT_TRUE(sig_nested_lvar != NULL);
  const psx_type_t *sig_nested_lvar_type = ps_lvar_get_decl_type(sig_nested_lvar);
  ASSERT_TRUE(sig_nested_lvar_type != NULL);
  assert_canonical_type_signature(
      sig_nested_lvar_type, "p<p<i32(i32)>()>");
  node_lvar_t *sig_nested_lvar_node = as_lvar(psx_node_new_lvar_for(sig_nested_lvar));
  assert_canonical_type_signature(
      ps_node_get_type((node_t *)sig_nested_lvar_node),
      "p<p<i32(i32)>()>");

  parsed_code = parse_program_input(
      "typedef double NestedSigParam; "
      "int __tm_sig_nested_param(int (*(*q)(void))(NestedSigParam)) "
      "{ return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *sig_nested_param = find_func_lvar(fn, "q");
  ASSERT_TRUE(sig_nested_param != NULL);
  const psx_type_t *sig_nested_param_type = ps_lvar_get_decl_type(sig_nested_param);
  ASSERT_TRUE(sig_nested_param_type != NULL);
  assert_canonical_type_signature(
      sig_nested_param_type, "p<p<i32(f64)>()>");
  const psx_type_t *outer_nested_param_function =
      ps_type_derived_function(sig_nested_param_type);
  ASSERT_TRUE(outer_nested_param_function != NULL);
  ASSERT_EQ(0, outer_nested_param_function->param_count);
  const psx_type_t *returned_nested_param_function =
      ps_type_derived_function(outer_nested_param_function->base);
  ASSERT_TRUE(returned_nested_param_function != NULL);
  ASSERT_EQ(1, returned_nested_param_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            returned_nested_param_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            returned_nested_param_function->param_types[0]->fp_kind);

  parsed_code = parse_program_input(
      "int __tm_sig_gf(int x){ return x; } "
      "int (*__tm_sig_gfp)(int); "
      "typedef double __tm_top_param_t; "
      "int (*__tm_top_fp)(__tm_top_param_t, int *, ...); "
      "int __tm_sig_global(void) { return __tm_sig_gfp(1); }");
  (void)parsed_code;
  global_var_t *sig_gvar = find_test_global_var("__tm_sig_gfp", 12);
  ASSERT_TRUE(sig_gvar != NULL);
  const psx_type_t *sig_gvar_type = ps_gvar_get_decl_type(sig_gvar);
  ASSERT_TRUE(sig_gvar_type != NULL);
  sig_gvar->decl_type = NULL;
  ASSERT_TRUE(ps_gvar_get_decl_type(sig_gvar) == NULL);
  sig_gvar->decl_type = sig_gvar_type;
  global_var_t *top_fp = find_test_global_var(
      "__tm_top_fp", (int)(sizeof("__tm_top_fp") - 1));
  ASSERT_TRUE(top_fp != NULL);
  const psx_type_t *top_function =
      ps_type_derived_function(ps_gvar_get_decl_type(top_fp));
  ASSERT_TRUE(top_function != NULL);
  ASSERT_EQ(2, top_function->param_count);
  ASSERT_TRUE(top_function->is_variadic_function);
  ASSERT_EQ(PSX_TYPE_FLOAT, top_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, top_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER, top_function->param_types[1]->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, top_function->param_types[1]->base->kind);

  parsed_code = parse_program_input(
      "typedef const struct __tm_ret_tag { int value; } __tm_ret_alias; "
      "__tm_ret_alias *__tm_ret_alias_fn(void); "
      "struct __tm_ret_tag *__tm_ret_tag_fn(void); "
      "int (*__tm_ret_nested(void))(double); "
      "int __tm_ret_explicit(void);");
  (void)parsed_code;
  const psx_type_t *alias_return_function = ps_ctx_get_function_type_in(test_semantic_context(),
      (char *)"__tm_ret_alias_fn",
      (int)(sizeof("__tm_ret_alias_fn") - 1));
  ASSERT_TRUE(alias_return_function != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, alias_return_function->kind);
  ASSERT_TRUE(alias_return_function->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, alias_return_function->base->kind);
  ASSERT_TRUE(alias_return_function->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, alias_return_function->base->base->kind);
  ASSERT_TRUE(ps_type_has_qualifier(alias_return_function->base->base, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_EQ((int)(sizeof("__tm_ret_tag") - 1),
            alias_return_function->base->base->tag_len);

  const psx_type_t *tag_return_function = ps_ctx_get_function_type_in(test_semantic_context(),
      (char *)"__tm_ret_tag_fn",
      (int)(sizeof("__tm_ret_tag_fn") - 1));
  ASSERT_TRUE(tag_return_function != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tag_return_function->base->kind);
  ASSERT_EQ(PSX_TYPE_STRUCT, tag_return_function->base->base->kind);
  ASSERT_EQ(alias_return_function->base->base->tag_scope_depth_p1,
            tag_return_function->base->base->tag_scope_depth_p1);

  const psx_type_t *nested_return_function = ps_ctx_get_function_type_in(test_semantic_context(),
      (char *)"__tm_ret_nested",
      (int)(sizeof("__tm_ret_nested") - 1));
  ASSERT_TRUE(nested_return_function != NULL);
  const psx_type_t *returned_function =
      ps_type_derived_function(nested_return_function->base);
  ASSERT_TRUE(returned_function != NULL);
  ASSERT_EQ(1, returned_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT, returned_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            returned_function->param_types[0]->fp_kind);

  const psx_type_t *explicit_return_function = ps_ctx_get_function_type_in(test_semantic_context(),
      (char *)"__tm_ret_explicit",
      (int)(sizeof("__tm_ret_explicit") - 1));
  ASSERT_TRUE(explicit_return_function != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, explicit_return_function->base->kind);
  ASSERT_EQ(TK_INT, explicit_return_function->base->scalar_kind);

  parsed_code = parse_program_input(
      "int (*(*__tm_sig_nested_gfp)(void))(double); "
      "int __tm_sig_nested_global(void) { return 0; }");
  (void)parsed_code;
  global_var_t *sig_nested_gvar =
      find_test_global_var("__tm_sig_nested_gfp",
                          (int)(sizeof("__tm_sig_nested_gfp") - 1));
  ASSERT_TRUE(sig_nested_gvar != NULL);
  const psx_type_t *sig_nested_gvar_type = ps_gvar_get_decl_type(sig_nested_gvar);
  ASSERT_TRUE(sig_nested_gvar_type != NULL);
  assert_canonical_type_signature(
      sig_nested_gvar_type, "p<p<i32(f64)>()>");
  node_t *sig_nested_gvar_node = ps_node_new_gvar_for(sig_nested_gvar);
  assert_canonical_type_signature(
      ps_node_get_type(sig_nested_gvar_node), "p<p<i32(f64)>()>");

  parsed_code = parse_program_input(
      "int **__tm_gpp; int *__tm_gptrs[2]; "
      "int __tm_gpp_use(void) { return *__tm_gpp[1]; }");
  (void)parsed_code;
  global_var_t *gpp = find_test_global_var("__tm_gpp", 8);
  ASSERT_TRUE(gpp != NULL);
  const psx_type_t *gpp_type = ps_gvar_get_decl_type(gpp);
  ASSERT_TRUE(gpp_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpp_type->kind);
  ASSERT_EQ(8, ps_type_deref_size(gpp_type));
  ASSERT_TRUE(gpp_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpp_type->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(gpp_type->base));
  node_t *gpp_node = ps_node_new_gvar_for(gpp);
  ASSERT_EQ(8, ps_node_deref_size(gpp_node));

  parsed_code = parse_program_input(
      "double __tm_gda[2][3]; int __tm_gda_use(void) { return 0; }");
  (void)parsed_code;
  global_var_t *gda = find_test_global_var("__tm_gda", 8);
  ASSERT_TRUE(gda != NULL);
  node_t *gda_node = ps_node_new_gvar_for(gda);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, canonical_node_pointee_fp_kind(gda_node));
  node_t *gda_stale_sidecar_node = ps_node_new_gvar_for(gda);
  ASSERT_EQ(8, canonical_node_base_deref_size(gda_stale_sidecar_node));
  node_t *gda_base = psx_node_new_gvar_array_base_for(gda);
  ASSERT_TRUE(ps_node_get_type(gda_base) == ps_gvar_get_decl_type(gda));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gda_base)->kind);
  ASSERT_EQ(2, ps_node_get_type(gda_base)->array_len);
  ASSERT_TRUE(ps_node_get_type(gda_base)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gda_base)->base->kind);
  ASSERT_EQ(3, ps_node_get_type(gda_base)->base->array_len);

  parsed_code = parse_program_input(
      "unsigned char __tm_global_su_arr[2]; _Bool __tm_global_sb_arr[2]; "
      "int __tm_global_si_arr[2]; "
      "int __tm_global_array_addr_flags(void) { return 0; }");
  (void)parsed_code;
  global_var_t *global_su_arr = find_test_global_var(
      "__tm_global_su_arr", (int)(sizeof("__tm_global_su_arr") - 1));
  ASSERT_TRUE(global_su_arr != NULL);
  ASSERT_TRUE(global_su_arr->decl_type != NULL);
  node_t *global_su_arr_addr = ps_node_new_gvar_array_addr_for(global_su_arr);
  ASSERT_TRUE(ps_node_get_type(global_su_arr_addr) != NULL);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(global_su_arr_addr));
  node_t *global_su_arr_base =
      psx_node_new_gvar_array_base_for(global_su_arr);
  ASSERT_TRUE(ps_node_get_type(global_su_arr_base) ==
              global_su_arr->decl_type);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(global_su_arr_base));

  global_var_t *global_sb_arr = find_test_global_var(
      "__tm_global_sb_arr", (int)(sizeof("__tm_global_sb_arr") - 1));
  ASSERT_TRUE(global_sb_arr != NULL);
  ASSERT_TRUE(global_sb_arr->decl_type != NULL);
  node_t *global_sb_arr_addr = ps_node_new_gvar_array_addr_for(global_sb_arr);
  ASSERT_TRUE(ps_node_get_type(global_sb_arr_addr) != NULL);
  ASSERT_TRUE(canonical_node_pointee_is_bool(global_sb_arr_addr));
  node_t *global_sb_arr_base =
      psx_node_new_gvar_array_base_for(global_sb_arr);
  ASSERT_TRUE(ps_node_get_type(global_sb_arr_base) ==
              global_sb_arr->decl_type);
  ASSERT_TRUE(canonical_node_pointee_is_bool(global_sb_arr_base));

  global_var_t *global_si_arr = find_test_global_var(
      "__tm_global_si_arr", (int)(sizeof("__tm_global_si_arr") - 1));
  ASSERT_TRUE(global_si_arr != NULL);
  ASSERT_TRUE(global_si_arr->decl_type != NULL);
  node_t *global_si_arr_addr = ps_node_new_gvar_array_addr_for(global_si_arr);
  ASSERT_TRUE(ps_node_get_type(global_si_arr_addr) != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, canonical_node_pointee_fp_kind(global_si_arr_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(global_si_arr_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(global_si_arr_addr));
  ASSERT_EQ(4, ps_node_deref_size(global_si_arr_addr));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(global_si_arr_addr));

  parsed_code = parse_program_input(
      "struct __tm_gsa_S { int x; int y; }; "
      "struct __tm_gsa_S __tm_gsa[2][2]; "
      "int __tm_gsa_use(void) { return 0; }");
  (void)parsed_code;
  global_var_t *gsa = find_test_global_var("__tm_gsa", 8);
  ASSERT_TRUE(gsa != NULL);
  ASSERT_TRUE(ps_gvar_is_tag_aggregate(gsa));
  ASSERT_TRUE(ps_gvar_is_struct_aggregate(gsa));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(gsa));
  ASSERT_EQ(8, ps_ctx_type_sizeof_in(
                   test_semantic_context(),
                   ps_type_array_leaf_type(ps_gvar_get_decl_type(gsa))));
  ASSERT_EQ(4, ps_type_array_flat_element_count(
                   ps_gvar_get_decl_type(gsa)));

  global_var_t *gptrs = find_test_global_var("__tm_gptrs", 10);
  ASSERT_TRUE(gptrs != NULL);
  const psx_type_t *gptrs_type = ps_gvar_get_decl_type(gptrs);
  ASSERT_TRUE(gptrs_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gptrs_type->kind);
  ASSERT_TRUE(gptrs_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gptrs_type->base->kind);
  ASSERT_EQ(8, ps_type_sizeof(gptrs_type->base));
  ASSERT_EQ(4, ps_type_deref_size(gptrs_type->base));
  parsed_code = parse_program_input(
      "int __tm_rows_a[2][3]; typedef int (*__tm_RowPtr3)[3]; "
      "__tm_RowPtr3 __tm_rows[2];");
  (void)parsed_code;
  global_var_t *rows_array = find_test_global_var("__tm_rows", 9);
  ASSERT_TRUE(rows_array != NULL);
  const psx_type_t *rows_array_type = ps_gvar_get_decl_type(rows_array);
  ASSERT_TRUE(rows_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, rows_array_type->kind);
  ASSERT_TRUE(rows_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, rows_array_type->base->kind);
  ASSERT_TRUE(rows_array_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, rows_array_type->base->base->kind);
  node_t *rows_array_node = ps_node_new_gvar_for(rows_array);
  node_t *rows_array_elem = ps_node_new_subscript_deref_for(
      rows_array_node, rows_array_node, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_value_is_pointer_like(rows_array_elem));
  ASSERT_EQ(12, ps_node_deref_size(rows_array_elem));
  int rows_inner =
      canonical_node_array_subscript_stride_bytes(rows_array_elem, 0);
  int rows_next =
      canonical_node_array_subscript_stride_bytes(rows_array_elem, 1);
  ASSERT_EQ(4, rows_inner);
  ASSERT_EQ(0, rows_next);

  parsed_code = parse_program_input(
      "typedef int (*__tm_RowPtrScalar)[3]; __tm_RowPtrScalar __tm_row_scalar; "
      "int __tm_row_scalar_use(void) { return __tm_row_scalar[1][0]; }");
  global_var_t *row_scalar = find_test_global_var("__tm_row_scalar", 15);
  ASSERT_TRUE(row_scalar != NULL);
  const psx_type_t *row_scalar_type = ps_gvar_get_decl_type(row_scalar);
  ASSERT_EQ(PSX_TYPE_POINTER, row_scalar_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, row_scalar_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, row_scalar_type->base->base->kind);
  fn = as_function_definition(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *row_scalar_elem = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      row_scalar_elem = body->body[i]->lhs;
      break;
    }
  }
  ASSERT_TRUE(row_scalar_elem != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(row_scalar_elem)->kind);
  ASSERT_EQ(4, ps_node_type_size(row_scalar_elem));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(row_scalar_elem));

  parsed_code = parse_program_input(
      "int __tm_bool_matrix_use(void) { "
      "  _Bool m[2][3]; m[1][0] = 99; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *bool_matrix = find_func_lvar(fn, "m");
  ASSERT_TRUE(bool_matrix != NULL);
  const psx_type_t *bool_matrix_type = ps_lvar_get_decl_type(bool_matrix);
  ASSERT_TRUE(bool_matrix_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, bool_matrix_type->kind);
  ASSERT_TRUE(bool_matrix_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, bool_matrix_type->base->kind);
  ASSERT_TRUE(bool_matrix_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_matrix_type->base->base->kind);
  node_t *bool_matrix_node = psx_node_new_lvar_identifier_ref_for(bool_matrix);
  node_t *bool_matrix_row = ps_node_new_subscript_deref_for(
      bool_matrix_node, bool_matrix_node, ps_node_new_num(0));
  const psx_type_t *bool_matrix_row_type = ps_node_get_type(bool_matrix_row);
  ASSERT_TRUE(bool_matrix_row_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, bool_matrix_row_type->kind);
  ASSERT_TRUE(bool_matrix_row_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_matrix_row_type->base->kind);
  node_t *bool_matrix_elem = ps_node_new_subscript_deref_for(
      bool_matrix_row, bool_matrix_row, ps_node_new_num(0));
  const psx_type_t *bool_matrix_elem_type =
      ps_node_get_type(bool_matrix_elem);
  ASSERT_TRUE(bool_matrix_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, bool_matrix_elem_type->kind);

  parsed_code = parse_program_input(
      "int __tm_grid_a[2][3]; int __tm_grid_b[2][3]; "
      "typedef int (*__tm_RowPtr)[3]; "
      "int (*(*__tm_grid_ptrs)[2])[3] = &(int (*[2])[3]){__tm_grid_a, __tm_grid_b}; "
      "__tm_RowPtr *__tm_grid_ptr_list = (__tm_RowPtr[]){__tm_grid_a, __tm_grid_b}; "
      "int __tm_grid_use(void) { "
      "  return (*__tm_grid_ptrs)[0][0][1] + __tm_grid_ptr_list[1][0][2]; }");
  (void)parsed_code;
  global_var_t *grid_ptrs = find_test_global_var("__tm_grid_ptrs", 14);
  ASSERT_TRUE(grid_ptrs != NULL);
  const psx_type_t *grid_ptrs_type = ps_gvar_get_decl_type(grid_ptrs);
  ASSERT_TRUE(grid_ptrs_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, grid_ptrs_type->kind);
  ASSERT_TRUE(grid_ptrs_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, grid_ptrs_type->base->kind);
  ASSERT_TRUE(grid_ptrs_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, grid_ptrs_type->base->base->kind);
  node_t *grid_ptrs_node = ps_node_new_gvar_for(grid_ptrs);
  node_t *grid_rows = ps_node_new_unary_deref_for(grid_ptrs_node);
  const psx_type_t *grid_rows_type = ps_node_get_type(grid_rows);
  ASSERT_TRUE(grid_rows_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, grid_rows_type->kind);
  ASSERT_TRUE(grid_rows_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, grid_rows_type->base->kind);
  node_t *grid_rowptr_elem = parse_expr_input("(*__tm_grid_ptrs)[0]");
  const psx_type_t *grid_rowptr_elem_type =
      ps_node_get_type(grid_rowptr_elem);
  ASSERT_TRUE(grid_rowptr_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, grid_rowptr_elem_type->kind);
  ASSERT_TRUE(grid_rowptr_elem_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, grid_rowptr_elem_type->base->kind);
  ASSERT_EQ(12, ps_node_deref_size(grid_rowptr_elem));

  parsed_code = parse_program_input(
      "int __tm_param_nested_rows(int (*(*rows)[2])[3]) { "
      "  return (*rows)[0][1][2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *nested_rows = find_func_lvar(fn, "rows");
  ASSERT_TRUE(nested_rows != NULL);
  const psx_type_t *nested_rows_type = ps_lvar_get_decl_type(nested_rows);
  ASSERT_TRUE(nested_rows_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, nested_rows_type->kind);
  ASSERT_TRUE(nested_rows_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, nested_rows_type->base->kind);
  ASSERT_TRUE(nested_rows_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, nested_rows_type->base->base->kind);
  node_t *nested_rows_node = psx_node_new_lvar_identifier_ref_for(nested_rows);
  ASSERT_EQ(16, ps_node_deref_size(nested_rows_node));
  node_t *nested_rows_array = ps_node_new_unary_deref_for(nested_rows_node);
  const psx_type_t *nested_rows_array_type =
      ps_node_get_type(nested_rows_array);
  ASSERT_TRUE(nested_rows_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, nested_rows_array_type->kind);
  ASSERT_TRUE(nested_rows_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, nested_rows_array_type->base->kind);
  node_t *nested_rowptr_elem = ps_node_new_subscript_deref_for(
      nested_rows_array,
      nested_rows_array->lhs ? nested_rows_array->lhs : nested_rows_array,
      ps_node_new_num(0));
  const psx_type_t *nested_rowptr_elem_type =
      ps_node_get_type(nested_rowptr_elem);
  ASSERT_TRUE(nested_rowptr_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, nested_rowptr_elem_type->kind);
  ASSERT_TRUE(nested_rowptr_elem_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, nested_rowptr_elem_type->base->kind);
  ASSERT_EQ(12, ps_node_deref_size(nested_rowptr_elem));

  parsed_code = parse_program_input(
      "int __tm_param_canonical(int a[2][3], int (*p)[3], "
      "int *q[3], int (*cb)(int)) { return a[0][0] + p[0][0] + *q[0] + cb(0); }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *canonical_a = find_func_lvar(fn, "a");
  lvar_t *canonical_p = find_func_lvar(fn, "p");
  lvar_t *canonical_q = find_func_lvar(fn, "q");
  lvar_t *canonical_cb = find_func_lvar(fn, "cb");
  ASSERT_TRUE(canonical_a != NULL);
  ASSERT_TRUE(canonical_p != NULL);
  ASSERT_TRUE(canonical_q != NULL);
  ASSERT_TRUE(canonical_cb != NULL);
  const psx_type_t *canonical_a_type = ps_lvar_get_decl_type(canonical_a);
  const psx_type_t *canonical_p_type = ps_lvar_get_decl_type(canonical_p);
  const psx_type_t *canonical_q_type = ps_lvar_get_decl_type(canonical_q);
  const psx_type_t *canonical_cb_type = ps_lvar_get_decl_type(canonical_cb);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_a_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_a_type->base->kind);
  ASSERT_EQ(3, canonical_a_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_a_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_p_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_p_type->base->kind);
  ASSERT_EQ(3, canonical_p_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_p_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_q_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_q_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_q_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_cb_type->kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION, canonical_cb_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_cb_type->base->base->kind);
  assert_canonical_type_signature(canonical_cb_type, "p<i32(i32)>");

  parsed_code = parse_program_input(
      "struct __tm_member_canonical { int a[2][3]; int (*p)[3]; "
      "int *q[3]; int (**cb)(int); }; ");
  (void)parsed_code;
  tag_member_info_t canonical_member_a = {0};
  tag_member_info_t canonical_member_p = {0};
  tag_member_info_t canonical_member_q = {0};
  tag_member_info_t canonical_member_cb = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, "__tm_member_canonical", 21, "a", 1, &canonical_member_a));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, "__tm_member_canonical", 21, "p", 1, &canonical_member_p));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, "__tm_member_canonical", 21, "q", 1, &canonical_member_q));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, "__tm_member_canonical", 21, "cb", 2, &canonical_member_cb));
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_member_a.decl_type->kind);
  ASSERT_EQ(2, canonical_member_a.decl_type->array_len);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_member_a.decl_type->base->kind);
  ASSERT_EQ(3, canonical_member_a.decl_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_p.decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_member_p.decl_type->base->kind);
  ASSERT_EQ(3, canonical_member_p.decl_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_ARRAY, canonical_member_q.decl_type->kind);
  ASSERT_EQ(3, canonical_member_q.decl_type->array_len);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_q.decl_type->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_cb.decl_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_cb.decl_type->base->kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            canonical_member_cb.decl_type->base->base->kind);
  assert_canonical_type_signature(
      canonical_member_cb.decl_type, "p<p<i32(i32)>>");

  const char *qualified_member_tag = "__tm_member_qualified";
  parsed_code = parse_program_input(
      "struct __tm_member_qualified { const int c; "
      "volatile unsigned long long v; _Atomic int a; char pad; "
      "_Alignas(8) int y; }; ");
  (void)parsed_code;
  tag_member_info_t qualified_member_c = {0};
  tag_member_info_t qualified_member_v = {0};
  tag_member_info_t qualified_member_a = {0};
  tag_member_info_t qualified_member_y = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)qualified_member_tag,
      (int)strlen(qualified_member_tag), "c", 1, &qualified_member_c));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)qualified_member_tag,
      (int)strlen(qualified_member_tag), "v", 1, &qualified_member_v));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)qualified_member_tag,
      (int)strlen(qualified_member_tag), "a", 1, &qualified_member_a));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)qualified_member_tag,
      (int)strlen(qualified_member_tag), "y", 1, &qualified_member_y));
  ASSERT_TRUE(ps_type_has_qualifier(qualified_member_c.decl_type, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(ps_type_has_qualifier(qualified_member_v.decl_type, PSX_TYPE_QUALIFIER_VOLATILE));
  ASSERT_TRUE(qualified_member_v.decl_type->is_unsigned);
  ASSERT_TRUE(qualified_member_v.decl_type->is_long_long);
  ASSERT_EQ(8, ps_type_sizeof(qualified_member_v.decl_type));
  ASSERT_TRUE(ps_type_has_qualifier(qualified_member_a.decl_type, PSX_TYPE_QUALIFIER_ATOMIC));
  ASSERT_EQ(0, qualified_member_y.offset % 8);

  parsed_code = parse_program_input(
      "int __tm_local_ptr_rows(void) { "
      "  int a[2][3]; int (*m[2][2])[3] = {{a, a}, {a, a}}; "
      "  return m[0][0][1][2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *local_ptr_rows = find_func_lvar(fn, "m");
  ASSERT_TRUE(local_ptr_rows != NULL);
  const psx_type_t *local_ptr_rows_type = ps_lvar_get_decl_type(local_ptr_rows);
  ASSERT_TRUE(local_ptr_rows_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_type->kind);
  ASSERT_TRUE(local_ptr_rows_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_type->base->kind);
  ASSERT_TRUE(local_ptr_rows_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_ptr_rows_type->base->base->kind);
  ASSERT_TRUE(local_ptr_rows_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_type->base->base->base->kind);
  node_t *local_ptr_rows_elem =
      ps_node_new_array_elem_lvar_for(local_ptr_rows, 0);
  const psx_type_t *local_ptr_rows_elem_type =
      ps_node_get_type(local_ptr_rows_elem);
  ASSERT_TRUE(local_ptr_rows_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_ptr_rows_elem_type->kind);
  ASSERT_TRUE(local_ptr_rows_elem_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_elem_type->base->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(local_ptr_rows_elem));
  ASSERT_EQ(12, ps_node_deref_size(local_ptr_rows_elem));
  node_t *local_ptr_rows_slot = ps_node_new_lvar_typed_at_for(
      local_ptr_rows, local_ptr_rows->offset,
      ps_lvar_array_scalar_element_size(local_ptr_rows));
  const psx_type_t *local_ptr_rows_slot_type =
      ps_node_get_type(local_ptr_rows_slot);
  ASSERT_TRUE(local_ptr_rows_slot_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_ptr_rows_slot_type->kind);
  ASSERT_TRUE(local_ptr_rows_slot_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_ptr_rows_slot_type->base->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(local_ptr_rows_slot));
  ASSERT_EQ(12, ps_node_deref_size(local_ptr_rows_slot));

  parsed_code = parse_program_input(
      "int __tm_local_scalar_array_flags(void) { "
      "  unsigned char u[2]; _Bool b[2]; int i[2]; return u[0] + b[0] + i[0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *local_scalar_u = find_func_lvar(fn, "u");
  ASSERT_TRUE(local_scalar_u != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(local_scalar_u) != NULL);
  node_t *local_scalar_u_elem =
      ps_node_new_array_elem_lvar_for(local_scalar_u, 0);
  ASSERT_TRUE(ps_node_is_unsigned_type(local_scalar_u_elem));
  node_t *local_scalar_u_slot = ps_node_new_lvar_typed_at_for(
      local_scalar_u, local_scalar_u->offset,
      ps_lvar_array_scalar_element_size(local_scalar_u));
  ASSERT_TRUE(ps_node_is_unsigned_type(local_scalar_u_slot));

  lvar_t *local_scalar_b = find_func_lvar(fn, "b");
  ASSERT_TRUE(local_scalar_b != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(local_scalar_b) != NULL);
  node_t *local_scalar_b_elem =
      ps_node_new_array_elem_lvar_for(local_scalar_b, 0);
  ASSERT_TRUE(ps_node_get_type(local_scalar_b_elem) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(local_scalar_b_elem)->kind);
  node_t *local_scalar_b_slot = ps_node_new_lvar_typed_at_for(
      local_scalar_b, local_scalar_b->offset,
      ps_lvar_array_scalar_element_size(local_scalar_b));
  ASSERT_TRUE(ps_node_get_type(local_scalar_b_slot) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(local_scalar_b_slot)->kind);

  lvar_t *local_scalar_i = find_func_lvar(fn, "i");
  ASSERT_TRUE(local_scalar_i != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(local_scalar_i) != NULL);
  node_t *local_scalar_i_addr =
      ps_node_new_lvar_array_addr_for(local_scalar_i);
  ASSERT_TRUE(ps_node_get_type(local_scalar_i_addr) != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, canonical_node_pointee_fp_kind(local_scalar_i_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(local_scalar_i_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(local_scalar_i_addr));
  ASSERT_EQ(4, ps_node_deref_size(local_scalar_i_addr));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(local_scalar_i_addr));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(local_scalar_i_addr));

  parsed_code = parse_program_input(
      "int __tm_larr_base(void) { int a[2]; return a[0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *larr_base_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(larr_base_a != NULL);
  ASSERT_TRUE(ps_lvar_get_decl_type(larr_base_a) != NULL);
  node_t *larr_base_node = psx_node_new_lvar_identifier_ref_for(larr_base_a);
  ASSERT_EQ(4, canonical_node_base_deref_size(larr_base_node));

  parsed_code = parse_program_input(
      "struct __tm_byref_S { long a; long b; long c; }; "
      "long __tm_byref(struct __tm_byref_S s) { return s.b; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *byref_s = find_func_lvar(fn, "s");
  ASSERT_TRUE(byref_s != NULL);
  ASSERT_TRUE(byref_s->is_byref_param);
  const psx_type_t *byref_s_type = ps_lvar_get_decl_type(byref_s);
  ASSERT_TRUE(byref_s_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, byref_s_type->kind);
  ASSERT_EQ(0, ps_type_sizeof(byref_s_type));
  ASSERT_EQ(24, ps_ctx_type_sizeof_in(
                    test_semantic_context(), byref_s_type));
  node_t *byref_s_node = psx_node_new_lvar_identifier_ref_for(byref_s);
  ASSERT_EQ(ND_LVAR, byref_s_node->kind);
  ASSERT_TRUE(ps_node_get_type(byref_s_node) == byref_s_type);
  ASSERT_EQ(0, ps_node_type_size(byref_s_node));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(byref_s_node));
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(byref_s_node)->kind);

  lvar_t byref_s_stale = *byref_s;
  byref_s_stale.size = 99;
  node_t *byref_s_stale_node = psx_node_new_lvar_identifier_ref_for(&byref_s_stale);
  ASSERT_TRUE(ps_node_get_type(byref_s_stale_node) == byref_s_type);
  ASSERT_EQ(0, ps_node_type_size(byref_s_stale_node));
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(byref_s_stale_node)->kind);

  parsed_code = parse_program_input(
      "int __tm_vla_sidecar(int m) { int a[2][3]; int (*p)[m] = a; return sizeof(p); }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *vla_ptr_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(vla_ptr_lvar != NULL);
  ASSERT_TRUE(vla_ptr_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, vla_ptr_lvar->decl_type->kind);
  ASSERT_TRUE(ps_type_contains_vla_array(vla_ptr_lvar->decl_type));
  ASSERT_EQ(8, ps_lvar_decl_sizeof(vla_ptr_lvar, 99));
  ASSERT_EQ(16, ps_lvar_storage_size(vla_ptr_lvar, 99));
  ASSERT_TRUE(ps_lvar_vla_row_stride_frame_off(vla_ptr_lvar) > 0);
  body = as_block(fn->base.rhs);
  node_t *vla_ptr_sizeof_return = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      vla_ptr_sizeof_return = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(vla_ptr_sizeof_return != NULL);
  ASSERT_EQ(ND_NUM, vla_ptr_sizeof_return->lhs->kind);
  ASSERT_EQ(8, as_num(vla_ptr_sizeof_return->lhs)->val);
  node_t *vla_ptr_node = psx_node_new_lvar_identifier_ref_for(vla_ptr_lvar);
  int vla_ptr_inner =
      canonical_node_array_subscript_stride_bytes(vla_ptr_node, 0);
  ASSERT_EQ(4, vla_ptr_inner);
  ASSERT_EQ(ps_lvar_vla_row_stride_frame_off(vla_ptr_lvar),
            ps_node_vla_row_stride_frame_off(vla_ptr_node));
  node_t *vla_ptr_row = ps_node_new_subscript_deref_for(
      vla_ptr_node, vla_ptr_node, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(vla_ptr_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(vla_ptr_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(vla_ptr_row));

  parsed_code = parse_program_input(
      "int __tm_vla_ptr_arith(int m, int a[2][3]) { "
      "int (*p)[m] = a; return (*(p + 1))[2]; }");
  fn = as_function_definition(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *vla_ptr_arith_elem = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      vla_ptr_arith_elem = body->body[i]->lhs;
      break;
    }
  }
  ASSERT_TRUE(vla_ptr_arith_elem != NULL);
  ASSERT_EQ(ND_DEREF, vla_ptr_arith_elem->kind);
  ASSERT_EQ(4, ps_node_type_size(vla_ptr_arith_elem));
  ASSERT_TRUE(vla_ptr_arith_elem->lhs != NULL);
  ASSERT_EQ(ND_ADD, vla_ptr_arith_elem->lhs->kind);
  ASSERT_TRUE(vla_ptr_arith_elem->lhs->lhs != NULL);
  ASSERT_TRUE(vla_ptr_arith_elem->lhs->lhs->kind != ND_DEREF);

  parsed_code = parse_program_input(
      "int __tm_vla_fp_sidecar(int m) { double a[2][3]; double (*p)[m] = a; "
      "p[0][1] = 9.5; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *vla_fp_array_lvar = find_func_lvar(fn, "a");
  ASSERT_TRUE(vla_fp_array_lvar != NULL);
  node_t *vla_fp_array_node =
      psx_node_new_lvar_identifier_ref_for(vla_fp_array_lvar);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            canonical_node_pointee_fp_kind(vla_fp_array_node));
  lvar_t *vla_fp_ptr_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(vla_fp_ptr_lvar != NULL);
  node_t *vla_fp_ptr_node =
      psx_node_new_lvar_identifier_ref_for(vla_fp_ptr_lvar);
  ASSERT_EQ(ps_lvar_vla_row_stride_frame_off(vla_fp_ptr_lvar),
            ps_node_vla_row_stride_frame_off(vla_fp_ptr_node));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            canonical_node_pointee_fp_kind(vla_fp_ptr_node));

  parsed_code = parse_program_input(
      "int __tm_vla_fp_leaf(int n) { double a[n]; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *vla_fp_leaf_lvar = find_func_lvar(fn, "a");
  ASSERT_TRUE(vla_fp_leaf_lvar != NULL);
  const psx_type_t *vla_fp_leaf = vla_fp_leaf_lvar->decl_type;
  while (vla_fp_leaf && vla_fp_leaf->kind == PSX_TYPE_ARRAY)
    vla_fp_leaf = vla_fp_leaf->base;
  ASSERT_TRUE(vla_fp_leaf != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, vla_fp_leaf->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, vla_fp_leaf->fp_kind);

  parsed_code = parse_program_input(
      "struct __tm_ptrarr_S { int x; }; "
      "int __tm_ptrarr(void) { const struct __tm_ptrarr_S a[1] = {{1}}; "
      "const struct __tm_ptrarr_S (*p)[1] = &a; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *ptrarr_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(ptrarr_p != NULL);
  ASSERT_TRUE(ptrarr_p->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptrarr_p->decl_type->kind);
  ASSERT_EQ(0, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                   ptrarr_p->decl_type));
  ASSERT_EQ(4, ps_ctx_type_sizeof_in(
                   test_semantic_context(), ptrarr_p->decl_type->base));
  node_t *ptrarr_p_node = psx_node_new_lvar_identifier_ref_for(ptrarr_p);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptrarr_p_node));
  ASSERT_EQ(0, ps_node_deref_size(ptrarr_p_node));
  node_t *ptrarr_row = ps_node_new_unary_deref_for(ptrarr_p_node);
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptrarr_row));
  ASSERT_EQ(0, ps_node_type_size(ptrarr_row));

  lvar_t tmp_fp_ptr_lvar = {0};
  tmp_fp_ptr_lvar.size = 8;
  set_test_storage_fixture_type(
      &tmp_fp_ptr_lvar,
      ps_type_new_pointer(
          ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8)));
  const psx_type_t *tmp_fp_ptr_double = tmp_fp_ptr_lvar.decl_type;
  ASSERT_TRUE(tmp_fp_ptr_double != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_fp_ptr_double->kind);
  ASSERT_TRUE(tmp_fp_ptr_double->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, tmp_fp_ptr_double->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tmp_fp_ptr_double->base->fp_kind);
  ASSERT_TRUE(ps_lvar_value_is_pointer_like(&tmp_fp_ptr_lvar));

  lvar_t tmp_void_ptr_lvar = {0};
  tmp_void_ptr_lvar.size = 8;
  psx_type_t *tmp_void_type = ps_type_new(PSX_TYPE_VOID);
  tmp_void_type->scalar_kind = TK_VOID;
  set_test_storage_fixture_type(
      &tmp_void_ptr_lvar, ps_type_new_pointer(tmp_void_type));
  ASSERT_TRUE(ps_lvar_value_is_pointer_like(&tmp_void_ptr_lvar));

  lvar_t tmp_complex_lvar = {0};
  tmp_complex_lvar.size = 16;
  psx_type_t *canonical_complex = ps_type_new(PSX_TYPE_COMPLEX);
  canonical_complex->fp_kind = TK_FLOAT_KIND_DOUBLE;
  set_test_storage_fixture_type(&tmp_complex_lvar, canonical_complex);
  const psx_type_t *tmp_complex_type = tmp_complex_lvar.decl_type;
  ASSERT_TRUE(tmp_complex_type != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, tmp_complex_type->kind);
  node_t *tmp_complex_node = psx_node_new_lvar_for(&tmp_complex_lvar);
  ASSERT_TRUE(ps_node_get_type(tmp_complex_node) == tmp_complex_type);
  ASSERT_EQ(PSX_TYPE_COMPLEX, ps_node_get_type(tmp_complex_node)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(tmp_complex_node));

  node_t *tmp_complex_slot =
      ps_node_new_lvar_fp_slot_for(&tmp_complex_lvar, tmp_complex_lvar.offset, 8);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(tmp_complex_slot));
  ASSERT_TRUE(ps_node_get_type(tmp_complex_slot) != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, ps_node_get_type(tmp_complex_slot)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_get_type(tmp_complex_slot)->fp_kind);

  node_t typed_complex_ptr_mem = {0};
  typed_complex_ptr_mem.kind = ND_DEREF;
  typed_complex_ptr_mem.type = ps_type_new_pointer(tmp_complex_type);
  node_t *typed_complex_deref =
      ps_node_new_unary_deref_for(&typed_complex_ptr_mem);
  ASSERT_TRUE(ps_node_get_type(typed_complex_deref) == tmp_complex_type);
  ASSERT_TRUE(ps_node_value_is_complex(typed_complex_deref));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(typed_complex_deref));

  node_t typed_mem = {0};
  typed_mem.kind = ND_LVAR;
  typed_mem.type = ps_type_new_integer(TK_LONG, 8, 0);
  ASSERT_TRUE(ps_node_get_type(&typed_mem) == typed_mem.type);
  ASSERT_EQ(8, ps_node_type_size(&typed_mem));
  node_t typed_stale_scalar_size_mem = {0};
  typed_stale_scalar_size_mem.kind = ND_LVAR;
  typed_stale_scalar_size_mem.type = ps_type_new_integer(TK_UNSIGNED, 1, 1);
  ASSERT_EQ(4, ps_node_type_size(&typed_stale_scalar_size_mem));
  ASSERT_EQ(4, ps_node_storage_type_size(&typed_stale_scalar_size_mem));
  node_t typed_incomplete_size_mem = {0};
  typed_incomplete_size_mem.kind = ND_LVAR;
  typed_incomplete_size_mem.type =
      ps_type_new_tag(TK_STRUCT, "Incomplete", 10, 0, 0);
  ASSERT_EQ(0, ps_node_storage_type_size(&typed_incomplete_size_mem));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(&typed_mem));
  ASSERT_EQ(0, ps_node_deref_size(&typed_mem));
  ASSERT_TRUE(!ps_node_is_unsigned_type(&typed_mem));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(&typed_mem));
  ASSERT_TRUE(!ps_node_is_long_long_type(&typed_mem));
  ASSERT_TRUE(!ps_node_is_plain_char_type(&typed_mem));
  ASSERT_TRUE(!ps_node_is_long_double_type(&typed_mem));
  ASSERT_TRUE(!ps_type_is_tag_aggregate(typed_mem.type));
  ASSERT_EQ(TK_EOF, typed_mem.type->tag_kind);
  ASSERT_EQ(0, typed_mem.type->tag_scope_depth_p1);
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(&typed_mem, 0));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(&typed_mem));
  ASSERT_EQ(0, canonical_node_base_deref_size(&typed_mem));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(&typed_mem));

  node_t typed_stale_value_flags_mem = {0};
  typed_stale_value_flags_mem.kind = ND_LVAR;
  typed_stale_value_flags_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind(&typed_stale_value_flags_mem));
  ASSERT_TRUE(!ps_node_value_is_complex(&typed_stale_value_flags_mem));

  node_t typed_stale_pointee_flags_mem = {0};
  typed_stale_pointee_flags_mem.kind = ND_LVAR;
  typed_stale_pointee_flags_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(&typed_stale_pointee_flags_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(&typed_stale_pointee_flags_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_void(&typed_stale_pointee_flags_mem));

  node_t typed_default_int_mem = {0};
  typed_default_int_mem.kind = ND_LVAR;
  typed_default_int_mem.type = ps_type_new(PSX_TYPE_INTEGER);
  ASSERT_EQ(4, ps_node_type_size(&typed_default_int_mem));

  node_t typed_unsigned_mem = {0};
  typed_unsigned_mem.kind = ND_LVAR;
  typed_unsigned_mem.type = ps_type_new_integer(TK_UNSIGNED, 4, 1);
  ASSERT_TRUE(ps_node_is_unsigned_type(&typed_unsigned_mem));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(&typed_unsigned_mem));

  node_t typed_bool_lhs_mem = {0};
  typed_bool_lhs_mem.kind = ND_LVAR;
  typed_bool_lhs_mem.type = ps_type_new(PSX_TYPE_BOOL);
  node_t *typed_bool_assign =
      ps_node_new_assign(&typed_bool_lhs_mem, ps_node_new_num(3));
  ASSERT_TRUE(typed_bool_assign->rhs != NULL);
  ASSERT_EQ(ND_NUM, typed_bool_assign->rhs->kind);
  analyze_test_expression(typed_bool_assign, NULL);
  ASSERT_EQ(ND_NE, typed_bool_assign->rhs->kind);

  node_t typed_stale_bool_lhs_mem = {0};
  typed_stale_bool_lhs_mem.kind = ND_LVAR;
  typed_stale_bool_lhs_mem.type = ps_type_new_integer(TK_INT, 4, 0);
  node_t *typed_stale_bool_assign =
      ps_node_new_assign(&typed_stale_bool_lhs_mem, ps_node_new_num(3));
  ASSERT_TRUE(typed_stale_bool_assign->rhs != NULL);
  ASSERT_EQ(ND_NUM, typed_stale_bool_assign->rhs->kind);

  node_t typed_assign_ptr_lhs_mem = {0};
  typed_assign_ptr_lhs_mem.kind = ND_LVAR;
  typed_assign_ptr_lhs_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_t *typed_assign_ptr =
      ps_node_new_assign(&typed_assign_ptr_lhs_mem, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_value_is_pointer_like(typed_assign_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_assign_ptr));
  ASSERT_EQ(4, ps_node_deref_size(typed_assign_ptr));
  ASSERT_EQ(4, canonical_node_base_deref_size(typed_assign_ptr));
  ASSERT_TRUE(ps_node_get_type(typed_assign_ptr) ==
              typed_assign_ptr_lhs_mem.type);

  node_t typed_assign_complex_lhs_mem = {0};
  typed_assign_complex_lhs_mem.kind = ND_LVAR;
  psx_type_t *typed_assign_complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  typed_assign_complex_type->fp_kind = TK_FLOAT_KIND_DOUBLE;
  typed_assign_complex_lhs_mem.type = typed_assign_complex_type;
  node_t *typed_assign_complex = ps_node_new_assign(
      &typed_assign_complex_lhs_mem, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(typed_assign_complex) ==
              typed_assign_complex_lhs_mem.type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_value_fp_kind(typed_assign_complex));
  ASSERT_TRUE(ps_node_value_is_complex(typed_assign_complex));

  node_t typed_assign_atomic_lhs_mem = {0};
  typed_assign_atomic_lhs_mem.kind = ND_LVAR;
  psx_type_t *typed_assign_atomic_type =
      ps_type_new_integer(TK_INT, 4, 0);
  ps_type_add_qualifiers(typed_assign_atomic_type, PSX_TYPE_QUALIFIER_ATOMIC);
  typed_assign_atomic_lhs_mem.type = typed_assign_atomic_type;
  node_t *typed_assign_atomic = ps_node_new_assign(
      &typed_assign_atomic_lhs_mem, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(typed_assign_atomic) ==
              typed_assign_atomic_lhs_mem.type);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(typed_assign_atomic), PSX_TYPE_QUALIFIER_ATOMIC));

  node_t typed_addr_ptr_operand_mem = {0};
  typed_addr_ptr_operand_mem.kind = ND_LVAR;
  psx_type_t *typed_addr_ptr_operand_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ps_type_add_qualifiers(typed_addr_ptr_operand_type, PSX_TYPE_QUALIFIER_CONST);
  typed_addr_ptr_operand_mem.type = typed_addr_ptr_operand_type;
  node_t *typed_addr_ptr =
      ps_node_new_unary_addr_for(&typed_addr_ptr_operand_mem);
  ASSERT_TRUE(ps_node_value_is_pointer_like(typed_addr_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_addr_ptr));
  ASSERT_EQ(8, ps_node_deref_size(typed_addr_ptr));
  ASSERT_EQ(2, canonical_node_pointer_qual_levels(typed_addr_ptr));
  ASSERT_EQ(4, canonical_node_base_deref_size(typed_addr_ptr));
  assert_node_pointer_qualifiers(typed_addr_ptr, "01", "00");
  ASSERT_TRUE(ps_node_get_type(typed_addr_ptr)->base ==
              typed_addr_ptr_operand_mem.type);

  node_t typed_addr_unsigned_operand_mem = {0};
  typed_addr_unsigned_operand_mem.kind = ND_LVAR;
  typed_addr_unsigned_operand_mem.type =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  node_t *typed_addr_unsigned =
      ps_node_new_unary_addr_for(&typed_addr_unsigned_operand_mem);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(typed_addr_unsigned));

  node_t typed_addr_bool_operand_mem = {0};
  typed_addr_bool_operand_mem.kind = ND_LVAR;
  psx_type_t *typed_addr_bool_operand_type = ps_type_new(PSX_TYPE_BOOL);
  typed_addr_bool_operand_mem.type = typed_addr_bool_operand_type;
  node_t *typed_addr_bool =
      ps_node_new_addr_value_for(&typed_addr_bool_operand_mem);
  ASSERT_TRUE(canonical_node_pointee_is_bool(typed_addr_bool));

  node_t typed_deref_ptrptr_operand_mem = {0};
  typed_deref_ptrptr_operand_mem.kind = ND_DEREF;
  psx_type_t *typed_deref_inner_ptr =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  psx_type_t *typed_deref_outer_ptr =
      ps_type_new_pointer(typed_deref_inner_ptr);
  typed_deref_ptrptr_operand_mem.type = typed_deref_outer_ptr;
  node_t *typed_deref_ptr =
      ps_node_new_unary_deref_for(&typed_deref_ptrptr_operand_mem);
  ASSERT_TRUE(ps_node_value_is_pointer_like(typed_deref_ptr));
  ASSERT_EQ(8, ps_node_type_size(typed_deref_ptr));
  ASSERT_EQ(4, ps_node_deref_size(typed_deref_ptr));
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(typed_deref_ptr));
  ASSERT_EQ(4, canonical_node_base_deref_size(typed_deref_ptr));
  ASSERT_TRUE(ps_node_get_type(typed_deref_ptr) == typed_deref_inner_ptr);

  node_t typed_deref_flat_ptrptr_operand_mem = {0};
  typed_deref_flat_ptrptr_operand_mem.kind = ND_DEREF;
  psx_type_t *typed_deref_flat_ptrptr =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  typed_deref_flat_ptrptr_operand_mem.type = typed_deref_flat_ptrptr;
  node_t *typed_deref_flat_ptr =
      ps_node_new_unary_deref_for(&typed_deref_flat_ptrptr_operand_mem);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(typed_deref_flat_ptr));
  ASSERT_EQ(4, ps_node_type_size(typed_deref_flat_ptr));
  ASSERT_EQ(0, ps_node_deref_size(typed_deref_flat_ptr));
  ASSERT_EQ(0, canonical_node_pointer_qual_levels(typed_deref_flat_ptr));
  ASSERT_TRUE(ps_node_get_type(typed_deref_flat_ptr) ==
              typed_deref_flat_ptrptr->base);

  node_t typed_nested_ptr_stale_quals_mem = {0};
  typed_nested_ptr_stale_quals_mem.kind = ND_LVAR;
  psx_type_t *typed_nested_ptr_inner =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ps_type_add_qualifiers(typed_nested_ptr_inner, PSX_TYPE_QUALIFIER_CONST);
  ps_type_add_qualifiers(typed_nested_ptr_inner, PSX_TYPE_QUALIFIER_VOLATILE);
  psx_type_t *typed_nested_ptr_outer =
      ps_type_new_pointer(typed_nested_ptr_inner);
  ps_type_add_qualifiers(typed_nested_ptr_outer, PSX_TYPE_QUALIFIER_CONST);
  typed_nested_ptr_stale_quals_mem.type = typed_nested_ptr_outer;
  ASSERT_EQ(2, canonical_node_pointer_qual_levels(
                   &typed_nested_ptr_stale_quals_mem));
  assert_node_pointer_qualifiers(
      &typed_nested_ptr_stale_quals_mem, "11", "01");

  node_t canonical_nested_ptr = {0};
  canonical_nested_ptr.kind = ND_DEREF;
  psx_type_t *raw_nested_ptr_inner = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ps_type_add_qualifiers(raw_nested_ptr_inner, PSX_TYPE_QUALIFIER_CONST);
  ps_type_add_qualifiers(raw_nested_ptr_inner, PSX_TYPE_QUALIFIER_VOLATILE);
  psx_type_t *raw_nested_ptr_middle =
      ps_type_new_pointer(raw_nested_ptr_inner);
  ps_type_add_qualifiers(raw_nested_ptr_middle, PSX_TYPE_QUALIFIER_VOLATILE);
  psx_type_t *raw_nested_ptr_type =
      ps_type_new_pointer(raw_nested_ptr_middle);
  ps_type_add_qualifiers(raw_nested_ptr_type, PSX_TYPE_QUALIFIER_CONST);
  canonical_nested_ptr.type = raw_nested_ptr_type;
  ASSERT_TRUE(raw_nested_ptr_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, raw_nested_ptr_type->kind);
  assert_pointer_qualifiers(raw_nested_ptr_type, "101", "011");
  ASSERT_TRUE(raw_nested_ptr_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, raw_nested_ptr_type->base->kind);
  assert_pointer_qualifiers(raw_nested_ptr_type->base, "01", "11");
  ASSERT_TRUE(raw_nested_ptr_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, raw_nested_ptr_type->base->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(raw_nested_ptr_type->base->base));
  ASSERT_TRUE(ps_node_get_type(&canonical_nested_ptr) ==
              raw_nested_ptr_type);
  ASSERT_EQ(3, ps_type_pointer_depth(
                   ps_node_get_type(&canonical_nested_ptr)));

  node_t typed_ptr_mem = {0};
  typed_ptr_mem.kind = ND_LVAR;
  psx_type_t *typed_ptr_base = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  ps_type_add_qualifiers(typed_ptr_base, PSX_TYPE_QUALIFIER_CONST);
  psx_type_t *typed_ptr_row = ps_type_new_array(
      typed_ptr_base, 2, 16, 1);
  psx_type_t *typed_ptr_type = ps_type_new_pointer(typed_ptr_row);
  ps_type_add_qualifiers(typed_ptr_type, PSX_TYPE_QUALIFIER_CONST);
  ps_type_add_qualifiers(typed_ptr_type, PSX_TYPE_QUALIFIER_VOLATILE);
  typed_ptr_mem.type = typed_ptr_type;
  ps_node_set_vla_runtime_view(&typed_ptr_mem, 24, 3);
  ASSERT_TRUE(ps_node_value_is_pointer_like(&typed_ptr_mem));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(&typed_ptr_mem));
  ASSERT_EQ(0, ps_node_deref_size(&typed_ptr_mem));
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(&typed_ptr_mem));
  assert_node_pointer_qualifiers(&typed_ptr_mem, "1", "1");
  ASSERT_EQ(8, canonical_node_base_deref_size(&typed_ptr_mem));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(&typed_ptr_mem));
  ASSERT_EQ(24, ps_node_vla_row_stride_frame_off(&typed_ptr_mem));
  int typed_inner_stride =
      canonical_node_array_subscript_stride_bytes(&typed_ptr_mem, 0);
  int typed_next_stride =
      canonical_node_array_subscript_stride_bytes(&typed_ptr_mem, 1);
  ASSERT_EQ(8, typed_inner_stride);
  ASSERT_EQ(0, typed_next_stride);
  node_t *typed_vla_sub = ps_node_new_subscript_deref_for(
      &typed_ptr_mem, ps_node_new_num(0), ps_node_new_num(0));
  ASSERT_EQ(32, ps_node_vla_row_stride_frame_off(typed_vla_sub));

  node_t typed_plain_ptr_stale_ptr_array = {0};
  typed_plain_ptr_stale_ptr_array.kind = ND_LVAR;
  typed_plain_ptr_stale_ptr_array.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_node_value_is_pointer_like(&typed_plain_ptr_stale_ptr_array));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(&typed_plain_ptr_stale_ptr_array));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(&typed_plain_ptr_stale_ptr_array));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   &typed_plain_ptr_stale_ptr_array, 0));
  node_t typed_scalar_array_stale_stride = {0};
  typed_scalar_array_stale_stride.kind = ND_LVAR;
  typed_scalar_array_stale_stride.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 4, 16, 0);
  ASSERT_EQ(4, canonical_node_array_subscript_stride_bytes(
                   &typed_scalar_array_stale_stride, 0));
  node_t typed_addr_stale_mem_stride = {0};
  typed_addr_stale_mem_stride.kind = ND_ADDR;
  typed_addr_stale_mem_stride.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   &typed_addr_stale_mem_stride, 0));
  node_t typed_deref_stale_mem_stride = typed_addr_stale_mem_stride;
  typed_deref_stale_mem_stride.kind = ND_DEREF;
  typed_deref_stale_mem_stride.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 4, 16, 0);
  ASSERT_EQ(4, canonical_node_array_subscript_stride_bytes(
                   &typed_deref_stale_mem_stride, 0));
  ASSERT_EQ(2, ps_node_vla_strides_remaining(typed_vla_sub));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, canonical_node_pointee_fp_kind(&typed_ptr_mem));
  ASSERT_TRUE(canonical_node_pointee_is_const_qualified(&typed_ptr_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_volatile_qualified(&typed_ptr_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(&typed_ptr_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(&typed_ptr_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_void(&typed_ptr_mem));

  node_t typed_ptr_stale_base_deref_mem = {0};
  typed_ptr_stale_base_deref_mem.kind = ND_LVAR;
  typed_ptr_stale_base_deref_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(4, canonical_node_base_deref_size(&typed_ptr_stale_base_deref_mem));

  node_t typed_ptrarr_stale_base_deref_mem = {0};
  typed_ptrarr_stale_base_deref_mem.kind = ND_LVAR;
  psx_type_t *typed_ptrarr_stale_base =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0);
  typed_ptrarr_stale_base_deref_mem.type =
      ps_type_new_pointer(typed_ptrarr_stale_base);
  ASSERT_EQ(4, canonical_node_base_deref_size(&typed_ptrarr_stale_base_deref_mem));
  ASSERT_EQ(12, canonical_node_ptr_array_pointee_bytes(
                    &typed_ptrarr_stale_base_deref_mem));

  node_t typed_signed_ptr_stale_unsigned_mem = {0};
  typed_signed_ptr_stale_unsigned_mem.kind = ND_LVAR;
  typed_signed_ptr_stale_unsigned_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_t *typed_signed_ptr_sub = ps_node_new_subscript_deref_for(
      &typed_signed_ptr_stale_unsigned_mem, ps_node_new_num(0),
      ps_node_new_num(0));
  ASSERT_TRUE(!ps_node_is_unsigned_type(typed_signed_ptr_sub));

  node_t typed_ptr_stale_pointee_scalar_ptr_mem = {0};
  typed_ptr_stale_pointee_scalar_ptr_mem.kind = ND_LVAR;
  typed_ptr_stale_pointee_scalar_ptr_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_t *typed_ptr_stale_pointee_scalar_sub =
      ps_node_new_subscript_deref_for(
          &typed_ptr_stale_pointee_scalar_ptr_mem, ps_node_new_num(0),
          ps_node_new_num(0));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(typed_ptr_stale_pointee_scalar_sub));
  ASSERT_EQ(0, ps_node_deref_size(typed_ptr_stale_pointee_scalar_sub));

  node_t typed_deref_stale_scalar_ptr_member = {0};
  typed_deref_stale_scalar_ptr_member.kind = ND_DEREF;
  typed_deref_stale_scalar_ptr_member.type = ps_type_new_pointer(
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0)));
  ASSERT_TRUE(!ps_node_scalar_ptr_member_lvalue(
      &typed_deref_stale_scalar_ptr_member));
  node_t *typed_deref_sub = ps_node_new_subscript_deref_for(
      &typed_deref_stale_scalar_ptr_member, ps_node_new_num(0),
      ps_node_new_num(0));
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(typed_deref_sub));
  ASSERT_EQ(4, canonical_node_base_deref_size(typed_deref_sub));

  node_t subscript_row_lvalue = {0};
  subscript_row_lvalue.kind = ND_DEREF;
  subscript_row_lvalue.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  subscript_row_lvalue.type_state.subscript_uses_base_address = 1;
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(
      &subscript_row_lvalue));

  node_t typed_row_array = {0};
  typed_row_array.kind = ND_DEREF;
  psx_type_t *typed_row_base = ps_type_new_integer(TK_INT, 4, 0);
  typed_row_array.type = ps_type_new_array(typed_row_base, 4, 16, 0);
  const psx_type_t *typed_row_decay =
      ps_node_row_decay_pointer_arith_type(&typed_row_array);
  ASSERT_TRUE(typed_row_decay != NULL);
  ASSERT_TRUE(typed_row_decay->kind == PSX_TYPE_POINTER);
  ASSERT_TRUE(typed_row_decay->base == typed_row_base);
  ASSERT_TRUE(!ps_type_is_unsigned(typed_row_decay->base));
  ASSERT_EQ(4, ps_type_deref_size(typed_row_decay));
  ASSERT_EQ(4, ps_type_pointer_view_structural_base_deref_size(
                   typed_row_decay));

  node_t typed_nonarray_stale_row = {0};
  typed_nonarray_stale_row.kind = ND_DEREF;
  typed_nonarray_stale_row.type = ps_type_new_integer(TK_INT, 16, 0);
  ASSERT_TRUE(ps_node_row_decay_pointer_arith_type(
                  &typed_nonarray_stale_row) == NULL);

  node_t typed_decay_array = {0};
  typed_decay_array.kind = ND_DEREF;
  typed_decay_array.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 2, 8, 0);
  ASSERT_TRUE(ps_node_deref_decays_to_address(&typed_decay_array));

  node_t typed_ptr_stale_array_decay = {0};
  typed_ptr_stale_array_decay.kind = ND_DEREF;
  typed_ptr_stale_array_decay.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(!ps_node_deref_decays_to_address(
      &typed_ptr_stale_array_decay));

  node_t canonical_decay_array = {0};
  canonical_decay_array.kind = ND_DEREF;
  canonical_decay_array.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 2, 8, 0);
  ASSERT_TRUE(ps_node_deref_decays_to_address(&canonical_decay_array));

  node_t canonical_loaded_pointer = {0};
  canonical_loaded_pointer.kind = ND_DEREF;
  canonical_loaded_pointer.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(!ps_node_deref_decays_to_address(&canonical_loaded_pointer));

  node_t canonical_struct_scalar = {0};
  canonical_struct_scalar.kind = ND_DEREF;
  canonical_struct_scalar.type = ps_type_new_tag(
      TK_STRUCT, "ScalarStruct", 12, 0, 32);
  ASSERT_TRUE(!ps_node_deref_decays_to_address(&canonical_struct_scalar));
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(&canonical_struct_scalar)->kind);

  parsed_code = parse_program_input(
      "int __tm_ptrarr2d(void) { int a[2][3]; int (*p)[3] = a; return p[1][2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *ptrarr2d_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(ptrarr2d_p != NULL);
  ASSERT_TRUE(ptrarr2d_p->decl_type != NULL);
  ASSERT_EQ(12, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    ptrarr2d_p->decl_type));
  ASSERT_EQ(4, ps_type_pointer_view_structural_base_deref_size(
                   ptrarr2d_p->decl_type));
  node_t *ptrarr2d_p_node = psx_node_new_lvar_identifier_ref_for(ptrarr2d_p);
  ASSERT_EQ(12, ps_node_deref_size(ptrarr2d_p_node));
  ASSERT_EQ(12, canonical_node_ptr_array_pointee_bytes(ptrarr2d_p_node));
  int ptrarr2d_inner =
      canonical_node_array_subscript_stride_bytes(ptrarr2d_p_node, 0);
  int ptrarr2d_next =
      canonical_node_array_subscript_stride_bytes(ptrarr2d_p_node, 1);
  ASSERT_EQ(4, ptrarr2d_inner);
  ASSERT_EQ(0, ptrarr2d_next);
  node_t *ptrarr2d_row = ps_node_new_subscript_deref_for(
      ptrarr2d_p_node, ptrarr2d_p_node, ps_node_new_num(0));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr2d_row)->kind);
  ASSERT_EQ(12, ps_node_type_size(ptrarr2d_row));
  ASSERT_EQ(4, ps_node_deref_size(ptrarr2d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(ptrarr2d_row));
  ASSERT_TRUE(ps_node_deref_decays_to_address(ptrarr2d_row));

  parsed_code = parse_program_input(
      "typedef int __tm_Row3[3]; typedef int (*__tm_PA3)[3]; "
      "int __tm_typedef_ptrarr(void) { __tm_Row3 *a; __tm_PA3 b; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *typedef_row_ptr = find_func_lvar(fn, "a");
  lvar_t *typedef_ptrarr = find_func_lvar(fn, "b");
  ASSERT_TRUE(typedef_row_ptr != NULL);
  ASSERT_TRUE(typedef_ptrarr != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, typedef_row_ptr->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, typedef_row_ptr->decl_type->base->kind);
  ASSERT_EQ(3, typedef_row_ptr->decl_type->base->array_len);
  ASSERT_EQ(12, ps_type_deref_size(typedef_row_ptr->decl_type));
  ASSERT_EQ(PSX_TYPE_POINTER, typedef_ptrarr->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, typedef_ptrarr->decl_type->base->kind);
  ASSERT_EQ(3, typedef_ptrarr->decl_type->base->array_len);
  ASSERT_EQ(12, ps_type_deref_size(typedef_ptrarr->decl_type));

  parsed_code = parse_program_input(
      "typedef int *TMDArrayPtr[3]; typedef int (*TMDPtrArray)[3]; "
      "int __tm_toplevel_decl_shape(void) { return 0; }");
  (void)parsed_code;
  psx_typedef_info_t tm_decl_array_ptr = {0};
  psx_typedef_info_t tm_decl_ptr_array = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TMDArrayPtr", 11,
                                        &tm_decl_array_ptr));
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TMDPtrArray", 11,
                                        &tm_decl_ptr_array));
  const psx_type_t *tm_array_ptr_type =
      ps_ctx_typedef_decl_type(&tm_decl_array_ptr);
  const psx_type_t *tm_ptr_array_type =
      ps_ctx_typedef_decl_type(&tm_decl_ptr_array);
  ASSERT_TRUE(tm_array_ptr_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_array_ptr_type->kind);
  ASSERT_EQ(3, tm_array_ptr_type->array_len);
  ASSERT_TRUE(tm_array_ptr_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_array_ptr_type->base->kind);
  ASSERT_TRUE(tm_array_ptr_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tm_array_ptr_type->base->base->kind);
  ASSERT_TRUE(tm_ptr_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_ptr_array_type->kind);
  ASSERT_TRUE(tm_ptr_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_ptr_array_type->base->kind);
  ASSERT_EQ(3, tm_ptr_array_type->base->array_len);
  ASSERT_TRUE(tm_ptr_array_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tm_ptr_array_type->base->base->kind);

  parsed_code = parse_program_input(
      "int __tm_ptrarr_leaf_flags(void) { "
      "  unsigned char uc[1][3]; unsigned char (*up)[3] = uc; "
      "  _Bool bb[1][2]; _Bool (*bp)[2] = bb; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *leaf_up = find_func_lvar(fn, "up");
  ASSERT_TRUE(leaf_up != NULL);
  node_t *leaf_up_node = psx_node_new_lvar_identifier_ref_for(leaf_up);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(leaf_up_node));
  ASSERT_TRUE(!ps_node_is_unsigned_type(leaf_up_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(leaf_up_node));
  ASSERT_EQ(3, ps_node_deref_size(leaf_up_node));
  ASSERT_EQ(1, canonical_node_base_deref_size(leaf_up_node));

  lvar_t *leaf_bp = find_func_lvar(fn, "bp");
  ASSERT_TRUE(leaf_bp != NULL);
  node_t *leaf_bp_node = psx_node_new_lvar_identifier_ref_for(leaf_bp);
  ASSERT_TRUE(canonical_node_pointee_is_bool(leaf_bp_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(leaf_bp_node));
  ASSERT_EQ(2, ps_node_deref_size(leaf_bp_node));
  ASSERT_EQ(1, canonical_node_base_deref_size(leaf_bp_node));

  parsed_code = parse_program_input(
      "int __tm_scalar_unsigned_not_pointee(void) { "
      "unsigned char u = 1; return u; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *leaf_scalar_u = find_func_lvar(fn, "u");
  ASSERT_TRUE(leaf_scalar_u != NULL);
  node_t *leaf_scalar_u_node = psx_node_new_lvar_identifier_ref_for(leaf_scalar_u);
  ASSERT_TRUE(ps_node_is_unsigned_type(leaf_scalar_u_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(leaf_scalar_u_node));

  parsed_code = parse_program_input(
      "int __tm_scalar_bool_not_pointee(void) { "
      "_Bool b = 1; return b; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *leaf_scalar_b = find_func_lvar(fn, "b");
  ASSERT_TRUE(leaf_scalar_b != NULL);
  node_t *leaf_scalar_b_node = psx_node_new_lvar_identifier_ref_for(leaf_scalar_b);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(leaf_scalar_b_node)->kind);
  ASSERT_TRUE(!canonical_node_pointee_is_bool(leaf_scalar_b_node));

  parsed_code = parse_program_input(
      "unsigned char __tm_guc[1][3]; unsigned char (*__tm_gup)[3] = __tm_guc; "
      "_Bool __tm_gb[1][2]; _Bool (*__tm_gbp)[2] = __tm_gb; "
      "int __tm_gptrarr_leaf_flags(void) { return 0; }");
  (void)parsed_code;
  global_var_t *leaf_gup = find_test_global_var("__tm_gup", 8);
  ASSERT_TRUE(leaf_gup != NULL);
  node_t *leaf_gup_node = ps_node_new_gvar_for(leaf_gup);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(leaf_gup_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(leaf_gup_node));

  global_var_t *leaf_gbp = find_test_global_var("__tm_gbp", 8);
  ASSERT_TRUE(leaf_gbp != NULL);
  node_t *leaf_gbp_node = ps_node_new_gvar_for(leaf_gbp);
  ASSERT_TRUE(canonical_node_pointee_is_bool(leaf_gbp_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(leaf_gbp_node));

  parsed_code = parse_program_input(
      "unsigned char *__tm_gucp; _Bool *__tm_gbp_scalar; "
      "int __tm_gptr_leaf_flags(void) { return 0; }");
  (void)parsed_code;
  global_var_t *leaf_gucp = find_test_global_var("__tm_gucp",
                                                sizeof("__tm_gucp") - 1);
  ASSERT_TRUE(leaf_gucp != NULL);
  node_t *leaf_gucp_node = ps_node_new_gvar_for(leaf_gucp);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(leaf_gucp_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(leaf_gucp_node));

  global_var_t *leaf_gbp_scalar = find_test_global_var(
      "__tm_gbp_scalar", sizeof("__tm_gbp_scalar") - 1);
  ASSERT_TRUE(leaf_gbp_scalar != NULL);
  node_t *leaf_gbp_scalar_node = ps_node_new_gvar_for(leaf_gbp_scalar);
  ASSERT_TRUE(canonical_node_pointee_is_bool(leaf_gbp_scalar_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(leaf_gbp_scalar_node));

  parsed_code = parse_program_input(
      "unsigned char __tm_gscalar_u; int __tm_gscalar_use(void) { return 0; }");
  (void)parsed_code;
  global_var_t *leaf_gscalar_u = find_test_global_var(
      "__tm_gscalar_u", sizeof("__tm_gscalar_u") - 1);
  ASSERT_TRUE(leaf_gscalar_u != NULL);
  node_t *leaf_gscalar_u_node = ps_node_new_gvar_for(leaf_gscalar_u);
  ASSERT_TRUE(ps_node_is_unsigned_type(leaf_gscalar_u_node));
  leaf_gscalar_u_node->type = NULL;
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(leaf_gscalar_u_node));

  parsed_code = parse_program_input(
      "_Bool __tm_gscalar_b; int __tm_gscalar_bool_use(void) { return 0; }");
  (void)parsed_code;
  global_var_t *leaf_gscalar_b = find_test_global_var(
      "__tm_gscalar_b", sizeof("__tm_gscalar_b") - 1);
  ASSERT_TRUE(leaf_gscalar_b != NULL);
  node_t *leaf_gscalar_b_node = ps_node_new_gvar_for(leaf_gscalar_b);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(leaf_gscalar_b_node)->kind);
  leaf_gscalar_b_node->type = NULL;
  ASSERT_TRUE(!canonical_node_pointee_is_bool(leaf_gscalar_b_node));

  psx_type_t *stale_bool_pointer_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(stale_bool_pointer_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, stale_bool_pointer_type->kind);
  ASSERT_TRUE(stale_bool_pointer_type->base != NULL);
  ASSERT_TRUE(stale_bool_pointer_type->base->kind != PSX_TYPE_BOOL);

  psx_type_t *legacy_bool_array_type = ps_type_new_array(
      ps_type_new_integer(TK_BOOL, 1, 1), 2, 2, 0);
  ASSERT_TRUE(legacy_bool_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_bool_array_type->kind);
  ASSERT_TRUE(legacy_bool_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, legacy_bool_array_type->base->kind);

  psx_type_t *legacy_unsigned_array_type = ps_type_new_array(
      ps_type_new_integer(TK_UNSIGNED, 1, 1), 3, 3, 0);
  ASSERT_TRUE(legacy_unsigned_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_unsigned_array_type->kind);
  ASSERT_TRUE(legacy_unsigned_array_type->base != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(legacy_unsigned_array_type->base));

  psx_type_t *legacy_bool_2d_row = ps_type_new_array(
      ps_type_new_integer(TK_BOOL, 1, 1), 3, 3, 0);
  psx_type_t *legacy_bool_2d_array_type = ps_type_new_array(
      legacy_bool_2d_row, 2, 6, 0);
  ASSERT_TRUE(legacy_bool_2d_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_bool_2d_array_type->kind);
  ASSERT_TRUE(legacy_bool_2d_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_bool_2d_array_type->base->kind);
  ASSERT_TRUE(legacy_bool_2d_array_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, legacy_bool_2d_array_type->base->base->kind);

  psx_type_t *legacy_unsigned_2d_row = ps_type_new_array(
      ps_type_new_integer(TK_UNSIGNED, 1, 1), 3, 3, 0);
  psx_type_t *legacy_unsigned_2d_array_type = ps_type_new_array(
      legacy_unsigned_2d_row, 2, 6, 0);
  ASSERT_TRUE(legacy_unsigned_2d_array_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_unsigned_2d_array_type->kind);
  ASSERT_TRUE(legacy_unsigned_2d_array_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, legacy_unsigned_2d_array_type->base->kind);
  ASSERT_TRUE(legacy_unsigned_2d_array_type->base->base != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(legacy_unsigned_2d_array_type->base->base));

  parsed_code = parse_program_input(
      "typedef int __tm_M[2][3][4]; "
      "int __tm_ptrarr3d(__tm_M *p) { return (*p)[1][2][3]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *ptrarr3d_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(ptrarr3d_p != NULL);
  ASSERT_TRUE(ptrarr3d_p->decl_type != NULL);
  ASSERT_EQ(96, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    ptrarr3d_p->decl_type));
  ASSERT_TRUE(ptrarr3d_p->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptrarr3d_p->decl_type->base->kind);
  ASSERT_EQ(2, ptrarr3d_p->decl_type->base->array_len);
  ASSERT_TRUE(ptrarr3d_p->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptrarr3d_p->decl_type->base->base->kind);
  ASSERT_EQ(3, ptrarr3d_p->decl_type->base->base->array_len);
  ASSERT_TRUE(ptrarr3d_p->decl_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptrarr3d_p->decl_type->base->base->base->kind);
  ASSERT_EQ(4, ptrarr3d_p->decl_type->base->base->base->array_len);
  ASSERT_TRUE(ptrarr3d_p->decl_type->base->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ptrarr3d_p->decl_type->base->base->base->base->kind);
  node_t *ptrarr3d_p_node = psx_node_new_lvar_identifier_ref_for(ptrarr3d_p);
  ASSERT_EQ(96, ps_node_deref_size(ptrarr3d_p_node));
  int ptrarr3d_inner =
      canonical_node_array_subscript_stride_bytes(ptrarr3d_p_node, 0);
  int ptrarr3d_next =
      canonical_node_array_subscript_stride_bytes(ptrarr3d_p_node, 1);
  ASSERT_EQ(48, ptrarr3d_inner);
  ASSERT_EQ(16, ptrarr3d_next);
  node_t *ptrarr3d_array = ps_node_new_unary_deref_for(ptrarr3d_p_node);
  ASSERT_EQ(96, ps_node_type_size(ptrarr3d_array));
  ASSERT_EQ(48, ps_node_deref_size(ptrarr3d_array));
  ptrarr3d_inner =
      canonical_node_array_subscript_stride_bytes(ptrarr3d_array, 0);
  ptrarr3d_next =
      canonical_node_array_subscript_stride_bytes(ptrarr3d_array, 1);
  ASSERT_EQ(48, ptrarr3d_inner);
  ASSERT_EQ(16, ptrarr3d_next);
  ASSERT_EQ(ptrarr3d_inner,
            ps_type_deref_size(ps_node_get_type(ptrarr3d_array)));

  parsed_code = parse_program_input(
      "struct __tm_ptrarr2d_S { int a; int b; }; "
      "int __tm_ptrarr2d_struct(void) { "
      "struct __tm_ptrarr2d_S a[2][3]; "
      "struct __tm_ptrarr2d_S (*p)[2][3] = &a; "
      "return (*p)[0][0].a; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *ptrarr2d_struct_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(ptrarr2d_struct_a != NULL);
  ASSERT_TRUE(ps_lvar_is_tag_aggregate(ptrarr2d_struct_a));
  ASSERT_TRUE(ps_lvar_is_struct_aggregate(ptrarr2d_struct_a));
  ASSERT_TRUE(!ps_lvar_is_union_aggregate(ptrarr2d_struct_a));
  lvar_t *ptrarr2d_struct_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(ptrarr2d_struct_p != NULL);
  node_t *ptrarr2d_struct_p_node =
      psx_node_new_lvar_identifier_ref_for(ptrarr2d_struct_p);
  node_t *ptrarr2d_struct_array =
      ps_node_new_unary_deref_for(ptrarr2d_struct_p_node);
  ASSERT_TRUE(ps_node_get_type(ptrarr2d_struct_array) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr2d_struct_array)->kind);
  ASSERT_EQ(0, ps_node_type_size(ptrarr2d_struct_array));
  ASSERT_EQ(0, ps_node_deref_size(ptrarr2d_struct_array));
  ASSERT_EQ(48, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    ps_node_get_type(ptrarr2d_struct_array)));
  ASSERT_TRUE(ps_node_deref_decays_to_address(ptrarr2d_struct_array));
  int ptrarr2d_struct_inner =
      canonical_node_array_subscript_stride_bytes(ptrarr2d_struct_array, 0);
  int ptrarr2d_struct_next =
      canonical_node_array_subscript_stride_bytes(ptrarr2d_struct_array, 1);
  ASSERT_EQ(0, ptrarr2d_struct_inner);
  ASSERT_EQ(0, ptrarr2d_struct_next);
  ASSERT_EQ(ptrarr2d_struct_inner,
            ps_type_deref_size(ps_node_get_type(ptrarr2d_struct_array)));
  node_t *ptrarr2d_struct_row = ps_node_new_subscript_deref_for(
      ptrarr2d_struct_array,
      ptrarr2d_struct_array->lhs ? ptrarr2d_struct_array->lhs
                                 : ptrarr2d_struct_array,
      ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(ptrarr2d_struct_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr2d_struct_row)->kind);
  ASSERT_EQ(0, ps_node_type_size(ptrarr2d_struct_row));
  ASSERT_EQ(0, ps_node_deref_size(ptrarr2d_struct_row));
  ASSERT_EQ(24, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    ps_node_get_type(ptrarr2d_struct_row)));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(ptrarr2d_struct_row));
  node_t *ptrarr2d_struct_elem = ps_node_new_subscript_deref_for(
      ptrarr2d_struct_row,
      ptrarr2d_struct_row->lhs ? ptrarr2d_struct_row->lhs
                               : ptrarr2d_struct_row,
      ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(ptrarr2d_struct_elem) != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(ptrarr2d_struct_elem)->kind);
  ASSERT_EQ(0, ps_node_type_size(ptrarr2d_struct_elem));
  ASSERT_EQ(8, ps_ctx_type_sizeof_in(
                   test_semantic_context(),
                   ps_node_get_type(ptrarr2d_struct_elem)));
  ASSERT_EQ(0, ps_node_deref_size(ptrarr2d_struct_elem));
  ASSERT_TRUE(!ps_node_deref_decays_to_address(ptrarr2d_struct_elem));

  parsed_code = parse_program_input(
      "int __tm_array3d(void) { int a[2][2][3]; return a[0][1][0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *array3d_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(array3d_a != NULL);
  ASSERT_TRUE(array3d_a->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, array3d_a->decl_type->kind);
  node_t *array3d_addr = ps_node_new_lvar_array_addr_for(array3d_a);
  ASSERT_TRUE(ps_node_value_is_pointer_like(array3d_addr));
  ASSERT_EQ(24, ps_node_deref_size(array3d_addr));
  int array3d_inner =
      canonical_node_array_subscript_stride_bytes(array3d_addr, 0);
  int array3d_next =
      canonical_node_array_subscript_stride_bytes(array3d_addr, 1);
  ASSERT_EQ(12, array3d_inner);
  ASSERT_EQ(4, array3d_next);
  node_t *array3d_row = ps_node_new_subscript_deref_for(
      array3d_addr, array3d_addr, ps_node_new_num(0));
  ASSERT_EQ(24, ps_node_type_size(array3d_row));
  ASSERT_EQ(12, ps_node_deref_size(array3d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(array3d_row));
  array3d_inner =
      canonical_node_array_subscript_stride_bytes(array3d_row, 0);
  array3d_next =
      canonical_node_array_subscript_stride_bytes(array3d_row, 1);
  ASSERT_EQ(12, array3d_inner);
  ASSERT_EQ(4, array3d_next);
  node_t *array3d_explicit_row = ps_node_new_unary_deref_for(array3d_addr);
  ASSERT_EQ(24, ps_node_type_size(array3d_explicit_row));
  ASSERT_EQ(12, ps_node_deref_size(array3d_explicit_row));
  ASSERT_TRUE(ps_node_deref_decays_to_address(array3d_explicit_row));
  node_t *array3d_explicit_cell = ps_node_new_unary_deref_for(array3d_explicit_row);
  ASSERT_EQ(12, ps_node_type_size(array3d_explicit_cell));
  ASSERT_EQ(4, ps_node_deref_size(array3d_explicit_cell));
  ASSERT_TRUE(ps_node_deref_decays_to_address(array3d_explicit_cell));
  node_t *array3d_explicit_scalar = ps_node_new_unary_deref_for(array3d_explicit_cell);
  ASSERT_EQ(4, ps_node_type_size(array3d_explicit_scalar));
  ASSERT_EQ(0, ps_node_deref_size(array3d_explicit_scalar));
  ASSERT_TRUE(!ps_node_deref_decays_to_address(array3d_explicit_scalar));

  parsed_code = parse_program_input(
      "int __tm_array2d_identifier(void) { int a[2][3]; return a[1][1]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *array2d_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(array2d_a != NULL);
  node_t *array2d_node = psx_node_new_lvar_identifier_ref_for(array2d_a);
  ASSERT_EQ(12, ps_node_deref_size(array2d_node));
  int array2d_inner =
      canonical_node_array_subscript_stride_bytes(array2d_node, 0);
  int array2d_next =
      canonical_node_array_subscript_stride_bytes(array2d_node, 1);
  ASSERT_EQ(12, array2d_inner);
  ASSERT_EQ(4, array2d_next);
  node_t *array2d_row = ps_node_new_subscript_deref_for(
      array2d_node, array2d_node, ps_node_new_num(0));
  ASSERT_EQ(12, ps_node_type_size(array2d_row));
  ASSERT_EQ(4, ps_node_deref_size(array2d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(array2d_row));
  node_t *array2d_cell = ps_node_new_subscript_deref_for(
      array2d_row,
      ps_node_subscript_deref_uses_base_address(array2d_row)
          ? array2d_row->lhs
          : array2d_row,
      ps_node_new_num(0));
  ASSERT_EQ(4, ps_node_type_size(array2d_cell));
  ASSERT_EQ(0, ps_node_deref_size(array2d_cell));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(array2d_cell));

  parsed_code = parse_program_input(
      "typedef int __tm_M2[3][4]; "
      "int __tm_param_ptrarr3d(__tm_M2 *a, int i, int j, int k) { return a[i][j][k]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *param_ptrarr3d_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(param_ptrarr3d_a != NULL);
  node_t *param_ptrarr3d_node = psx_node_new_lvar_identifier_ref_for(param_ptrarr3d_a);
  ASSERT_EQ(48, ps_node_deref_size(param_ptrarr3d_node));
  int param_ptrarr3d_inner =
      canonical_node_array_subscript_stride_bytes(param_ptrarr3d_node, 0);
  int param_ptrarr3d_next =
      canonical_node_array_subscript_stride_bytes(param_ptrarr3d_node, 1);
  ASSERT_EQ(16, param_ptrarr3d_inner);
  ASSERT_EQ(4, param_ptrarr3d_next);
  node_t *param_ptrarr3d_arg_node = ps_node_new_param_lvar_for(
      param_ptrarr3d_a);
  ASSERT_TRUE(ps_node_get_type(param_ptrarr3d_arg_node) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(param_ptrarr3d_arg_node)->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(param_ptrarr3d_arg_node));
  ASSERT_EQ(8, ps_node_type_size(param_ptrarr3d_arg_node));
  ASSERT_EQ(48, ps_node_deref_size(param_ptrarr3d_arg_node));
  ASSERT_TRUE(ps_type_pointer_view_structural_base_deref_size(
                  ps_node_get_type(param_ptrarr3d_arg_node)) > 0);
  ASSERT_EQ(4, ps_type_pointer_view_structural_base_deref_size(
                   ps_node_get_type(param_ptrarr3d_arg_node)));
  ASSERT_EQ(4, canonical_node_base_deref_size(param_ptrarr3d_arg_node));

  parsed_code = parse_program_input(
      "int __tm_param_decl_type(unsigned char u, double d, int i) { return u + i; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *param_decl_u = find_func_lvar(fn, "u");
  lvar_t *param_decl_d = find_func_lvar(fn, "d");
  lvar_t *param_decl_i = find_func_lvar(fn, "i");
  ASSERT_TRUE(param_decl_u != NULL);
  ASSERT_TRUE(param_decl_d != NULL);
  ASSERT_TRUE(param_decl_i != NULL);
  node_t *param_decl_u_node = ps_node_new_param_lvar_for(
      param_decl_u);
  ASSERT_TRUE(ps_node_get_type(param_decl_u_node) == param_decl_u->decl_type);
  ASSERT_EQ(1, ps_node_type_size(param_decl_u_node));
  ASSERT_TRUE(ps_node_is_unsigned_type(param_decl_u_node));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(param_decl_u_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_value_fp_kind(param_decl_u_node));
  ASSERT_TRUE(ps_node_get_type(param_decl_u_node)->kind != PSX_TYPE_COMPLEX);
  node_t *param_decl_d_node = ps_node_new_param_lvar_for(
      param_decl_d);
  ASSERT_TRUE(ps_node_get_type(param_decl_d_node) == param_decl_d->decl_type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(param_decl_d_node));
  ASSERT_EQ(8, ps_node_type_size(param_decl_d_node));
  node_t *param_decl_i_node = ps_node_new_param_lvar_for(
      param_decl_i);
  ASSERT_TRUE(ps_node_get_type(param_decl_i_node) == param_decl_i->decl_type);
  ASSERT_EQ(4, ps_node_type_size(param_decl_i_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_value_fp_kind(param_decl_i_node));
  ASSERT_TRUE(ps_node_get_type(param_decl_i_node)->kind != PSX_TYPE_COMPLEX);

  node_t *param_ptrarr3d_row = ps_node_new_subscript_deref_for(
      param_ptrarr3d_node, param_ptrarr3d_node, ps_node_new_num(0));
  ASSERT_EQ(48, ps_node_type_size(param_ptrarr3d_row));
  ASSERT_EQ(16, ps_node_deref_size(param_ptrarr3d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(param_ptrarr3d_row));
  param_ptrarr3d_inner =
      canonical_node_array_subscript_stride_bytes(param_ptrarr3d_row, 0);
  param_ptrarr3d_next =
      canonical_node_array_subscript_stride_bytes(param_ptrarr3d_row, 1);
  ASSERT_EQ(16, param_ptrarr3d_inner);
  ASSERT_EQ(4, param_ptrarr3d_next);
  node_t *param_ptrarr3d_cell_row = ps_node_new_subscript_deref_for(
      param_ptrarr3d_row,
      ps_node_subscript_deref_uses_base_address(param_ptrarr3d_row)
          ? param_ptrarr3d_row->lhs
          : param_ptrarr3d_row,
      ps_node_new_num(0));
  ASSERT_EQ(16, ps_node_type_size(param_ptrarr3d_cell_row));
  ASSERT_EQ(4, ps_node_deref_size(param_ptrarr3d_cell_row));
  param_ptrarr3d_inner =
      canonical_node_array_subscript_stride_bytes(param_ptrarr3d_cell_row, 0);
  param_ptrarr3d_next =
      canonical_node_array_subscript_stride_bytes(param_ptrarr3d_cell_row, 1);
  ASSERT_EQ(4, param_ptrarr3d_inner);
  ASSERT_EQ(0, param_ptrarr3d_next);

  parsed_code = parse_program_input(
      "int (*__tm_ret_ptrarr3d(void))[3][4]; "
      "int __tm_local_ptrarr3d(void) { "
      "  int (*p)[3][4] = __tm_ret_ptrarr3d(); return p[1][2][0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *local_ptrarr3d_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(local_ptrarr3d_p != NULL);
  ASSERT_TRUE(local_ptrarr3d_p->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_ptrarr3d_p->decl_type->kind);
  ASSERT_EQ(48, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    local_ptrarr3d_p->decl_type));
  node_t *local_ptrarr3d_node = psx_node_new_lvar_identifier_ref_for(local_ptrarr3d_p);
  ASSERT_EQ(48, ps_node_deref_size(local_ptrarr3d_node));
  int local_ptrarr3d_inner =
      canonical_node_array_subscript_stride_bytes(local_ptrarr3d_node, 0);
  int local_ptrarr3d_next =
      canonical_node_array_subscript_stride_bytes(local_ptrarr3d_node, 1);
  ASSERT_EQ(16, local_ptrarr3d_inner);
  ASSERT_EQ(4, local_ptrarr3d_next);
  node_t *local_ptrarr3d_row = ps_node_new_subscript_deref_for(
      local_ptrarr3d_node, local_ptrarr3d_node, ps_node_new_num(0));
  ASSERT_EQ(48, ps_node_type_size(local_ptrarr3d_row));
  ASSERT_EQ(16, ps_node_deref_size(local_ptrarr3d_row));
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(local_ptrarr3d_row));
  local_ptrarr3d_inner =
      canonical_node_array_subscript_stride_bytes(local_ptrarr3d_row, 0);
  local_ptrarr3d_next =
      canonical_node_array_subscript_stride_bytes(local_ptrarr3d_row, 1);
  ASSERT_EQ(16, local_ptrarr3d_inner);
  ASSERT_EQ(4, local_ptrarr3d_next);

  parsed_code = parse_program_input(
      "int __tm_vla3d(int n, int m, int k) { "
      "  int a[n][m][k]; return a[0][0][0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *vla3d_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(vla3d_a != NULL);
  ASSERT_TRUE(ps_lvar_is_vla(vla3d_a));
  ASSERT_TRUE(ps_lvar_vla_row_stride_frame_off(vla3d_a) > 0);
  ASSERT_EQ(1, ps_lvar_vla_strides_remaining(vla3d_a));
  node_t *vla3d_node = psx_node_new_lvar_identifier_ref_for(vla3d_a);
  node_t *vla3d_decay = psx_node_new_vla_decay_ref_for(vla3d_a);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(vla3d_decay)->kind);
  ASSERT_TRUE(ps_node_value_is_pointer_like(vla3d_decay));
  ASSERT_EQ(0, ps_node_aggregate_value_size(vla3d_decay));
  ASSERT_EQ(ps_lvar_vla_row_stride_frame_off(vla3d_a),
            ps_node_vla_row_stride_frame_off(vla3d_decay));
  int vla3d_inner =
      canonical_node_array_subscript_stride_bytes(vla3d_node, 0);
  int vla3d_next =
      canonical_node_array_subscript_stride_bytes(vla3d_node, 1);
  ASSERT_EQ(0, vla3d_inner);
  ASSERT_EQ(0, vla3d_next);
  ASSERT_EQ(4, canonical_node_array_subscript_stride_bytes(vla3d_node, 2));
  node_t *vla3d_row = ps_node_new_subscript_deref_for(
      vla3d_node, vla3d_node, ps_node_new_num(0));
  ASSERT_EQ(ps_lvar_vla_row_stride_frame_off(vla3d_a) + 8,
            ps_node_vla_row_stride_frame_off(vla3d_row));
  ASSERT_TRUE(ps_node_get_type(vla3d_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(vla3d_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(vla3d_row));
  vla3d_inner =
      canonical_node_array_subscript_stride_bytes(vla3d_row, 0);
  vla3d_next =
      canonical_node_array_subscript_stride_bytes(vla3d_row, 1);
  ASSERT_EQ(0, vla3d_inner);
  ASSERT_EQ(4, vla3d_next);
  int vla3d_type_inner = ps_type_array_subscript_stride_bytes(
      ps_node_get_type(vla3d_row), 0);
  int vla3d_type_next = ps_type_array_subscript_stride_bytes(
      ps_node_get_type(vla3d_row), 1);
  ASSERT_EQ(vla3d_inner, vla3d_type_inner);
  ASSERT_EQ(vla3d_next, vla3d_type_next);
  node_t *vla3d_row_base =
      ps_node_subscript_deref_uses_base_address(vla3d_row)
          ? vla3d_row->lhs
          : vla3d_row;
  node_t *vla3d_cell_row = ps_node_new_subscript_deref_for(
      vla3d_row, vla3d_row_base, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(vla3d_cell_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(vla3d_cell_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(vla3d_cell_row));

  parsed_code = parse_program_input(
      "int __tm_vla_param_shape(int m, int k, int t[][m][3][k]) { "
      "  return t[0][0][0][0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *vla_param_m = find_func_lvar(fn, "m");
  lvar_t *vla_param_k = find_func_lvar(fn, "k");
  lvar_t *vla_param_t = find_func_lvar(fn, "t");
  ASSERT_TRUE(vla_param_m != NULL);
  ASSERT_TRUE(vla_param_k != NULL);
  ASSERT_TRUE(vla_param_t != NULL);
  ASSERT_TRUE(vla_param_t->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, vla_param_t->decl_type->kind);
  ASSERT_TRUE(ps_type_contains_vla_array(vla_param_t->decl_type));
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_count(vla_param_t));
  ASSERT_EQ(0, ps_lvar_vla_param_inner_dim_const(vla_param_t, 0));
  ASSERT_EQ(3, ps_lvar_vla_param_inner_dim_const(vla_param_t, 1));
  ASSERT_EQ(0, ps_lvar_vla_param_inner_dim_const(vla_param_t, 2));
  ASSERT_EQ(vla_param_m->offset,
            ps_lvar_vla_param_inner_dim_src_offset(vla_param_t, 0));
  ASSERT_EQ(vla_param_k->offset,
            ps_lvar_vla_param_inner_dim_src_offset(vla_param_t, 2));
  ASSERT_EQ(4, ps_lvar_vla_row_stride_elem_size(vla_param_t));

  int canonical_stride_dims[4] = {2, 3, 4, 5};
  psx_type_t *canonical_stride_array = ps_type_wrap_array_dims(
      ps_type_new_integer(TK_INT, 4, 0), canonical_stride_dims, 4);
  int canonical_outer_stride =
      ps_type_array_subscript_stride_bytes(canonical_stride_array, 0);
  int canonical_mid_stride =
      ps_type_array_subscript_stride_bytes(canonical_stride_array, 1);
  int canonical_inner_stride =
      ps_type_array_subscript_stride_bytes(canonical_stride_array, 2);
  ASSERT_EQ(240, canonical_outer_stride);
  ASSERT_EQ(80, canonical_mid_stride);
  ASSERT_EQ(20, canonical_inner_stride);
  psx_type_t *canonical_stride_pointer = ps_type_new_pointer(
      ps_type_clone(canonical_stride_array->base));
  ASSERT_TRUE(canonical_stride_pointer->base != NULL);
  canonical_outer_stride = ps_type_sizeof(canonical_stride_pointer->base);
  canonical_mid_stride = ps_type_array_subscript_stride_bytes(
      canonical_stride_pointer->base, 0);
  canonical_inner_stride = ps_type_array_subscript_stride_bytes(
      canonical_stride_pointer->base, 1);
  ASSERT_EQ(240, canonical_outer_stride);
  ASSERT_EQ(80, canonical_mid_stride);
  ASSERT_EQ(20, canonical_inner_stride);

  psx_declarator_shape_t array_of_pointer_shape;
  ps_declarator_shape_init(&array_of_pointer_shape);
  ASSERT_TRUE(ps_declarator_shape_append_array(
      &array_of_pointer_shape, 3));
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &array_of_pointer_shape, 0, 0));
  psx_type_t *array_of_pointer_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &array_of_pointer_shape);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_of_pointer_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, array_of_pointer_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, array_of_pointer_type->base->base->kind);

  psx_declarator_shape_t incomplete_array_shape;
  ps_declarator_shape_init(&incomplete_array_shape);
  ASSERT_TRUE(ps_declarator_shape_append_array_ex(
      &incomplete_array_shape, 0, 1));
  ASSERT_EQ(1, incomplete_array_shape.count);
  ASSERT_TRUE(incomplete_array_shape.ops[0].is_incomplete_array);
  ASSERT_TRUE(!incomplete_array_shape.ops[0].is_vla_array);
  ASSERT_EQ(0, incomplete_array_shape.ops[0].array_len);

  psx_declarator_shape_t vla_array_shape;
  ps_declarator_shape_init(&vla_array_shape);
  ASSERT_TRUE(ps_declarator_shape_append_vla_array(&vla_array_shape));
  ASSERT_EQ(1, vla_array_shape.count);
  ASSERT_TRUE(vla_array_shape.ops[0].is_vla_array);
  ASSERT_TRUE(!vla_array_shape.ops[0].is_incomplete_array);
  psx_type_t *vla_array_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &vla_array_shape);
  ASSERT_EQ(PSX_TYPE_ARRAY, vla_array_type->kind);
  ASSERT_TRUE(vla_array_type->is_vla);

  psx_declarator_shape_t pointer_to_array_shape;
  ps_declarator_shape_init(&pointer_to_array_shape);
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &pointer_to_array_shape, 0, 0));
  ASSERT_TRUE(ps_declarator_shape_append_array(
      &pointer_to_array_shape, 3));
  psx_type_t *pointer_to_array_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &pointer_to_array_shape);
  ASSERT_EQ(PSX_TYPE_POINTER, pointer_to_array_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, pointer_to_array_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER, pointer_to_array_type->base->base->kind);

  psx_declarator_shape_t array_of_funcptr_shape;
  ps_declarator_shape_init(&array_of_funcptr_shape);
  ASSERT_TRUE(ps_declarator_shape_append_array(
      &array_of_funcptr_shape, 2));
  ASSERT_TRUE(ps_declarator_shape_append_pointer(
      &array_of_funcptr_shape, 0, 0));
  ASSERT_TRUE(ps_declarator_shape_append_function(
      &array_of_funcptr_shape));
  const psx_type_t *declarator_func_params[] = {
      ps_type_new_integer(TK_INT, 4, 0)};
  psx_set_resolved_function_parameter_types(
      test_arena_context(),
      &array_of_funcptr_shape.ops[array_of_funcptr_shape.count - 1],
      declarator_func_params, 1, 0);
  psx_type_t *array_of_funcptr_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0),
      &array_of_funcptr_shape);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_of_funcptr_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, array_of_funcptr_type->base->kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION, array_of_funcptr_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            array_of_funcptr_type->base->base->base->kind);

  parsed_code = parse_program_input(
      "int __tm_vla_const_inner(int n) { int a[n][4]; return a[0][1]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *vla_const_inner_a = find_func_lvar(fn, "a");
  ASSERT_TRUE(vla_const_inner_a != NULL);
  ASSERT_TRUE(ps_lvar_is_vla(vla_const_inner_a));
  node_t *vla_const_inner_node = psx_node_new_lvar_identifier_ref_for(vla_const_inner_a);
  ASSERT_EQ(16, ps_node_deref_size(vla_const_inner_node));
  ASSERT_EQ(16, ps_node_deref_size(vla_const_inner_node));
  int vla_const_inner_stride =
      canonical_node_array_subscript_stride_bytes(vla_const_inner_node, 0);
  ASSERT_EQ(16, vla_const_inner_stride);
  node_t *vla_const_inner_row = ps_node_new_subscript_deref_for(
      vla_const_inner_node, vla_const_inner_node, ps_node_new_num(0));
  ASSERT_EQ(16, ps_node_type_size(vla_const_inner_row));
  ASSERT_EQ(4, ps_node_deref_size(vla_const_inner_row));
  ASSERT_EQ(4, canonical_node_array_subscript_stride_bytes(
                   vla_const_inner_row, 0));

  parsed_code = parse_program_input(
      "typedef int *__tm_IP; "
      "int __tm_ptrarr_ptr_elem(void) { "
      "  int a, b, c; __tm_IP row[3] = { &a, &b, &c }; "
      "  __tm_IP (*pia)[3] = &row; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *ptrarr_ip_pia = find_func_lvar(fn, "pia");
  ASSERT_TRUE(ptrarr_ip_pia != NULL);
  ASSERT_TRUE(ptrarr_ip_pia->decl_type != NULL);
  ASSERT_EQ(24, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    ptrarr_ip_pia->decl_type));
  ASSERT_EQ(4, ps_type_pointer_view_structural_base_deref_size(
                   ptrarr_ip_pia->decl_type));
  node_t *ptrarr_ip_pia_node = psx_node_new_lvar_identifier_ref_for(ptrarr_ip_pia);
  ASSERT_EQ(24, ps_node_deref_size(ptrarr_ip_pia_node));
  int ptrarr_ip_inner =
      canonical_node_array_subscript_stride_bytes(ptrarr_ip_pia_node, 0);
  int ptrarr_ip_next =
      canonical_node_array_subscript_stride_bytes(ptrarr_ip_pia_node, 1);
  ASSERT_EQ(8, ptrarr_ip_inner);
  ASSERT_EQ(0, ptrarr_ip_next);
  node_t *ptrarr_ip_row = ps_node_new_unary_deref_for(ptrarr_ip_pia_node);
  ASSERT_EQ(24, ps_node_type_size(ptrarr_ip_row));
  ASSERT_EQ(8, ps_node_deref_size(ptrarr_ip_row));
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptrarr_ip_row));
  ASSERT_TRUE(ps_node_get_type(ptrarr_ip_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(ptrarr_ip_row)->kind);
  ASSERT_TRUE(ps_node_get_type(ptrarr_ip_row)->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(ptrarr_ip_row)->base->kind);
  node_t *ptrarr_ip_elem = ps_node_new_subscript_deref_for(
      ptrarr_ip_row, ptrarr_ip_row->lhs ? ptrarr_ip_row->lhs : ptrarr_ip_row,
      ps_node_new_num(0));
  ASSERT_EQ(8, ps_node_type_size(ptrarr_ip_elem));
  ASSERT_EQ(4, ps_node_deref_size(ptrarr_ip_elem));
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptrarr_ip_elem));
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(ptrarr_ip_elem));
  node_t *ptrarr_ip_value = ps_node_new_unary_deref_for(ptrarr_ip_elem);
  ASSERT_EQ(4, ps_node_type_size(ptrarr_ip_value));
  ASSERT_EQ(0, ps_node_deref_size(ptrarr_ip_value));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(ptrarr_ip_value));

  parsed_code = parse_program_input(
      "typedef int *__tm_param_IP; "
      "int __tm_param_ptrarr_ptr_elem(__tm_param_IP (*p)[3]) { "
      "  return *(*p)[0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *param_ptrarr_ip_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(param_ptrarr_ip_p != NULL);
  ASSERT_TRUE(param_ptrarr_ip_p->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, param_ptrarr_ip_p->decl_type->kind);
  ASSERT_TRUE(param_ptrarr_ip_p->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, param_ptrarr_ip_p->decl_type->base->kind);
  ASSERT_TRUE(param_ptrarr_ip_p->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, param_ptrarr_ip_p->decl_type->base->base->kind);
  ASSERT_EQ(24, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    param_ptrarr_ip_p->decl_type));
  ASSERT_EQ(4, ps_type_pointer_view_structural_base_deref_size(
                   param_ptrarr_ip_p->decl_type));
  node_t *param_ptrarr_ip_p_node =
      psx_node_new_lvar_identifier_ref_for(param_ptrarr_ip_p);
  ASSERT_EQ(24, ps_node_deref_size(param_ptrarr_ip_p_node));
  int param_ptrarr_ip_inner =
      canonical_node_array_subscript_stride_bytes(param_ptrarr_ip_p_node, 0);
  int param_ptrarr_ip_next =
      canonical_node_array_subscript_stride_bytes(param_ptrarr_ip_p_node, 1);
  ASSERT_EQ(8, param_ptrarr_ip_inner);
  ASSERT_EQ(0, param_ptrarr_ip_next);
  node_t *param_ptrarr_ip_row =
      ps_node_new_unary_deref_for(param_ptrarr_ip_p_node);
  ASSERT_EQ(24, ps_node_type_size(param_ptrarr_ip_row));
  ASSERT_EQ(8, ps_node_deref_size(param_ptrarr_ip_row));
  ASSERT_TRUE(ps_node_value_is_pointer_like(param_ptrarr_ip_row));
  ASSERT_TRUE(ps_node_get_type(param_ptrarr_ip_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(param_ptrarr_ip_row)->kind);
  ASSERT_TRUE(ps_node_get_type(param_ptrarr_ip_row)->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(param_ptrarr_ip_row)->base->kind);
  node_t *param_ptrarr_ip_elem = ps_node_new_subscript_deref_for(
      param_ptrarr_ip_row,
      param_ptrarr_ip_row->lhs ? param_ptrarr_ip_row->lhs : param_ptrarr_ip_row,
      ps_node_new_num(0));
  ASSERT_EQ(8, ps_node_type_size(param_ptrarr_ip_elem));
  ASSERT_EQ(4, ps_node_deref_size(param_ptrarr_ip_elem));
  ASSERT_TRUE(ps_node_value_is_pointer_like(param_ptrarr_ip_elem));
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(param_ptrarr_ip_elem));
  node_t *param_ptrarr_ip_value =
      ps_node_new_unary_deref_for(param_ptrarr_ip_elem);
  ASSERT_EQ(4, ps_node_type_size(param_ptrarr_ip_value));
  ASSERT_EQ(0, ps_node_deref_size(param_ptrarr_ip_value));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(param_ptrarr_ip_value));

  parsed_code = parse_program_input(
      "typedef int *__tm_param_IP2; "
      "int __tm_param_ptrarr_ptr_elem_2d(__tm_param_IP2 (*p)[2][3]) { "
      "  return *(*p)[1][0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *param_ptrarr_ip2_p = find_func_lvar(fn, "p");
  ASSERT_TRUE(param_ptrarr_ip2_p != NULL);
  ASSERT_TRUE(param_ptrarr_ip2_p->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, param_ptrarr_ip2_p->decl_type->kind);
  ASSERT_TRUE(param_ptrarr_ip2_p->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, param_ptrarr_ip2_p->decl_type->base->kind);
  ASSERT_EQ(2, param_ptrarr_ip2_p->decl_type->base->array_len);
  ASSERT_EQ(24, ps_type_deref_size(param_ptrarr_ip2_p->decl_type->base));
  ASSERT_TRUE(param_ptrarr_ip2_p->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, param_ptrarr_ip2_p->decl_type->base->base->kind);
  ASSERT_EQ(3, param_ptrarr_ip2_p->decl_type->base->base->array_len);
  ASSERT_EQ(
      8, ps_type_deref_size(param_ptrarr_ip2_p->decl_type->base->base));
  ASSERT_TRUE(param_ptrarr_ip2_p->decl_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            param_ptrarr_ip2_p->decl_type->base->base->base->kind);
  ASSERT_EQ(48, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    param_ptrarr_ip2_p->decl_type));
  ASSERT_EQ(4, ps_type_pointer_view_structural_base_deref_size(
                   param_ptrarr_ip2_p->decl_type));
  node_t *param_ptrarr_ip2_p_node =
      psx_node_new_lvar_identifier_ref_for(param_ptrarr_ip2_p);
  ASSERT_EQ(48, ps_node_deref_size(param_ptrarr_ip2_p_node));
  int param_ptrarr_ip2_inner =
      canonical_node_array_subscript_stride_bytes(param_ptrarr_ip2_p_node, 0);
  int param_ptrarr_ip2_next =
      canonical_node_array_subscript_stride_bytes(param_ptrarr_ip2_p_node, 1);
  ASSERT_EQ(24, param_ptrarr_ip2_inner);
  ASSERT_EQ(8, param_ptrarr_ip2_next);
  node_t *param_ptrarr_ip2_row =
      ps_node_new_unary_deref_for(param_ptrarr_ip2_p_node);
  ASSERT_EQ(48, ps_node_type_size(param_ptrarr_ip2_row));
  ASSERT_EQ(24, ps_node_deref_size(param_ptrarr_ip2_row));
  ASSERT_TRUE(ps_node_get_type(param_ptrarr_ip2_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(param_ptrarr_ip2_row)->kind);
  node_t *param_ptrarr_ip2_row1 = ps_node_new_subscript_deref_for(
      param_ptrarr_ip2_row,
      param_ptrarr_ip2_row->lhs ? param_ptrarr_ip2_row->lhs
                                : param_ptrarr_ip2_row,
      ps_node_new_num(1));
  ASSERT_EQ(24, ps_node_type_size(param_ptrarr_ip2_row1));
  ASSERT_EQ(8, ps_node_deref_size(param_ptrarr_ip2_row1));
  ASSERT_TRUE(ps_node_get_type(param_ptrarr_ip2_row1) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(param_ptrarr_ip2_row1)->kind);
  param_ptrarr_ip2_inner =
      canonical_node_array_subscript_stride_bytes(param_ptrarr_ip2_row1, 0);
  param_ptrarr_ip2_next =
      canonical_node_array_subscript_stride_bytes(param_ptrarr_ip2_row1, 1);
  ASSERT_EQ(8, param_ptrarr_ip2_inner);
  ASSERT_EQ(0, param_ptrarr_ip2_next);
  node_t *param_ptrarr_ip2_elem = ps_node_new_subscript_deref_for(
      param_ptrarr_ip2_row1,
      ps_node_subscript_deref_uses_base_address(param_ptrarr_ip2_row1)
          ? param_ptrarr_ip2_row1->lhs
          : param_ptrarr_ip2_row1,
      ps_node_new_num(0));
  ASSERT_EQ(8, ps_node_type_size(param_ptrarr_ip2_elem));
  ASSERT_EQ(4, ps_node_deref_size(param_ptrarr_ip2_elem));
  ASSERT_TRUE(ps_node_value_is_pointer_like(param_ptrarr_ip2_elem));
  node_t *param_ptrarr_ip2_value =
      ps_node_new_unary_deref_for(param_ptrarr_ip2_elem);
  ASSERT_EQ(4, ps_node_type_size(param_ptrarr_ip2_value));
  ASSERT_EQ(0, ps_node_deref_size(param_ptrarr_ip2_value));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(param_ptrarr_ip2_value));

  parsed_code = parse_program_input(
      "int __tm_nested_ptrarr(int (*(*rows)[2])[3]) { "
      "  return (*rows)[0][1][2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *nested2_rows = find_func_lvar(fn, "rows");
  ASSERT_TRUE(nested2_rows != NULL);
  node_t *nested2_rows_node = psx_node_new_lvar_identifier_ref_for(nested2_rows);
  node_t *nested2_rows_array = ps_node_new_unary_deref_for(nested2_rows_node);
  ASSERT_TRUE(ps_node_get_type(nested2_rows_array) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(nested2_rows_array)->kind);
  int nested2_inner =
      canonical_node_array_subscript_stride_bytes(nested2_rows_array, 0);
  int nested2_next =
      canonical_node_array_subscript_stride_bytes(nested2_rows_array, 1);
  ASSERT_EQ(8, nested2_inner);
  ASSERT_EQ(0, nested2_next);
  node_t *nested2_rowptr = ps_node_new_subscript_deref_for(
      nested2_rows_array,
      ps_node_subscript_deref_uses_base_address(nested2_rows_array)
          ? nested2_rows_array->lhs
          : nested2_rows_array,
      ps_node_new_num(0));
  ASSERT_TRUE(ps_node_value_is_pointer_like(nested2_rowptr));
  ASSERT_TRUE(ps_node_get_type(nested2_rowptr) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(nested2_rowptr)->kind);
  ASSERT_TRUE(ps_node_get_type(nested2_rowptr)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(nested2_rowptr)->base->kind);
  ASSERT_TRUE(ps_node_get_type(nested2_rowptr)->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(nested2_rowptr)->base->base->kind);
  nested2_inner =
      canonical_node_array_subscript_stride_bytes(nested2_rowptr, 0);
  nested2_next =
      canonical_node_array_subscript_stride_bytes(nested2_rowptr, 1);
  ASSERT_EQ(4, nested2_inner);
  node_t *nested2_int_row = ps_node_new_subscript_deref_for(
      nested2_rowptr, nested2_rowptr, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(nested2_int_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(nested2_int_row)->kind);
  ASSERT_TRUE(ps_node_get_type(nested2_int_row)->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(nested2_int_row)->base->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(nested2_int_row));
  node_t *nested2_int_cell = ps_node_new_subscript_deref_for(
      nested2_int_row,
      ps_node_subscript_deref_uses_base_address(nested2_int_row)
          ? nested2_int_row->lhs
          : nested2_int_row,
      ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(nested2_int_cell) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(nested2_int_cell)->kind);
  ASSERT_EQ(4, ps_node_type_size(nested2_int_cell));
  ASSERT_EQ(0, ps_node_deref_size(nested2_int_cell));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(nested2_int_cell));

  parsed_code = parse_program_input(
      "int __tm_local_nested_ptrarr(void) { "
      "  int x[2][3]; int (*(*ptrs)[2])[3] = &(int (*[2])[3]){x, x}; "
      "  return (*ptrs)[0][1][2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *local_nested_ptrs = find_func_lvar(fn, "ptrs");
  ASSERT_TRUE(local_nested_ptrs != NULL);
  node_t *local_nested_ptrs_node =
      psx_node_new_lvar_identifier_ref_for(local_nested_ptrs);
  node_t *local_nested_rows_array =
      ps_node_new_unary_deref_for(local_nested_ptrs_node);
  ASSERT_TRUE(ps_node_get_type(local_nested_rows_array) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(local_nested_rows_array)->kind);
  int local_nested_inner =
      canonical_node_array_subscript_stride_bytes(local_nested_rows_array, 0);
  int local_nested_next =
      canonical_node_array_subscript_stride_bytes(local_nested_rows_array, 1);
  ASSERT_EQ(8, local_nested_inner);
  ASSERT_EQ(0, local_nested_next);

  parsed_code = parse_program_input(
      "typedef int (*__tm_RowPtr3_local)[3]; "
      "int __tm_local_rowptr_typedef_subscript(void) { "
      "  int x[2][3]; __tm_RowPtr3_local *typedef_ptrs = "
      "      (__tm_RowPtr3_local[]){x, x}; "
      "  return typedef_ptrs[0][0][2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *local_typedef_ptrs = find_func_lvar(fn, "typedef_ptrs");
  ASSERT_TRUE(local_typedef_ptrs != NULL);
  ASSERT_TRUE(local_typedef_ptrs->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_typedef_ptrs->decl_type->kind);
  ASSERT_TRUE(local_typedef_ptrs->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, local_typedef_ptrs->decl_type->base->kind);
  ASSERT_TRUE(local_typedef_ptrs->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, local_typedef_ptrs->decl_type->base->base->kind);
  node_t *local_typedef_ptrs_node =
      psx_node_new_lvar_identifier_ref_for(local_typedef_ptrs);
  node_t *local_typedef_rowptr = ps_node_new_subscript_deref_for(
      local_typedef_ptrs_node, local_typedef_ptrs_node, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(local_typedef_rowptr) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(local_typedef_rowptr)->kind);
  ASSERT_TRUE(ps_node_get_type(local_typedef_rowptr)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(local_typedef_rowptr)->base->kind);
  int local_typedef_row_inner =
      canonical_node_array_subscript_stride_bytes(local_typedef_rowptr, 0);
  int local_typedef_row_next =
      canonical_node_array_subscript_stride_bytes(local_typedef_rowptr, 1);
  ASSERT_EQ(4, local_typedef_row_inner);
  ASSERT_EQ(0, local_typedef_row_next);
  node_t *local_typedef_int_row = ps_node_new_subscript_deref_for(
      local_typedef_rowptr, local_typedef_rowptr, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(local_typedef_int_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(local_typedef_int_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(local_typedef_int_row));
  node_t *local_typedef_cell = ps_node_new_subscript_deref_for(
      local_typedef_int_row,
      ps_node_subscript_deref_uses_base_address(local_typedef_int_row)
          ? local_typedef_int_row->lhs
          : local_typedef_int_row,
      ps_node_new_num(2));
  ASSERT_TRUE(ps_node_get_type(local_typedef_cell) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(local_typedef_cell)->kind);
  ASSERT_EQ(4, ps_node_type_size(local_typedef_cell));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(local_typedef_cell));

  parsed_code = parse_program_input(
      "int __tm_flat_rowptr_param(int (*rows[2])[3]) { "
      "  return rows[0][0][2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *flat_rows_param = find_func_lvar(fn, "rows");
  ASSERT_TRUE(flat_rows_param != NULL);
  ASSERT_TRUE(flat_rows_param->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, flat_rows_param->decl_type->kind);
  ASSERT_TRUE(flat_rows_param->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, flat_rows_param->decl_type->base->kind);
  ASSERT_TRUE(flat_rows_param->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, flat_rows_param->decl_type->base->base->kind);
  node_t *flat_rows_param_node =
      psx_node_new_lvar_identifier_ref_for(flat_rows_param);
  node_t *flat_rows_rowptr = ps_node_new_subscript_deref_for(
      flat_rows_param_node, flat_rows_param_node, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(flat_rows_rowptr) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(flat_rows_rowptr)->kind);
  ASSERT_TRUE(ps_node_get_type(flat_rows_rowptr)->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(flat_rows_rowptr)->base->kind);
  int flat_rows_inner =
      canonical_node_array_subscript_stride_bytes(flat_rows_rowptr, 0);
  int flat_rows_next =
      canonical_node_array_subscript_stride_bytes(flat_rows_rowptr, 1);
  ASSERT_EQ(4, flat_rows_inner);
  ASSERT_EQ(0, flat_rows_next);
  node_t *flat_rows_int_row = ps_node_new_subscript_deref_for(
      flat_rows_rowptr, flat_rows_rowptr, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(flat_rows_int_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(flat_rows_int_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(flat_rows_int_row));

  node_t *vla_alloc = ps_node_new_vla_alloc(64, 80, ps_node_new_num(3), ps_node_new_num(12));
  int vla_desc_off = 0;
  int vla_row_off = 0;
  ASSERT_TRUE(ps_node_vla_alloc_descriptor_info(vla_alloc, &vla_desc_off, &vla_row_off));
  ASSERT_EQ(64, vla_desc_off);
  ASSERT_EQ(80, vla_row_off);
  ASSERT_TRUE(!ps_node_vla_alloc_descriptor_info(&canonical_struct_scalar,
                                                  &vla_desc_off, &vla_row_off));
  ASSERT_EQ(0, vla_desc_off);
  ASSERT_EQ(0, vla_row_off);

  node_t typed_funcptr_sig = {0};
  typed_funcptr_sig.kind = ND_LVAR;
  typed_funcptr_sig.type = test_function_pointer(
      ps_type_new_integer(TK_LONG, 8, 0), NULL, 0, 0);
  assert_canonical_type_signature(
      ps_node_get_type(&typed_funcptr_sig), "p<l64()>");

  node_t typed_data_ptr_stale_funcptr_sig = {0};
  typed_data_ptr_stale_funcptr_sig.kind = ND_LVAR;
  typed_data_ptr_stale_funcptr_sig.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_type_derived_function(
      ps_node_get_type(&typed_data_ptr_stale_funcptr_sig)) == NULL);

  node_t canonical_funcptr = {0};
  canonical_funcptr.kind = ND_DEREF;
  canonical_funcptr.type = test_function_pointer(
      ps_type_new_integer(TK_INT, 4, 0), NULL, 0, 0);
  assert_canonical_type_signature(
      ps_node_get_type(&canonical_funcptr), "p<i32()>");

  node_t compound_lit_object = {0};
  compound_lit_object.kind = ND_LVAR;
  compound_lit_object.type = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0);
  node_t compound_lit_addr = {0};
  compound_lit_addr.kind = ND_ADDR;
  compound_lit_addr.lhs = &compound_lit_object;
  ASSERT_EQ(12, ps_node_compound_literal_array_size(&compound_lit_addr));
  node_t compound_lit_comma = {0};
  compound_lit_comma.kind = ND_COMMA;
  compound_lit_comma.rhs = &compound_lit_addr;
  ASSERT_EQ(12, ps_node_compound_literal_array_size(&compound_lit_comma));
  node_t compound_lit_nonaddr = {0};
  compound_lit_nonaddr.kind = ND_DEREF;
  compound_lit_nonaddr.type = compound_lit_object.type;
  ASSERT_EQ(0, ps_node_compound_literal_array_size(&compound_lit_nonaddr));
  node_t typed_noncompound_addr = {0};
  typed_noncompound_addr.kind = ND_ADDR;
  typed_noncompound_addr.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(0, ps_node_compound_literal_array_size(
      &typed_noncompound_addr));

  node_t bitfield_deref = {0};
  bitfield_deref.kind = ND_DEREF;
  bitfield_deref.type_state.bit_width = 3;
  bitfield_deref.type_state.bit_offset = 5;
  bitfield_deref.type_state.bit_is_signed = 1;
  ASSERT_EQ(3, ps_node_bitfield_width(&bitfield_deref));
  int bf_width = 0;
  int bf_offset = 0;
  int bf_is_signed = 0;
  ASSERT_TRUE(ps_node_bitfield_info(&bitfield_deref,
                                     &bf_width, &bf_offset, &bf_is_signed));
  ASSERT_EQ(3, bf_width);
  ASSERT_EQ(5, bf_offset);
  ASSERT_EQ(1, bf_is_signed);
  node_t typed_nonbitfield = {0};
  typed_nonbitfield.kind = ND_DEREF;
  typed_nonbitfield.type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_EQ(0, ps_node_bitfield_width(&typed_nonbitfield));
  ASSERT_TRUE(!ps_node_bitfield_info(&typed_nonbitfield,
                                      NULL, NULL, NULL));
  node_t non_mem_num = {0};
  non_mem_num.kind = ND_NUM;
  ASSERT_EQ(0, ps_node_bitfield_width(&non_mem_num));
  ASSERT_TRUE(!ps_node_bitfield_info(&non_mem_num, NULL, NULL, NULL));

  node_t typed_nonptr_stale_pointer_like = {0};
  typed_nonptr_stale_pointer_like.kind = ND_DEREF;
  typed_nonptr_stale_pointer_like.type = ps_type_new_integer(TK_INT, 8, 0);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(
      &typed_nonptr_stale_pointer_like));
  ASSERT_EQ(4, ps_node_storage_type_size(&typed_nonptr_stale_pointer_like));

  node_t typed_ptr_no_mem_pointer_like = {0};
  typed_ptr_no_mem_pointer_like.kind = ND_DEREF;
  typed_ptr_no_mem_pointer_like.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_node_value_is_pointer_like(&typed_ptr_no_mem_pointer_like));

  parsed_code = parse_program_input(
      "int __tm_ptr_array_unary_deref(void) { "
      "  int w = 1; int *p1[2] = { &w, &w }; return *p1[0]; }");
  fn = as_function_definition(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *ptrarr_ret = body->body[2];
  ASSERT_EQ(ND_RETURN, ptrarr_ret->kind);
  ASSERT_EQ(4, ps_node_type_size(ptrarr_ret->lhs));
  ASSERT_EQ(4, ps_node_storage_type_size(ptrarr_ret->lhs));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(ptrarr_ret->lhs));
  ASSERT_EQ(0, canonical_node_pointer_qual_levels(ptrarr_ret->lhs));

  node_t typed_deref_stale_tag_mem = {0};
  typed_deref_stale_tag_mem.kind = ND_DEREF;
  psx_type_t *typed_deref_stale_tag_inner =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  typed_deref_stale_tag_mem.type =
      ps_type_new_pointer(typed_deref_stale_tag_inner);
  node_t *typed_deref_unary =
      ps_node_new_unary_deref_for(&typed_deref_stale_tag_mem);
  ASSERT_EQ(4, ps_node_deref_size(typed_deref_unary));

  node_t typed_tag_array_ptr = {0};
  typed_tag_array_ptr.kind = ND_LVAR;
  psx_type_t *typed_array_tag =
      ps_type_new_tag(TK_STRUCT, "TMFlatTag", 9, 0, 4);
  psx_type_t *typed_tag_array =
      ps_type_new_array(typed_array_tag, 4, 16, 0);
  typed_tag_array_ptr.type = ps_type_new_pointer(typed_tag_array);
  node_t *typed_tag_array_deref =
      ps_node_new_unary_deref_for(&typed_tag_array_ptr);
  const psx_type_t *typed_tag_array_deref_type =
      ps_node_get_type(typed_tag_array_deref);
  ASSERT_TRUE(typed_tag_array_deref_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, typed_tag_array_deref_type->kind);
  ASSERT_EQ(0, ps_type_sizeof(typed_tag_array_deref_type));
  ASSERT_EQ(0, ps_type_deref_size(typed_tag_array_deref_type));

  node_t typed_missing_ptr_mem = {0};
  typed_missing_ptr_mem.kind = ND_LVAR;
  typed_missing_ptr_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(4, canonical_node_base_deref_size(&typed_missing_ptr_mem));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(&typed_missing_ptr_mem));
  ASSERT_EQ(0, ps_node_vla_row_stride_frame_off(&typed_missing_ptr_mem));

  node_t typed_unsigned_ptr_mem = {0};
  typed_unsigned_ptr_mem.kind = ND_LVAR;
  typed_unsigned_ptr_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 1));
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(&typed_unsigned_ptr_mem));
  int atomic_width = 0;
  int atomic_is_unsigned = 0;
  ASSERT_TRUE(test_node_atomic_pointer_info(
      &typed_unsigned_ptr_mem, ps_ctx_target_info(test_semantic_context()),
      &atomic_width, &atomic_is_unsigned));
  ASSERT_EQ(4, atomic_width);
  ASSERT_EQ(1, atomic_is_unsigned);

  node_t typed_unsigned_ptrptr_mem = {0};
  typed_unsigned_ptrptr_mem.kind = ND_DEREF;
  psx_type_t *typed_unsigned_char =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  typed_unsigned_ptrptr_mem.type = ps_type_new_pointer(
      ps_type_new_pointer(typed_unsigned_char));
  node_t *typed_unsigned_ptrptr_elem = ps_node_new_subscript_deref_for(
      &typed_unsigned_ptrptr_mem,
      &typed_unsigned_ptrptr_mem, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(typed_unsigned_ptrptr_elem) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_node_get_type(typed_unsigned_ptrptr_elem)->kind);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(typed_unsigned_ptrptr_elem));
  node_t *typed_unsigned_ptrptr_cell = ps_node_new_subscript_deref_for(
      typed_unsigned_ptrptr_elem, typed_unsigned_ptrptr_elem,
      ps_node_new_num(1));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(typed_unsigned_ptrptr_cell));

  parsed_code = parse_program_input(
      "int __tm_unsigned_ptrptr(void) { "
      "  unsigned char a[2]; unsigned char *lp = a; "
      "  unsigned char **pp = &lp; return pp[0][1]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *unsigned_pp = find_func_lvar(fn, "pp");
  ASSERT_TRUE(unsigned_pp != NULL);
  ASSERT_TRUE(unsigned_pp->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, unsigned_pp->decl_type->kind);
  ASSERT_TRUE(unsigned_pp->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, unsigned_pp->decl_type->base->kind);
  ASSERT_TRUE(unsigned_pp->decl_type->base->base != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(unsigned_pp->decl_type->base->base));
  node_t *unsigned_pp_node = psx_node_new_lvar_identifier_ref_for(unsigned_pp);
  node_t *unsigned_pp_elem = ps_node_new_subscript_deref_for(
      unsigned_pp_node, unsigned_pp_node, ps_node_new_num(0));
  ASSERT_TRUE(ps_node_get_type(unsigned_pp_elem) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(unsigned_pp_elem)->kind);
  ASSERT_TRUE(ps_node_get_type(unsigned_pp_elem)->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(unsigned_pp_elem)->base->kind);
  ASSERT_TRUE(!ps_node_scalar_ptr_member_lvalue(unsigned_pp_elem));
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(unsigned_pp_elem));
  node_t *unsigned_pp_cell = ps_node_new_subscript_deref_for(
      unsigned_pp_elem, unsigned_pp_elem, ps_node_new_num(1));
  ASSERT_TRUE(ps_node_get_type(unsigned_pp_cell) != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(unsigned_pp_cell)->kind);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(unsigned_pp_cell));
  ASSERT_TRUE(ps_node_integer_value_is_unsigned(unsigned_pp_cell));

  node_t canonical_atomic_ptr = {0};
  canonical_atomic_ptr.kind = ND_DEREF;
  canonical_atomic_ptr.type = ps_type_new_pointer(
      ps_type_new_integer(TK_UNSIGNED, 4, 1));
  ASSERT_TRUE(test_node_atomic_pointer_info(
      &canonical_atomic_ptr, ps_ctx_target_info(test_semantic_context()),
      &atomic_width, &atomic_is_unsigned));
  ASSERT_EQ(4, atomic_width);
  ASSERT_EQ(1, atomic_is_unsigned);

  node_t unsigned_atomic_target_mem = {0};
  unsigned_atomic_target_mem.kind = ND_LVAR;
  unsigned_atomic_target_mem.type =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  node_t *unsigned_atomic_addr =
      ps_node_new_unary_addr_for(&unsigned_atomic_target_mem);
  ASSERT_TRUE(test_node_atomic_pointer_info(
      unsigned_atomic_addr, ps_ctx_target_info(test_semantic_context()),
      &atomic_width, &atomic_is_unsigned));
  ASSERT_EQ(4, atomic_width);
  ASSERT_EQ(1, atomic_is_unsigned);

  node_t typed_bool_ptr_mem = {0};
  typed_bool_ptr_mem.kind = ND_LVAR;
  psx_type_t *typed_bool_type = ps_type_new(PSX_TYPE_BOOL);
  typed_bool_ptr_mem.type = ps_type_new_pointer(typed_bool_type);
  ASSERT_TRUE(canonical_node_pointee_is_bool(&typed_bool_ptr_mem));

  node_t typed_void_ptr_mem = {0};
  typed_void_ptr_mem.kind = ND_LVAR;
  typed_void_ptr_mem.type = ps_type_new_pointer(ps_type_new(PSX_TYPE_VOID));
  ASSERT_TRUE(canonical_node_pointee_is_void(&typed_void_ptr_mem));

  node_t typed_void_ptr_ptr_mem = {0};
  typed_void_ptr_ptr_mem.kind = ND_LVAR;
  typed_void_ptr_ptr_mem.type = ps_type_new_pointer(
      ps_type_new_pointer(ps_type_new(PSX_TYPE_VOID)));
  ASSERT_TRUE(!canonical_node_pointee_is_void(&typed_void_ptr_ptr_mem));
  expect_parse_ok(
      "int void_pp(void) { void *value = 0; void **out = &value; "
      "*out = (void *)0; return 0; }");

  node_t typed_const_view_mem = {0};
  typed_const_view_mem.kind = ND_LVAR;
  typed_const_view_mem.type = ps_type_new_tag(TK_STRUCT, "TypedView", 9, 1, 4);
  ASSERT_EQ(0, ps_node_aggregate_value_size(&typed_const_view_mem));
  ASSERT_EQ(0, canonical_node_pointer_qual_levels(&typed_const_view_mem));
  ASSERT_EQ(0, canonical_node_base_deref_size(&typed_const_view_mem));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(&typed_const_view_mem));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, canonical_node_pointee_fp_kind(&typed_const_view_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_const_qualified(&typed_const_view_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_volatile_qualified(&typed_const_view_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(&typed_const_view_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(&typed_const_view_mem));
  ASSERT_TRUE(!canonical_node_pointee_is_void(&typed_const_view_mem));

  node_t typed_stale_self_const_mem = {0};
  typed_stale_self_const_mem.kind = ND_LVAR;
  typed_stale_self_const_mem.type = ps_type_new_integer(TK_INT, 4, 0);
  expect_const_assign_ok_for_node(&typed_stale_self_const_mem);

  node_t typed_scalar_canonical_const_mem = {0};
  typed_scalar_canonical_const_mem.kind = ND_LVAR;
  psx_type_t *typed_scalar_canonical_const_type =
      ps_type_new_integer(TK_INT, 4, 0);
  ps_type_add_qualifiers(typed_scalar_canonical_const_type, PSX_TYPE_QUALIFIER_CONST);
  typed_scalar_canonical_const_mem.type =
      typed_scalar_canonical_const_type;
  expect_const_assign_fail_for_node(
      &typed_scalar_canonical_const_mem);

  node_t typed_ptr_canonical_self_const_mem = {0};
  typed_ptr_canonical_self_const_mem.kind = ND_LVAR;
  psx_type_t *typed_ptr_canonical_self_const_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ps_type_add_qualifiers(typed_ptr_canonical_self_const_type, PSX_TYPE_QUALIFIER_CONST);
  typed_ptr_canonical_self_const_mem.type =
      typed_ptr_canonical_self_const_type;
  expect_const_assign_fail_for_node(
      &typed_ptr_canonical_self_const_mem);

  node_t typed_nonconst_ptr_lhs_mem = {0};
  typed_nonconst_ptr_lhs_mem.kind = ND_LVAR;
  typed_nonconst_ptr_lhs_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_t typed_const_ptr_rhs_mem = {0};
  typed_const_ptr_rhs_mem.kind = ND_LVAR;
  psx_type_t *typed_const_rhs_base = ps_type_new_integer(TK_INT, 4, 0);
  ps_type_add_qualifiers(typed_const_rhs_base, PSX_TYPE_QUALIFIER_CONST);
  typed_const_ptr_rhs_mem.type = ps_type_new_pointer(typed_const_rhs_base);
  expect_const_qual_discard_fail_for_nodes(&typed_nonconst_ptr_lhs_mem,
                                           &typed_const_ptr_rhs_mem);

  node_t typed_funcptr_view_mem = {0};
  typed_funcptr_view_mem.kind = ND_LVAR;
  psx_type_t *typed_funcptr_view_param =
      ps_type_new_float(TK_FLOAT_KIND_FLOAT, 4);
  const psx_type_t *typed_funcptr_view_params[] = {
      typed_funcptr_view_param};
  typed_funcptr_view_mem.type = test_function_pointer(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
      typed_funcptr_view_params, 1, 0);
  const psx_type_t *typed_funcptr_view_function =
      ps_type_derived_function(ps_node_get_type(&typed_funcptr_view_mem));
  ASSERT_TRUE(typed_funcptr_view_function != NULL);
  ASSERT_EQ(1, typed_funcptr_view_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            typed_funcptr_view_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            typed_funcptr_view_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, typed_funcptr_view_function->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            typed_funcptr_view_function->base->fp_kind);

  node_t typed_funcptr_array_mem = {0};
  typed_funcptr_array_mem.kind = ND_DEREF;
  psx_type_t *typed_funcptr_array_param =
      ps_type_new_float(TK_FLOAT_KIND_FLOAT, 4);
  const psx_type_t *typed_funcptr_array_params[] = {
      typed_funcptr_array_param};
  psx_type_t *typed_funcptr_array_elem = test_function_pointer(
      ps_type_new_integer(TK_INT, 4, 0),
      typed_funcptr_array_params, 1, 0);
  typed_funcptr_array_mem.type =
      ps_type_new_array(typed_funcptr_array_elem, 2, 16, 0);
  node_t *typed_funcptr_array_elem_node = ps_node_new_subscript_deref_for(
      &typed_funcptr_array_mem, &typed_funcptr_array_mem,
      ps_node_new_num(0));
  const psx_type_t *typed_funcptr_array_result_type =
      ps_node_get_type(typed_funcptr_array_elem_node);
  ASSERT_TRUE(typed_funcptr_array_result_type != NULL);
  const psx_type_t *typed_funcptr_array_function =
      ps_type_derived_function(typed_funcptr_array_result_type);
  ASSERT_TRUE(typed_funcptr_array_function != NULL);
  ASSERT_EQ(1, typed_funcptr_array_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            typed_funcptr_array_function->param_types[0]->kind);

  node_t typed_funcptr_callee_mem = {0};
  typed_funcptr_callee_mem.kind = ND_LVAR;
  typed_funcptr_callee_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_function_call_t typed_indirect_call = {0};
  typed_indirect_call.base.kind = ND_FUNCALL;
  typed_indirect_call.callee = &typed_funcptr_callee_mem;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_indirect_call) == NULL);

  node_t typed_stale_funcptr_callee_mem = {0};
  typed_stale_funcptr_callee_mem.kind = ND_LVAR;
  typed_stale_funcptr_callee_mem.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_function_call_t typed_stale_indirect_call = {0};
  typed_stale_indirect_call.base.kind = ND_FUNCALL;
  typed_stale_indirect_call.callee = &typed_stale_funcptr_callee_mem;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_stale_indirect_call) == NULL);

  node_t typed_tag_ret_funcptr_callee_mem = {0};
  typed_tag_ret_funcptr_callee_mem.kind = ND_LVAR;
  psx_type_t *typed_tag_ret = ps_type_new_tag(TK_STRUCT, "Ret", 3, 7, 4);
  typed_tag_ret_funcptr_callee_mem.type = ps_type_new_pointer(typed_tag_ret);
  node_function_call_t typed_tag_ret_indirect_call = {0};
  typed_tag_ret_indirect_call.base.kind = ND_FUNCALL;
  typed_tag_ret_indirect_call.callee = &typed_tag_ret_funcptr_callee_mem;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_tag_ret_indirect_call) == NULL);

  node_function_call_t typed_cached_ptr_call = {0};
  typed_cached_ptr_call.base.kind = ND_FUNCALL;
  typed_cached_ptr_call.base.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_ptr_call) ==
              typed_cached_ptr_call.base.type);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind((node_t *)&typed_cached_ptr_call));
  ASSERT_TRUE(!ps_node_value_is_complex((node_t *)&typed_cached_ptr_call));
  ASSERT_TRUE(!ps_node_conversion_value_is_unsigned(
      (node_t *)&typed_cached_ptr_call));
  ASSERT_TRUE(!ps_node_is_long_long_type(
      (node_t *)&typed_cached_ptr_call));
  ASSERT_EQ(0, ps_node_aggregate_value_size((node_t *)&typed_cached_ptr_call));
  ASSERT_TRUE(!ps_node_value_is_void((node_t *)&typed_cached_ptr_call));

  node_function_call_t typed_cached_complex_call = {0};
  typed_cached_complex_call.base.kind = ND_FUNCALL;
  psx_type_t *typed_cached_complex_type = ps_type_new(PSX_TYPE_COMPLEX);
  typed_cached_complex_type->fp_kind = TK_FLOAT_KIND_FLOAT;
  typed_cached_complex_call.base.type = typed_cached_complex_type;
  ps_node_bind_type((node_t *)&typed_cached_complex_call,
                    typed_cached_complex_type);
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_complex_call) ==
              typed_cached_complex_call.base.type);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            ps_node_value_fp_kind((node_t *)&typed_cached_complex_call));
  ASSERT_TRUE(ps_node_value_is_complex((node_t *)&typed_cached_complex_call));

  node_function_call_t typed_cached_struct_call = {0};
  typed_cached_struct_call.base.kind = ND_FUNCALL;
  ps_node_bind_type(
      (node_t *)&typed_cached_struct_call,
      ps_type_new_tag(TK_STRUCT, "CallRet", 7, 0, 12));
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_struct_call) ==
              typed_cached_struct_call.base.type);
  ASSERT_EQ(0, ps_node_aggregate_value_size(
                   (node_t *)&typed_cached_struct_call));

  node_function_call_t typed_cached_void_call = {0};
  typed_cached_void_call.base.kind = ND_FUNCALL;
  ps_node_bind_type((node_t *)&typed_cached_void_call,
                    ps_type_new(PSX_TYPE_VOID));
  ASSERT_TRUE(ps_node_value_is_void((node_t *)&typed_cached_void_call));

  node_function_call_t typed_cached_unsigned_call = {0};
  typed_cached_unsigned_call.base.kind = ND_FUNCALL;
  psx_type_t *typed_cached_unsigned_type =
      ps_type_new_integer(TK_UNSIGNED, 8, 1);
  typed_cached_unsigned_type->is_long_long = 1;
  typed_cached_unsigned_call.base.type = typed_cached_unsigned_type;
  ps_node_bind_type((node_t *)&typed_cached_unsigned_call,
                    typed_cached_unsigned_call.base.type);
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_unsigned_call) ==
              typed_cached_unsigned_call.base.type);
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      (node_t *)&typed_cached_unsigned_call));
  ASSERT_TRUE(ps_node_is_long_long_type(
      (node_t *)&typed_cached_unsigned_call));
  ASSERT_TRUE(ps_node_i64_widen_source_is_unsigned(
      (node_t *)&typed_cached_unsigned_call));

  parsed_code = parse_program_input("long __tm_funcdef_long(void) { return 1; }");
  node_function_definition_t *typed_long_funcdef = as_function_definition(parsed_code[0]);
  const psx_type_t *typed_long_funcdef_ty =
      ps_function_definition_return_type(typed_long_funcdef);
  ASSERT_TRUE(typed_long_funcdef->base.type == NULL);
  ASSERT_TRUE(typed_long_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_long_funcdef_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(typed_long_funcdef_ty));

  parsed_code =
      parse_program_input("int *__tm_funcdef_ptr(int *p) { return p; }");
  node_function_definition_t *typed_ptr_funcdef = as_function_definition(parsed_code[0]);
  const psx_type_t *typed_ptr_funcdef_ty =
      ps_function_definition_return_type(typed_ptr_funcdef);
  ASSERT_TRUE(typed_ptr_funcdef->base.type == NULL);
  ASSERT_TRUE(typed_ptr_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, typed_ptr_funcdef_ty->kind);
  ASSERT_TRUE(ps_type_is_pointer_like(typed_ptr_funcdef_ty));

  parsed_code = parse_program_input("void __tm_funcdef_void(void) { }");
  node_function_definition_t *typed_void_funcdef = as_function_definition(parsed_code[0]);
  const psx_type_t *typed_void_funcdef_ty =
      ps_function_definition_return_type(typed_void_funcdef);
  ASSERT_TRUE(typed_void_funcdef->base.type == NULL);
  ASSERT_TRUE(typed_void_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_VOID, typed_void_funcdef_ty->kind);

  parsed_code = parse_program_input(
      "struct __tm_fdr { int a; int b; } __tm_funcdef_struct(void) { "
      "struct __tm_fdr r; return r; }");
  node_function_definition_t *typed_struct_funcdef = as_function_definition(parsed_code[0]);
  const psx_type_t *typed_struct_funcdef_ty =
      ps_function_definition_return_type(typed_struct_funcdef);
  ASSERT_TRUE(typed_struct_funcdef->base.type == NULL);
  ASSERT_TRUE(typed_struct_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, typed_struct_funcdef_ty->kind);
  ASSERT_EQ(0, ps_type_sizeof(typed_struct_funcdef_ty));
  ASSERT_EQ(8, ps_ctx_type_sizeof_in(
                   test_semantic_context(), typed_struct_funcdef_ty));

  parsed_code =
      parse_program_input("_Complex double __tm_funcdef_complex(void) { return 1; }");
  node_function_definition_t *typed_complex_funcdef = as_function_definition(parsed_code[0]);
  const psx_type_t *typed_complex_funcdef_ty =
      ps_function_definition_return_type(typed_complex_funcdef);
  ASSERT_TRUE(typed_complex_funcdef->base.type == NULL);
  ASSERT_TRUE(typed_complex_funcdef_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, typed_complex_funcdef_ty->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, typed_complex_funcdef_ty->fp_kind);

  node_t typed_cached_pointer_stale_complex = {0};
  typed_cached_pointer_stale_complex.kind = ND_ADD;
  typed_cached_pointer_stale_complex.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind(&typed_cached_pointer_stale_complex));
  ASSERT_TRUE(!ps_node_value_is_complex(&typed_cached_pointer_stale_complex));

  node_t typed_double_ret_callee = {0};
  typed_double_ret_callee.kind = ND_LVAR;
  typed_double_ret_callee.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_function_call_t typed_double_ret_call = {0};
  typed_double_ret_call.base.kind = ND_FUNCALL;
  typed_double_ret_call.callee = &typed_double_ret_callee;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_double_ret_call) == NULL);

  node_t typed_complex_operand = {0};
  typed_complex_operand.kind = ND_NUM;
  psx_type_t *typed_complex_operand_type = ps_type_new(PSX_TYPE_COMPLEX);
  typed_complex_operand_type->fp_kind = TK_FLOAT_KIND_FLOAT;
  typed_complex_operand.type = typed_complex_operand_type;
  node_t *typed_complex_binary =
      ps_node_new_binary(ND_ADD, &typed_complex_operand, ps_node_new_num(1));
  const psx_type_t *typed_complex_binary_ty =
      ps_node_get_type(typed_complex_binary);
  ASSERT_TRUE(typed_complex_binary_ty != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, typed_complex_binary_ty->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            ps_node_value_fp_kind(typed_complex_binary));
  ASSERT_TRUE(ps_node_value_is_complex(typed_complex_binary));
  ps_node_bind_type(typed_complex_binary, typed_complex_binary_ty);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            ps_node_value_fp_kind(typed_complex_binary));
  ASSERT_TRUE(ps_node_value_is_complex(typed_complex_binary));

  node_t typed_double_lhs = {0};
  typed_double_lhs.kind = ND_NUM;
  typed_double_lhs.type = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  node_t *typed_constructed_double =
      ps_node_new_binary(ND_ADD, &typed_double_lhs, ps_node_new_num(1));
  ASSERT_TRUE(typed_constructed_double->type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, typed_constructed_double->type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_value_fp_kind(typed_constructed_double));

  node_t typed_unsigned_ll_lhs = {0};
  typed_unsigned_ll_lhs.kind = ND_NUM;
  psx_type_t *typed_unsigned_ll_type =
      ps_type_new_integer(TK_UNSIGNED, 8, 1);
  typed_unsigned_ll_type->is_long_long = 1;
  typed_unsigned_ll_lhs.type = typed_unsigned_ll_type;
  node_t *typed_constructed_unsigned_ll =
      ps_node_new_binary(ND_MUL, &typed_unsigned_ll_lhs, ps_node_new_num(2));
  ASSERT_TRUE(typed_constructed_unsigned_ll->type != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(typed_constructed_unsigned_ll->type));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      typed_constructed_unsigned_ll));
  ASSERT_TRUE(ps_node_is_long_long_type(typed_constructed_unsigned_ll));

  node_t typed_int_lhs_with_stale_fp = {0};
  typed_int_lhs_with_stale_fp.kind = ND_NUM;
  typed_int_lhs_with_stale_fp.type = ps_type_new_integer(TK_INT, 4, 0);
  node_t typed_int_rhs_with_stale_fp = {0};
  typed_int_rhs_with_stale_fp.kind = ND_NUM;
  typed_int_rhs_with_stale_fp.type = ps_type_new_integer(TK_INT, 4, 0);
  node_t typed_binary_stale_result = {0};
  typed_binary_stale_result.kind = ND_MUL;
  typed_binary_stale_result.lhs = &typed_int_lhs_with_stale_fp;
  typed_binary_stale_result.rhs = &typed_int_rhs_with_stale_fp;
  ASSERT_TRUE(ps_node_get_type(&typed_binary_stale_result) == NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind(&typed_binary_stale_result));
  ASSERT_TRUE(!ps_node_value_is_complex(&typed_binary_stale_result));
  node_t *typed_constructed_stale_int_binary = ps_node_new_binary(
      ND_ADD, &typed_int_lhs_with_stale_fp, &typed_int_rhs_with_stale_fp);
  ASSERT_TRUE(typed_constructed_stale_int_binary->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_constructed_stale_int_binary->type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind(typed_constructed_stale_int_binary));
  ASSERT_TRUE(!ps_node_value_is_complex(typed_constructed_stale_int_binary));

  node_ctrl_t typed_cached_ternary = {0};
  typed_cached_ternary.base.kind = ND_TERNARY;
  psx_type_t *typed_cached_ternary_type =
      ps_type_new_integer(TK_UNSIGNED, 8, 1);
  typed_cached_ternary_type->is_long_long = 1;
  typed_cached_ternary.base.type = typed_cached_ternary_type;
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_ternary) ==
              typed_cached_ternary.base.type);
  ps_node_bind_type((node_t *)&typed_cached_ternary,
                    typed_cached_ternary.base.type);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind((node_t *)&typed_cached_ternary));
  ASSERT_TRUE(!ps_node_value_is_complex((node_t *)&typed_cached_ternary));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(
      (node_t *)&typed_cached_ternary));
  ASSERT_TRUE(ps_node_is_long_long_type((node_t *)&typed_cached_ternary));

  node_t typed_stale_scalar_flags_branch = {0};
  typed_stale_scalar_flags_branch.kind = ND_DEREF;
  node_ctrl_t typed_cached_int_ternary = {0};
  typed_cached_int_ternary.base.kind = ND_TERNARY;
  typed_cached_int_ternary.base.rhs = &typed_stale_scalar_flags_branch;
  typed_cached_int_ternary.els = &typed_stale_scalar_flags_branch;
  ps_node_bind_type((node_t *)&typed_cached_int_ternary,
                    ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_node_get_type((node_t *)&typed_cached_int_ternary) ==
              typed_cached_int_ternary.base.type);
  ASSERT_TRUE(!ps_node_is_long_long_type((node_t *)&typed_cached_int_ternary));
  ASSERT_TRUE(!ps_node_is_plain_char_type((node_t *)&typed_cached_int_ternary));
  ASSERT_TRUE(!ps_node_is_long_double_type((node_t *)&typed_cached_int_ternary));

  node_ctrl_t typed_uncached_int_ternary = {0};
  typed_uncached_int_ternary.base.kind = ND_TERNARY;
  typed_uncached_int_ternary.base.rhs = &typed_int_lhs_with_stale_fp;
  typed_uncached_int_ternary.els = &typed_int_rhs_with_stale_fp;
  ASSERT_TRUE(ps_node_get_type(
                  (node_t *)&typed_uncached_int_ternary) == NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind((node_t *)&typed_uncached_int_ternary));
  ASSERT_TRUE(!ps_node_value_is_complex(
      (node_t *)&typed_uncached_int_ternary));

  node_ctrl_t typed_cached_long_double_ternary = {0};
  typed_cached_long_double_ternary.base.kind = ND_TERNARY;
  psx_type_t *typed_cached_long_double_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  typed_cached_long_double_type->is_long_double = 1;
  typed_cached_long_double_ternary.base.type =
      typed_cached_long_double_type;
  ASSERT_TRUE(ps_node_is_long_double_type(
      (node_t *)&typed_cached_long_double_ternary));

  node_t typed_float_rhs = {0};
  typed_float_rhs.kind = ND_NUM;
  typed_float_rhs.type = ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  node_t *typed_comma = ps_node_new_binary(
      ND_COMMA, ps_node_new_num(0), &typed_float_rhs);
  ASSERT_TRUE(ps_node_get_type(typed_comma) == typed_comma->type);
  ASSERT_TRUE(typed_comma->type != typed_float_rhs.type);
  ASSERT_EQ(PSX_TYPE_FLOAT, typed_comma->type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(typed_comma));
  ps_node_bind_type(typed_comma, typed_comma->type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(typed_comma));

  node_t typed_stmt_expr = {0};
  typed_stmt_expr.kind = ND_STMT_EXPR;
  typed_stmt_expr.rhs = &typed_float_rhs;
  ASSERT_TRUE(ps_node_get_type(&typed_stmt_expr) == NULL);
  ASSERT_TRUE(typed_stmt_expr.type == NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_value_fp_kind(&typed_stmt_expr));

  node_t typed_stmt_tag_ptr_rhs = {0};
  typed_stmt_tag_ptr_rhs.kind = ND_LVAR;
  psx_type_t *typed_stmt_tag =
      ps_type_new_tag(TK_STRUCT, "StmtTag", 7, 4, 16);
  typed_stmt_tag_ptr_rhs.type = ps_type_new_pointer(typed_stmt_tag);
  node_t typed_stmt_tag_ptr = {0};
  typed_stmt_tag_ptr.kind = ND_STMT_EXPR;
  typed_stmt_tag_ptr.rhs = &typed_stmt_tag_ptr_rhs;
  ASSERT_TRUE(ps_node_get_type(&typed_stmt_tag_ptr) == NULL);
  ASSERT_EQ(0, ps_node_value_is_pointer_like(&typed_stmt_tag_ptr));
  ASSERT_EQ(0, ps_node_deref_size(&typed_stmt_tag_ptr));
  analyze_test_expression(&typed_stmt_tag_ptr, NULL);
  ASSERT_EQ(1, ps_node_value_is_pointer_like(&typed_stmt_tag_ptr));
  ASSERT_EQ(0, ps_node_deref_size(&typed_stmt_tag_ptr));
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(&typed_stmt_tag_ptr));
  ASSERT_EQ(0, canonical_node_base_deref_size(&typed_stmt_tag_ptr));
  const psx_type_t *typed_stmt_tag_ptr_type =
      ps_node_get_type(&typed_stmt_tag_ptr);
  ASSERT_EQ(PSX_TYPE_POINTER, typed_stmt_tag_ptr_type->kind);
  ASSERT_TRUE(typed_stmt_tag_ptr_type->base != NULL);
  ASSERT_EQ(TK_STRUCT, typed_stmt_tag_ptr_type->base->tag_kind);
  ASSERT_EQ(7, typed_stmt_tag_ptr_type->base->tag_len);
  ASSERT_TRUE(strncmp(typed_stmt_tag_ptr_type->base->tag_name,
                      "StmtTag", 7) == 0);
  ASSERT_EQ(4, typed_stmt_tag_ptr_type->base->tag_scope_depth_p1);

  node_t typed_stmt_fp_ptr_rhs = {0};
  typed_stmt_fp_ptr_rhs.kind = ND_LVAR;
  typed_stmt_fp_ptr_rhs.type =
      ps_type_new_pointer(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8));
  node_t typed_stmt_fp_ptr = {0};
  typed_stmt_fp_ptr.kind = ND_STMT_EXPR;
  typed_stmt_fp_ptr.rhs = &typed_stmt_fp_ptr_rhs;
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            canonical_node_pointee_fp_kind(&typed_stmt_fp_ptr));
  analyze_test_expression(&typed_stmt_fp_ptr, NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            canonical_node_pointee_fp_kind(&typed_stmt_fp_ptr));

  node_t cached_scalar_rhs = {0};
  cached_scalar_rhs.kind = ND_NUM;
  cached_scalar_rhs.type = ps_type_new_integer(TK_INT, 4, 0);
  node_t cached_unsigned_comma = {0};
  cached_unsigned_comma.kind = ND_COMMA;
  cached_unsigned_comma.rhs = &cached_scalar_rhs;
  psx_type_t *cached_unsigned_comma_type =
      ps_type_new_integer(TK_UNSIGNED, 8, 1);
  cached_unsigned_comma_type->is_long_long = 1;
  cached_unsigned_comma.type = cached_unsigned_comma_type;
  ASSERT_TRUE(ps_node_is_unsigned_type(&cached_unsigned_comma));
  ASSERT_TRUE(ps_node_is_long_long_type(&cached_unsigned_comma));

  node_t cached_plain_char_stmt = {0};
  cached_plain_char_stmt.kind = ND_STMT_EXPR;
  cached_plain_char_stmt.rhs = &cached_scalar_rhs;
  psx_type_t *cached_plain_char_stmt_type =
      ps_type_new_integer(TK_CHAR, 1, 0);
  cached_plain_char_stmt_type->is_plain_char = 1;
  cached_plain_char_stmt.type = cached_plain_char_stmt_type;
  ASSERT_TRUE(ps_node_is_plain_char_type(&cached_plain_char_stmt));

  node_t cached_long_double_stmt = {0};
  cached_long_double_stmt.kind = ND_STMT_EXPR;
  cached_long_double_stmt.rhs = &cached_scalar_rhs;
  psx_type_t *cached_long_double_stmt_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  cached_long_double_stmt_type->is_long_double = 1;
  cached_long_double_stmt.type = cached_long_double_stmt_type;
  ASSERT_TRUE(ps_node_is_long_double_type(&cached_long_double_stmt));

  node_t cached_add_ptr = {0};
  cached_add_ptr.kind = ND_ADD;
  cached_add_ptr.lhs = &cached_scalar_rhs;
  cached_add_ptr.rhs = ps_node_new_num(1);
  psx_type_t *cached_add_ptr_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ps_type_add_qualifiers(cached_add_ptr_type, PSX_TYPE_QUALIFIER_CONST);
  cached_add_ptr.type = cached_add_ptr_type;
  ASSERT_EQ(1, canonical_node_pointer_qual_levels(&cached_add_ptr));
  ASSERT_EQ(4, canonical_node_base_deref_size(&cached_add_ptr));
  assert_node_pointer_qualifiers(&cached_add_ptr, "1", "0");

  node_t cached_nested_ptr = {0};
  cached_nested_ptr.kind = ND_ADD;
  cached_nested_ptr.lhs = &cached_scalar_rhs;
  cached_nested_ptr.rhs = ps_node_new_num(1);
  psx_type_t *cached_nested_inner =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ps_type_add_qualifiers(cached_nested_inner, PSX_TYPE_QUALIFIER_CONST);
  ps_type_add_qualifiers(cached_nested_inner, PSX_TYPE_QUALIFIER_VOLATILE);
  psx_type_t *cached_nested_ptr_type =
      ps_type_new_pointer(cached_nested_inner);
  ps_type_add_qualifiers(cached_nested_ptr_type, PSX_TYPE_QUALIFIER_CONST);
  cached_nested_ptr.type = cached_nested_ptr_type;
  ASSERT_EQ(2, canonical_node_pointer_qual_levels(&cached_nested_ptr));
  assert_node_pointer_qualifiers(&cached_nested_ptr, "11", "01");

  node_t cached_ptr_array_add = {0};
  cached_ptr_array_add.kind = ND_ADD;
  cached_ptr_array_add.lhs = &cached_scalar_rhs;
  cached_ptr_array_add.rhs = ps_node_new_num(1);
  cached_ptr_array_add.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(&cached_ptr_array_add));

  node_t cached_real_ptr_array_add = {0};
  cached_real_ptr_array_add.kind = ND_ADD;
  cached_real_ptr_array_add.lhs = &cached_scalar_rhs;
  cached_real_ptr_array_add.rhs = ps_node_new_num(1);
  cached_real_ptr_array_add.type = ps_type_new_pointer(
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0));
  ASSERT_EQ(12, canonical_node_ptr_array_pointee_bytes(&cached_real_ptr_array_add));

  node_t cached_ptr_to_array_stride = {0};
  cached_ptr_to_array_stride.kind = ND_ADD;
  cached_ptr_to_array_stride.lhs = &cached_scalar_rhs;
  cached_ptr_to_array_stride.rhs = ps_node_new_num(1);
  psx_type_t *cached_ptr_to_array_stride_inner =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 0);
  psx_type_t *cached_ptr_to_array_stride_array =
      ps_type_new_array(cached_ptr_to_array_stride_inner, 3, 48, 0);
  cached_ptr_to_array_stride.type =
      ps_type_new_pointer(cached_ptr_to_array_stride_array);
  int cached_ptr_to_array_inner =
      canonical_node_array_subscript_stride_bytes(
          &cached_ptr_to_array_stride, 0);
  int cached_ptr_to_array_next =
      canonical_node_array_subscript_stride_bytes(
          &cached_ptr_to_array_stride, 1);
  ASSERT_EQ(16, cached_ptr_to_array_inner);
  ASSERT_EQ(4, cached_ptr_to_array_next);

  node_t cached_stale_array_subscript_base = {0};
  cached_stale_array_subscript_base.kind = ND_LVAR;
  psx_type_t *cached_stale_subscript_leaf =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 0);
  psx_type_t *cached_stale_subscript_row =
      ps_type_new_array(cached_stale_subscript_leaf, 3, 48, 0);
  cached_stale_array_subscript_base.type =
      ps_type_new_array(cached_stale_subscript_row, 2, 96, 0);
  int cached_stale_subscript_inner =
      canonical_node_array_subscript_stride_bytes(
          &cached_stale_array_subscript_base, 0);
  int cached_stale_subscript_next =
      canonical_node_array_subscript_stride_bytes(
          &cached_stale_array_subscript_base, 1);
  ASSERT_EQ(48, cached_stale_subscript_inner);
  ASSERT_EQ(16, cached_stale_subscript_next);
  node_t *cached_stale_array_row = ps_node_new_subscript_deref_for(
      &cached_stale_array_subscript_base,
      &cached_stale_array_subscript_base, ps_node_new_num(0));
  cached_stale_subscript_inner =
      canonical_node_array_subscript_stride_bytes(cached_stale_array_row, 0);
  cached_stale_subscript_next =
      canonical_node_array_subscript_stride_bytes(cached_stale_array_row, 1);
  ASSERT_EQ(16, ps_node_deref_size(cached_stale_array_row));
  ASSERT_EQ(16, cached_stale_subscript_inner);
  ASSERT_EQ(4, cached_stale_subscript_next);

  node_t cached_vla_add = {0};
  cached_vla_add.kind = ND_ADD;
  cached_vla_add.lhs = &cached_scalar_rhs;
  cached_vla_add.rhs = ps_node_new_num(1);
  cached_vla_add.type = ps_type_new_pointer(
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 0, 0, 1));
  ps_node_set_vla_runtime_view(&cached_vla_add, 123, 2);
  ASSERT_EQ(123, ps_node_vla_row_stride_frame_off(&cached_vla_add));

  node_t cached_stale_plain_stride_add = {0};
  cached_stale_plain_stride_add.kind = ND_ADD;
  cached_stale_plain_stride_add.lhs = &cached_scalar_rhs;
  cached_stale_plain_stride_add.rhs = ps_node_new_num(1);
  cached_stale_plain_stride_add.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   &cached_stale_plain_stride_add, 0));

  node_t cached_pointee_comma = {0};
  cached_pointee_comma.kind = ND_COMMA;
  cached_pointee_comma.rhs = &cached_scalar_rhs;
  psx_type_t *cached_unsigned_char = ps_type_new_integer(TK_UNSIGNED, 1, 1);
  cached_pointee_comma.type = ps_type_new_pointer(cached_unsigned_char);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(&cached_pointee_comma));

  node_t cached_tag_add = {0};
  cached_tag_add.kind = ND_ADD;
  cached_tag_add.lhs = &cached_scalar_rhs;
  cached_tag_add.rhs = ps_node_new_num(1);
  psx_type_t *cached_own_tag =
      ps_type_new_tag(TK_STRUCT, "OwnTag", 6, 5, 12);
  cached_tag_add.type = ps_type_new_pointer(cached_own_tag);
  const psx_type_t *cached_tag_type = ps_node_get_type(&cached_tag_add);
  ASSERT_EQ(PSX_TYPE_POINTER, cached_tag_type->kind);
  ASSERT_TRUE(cached_tag_type->base == cached_own_tag);
  ASSERT_EQ(TK_STRUCT, cached_tag_type->base->tag_kind);
  ASSERT_EQ(6, cached_tag_type->base->tag_len);
  ASSERT_TRUE(strncmp(cached_tag_type->base->tag_name,
                      "OwnTag", 6) == 0);
  ASSERT_EQ(5, cached_tag_type->base->tag_scope_depth_p1);

  node_t typed_tag_mem = {0};
  typed_tag_mem.kind = ND_LVAR;
  psx_type_t *typed_tag = ps_type_new_tag(TK_STRUCT, "Typed", 5, 3, 4);
  typed_tag_mem.type = ps_type_new_pointer(typed_tag);
  const psx_type_t *typed_tag_ptr_type = ps_node_get_type(&typed_tag_mem);
  ASSERT_EQ(PSX_TYPE_POINTER, typed_tag_ptr_type->kind);
  ASSERT_TRUE(typed_tag_ptr_type->base == typed_tag);
  ASSERT_EQ(TK_STRUCT, typed_tag_ptr_type->base->tag_kind);
  ASSERT_EQ(5, typed_tag_ptr_type->base->tag_len);
  ASSERT_TRUE(strncmp(typed_tag_ptr_type->base->tag_name,
                      "Typed", 5) == 0);
  ASSERT_EQ(3, typed_tag_ptr_type->base->tag_scope_depth_p1);
  ASSERT_EQ(0, ps_node_aggregate_value_size(&typed_tag_mem));

  node_t canonical_aggregate = {0};
  canonical_aggregate.kind = ND_DEREF;
  canonical_aggregate.type = ps_type_new_tag(
      TK_STRUCT, "CanonicalAgg", 12, 0, 6);
  ASSERT_EQ(0, ps_node_aggregate_value_size(&canonical_aggregate));

  node_t typed_cast_long = {0};
  typed_cast_long.kind = ND_CAST;
  typed_cast_long.type = ps_type_new_integer(TK_LONG, 8, 0);
  int cast_target_size = 0;
  int cast_widen_zext = 0;
  int cast_needs_i64 = 0;
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      &typed_cast_long, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(8, cast_target_size);
  ASSERT_EQ(0, cast_widen_zext);
  ASSERT_EQ(1, cast_needs_i64);

  psx_type_t *inferred_unsigned_cast_type =
      ps_type_new_integer(TK_UNSIGNED, 8, 1);
  node_t *inferred_unsigned_cast = ps_node_new_integer_cast_result(
      ps_node_new_num(1), inferred_unsigned_cast_type);
  ASSERT_TRUE(inferred_unsigned_cast->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, inferred_unsigned_cast->type->kind);
  ASSERT_TRUE(ps_type_is_unsigned(inferred_unsigned_cast->type));

  node_t *canonical_zext_cast = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), ps_type_new_integer(TK_UNSIGNED, 8, 1), 1);
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      canonical_zext_cast, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(1, cast_widen_zext);

  node_t *typed_internal_slot = ps_node_new_lvar_typed(1234, 8);
  ASSERT_TRUE(typed_internal_slot->type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typed_internal_slot->type->kind);
  ASSERT_EQ(8, ps_type_sizeof(typed_internal_slot->type));

  psx_type_t *typed_unsigned_cast_type = ps_type_new_integer(TK_UNSIGNED, 4, 1);
  node_t *typed_unsigned_cast_node = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), typed_unsigned_cast_type, 0);
  ASSERT_TRUE(ps_node_get_type(typed_unsigned_cast_node) == typed_unsigned_cast_type);
  ASSERT_TRUE(ps_node_is_unsigned_type(typed_unsigned_cast_node));

  psx_type_t *typed_bool_cast_type = ps_type_new(PSX_TYPE_BOOL);
  node_t *typed_bool_cast_node = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), typed_bool_cast_type, 0);
  ASSERT_TRUE(ps_node_get_type(typed_bool_cast_node) == typed_bool_cast_type);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(typed_bool_cast_node)->kind);

  psx_type_t *typed_atomic_cast_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_type_add_qualifiers(typed_atomic_cast_type, PSX_TYPE_QUALIFIER_ATOMIC);
  node_t *typed_atomic_cast_node = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), typed_atomic_cast_type, 0);
  ASSERT_TRUE(ps_node_get_type(typed_atomic_cast_node) == typed_atomic_cast_type);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(typed_atomic_cast_node), PSX_TYPE_QUALIFIER_ATOMIC));

  psx_type_t *typed_fp_to_unsigned_type = ps_type_new_integer(TK_UNSIGNED, 4, 1);
  node_t *typed_fp_to_unsigned = ps_node_new_fp_to_int_cast(
      ps_node_new_num(1), typed_fp_to_unsigned_type);
  ASSERT_TRUE(ps_node_get_type(typed_fp_to_unsigned) == typed_fp_to_unsigned_type);
  ASSERT_TRUE(ps_node_is_unsigned_type(typed_fp_to_unsigned));
  ASSERT_EQ(4, ps_node_storage_type_size(typed_fp_to_unsigned));

  psx_type_t *typed_fp_to_bool_type = ps_type_new(PSX_TYPE_BOOL);
  node_t *typed_fp_to_bool = ps_node_new_fp_to_int_cast(
      ps_node_new_num(1), typed_fp_to_bool_type);
  ASSERT_TRUE(ps_node_get_type(typed_fp_to_bool) == typed_fp_to_bool_type);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(typed_fp_to_bool)->kind);

  psx_type_t *typed_fp_to_atomic_type = ps_type_new_integer(TK_INT, 4, 0);
  ps_type_add_qualifiers(typed_fp_to_atomic_type, PSX_TYPE_QUALIFIER_ATOMIC);
  node_t *typed_fp_to_atomic = ps_node_new_fp_to_int_cast(
      ps_node_new_num(1), typed_fp_to_atomic_type);
  ASSERT_TRUE(ps_node_get_type(typed_fp_to_atomic) == typed_fp_to_atomic_type);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(typed_fp_to_atomic), PSX_TYPE_QUALIFIER_ATOMIC));

  parsed_code = parse_program_input(
      "unsigned __tm_fp_to_unsigned_expr(double d){ return (unsigned)d; }");
  fn = as_function_definition(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *fp_to_unsigned_ret = body->body[0];
  ASSERT_EQ(ND_RETURN, fp_to_unsigned_ret->kind);
  node_t *fp_to_unsigned_result = fp_to_unsigned_ret->lhs;
  ASSERT_TRUE(ps_node_is_unsigned_type(fp_to_unsigned_result));
  node_t *fp_to_unsigned_inner =
      fp_to_unsigned_result->kind == ND_CAST
          ? fp_to_unsigned_result->lhs
          : fp_to_unsigned_result;
  ASSERT_EQ(ND_FP_TO_INT, fp_to_unsigned_inner->kind);
  ASSERT_TRUE(ps_node_get_type(fp_to_unsigned_inner) != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(ps_node_get_type(fp_to_unsigned_inner)));
  ASSERT_TRUE(ps_node_conversion_value_is_unsigned(fp_to_unsigned_inner));

  node_t typed_cast_ptr = {0};
  typed_cast_ptr.kind = ND_CAST;
  typed_cast_ptr.type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      &typed_cast_ptr, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(8, cast_target_size);
  ASSERT_EQ(0, cast_widen_zext);
  ASSERT_EQ(0, cast_needs_i64);

  node_t typed_incomplete_cast = {0};
  typed_incomplete_cast.kind = ND_CAST;
  typed_incomplete_cast.type =
      ps_type_new_tag(TK_STRUCT, "Incomplete", 10, 0, 0);
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      &typed_incomplete_cast, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(0, cast_target_size);
  ASSERT_EQ(0, cast_widen_zext);
  ASSERT_EQ(0, cast_needs_i64);

  psx_type_t *typed_signed_ptr_cast_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 0));
  node_t *typed_signed_ptr_cast = ps_node_new_pointer_cast_result(
      ps_node_new_num(0), typed_signed_ptr_cast_type);
  ASSERT_TRUE(ps_node_get_type(typed_signed_ptr_cast) == typed_signed_ptr_cast_type);
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(typed_signed_ptr_cast));

  psx_type_t *typed_flat_ptr_stale_cast_type = ps_type_new_pointer(NULL);
  node_t *typed_flat_ptr_stale_cast = ps_node_new_pointer_cast_result(
      ps_node_new_num(0), typed_flat_ptr_stale_cast_type);
  ASSERT_TRUE(ps_node_get_type(typed_flat_ptr_stale_cast) ==
              typed_flat_ptr_stale_cast_type);
  ASSERT_TRUE(ps_node_value_is_pointer_like(typed_flat_ptr_stale_cast));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(typed_flat_ptr_stale_cast));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   typed_flat_ptr_stale_cast, 0));

  psx_type_t *typed_bool_ptr_cast_type =
      ps_type_new_pointer(ps_type_new(PSX_TYPE_BOOL));
  node_t *typed_bool_ptr_cast = ps_node_new_pointer_cast_result(
      ps_node_new_num(0), typed_bool_ptr_cast_type);
  ASSERT_TRUE(ps_node_get_type(typed_bool_ptr_cast) == typed_bool_ptr_cast_type);
  ASSERT_TRUE(canonical_node_pointee_is_bool(typed_bool_ptr_cast));

  psx_type_t *typed_tag_ptr_cast_type = ps_type_new_pointer(
      ps_type_new_tag(TK_STRUCT, "PCast", 5, 0, 4));
  node_t *typed_tag_ptr_cast = ps_node_new_pointer_cast_result(
      ps_node_new_num(0), typed_tag_ptr_cast_type);
  const psx_type_t *typed_tag_ptr_cast_result_type =
      ps_node_get_type(typed_tag_ptr_cast);
  ASSERT_TRUE(typed_tag_ptr_cast_result_type ==
              typed_tag_ptr_cast_type);
  ASSERT_EQ(PSX_TYPE_POINTER,
            typed_tag_ptr_cast_result_type->kind);
  ASSERT_TRUE(typed_tag_ptr_cast_result_type->base != NULL);
  ASSERT_EQ(TK_STRUCT,
            typed_tag_ptr_cast_result_type->base->tag_kind);
  ASSERT_EQ(5, typed_tag_ptr_cast_result_type->base->tag_len);
  ASSERT_TRUE(strncmp(typed_tag_ptr_cast_result_type->base->tag_name,
                      "PCast", 5) == 0);

  node_t *plain_widen_cast = ps_node_new_integer_cast_result_ex(
      ps_node_new_num(1), ps_type_new_integer(TK_UNSIGNED, 8, 1), 1);
  ASSERT_TRUE(ps_node_cast_i64_extension_info(
      plain_widen_cast, &cast_target_size, &cast_widen_zext,
      &cast_needs_i64));
  ASSERT_EQ(4, cast_target_size);
  ASSERT_EQ(1, cast_widen_zext);
  ASSERT_EQ(0, cast_needs_i64);

  parsed_code = parse_program_input("int main() { struct S { int x; } *p; p=0; return p==0; }");
  fn = as_function_definition(parsed_code[0]);
  body = as_block(fn->base.rhs);
  assign = body->body[1];
  ASSERT_EQ(ND_ASSIGN, assign->kind);
  const psx_type_t *ptr_ty = ps_node_get_type(assign->lhs);
  ASSERT_TRUE(ptr_ty != NULL);
  ASSERT_TRUE(assign->lhs->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_ty->kind);
  ASSERT_TRUE(ps_type_is_pointer(ptr_ty));
  ASSERT_TRUE(ptr_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, ptr_ty->base->tag_kind);
  ASSERT_EQ(1, ptr_ty->base->tag_len);
  ASSERT_TRUE(strncmp(ptr_ty->base->tag_name, "S", 1) == 0);
  lvar_t *p_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(p_lvar != NULL);
  ASSERT_TRUE(ps_node_lvar_symbol(assign->lhs) == p_lvar);
  ASSERT_TRUE(p_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, p_lvar->decl_type->kind);
  ASSERT_TRUE(p_lvar->decl_type->base != NULL);
  ASSERT_EQ(TK_STRUCT, p_lvar->decl_type->base->tag_kind);

  parsed_code = parse_program_input("double __tm_param_fp(double *p) { return p[0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *param_fp_lvar = find_func_lvar(fn, "p");
  ASSERT_TRUE(param_fp_lvar != NULL);
  ASSERT_TRUE(param_fp_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, param_fp_lvar->decl_type->kind);
  ASSERT_TRUE(param_fp_lvar->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, param_fp_lvar->decl_type->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, param_fp_lvar->decl_type->base->fp_kind);

  parsed_code = parse_program_input(
      "int main() { struct R { int r[4]; }; struct R r1={{1,2,3,4}}; r1.r; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *member = body->body[2];
  ASSERT_EQ(ND_DEREF, member->kind);
  const psx_type_t *array_ty = ps_node_get_type(member);
  ASSERT_TRUE(array_ty != NULL);
  ASSERT_TRUE(member->type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_ty->kind);
  ASSERT_EQ(16, ps_type_sizeof(array_ty));
  ASSERT_TRUE(!ps_type_is_pointer(array_ty));
  ASSERT_TRUE(ps_type_is_pointer_like(array_ty));
  lvar_t *r1_lvar = find_func_lvar(fn, "r1");
  ASSERT_TRUE(r1_lvar != NULL);
  ASSERT_TRUE(r1_lvar->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, r1_lvar->decl_type->kind);
  ASSERT_EQ(0, ps_type_sizeof(r1_lvar->decl_type));
  ASSERT_EQ(16, ps_ctx_type_sizeof_in(
                    test_semantic_context(), r1_lvar->decl_type));
  ASSERT_TRUE(ps_lvar_is_tag_aggregate(r1_lvar));
  ASSERT_TRUE(ps_lvar_is_struct_aggregate(r1_lvar));
  ASSERT_TRUE(!ps_lvar_is_union_aggregate(r1_lvar));

  lvar_t tmp_tag_lvar = {0};
  tmp_tag_lvar.size = 4;
  set_test_storage_fixture_type(
      &tmp_tag_lvar, ps_type_new_tag(TK_UNION, NULL, 0, 0, 4));
  ASSERT_TRUE(ps_lvar_is_tag_aggregate(&tmp_tag_lvar));
  ASSERT_TRUE(!ps_lvar_is_struct_aggregate(&tmp_tag_lvar));
  ASSERT_TRUE(ps_lvar_is_union_aggregate(&tmp_tag_lvar));
  set_test_storage_fixture_type(
      &tmp_tag_lvar,
      ps_type_new_pointer(
          ps_type_new_tag(TK_UNION, NULL, 0, 0, 4)));
  ASSERT_TRUE(!ps_lvar_is_tag_aggregate(&tmp_tag_lvar));
  ASSERT_TRUE(!ps_lvar_is_union_aggregate(&tmp_tag_lvar));
  ASSERT_TRUE(ps_ctx_is_tag_aggregate_kind(TK_STRUCT));
  ASSERT_TRUE(ps_ctx_is_tag_aggregate_kind(TK_UNION));
  ASSERT_TRUE(!ps_ctx_is_tag_aggregate_kind(TK_ENUM));
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_type_kind_from_tag_kind(TK_STRUCT));
  ASSERT_EQ(PSX_TYPE_UNION, ps_type_kind_from_tag_kind(TK_UNION));
  ASSERT_EQ(PSX_TYPE_INVALID, ps_type_kind_from_tag_kind(TK_ENUM));
  psx_type_t *tmp_struct_type = ps_type_new_tag(TK_STRUCT, "TS", 2, 1, 4);
  ASSERT_TRUE(ps_type_is_tag_aggregate(tmp_struct_type));
  ASSERT_EQ(PSX_TYPE_STRUCT, tmp_struct_type->kind);
  psx_type_t *tmp_union_type = ps_type_new_tag(TK_UNION, "TU", 2, 1, 4);
  ASSERT_TRUE(ps_type_is_tag_aggregate(tmp_union_type));
  ASSERT_EQ(PSX_TYPE_UNION, tmp_union_type->kind);
  psx_type_t *tmp_invalid_tag_type = ps_type_new_tag(TK_ENUM, "TE", 2, 1, 4);
  ASSERT_TRUE(!ps_type_is_tag_aggregate(tmp_invalid_tag_type));
  ASSERT_EQ(PSX_TYPE_INVALID, tmp_invalid_tag_type->kind);
  parsed_code = parse_program_input(
      "struct FlatIn { int a; int b; };"
      "struct FlatOut { int x; struct FlatIn in; union { int u; int v; }; int y; };"
      "union FlatU { int i; struct FlatIn in; };"
      "union FlatFpU { int i; float f; double d; };"
      "int main(){ return 0; }");
  (void)parsed_code;
  ASSERT_EQ(2, test_tag_flat_slot_count(TK_STRUCT, "FlatIn", 6));
  ASSERT_EQ(5, test_tag_flat_slot_count(TK_STRUCT, "FlatOut", 7));
  ASSERT_EQ(2, test_tag_flat_slot_count(TK_UNION, "FlatU", 5));
  tag_member_info_t flat_member = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "FlatOut", 7, "in", 2, &flat_member));
  ASSERT_EQ(2, test_tag_member_flat_slots(&flat_member));
  ASSERT_EQ(2, test_tag_member_elem_flat_slots(&flat_member));
  int named_ordinal = -1;
  tag_member_info_t named_member = {0};
  ASSERT_TRUE(test_tag_find_named_member(TK_STRUCT, "FlatOut", 7, "y", 1,
                                        &named_member, &named_ordinal));
  ASSERT_EQ(5, named_ordinal);
  ASSERT_TRUE(named_member.name != NULL);
  ASSERT_EQ(0, strncmp(named_member.name, "y", (size_t)named_member.len));
  named_ordinal = -1;
  ASSERT_TRUE(!test_tag_find_named_member(TK_STRUCT, "FlatOut", 7, "missing", 7,
                                         &named_member, &named_ordinal));
  ASSERT_EQ(-1, named_ordinal);
  global_var_t tmp_no_init = {0};
  psx_gvar_initializer_class_t no_init_cls =
      ps_gvar_initializer_class(&tmp_no_init, 0);
  ASSERT_TRUE(!no_init_cls.has_explicit_initializer);
  ASSERT_TRUE(!no_init_cls.has_payload);
  global_var_t tmp_zero_init = {0};
  tmp_zero_init.has_init = 1;
  psx_gvar_initializer_class_t zero_init_cls =
      ps_gvar_initializer_class(&tmp_zero_init, 0);
  ASSERT_TRUE(zero_init_cls.has_explicit_initializer);
  ASSERT_TRUE(zero_init_cls.has_payload);
  global_var_t tmp_empty_aggregate_init = {0};
  tmp_empty_aggregate_init.has_init = 1;
  tmp_empty_aggregate_init.decl_type =
      ps_type_new_tag(TK_STRUCT, NULL, 0, 0, 0);
  psx_gvar_initializer_class_t empty_aggregate_cls =
      ps_gvar_initializer_class(&tmp_empty_aggregate_init, 1);
  ASSERT_TRUE(empty_aggregate_cls.has_explicit_initializer);
  ASSERT_TRUE(!empty_aggregate_cls.has_payload);
  ASSERT_EQ(PSX_GVAR_INIT_KIND_AGGREGATE, empty_aggregate_cls.kind);
  char *init_syms[1] = {NULL};
  int init_sym_lens[1] = {-2};
  global_var_t tmp_union_init = {0};
  tmp_union_init.init_count = 1;
  tmp_union_init.init_value_symbols = init_syms;
  tmp_union_init.init_value_symbol_lens = init_sym_lens;
  psx_gvar_init_slot_t sentinel_slot = ps_gvar_init_slot_view(&tmp_union_init, 0);
  ASSERT_TRUE(sentinel_slot.in_range);
  ASSERT_TRUE(sentinel_slot.symbol == NULL);
  ASSERT_EQ(-2, sentinel_slot.symbol_len);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, sentinel_slot.fp_sentinel_kind);
  ASSERT_EQ(0, ps_gvar_union_init_slot_ordinal(&tmp_union_init, 0));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, ps_gvar_init_slot_fp_kind(&tmp_union_init, 0));
  ASSERT_TRUE(!ps_gvar_init_slot_is_plain_zero(&tmp_union_init, 0));
  ASSERT_EQ(4, ps_gvar_union_init_slot_fp_size(&tmp_union_init, 0));
  tag_member_info_t selected_union_member = {0};
  ASSERT_TRUE(ps_ctx_get_tag_member_info_in(test_semantic_context(), TK_UNION, "FlatFpU", 7, 0,
                                          &selected_union_member));
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_tag_member_decl_fp_kind(&selected_union_member));
  ASSERT_TRUE(test_tag_select_union_member_for_init_slot(TK_UNION, "FlatFpU", 7,
                                                        &tmp_union_init, 0,
                                                        &selected_union_member));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            ps_tag_member_decl_fp_kind(&selected_union_member));
  selected_union_member = (tag_member_info_t){0};
  selected_union_member.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(test_tag_select_union_member_for_init_slot(TK_UNION, "FlatFpU", 7,
                                                        &tmp_union_init, 0,
                                                        &selected_union_member));
  ASSERT_TRUE(selected_union_member.name != NULL);
  ASSERT_EQ(0, strncmp(selected_union_member.name, "f",
                       (size_t)selected_union_member.len));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            ps_tag_member_decl_fp_kind(&selected_union_member));
  global_var_t tmp_member_value_gv = {0};
  ps_gvar_init_slots_alloc(&tmp_member_value_gv, 1, 1);
  tmp_member_value_gv.init_count = 1;
  ps_gvar_init_slot_write(&tmp_member_value_gv, 0, 42, 3.5, NULL, 0);
  tag_member_info_t tmp_member_value_double = {0};
  tmp_member_value_double.decl_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  psx_gvar_init_member_value_t tmp_member_value =
      ps_gvar_init_member_value(&tmp_member_value_gv, 0,
                                 &tmp_member_value_double, 8);
  ASSERT_EQ(PSX_GVAR_INIT_VALUE_FLOAT, tmp_member_value.kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tmp_member_value.fp_kind);
  ASSERT_EQ(8, tmp_member_value.size);
  global_var_t tmp_member_bool_gv = {0};
  ps_gvar_init_slots_alloc(&tmp_member_bool_gv, 1, 0);
  tmp_member_bool_gv.init_count = 1;
  ps_gvar_init_slot_write(&tmp_member_bool_gv, 0, 7, 0.0, NULL, 0);
  tag_member_info_t tmp_member_value_bool = {0};
  tmp_member_value_bool.decl_type = ps_type_new_integer(TK_BOOL, 1, 1);
  tmp_member_value = ps_gvar_init_member_value(
      &tmp_member_bool_gv, 0, &tmp_member_value_bool, 1);
  ASSERT_EQ(PSX_GVAR_INIT_VALUE_INTEGER, tmp_member_value.kind);
  ASSERT_EQ(1, tmp_member_value.value);
  ASSERT_EQ(1, tmp_member_value.size);
  selected_union_member = (tag_member_info_t){0};
  ASSERT_TRUE(test_tag_union_init_member_for_slot(TK_UNION, "FlatFpU", 7,
                                                 &tmp_union_init, 0,
                                                 &selected_union_member));
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            ps_tag_member_decl_fp_kind(&selected_union_member));
  int flatu_in_ordinal = -1;
  tag_member_info_t flatu_in_member = {0};
  ASSERT_TRUE(test_tag_find_named_member(TK_UNION, "FlatU", 5, "in", 2,
                                        &flatu_in_member, &flatu_in_ordinal));
  int init_ordinals[1] = {flatu_in_ordinal};
  global_var_t tmp_union_ord = {0};
  tmp_union_ord.init_count = 1;
  tmp_union_ord.init_union_ordinals = init_ordinals;
  psx_gvar_init_slot_t zero_slot = ps_gvar_init_slot_view(&tmp_union_ord, 0);
  ASSERT_TRUE(zero_slot.in_range);
  ASSERT_EQ(0, zero_slot.symbol_len);
  ASSERT_EQ(0, zero_slot.value);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, zero_slot.fp_sentinel_kind);
  ASSERT_EQ(flatu_in_ordinal, ps_gvar_union_init_slot_ordinal(&tmp_union_ord, 0));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_gvar_init_slot_fp_kind(&tmp_union_ord, 0));
  ASSERT_TRUE(ps_gvar_init_slot_is_plain_zero(&tmp_union_ord, 0));
  tag_member_info_t overridden_union_member = {0};
  ASSERT_TRUE(test_tag_union_init_member_for_slot(TK_UNION, "FlatU", 5,
                                                 &tmp_union_ord, 0,
                                                 &overridden_union_member));
  ASSERT_TRUE(overridden_union_member.name != NULL);
  ASSERT_EQ(0, strncmp(overridden_union_member.name, "in",
                       (size_t)overridden_union_member.len));
  int first_named_ordinal = -1;
  tag_member_info_t first_named_member = {0};
  ASSERT_TRUE(test_tag_first_named_member(TK_STRUCT, "FlatOut", 7,
                                         &first_named_member, &first_named_ordinal));
  ASSERT_EQ(0, first_named_ordinal);
  ASSERT_TRUE(first_named_member.name != NULL);
  ASSERT_EQ(0, strncmp(first_named_member.name, "x", (size_t)first_named_member.len));
  first_named_ordinal = -1;
  ASSERT_TRUE(test_tag_first_named_member(TK_UNION, "FlatU", 5,
                                         &first_named_member, &first_named_ordinal));
  ASSERT_EQ(0, first_named_ordinal);
  ASSERT_TRUE(first_named_member.name != NULL);
  ASSERT_EQ(0, strncmp(first_named_member.name, "i", (size_t)first_named_member.len));
  int next_named_ordinal = 0;
  tag_member_info_t next_named_member = {0};
  ASSERT_TRUE(test_tag_next_named_member(TK_STRUCT, "FlatOut", 7,
                                        &next_named_ordinal, &next_named_member));
  ASSERT_EQ(1, next_named_ordinal);
  ASSERT_TRUE(next_named_member.name != NULL);
  ASSERT_EQ(0, strncmp(next_named_member.name, "x", (size_t)next_named_member.len));
  ASSERT_TRUE(test_tag_next_named_member(TK_STRUCT, "FlatOut", 7,
                                        &next_named_ordinal, &next_named_member));
  ASSERT_EQ(2, next_named_ordinal);
  ASSERT_TRUE(next_named_member.name != NULL);
  ASSERT_EQ(0, strncmp(next_named_member.name, "in", (size_t)next_named_member.len));
  int flat_slot_ordinal = -1;
  tag_member_info_t flat_slot_member = {0};
  ASSERT_TRUE(test_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 0,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(0, flat_slot_ordinal);
  ASSERT_TRUE(flat_slot_member.name != NULL);
  ASSERT_EQ(0, strncmp(flat_slot_member.name, "x", (size_t)flat_slot_member.len));
  ASSERT_TRUE(test_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 1,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(1, flat_slot_ordinal);
  ASSERT_TRUE(flat_slot_member.name != NULL);
  ASSERT_EQ(0, strncmp(flat_slot_member.name, "in", (size_t)flat_slot_member.len));
  ASSERT_TRUE(test_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 2,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(1, flat_slot_ordinal);
  ASSERT_TRUE(test_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 3,
                                          &flat_slot_member, &flat_slot_ordinal));
  const psx_type_t *flat_slot_tag_type =
      ps_tag_member_decl_tag_type(&flat_slot_member);
  ASSERT_TRUE(flat_slot_tag_type != NULL);
  ASSERT_EQ(TK_UNION, flat_slot_tag_type->tag_kind);
  ASSERT_TRUE(test_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 4,
                                          &flat_slot_member, &flat_slot_ordinal));
  ASSERT_EQ(5, flat_slot_ordinal);
  ASSERT_TRUE(flat_slot_member.name != NULL);
  ASSERT_EQ(0, strncmp(flat_slot_member.name, "y", (size_t)flat_slot_member.len));
  ASSERT_TRUE(!test_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 5,
                                           &flat_slot_member, &flat_slot_ordinal));
  ASSERT_TRUE(test_tag_member_at_flat_slot(TK_STRUCT, "FlatOut", 7, 3,
                                          &flat_slot_member, &flat_slot_ordinal));
  psx_tag_flat_cover_state_t flat_cover;
  ps_tag_flat_cover_state_init(&flat_cover);
  ASSERT_TRUE(!ps_tag_flat_cover_state_covers(&flat_cover, &flat_slot_member));
  test_tag_flat_cover_state_note(&flat_cover, TK_STRUCT, "FlatOut", 7, &flat_slot_member);
  tag_member_info_t flat_promoted_union_member = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "FlatOut", 7, "u", 1,
                                           &flat_promoted_union_member));
  ASSERT_TRUE(ps_tag_flat_cover_state_covers(&flat_cover, &flat_promoted_union_member));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "FlatOut", 7, "y", 1,
                                           &flat_slot_member));
  ASSERT_TRUE(!ps_tag_flat_cover_state_covers(&flat_cover, &flat_slot_member));
  int flat_ordinal = -1;
  ASSERT_EQ(4, test_tag_member_designator_slot(TK_STRUCT, "FlatOut", 7, "y", 1,
                                              &flat_ordinal));
  ASSERT_EQ(5, flat_ordinal);
  flat_ordinal = -1;
  ASSERT_EQ(0, test_tag_member_designator_slot(TK_UNION, "FlatU", 5, "in", 2,
                                              &flat_ordinal));
  ASSERT_EQ(1, flat_ordinal);

  parsed_code = parse_program_input("union LongMemberU { int a; long b; }; "
                                    "static union LongMemberU __tm_lu = {.b = 0x1122334455L}; "
                                    "int main(){ return 0; }");
  (void)parsed_code;
  global_var_t *__tm_lu = find_test_global_var("__tm_lu", 7);
  ASSERT_TRUE(__tm_lu != NULL);
  ASSERT_TRUE(ps_gvar_is_union_aggregate(__tm_lu));
  psx_gvar_initializer_class_t long_union_cls =
      ps_gvar_initializer_class(__tm_lu, 0);
  ASSERT_EQ(PSX_GVAR_INIT_KIND_AGGREGATE, long_union_cls.kind);
  ASSERT_TRUE(long_union_cls.has_aggregate_initializer);
  tag_member_info_t long_union_member = {0};
  ASSERT_TRUE(test_tag_union_init_member_for_slot(TK_UNION, "LongMemberU", 11,
                                                 __tm_lu, 0, &long_union_member));
  ASSERT_TRUE(long_union_member.name != NULL);
  ASSERT_EQ(0, strncmp(long_union_member.name, "b",
                       (size_t)long_union_member.len));
  ASSERT_EQ(8, test_tag_member_decl_value_size(&long_union_member));

  parsed_code = parse_program_input("struct P { int x, y; }; "
                                    "union U { int a; long b; }; "
                                    "enum E { E0, E1, E9 = 9 }; "
                                    "static struct P sp = {3, 4}; "
                                    "static union U su = {.b = 0x1122334455L}; "
                                    "static enum E se = E9; "
                                    "static struct P *get_p(void) { return &sp; } "
                                    "static union U *get_u(void) { return &su; } "
                                    "static enum E *get_e(void) { return &se; } "
                                    "static struct P *get_p_arg(int d) { sp.x += d; return &sp; } "
                                    "static struct P make_p(int a, int b) { struct P p = {a, b}; return p; } "
                                    "static struct P arr[3] = {{1, 2}, {3, 4}, {5, 6}}; "
                                    "static struct P *get_arr(void) { return arr; } "
                                    "int main(){ return get_u()->b == 0x1122334455L ? 0 : 1; }");
  (void)parsed_code;
  global_var_t *short_union_su = find_test_global_var("su", 2);
  ASSERT_TRUE(short_union_su != NULL);
  ASSERT_TRUE(ps_gvar_is_union_aggregate(short_union_su));
  psx_gvar_initializer_class_t short_union_cls =
      ps_gvar_initializer_class(short_union_su, 0);
  ASSERT_EQ(PSX_GVAR_INIT_KIND_AGGREGATE, short_union_cls.kind);
  tag_member_info_t short_union_member = {0};
  ASSERT_TRUE(test_tag_union_init_member_for_slot(TK_UNION, "U", 1,
                                                 short_union_su, 0,
                                                 &short_union_member));
  ASSERT_TRUE(short_union_member.name != NULL);
  ASSERT_EQ(0, strncmp(short_union_member.name, "b",
                       (size_t)short_union_member.len));
  ASSERT_EQ(8, test_tag_member_decl_value_size(&short_union_member));

  parsed_code = parse_program_input("unsigned int __tm_gu; int *__tm_gp; int __tm_ga[3]; int main(){ return 0; }");
  (void)parsed_code;
  global_var_t *gu = find_test_global_var("__tm_gu", 7);
  ASSERT_TRUE(gu != NULL);
  ASSERT_TRUE(gu->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, gu->decl_type->kind);
  ASSERT_EQ(4, ps_type_sizeof(gu->decl_type));
  ASSERT_TRUE(ps_type_is_unsigned(gu->decl_type));
  const psx_type_t *gu_decl_a = ps_gvar_get_decl_type(gu);
  ASSERT_TRUE(gu_decl_a != NULL);
  gu->decl_type = NULL;
  ASSERT_TRUE(ps_gvar_get_decl_type(gu) == NULL);
  gu->decl_type = gu_decl_a;

  global_var_t tmp_gv = {0};
  psx_type_t *tmp_gv_int_type = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_id_t tmp_gv_int_type_id = intern_test_type_id(tmp_gv_int_type);
  ASSERT_TRUE(tmp_gv_int_type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_gv, tmp_gv_int_type));
  ASSERT_EQ(tmp_gv_int_type_id, ps_gvar_decl_type_id(&tmp_gv));
  const psx_type_t *tmp_gv_int = ps_gvar_get_decl_type(&tmp_gv);
  ASSERT_TRUE(tmp_gv_int != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, tmp_gv_int->kind);
  ASSERT_EQ(4, ps_gvar_storage_size(&tmp_gv, 99));
  ASSERT_TRUE(!ps_gvar_is_array(&tmp_gv));
  ASSERT_TRUE(!ps_gvar_is_tag_aggregate(&tmp_gv));
  ASSERT_TRUE(!ps_gvar_is_struct_aggregate(&tmp_gv));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(&tmp_gv));
  ASSERT_TRUE(!ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_gv,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0))));
  ASSERT_TRUE(tmp_gv.decl_type == tmp_gv_int);

  global_var_t tmp_ptr_gv = {0};
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_ptr_gv,
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0))));
  const psx_type_t *tmp_gv_ptr = tmp_ptr_gv.decl_type;
  ASSERT_TRUE(tmp_gv_ptr != NULL);
  ASSERT_TRUE(tmp_gv_ptr != tmp_gv_int);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_gv_ptr->kind);

  global_var_t tmp_double_ptr_gv = {0};
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_double_ptr_gv,
      ps_type_new_pointer(
          ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8))));
  const psx_type_t *tmp_gv_double_ptr = tmp_double_ptr_gv.decl_type;
  ASSERT_TRUE(tmp_gv_double_ptr != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_gv_double_ptr->kind);
  ASSERT_TRUE(tmp_gv_double_ptr->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, tmp_gv_double_ptr->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, tmp_gv_double_ptr->base->fp_kind);

  global_var_t tmp_arr_gv = {0};
  psx_type_t *tmp_arr_incomplete = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 0, 0, 0);
  psx_type_id_t tmp_arr_incomplete_id =
      intern_test_type_id(tmp_arr_incomplete);
  ASSERT_TRUE(tmp_arr_incomplete_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_arr_gv, tmp_arr_incomplete));
  ASSERT_EQ(tmp_arr_incomplete_id, ps_gvar_decl_type_id(&tmp_arr_gv));
  ASSERT_EQ(99, ps_gvar_storage_size(&tmp_arr_gv, 99));
  const psx_type_t *tmp_arr_empty = ps_gvar_get_decl_type(&tmp_arr_gv);
  ASSERT_TRUE(tmp_arr_empty != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tmp_arr_empty->kind);
  ASSERT_TRUE(ps_gvar_is_array(&tmp_arr_gv));
  psx_type_t *tmp_arr_complete = ps_type_new_array(
      ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0);
  psx_type_id_t tmp_arr_complete_id = intern_test_type_id(tmp_arr_complete);
  ASSERT_TRUE(tmp_arr_complete_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_global_registry_complete_array_type(
      test_global_registry(), &tmp_arr_gv, tmp_arr_complete));
  ASSERT_EQ(tmp_arr_complete_id, ps_gvar_decl_type_id(&tmp_arr_gv));
  const psx_type_t *tmp_arr_sized = tmp_arr_gv.decl_type;
  ASSERT_TRUE(tmp_arr_sized != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tmp_arr_sized->kind);
  ASSERT_EQ(12, ps_type_sizeof(tmp_arr_sized));
  ASSERT_EQ(4, ps_gvar_array_element_size(&tmp_arr_gv));
  ASSERT_EQ(3, ps_gvar_array_element_count(&tmp_arr_gv));
  ASSERT_EQ(4, ps_gvar_initializer_element_size(&tmp_arr_gv, 12));
  ASSERT_EQ(3, ps_gvar_initializer_element_count(&tmp_arr_gv, 12));
  ASSERT_EQ(12, ps_gvar_initializer_element_size(gu, 12));
  ASSERT_TRUE(!ps_global_registry_complete_array_type(
      test_global_registry(), &tmp_arr_gv, tmp_arr_complete));

  global_var_t tmp_arr_stale_size_gv = {0};
  tmp_arr_stale_size_gv.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 0, 16, 0);
  ASSERT_TRUE(ps_gvar_is_array(&tmp_arr_stale_size_gv));
  ASSERT_EQ(4, ps_gvar_array_element_size(&tmp_arr_stale_size_gv));
  ASSERT_EQ(0, ps_gvar_array_element_count(&tmp_arr_stale_size_gv));
  ASSERT_EQ(4, ps_gvar_initializer_element_size(&tmp_arr_stale_size_gv, 4));
  ASSERT_EQ(0, ps_gvar_initializer_element_count(&tmp_arr_stale_size_gv, 4));

  global_var_t tmp_bool_scalar_decl_type_wins = {0};
  tmp_bool_scalar_decl_type_wins.decl_type =
      ps_type_new_integer(TK_BOOL, 1, 0);
  ASSERT_TRUE(ps_gvar_is_bool_scalar(&tmp_bool_scalar_decl_type_wins));

  global_var_t tmp_int_scalar_decl_type_wins = {0};
  tmp_int_scalar_decl_type_wins.decl_type =
      ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(!ps_gvar_is_bool_scalar(&tmp_int_scalar_decl_type_wins));

  global_var_t tmp_bool_array_decl_type_wins = {0};
  tmp_bool_array_decl_type_wins.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_BOOL, 1, 0), 3, 3, 0);
  ASSERT_TRUE(ps_gvar_array_element_is_bool(&tmp_bool_array_decl_type_wins));

  global_var_t tmp_int_array_decl_type_wins = {0};
  tmp_int_array_decl_type_wins.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 3, 12, 0);
  ASSERT_TRUE(!ps_gvar_array_element_is_bool(&tmp_int_array_decl_type_wins));

  global_var_t tmp_gv_scalar_decl_type_wins = {0};
  psx_type_t *tmp_gv_scalar_canonical =
      ps_type_new_integer(TK_INT, 4, 0);
  tmp_gv_scalar_decl_type_wins.name = "__tm_gv_scalar_decl_type_wins";
  tmp_gv_scalar_decl_type_wins.name_len =
      (int)strlen(tmp_gv_scalar_decl_type_wins.name);
  tmp_gv_scalar_decl_type_wins.decl_type = tmp_gv_scalar_canonical;
  ASSERT_TRUE(ps_gvar_get_decl_type(&tmp_gv_scalar_decl_type_wins) ==
              tmp_gv_scalar_canonical);
  ASSERT_TRUE(!ps_gvar_is_array(&tmp_gv_scalar_decl_type_wins));
  ASSERT_TRUE(!ps_gvar_is_tag_aggregate(&tmp_gv_scalar_decl_type_wins));
  node_t *tmp_gv_scalar_decl_type_ref =
      ps_node_new_gvar_for(&tmp_gv_scalar_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_scalar_decl_type_ref) ==
              tmp_gv_scalar_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_gv_scalar_decl_type_ref));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_gv_scalar_decl_type_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_gv_scalar_decl_type_ref));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(tmp_gv_scalar_decl_type_ref)->kind);
  node_t *tmp_gv_scalar_funcptr_ref =
      ps_node_new_gvar_for(&tmp_gv_scalar_decl_type_wins);
  ASSERT_TRUE(ps_type_derived_function(
      ps_node_get_type(tmp_gv_scalar_funcptr_ref)) == NULL);

  global_var_t tmp_gv_ptr_array_cache_decl_type_wins = {0};
  psx_type_t *tmp_gv_ptr_array_cache_canonical =
      ps_type_new_integer(TK_INT, 4, 0);
  tmp_gv_ptr_array_cache_decl_type_wins.name =
      "__tm_gv_ptr_array_cache_decl_type_wins";
  tmp_gv_ptr_array_cache_decl_type_wins.name_len =
      (int)strlen(tmp_gv_ptr_array_cache_decl_type_wins.name);
  tmp_gv_ptr_array_cache_decl_type_wins.decl_type =
      tmp_gv_ptr_array_cache_canonical;
  ASSERT_TRUE(ps_gvar_get_decl_type(
                  &tmp_gv_ptr_array_cache_decl_type_wins) ==
              tmp_gv_ptr_array_cache_canonical);
  ASSERT_TRUE(!ps_gvar_is_array(&tmp_gv_ptr_array_cache_decl_type_wins));
  node_t *tmp_gv_ptr_array_cache_ref =
      ps_node_new_gvar_for(&tmp_gv_ptr_array_cache_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_ptr_array_cache_ref) ==
              tmp_gv_ptr_array_cache_canonical);
  ASSERT_EQ(4, ps_node_type_size(tmp_gv_ptr_array_cache_ref));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_gv_ptr_array_cache_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_gv_ptr_array_cache_ref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_gv_ptr_array_cache_ref));

  global_var_t tmp_gv_ptr_decl_type_wins = {0};
  tmp_gv_ptr_decl_type_wins.name = "__tm_gv_ptr_decl_type_wins";
  tmp_gv_ptr_decl_type_wins.name_len =
      (int)strlen(tmp_gv_ptr_decl_type_wins.name);
  tmp_gv_ptr_decl_type_wins.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_t *tmp_gv_ptr_decl_type_ref =
      ps_node_new_gvar_for(&tmp_gv_ptr_decl_type_wins);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_ptr_decl_type_ref) ==
              tmp_gv_ptr_decl_type_wins.decl_type);
  ASSERT_TRUE(ps_node_value_is_pointer_like(tmp_gv_ptr_decl_type_ref));
  ASSERT_EQ(4, ps_node_deref_size(tmp_gv_ptr_decl_type_ref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_gv_ptr_decl_type_ref));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   tmp_gv_ptr_decl_type_ref, 0));
  node_t *tmp_gv_ptr_decl_type_deref =
      ps_node_new_unary_deref_for(tmp_gv_ptr_decl_type_ref);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_ptr_decl_type_deref) ==
              tmp_gv_ptr_decl_type_wins.decl_type->base);
  ASSERT_TRUE(!ps_node_value_is_pointer_like(tmp_gv_ptr_decl_type_deref));
  ASSERT_EQ(4, ps_node_type_size(tmp_gv_ptr_decl_type_deref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_gv_ptr_decl_type_deref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_gv_ptr_decl_type_deref));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   tmp_gv_ptr_decl_type_deref, 0));

  global_var_t tmp_gv_flat_ptr_mismatch_decl_type = {0};
  tmp_gv_flat_ptr_mismatch_decl_type.name =
      "__tm_gv_flat_ptr_mismatch_decl_type";
  tmp_gv_flat_ptr_mismatch_decl_type.name_len =
      (int)strlen(tmp_gv_flat_ptr_mismatch_decl_type.name);
  tmp_gv_flat_ptr_mismatch_decl_type.decl_type =
      ps_type_new_pointer(NULL);
  node_t *tmp_gv_flat_ptr_mismatch_ref =
      ps_node_new_gvar_for(&tmp_gv_flat_ptr_mismatch_decl_type);
  ASSERT_TRUE(ps_node_get_type(tmp_gv_flat_ptr_mismatch_ref) ==
              tmp_gv_flat_ptr_mismatch_decl_type.decl_type);
  ASSERT_TRUE(ps_node_value_is_pointer_like(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, ps_node_deref_size(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, canonical_node_pointer_qual_levels(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, canonical_node_base_deref_size(tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   tmp_gv_flat_ptr_mismatch_ref));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   tmp_gv_flat_ptr_mismatch_ref, 0));

  global_var_t tmp_gv_cached_runtime_shape = {0};
  tmp_gv_cached_runtime_shape.name = "__tm_gv_cached_runtime_shape";
  tmp_gv_cached_runtime_shape.name_len =
      (int)strlen(tmp_gv_cached_runtime_shape.name);
  tmp_gv_cached_runtime_shape.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_gvar_get_decl_type(&tmp_gv_cached_runtime_shape) ==
              tmp_gv_cached_runtime_shape.decl_type);

  global_var_t tmp_gv_cached_stale_ptr_array_shape = {0};
  tmp_gv_cached_stale_ptr_array_shape.name =
      "__tm_gv_cached_stale_ptr_array_shape";
  tmp_gv_cached_stale_ptr_array_shape.name_len =
      (int)strlen(tmp_gv_cached_stale_ptr_array_shape.name);
  tmp_gv_cached_stale_ptr_array_shape.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_gvar_get_decl_type(
                  &tmp_gv_cached_stale_ptr_array_shape) ==
              tmp_gv_cached_stale_ptr_array_shape.decl_type);

  global_var_t tmp_gv_cached_stale_array_shape = {0};
  tmp_gv_cached_stale_array_shape.name =
      "__tm_gv_cached_stale_array_shape";
  tmp_gv_cached_stale_array_shape.name_len =
      (int)strlen(tmp_gv_cached_stale_array_shape.name);
  psx_type_t *tmp_gv_stale_array_d2 =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 0);
  psx_type_t *tmp_gv_stale_array_d1 =
      ps_type_new_array(tmp_gv_stale_array_d2, 3, 48, 0);
  tmp_gv_cached_stale_array_shape.decl_type =
      ps_type_new_array(tmp_gv_stale_array_d1, 2, 96, 0);
  ASSERT_TRUE(ps_gvar_get_decl_type(
                  &tmp_gv_cached_stale_array_shape) ==
              tmp_gv_cached_stale_array_shape.decl_type);
  node_t *tmp_gv_stale_array_addr =
      ps_node_new_gvar_array_addr_for(&tmp_gv_cached_stale_array_shape);
  ASSERT_TRUE(ps_node_value_is_pointer_like(tmp_gv_stale_array_addr));
  const psx_type_t *tmp_gv_stale_array_addr_type =
      ps_node_get_type(tmp_gv_stale_array_addr);
  ASSERT_TRUE(tmp_gv_stale_array_addr_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tmp_gv_stale_array_addr_type->kind);
  ASSERT_EQ(8, ps_node_type_size(tmp_gv_stale_array_addr));
  ASSERT_EQ(48, ps_node_deref_size(tmp_gv_stale_array_addr));
  int tmp_gv_stale_array_inner =
      canonical_node_array_subscript_stride_bytes(
          tmp_gv_stale_array_addr, 0);
  int tmp_gv_stale_array_next =
      canonical_node_array_subscript_stride_bytes(
          tmp_gv_stale_array_addr, 1);
  ASSERT_EQ(16, tmp_gv_stale_array_inner);
  ASSERT_EQ(4, tmp_gv_stale_array_next);

  global_var_t *gp = find_test_global_var("__tm_gp", 7);
  ASSERT_TRUE(gp != NULL);
  ASSERT_TRUE(gp->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gp->decl_type->kind);
  ASSERT_TRUE(ps_type_is_pointer(gp->decl_type));
  global_var_t *ga = find_test_global_var("__tm_ga", 7);
  ASSERT_TRUE(ga != NULL);
  ASSERT_TRUE(ga->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ga->decl_type->kind);
  ASSERT_EQ(12, ps_type_sizeof(ga->decl_type));
  ASSERT_TRUE(ps_gvar_is_array(ga));
  ASSERT_EQ(4, ps_gvar_array_element_size(ga));
  ASSERT_EQ(3, ps_gvar_array_element_count(ga));

  parsed_code = parse_program_input("static short __tm_sha[2][2] = {{10,20},{30,40}}; "
                                    "int main(void){ return __tm_sha[1][0]; }");
  (void)parsed_code;
  global_var_t *sha = find_test_global_var("__tm_sha", 8);
  ASSERT_TRUE(sha != NULL);
  ASSERT_TRUE(ps_gvar_is_array(sha));
  ASSERT_EQ(4, ps_gvar_array_element_size(sha));
  ASSERT_EQ(2, ps_gvar_array_element_count(sha));
  ASSERT_EQ(2, ps_gvar_initializer_element_size(sha, 4));
  ASSERT_EQ(4, ps_gvar_initializer_element_count(sha, 4));

  parsed_code = parse_program_input(
      "struct __tm817_S { char tag[3]; int n; }; "
      "struct __tm817_S __tm817_gs[2] = {{{1,2,3},4},{{5,6,7},8}}; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *tm817_gs = find_test_global_var("__tm817_gs", 10);
  ASSERT_TRUE(tm817_gs != NULL);
  ASSERT_TRUE(ps_gvar_is_tag_aggregate(tm817_gs));
  ASSERT_TRUE(ps_gvar_is_struct_aggregate(tm817_gs));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(tm817_gs));
  ASSERT_EQ(0, ps_gvar_array_element_size(tm817_gs));
  ASSERT_EQ(8, ps_ctx_type_sizeof_in(
                   test_semantic_context(),
                   ps_gvar_get_decl_type(tm817_gs)->base));
  ASSERT_EQ(2, ps_gvar_array_element_count(tm817_gs));
  global_var_t tmp_tag_arr_gv = {0};
  const char tmp_tag_arr_name[] = "__tm_tmp_tag_arr";
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_tag_arr_gv, ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)tmp_tag_arr_name,
                       (int)sizeof(tmp_tag_arr_name) - 1, 0, 8),
      2, 16, 0)));
  ASSERT_TRUE(ps_gvar_is_tag_aggregate(&tmp_tag_arr_gv));
  ASSERT_TRUE(ps_gvar_is_struct_aggregate(&tmp_tag_arr_gv));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(&tmp_tag_arr_gv));
  ASSERT_EQ(0, ps_gvar_array_element_size(&tmp_tag_arr_gv));
  ASSERT_EQ(2, ps_gvar_array_element_count(&tmp_tag_arr_gv));
  ASSERT_TRUE(!ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_tag_arr_gv,
      ps_type_new_array(
          ps_type_new_tag(TK_UNION, (char *)tmp_tag_arr_name,
                           (int)sizeof(tmp_tag_arr_name) - 1, 0, 8),
          2, 16, 0)));
  ASSERT_TRUE(ps_gvar_is_struct_aggregate(&tmp_tag_arr_gv));

  global_var_t tmp_union_arr_gv = {0};
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_union_arr_gv,
      ps_type_new_array(
          ps_type_new_tag(TK_UNION, (char *)tmp_tag_arr_name,
                           (int)sizeof(tmp_tag_arr_name) - 1, 0, 8),
          2, 16, 0)));
  ASSERT_TRUE(ps_gvar_is_tag_aggregate(&tmp_union_arr_gv));
  ASSERT_TRUE(!ps_gvar_is_struct_aggregate(&tmp_union_arr_gv));
  ASSERT_TRUE(ps_gvar_is_union_aggregate(&tmp_union_arr_gv));

  global_var_t tmp_union_ptr_gv = {0};
  ASSERT_TRUE(ps_global_registry_bind_decl_type(
      test_global_registry(), &tmp_union_ptr_gv,
      ps_type_new_pointer(
          ps_type_new_tag(TK_UNION, (char *)tmp_tag_arr_name,
                           (int)sizeof(tmp_tag_arr_name) - 1, 0, 8))));
  ASSERT_TRUE(!ps_gvar_is_tag_aggregate(&tmp_union_ptr_gv));
  ASSERT_TRUE(!ps_gvar_is_union_aggregate(&tmp_union_ptr_gv));

  tag_member_info_t tmp_member = {0};
  psx_type_t *tmp_member_tag_type =
      ps_type_new_tag(TK_STRUCT, NULL, 0, 0, 8);
  tmp_member.decl_type = tmp_member_tag_type;
  ASSERT_TRUE(ps_tag_member_is_tag_aggregate(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_struct_aggregate(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_union_aggregate(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_unnamed_struct(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_union(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_unnamed_aggregate(&tmp_member));
  tmp_member.len = 1;
  ASSERT_TRUE(!ps_tag_member_is_unnamed_struct(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_aggregate(&tmp_member));
  tmp_member.len = 0;
  tmp_member.decl_type = ps_type_new_tag(TK_UNION, NULL, 0, 0, 8);
  ASSERT_TRUE(ps_tag_member_is_union_aggregate(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_unnamed_union(&tmp_member));
  ASSERT_TRUE(ps_tag_member_is_unnamed_aggregate(&tmp_member));
  tmp_member.decl_type = ps_type_new_pointer(tmp_member_tag_type);
  ASSERT_TRUE(!ps_tag_member_is_tag_aggregate(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_union(&tmp_member));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_aggregate(&tmp_member));
  tag_member_info_t tmp_member_decl_array = {0};
  tmp_member_decl_array.decl_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, "__tm_member_decl_tag", 20, 0, 8),
      2, 16, 0);
  ASSERT_TRUE(ps_tag_member_is_tag_aggregate(&tmp_member_decl_array));
  ASSERT_TRUE(ps_tag_member_is_struct_aggregate(&tmp_member_decl_array));
  ASSERT_TRUE(!ps_tag_member_is_union_aggregate(&tmp_member_decl_array));
  ASSERT_TRUE(ps_tag_member_is_unnamed_struct(&tmp_member_decl_array));
  tag_member_info_t tmp_member_decl_ptr = {0};
  tmp_member_decl_ptr.decl_type = ps_type_new_pointer(
      ps_type_new_tag(TK_STRUCT, "__tm_member_decl_ptr_tag", 24, 0, 8));
  ASSERT_TRUE(!ps_tag_member_is_tag_aggregate(&tmp_member_decl_ptr));
  ASSERT_TRUE(!ps_tag_member_is_unnamed_aggregate(&tmp_member_decl_ptr));
  const char flat_decl_inner_tag[] = "__tm_flat_decl_inner";
  test_semantic_define_tag_type_with_layout(TK_STRUCT, (char *)flat_decl_inner_tag,
                                      (int)sizeof(flat_decl_inner_tag) - 1,
                                      2, 8, 4);
  tag_member_info_t flat_decl_inner_a = {0};
  flat_decl_inner_a.name = "a";
  flat_decl_inner_a.len = 1;
  flat_decl_inner_a.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)flat_decl_inner_tag,
      (int)sizeof(flat_decl_inner_tag) - 1, &flat_decl_inner_a));
  tag_member_info_t flat_decl_inner_b = {0};
  flat_decl_inner_b.name = "b";
  flat_decl_inner_b.len = 1;
  flat_decl_inner_b.offset = 4;
  flat_decl_inner_b.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)flat_decl_inner_tag,
      (int)sizeof(flat_decl_inner_tag) - 1, &flat_decl_inner_b));
  tag_member_info_t flat_decl_array_member = {0};
  flat_decl_array_member.name = "arr";
  flat_decl_array_member.len = 3;
  psx_type_t *flat_decl_array_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)flat_decl_inner_tag,
                       (int)sizeof(flat_decl_inner_tag) - 1, 0, 8),
      3, 24, 0);
  ps_ctx_bind_record_ids_in(
      test_semantic_context(), flat_decl_array_type);
  flat_decl_array_member.decl_type = flat_decl_array_type;
  ASSERT_EQ(24, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    flat_decl_array_member.decl_type));
  ASSERT_EQ(8, ps_ctx_type_sizeof_in(
                   test_semantic_context(),
                   flat_decl_array_member.decl_type->base));
  ASSERT_EQ(2, test_tag_flat_slot_count(TK_STRUCT, (char *)flat_decl_inner_tag,
                                       (int)sizeof(flat_decl_inner_tag) - 1));
  ASSERT_EQ(6, test_tag_member_flat_slots(&flat_decl_array_member));
  ASSERT_EQ(2, test_tag_member_elem_flat_slots(&flat_decl_array_member));
  const char flat_decl_union_tag[] = "__tm_flat_decl_union";
  test_semantic_define_tag_type_with_layout(TK_UNION, (char *)flat_decl_union_tag,
                                      (int)sizeof(flat_decl_union_tag) - 1,
                                      2, 24, 8);
  tag_member_info_t flat_decl_union_large = {0};
  flat_decl_union_large.name = "large";
  flat_decl_union_large.len = 5;
  flat_decl_union_large.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_CHAR, 1, 0),
                         20, 20, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_UNION, (char *)flat_decl_union_tag,
      (int)sizeof(flat_decl_union_tag) - 1, &flat_decl_union_large));
  ASSERT_TRUE(register_test_tag_member(
      TK_UNION, (char *)flat_decl_union_tag,
      (int)sizeof(flat_decl_union_tag) - 1, &flat_decl_array_member));
  ASSERT_EQ(6, test_tag_flat_slot_count(TK_UNION, (char *)flat_decl_union_tag,
                                       (int)sizeof(flat_decl_union_tag) - 1));

  parsed_code = parse_program_input(
      "struct __tm_bf_decl_type { unsigned long wide:40; _Bool flag:1; }; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  tag_member_info_t bf_wide_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "__tm_bf_decl_type", 17,
                                           "wide", 4, &bf_wide_info));
  ASSERT_TRUE(bf_wide_info.decl_type != NULL);
  ASSERT_EQ(40, bf_wide_info.bit_width);
  ASSERT_EQ(8, test_tag_member_decl_value_size(&bf_wide_info));
  ASSERT_EQ(8, test_tag_member_decl_storage_size(&bf_wide_info));
  tag_member_info_t bf_bool_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "__tm_bf_decl_type", 17,
                                           "flag", 4, &bf_bool_info));
  ASSERT_TRUE(bf_bool_info.decl_type != NULL);
  ASSERT_EQ(1, bf_bool_info.bit_width);
  ASSERT_TRUE(ps_tag_member_decl_is_bool(&bf_bool_info));
  ASSERT_EQ(1, test_tag_member_decl_value_size(&bf_bool_info));

  const char member_sync_tag[] = "__tm_member_sync_clean";
  int member_sync_len = (int)sizeof(member_sync_tag) - 1;
  test_semantic_define_tag_type_with_layout(TK_STRUCT, (char *)member_sync_tag,
                                      member_sync_len, 1, 4, 4);
  tag_member_info_t member_sync_stale = {0};
  member_sync_stale.name = "x";
  member_sync_stale.len = 1;
  member_sync_stale.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)member_sync_tag, member_sync_len,
      &member_sync_stale));
  tag_member_info_t member_sync_out = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, (char *)member_sync_tag,
                                           member_sync_len, "x", 1,
                                           &member_sync_out));
  ASSERT_TRUE(member_sync_out.decl_type != NULL);
  ASSERT_TRUE(ps_tag_member_decl_tag_type(&member_sync_out) == NULL);
  ASSERT_EQ(0, canonical_pointer_qual_levels(
                   ps_tag_member_decl_type(&member_sync_out)));
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_tag_member_decl_fp_kind(&member_sync_out));
  ASSERT_EQ(0, ps_tag_member_decl_is_bool(&member_sync_out));
  ASSERT_EQ(0, ps_tag_member_decl_is_unsigned(&member_sync_out));

  psx_type_t *recursive_member_tag =
      ps_type_new_tag(TK_STRUCT, "RecursiveMember", 15, 0, 4);
  tag_member_info_t recursive_member = {
      .decl_type = ps_type_new_pointer(ps_type_new_pointer(
          ps_type_new_array(recursive_member_tag, 2, 8, 0))),
  };
  ASSERT_TRUE(ps_tag_member_decl_tag_type(&recursive_member) ==
              recursive_member_tag);
  ASSERT_EQ(4, test_tag_member_decl_value_size(&member_sync_out));

  const char canonical_member_tag[] = "__tm_canonical_member_desc";
  int canonical_member_tag_len = (int)sizeof(canonical_member_tag) - 1;
  test_semantic_define_tag_type_with_layout(TK_STRUCT, (char *)canonical_member_tag,
                                      canonical_member_tag_len, 1, 8, 8);
  tag_member_info_t canonical_member_desc = {0};
  canonical_member_desc.name = "p";
  canonical_member_desc.len = 1;
  canonical_member_desc.decl_type = ps_type_new_pointer(
      ps_type_new_integer(TK_INT, 4, 1));
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)canonical_member_tag, canonical_member_tag_len,
      &canonical_member_desc));
  tag_member_info_t canonical_member_out = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)canonical_member_tag, canonical_member_tag_len,
      "p", 1, &canonical_member_out));
  ASSERT_TRUE(canonical_member_out.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, canonical_member_out.decl_type->kind);
  ASSERT_TRUE(canonical_member_out.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, canonical_member_out.decl_type->base->kind);
  ASSERT_TRUE(canonical_member_out.decl_type->base->is_unsigned);

  const char walk_inner_tag[] = "__tm_walk_inner";
  int walk_inner_len = (int)sizeof(walk_inner_tag) - 1;
  test_semantic_define_tag_type_with_layout(TK_STRUCT, (char *)walk_inner_tag,
                                      walk_inner_len, 2, 8, 4);
  tag_member_info_t walk_inner_a = {0};
  walk_inner_a.name = "a";
  walk_inner_a.len = 1;
  walk_inner_a.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)walk_inner_tag, walk_inner_len, &walk_inner_a));
  tag_member_info_t walk_inner_b = {0};
  walk_inner_b.name = "b";
  walk_inner_b.len = 1;
  walk_inner_b.offset = 4;
  walk_inner_b.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)walk_inner_tag, walk_inner_len, &walk_inner_b));

  const char walk_outer_tag[] = "__tm_walk_outer";
  int walk_outer_len = (int)sizeof(walk_outer_tag) - 1;
  test_semantic_define_tag_type_with_layout(TK_STRUCT, (char *)walk_outer_tag,
                                      walk_outer_len, 2, 20, 4);
  tag_member_info_t walk_outer_arr = {0};
  walk_outer_arr.name = "arr";
  walk_outer_arr.len = 3;
  walk_outer_arr.decl_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)walk_inner_tag, walk_inner_len, 0, 8),
      2, 16, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)walk_outer_tag, walk_outer_len, &walk_outer_arr));
  tag_member_info_t walk_outer_tail = {0};
  walk_outer_tail.name = "tail";
  walk_outer_tail.len = 4;
  walk_outer_tail.offset = 16;
  walk_outer_tail.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)walk_outer_tag, walk_outer_len, &walk_outer_tail));

  global_var_t walk_gv = {0};
  walk_gv.decl_type = ps_ctx_clone_tag_type_at_in_contexts(
      test_semantic_context(), test_local_registry(),
      TK_STRUCT, (char *)walk_outer_tag, walk_outer_len,
      ps_local_registry_capture_lookup_point_in(test_local_registry()));
  ASSERT_TRUE(walk_gv.decl_type != NULL);
  walk_gv.decl_type_id = intern_test_type_id(walk_gv.decl_type);
  ASSERT_TRUE(walk_gv.decl_type_id != PSX_TYPE_ID_INVALID);
  psx_record_decl_t *walk_outer_record =
      test_record_decl_mut(walk_gv.decl_type);
  ASSERT_TRUE(walk_outer_record != NULL);
  ((tag_member_info_t *)walk_outer_record->members)[1].offset = 77;
  psx_type_t *walk_inner_type = (psx_type_t *)
      ps_tag_member_decl_type(&walk_outer_record->members[0])->base;
  ASSERT_TRUE(walk_inner_type != NULL);
  psx_record_decl_t *walk_inner_record =
      test_record_decl_mut(walk_inner_type);
  ASSERT_TRUE(walk_inner_record != NULL);
  ((tag_member_info_t *)walk_inner_record->members)[1].offset = 55;
  ps_gvar_init_slots_alloc(&walk_gv, 5, 0);
  walk_gv.init_count = 5;
  for (int i = 0; i < 5; i++)
    ps_gvar_init_slot_write(&walk_gv, i, i + 1, 0.0, NULL, 0);
  aggregate_walk_trace_t walk_trace = {.gv = &walk_gv};
  const psx_gvar_aggregate_walk_ops_t walk_ops = {
      .scalar = aggregate_walk_trace_scalar,
      .padding = aggregate_walk_trace_padding,
  };
  ASSERT_TRUE(test_gvar_walk_aggregate_initializer(&walk_gv, 0, &walk_ops,
                                                  &walk_trace));
  ASSERT_EQ(5, walk_trace.scalar_count);
  ASSERT_EQ(0, walk_trace.scalar_offsets[0]);
  ASSERT_EQ(4, walk_trace.scalar_offsets[1]);
  ASSERT_EQ(8, walk_trace.scalar_offsets[2]);
  ASSERT_EQ(12, walk_trace.scalar_offsets[3]);
  ASSERT_EQ(16, walk_trace.scalar_offsets[4]);
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(i + 1, walk_trace.scalar_values[i]);
    ASSERT_EQ(4, walk_trace.scalar_sizes[i]);
    ASSERT_TRUE(walk_trace.scalar_type_ids[i] != PSX_TYPE_ID_INVALID);
  }
  ASSERT_EQ(0, walk_trace.padding_count);

  const char walk_array_tag[] = "__tm_walk_array_elem";
  int walk_array_len = (int)sizeof(walk_array_tag) - 1;
  test_semantic_define_tag_type_with_layout(TK_STRUCT, (char *)walk_array_tag,
                                      walk_array_len, 2, 8, 4);
  tag_member_info_t walk_array_a = {0};
  walk_array_a.name = "a";
  walk_array_a.len = 1;
  walk_array_a.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)walk_array_tag, walk_array_len, &walk_array_a));
  tag_member_info_t walk_array_b = {0};
  walk_array_b.name = "b";
  walk_array_b.len = 1;
  walk_array_b.offset = 4;
  walk_array_b.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)walk_array_tag, walk_array_len, &walk_array_b));

  global_var_t walk_array_gv = {0};
  psx_type_t *walk_array_element = ps_ctx_clone_tag_type_at_in_contexts(
      test_semantic_context(), test_local_registry(),
      TK_STRUCT, (char *)walk_array_tag, walk_array_len,
      ps_local_registry_capture_lookup_point_in(test_local_registry()));
  ASSERT_TRUE(walk_array_element != NULL);
  walk_array_gv.decl_type = ps_type_new_array(
      walk_array_element, 2, 16, 0);
  walk_array_gv.decl_type_id = intern_test_type_id(
      walk_array_gv.decl_type);
  ASSERT_TRUE(walk_array_gv.decl_type_id != PSX_TYPE_ID_INVALID);
  psx_record_decl_t *walk_array_record =
      test_record_decl_mut(walk_array_element);
  ASSERT_TRUE(walk_array_record != NULL);
  ((tag_member_info_t *)walk_array_record->members)[1].offset = 66;
  ps_gvar_init_slots_alloc(&walk_array_gv, 4, 0);
  walk_array_gv.init_count = 4;
  for (int i = 0; i < 4; i++)
    ps_gvar_init_slot_write(&walk_array_gv, i, i + 11, 0.0, NULL, 0);
  aggregate_walk_trace_t walk_array_trace = {.gv = &walk_array_gv};
  ASSERT_TRUE(test_gvar_walk_aggregate_initializer(&walk_array_gv, 0, &walk_ops,
                                                  &walk_array_trace));
  ASSERT_EQ(4, walk_array_trace.scalar_count);
  ASSERT_EQ(0, walk_array_trace.scalar_offsets[0]);
  ASSERT_EQ(4, walk_array_trace.scalar_offsets[1]);
  ASSERT_EQ(8, walk_array_trace.scalar_offsets[2]);
  ASSERT_EQ(12, walk_array_trace.scalar_offsets[3]);
  for (int i = 0; i < 4; i++) {
    ASSERT_EQ(i + 11, walk_array_trace.scalar_values[i]);
    ASSERT_EQ(4, walk_array_trace.scalar_sizes[i]);
    ASSERT_TRUE(
        walk_array_trace.scalar_type_ids[i] != PSX_TYPE_ID_INVALID);
  }
  ASSERT_EQ(0, walk_array_trace.padding_count);

  parsed_code = parse_program_input(
      "struct __tm_walk_anon { "
      "  struct { union { int a; int b; }; }; int tail; "
      "}; "
      "struct __tm_walk_anon __tm_walk_anon_g = {1, 2}; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  const char walk_anon_global_name[] = "__tm_walk_anon_g";
  global_var_t *walk_anon_gv = find_test_global_var(
      (char *)walk_anon_global_name,
      (int)sizeof(walk_anon_global_name) - 1);
  ASSERT_TRUE(walk_anon_gv != NULL);
  ASSERT_TRUE(walk_anon_gv->decl_type != NULL);
  psx_record_decl_t *walk_anon_outer_record =
      test_record_decl_mut(walk_anon_gv->decl_type);
  ASSERT_TRUE(walk_anon_outer_record != NULL);
  ASSERT_TRUE(walk_anon_outer_record->member_count >= 2);
  const psx_type_t *walk_anon_inner_type = ps_tag_member_decl_tag_type(
      &walk_anon_outer_record->members[0]);
  ASSERT_TRUE(walk_anon_inner_type != NULL);
  psx_record_decl_t *walk_anon_inner_record =
      test_record_decl_mut(walk_anon_inner_type);
  ASSERT_TRUE(walk_anon_inner_record != NULL);
  ASSERT_TRUE(walk_anon_inner_record->member_count >= 1);
  for (int i = 0; i < walk_anon_outer_record->member_count; i++)
    ((tag_member_info_t *)walk_anon_outer_record->members)[i].offset = 80 + i;
  for (int i = 0; i < walk_anon_inner_record->member_count; i++)
    ((tag_member_info_t *)walk_anon_inner_record->members)[i].offset = 60 + i;
  aggregate_walk_trace_t walk_anon_trace = {.gv = walk_anon_gv};
  ASSERT_TRUE(test_gvar_walk_aggregate_initializer(
      walk_anon_gv, 0, &walk_ops, &walk_anon_trace));
  ASSERT_EQ(2, walk_anon_trace.scalar_count);
  ASSERT_EQ(0, walk_anon_trace.scalar_offsets[0]);
  ASSERT_EQ(4, walk_anon_trace.scalar_offsets[1]);
  ASSERT_EQ(1, walk_anon_trace.scalar_values[0]);
  ASSERT_EQ(2, walk_anon_trace.scalar_values[1]);
  ASSERT_EQ(0, walk_anon_trace.padding_count);

  parsed_code = parse_program_input("extern int __tm_extern_arr[]; int __tm_extern_arr[3]; int main(){ return 0; }");
  (void)parsed_code;
  global_var_t *gext = find_test_global_var("__tm_extern_arr", 15);
  ASSERT_TRUE(gext != NULL);
  ASSERT_TRUE(gext->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gext->decl_type->kind);
  ASSERT_EQ(12, ps_type_sizeof(gext->decl_type));

  parsed_code = parse_program_input("int __tm_static_decl_type(void) { static double sd = 1.0; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *sd = find_func_lvar(fn, "sd");
  ASSERT_TRUE(sd != NULL);
  ASSERT_TRUE(sd->is_static_local);
  ASSERT_TRUE(sd->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, sd->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, sd->decl_type->fp_kind);
  global_var_t *sd_gv = find_test_global_var(sd->static_global_name, sd->static_global_name_len);
  ASSERT_TRUE(sd_gv != NULL);
  ASSERT_TRUE(sd_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, sd_gv->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, sd_gv->decl_type->fp_kind);
  node_t *sd_node = psx_node_new_static_local_gvar_for(sd);
  ASSERT_TRUE(sd->static_global == sd_gv);
  ASSERT_TRUE(((node_gvar_t *)sd_node)->symbol == sd_gv);
  ASSERT_TRUE(ps_node_get_type(sd_node) == sd_gv->decl_type);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_get_type(sd_node)->fp_kind);

  parsed_code = parse_program_input(
      "int __tm_static_scalar_flags(void) { "
      "static _Bool sb = 1; static unsigned int su = 2; return sb + su; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *static_scalar_sb = find_func_lvar(fn, "sb");
  ASSERT_TRUE(static_scalar_sb != NULL);
  ASSERT_TRUE(static_scalar_sb->is_static_local);
  global_var_t *static_scalar_sb_gv = find_test_global_var(
      static_scalar_sb->static_global_name, static_scalar_sb->static_global_name_len);
  ASSERT_TRUE(static_scalar_sb_gv != NULL);
  ASSERT_TRUE(static_scalar_sb_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, static_scalar_sb_gv->decl_type->kind);
  node_t *static_scalar_sb_node =
      psx_node_new_static_local_gvar_for(static_scalar_sb);
  ASSERT_TRUE(ps_node_get_type(static_scalar_sb_node) == static_scalar_sb_gv->decl_type);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(static_scalar_sb_node)->kind);

  lvar_t *static_scalar_su = find_func_lvar(fn, "su");
  ASSERT_TRUE(static_scalar_su != NULL);
  ASSERT_TRUE(static_scalar_su->is_static_local);
  global_var_t *static_scalar_su_gv = find_test_global_var(
      static_scalar_su->static_global_name, static_scalar_su->static_global_name_len);
  ASSERT_TRUE(static_scalar_su_gv != NULL);
  ASSERT_TRUE(static_scalar_su_gv->decl_type != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(static_scalar_su_gv->decl_type));
  node_t *static_scalar_su_node =
      psx_node_new_static_local_gvar_for(static_scalar_su);
  ASSERT_TRUE(ps_node_get_type(static_scalar_su_node) == static_scalar_su_gv->decl_type);
  ASSERT_TRUE(ps_node_is_unsigned_type(static_scalar_su_node));

  parsed_code = parse_program_input("int __tm_static_arr_decl_type(void) { static int sa[2] = {1,2}; return sa[0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *sa = find_func_lvar(fn, "sa");
  ASSERT_TRUE(sa != NULL);
  ASSERT_TRUE(sa->is_static_local);
  ASSERT_TRUE(sa->decl_type != NULL);
  global_var_t *sa_gv = find_test_global_var(sa->static_global_name, sa->static_global_name_len);
  ASSERT_TRUE(sa_gv != NULL);
  ASSERT_TRUE(sa_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, sa_gv->decl_type->kind);
  ASSERT_EQ(8, ps_type_sizeof(sa_gv->decl_type));
  node_t *sa_node = psx_node_new_static_local_gvar_for(sa);
  ASSERT_TRUE(ps_node_get_type(sa_node) == sa_gv->decl_type);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(sa_node)->kind);
  node_t *sa_addr = psx_node_new_static_local_array_addr_for(sa);
  const psx_type_t *sa_addr_type = ps_node_get_type(sa_addr);
  ASSERT_TRUE(sa_addr_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, sa_addr_type->kind);
  ASSERT_TRUE(sa_addr_type->base == sa_gv->decl_type->base);

  parsed_code = parse_program_input(
      "int __tm_static_inferred_arr(void) { "
      "static int inferred[] = {1,2,3}; return inferred[2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *static_inferred = find_func_lvar(fn, "inferred");
  ASSERT_TRUE(static_inferred != NULL);
  global_var_t *static_inferred_gv = find_test_global_var(
      static_inferred->static_global_name,
      static_inferred->static_global_name_len);
  ASSERT_TRUE(static_inferred_gv != NULL);
  ASSERT_EQ(3, ps_lvar_get_decl_type(static_inferred)->array_len);
  ASSERT_EQ(3, ps_gvar_get_decl_type(static_inferred_gv)->array_len);
  ASSERT_EQ(12, ps_type_sizeof(ps_lvar_get_decl_type(static_inferred)));
  ASSERT_EQ(12, ps_type_sizeof(ps_gvar_get_decl_type(static_inferred_gv)));

  parsed_code = parse_program_input(
      "int __tm_static_2d_arr_decl_type(void) { "
      "static int sm[2][3] = {{1,2,3},{4,5,6}}; return sm[1][2]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *sm = find_func_lvar(fn, "sm");
  ASSERT_TRUE(sm != NULL);
  ASSERT_TRUE(sm->is_static_local);
  global_var_t *sm_gv = find_test_global_var(sm->static_global_name, sm->static_global_name_len);
  ASSERT_TRUE(sm_gv != NULL);
  ASSERT_TRUE(sm_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, sm_gv->decl_type->kind);
  node_t *sm_addr = psx_node_new_static_local_array_addr_for(sm);
  const psx_type_t *sm_addr_type = ps_node_get_type(sm_addr);
  ASSERT_TRUE(sm_addr_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, sm_addr_type->kind);
  ASSERT_TRUE(sm_addr_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, sm_addr_type->base->kind);
  ASSERT_EQ(3, sm_addr_type->base->array_len);

  parsed_code = parse_program_input(
      "int __tm_static_array_addr_flags(void) { "
      "static unsigned char su[2] = {1,2}; static _Bool sb[2] = {0,1}; "
      "static int si[2] = {3,4}; return su[0] + sb[1] + si[0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *static_su = find_func_lvar(fn, "su");
  ASSERT_TRUE(static_su != NULL);
  ASSERT_TRUE(static_su->is_static_local);
  global_var_t *static_su_gv = find_test_global_var(
      static_su->static_global_name, static_su->static_global_name_len);
  ASSERT_TRUE(static_su_gv != NULL);
  ASSERT_TRUE(static_su_gv->decl_type != NULL);
  node_t *static_su_addr =
      psx_node_new_static_local_array_addr_for(static_su);
  ASSERT_TRUE(ps_node_get_type(static_su_addr) != NULL);
  ASSERT_TRUE(ps_node_get_type(static_su_addr)->base ==
              static_su_gv->decl_type->base);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(static_su_addr));

  lvar_t *static_sb = find_func_lvar(fn, "sb");
  ASSERT_TRUE(static_sb != NULL);
  ASSERT_TRUE(static_sb->is_static_local);
  global_var_t *static_sb_gv = find_test_global_var(
      static_sb->static_global_name, static_sb->static_global_name_len);
  ASSERT_TRUE(static_sb_gv != NULL);
  ASSERT_TRUE(static_sb_gv->decl_type != NULL);
  node_t *static_sb_addr =
      psx_node_new_static_local_array_addr_for(static_sb);
  ASSERT_TRUE(ps_node_get_type(static_sb_addr) != NULL);
  ASSERT_TRUE(ps_node_get_type(static_sb_addr)->base ==
              static_sb_gv->decl_type->base);
  ASSERT_TRUE(canonical_node_pointee_is_bool(static_sb_addr));

  lvar_t *static_si = find_func_lvar(fn, "si");
  ASSERT_TRUE(static_si != NULL);
  ASSERT_TRUE(static_si->is_static_local);
  global_var_t *static_si_gv = find_test_global_var(
      static_si->static_global_name, static_si->static_global_name_len);
  ASSERT_TRUE(static_si_gv != NULL);
  ASSERT_TRUE(static_si_gv->decl_type != NULL);
  node_t *static_si_addr =
      psx_node_new_static_local_array_addr_for(static_si);
  ASSERT_TRUE(ps_node_get_type(static_si_addr) != NULL);
  ASSERT_TRUE(ps_node_get_type(static_si_addr)->base ==
              static_si_gv->decl_type->base);
  ASSERT_EQ(TK_FLOAT_KIND_NONE, canonical_node_pointee_fp_kind(static_si_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(static_si_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(static_si_addr));
  ASSERT_EQ(4, ps_node_deref_size(static_si_addr));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(static_si_addr));

  parsed_code = parse_program_input(
      "int __tm_static_pointer_pointee_flags(void) { "
      "static _Bool *bp = 0; static unsigned int *up = 0; "
      "return bp == 0 && up == 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *static_bp = find_func_lvar(fn, "bp");
  ASSERT_TRUE(static_bp != NULL);
  ASSERT_TRUE(static_bp->is_static_local);
  global_var_t *static_bp_gv = find_test_global_var(
      static_bp->static_global_name, static_bp->static_global_name_len);
  ASSERT_TRUE(static_bp_gv != NULL);
  ASSERT_TRUE(static_bp_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, static_bp_gv->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_BOOL, static_bp_gv->decl_type->base->kind);
  node_t *static_bp_node =
      psx_node_new_static_local_gvar_for(static_bp);
  ASSERT_TRUE(ps_node_get_type(static_bp_node) == static_bp_gv->decl_type);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(static_bp_node)->kind);
  ASSERT_TRUE(canonical_node_pointee_is_bool(static_bp_node));

  lvar_t *static_up = find_func_lvar(fn, "up");
  ASSERT_TRUE(static_up != NULL);
  ASSERT_TRUE(static_up->is_static_local);
  global_var_t *static_up_gv = find_test_global_var(
      static_up->static_global_name, static_up->static_global_name_len);
  ASSERT_TRUE(static_up_gv != NULL);
  ASSERT_TRUE(static_up_gv->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, static_up_gv->decl_type->kind);
  ASSERT_TRUE(ps_type_is_unsigned(static_up_gv->decl_type->base));
  node_t *static_up_node =
      psx_node_new_static_local_gvar_for(static_up);
  ASSERT_TRUE(ps_node_get_type(static_up_node) == static_up_gv->decl_type);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(static_up_node));

  parsed_code = parse_program_input(
      "int __tm_static_self_reference(void) { "
      "static int *self = &self; return self != 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *static_self = find_func_lvar(fn, "self");
  ASSERT_TRUE(static_self != NULL);
  ASSERT_TRUE(static_self->is_static_local);
  global_var_t *static_self_gv = find_test_global_var(
      static_self->static_global_name,
      static_self->static_global_name_len);
  ASSERT_TRUE(static_self_gv != NULL);
  ASSERT_TRUE(static_self_gv->init_symbol == static_self_gv->name);
  ASSERT_EQ(static_self_gv->name_len, static_self_gv->init_symbol_len);

  parsed_code = parse_program_input("int __tm_local_extern_decl_type(void) { extern double __tm_local_extern_dp; return 0; }");
  (void)parsed_code;
  const char *local_extern_name = "__tm_local_extern_dp";
  global_var_t *local_extern_dp =
      find_test_global_var((char *)local_extern_name, (int)(sizeof("__tm_local_extern_dp") - 1));
  ASSERT_TRUE(local_extern_dp != NULL);
  ASSERT_TRUE(local_extern_dp->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, local_extern_dp->decl_type->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, local_extern_dp->decl_type->fp_kind);

  parsed_code = parse_program_input(
      "int __tm_block_extern_proto(void) { "
      "extern double __tm_block_declared_fn(int); return 0; }");
  (void)parsed_code;
  const psx_type_t *block_declared_function = ps_ctx_get_function_type_in(test_semantic_context(),
      (char *)"__tm_block_declared_fn", 22);
  ASSERT_TRUE(block_declared_function != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, block_declared_function->kind);
  ASSERT_TRUE(block_declared_function->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, block_declared_function->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            block_declared_function->base->fp_kind);
  ASSERT_EQ(1, block_declared_function->param_count);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            block_declared_function->param_types[0]->kind);

  node_t *compound_lit_expr = parse_expr_input("(int[3]){1,2,3}");
  ASSERT_EQ(ND_COMMA, compound_lit_expr->kind);
  ASSERT_TRUE(compound_lit_expr->rhs != NULL);
  ASSERT_EQ(ND_ADDR, compound_lit_expr->rhs->kind);
  ASSERT_TRUE(compound_lit_expr->rhs->lhs != NULL);
  ASSERT_EQ(ND_LVAR, compound_lit_expr->rhs->lhs->kind);
  lvar_t *compound_lit_local = as_lvar(compound_lit_expr->rhs->lhs)->var;
  ASSERT_TRUE(compound_lit_local != NULL);
  ASSERT_TRUE(compound_lit_local->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, compound_lit_local->decl_type->kind);
  ASSERT_EQ(12, ps_type_sizeof(compound_lit_local->decl_type));
  node_t *compound_lit_stale_addr =
      ps_node_new_lvar_array_addr_for(compound_lit_local);
  ASSERT_TRUE(ps_node_get_type(compound_lit_stale_addr) != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            canonical_node_pointee_fp_kind(compound_lit_stale_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(compound_lit_stale_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(compound_lit_stale_addr));

  node_t *compound_unsigned_expr = parse_expr_input("(unsigned char[2]){1,2}");
  ASSERT_EQ(ND_COMMA, compound_unsigned_expr->kind);
  ASSERT_TRUE(compound_unsigned_expr->rhs != NULL);
  ASSERT_EQ(ND_ADDR, compound_unsigned_expr->rhs->kind);
  lvar_t *compound_unsigned_local =
      as_lvar(compound_unsigned_expr->rhs->lhs)->var;
  ASSERT_TRUE(compound_unsigned_local != NULL);
  ASSERT_TRUE(compound_unsigned_local->decl_type != NULL);
  node_t *compound_unsigned_addr =
      ps_node_new_lvar_array_addr_for(compound_unsigned_local);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(compound_unsigned_addr));

  node_t *compound_bool_expr = parse_expr_input("(_Bool[2]){0,1}");
  ASSERT_EQ(ND_COMMA, compound_bool_expr->kind);
  ASSERT_TRUE(compound_bool_expr->rhs != NULL);
  ASSERT_EQ(ND_ADDR, compound_bool_expr->rhs->kind);
  lvar_t *compound_bool_local = as_lvar(compound_bool_expr->rhs->lhs)->var;
  ASSERT_TRUE(compound_bool_local != NULL);
  ASSERT_TRUE(compound_bool_local->decl_type != NULL);
  node_t *compound_bool_addr =
      ps_node_new_lvar_array_addr_for(compound_bool_local);
  ASSERT_TRUE(canonical_node_pointee_is_bool(compound_bool_addr));

  reset_test_translation_unit_state();
  parsed_code = parse_program_input("int *__tm_compound_lit_global = (int[]){1,2,3}; int main(void) { return 0; }");
  (void)parsed_code;
  global_var_t *compound_lit_global =
      find_test_global_var("__compound_lit_0", (int)(sizeof("__compound_lit_0") - 1));
  ASSERT_TRUE(compound_lit_global != NULL);
  ASSERT_TRUE(compound_lit_global->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, compound_lit_global->decl_type->kind);
  ASSERT_EQ(12, ps_type_sizeof(compound_lit_global->decl_type));
  node_t *compound_global_stale_addr =
      ps_node_new_gvar_array_addr_for(compound_lit_global);
  ASSERT_TRUE(ps_node_get_type(compound_global_stale_addr) != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            canonical_node_pointee_fp_kind(compound_global_stale_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(compound_global_stale_addr));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(compound_global_stale_addr));

  reset_test_translation_unit_state();
  parsed_code = parse_program_input(
      "unsigned char *__tm_compound_u = (unsigned char[]){1,2}; "
      "_Bool *__tm_compound_b = (_Bool[]){0,1}; int main(void) { return 0; }");
  (void)parsed_code;
  global_var_t *compound_global_u =
      find_test_global_var("__compound_lit_0", (int)(sizeof("__compound_lit_0") - 1));
  ASSERT_TRUE(compound_global_u != NULL);
  ASSERT_TRUE(compound_global_u->decl_type != NULL);
  node_t *compound_global_u_addr =
      ps_node_new_gvar_array_addr_for(compound_global_u);
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(compound_global_u_addr));

  global_var_t *compound_global_b =
      find_test_global_var("__compound_lit_1", (int)(sizeof("__compound_lit_1") - 1));
  ASSERT_TRUE(compound_global_b != NULL);
  ASSERT_TRUE(compound_global_b->decl_type != NULL);
  node_t *compound_global_b_addr =
      ps_node_new_gvar_array_addr_for(compound_global_b);
  ASSERT_TRUE(canonical_node_pointee_is_bool(compound_global_b_addr));

  parsed_code = parse_program_input("int __tm_mix_f(int a), __tm_mix_g(int a), __tm_mix_a; "
                                    "int __tm_mix_f(int a) { return a; } "
                                    "int __tm_mix_g(int a) { return a; } "
                                    "int main(void) { __tm_mix_a = 5; return __tm_mix_a; }");
  (void)parsed_code;
  global_var_t *mix_a = find_test_global_var("__tm_mix_a", 10);
  ASSERT_TRUE(mix_a != NULL);
  ASSERT_EQ(4, ps_gvar_storage_size(mix_a, 99));
  ASSERT_TRUE(mix_a->decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, mix_a->decl_type->kind);
  ASSERT_EQ(4, ps_type_sizeof(mix_a->decl_type));

  parsed_code = parse_program_input("char *__tm_ptr_s = (char[6]){\"hi\"}; "
                                    "int main(void) { return __tm_ptr_s[0]; }");
  (void)parsed_code;
  global_var_t *ptr_s = find_test_global_var("__tm_ptr_s", 10);
  ASSERT_TRUE(ptr_s != NULL);
  ASSERT_TRUE(!ps_gvar_is_array(ptr_s));
  ASSERT_EQ(8, ps_gvar_decl_sizeof(ptr_s, 99));
  ASSERT_EQ(8, ps_gvar_storage_size(ptr_s, 99));
  ASSERT_EQ(1, ps_gvar_initializer_element_count(ptr_s, 4));

  parsed_code = parse_program_input(
      "long __tm_lf(void); int *__tm_ip(void); int **__tm_pp(void); "
      "double (*__tm_dp(void))[2]; "
      "int main(void){ int x; int *p; x + 1L; p + 1; "
      "double (*(*dpa)(void))[2]=__tm_dp; "
      "__tm_lf(); __tm_ip(); __tm_pp(); __tm_dp(); dpa(); return 0; }");
  fn = as_function_definition(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *long_add = NULL;
  node_t *ptr_add = NULL;
  node_t *long_call = NULL;
  node_t *ptr_call = NULL;
  node_t *ptrptr_call = NULL;
  node_t *double_ptr_to_array_call = NULL;
  node_t *indirect_double_ptr_to_array_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *n = body->body[i];
    if (n->kind == ND_ADD && ps_node_value_is_pointer_like(n)) ptr_add = n;
    if (n->kind == ND_ADD && !ps_node_value_is_pointer_like(n) && ps_node_type_size(n) == 8) long_add = n;
    if (n->kind == ND_FUNCALL) {
      node_function_call_t *call = as_function_call(n);
      if (call->direct_name_len == 7 &&
          strncmp(call->direct_name, "__tm_lf", 7) == 0) long_call = n;
      if (call->direct_name_len == 7 &&
          strncmp(call->direct_name, "__tm_ip", 7) == 0) ptr_call = n;
      if (call->direct_name_len == 7 &&
          strncmp(call->direct_name, "__tm_pp", 7) == 0) ptrptr_call = n;
      if (call->direct_name_len == 7 &&
          strncmp(call->direct_name, "__tm_dp", 7) == 0)
        double_ptr_to_array_call = n;
      if (call->callee && call->callee->kind == ND_LVAR) {
        lvar_t *callee_lvar = ps_node_lvar_symbol(call->callee);
        if (callee_lvar && callee_lvar->len == 3 &&
            strncmp(callee_lvar->name, "dpa", 3) == 0) {
          indirect_double_ptr_to_array_call = n;
        }
      }
    }
  }
  const psx_type_t *long_add_ty = ps_node_get_type(long_add);
  ASSERT_TRUE(long_add_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, long_add_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(long_add_ty));
  const psx_type_t *ptr_add_ty = ps_node_get_type(ptr_add);
  ASSERT_TRUE(ptr_add_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_add_ty->kind);
  ASSERT_EQ(4, ps_type_deref_size(ptr_add_ty));
  ASSERT_TRUE(long_call->type != NULL);
  const psx_type_t *long_call_ty = ps_node_get_type(long_call);
  ASSERT_TRUE(long_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, long_call_ty->kind);
  ASSERT_EQ(8, ps_type_sizeof(long_call_ty));
  ASSERT_EQ(8, ps_node_type_size(long_call));
  ASSERT_TRUE(ptr_call->type != NULL);
  const psx_type_t *ptr_call_ty = ps_node_get_type(ptr_call);
  ASSERT_TRUE(ptr_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_call_ty->kind);
  ASSERT_EQ(4, ps_type_deref_size(ptr_call_ty));
  ASSERT_TRUE(ps_node_value_is_pointer_like(ptr_call));
  ASSERT_EQ(4, ps_node_deref_size(ptr_call));
  ASSERT_TRUE(ptrptr_call->type != NULL);
  const psx_type_t *ptrptr_call_ty = ps_node_get_type(ptrptr_call);
  ASSERT_TRUE(ptrptr_call_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptrptr_call_ty->kind);
  ASSERT_EQ(8, ps_type_deref_size(ptrptr_call_ty));
  ASSERT_TRUE(ptrptr_call_ty->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptrptr_call_ty->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(ptrptr_call_ty->base));
  ASSERT_EQ(8, ps_node_deref_size(ptrptr_call));
  ASSERT_EQ(2, canonical_node_pointer_qual_levels(ptrptr_call));
  const psx_type_t *ptrptr_ret_type =
      psx_ctx_get_function_ret_type_in(test_semantic_context(), "__tm_pp", 7);
  ASSERT_TRUE(ptrptr_ret_type != NULL);
  assert_canonical_type_signature(ptrptr_ret_type, "p<p<i32>>");
  ASSERT_TRUE(double_ptr_to_array_call->type != NULL);
  const psx_type_t *double_ptr_to_array_ty =
      ps_node_get_type(double_ptr_to_array_call);
  ASSERT_TRUE(double_ptr_to_array_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, double_ptr_to_array_ty->kind);
  ASSERT_EQ(16, ps_type_deref_size(double_ptr_to_array_ty));
  ASSERT_TRUE(double_ptr_to_array_ty->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, double_ptr_to_array_ty->base->kind);
  ASSERT_EQ(16, ps_type_sizeof(double_ptr_to_array_ty->base));
  ASSERT_EQ(16, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    double_ptr_to_array_ty));
  ASSERT_EQ(16, ps_node_deref_size(double_ptr_to_array_call));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, canonical_node_pointee_fp_kind(double_ptr_to_array_call));
  const psx_type_t *dpa_ret_type =
      psx_ctx_get_function_ret_type_in(test_semantic_context(), "__tm_dp", 7);
  ASSERT_TRUE(dpa_ret_type != NULL);
  ASSERT_TRUE(ps_type_shape_matches(dpa_ret_type, double_ptr_to_array_ty));
  ASSERT_TRUE(ps_type_derived_function(dpa_ret_type) == NULL);
  ASSERT_EQ(2, double_ptr_to_array_ty->base->array_len);
  ASSERT_EQ(8, ps_type_sizeof(double_ptr_to_array_ty->base->base));
  test_semantic_define_function_name_with_ret("__tm_manual_type", 16, 0);
  psx_type_t *manual_ret_type =
      ps_type_new_pointer(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8));
  psx_type_t *manual_function_type =
      ps_type_new_function(manual_ret_type);
  ASSERT_TRUE(test_semantic_track_function_type("__tm_manual_type", 16,
                                          manual_function_type));
  const psx_type_t *manual_ret_stored =
      psx_ctx_get_function_ret_type_in(test_semantic_context(), "__tm_manual_type", 16);
  ASSERT_TRUE(manual_ret_stored != NULL);
  assert_canonical_type_signature(manual_ret_stored, "p<f64>");

  const char manual_ptrarr_name[] = "__tm_manual_ptrarr_type";
  test_semantic_define_function_name_with_ret(
      (char *)manual_ptrarr_name, (int)sizeof(manual_ptrarr_name) - 1, 0);
  psx_type_t *manual_ptrarr_element = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *manual_ptrarr_inner =
      ps_type_new_array(manual_ptrarr_element, 4, 16, 0);
  psx_type_t *manual_ptrarr_outer =
      ps_type_new_array(manual_ptrarr_inner, 3, 48, 0);
  psx_type_t *manual_ptrarr_ret =
      ps_type_new_pointer(manual_ptrarr_outer);
  psx_type_t *manual_ptrarr_function_type =
      ps_type_new_function(manual_ptrarr_ret);
  ASSERT_TRUE(test_semantic_track_function_type(
      (char *)manual_ptrarr_name, (int)sizeof(manual_ptrarr_name) - 1,
      manual_ptrarr_function_type));
  const psx_type_t *manual_ptrarr_stored = psx_ctx_get_function_ret_type_in(test_semantic_context(),
      (char *)manual_ptrarr_name, (int)sizeof(manual_ptrarr_name) - 1);
  ASSERT_TRUE(manual_ptrarr_stored != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, manual_ptrarr_stored->kind);
  ASSERT_TRUE(manual_ptrarr_stored->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, manual_ptrarr_stored->base->kind);
  ASSERT_EQ(48, ps_type_deref_size(manual_ptrarr_stored));
  ASSERT_EQ(48, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    manual_ptrarr_stored));
  ASSERT_EQ(3, manual_ptrarr_stored->base->array_len);
  ASSERT_EQ(4, manual_ptrarr_stored->base->base->array_len);
  ASSERT_EQ(4, ps_type_sizeof(manual_ptrarr_stored->base->base->base));

  assert_canonical_type_signature(
      manual_ptrarr_function_type, "p<a3<a4<i32>>>()");

  const char many_param_name[] = "__tm_many_param_type";
  test_semantic_define_function_name_with_ret(
      (char *)many_param_name, (int)sizeof(many_param_name) - 1, 0);
  psx_type_t *many_param_function = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0));
  const psx_type_t *many_param_types[17] = {0};
  for (int i = 0; i < 17; i++)
    many_param_types[i] = ps_type_new_integer(TK_INT, 4, 0);
  many_param_types[16] =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  ps_type_set_function_params(many_param_function, many_param_types, 17, 0);
  ASSERT_EQ(17, many_param_function->param_count);
  ASSERT_TRUE(many_param_function->param_types != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, many_param_function->param_types[16]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            many_param_function->param_types[16]->fp_kind);
  assert_canonical_type_signature(
      many_param_function,
      "i32(i32,i32,i32,i32,i32,i32,i32,i32,i32,i32,i32,i32,i32,i32,i32,i32,f64)");

  psx_type_t *many_param_clone = ps_type_clone(many_param_function);
  ASSERT_TRUE(many_param_clone != many_param_function);
  ASSERT_TRUE(many_param_clone->param_types !=
              many_param_function->param_types);
  ASSERT_EQ(17, many_param_clone->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT, many_param_clone->param_types[16]->kind);
  ASSERT_TRUE(ps_type_shape_matches(many_param_function,
                                    many_param_clone));

  psx_type_t *many_param_persistent =
      ps_type_clone_persistent(many_param_function);
  ASSERT_TRUE(many_param_persistent != NULL);
  ASSERT_TRUE(many_param_persistent->param_types !=
              many_param_function->param_types);
  ASSERT_EQ(17, many_param_persistent->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            many_param_persistent->param_types[16]->kind);

  const psx_type_t *many_param_variant_types[17];
  for (int i = 0; i < 17; i++)
    many_param_variant_types[i] = many_param_types[i];
  many_param_variant_types[16] =
      ps_type_new_float(TK_FLOAT_KIND_FLOAT, 4);
  psx_type_t *many_param_variant = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0));
  ps_type_set_function_params(
      many_param_variant, many_param_variant_types, 17, 0);
  ASSERT_TRUE(!ps_type_shape_matches(many_param_function,
                                     many_param_variant));

  ASSERT_TRUE(test_semantic_track_function_type(
      (char *)many_param_name, (int)sizeof(many_param_name) - 1,
      many_param_function));
  const psx_type_t *tracked_many_param = ps_ctx_get_function_type_in(test_semantic_context(),
      (char *)many_param_name, (int)sizeof(many_param_name) - 1);
  ASSERT_TRUE(tracked_many_param != NULL);
  ASSERT_EQ(17, tracked_many_param->param_count);
  const psx_type_t *many_param_last = tracked_many_param->param_types[16];
  ASSERT_TRUE(many_param_last != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, many_param_last->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, many_param_last->fp_kind);

  const char many_param_source_name[] = "__tm_many_param_source";
  node_t **many_param_source_program = parse_program_input(
      "int __tm_many_param_source("
      "int p0,int p1,int p2,int p3,int p4,int p5,int p6,int p7,"
      "int p8,int p9,int p10,int p11,int p12,int p13,int p14,int p15,"
      "double p16){return p0;}");
  ASSERT_TRUE(many_param_source_program != NULL);
  ASSERT_TRUE(many_param_source_program[0] != NULL);
  node_function_definition_t *many_param_source_function =
      as_function_definition(many_param_source_program[0]);
  ASSERT_EQ(17, many_param_source_function->parameter_count);
  ASSERT_TRUE(many_param_source_function->parameters[17] == NULL);
  const psx_type_t *many_param_source = ps_ctx_get_function_type_in(test_semantic_context(),
      (char *)many_param_source_name,
      (int)sizeof(many_param_source_name) - 1);
  ASSERT_TRUE(many_param_source != NULL);
  ASSERT_EQ(17, many_param_source->param_count);
  const psx_type_t *many_param_source_last =
      many_param_source->param_types[16];
  ASSERT_TRUE(many_param_source_last != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, many_param_source_last->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, many_param_source_last->fp_kind);

  const char typedef_shape_name[] = "__tm_typedef_shape_cmp";
  psx_type_t *typedef_shape_int_ptr =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  psx_typedef_info_t typedef_shape_a = {0};
  typedef_shape_a.decl_type = typedef_shape_int_ptr;
  ASSERT_TRUE(test_semantic_define_typedef_name((char *)typedef_shape_name,
                                          (int)sizeof(typedef_shape_name) - 1,
                                          &typedef_shape_a));
  psx_typedef_info_t typedef_shape_same_type = typedef_shape_a;
  ASSERT_TRUE(test_semantic_define_typedef_name(
      (char *)typedef_shape_name, (int)sizeof(typedef_shape_name) - 1,
      &typedef_shape_same_type));
  psx_typedef_info_t typedef_shape_different = typedef_shape_a;
  typedef_shape_different.decl_type =
      ps_type_new_pointer(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8));
  ASSERT_TRUE(!test_semantic_define_typedef_name((char *)typedef_shape_name,
                                           (int)sizeof(typedef_shape_name) - 1,
                                           &typedef_shape_different));
  const char typedef_sync_name[] = "__tm_typedef_sync_from_type";
  psx_typedef_info_t typedef_sync = {0};
  typedef_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(test_semantic_define_typedef_name((char *)typedef_sync_name,
                                          (int)sizeof(typedef_sync_name) - 1,
                                          &typedef_sync));
  psx_typedef_info_t typedef_sync_out = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), (char *)typedef_sync_name,
                                        (int)sizeof(typedef_sync_name) - 1,
                                        &typedef_sync_out));
  ASSERT_TRUE(ps_ctx_typedef_decl_type(&typedef_sync_out) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_ctx_typedef_decl_type(&typedef_sync_out)->kind);
  ASSERT_EQ(1, ps_type_pointer_depth(
                   ps_ctx_typedef_decl_type(&typedef_sync_out)));

  const char tag_member_desc_tag[] = "__tm_tag_member_desc_sync";
  const char tag_member_desc_name[] = "rows";
  test_semantic_define_tag_type_with_layout(TK_STRUCT, (char *)tag_member_desc_tag,
                                      (int)sizeof(tag_member_desc_tag) - 1,
                                      1, 6, 1);
  tag_member_info_t tag_member_desc = {0};
  tag_member_desc.name = (char *)tag_member_desc_name;
  tag_member_desc.len = (int)sizeof(tag_member_desc_name) - 1;
  psx_type_t *tag_member_desc_leaf =
      ps_type_new_integer(TK_CHAR, 1, 1);
  psx_type_t *tag_member_desc_inner =
      ps_type_new_array(tag_member_desc_leaf, 3, 3, 0);
  tag_member_desc.decl_type =
      ps_type_new_array(tag_member_desc_inner, 2, 6, 0);
  ASSERT_TRUE(register_test_tag_member(
      TK_STRUCT, (char *)tag_member_desc_tag,
      (int)sizeof(tag_member_desc_tag) - 1, &tag_member_desc));
  tag_member_info_t tag_member_desc_out = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)tag_member_desc_tag,
      (int)sizeof(tag_member_desc_tag) - 1,
      (char *)tag_member_desc_name, (int)sizeof(tag_member_desc_name) - 1,
      &tag_member_desc_out));
  ASSERT_EQ(1, test_tag_member_decl_value_size(&tag_member_desc_out));
  const psx_type_t *tag_member_desc_type =
      ps_tag_member_decl_type(&tag_member_desc_out);
  ASSERT_EQ(3, ps_type_subscript_static_stride(tag_member_desc_type));
  ASSERT_EQ(2, ps_type_array_rank(tag_member_desc_type));
  ASSERT_EQ(2, ps_type_array_dimension(tag_member_desc_type, 0));
  ASSERT_EQ(3, ps_type_array_dimension(tag_member_desc_type, 1));
  ASSERT_TRUE(ps_tag_member_decl_is_unsigned(&tag_member_desc_out));
  tag_member_info_t tag_member_desc_stale_stride = tag_member_desc_out;
  ASSERT_EQ(3, ps_type_subscript_static_stride(
                   ps_tag_member_decl_type(&tag_member_desc_stale_stride)));
  ASSERT_EQ(0, ps_type_array_dimension(tag_member_desc_type, 2));

  global_var_t gvar_view_sync = {0};
  gvar_view_sync.name = "__tm_gvar_view_sync";
  gvar_view_sync.name_len = (int)strlen(gvar_view_sync.name);
  gvar_view_sync.decl_type =
      ps_type_new_array(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 2, 16, 0);
  ASSERT_EQ(16, ps_type_sizeof(gvar_view_sync.decl_type));
  ASSERT_EQ(PSX_TYPE_ARRAY, gvar_view_sync.decl_type->kind);
  ASSERT_TRUE(gvar_view_sync.decl_type->base != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            gvar_view_sync.decl_type->base->fp_kind);

  global_var_t gvar_view_array_shape = {0};
  gvar_view_array_shape.name = "__tm_gvar_view_array_shape";
  gvar_view_array_shape.name_len = (int)strlen(gvar_view_array_shape.name);
  psx_type_t *gvar_view_array_row =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 5, 20, 0);
  gvar_view_array_shape.decl_type =
      ps_type_new_array(gvar_view_array_row, 3, 60, 0);
  ASSERT_EQ(60, ps_type_sizeof(gvar_view_array_shape.decl_type));
  ASSERT_EQ(20, ps_type_sizeof(gvar_view_array_shape.decl_type->base));

  global_var_t gvar_view_deep_array_shape = {0};
  gvar_view_deep_array_shape.name = "__tm_gvar_view_deep_array_shape";
  gvar_view_deep_array_shape.name_len =
      (int)strlen(gvar_view_deep_array_shape.name);
  psx_type_t *gvar_view_deep_d3 =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 5, 20, 0);
  psx_type_t *gvar_view_deep_d2 =
      ps_type_new_array(gvar_view_deep_d3, 4, 80, 0);
  psx_type_t *gvar_view_deep_d1 =
      ps_type_new_array(gvar_view_deep_d2, 3, 240, 0);
  gvar_view_deep_array_shape.decl_type =
      ps_type_new_array(gvar_view_deep_d1, 2, 480, 0);
  ASSERT_EQ(480, ps_type_sizeof(gvar_view_deep_array_shape.decl_type));
  ASSERT_EQ(240, ps_type_sizeof(gvar_view_deep_array_shape.decl_type->base));
  ASSERT_EQ(80, ps_type_sizeof(
                    gvar_view_deep_array_shape.decl_type->base->base));

  global_var_t gvar_view_scalar_stale_tag = {0};
  gvar_view_scalar_stale_tag.name = "__tm_gvar_view_scalar_stale_tag";
  gvar_view_scalar_stale_tag.name_len =
      (int)strlen(gvar_view_scalar_stale_tag.name);
  gvar_view_scalar_stale_tag.decl_type =
      ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_EQ(4, ps_type_sizeof(gvar_view_scalar_stale_tag.decl_type));
  ASSERT_TRUE(!ps_type_is_tag_aggregate(
      gvar_view_scalar_stale_tag.decl_type));
  ASSERT_EQ(TK_EOF, gvar_view_scalar_stale_tag.decl_type->tag_kind);

  const char gvar_view_tag_name[] = "__tm_gvar_view_tag";
  global_var_t gvar_view_tag_ptr = {0};
  gvar_view_tag_ptr.name = "__tm_gvar_view_tag_ptr";
  gvar_view_tag_ptr.name_len = (int)strlen(gvar_view_tag_ptr.name);
  gvar_view_tag_ptr.decl_type = ps_type_new_pointer(
      ps_type_new_tag(TK_STRUCT, (char *)gvar_view_tag_name,
                       (int)sizeof(gvar_view_tag_name) - 1, 0, 12));
  ASSERT_EQ(8, ps_type_sizeof(gvar_view_tag_ptr.decl_type));
  ASSERT_EQ(PSX_TYPE_POINTER, gvar_view_tag_ptr.decl_type->kind);
  ASSERT_TRUE(gvar_view_tag_ptr.decl_type->base != NULL);
  ASSERT_EQ(TK_STRUCT, gvar_view_tag_ptr.decl_type->base->tag_kind);
  ASSERT_EQ((int)sizeof(gvar_view_tag_name) - 1,
            gvar_view_tag_ptr.decl_type->base->tag_len);

  const char stale_node_tag_name[] = "__tm_stale_node_tag";
  global_var_t gvar_node_scalar_sync = {0};
  gvar_node_scalar_sync.name = "__tm_gvar_node_scalar";
  gvar_node_scalar_sync.name_len = (int)strlen(gvar_node_scalar_sync.name);
  gvar_node_scalar_sync.decl_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  node_t *gvar_node_scalar_sync_node =
      ps_node_new_gvar_for(&gvar_node_scalar_sync);
  ASSERT_EQ(8, ps_node_type_size(gvar_node_scalar_sync_node));
  ASSERT_EQ(PSX_TYPE_FLOAT,
            ps_node_get_type(gvar_node_scalar_sync_node)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_get_type(gvar_node_scalar_sync_node)->fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            canonical_node_pointee_fp_kind(gvar_node_scalar_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(gvar_node_scalar_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(gvar_node_scalar_sync_node));
  global_var_t gvar_materialized_scalar_stale_tag = gvar_node_scalar_sync;
  gvar_materialized_scalar_stale_tag.decl_type = NULL;
  const psx_type_t *gvar_materialized_scalar_type =
      ps_gvar_get_decl_type(&gvar_materialized_scalar_stale_tag);
  ASSERT_TRUE(gvar_materialized_scalar_type == NULL);

  const char gvar_node_tag_name[] = "__tm_gvar_node_tag";
  global_var_t gvar_node_tag_sync = {0};
  gvar_node_tag_sync.name = "__tm_gvar_node_tag_obj";
  gvar_node_tag_sync.name_len = (int)strlen(gvar_node_tag_sync.name);
  gvar_node_tag_sync.decl_type =
      ps_type_new_tag(TK_STRUCT, (char *)gvar_node_tag_name,
                       (int)sizeof(gvar_node_tag_name) - 1, 0, 12);
  node_t *gvar_node_tag_sync_node =
      ps_node_new_gvar_for(&gvar_node_tag_sync);
  ASSERT_EQ(0, ps_node_type_size(gvar_node_tag_sync_node));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_node_get_type(gvar_node_tag_sync_node)->kind);
  ASSERT_EQ(TK_STRUCT,
            ps_node_get_type(gvar_node_tag_sync_node)->tag_kind);

  global_var_t gvar_node_ptr_scalar_sync = {0};
  gvar_node_ptr_scalar_sync.name = "__tm_gvar_node_ptr_scalar";
  gvar_node_ptr_scalar_sync.name_len = (int)strlen(gvar_node_ptr_scalar_sync.name);
  gvar_node_ptr_scalar_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_t *gvar_node_ptr_scalar_sync_node =
      ps_node_new_gvar_for(&gvar_node_ptr_scalar_sync);
  ASSERT_TRUE(ps_node_value_is_pointer_like(gvar_node_ptr_scalar_sync_node));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(gvar_node_ptr_scalar_sync_node)->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            canonical_node_pointee_fp_kind(gvar_node_ptr_scalar_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(gvar_node_ptr_scalar_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(
      gvar_node_ptr_scalar_sync_node));

  global_var_t gvar_node_ptr_bool_sync = {0};
  gvar_node_ptr_bool_sync.name = "__tm_gvar_node_ptr_bool";
  gvar_node_ptr_bool_sync.name_len = (int)strlen(gvar_node_ptr_bool_sync.name);
  gvar_node_ptr_bool_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_BOOL, 1, 0));
  node_t *gvar_node_ptr_bool_sync_node =
      ps_node_new_gvar_for(&gvar_node_ptr_bool_sync);
  ASSERT_TRUE(ps_node_value_is_pointer_like(gvar_node_ptr_bool_sync_node));
  ASSERT_TRUE(canonical_node_pointee_is_bool(gvar_node_ptr_bool_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(gvar_node_ptr_bool_sync_node));

  global_var_t gvar_node_ptr_unsigned_sync = {0};
  gvar_node_ptr_unsigned_sync.name = "__tm_gvar_node_ptr_unsigned";
  gvar_node_ptr_unsigned_sync.name_len =
      (int)strlen(gvar_node_ptr_unsigned_sync.name);
  gvar_node_ptr_unsigned_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_UNSIGNED, 4, 1));
  node_t *gvar_node_ptr_unsigned_sync_node =
      ps_node_new_gvar_for(&gvar_node_ptr_unsigned_sync);
  ASSERT_TRUE(ps_node_value_is_pointer_like(gvar_node_ptr_unsigned_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(gvar_node_ptr_unsigned_sync_node));
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(
      gvar_node_ptr_unsigned_sync_node));

  lvar_t lvar_view_fp_array = {0};
  lvar_view_fp_array.decl_type = ps_type_new_array(
      ps_type_new_array(ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8), 3, 24, 0),
      2, 48, 0);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_lvar_fp_kind(&lvar_view_fp_array));

  lvar_t lvar_view_complex_array = {0};
  psx_type_t *lvar_view_complex_leaf = ps_type_new(PSX_TYPE_COMPLEX);
  lvar_view_complex_leaf->fp_kind = TK_FLOAT_KIND_DOUBLE;
  lvar_view_complex_array.decl_type =
      ps_type_new_array(lvar_view_complex_leaf, 2, 32, 0);
  ASSERT_TRUE(ps_lvar_is_complex(&lvar_view_complex_array));

  lvar_t lvar_view_array_shape = {0};
  lvar_view_array_shape.size = 77;
  psx_type_t *lvar_view_array_d2 =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 4, 16, 0);
  psx_type_t *lvar_view_array_d1 =
      ps_type_new_array(lvar_view_array_d2, 3, 48, 0);
  lvar_view_array_shape.decl_type =
      ps_type_new_array(lvar_view_array_d1, 2, 96, 0);
  ASSERT_TRUE(ps_lvar_is_array(&lvar_view_array_shape));
  ASSERT_EQ(24, ps_lvar_array_flat_element_count(&lvar_view_array_shape));
  ASSERT_EQ(12, ps_lvar_array_designator_stride_elements(
                    &lvar_view_array_shape, 0));
  ASSERT_EQ(4, ps_lvar_array_designator_stride_elements(
                   &lvar_view_array_shape, 1));
  ASSERT_EQ(1, ps_lvar_array_designator_stride_elements(
                   &lvar_view_array_shape, 2));
  node_t *lvar_view_array_shape_addr =
      ps_node_new_lvar_array_addr_for(&lvar_view_array_shape);
  ASSERT_TRUE(ps_node_value_is_pointer_like(lvar_view_array_shape_addr));
  ASSERT_EQ(8, ps_node_type_size(lvar_view_array_shape_addr));
  ASSERT_EQ(48, ps_node_deref_size(lvar_view_array_shape_addr));
  int lvar_view_array_shape_inner =
      canonical_node_array_subscript_stride_bytes(
          lvar_view_array_shape_addr, 0);
  int lvar_view_array_shape_next =
      canonical_node_array_subscript_stride_bytes(
          lvar_view_array_shape_addr, 1);
  ASSERT_EQ(16, lvar_view_array_shape_inner);
  ASSERT_EQ(4, lvar_view_array_shape_next);

  const char lvar_view_struct_array_tag_name[] = "__tm_lvar_view_struct_array";
  test_semantic_define_tag_type_with_layout(
      TK_STRUCT, (char *)lvar_view_struct_array_tag_name,
      (int)sizeof(lvar_view_struct_array_tag_name) - 1, 0, 12, 4);
  lvar_t lvar_view_struct_array_shape = {0};
  lvar_view_struct_array_shape.offset = 100;
  lvar_view_struct_array_shape.size = 77;
  psx_type_t *lvar_view_struct_array_type = ps_type_new_array(
      ps_type_new_tag(TK_STRUCT, (char *)lvar_view_struct_array_tag_name,
                       (int)sizeof(lvar_view_struct_array_tag_name) - 1, 0, 12),
      2, 24, 0);
  ps_ctx_bind_record_ids_in(
      test_semantic_context(), lvar_view_struct_array_type);
  lvar_view_struct_array_shape.decl_type = lvar_view_struct_array_type;
  ASSERT_TRUE(ps_lvar_is_array(&lvar_view_struct_array_shape));
  ASSERT_TRUE(ps_lvar_is_tag_aggregate(&lvar_view_struct_array_shape));
  ASSERT_EQ(24, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    lvar_view_struct_array_shape.decl_type));
  const psx_type_t *lvar_view_struct_element_type =
      ps_type_array_leaf_type(lvar_view_struct_array_shape.decl_type);
  int lvar_view_struct_element_size = ps_ctx_type_sizeof_in(
      test_semantic_context(), lvar_view_struct_element_type);
  ASSERT_EQ(12, lvar_view_struct_element_size);
  node_lvar_t *lvar_view_struct_array_elem =
      as_lvar(ps_node_new_lvar_type_at_for_in(
          test_arena_context(), &lvar_view_struct_array_shape,
          lvar_view_struct_array_shape.offset + lvar_view_struct_element_size,
          lvar_view_struct_element_type));
  ASSERT_EQ(112, lvar_view_struct_array_elem->offset);
  ASSERT_EQ(12, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    ps_node_get_type((node_t *)lvar_view_struct_array_elem)));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_node_get_type((node_t *)lvar_view_struct_array_elem)->kind);

  lvar_t lvar_view_struct_object_size = {0};
  lvar_view_struct_object_size.offset = 200;
  lvar_view_struct_object_size.size = 77;
  psx_type_t *lvar_view_struct_object_type =
      ps_type_new_tag(TK_STRUCT, (char *)lvar_view_struct_array_tag_name,
                       (int)sizeof(lvar_view_struct_array_tag_name) - 1, 0, 12);
  ps_ctx_bind_record_ids_in(
      test_semantic_context(), lvar_view_struct_object_type);
  lvar_view_struct_object_size.decl_type = lvar_view_struct_object_type;
  ASSERT_EQ(12, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    lvar_view_struct_object_size.decl_type));
  node_lvar_t *lvar_view_struct_object_ref =
      as_lvar(psx_node_new_lvar_object_ref_for(&lvar_view_struct_object_size));
  ASSERT_EQ(12, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    ps_node_get_type((node_t *)lvar_view_struct_object_ref)));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_node_get_type((node_t *)lvar_view_struct_object_ref)->kind);

  lvar_t lvar_view_const_array_addr = {0};
  lvar_view_const_array_addr.size = 8;
  psx_type_t *lvar_view_const_array_leaf =
      ps_type_new_integer(TK_INT, 4, 0);
  ps_type_add_qualifiers(lvar_view_const_array_leaf, PSX_TYPE_QUALIFIER_CONST);
  ps_type_add_qualifiers(lvar_view_const_array_leaf, PSX_TYPE_QUALIFIER_VOLATILE);
  lvar_view_const_array_addr.decl_type =
      ps_type_new_array(lvar_view_const_array_leaf, 2, 8, 0);
  node_t *lvar_view_const_array_addr_node =
      ps_node_new_lvar_array_addr_for(&lvar_view_const_array_addr);
  ASSERT_TRUE(canonical_node_pointee_is_const_qualified(
      lvar_view_const_array_addr_node));
  ASSERT_TRUE(canonical_node_pointee_is_volatile_qualified(
      lvar_view_const_array_addr_node));

  const char lvar_view_tag_name[] = "__tm_lvar_view_tag";
  lvar_t lvar_view_tag_ptr = {0};
  lvar_view_tag_ptr.decl_type = ps_type_new_pointer(
      ps_type_new_array(
          ps_type_new_tag(TK_STRUCT, (char *)lvar_view_tag_name,
                           (int)sizeof(lvar_view_tag_name) - 1, 0, 12),
          2, 24, 0));
  ASSERT_TRUE(ps_lvar_is_tag_pointer(&lvar_view_tag_ptr));
  ASSERT_EQ(TK_STRUCT, ps_lvar_tag_kind(&lvar_view_tag_ptr));

  lvar_t lvar_node_scalar_sync = {0};
  lvar_node_scalar_sync.size = 4;
  lvar_node_scalar_sync.decl_type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  node_lvar_t *lvar_node_scalar_sync_node =
      as_lvar(psx_node_new_lvar_for(&lvar_node_scalar_sync));
  ASSERT_EQ(8, ps_node_type_size((node_t *)lvar_node_scalar_sync_node));
  ASSERT_EQ(PSX_TYPE_FLOAT,
            ps_node_get_type((node_t *)lvar_node_scalar_sync_node)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_get_type((node_t *)lvar_node_scalar_sync_node)->fp_kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            canonical_node_pointee_fp_kind((node_t *)lvar_node_scalar_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool((node_t *)lvar_node_scalar_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(
      (node_t *)lvar_node_scalar_sync_node));
  lvar_t lvar_materialized_scalar_stale_tag = lvar_node_scalar_sync;
  lvar_materialized_scalar_stale_tag.decl_type = NULL;
  const psx_type_t *lvar_materialized_scalar_type =
      ps_lvar_get_decl_type(&lvar_materialized_scalar_stale_tag);
  ASSERT_TRUE(lvar_materialized_scalar_type == NULL);

  const char lvar_node_tag_name[] = "__tm_lvar_node_tag";
  lvar_t lvar_node_tag_sync = {0};
  lvar_node_tag_sync.size = 4;
  lvar_node_tag_sync.decl_type =
      ps_type_new_tag(TK_STRUCT, (char *)lvar_node_tag_name,
                       (int)sizeof(lvar_node_tag_name) - 1, 0, 12);
  node_lvar_t *lvar_node_tag_sync_node =
      as_lvar(psx_node_new_lvar_for(&lvar_node_tag_sync));
  ASSERT_EQ(0, ps_node_type_size((node_t *)lvar_node_tag_sync_node));
  ASSERT_EQ(PSX_TYPE_STRUCT,
            ps_node_get_type((node_t *)lvar_node_tag_sync_node)->kind);

  lvar_t lvar_array_elem_scalar_sync = {0};
  lvar_array_elem_scalar_sync.size = 16;
  lvar_array_elem_scalar_sync.decl_type =
      ps_type_new_array(ps_type_new_integer(TK_INT, 4, 0), 2, 8, 0);
  node_lvar_t *lvar_array_elem_scalar_sync_node =
      as_lvar(ps_node_new_array_elem_lvar_for(&lvar_array_elem_scalar_sync, 1));
  ASSERT_EQ(4, ps_node_type_size((node_t *)lvar_array_elem_scalar_sync_node));
  ASSERT_EQ(4, lvar_array_elem_scalar_sync_node->offset);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type((node_t *)lvar_array_elem_scalar_sync_node)->kind);

  lvar_t lvar_array_elem_pointer_sync = {0};
  lvar_array_elem_pointer_sync.size = 16;
  lvar_array_elem_pointer_sync.decl_type = ps_type_new_array(
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0)), 2, 16, 0);
  node_lvar_t *lvar_array_elem_pointer_sync_node =
      as_lvar(ps_node_new_array_elem_lvar_for(
          &lvar_array_elem_pointer_sync, 1));
  ASSERT_EQ(8, ps_node_type_size((node_t *)lvar_array_elem_pointer_sync_node));
  ASSERT_EQ(8, lvar_array_elem_pointer_sync_node->offset);
  ASSERT_TRUE(ps_node_value_is_pointer_like((node_t *)lvar_array_elem_pointer_sync_node));
  ASSERT_EQ(4, ps_node_deref_size((node_t *)lvar_array_elem_pointer_sync_node));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type((node_t *)lvar_array_elem_pointer_sync_node)
                ->base->kind);

  tag_member_info_t member_scalar_decl_type_wins = {0};
  member_scalar_decl_type_wins.offset = 4;
  member_scalar_decl_type_wins.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  lvar_t member_scalar_owner = {0};
  member_scalar_owner.offset = 32;
  node_lvar_t *member_scalar_decl_type_node =
      as_lvar(ps_node_new_tag_member_lvar_ref_for(
          &member_scalar_owner, member_scalar_decl_type_wins.offset,
          &member_scalar_decl_type_wins));
  ASSERT_EQ(4, ps_node_type_size((node_t *)member_scalar_decl_type_node));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type((node_t *)member_scalar_decl_type_node)->kind);

  tag_member_info_t member_const_owner_info = {0};
  member_const_owner_info.offset = 0;
  member_const_owner_info.decl_type = ps_type_new_integer(TK_INT, 4, 0);
  lvar_t member_const_owner = {0};
  member_const_owner.offset = 48;
  psx_type_t *member_const_owner_type =
      ps_type_new_tag(TK_STRUCT, (char *)stale_node_tag_name,
                       (int)sizeof(stale_node_tag_name) - 1, 0, 4);
  ps_type_add_qualifiers(member_const_owner_type, PSX_TYPE_QUALIFIER_CONST);
  ps_type_add_qualifiers(member_const_owner_type, PSX_TYPE_QUALIFIER_VOLATILE);
  member_const_owner.decl_type = member_const_owner_type;
  node_lvar_t *member_const_owner_node =
      as_lvar(ps_node_new_tag_member_lvar_ref_for(
          &member_const_owner, member_const_owner_info.offset,
          &member_const_owner_info));
  ASSERT_TRUE(ps_node_get_type((node_t *)member_const_owner_node) != NULL);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type((node_t *)member_const_owner_node),
      PSX_TYPE_QUALIFIER_CONST));
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type((node_t *)member_const_owner_node),
      PSX_TYPE_QUALIFIER_VOLATILE));
  expect_const_assign_fail_for_node((node_t *)member_const_owner_node);

  tag_member_info_t member_pointer_decl_type_wins = {0};
  member_pointer_decl_type_wins.offset = 8;
  member_pointer_decl_type_wins.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_t *member_pointer_decl_type_node =
      ps_node_new_tag_member_deref_for(
          ps_node_new_num(0), psx_node_new_lvar_for(&member_scalar_owner),
          &member_pointer_decl_type_wins);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_pointer_decl_type_node));
  ASSERT_EQ(8, ps_node_type_size(member_pointer_decl_type_node));
  ASSERT_EQ(4, ps_node_deref_size(member_pointer_decl_type_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind(member_pointer_decl_type_node));
  ASSERT_EQ(0, ps_node_integer_value_is_unsigned(
                   member_pointer_decl_type_node));
  ASSERT_EQ(PSX_TYPE_POINTER,
            ps_node_get_type(member_pointer_decl_type_node)->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(member_pointer_decl_type_node)->base->kind);

  lvar_t lvar_node_ptr_scalar_sync = {0};
  lvar_node_ptr_scalar_sync.size = 8;
  lvar_node_ptr_scalar_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  node_lvar_t *lvar_node_ptr_scalar_sync_node =
      as_lvar(psx_node_new_lvar_for(&lvar_node_ptr_scalar_sync));
  ASSERT_TRUE(ps_node_value_is_pointer_like((node_t *)lvar_node_ptr_scalar_sync_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            canonical_node_pointee_fp_kind((node_t *)lvar_node_ptr_scalar_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(
      (node_t *)lvar_node_ptr_scalar_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(
      (node_t *)lvar_node_ptr_scalar_sync_node));

  lvar_t lvar_node_ptr_bool_sync = {0};
  lvar_node_ptr_bool_sync.size = 8;
  lvar_node_ptr_bool_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_BOOL, 1, 0));
  node_lvar_t *lvar_node_ptr_bool_sync_node =
      as_lvar(psx_node_new_lvar_for(&lvar_node_ptr_bool_sync));
  ASSERT_TRUE(ps_node_value_is_pointer_like((node_t *)lvar_node_ptr_bool_sync_node));
  ASSERT_TRUE(canonical_node_pointee_is_bool((node_t *)lvar_node_ptr_bool_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_unsigned(
      (node_t *)lvar_node_ptr_bool_sync_node));

  lvar_t lvar_node_ptr_unsigned_sync = {0};
  lvar_node_ptr_unsigned_sync.size = 8;
  lvar_node_ptr_unsigned_sync.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_UNSIGNED, 4, 1));
  node_lvar_t *lvar_node_ptr_unsigned_sync_node =
      as_lvar(psx_node_new_lvar_for(&lvar_node_ptr_unsigned_sync));
  ASSERT_TRUE(ps_node_value_is_pointer_like((node_t *)lvar_node_ptr_unsigned_sync_node));
  ASSERT_TRUE(!canonical_node_pointee_is_bool(
      (node_t *)lvar_node_ptr_unsigned_sync_node));
  ASSERT_TRUE(canonical_node_pointee_is_unsigned(
      (node_t *)lvar_node_ptr_unsigned_sync_node));

  ASSERT_TRUE(indirect_double_ptr_to_array_call->type != NULL);
  const psx_type_t *indirect_double_ptr_to_array_ty =
      ps_node_get_type(indirect_double_ptr_to_array_call);
  ASSERT_TRUE(indirect_double_ptr_to_array_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, indirect_double_ptr_to_array_ty->kind);
  ASSERT_EQ(16, ps_type_deref_size(indirect_double_ptr_to_array_ty));
  ASSERT_TRUE(indirect_double_ptr_to_array_ty->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, indirect_double_ptr_to_array_ty->base->kind);
  ASSERT_EQ(16, ps_type_sizeof(indirect_double_ptr_to_array_ty->base));
  ASSERT_EQ(16, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    indirect_double_ptr_to_array_ty));
  ASSERT_EQ(16, ps_node_deref_size(indirect_double_ptr_to_array_call));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            canonical_node_pointee_fp_kind(indirect_double_ptr_to_array_call));
  lvar_t *dpa_lvar = find_func_lvar(fn, "dpa");
  ASSERT_TRUE(dpa_lvar != NULL);
  const psx_type_t *dpa_function_type =
      ps_type_derived_function(dpa_lvar->decl_type);
  ASSERT_TRUE(dpa_function_type != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, dpa_function_type->kind);
  ASSERT_TRUE(dpa_function_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, dpa_function_type->base->kind);
  ASSERT_TRUE(dpa_function_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, dpa_function_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, dpa_function_type->base->base->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            dpa_function_type->base->base->base->fp_kind);

  parsed_code = parse_program_input(
      "int (*__tm_deref_getrow(void))[3]; "
      "int main(void){ int (*(*direct)(void))[3]=__tm_deref_getrow; "
      "return (*direct())[2]; }");
  fn = as_function_definition(parsed_code[0]);
  body = as_block(fn->base.rhs);
  node_t *indirect_explicit_row_ret = NULL;
  for (int i = 0; body->body[i]; i++) {
    if (body->body[i]->kind == ND_RETURN) {
      indirect_explicit_row_ret = body->body[i];
      break;
    }
  }
  ASSERT_TRUE(indirect_explicit_row_ret != NULL);
  node_t *indirect_explicit_row_elem = indirect_explicit_row_ret->lhs;
  ASSERT_EQ(ND_DEREF, indirect_explicit_row_elem->kind);
  ASSERT_EQ(4, ps_node_type_size(indirect_explicit_row_elem));
  ASSERT_EQ(0, ps_node_deref_size(indirect_explicit_row_elem));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(indirect_explicit_row_elem));
  const psx_type_t *indirect_explicit_row_elem_ty =
      ps_node_get_type(indirect_explicit_row_elem);
  ASSERT_TRUE(indirect_explicit_row_elem_ty != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, indirect_explicit_row_elem_ty->kind);
  ASSERT_EQ(4, ps_type_sizeof(indirect_explicit_row_elem_ty));

  parsed_code = parse_program_input(
      "int add(int a,int b){return a+b;} int (*ops[3])(int,int)={add,add,add}; "
      "int main(void){ int (*(*pb)[3])(int,int)=&ops; return (*pb)[0](1,2); }");
  fn = as_function_definition(parsed_code[1]);
  lvar_t *pb_lvar = find_func_lvar(fn, "pb");
  ASSERT_TRUE(pb_lvar != NULL);
  const psx_type_t *pb_type = pb_lvar->decl_type;
  ASSERT_TRUE(pb_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, pb_type->kind);
  ASSERT_TRUE(pb_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, pb_type->base->kind);
  ASSERT_TRUE(pb_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, pb_type->base->base->kind);
  ASSERT_TRUE(pb_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, pb_type->base->base->base->kind);

  parsed_code = parse_program_input(
      "static int data; static void *get(void){return &data;} "
      "int main(void){int *p=(int *)get(); return p[0];}");
  fn = as_function_definition(parsed_code[1]);
  body = as_block(fn->base.rhs);
  node_t *void_ptr_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *candidate = body->body[i];
    if (candidate->kind == ND_ASSIGN && candidate->rhs &&
        candidate->rhs->kind == ND_CAST) {
      void_ptr_call = candidate->rhs->lhs;
      break;
    }
  }
  ASSERT_TRUE(void_ptr_call != NULL);
  ASSERT_EQ(ND_FUNCALL, void_ptr_call->kind);
  const psx_type_t *void_ptr_call_type = ps_node_get_type(void_ptr_call);
  ASSERT_TRUE(void_ptr_call_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, void_ptr_call_type->kind);
  ASSERT_TRUE(void_ptr_call_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_VOID, void_ptr_call_type->base->kind);
  ASSERT_TRUE(!ps_node_value_is_void(void_ptr_call));
  const psx_type_t *void_ptr_ret_type =
      psx_ctx_get_function_ret_type_in(test_semantic_context(), "get", 3);
  ASSERT_TRUE(void_ptr_ret_type != NULL);
  assert_canonical_type_signature(void_ptr_ret_type, "p<v>");

  parsed_code = parse_program_input(
      "typedef int (*TMFunc)(int,int); typedef TMFunc TMFuncs[3]; "
      "int tm_add(int a,int b){return a+b;} "
      "int main(void){TMFuncs ops={tm_add,tm_add,tm_add}; TMFuncs *pa=&ops; "
      "return (*pa)[0](1,2);}");
  psx_typedef_info_t tm_func_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TMFunc", 6, &tm_func_info));
  ASSERT_TRUE(ps_type_derived_function(
      ps_ctx_typedef_decl_type(&tm_func_info)) != NULL);
  psx_typedef_info_t tm_funcs_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TMFuncs", 7, &tm_funcs_info));
  ASSERT_TRUE(ps_type_derived_function(
      ps_ctx_typedef_decl_type(&tm_funcs_info)) != NULL);
  fn = as_function_definition(parsed_code[1]);
  lvar_t *tm_ops = find_func_lvar(fn, "ops");
  lvar_t *tm_pa = find_func_lvar(fn, "pa");
  ASSERT_TRUE(tm_ops != NULL);
  ASSERT_TRUE(tm_pa != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_ops->decl_type->kind);
  ASSERT_TRUE(tm_ops->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_ops->decl_type->base->kind);
  ASSERT_TRUE(ps_type_derived_function(tm_ops->decl_type) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_pa->decl_type->kind);
  ASSERT_TRUE(tm_pa->decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_pa->decl_type->base->kind);
  ASSERT_TRUE(tm_pa->decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_pa->decl_type->base->base->kind);
  ASSERT_TRUE(ps_type_derived_function(tm_pa->decl_type) != NULL);

  parsed_code = parse_program_input(
      "int tm_local_add(int a,int b){return a+b;} "
      "int main(void){typedef int (*TMLocalFunc)(int,int); "
      "typedef TMLocalFunc TMLocalFuncs[2]; "
      "typedef int *TMLocalArrayPtr[3]; typedef int (*TMLocalPtrArray)[3]; "
      "TMLocalFuncs ops={tm_local_add,tm_local_add}; "
      "TMLocalFuncs matrix[2]; "
      "TMLocalFuncs *pa=&ops; TMLocalArrayPtr array_ptrs; "
      "TMLocalPtrArray ptr_array; return (*pa)[0](1,2);}");
  fn = as_function_definition(parsed_code[1]);
  lvar_t *tm_local_ops = find_func_lvar(fn, "ops");
  lvar_t *tm_local_matrix = find_func_lvar(fn, "matrix");
  lvar_t *tm_local_pa = find_func_lvar(fn, "pa");
  lvar_t *tm_local_array_ptrs = find_func_lvar(fn, "array_ptrs");
  lvar_t *tm_local_ptr_array = find_func_lvar(fn, "ptr_array");
  ASSERT_TRUE(tm_local_ops != NULL);
  ASSERT_TRUE(tm_local_matrix != NULL);
  ASSERT_TRUE(tm_local_pa != NULL);
  ASSERT_TRUE(tm_local_array_ptrs != NULL);
  ASSERT_TRUE(tm_local_ptr_array != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_ops->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_ops->decl_type->base->kind);
  ASSERT_TRUE(ps_type_derived_function(tm_local_ops->decl_type) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_pa->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_pa->decl_type->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_pa->decl_type->base->base->kind);
  ASSERT_TRUE(ps_type_derived_function(tm_local_pa->decl_type) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_matrix->decl_type->kind);
  ASSERT_EQ(2, tm_local_matrix->decl_type->array_len);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_matrix->decl_type->base->kind);
  ASSERT_EQ(2, tm_local_matrix->decl_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_POINTER,
            tm_local_matrix->decl_type->base->base->kind);
  ASSERT_TRUE(ps_type_derived_function(tm_local_matrix->decl_type) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_array_ptrs->decl_type->kind);
  ASSERT_EQ(3, tm_local_array_ptrs->decl_type->array_len);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_array_ptrs->decl_type->base->kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            tm_local_array_ptrs->decl_type->base->base->kind);
  ASSERT_EQ(PSX_TYPE_POINTER, tm_local_ptr_array->decl_type->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, tm_local_ptr_array->decl_type->base->kind);
  ASSERT_EQ(3, tm_local_ptr_array->decl_type->base->array_len);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            tm_local_ptr_array->decl_type->base->base->kind);

  parsed_code = parse_program_input(
      "struct TM695 { double *dp; double (*fp)(void); }; int main(void){ return 0; }");
  (void)parsed_code;
  tag_member_info_t dp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "TM695", 5, "dp", 2, &dp_info));
  ASSERT_TRUE(dp_info.decl_type != NULL && dp_info.decl_type->base != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, dp_info.decl_type->base->fp_kind);
  ASSERT_TRUE(ps_type_derived_function(dp_info.decl_type) == NULL);
  node_t canonical_int_ptr = {0};
  canonical_int_ptr.kind = ND_LVAR;
  canonical_int_ptr.type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, canonical_node_pointee_fp_kind(&canonical_int_ptr));

  tag_member_info_t fp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "TM695", 5, "fp", 2, &fp_info));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_tag_member_decl_fp_kind(&fp_info));
  ASSERT_TRUE(fp_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, fp_info.decl_type->kind);
  assert_canonical_type_signature(fp_info.decl_type, "p<f64()>");

  tag_member_info_t partial_sig_member = {0};
  psx_type_t *partial_sig_param =
      ps_type_new_float(TK_FLOAT_KIND_FLOAT, 4);
  const psx_type_t *partial_sig_params[] = {partial_sig_param};
  partial_sig_member.decl_type = test_function_pointer(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8),
      partial_sig_params, 1, 0);
  assert_canonical_type_signature(
      partial_sig_member.decl_type, "p<f64(f32)>");
  psx_type_t *partial_sig_function = (psx_type_t *)ps_type_derived_function(
      partial_sig_member.decl_type);
  ASSERT_TRUE(partial_sig_function != NULL);
  ASSERT_EQ(1, partial_sig_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT, partial_sig_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT, partial_sig_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, partial_sig_function->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, partial_sig_function->base->fp_kind);
  const psx_type_t *partial_sig_member_decl_type =
      partial_sig_member.decl_type;
  ASSERT_TRUE(partial_sig_member.decl_type == partial_sig_member_decl_type);
  node_t *partial_sig_node = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), ps_node_new_num(0), &partial_sig_member);
  assert_canonical_type_signature(
      ps_node_get_type(partial_sig_node), "p<f64(f32)>");
  const psx_type_t *partial_sig_node_function =
      ps_type_derived_function(ps_node_get_type(partial_sig_node));
  ASSERT_TRUE(partial_sig_node_function != NULL);
  ASSERT_EQ(1, partial_sig_node_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            partial_sig_node_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_FLOAT,
            partial_sig_node_function->param_types[0]->fp_kind);

  parsed_code = parse_program_input(
      "struct TM695F { int (*fns[2])(int, int); }; int main(void){ return 0; }");
  (void)parsed_code;
  tag_member_info_t fns_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "TM695F", 6, "fns", 3, &fns_info));
  ASSERT_TRUE(fns_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, fns_info.decl_type->kind);
  ASSERT_EQ(2, fns_info.decl_type->array_len);
  ASSERT_EQ(8, ps_type_deref_size(fns_info.decl_type));
  ASSERT_TRUE(fns_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, fns_info.decl_type->base->kind);
  ASSERT_TRUE(fns_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, fns_info.decl_type->base->base->kind);
  ASSERT_TRUE(fns_info.decl_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, fns_info.decl_type->base->base->base->kind);
  assert_canonical_type_signature(
      fns_info.decl_type, "a2<p<i32(i32,i32)>>");

  parsed_code = parse_program_input(
      "struct TM695Ops { double (*d)(double); }; "
      "struct TM695Holder { struct TM695Ops ops[2]; }; "
      "double __tm695_d(double x){ return x; } "
      "int main(void){ struct TM695Holder h; h.ops[0].d = __tm695_d; return 0; }");
  (void)parsed_code;
  tag_member_info_t ops_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "TM695Holder", 11,
                                           "ops", 3, &ops_info));
  ASSERT_TRUE(ops_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ops_info.decl_type->kind);
  ASSERT_TRUE(ops_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, ops_info.decl_type->base->kind);
  ASSERT_EQ(TK_STRUCT, ops_info.decl_type->base->tag_kind);
  ASSERT_EQ(8, ops_info.decl_type->base->tag_len);

  parsed_code = parse_program_input(
      "typedef int (*__tm695RowPtr)[3]; "
      "struct TM695RowHolder { struct { __tm695RowPtr rows[2]; }; int z; }; "
      "int main(void){ int a[2][3]; struct TM695RowHolder h = {.rows = {a, a}}; "
      "return h.rows[0][1][2]; }");
  (void)parsed_code;
  tag_member_info_t rows_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "TM695RowHolder", 14,
                                           "rows", 4, &rows_info));
  ASSERT_TRUE(rows_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, rows_info.decl_type->kind);
  ASSERT_EQ(12, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    rows_info.decl_type));
  ASSERT_TRUE(rows_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, rows_info.decl_type->base->kind);
  ASSERT_EQ(12, ps_type_deref_size(rows_info.decl_type->base));
  ASSERT_EQ(12, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    rows_info.decl_type->base));
  ASSERT_TRUE(rows_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, rows_info.decl_type->base->base->kind);
  ASSERT_EQ(3, rows_info.decl_type->base->base->array_len);
  ASSERT_EQ(4, ps_type_deref_size(rows_info.decl_type->base->base));

  parsed_code = parse_program_input(
      "struct TMPtrRows { int (*p[2])[3]; }; int main(void){return 0;}");
  (void)parsed_code;
  tag_member_info_t ptr_rows_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, "TMPtrRows", 9, "p", 1, &ptr_rows_info));
  ASSERT_TRUE(ptr_rows_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptr_rows_info.decl_type->kind);
  ASSERT_EQ(2, ptr_rows_info.decl_type->array_len);
  ASSERT_TRUE(ptr_rows_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ptr_rows_info.decl_type->base->kind);
  ASSERT_TRUE(ptr_rows_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ptr_rows_info.decl_type->base->base->kind);
  ASSERT_EQ(3, ptr_rows_info.decl_type->base->base->array_len);
  ASSERT_TRUE(ptr_rows_info.decl_type->base->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ptr_rows_info.decl_type->base->base->base->kind);
  ASSERT_EQ(12, ps_type_sizeof(ptr_rows_info.decl_type->base->base));

  parsed_code = parse_program_input(
      "struct __tm_member_ptrarr { int (*p)[3]; }; "
      "int main(void){ struct __tm_member_ptrarr h; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *member_ptrarr_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_ptrarr_h != NULL);
  tag_member_info_t member_ptrarr_p_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "__tm_member_ptrarr", 18,
                                           "p", 1, &member_ptrarr_p_info));
  node_t *member_ptrarr_p_node = ps_node_new_tag_member_lvar_ref_for(
      member_ptrarr_h, member_ptrarr_p_info.offset, &member_ptrarr_p_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_ptrarr_p_node));
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_node));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_ptrarr_p_node));
  ASSERT_EQ(12, canonical_node_ptr_array_pointee_bytes(member_ptrarr_p_node));
  int member_ptrarr_inner =
      canonical_node_array_subscript_stride_bytes(member_ptrarr_p_node, 0);
  ASSERT_EQ(4, member_ptrarr_inner);
  ASSERT_TRUE(ps_node_get_type(member_ptrarr_p_node) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(member_ptrarr_p_node)->kind);
  node_t *member_ptrarr_p_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_ptrarr_h),
      &member_ptrarr_p_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_ptrarr_p_deref));
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_deref));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_ptrarr_p_deref));
  ASSERT_EQ(12, canonical_node_ptr_array_pointee_bytes(member_ptrarr_p_deref));
  ASSERT_TRUE(ps_node_get_type(member_ptrarr_p_deref) != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, ps_node_get_type(member_ptrarr_p_deref)->kind);
  ASSERT_EQ(12, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    member_ptrarr_p_info.decl_type));
  ASSERT_EQ(4, ps_type_array_scalar_element_size(
                   member_ptrarr_p_info.decl_type->base));

  parsed_code = parse_program_input(
      "typedef int *__tm_member_IP; "
      "struct __tm_member_ip_ptrarr { __tm_member_IP (*p)[3]; }; "
      "int main(void){ struct __tm_member_ip_ptrarr h; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *member_ip_ptrarr_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_ip_ptrarr_h != NULL);
  tag_member_info_t member_ip_ptrarr_p_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, "__tm_member_ip_ptrarr",
      (int)sizeof("__tm_member_ip_ptrarr") - 1, "p", 1,
      &member_ip_ptrarr_p_info));
  ASSERT_TRUE(member_ip_ptrarr_p_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_ip_ptrarr_p_info.decl_type->kind);
  ASSERT_EQ(24, ps_type_deref_size(member_ip_ptrarr_p_info.decl_type));
  ASSERT_EQ(24, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    member_ip_ptrarr_p_info.decl_type));
  ASSERT_TRUE(member_ip_ptrarr_p_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, member_ip_ptrarr_p_info.decl_type->base->kind);
  ASSERT_EQ(3, member_ip_ptrarr_p_info.decl_type->base->array_len);
  ASSERT_EQ(
      8, ps_type_deref_size(member_ip_ptrarr_p_info.decl_type->base));
  ASSERT_TRUE(member_ip_ptrarr_p_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_ip_ptrarr_p_info.decl_type->base->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(
                   member_ip_ptrarr_p_info.decl_type->base->base));
  ASSERT_EQ(24, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    member_ip_ptrarr_p_info.decl_type));
  node_t *member_ip_ptrarr_p_node = ps_node_new_tag_member_lvar_ref_for(
      member_ip_ptrarr_h, member_ip_ptrarr_p_info.offset,
      &member_ip_ptrarr_p_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_ip_ptrarr_p_node));
  ASSERT_EQ(24, ps_node_deref_size(member_ip_ptrarr_p_node));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_ip_ptrarr_p_node));
  int member_ip_ptrarr_inner =
      canonical_node_array_subscript_stride_bytes(member_ip_ptrarr_p_node, 0);
  ASSERT_EQ(8, member_ip_ptrarr_inner);
  node_t *member_ip_ptrarr_row =
      ps_node_new_unary_deref_for(member_ip_ptrarr_p_node);
  ASSERT_EQ(24, ps_node_type_size(member_ip_ptrarr_row));
  ASSERT_EQ(8, ps_node_deref_size(member_ip_ptrarr_row));
  node_t *member_ip_ptrarr_elem = ps_node_new_subscript_deref_for(
      member_ip_ptrarr_row,
      member_ip_ptrarr_row->lhs ? member_ip_ptrarr_row->lhs
                                : member_ip_ptrarr_row,
      ps_node_new_num(0));
  ASSERT_EQ(8, ps_node_type_size(member_ip_ptrarr_elem));
  ASSERT_EQ(4, ps_node_deref_size(member_ip_ptrarr_elem));
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_ip_ptrarr_elem));

  parsed_code = parse_program_input(
      "struct __tm_member_pp { int **pp; }; "
      "int main(void){ struct __tm_member_pp h; return 0; }");
  (void)parsed_code;
  tag_member_info_t member_pp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, "__tm_member_pp", (int)sizeof("__tm_member_pp") - 1,
      "pp", 2, &member_pp_info));
  ASSERT_TRUE(member_pp_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_pp_info.decl_type->kind);
  ASSERT_EQ(2, canonical_pointer_qual_levels(member_pp_info.decl_type));
  ASSERT_TRUE(member_pp_info.decl_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_pp_info.decl_type->base->kind);
  ASSERT_EQ(4, ps_type_deref_size(member_pp_info.decl_type->base));
  ASSERT_TRUE(member_pp_info.decl_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, member_pp_info.decl_type->base->base->kind);

  tag_member_info_t member_ptrarr_p_stale_info = member_ptrarr_p_info;
  ASSERT_EQ(12, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                    member_ptrarr_p_stale_info.decl_type));
  ASSERT_EQ(4, ps_type_array_scalar_element_size(
                   member_ptrarr_p_stale_info.decl_type->base));
  node_t *member_ptrarr_p_stale_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_ptrarr_h),
      &member_ptrarr_p_stale_info);
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_stale_deref));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_ptrarr_p_stale_deref));
  tag_member_info_t member_ptrarr_p_stale_hi_info = member_ptrarr_p_info;
  node_t *member_ptrarr_p_stale_hi_node = ps_node_new_tag_member_lvar_ref_for(
      member_ptrarr_h, member_ptrarr_p_stale_hi_info.offset,
      &member_ptrarr_p_stale_hi_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_ptrarr_p_stale_hi_node));
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_stale_hi_node));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_ptrarr_p_stale_hi_node));
  ASSERT_EQ(12, canonical_node_ptr_array_pointee_bytes(member_ptrarr_p_stale_hi_node));
  node_t *member_ptrarr_p_stale_hi_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_ptrarr_h),
      &member_ptrarr_p_stale_hi_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_ptrarr_p_stale_hi_deref));
  ASSERT_EQ(12, ps_node_deref_size(member_ptrarr_p_stale_hi_deref));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_ptrarr_p_stale_hi_deref));
  ASSERT_EQ(12, canonical_node_ptr_array_pointee_bytes(
                    member_ptrarr_p_stale_hi_deref));

  parsed_code = parse_program_input(
      "struct __tm_member_arr { int a[2]; }; "
      "int main(void){ struct __tm_member_arr h; return h.a[0]; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *member_arr_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_arr_h != NULL);
  tag_member_info_t member_arr_a_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "__tm_member_arr", 15,
                                           "a", 1, &member_arr_a_info));
  node_t *member_arr_a_node = ps_node_new_tag_member_lvar_ref_for(
      member_arr_h, member_arr_a_info.offset, &member_arr_a_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_arr_a_node));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_node));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_node));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_arr_a_node));
  ASSERT_TRUE(ps_node_get_type(member_arr_a_node) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(member_arr_a_node)->kind);
  node_t *member_arr_a_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_arr_h),
      &member_arr_a_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_arr_a_deref));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_deref));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_deref));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_arr_a_deref));
  ASSERT_TRUE(ps_node_deref_decays_to_address(member_arr_a_deref));
  ASSERT_TRUE(ps_node_get_type(member_arr_a_deref) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(member_arr_a_deref)->kind);
  tag_member_info_t member_arr_a_stale_info = member_arr_a_info;
  ASSERT_EQ(1, ps_type_array_rank(member_arr_a_stale_info.decl_type));
  ASSERT_EQ(2, ps_type_array_dimension(member_arr_a_stale_info.decl_type, 0));
  ASSERT_EQ(4, ps_type_deref_size(member_arr_a_stale_info.decl_type));
  node_t *member_arr_a_stale_node = ps_node_new_tag_member_lvar_ref_for(
      member_arr_h, member_arr_a_stale_info.offset, &member_arr_a_stale_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_arr_a_stale_node));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_stale_node));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_stale_node));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(member_arr_a_stale_node)->kind);
  node_t *member_arr_a_stale_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_arr_h),
      &member_arr_a_stale_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_arr_a_stale_deref));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_stale_deref));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_stale_deref));
  ASSERT_TRUE(ps_node_deref_decays_to_address(member_arr_a_stale_deref));
  tag_member_info_t member_arr_a_stale_hi_info = member_arr_a_info;
  ASSERT_EQ(1, ps_type_array_rank(member_arr_a_stale_hi_info.decl_type));
  ASSERT_EQ(2, ps_type_array_dimension(
                   member_arr_a_stale_hi_info.decl_type, 0));
  ASSERT_EQ(0, ps_type_array_dimension(
                   member_arr_a_stale_hi_info.decl_type, 1));
  node_t *member_arr_a_stale_hi_node = ps_node_new_tag_member_lvar_ref_for(
      member_arr_h, member_arr_a_stale_hi_info.offset,
      &member_arr_a_stale_hi_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_arr_a_stale_hi_node));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_stale_hi_node));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_stale_hi_node));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_arr_a_stale_hi_node));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(member_arr_a_stale_hi_node));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(member_arr_a_stale_hi_node)->kind);
  node_t *member_arr_a_stale_hi_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_arr_h),
      &member_arr_a_stale_hi_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_arr_a_stale_hi_deref));
  ASSERT_EQ(8, ps_node_type_size(member_arr_a_stale_hi_deref));
  ASSERT_EQ(4, ps_node_deref_size(member_arr_a_stale_hi_deref));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_arr_a_stale_hi_deref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   member_arr_a_stale_hi_deref));
  ASSERT_TRUE(ps_node_deref_decays_to_address(member_arr_a_stale_hi_deref));

  parsed_code = parse_program_input(
      "struct __tm_member_plain_ptr { int *p; }; "
      "int main(void){ struct __tm_member_plain_ptr h; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *member_plain_ptr_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_plain_ptr_h != NULL);
  tag_member_info_t member_plain_ptr_p_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT,
                                           "__tm_member_plain_ptr", 21,
                                           "p", 1,
                                           &member_plain_ptr_p_info));
  ASSERT_TRUE(member_plain_ptr_p_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, member_plain_ptr_p_info.decl_type->kind);
  tag_member_info_t member_plain_ptr_p_stale_info = member_plain_ptr_p_info;
  member_plain_ptr_p_stale_info.decl_type =
      ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0));
  const psx_type_t *member_plain_ptr_type =
      member_plain_ptr_p_stale_info.decl_type;
  ASSERT_EQ(0, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                   member_plain_ptr_type));
  ASSERT_EQ(0, ps_type_array_rank(member_plain_ptr_type));
  ASSERT_EQ(0, ps_type_array_dimension(member_plain_ptr_type, 0));
  ASSERT_EQ(0, ps_type_array_dimension(member_plain_ptr_type, 1));
  ASSERT_EQ(0, ps_type_array_subscript_stride_bytes(member_plain_ptr_type, 0));
  node_t *member_plain_ptr_p_node = ps_node_new_tag_member_lvar_ref_for(
      member_plain_ptr_h, member_plain_ptr_p_stale_info.offset,
      &member_plain_ptr_p_stale_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_plain_ptr_p_node));
  ASSERT_EQ(4, ps_node_deref_size(member_plain_ptr_p_node));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_plain_ptr_p_node));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(member_plain_ptr_p_node));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   member_plain_ptr_p_node, 0));
  node_t *member_plain_ptr_p_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_plain_ptr_h),
      &member_plain_ptr_p_stale_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_plain_ptr_p_deref));
  ASSERT_EQ(4, ps_node_deref_size(member_plain_ptr_p_deref));
  ASSERT_EQ(4, canonical_node_base_deref_size(member_plain_ptr_p_deref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(member_plain_ptr_p_deref));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   member_plain_ptr_p_deref, 0));
  ASSERT_TRUE(ps_node_scalar_ptr_member_lvalue(member_plain_ptr_p_deref));

  tag_member_info_t member_plain_ptr_p_flat_mismatch_info =
      member_plain_ptr_p_info;
  member_plain_ptr_p_flat_mismatch_info.decl_type =
      ps_type_new_pointer(NULL);
  const psx_type_t *member_plain_ptr_missing_base =
      member_plain_ptr_p_flat_mismatch_info.decl_type;
  ASSERT_EQ(0, ps_type_pointer_view_structural_ptr_array_pointee_bytes(
                   member_plain_ptr_missing_base));
  ASSERT_EQ(0, canonical_pointer_qual_levels(
                   member_plain_ptr_missing_base));
  ASSERT_EQ(0, ps_type_array_subscript_stride_bytes(
                   member_plain_ptr_missing_base, 0));
  node_t *member_plain_ptr_p_flat_mismatch_node =
      ps_node_new_tag_member_lvar_ref_for(
          member_plain_ptr_h,
          member_plain_ptr_p_flat_mismatch_info.offset,
          &member_plain_ptr_p_flat_mismatch_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(0, ps_node_deref_size(member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(0, canonical_node_pointer_qual_levels(member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(0, canonical_node_base_deref_size(member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   member_plain_ptr_p_flat_mismatch_node));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   member_plain_ptr_p_flat_mismatch_node, 0));
  node_t *member_plain_ptr_p_flat_mismatch_deref =
      ps_node_new_tag_member_deref_for(
          ps_node_new_num(0), psx_node_new_lvar_for(member_plain_ptr_h),
          &member_plain_ptr_p_flat_mismatch_info);
  ASSERT_TRUE(ps_node_value_is_pointer_like(member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(0, ps_node_deref_size(member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(0, canonical_node_pointer_qual_levels(member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(0, canonical_node_base_deref_size(member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(
                   member_plain_ptr_p_flat_mismatch_deref));
  ASSERT_EQ(0, canonical_node_array_subscript_stride_bytes(
                   member_plain_ptr_p_flat_mismatch_deref, 0));

  parsed_code = parse_program_input(
      "struct __tm_member_scalar { unsigned int u; _Bool b; _Atomic int a; "
      "double _Complex z; }; "
      "int main(void){ struct __tm_member_scalar h; return 0; }");
  fn = as_function_definition(parsed_code[0]);
  lvar_t *member_scalar_h = find_func_lvar(fn, "h");
  ASSERT_TRUE(member_scalar_h != NULL);
  const char *member_scalar_tag = "__tm_member_scalar";
  tag_member_info_t member_scalar_u_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)member_scalar_tag, (int)strlen(member_scalar_tag),
      "u", 1, &member_scalar_u_info));
  ASSERT_TRUE(member_scalar_u_info.decl_type != NULL);
  ASSERT_TRUE(ps_type_is_unsigned(member_scalar_u_info.decl_type));
  node_t *member_scalar_u_node = ps_node_new_tag_member_lvar_ref_for(
      member_scalar_h, member_scalar_u_info.offset, &member_scalar_u_info);
  ASSERT_TRUE(ps_node_is_unsigned_type(member_scalar_u_node));
  node_t *member_scalar_u_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_scalar_h),
      &member_scalar_u_info);
  ASSERT_TRUE(ps_node_is_unsigned_type(member_scalar_u_deref));
  ASSERT_TRUE(ps_type_is_unsigned(ps_node_get_type(member_scalar_u_deref)));

  tag_member_info_t member_scalar_b_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)member_scalar_tag, (int)strlen(member_scalar_tag),
      "b", 1, &member_scalar_b_info));
  ASSERT_TRUE(member_scalar_b_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, member_scalar_b_info.decl_type->kind);
  node_t *member_scalar_b_node = ps_node_new_tag_member_lvar_ref_for(
      member_scalar_h, member_scalar_b_info.offset, &member_scalar_b_info);
  ASSERT_TRUE(ps_node_get_type(member_scalar_b_node) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(member_scalar_b_node)->kind);
  node_t *member_scalar_b_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_scalar_h),
      &member_scalar_b_info);
  ASSERT_TRUE(ps_node_get_type(member_scalar_b_deref) != NULL);
  ASSERT_EQ(PSX_TYPE_BOOL, ps_node_get_type(member_scalar_b_deref)->kind);

  tag_member_info_t member_scalar_a_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)member_scalar_tag, (int)strlen(member_scalar_tag),
      "a", 1, &member_scalar_a_info));
  ASSERT_TRUE(member_scalar_a_info.decl_type != NULL);
  ASSERT_TRUE(ps_type_has_qualifier(member_scalar_a_info.decl_type, PSX_TYPE_QUALIFIER_ATOMIC));
  node_t *member_scalar_a_node = ps_node_new_tag_member_lvar_ref_for(
      member_scalar_h, member_scalar_a_info.offset, &member_scalar_a_info);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(member_scalar_a_node), PSX_TYPE_QUALIFIER_ATOMIC));
  ASSERT_EQ(PSX_TYPE_INTEGER, ps_node_get_type(member_scalar_a_node)->kind);
  ASSERT_EQ(0, canonical_node_pointer_qual_levels(member_scalar_a_node));
  ASSERT_EQ(TK_FLOAT_KIND_NONE, ps_node_value_fp_kind(member_scalar_a_node));
  ASSERT_TRUE(!ps_node_is_unsigned_type(member_scalar_a_node));
  node_t *member_scalar_a_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_scalar_h),
      &member_scalar_a_info);
  ASSERT_TRUE(ps_type_has_qualifier(
      ps_node_get_type(member_scalar_a_deref), PSX_TYPE_QUALIFIER_ATOMIC));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            ps_node_get_type(member_scalar_a_deref)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_NONE,
            ps_node_value_fp_kind(member_scalar_a_deref));
  ASSERT_TRUE(!ps_node_value_is_pointer_like(member_scalar_a_deref));

  tag_member_info_t member_scalar_z_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(),
      TK_STRUCT, (char *)member_scalar_tag, (int)strlen(member_scalar_tag),
      "z", 1, &member_scalar_z_info));
  ASSERT_TRUE(member_scalar_z_info.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_COMPLEX, member_scalar_z_info.decl_type->kind);
  node_t *member_scalar_z_node = ps_node_new_tag_member_lvar_ref_for(
      member_scalar_h, member_scalar_z_info.offset, &member_scalar_z_info);
  ASSERT_EQ(PSX_TYPE_COMPLEX, ps_node_get_type(member_scalar_z_node)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, ps_node_value_fp_kind(member_scalar_z_node));
  node_t *member_scalar_z_deref = ps_node_new_tag_member_deref_for(
      ps_node_new_num(0), psx_node_new_lvar_for(member_scalar_h),
      &member_scalar_z_info);
  ASSERT_EQ(PSX_TYPE_COMPLEX,
            ps_node_get_type(member_scalar_z_deref)->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            ps_node_value_fp_kind(member_scalar_z_deref));

  parsed_code = parse_program_input(
      "double __tm696_ret_d(void){ return 1.0; } "
      "double *__tm696_gdp; double (*__tm696_gfp)(void)=__tm696_ret_d; "
      "int main(void){ double d; double *dp=&d; double (*fp)(void)=__tm696_ret_d; return 0; }");
  fn = as_function_definition(parsed_code[1]);
  lvar_t *dp_lvar = find_func_lvar(fn, "dp");
  ASSERT_TRUE(dp_lvar != NULL);
  const psx_type_t *dp_type = ps_lvar_get_decl_type(dp_lvar);
  ASSERT_TRUE(dp_type != NULL && dp_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, dp_type->base->kind);
  ASSERT_TRUE(ps_type_derived_function(dp_type) == NULL);
  lvar_t *fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(fp_lvar != NULL);
  assert_canonical_type_signature(
      ps_lvar_get_decl_type(fp_lvar), "p<f64()>");
  global_var_t *gdp = find_test_global_var("__tm696_gdp", 11);
  ASSERT_TRUE(gdp != NULL);
  const psx_type_t *gdp_type = ps_gvar_get_decl_type(gdp);
  ASSERT_TRUE(gdp_type != NULL && gdp_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, gdp_type->base->kind);
  ASSERT_TRUE(ps_type_derived_function(gdp_type) == NULL);
  global_var_t *gfp = find_test_global_var("__tm696_gfp", 11);
  ASSERT_TRUE(gfp != NULL);
  assert_canonical_type_signature(
      ps_gvar_get_decl_type(gfp), "p<f64()>");

  parsed_code = parse_program_input(
      "typedef int *__tm696_GPI; int __tm696_gia[3]; "
      "__tm696_GPI __tm696_gpi = __tm696_gia; int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *typedef_gpi = find_test_global_var("__tm696_gpi", 11);
  ASSERT_TRUE(typedef_gpi != NULL);
  const psx_type_t *typedef_gpi_type = ps_gvar_get_decl_type(typedef_gpi);
  ASSERT_TRUE(typedef_gpi_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, typedef_gpi_type->kind);
  ASSERT_EQ(8, ps_type_sizeof(typedef_gpi_type));
  ASSERT_TRUE(typedef_gpi_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, typedef_gpi_type->base->kind);

  parsed_code = parse_program_input(
      "double __tm696_rows[2][2]; double (*__tm696_gpa)[2]=__tm696_rows; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gpa = find_test_global_var("__tm696_gpa", 11);
  ASSERT_TRUE(gpa != NULL);
  node_t *gpa_node = ps_node_new_gvar_for(gpa);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, canonical_node_pointee_fp_kind(gpa_node));
  ASSERT_EQ(16, canonical_node_ptr_array_pointee_bytes(gpa_node));
  node_t *gpa_row = ps_node_new_unary_deref_for(gpa_node);
  ASSERT_EQ(16, ps_node_type_size(gpa_row));
  ASSERT_EQ(8, ps_node_deref_size(gpa_row));
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, canonical_node_pointee_fp_kind(gpa_row));

  parsed_code = parse_program_input(
      "typedef int *__tm696_GIP; "
      "int __tm696_ga, __tm696_gb, __tm696_gc; "
      "__tm696_GIP __tm696_grow[3] = { &__tm696_ga, &__tm696_gb, &__tm696_gc }; "
      "__tm696_GIP (*__tm696_gpia)[3] = &__tm696_grow; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gpia = find_test_global_var("__tm696_gpia", 12);
  ASSERT_TRUE(gpia != NULL);
  node_t *gpia_node = ps_node_new_gvar_for(gpia);
  const psx_type_t *gpia_type = ps_node_get_type(gpia_node);
  ASSERT_TRUE(gpia_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpia_type->kind);
  ASSERT_TRUE(gpia_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gpia_type->base->kind);
  ASSERT_TRUE(gpia_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpia_type->base->base->kind);
  ASSERT_EQ(24, ps_node_deref_size(gpia_node));
  int gpia_inner =
      canonical_node_array_subscript_stride_bytes(gpia_node, 0);
  ASSERT_EQ(8, gpia_inner);
  node_t *gpia_row = ps_node_new_unary_deref_for(gpia_node);
  ASSERT_EQ(24, ps_node_type_size(gpia_row));
  ASSERT_EQ(8, ps_node_deref_size(gpia_row));
  ASSERT_TRUE(ps_node_value_is_pointer_like(gpia_row));
  node_t *gpia_elem = ps_node_new_subscript_deref_for(
      gpia_row, gpia_row->lhs ? gpia_row->lhs : gpia_row,
      ps_node_new_num(0));
  ASSERT_EQ(8, ps_node_type_size(gpia_elem));
  ASSERT_EQ(4, ps_node_deref_size(gpia_elem));
  ASSERT_TRUE(ps_node_value_is_pointer_like(gpia_elem));

  parsed_code = parse_program_input(
      "struct __tm696_S { int a; int b; }; "
      "struct __tm696_S __tm696_sa[3]; "
      "struct __tm696_S (*__tm696_gap)[3] = &__tm696_sa; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gap = find_test_global_var("__tm696_gap", 11);
  ASSERT_TRUE(gap != NULL);
  node_t *gap_node = ps_node_new_gvar_for(gap);
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(gap_node));
  ASSERT_EQ(24, ps_ctx_type_sizeof_in(
                    test_semantic_context(),
                    ps_node_get_type(gap_node)->base));
  node_t *gap_row = ps_node_new_unary_deref_for(gap_node);
  ASSERT_EQ(0, canonical_node_ptr_array_pointee_bytes(gap_row));
  ASSERT_EQ(0, ps_node_type_size(gap_row));
  ASSERT_EQ(0, ps_node_deref_size(gap_row));
  ASSERT_EQ(24, ps_ctx_type_sizeof_in(
                    test_semantic_context(), ps_node_get_type(gap_row)));
  ASSERT_EQ(8, ps_ctx_type_sizeof_in(
                   test_semantic_context(),
                   ps_node_get_type(gap_row)->base));
  ASSERT_TRUE(ps_node_get_type(gap_row) != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gap_row)->kind);
  ASSERT_TRUE(ps_node_get_type(gap_row)->base != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, ps_node_get_type(gap_row)->base->kind);
  node_t *gap_elem = ps_node_new_subscript_deref_for(
      gap_row, gap_row->lhs ? gap_row->lhs : gap_row,
      ps_node_new_num(0));
  const psx_type_t *gap_elem_type = ps_node_get_type(gap_elem);
  ASSERT_TRUE(gap_elem_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, gap_elem_type->kind);
  ASSERT_EQ(TK_STRUCT, gap_elem_type->tag_kind);

  parsed_code = parse_program_input(
      "int __tm696_int_rows[2][3][4]; "
      "int (*__tm696_gpi2)[3][4] = __tm696_int_rows; "
      "int main(void){ return 0; }");
  (void)parsed_code;
  global_var_t *gpi = find_test_global_var("__tm696_gpi2", 12);
  ASSERT_TRUE(gpi != NULL);
  node_t *gpi_node = ps_node_new_gvar_for(gpi);
  ASSERT_EQ(48, ps_node_deref_size(gpi_node));
  const psx_type_t *gpi_type = ps_node_get_type(gpi_node);
  ASSERT_TRUE(gpi_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, gpi_type->kind);
  ASSERT_TRUE(gpi_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_ARRAY, gpi_type->base->kind);
  int gpi_inner =
      ps_type_array_subscript_stride_bytes(gpi_type->base, 0);
  int gpi_next =
      ps_type_array_subscript_stride_bytes(gpi_type->base, 1);
  ASSERT_EQ(16, gpi_inner);
  ASSERT_EQ(4, gpi_next);
  node_t *gpi_outer_row = ps_node_new_subscript_deref_for(
      gpi_node, gpi_node, ps_node_new_num(0));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gpi_outer_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(gpi_outer_row));
  gpi_inner =
      canonical_node_array_subscript_stride_bytes(gpi_outer_row, 0);
  gpi_next =
      canonical_node_array_subscript_stride_bytes(gpi_outer_row, 1);
  ASSERT_EQ(16, gpi_inner);
  ASSERT_EQ(4, gpi_next);
  node_t *gpi_outer_base =
      ps_node_subscript_deref_uses_base_address(gpi_outer_row)
          ? gpi_outer_row->lhs
          : gpi_outer_row;
  node_t *gpi_inner_row = ps_node_new_subscript_deref_for(
      gpi_outer_row, gpi_outer_base, ps_node_new_num(0));
  ASSERT_EQ(PSX_TYPE_ARRAY, ps_node_get_type(gpi_inner_row)->kind);
  ASSERT_TRUE(ps_node_subscript_deref_uses_base_address(gpi_inner_row));

  parsed_code = parse_program_input(
      "double __tm697_ret_d(void){ return 1.0; } "
      "typedef double *TM697_DP; typedef double (*TM697_FP)(void); "
      "int main(void){ double d; TM697_DP dp=&d; TM697_FP fp=__tm697_ret_d; "
      "{ typedef double (*TM697_BFP)(void); TM697_BFP bfp=__tm697_ret_d; bfp(); } "
      "return fp() == *dp; }");
  (void)parsed_code;
  psx_typedef_info_t td_dp = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TM697_DP", 8, &td_dp));
  ASSERT_TRUE(td_dp.decl_type != NULL && td_dp.decl_type->base != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_dp.decl_type->base->fp_kind);
  assert_canonical_type_signature(td_dp.decl_type, "p<f64>");
  psx_typedef_info_t td_fp = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TM697_FP", 8, &td_fp));
  ASSERT_TRUE(td_fp.decl_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, td_fp.decl_type->kind);
  assert_canonical_type_signature(td_fp.decl_type, "p<f64()>");
  const psx_type_t *td_fp_decl_type = td_fp.decl_type;
  ASSERT_TRUE(td_fp.decl_type == td_fp_decl_type);
  fn = as_function_definition(parsed_code[1]);
  lvar_t *td_dp_lvar = find_func_lvar(fn, "dp");
  ASSERT_TRUE(td_dp_lvar != NULL);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, td_dp_lvar->decl_type->base->fp_kind);
  assert_canonical_type_signature(td_dp_lvar->decl_type, "p<f64>");
  lvar_t *td_fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(td_fp_lvar != NULL);
  assert_canonical_type_signature(td_fp_lvar->decl_type, "p<f64()>");
  lvar_t *td_bfp_lvar = find_func_lvar(fn, "bfp");
  ASSERT_TRUE(td_bfp_lvar != NULL);
  assert_canonical_type_signature(td_bfp_lvar->decl_type, "p<f64()>");

  parsed_code = parse_program_input(
      "int __tm817_i; int *__tm817_retp(void){ return &__tm817_i; } "
      "int __tm817_inc(int x){ return x + 1; } "
      "typedef int *TM817_IP; typedef int *(*TM817_GET)(void); "
      "typedef TM817_IP (*TM817_GET2)(void); typedef int (**TM817_PP)(int); "
      "int main(void){ int (*p)(int)=__tm817_inc; TM817_GET get=__tm817_retp; "
      "TM817_GET2 get2=__tm817_retp; "
      "TM817_PP pp=&p; { typedef int *(*TM817_LOCAL_GET)(void); "
      "typedef int (**TM817_LOCAL_PP)(int); TM817_LOCAL_GET lget=__tm817_retp; "
      "TM817_LOCAL_PP lpp=&p; lget(); (*lpp)(1); } return *get()+*get2()+(*pp)(1); }");
  psx_typedef_info_t tm817_get_td = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TM817_GET", 9, &tm817_get_td));
  assert_canonical_type_signature(
      tm817_get_td.decl_type, "p<p<i32>()>");
  ASSERT_EQ(1, ps_type_pointer_depth(tm817_get_td.decl_type));
  psx_typedef_info_t tm817_get2_td = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TM817_GET2", 10, &tm817_get2_td));
  assert_canonical_type_signature(
      tm817_get2_td.decl_type, "p<p<i32>()>");
  ASSERT_EQ(1, ps_type_pointer_depth(tm817_get2_td.decl_type));
  psx_typedef_info_t tm817_pp_td = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(), "TM817_PP", 8, &tm817_pp_td));
  assert_canonical_type_signature(
      tm817_pp_td.decl_type, "p<p<i32(i32)>>");
  ASSERT_EQ(2, ps_type_pointer_depth(tm817_pp_td.decl_type));
  fn = as_function_definition(parsed_code[2]);
  lvar_t *tm817_get_lvar = find_func_lvar(fn, "get");
  ASSERT_TRUE(tm817_get_lvar != NULL);
  ASSERT_EQ(1, canonical_lvar_pointer_qual_levels(tm817_get_lvar));
  assert_canonical_type_signature(
      tm817_get_lvar->decl_type, "p<p<i32>()>");
  lvar_t *tm817_get2_lvar = find_func_lvar(fn, "get2");
  ASSERT_TRUE(tm817_get2_lvar != NULL);
  ASSERT_EQ(1, canonical_lvar_pointer_qual_levels(tm817_get2_lvar));
  assert_canonical_type_signature(
      tm817_get2_lvar->decl_type, "p<p<i32>()>");
  lvar_t *tm817_pp_lvar = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm817_pp_lvar != NULL);
  ASSERT_EQ(2, canonical_lvar_pointer_qual_levels(tm817_pp_lvar));
  assert_canonical_type_signature(
      tm817_pp_lvar->decl_type, "p<p<i32(i32)>>");
  lvar_t *tm817_lget_lvar = find_func_lvar(fn, "lget");
  ASSERT_TRUE(tm817_lget_lvar != NULL);
  ASSERT_EQ(1, canonical_lvar_pointer_qual_levels(tm817_lget_lvar));
  assert_canonical_type_signature(
      tm817_lget_lvar->decl_type, "p<p<i32>()>");
  lvar_t *tm817_lpp_lvar = find_func_lvar(fn, "lpp");
  ASSERT_TRUE(tm817_lpp_lvar != NULL);
  ASSERT_EQ(2, canonical_lvar_pointer_qual_levels(tm817_lpp_lvar));
  assert_canonical_type_signature(
      tm817_lpp_lvar->decl_type, "p<p<i32(i32)>>");

  parsed_code = parse_program_input(
      "double __tm700_d; double *__tm700_ret_dp(void){ return &__tm700_d; } "
      "double *(*__tm700_gfp)(void)=__tm700_ret_dp; "
      "int main(void){ double *(*fp)(void)=__tm700_ret_dp; fp(); __tm700_gfp(); return 0; }");
  fn = as_function_definition(parsed_code[1]);
  lvar_t *tm700_fp_lvar = find_func_lvar(fn, "fp");
  ASSERT_TRUE(tm700_fp_lvar != NULL);
  assert_canonical_type_signature(
      tm700_fp_lvar->decl_type, "p<p<f64>()>");
  global_var_t *tm700_gfp = find_test_global_var("__tm700_gfp", 11);
  ASSERT_TRUE(tm700_gfp != NULL);
  assert_canonical_type_signature(
      tm700_gfp->decl_type, "p<p<f64>()>");

  parsed_code = parse_program_input(
      "int __tm814_inc(int x){ return x + 1; } "
      "int main(void){ int (*p)(int)=__tm814_inc; int (**pp)(int)=&p; return (*pp)(41); }");
  fn = as_function_definition(parsed_code[1]);
  lvar_t *tm814_pp_lvar = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm814_pp_lvar != NULL);
  ASSERT_EQ(2, canonical_lvar_pointer_qual_levels(tm814_pp_lvar));
  assert_canonical_type_signature(
      tm814_pp_lvar->decl_type, "p<p<i32(i32)>>");
  node_t *tm814_pp_node = psx_node_new_lvar_for(tm814_pp_lvar);
  node_t *tm814_deref_pp = ps_node_new_unary_deref_for(tm814_pp_node);
  assert_canonical_type_signature(
      ps_node_get_type(tm814_deref_pp), "p<i32(i32)>");

  parsed_code = parse_program_input(
      "int __tm815_inc(int x){ return x + 1; } "
      "int (*__tm815_gp)(int)=__tm815_inc; int (**__tm815_gpp)(int)=&__tm815_gp; "
      "int __tm815_apply(int (**pp)(int)){ return (*pp)(41); } "
      "int main(void){ return (*__tm815_gpp)(41) + __tm815_apply(__tm815_gpp); }");
  global_var_t *tm815_gpp = find_test_global_var("__tm815_gpp", 11);
  ASSERT_TRUE(tm815_gpp != NULL);
  ASSERT_EQ(2, ps_type_pointer_depth(tm815_gpp->decl_type));
  assert_canonical_type_signature(
      tm815_gpp->decl_type, "p<p<i32(i32)>>");
  fn = as_function_definition(parsed_code[1]);
  lvar_t *tm815_param_pp = find_func_lvar(fn, "pp");
  ASSERT_TRUE(tm815_param_pp != NULL);
  ASSERT_EQ(2, canonical_lvar_pointer_qual_levels(tm815_param_pp));
  assert_canonical_type_signature(
      tm815_param_pp->decl_type, "p<p<i32(i32)>>");

  parsed_code = parse_program_input(
      "int __tm816_i; int *__tm816_retp(void){ return &__tm816_i; } "
      "int __tm816_inc(int x){ return x + 1; } "
      "int *(*__tm816_gfp)(void)=__tm816_retp; "
      "struct TM816 { int *(*fp)(void); int (**pp)(int); }; "
      "int *__tm816_apply(int *(*fp)(void)){ return fp(); } "
      "int __tm816_call(struct TM816 *s){ return *s->fp() + (*s->pp)(1); } "
      "int main(void){ return *__tm816_apply(__tm816_retp); }");
  global_var_t *tm816_gfp = find_test_global_var("__tm816_gfp", 11);
  ASSERT_TRUE(tm816_gfp != NULL);
  assert_canonical_type_signature(
      tm816_gfp->decl_type, "p<p<i32>()>");
  fn = as_function_definition(parsed_code[2]);
  lvar_t *tm816_param_fp = find_func_lvar(fn, "fp");
  ASSERT_TRUE(tm816_param_fp != NULL);
  assert_canonical_type_signature(
      tm816_param_fp->decl_type, "p<p<i32>()>");
  tag_member_info_t tm816_fp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "TM816", 5, "fp", 2,
                                           &tm816_fp_info));
  assert_canonical_type_signature(
      tm816_fp_info.decl_type, "p<p<i32>()>");
  tag_member_info_t tm816_pp_info = {0};
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(test_semantic_context(), TK_STRUCT, "TM816", 5, "pp", 2,
                                           &tm816_pp_info));
  ASSERT_EQ(2, canonical_pointer_qual_levels(
                   tm816_pp_info.decl_type));
  assert_canonical_type_signature(
      tm816_pp_info.decl_type, "p<p<i32(i32)>>");

  parsed_code = parse_program_input(
      "double __tm_sq(double x){ return x*x; } "
      "int *__tm_makep(void){ static int x=1; return &x; } "
      "int main(void){ double (*df)(double)=__tm_sq; int *(*pf)(void)=__tm_makep; "
      "df(2.0); pf(); return 0; }");
  fn = as_function_definition(parsed_code[2]);
  body = as_block(fn->base.rhs);
  node_t *indirect_double_call = NULL;
  node_t *indirect_ptr_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *n = body->body[i];
    if (n->kind != ND_FUNCALL) continue;
    node_function_call_t *call = as_function_call(n);
    if (!call->callee || call->callee->kind != ND_LVAR) continue;
    lvar_t *callee_lvar = ps_node_lvar_symbol(call->callee);
    if (callee_lvar && callee_lvar->len == 2 && strncmp(callee_lvar->name, "df", 2) == 0)
      indirect_double_call = n;
    if (callee_lvar && callee_lvar->len == 2 && strncmp(callee_lvar->name, "pf", 2) == 0)
      indirect_ptr_call = n;
	  }
	  ASSERT_TRUE(indirect_double_call->type != NULL);
	  const psx_type_t *indirect_double_ty =
	      ps_node_get_type(indirect_double_call);
	  ASSERT_TRUE(indirect_double_ty != NULL);
	  ASSERT_EQ(PSX_TYPE_FLOAT, indirect_double_ty->kind);
	  ASSERT_EQ(8, ps_type_sizeof(indirect_double_ty));
	  ASSERT_TRUE(indirect_ptr_call->type != NULL);
	  const psx_type_t *indirect_ptr_ty = ps_node_get_type(indirect_ptr_call);
	  ASSERT_TRUE(indirect_ptr_ty != NULL);
	  ASSERT_EQ(PSX_TYPE_POINTER, indirect_ptr_ty->kind);
	  ASSERT_EQ(4, ps_type_deref_size(indirect_ptr_ty));
	  ASSERT_TRUE(ps_node_value_is_pointer_like(indirect_ptr_call));

  parsed_code = parse_program_input(
      "struct CQ { int v; }; const struct CQ *__tm_cq(void){ return 0; } "
      "int main(void){ __tm_cq(); return 0; }");
  fn = as_function_definition(parsed_code[1]);
  body = as_block(fn->base.rhs);
  node_t *const_struct_ptr_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *n = body->body[i];
    if (n->kind != ND_FUNCALL) continue;
    node_function_call_t *call = as_function_call(n);
    if (call->direct_name_len == 7 &&
        strncmp(call->direct_name, "__tm_cq", 7) == 0) {
      const_struct_ptr_call = n;
      break;
    }
  }
  ASSERT_TRUE(const_struct_ptr_call != NULL);
  const psx_type_t *const_struct_ptr_ty =
      ps_node_get_type(const_struct_ptr_call);
  ASSERT_TRUE(const_struct_ptr_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, const_struct_ptr_ty->kind);
  ASSERT_TRUE(const_struct_ptr_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, const_struct_ptr_ty->base->tag_kind);
  ASSERT_TRUE(ps_type_has_qualifier(const_struct_ptr_ty->base, PSX_TYPE_QUALIFIER_CONST));
  ASSERT_EQ(2, const_struct_ptr_ty->base->tag_len);
  ASSERT_TRUE(strncmp(const_struct_ptr_ty->base->tag_name,
                      "CQ", 2) == 0);

  parsed_code = parse_program_input(
      "int __tm_zero(void){ return 0; } "
      "struct FS { int (*zerofunc)(void); }; "
      "struct FS __tm_fs = { __tm_zero }; "
      "struct FS *__tm_anon(void){ return &__tm_fs; } "
      "typedef struct FS *(*__tm_fty)(void); "
      "__tm_fty __tm_go(void){ return __tm_anon; } "
      "int main(void){ __tm_go()(); return 0; }");
  node_function_definition_t *go_def = as_function_definition(parsed_code[2]);
  ASSERT_TRUE(go_def->signature != NULL);
  assert_canonical_type_signature(
      go_def->signature, "p<p<s{2:FS}>()>()");
  ASSERT_TRUE(go_def->base.type == NULL);
  const psx_type_t *go_return_type =
      ps_function_definition_return_type(go_def);
  ASSERT_TRUE(go_return_type != NULL);
  assert_canonical_type_signature(
      go_return_type, "p<p<s{2:FS}>()>");
  const psx_type_t *go_ctx_return =
      psx_ctx_get_function_ret_type_in(test_semantic_context(), "__tm_go", 7);
  ASSERT_TRUE(go_ctx_return != NULL);
  ASSERT_TRUE(ps_type_shape_matches(go_ctx_return, go_return_type));
  fn = as_function_definition(parsed_code[3]);
  body = as_block(fn->base.rhs);
  node_t *funcptr_chain_call = NULL;
  for (int i = 0; body->body[i]; i++) {
    node_t *n = body->body[i];
    if (n->kind != ND_FUNCALL) continue;
    node_function_call_t *call = as_function_call(n);
    if (call->callee && call->callee->kind == ND_FUNCALL) {
      funcptr_chain_call = n;
      break;
    }
  }
  ASSERT_TRUE(funcptr_chain_call != NULL);
  const psx_type_t *funcptr_chain_ty = ps_node_get_type(funcptr_chain_call);
  ASSERT_TRUE(funcptr_chain_ty != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, funcptr_chain_ty->kind);
  ASSERT_TRUE(funcptr_chain_ty->base != NULL);
  ASSERT_EQ(TK_STRUCT, funcptr_chain_ty->base->tag_kind);
  ASSERT_EQ(2, funcptr_chain_ty->base->tag_len);
  ASSERT_TRUE(strncmp(funcptr_chain_ty->base->tag_name,
                      "FS", 2) == 0);

  parsed_code = parse_program_input(
      "double __tm698_add(double x){ return x + 0.5; } "
      "typedef double (*TM698_DF)(double); "
      "TM698_DF __tm698_pick(void){ return __tm698_add; } "
      "int main(void){ return __tm698_pick()(3.0) == 3.5; }");
  node_function_definition_t *pick_def = as_function_definition(parsed_code[1]);
  ASSERT_TRUE(pick_def->signature != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, pick_def->signature->kind);
  ASSERT_TRUE(pick_def->signature->base != NULL);
  ASSERT_TRUE(pick_def->base.type == NULL);
  ASSERT_TRUE(ps_function_definition_return_type(pick_def) ==
              pick_def->signature->base);
  assert_canonical_type_signature(
      pick_def->signature, "p<f64(f64)>()");
  assert_canonical_type_signature(
      pick_def->signature->base, "p<f64(f64)>");
  node_function_call_t pick_call = {0};
  pick_call.base.kind = ND_FUNCALL;
  pick_call.direct_name = "__tm698_pick";
  pick_call.direct_name_len = 12;
  pick_call.callee_type = ps_type_clone(pick_def->signature);
  ASSERT_TRUE(ps_node_get_type((node_t *)&pick_call) == NULL);
  analyze_test_expression((node_t *)&pick_call, NULL);
  const psx_type_t *pick_call_type =
      ps_node_get_type((node_t *)&pick_call);
  ASSERT_TRUE(pick_call_type != NULL);
  ASSERT_TRUE(pick_call_type == pick_call.callee_type->base);
  ASSERT_TRUE(ps_node_value_is_pointer_like((node_t *)&pick_call));
  assert_canonical_type_signature(pick_call_type, "p<f64(f64)>");
  const psx_type_t *pick_ctx_return =
      psx_ctx_get_function_ret_type_in(test_semantic_context(), "__tm698_pick", 12);
  ASSERT_TRUE(pick_ctx_return != NULL);
  ASSERT_TRUE(ps_type_shape_matches(
      pick_ctx_return, pick_def->signature->base));
  node_funcref_t pick_add_ref = {0};
  pick_add_ref.base.kind = ND_FUNCREF;
  pick_add_ref.funcname = "__tm698_add";
  pick_add_ref.funcname_len = 11;
  pick_add_ref.base.type = ps_type_clone(
      as_function_definition(parsed_code[0])->signature);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            ps_node_get_type((node_t *)&pick_add_ref)->kind);
  analyze_test_expression((node_t *)&pick_add_ref, NULL);
  const psx_type_t *pick_add_ref_type =
      ps_node_get_type((node_t *)&pick_add_ref);
  ASSERT_TRUE(pick_add_ref_type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, pick_add_ref_type->kind);
  ASSERT_TRUE(pick_add_ref_type->base != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION, pick_add_ref_type->base->kind);
  ASSERT_TRUE(pick_add_ref_type->base->base != NULL);
  ASSERT_EQ(PSX_TYPE_FLOAT, pick_add_ref_type->base->base->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE, pick_add_ref_type->base->base->fp_kind);
  assert_canonical_type_signature(pick_add_ref_type, "p<f64(f64)>");

  parsed_code = parse_program_input(
      "int __tm818_inc(int x){ return x + 1; } "
      "int (*__tm818_fp)(int)=__tm818_inc; "
      "int (**__tm818_getpp(void))(int){ return &__tm818_fp; } "
      "int main(void){ return (*__tm818_getpp())(20) + (**__tm818_getpp())(30); }");
  node_function_definition_t *tm818_getpp_def = as_function_definition(parsed_code[1]);
  assert_canonical_type_signature(
      tm818_getpp_def->signature, "p<p<i32(i32)>>()");
  ASSERT_EQ(2, ps_type_pointer_depth(
                   psx_ctx_get_function_ret_type_in(test_semantic_context(), "__tm818_getpp", 13)));
}

static void test_translation_unit_reset_static_local_state() {
  printf("test_translation_unit_reset_static_local_state...\n");

  const char *input = "int f(void) { static int x=1; return x; }";
  reset_test_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(find_test_global_var("f.x.0", 5) != NULL);
  ASSERT_TRUE(find_test_global_var("f.x.1", 5) == NULL);

  reset_test_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(find_test_global_var("f.x.0", 5) != NULL);
  ASSERT_TRUE(find_test_global_var("f.x.1", 5) == NULL);
  reset_test_translation_unit_state();
}

static void test_translation_unit_reset_anonymous_tag_state() {
  printf("test_translation_unit_reset_anonymous_tag_state...\n");

  const char *input = "struct { int x; } g; int main(void) { return 0; }";
  reset_test_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_TRUE(test_semantic_has_tag_type(TK_STRUCT, "__anon_tag_0", 12));
  ASSERT_TRUE(!test_semantic_has_tag_type(TK_STRUCT, "__anon_tag_1", 12));

  reset_test_translation_unit_state();
  parsed_code = parse_program_input(input);
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_TRUE(test_semantic_has_tag_type(TK_STRUCT, "__anon_tag_0", 12));
  ASSERT_TRUE(!test_semantic_has_tag_type(TK_STRUCT, "__anon_tag_1", 12));
  reset_test_translation_unit_state();
}

static void test_translation_unit_reset_decl_locals_state() {
  printf("test_translation_unit_reset_decl_locals_state...\n");

  char name[] = "x";
  reset_test_locals();
  ASSERT_TRUE(register_test_default_storage_fixture(name, 1) != NULL);
  ASSERT_TRUE(ps_decl_find_lvar_in(test_local_registry(), name, 1) != NULL);
  reset_test_translation_unit_state();
  ASSERT_TRUE(ps_decl_find_lvar_in(test_local_registry(), name, 1) == NULL);
}

static void test_translation_unit_reset_pragma_pack_state() {
  printf("test_translation_unit_reset_pragma_pack_state...\n");

  psx_parser_runtime_context_t *runtime_context =
      ag_compilation_session_parser_runtime_context(test_suite_session);
  pragma_pack_reset_in(runtime_context);
  pragma_pack_set_in(runtime_context, 8);
  pragma_pack_push_in(runtime_context, 1);
  ASSERT_EQ(1, pragma_pack_current_alignment_in(runtime_context));
  reset_test_translation_unit_state();
  ASSERT_EQ(0, pragma_pack_current_alignment_in(runtime_context));
  pragma_pack_pop_in(runtime_context);
  ASSERT_EQ(0, pragma_pack_current_alignment_in(runtime_context));
}

static void test_multiple_funcdefs() {
  printf("test_multiple_funcdefs...\n");
  parsed_code = parse_program_input(
      "int foo(void) { 1; } int bar(void) { 2; }");

  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "foo", 3) == 0);

  ASSERT_TRUE(parsed_code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[1])->name, "bar", 3) == 0);

  ASSERT_TRUE(parsed_code[2] == NULL);

  parsed_code = parse_program_input("int add(int a, int b); int add(int a, int b) { return a+b; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "add", 3) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("int log(const char *fmt, ...); int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("int **sig_proto_pp(void); int main(void) { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(2, ps_type_pointer_depth(
                   psx_ctx_get_function_ret_type_in(test_semantic_context(), "sig_proto_pp", 12)));

  parsed_code = parse_program_input("int **sig_def_pp(void) { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(2, ps_type_pointer_depth(
                   psx_ctx_get_function_ret_type_in(test_semantic_context(), "sig_def_pp", 10)));

  parsed_code = parse_program_input(
      "int (*(*(*sig_deep(void))(int))(double))[3] { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  const psx_type_t *deep_return =
      psx_ctx_get_function_ret_type_in(test_semantic_context(), "sig_deep", 8);
  const psx_type_t *deep_int_function =
      ps_type_derived_function(deep_return);
  ASSERT_TRUE(deep_int_function != NULL);
  ASSERT_EQ(1, deep_int_function->param_count);
  ASSERT_EQ(PSX_TYPE_INTEGER, deep_int_function->param_types[0]->kind);
  const psx_type_t *deep_double_function =
      ps_type_derived_function(deep_int_function->base);
  ASSERT_TRUE(deep_double_function != NULL);
  ASSERT_EQ(1, deep_double_function->param_count);
  ASSERT_EQ(PSX_TYPE_FLOAT, deep_double_function->param_types[0]->kind);
  ASSERT_EQ(TK_FLOAT_KIND_DOUBLE,
            deep_double_function->param_types[0]->fp_kind);
  ASSERT_EQ(PSX_TYPE_POINTER, deep_double_function->base->kind);
  ASSERT_EQ(PSX_TYPE_ARRAY, deep_double_function->base->base->kind);
  ASSERT_EQ(3, deep_double_function->base->base->array_len);

  parsed_code = parse_program_input("int variadic(...){ return 0; } int main() { return variadic(); }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "variadic", 8) == 0);
  ASSERT_TRUE(parsed_code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);
  ASSERT_TRUE(parsed_code[2] == NULL);

  parsed_code = parse_program_input("int f(int a[static 3], int b[restrict static 2]) { return 7; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "f", 1) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("struct S { int x; }; int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("struct S { int x; } *gp; int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("int g=1; int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input("extern int g; inline int add(int a, int b) { return a+b; } int main() { return add(3,4); }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "add", 3) == 0);
  ASSERT_TRUE(parsed_code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[1])->name, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[2] == NULL);

  parsed_code = parse_program_input("_Noreturn void die() { return; } int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "die", 3) == 0);
  ASSERT_TRUE(parsed_code[1] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[1]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[1])->name, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[2] == NULL);

  parsed_code = parse_program_input("_Static_assert(1, \"ok\"); int main() { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
  ASSERT_TRUE(strncmp(as_function_definition(parsed_code[0])->name, "main", 4) == 0);
  ASSERT_TRUE(parsed_code[1] == NULL);

  parsed_code = parse_program_input(
      "struct SA { _Static_assert(sizeof(int)==4, \"ok\"); int x; }; "
      "int main(void) { return 0; }");
  ASSERT_TRUE(parsed_code[0] != NULL);
  ASSERT_EQ(ND_FUNCDEF, parsed_code[0]->kind);
}

static void test_parse_invalid() {
  printf("test_parse_invalid...\n");
  expect_parse_fail("int main(void { return 0; }"); // ')' がない
  expect_parse_fail("int main() { return 0; ");     // '}' がない
  expect_parse_fail("int main() { return 0 }");     // ';' がない
  expect_parse_fail("int main() { if (1) return 1; else }"); // else ブロック不正
  expect_parse_fail("int main() { int ; return 0; }");       // 変数名なし
  expect_parse_fail("int main() { int a[; return 0; }");     // 配列サイズ不正
  expect_parse_fail("int main() { int a[1 return 0; }");     // ']' がない
  expect_parse_fail("int main() { return (1+2; }");          // ')' がない
  expect_parse_fail("int main() { if 1) return 0; }");       // '(' がない
  expect_parse_fail("int main() { for (i=0 i<3; i=i+1) return 0; }"); // ';' 不足
  expect_parse_fail("int main() { ++1; }");                  // lvalueでない
  expect_parse_fail("int main() { 1++; }");                  // lvalueでない
  expect_parse_fail(
      "int main(void) { const int value=0; value++; return 0; }");
  expect_parse_fail(
      "typedef int *P; int main(void) { int x=0; const P p=&x; "
      "p=&x; return 0; }");
  expect_parse_fail(
      "struct S { const int c; }; int main(void) { "
      "struct S s={0}; s.c=1; return 0; }");
  expect_parse_fail(
      "int main(void) { struct S { int x; } value; value++; return 0; }");
  expect_parse_fail(
      "int main(void) { _Complex double value=0; ++value; return 0; }");
  expect_parse_ok("int main() { float f=1.0; ++f; }");       // C11: 浮動小数点の ++ は合法
  expect_parse_ok("int main() { double d=1.0; d--; }");      // C11: 浮動小数点の -- は合法
  expect_parse_fail("int main() { 1 += 2; }");               // lvalueでない
  expect_parse_fail("int main() { return; }");               // 非void関数で式なしreturn
  expect_parse_fail("void f() { return 1; }");           // void関数で値return
  expect_parse_fail("int main() { goto MISSING; return 0; }"); // 未定義ラベル
  expect_parse_fail("int main() { struct T x; return 0; }");   // 未定義タグ参照
  expect_parse_ok(
      "int main(void) { struct Forward (*p); (void)p; return 0; }");
  expect_parse_ok(
      "typedef struct Forward Forward; struct Holder { Forward *p; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(
      "struct Holder { struct Forward values[2]; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(
      "typedef int FunctionType(int); struct Holder { FunctionType f; }; "
      "int main(void) { return 0; }");
  expect_parse_fail("struct S; union S; int main(void) { return 0; }");
  expect_parse_fail("struct E {}; struct E {}; int main(void) { return 0; }");
  expect_parse_fail("struct S { int x; int x; }; int main(void) { return 0; }");
  expect_parse_fail(
      "struct S { int x; struct { int x; }; }; int main(void) { return 0; }");
  expect_parse_fail(
      "struct S { unsigned int x : 33; }; int main(void) { return 0; }");
  expect_parse_ok(
      "struct S { struct { int x; }; struct { int y; }; }; "
      "int main(void) { struct S s = {{1}, {2}}; return s.x + s.y; }");
  expect_parse_ok("typedef int X; typedef int X; int main(void) { return 0; }");
  expect_parse_fail("typedef int X; typedef long X; int main(void) { return 0; }");
  expect_parse_fail("int X; typedef int X; int main(void) { return 0; }");
  expect_parse_fail("int f(void); typedef int f; int main(void) { return 0; }");
  expect_parse_fail("enum E { X }; typedef int X; int main(void) { return 0; }");
  expect_parse_fail("int main(void) { int x; typedef int x; return 0; }");
  expect_parse_fail("int X; enum E { X }; int main(void) { return 0; }");
  expect_parse_fail("int f(void); enum E { f }; int main(void) { return 0; }");
  expect_parse_fail("typedef int T; enum E { T }; int main(void) { return 0; }");
  expect_parse_fail("int main(void) { int x; enum E { x }; return 0; }");
  expect_parse_ok("int X; int main(void) { enum E { X }; return 0; }");
  expect_parse_ok("typedef int T; int main(void) { enum E { T }; return 0; }");
  expect_parse_ok("int main() { { struct T { int x; }; } struct T *p; return 0; }"); // 外側スコープで新規前方宣言
  expect_parse_fail("int main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }"); // 非同種非スカラ型cast未対応
  expect_parse_fail("int main() { struct A { int x; }; struct B { int x; }; struct A a={1}; struct B b=a; return 0; }"); // 同サイズでも別タグは互換structではない
  expect_parse_fail("int main() { short double x; return 0; }");   // 不正な型指定子組み合わせ
  expect_parse_fail("int main() { _Complex int x; return 0; }");   // 浮動小数型以外との組み合わせは不正
  expect_parse_fail("int main() { _Imaginary int x; return 0; }"); // 浮動小数型以外との組み合わせは不正
  expect_parse_fail("int main() { return (_Thread_local int)1; }"); // cast型名のストレージ指定は未対応
  expect_parse_fail("int main() { int a[-1]; return 0; }");         // 配列サイズは負数不可
  expect_parse_fail("int main() { return sizeof(int[-1]); }");      // sizeof 型名も同じ制約
  expect_parse_fail("int main() { return _Generic(1, float:2); }"); // 一致なし + defaultなし
  expect_parse_fail(
      "int main() { return _Generic(1, int:1, signed int:2, default:3); }");
  expect_parse_fail(
      "int main() { return _Generic(1, int:1, default:2, default:3); }");
  expect_parse_fail("int bad(int a, ..., int b) { return 0; }"); // ... は末尾のみ
  expect_parse_fail("int bad(int) { return 0; }"); // 関数定義の仮引数には名前が必要
  expect_parse_fail("int main() { _Static_assert(0, \"ng\"); return 0; }"); // static_assert失敗
  expect_parse_fail("int main() { _Static_assert(x, \"ng\"); return 0; }"); // 非定数式
  expect_parse_fail(
      "struct SA { _Static_assert(0, \"ng\"); int x; }; int main(void){return 0;}");
  expect_parse_fail("int main() { int x; x.y=1; }");            // 非構造体への .
  expect_parse_fail("int main() { int *p; p->y=1; }");          // 非構造体ポインタへの ->
  expect_parse_fail("int main() { int x; int *p=&x; return *(void *)p; }"); // void* deref
  expect_parse_fail("int main(void) { int *p=0; return -p; }");
  expect_parse_fail("int main(void) { int *p=0; return __real__ p; }");
  expect_parse_fail("int main(void) { int value=0; return value(); }");
  expect_parse_fail("int main(void) { int (**pp)(void)=0; return pp(); }");
  expect_parse_fail("int main(void) { struct S { int bits:3; } s; return &s.bits != 0; }");
  expect_parse_fail("int main() { break; }");                // ループ/switch外
  expect_parse_fail("int main() { continue; }");             // ループ外
  expect_parse_fail("int main(void) { int x = ({ continue; 0; }); return x; }");
  expect_parse_fail("int main(void) { while (({ continue; 1; })) return 0; return 0; }");
  expect_parse_fail("int main() { case 1: return 0; }");     // switch外のcase
  expect_parse_fail("int main() { default: return 0; }");    // switch外のdefault
  expect_parse_fail("int main() { switch (1) { case 1: 0; case 1: 0; } }"); // case 重複
  expect_parse_fail("int main() { switch (0) { case 1+2: 0; case 3: 0; } }"); // 定数式評価後のcase重複
  expect_parse_fail("int main() { switch (1) { default: 0; default: 1; } }"); // default 重複
  expect_parse_fail("enum E { A = 2147483648 }; int main(void){ return 0; }"); // enum定数はint幅
  expect_parse_fail("int main() { int x={1,2}; return x; }"); // スカラ波括弧初期化は単一要素のみ
  expect_parse_fail("int main() { int a[2]={1,2,3}; return 0; }"); // 配列初期化子過多
  expect_parse_fail("int main() { struct S { int x; }; struct S s=1; return 0; }"); // 構造体単一式初期化は未対応
  expect_parse_fail("int main() { struct S { int x; }; struct S t={1}; struct S s=(t,1); return 0; }"); // 最終値が同型オブジェクトでない
  expect_parse_fail("int main() { union U { int x; char y; }; union U u={1,2}; return 0; }"); // 共用体は1要素のみ
  expect_parse_fail("int main() { union U { int x; char y; }; union U u={.x=1,2}; return 0; }"); // designatedでも1要素のみ
  expect_parse_fail("int main() { int a[2]={[3]=1}; return 0; }"); // array designator 範囲外
  // C11 6.7.9p19: 同一 subobject への複数指定初期化子は後勝ちで受理される。
  expect_parse_ok("int main() { struct S { int x; int y; }; struct S s={.x=1,.x=2}; return 0; }"); // struct重複designator (後勝ち)
  expect_parse_ok("int main() { struct __BraceDup { int a[2]; int z; }; struct __BraceDup s={1,2,.a={3,4}}; return 0; }"); // brace elision後の上書き
  expect_parse_ok("int main() { int a[2]={[0]=1,[0]=2}; return 0; }"); // array重複designator (後勝ち)
}

static void test_parse_invalid_diagnostics() {
  printf("test_parse_invalid_diagnostics...\n");
  expect_parse_fail_with_message("int main() { goto MISSING; return 0; }", "[goto] 未定義ラベル 'MISSING'");
  expect_parse_fail_with_message("int main() { L1: return 0; L1: return 1; }", "識別子が重複しています (ラベル): 'L1'");
  expect_parse_fail_with_message("int main() { struct T x; return 0; }", "不完全型のオブジェクトは宣言できません");
  expect_parse_fail_with_message("int main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }", "[cast] struct 値へのキャストは未対応です（型不整合）");
  expect_parse_fail_with_message("int main() { union U { int x; char y; }; struct S { int z; } s={1}; return (union U)s; }", "[cast] union 値へのキャストは未対応です（型不整合）");
  expect_parse_fail_with_message("int main() { struct A { int x; }; struct B { int x; }; struct A a={1}; struct B b=a; return 0; }", "[decl] 構造体の単一式初期化は同型オブジェクトのみ対応です");
  expect_parse_fail_with_message("int main() { struct S { int x; }; struct S s=1; return 0; }", "[decl] 構造体の単一式初期化は同型オブジェクトのみ対応です");
  expect_parse_fail_with_message("int main() { struct S { int x; }; struct S t={1}; struct S s=(t,1); return 0; }", "[decl] 構造体の単一式初期化は同型オブジェクトのみ対応です");
  expect_parse_fail_with_message("int main() { struct S { int x; }; int y=(0,(struct S){1}); return y; }", "スカラ変数を struct 値で初期化できません");
  expect_parse_fail_with_message("int main() { union U { int x; char y; }; union U u={1,2}; return 0; }", "[init] 共用体初期化子は現状1要素のみ対応です");
  expect_parse_fail_with_message("int main() { union U { int x; char y; }; union U u={.x=1,2}; return 0; }", "[init] 共用体初期化子は現状1要素のみ対応です");
  expect_parse_fail_with_message("int main() { _Complex int x; return 0; }", "_Complex/_Imaginary は浮動小数型にのみ指定できます");
  expect_parse_fail_with_message("int main() { return (_Complex int)1; }", "_Complex/_Imaginary cast は浮動小数型のみ対応です");
  expect_parse_fail_with_message("int main() { return (_Thread_local int)1; }", "[cast] cast 型名にストレージ指定子は使えません");
  expect_parse_fail_with_message("int main() { struct __IncOnly; struct __HasInc { struct __IncOnly m; }; return 0; }", "[decl] 不完全型のメンバは定義できません");
  expect_parse_fail_with_message("int main() { struct T { int f(int); }; return 0; }", "[decl] 関数型のメンバは定義できません");
  expect_parse_fail_with_message(
      "int main() { struct S { int *p:1; }; return 0; }",
      "bit-field has non-integer canonical type");
  expect_parse_fail("int main() { struct S { int a[2]:1; }; return 0; }");
  expect_parse_fail("int main() { struct S { float f:1; }; return 0; }");
  expect_parse_fail_with_message("int bad(int) { return 0; }", "必要な項目がありません: 仮引数");
  expect_parse_fail_with_message("int main() { int x; int *p=&x; return *(void *)p; }",
                                 "void* の deref はできません");
  expect_parse_fail_with_message("void f(void); int main(void){ int x; x=f(); return 0; }",
                                 "void 戻り値関数の結果は代入/初期化に使えません");
  expect_parse_fail_with_message("void f(void); int main(void){ void (*fp)(void)=f; int x; x=fp(); return 0; }",
                                 "void 戻り値関数の結果は代入/初期化に使えません");

  // 汎用cast未対応診断（"この型へのキャストは未対応です"）は現状到達しないことを固定する。
  expect_parse_fail_without_message("int main() { return (_Thread_local int)1; }", "[cast] この型へのキャストは未対応です");
  expect_parse_fail_without_message("int main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }", "[cast] この型へのキャストは未対応です");

  // Parser拡張設定: 同種同サイズの非スカラcast受理を無効化できること。
  test_compilation_options()->enable_size_compatible_nonscalar_cast = false;
  expect_parse_fail_with_message(
      "int main() { struct A { int x; }; struct B { int x; }; struct A a={7}; return ((struct B)a).x; }",
      "[cast] struct 値へのキャストは未対応です（型不整合）");
  expect_parse_fail_with_message(
      "struct S { int x; }; int main() { struct S outer={1}; { struct S { int y; }; return ((struct S)outer).y; } }",
      "[cast] struct 値へのキャストは未対応です（型不整合）");
  test_compilation_options()->enable_size_compatible_nonscalar_cast = true;

  // Parser拡張設定: struct への scalar/pointer cast 受理を無効化できること。
  test_compilation_options()->enable_struct_scalar_pointer_cast = false;
  expect_parse_fail_with_message(
      "int main() { struct S { int x; }; int a=0; return (struct S)a; }",
      "[cast] struct への scalar/pointer cast は設定で無効です");
  test_compilation_options()->enable_struct_scalar_pointer_cast = true;

  // Parser拡張設定: union への scalar/pointer cast 受理を無効化できること。
  test_compilation_options()->enable_union_scalar_pointer_cast = false;
  expect_parse_fail_with_message(
      "int main() { union U { int x; char y; }; int a=0; return (union U)a; }",
      "[cast] union への scalar/pointer cast は設定で無効です");
  test_compilation_options()->enable_union_scalar_pointer_cast = true;

  // Parser拡張設定: union 先頭配列メンバの非波括弧初期化受理を無効化できること。
  test_compilation_options()->enable_union_array_member_nonbrace_init = false;
  expect_parse_fail_with_message(
      "int main() { union U { int a[2]; int z; }; union U u={1,2}; return 0; }",
      "[decl] 共用体の配列メンバ非波括弧初期化は設定で無効です");
  test_compilation_options()->enable_union_array_member_nonbrace_init = true;

  // 入れ子 designator: 非配列メンバに .member[idx]=val は診断エラー
  expect_parse_fail_with_message("int main() { struct S { int x; }; struct S s={.x[0]=3}; return 0; }",
                                 "入れ子designatorの対象が配列メンバではありません");

  // decl.c の「1/2/4/8 byte スカラのみ」診断は、現行型セットでは到達不能であることを固定する。
  // 将来 16-byte などの新スカラ型導入時は、ここを陽性診断テストへ置き換える。
  expect_parse_fail_without_message("int main() { struct __IncOnly; struct __HasInc { struct __IncOnly m; }; return 0; }",
                                    "[decl] 構造体/共用体初期化は現在 1/2/4/8 byte スカラのみ対応です");
  expect_parse_fail_without_message("int main() { struct T { int f(int); }; return 0; }",
                                    "[decl] 構造体/共用体初期化は現在 1/2/4/8 byte スカラのみ対応です");
}

// 意地悪テスト: パーサーの境界ケース
static void test_parse_evil_edge_cases() {
  printf("test_parse_evil_edge_cases...\n");

  // ネストした三項演算子: a?b?c:d:e は a?(b?c:d):e
    node_t *tn = parse_expr_input("1 ? 2 ? 3 : 4 : 5");
  ASSERT_EQ(ND_TERNARY, tn->kind);
  ASSERT_EQ(1, as_num(tn->lhs)->val);         // 条件: 1
  ASSERT_EQ(ND_TERNARY, tn->rhs->kind);       // then: 2?3:4
  ASSERT_EQ(2, as_num(tn->rhs->lhs)->val);    // 内側条件: 2
  ASSERT_EQ(3, as_num(tn->rhs->rhs)->val);    // 内側then: 3
  ASSERT_EQ(4, as_num(as_ctrl(tn->rhs)->els)->val); // 内側else: 4
  ASSERT_EQ(5, as_num(as_ctrl(tn)->els)->val);      // 外側else: 5

  // 複雑な優先順位: 1+2*3==7&&1||0 → (((1+(2*3))==7)&&1)||0
    node_t *cp = parse_expr_input("1+2*3==7&&1||0");
  ASSERT_EQ(ND_LOGOR, cp->kind);
  ASSERT_EQ(0, as_num(cp->rhs)->val);         // ||0
  ASSERT_EQ(ND_LOGAND, cp->lhs->kind);
  ASSERT_EQ(1, as_num(cp->lhs->rhs)->val);    // &&1
  ASSERT_EQ(ND_EQ, cp->lhs->lhs->kind);       // ==
  ASSERT_EQ(7, as_num(cp->lhs->lhs->rhs)->val); // ==7
  ASSERT_EQ(ND_ADD, cp->lhs->lhs->lhs->kind); // 1+...
  ASSERT_EQ(ND_MUL, cp->lhs->lhs->lhs->rhs->kind); // 2*3

  // ビット演算と論理演算の優先順位: 1&2|3^4
  // → (1&2) | (3^4) → ND_BITOR( ND_BITAND(1,2), ND_BITXOR(3,4) )
    node_t *bw = parse_expr_input("1&2|3^4");
  ASSERT_EQ(ND_BITOR, bw->kind);
  ASSERT_EQ(ND_BITAND, bw->lhs->kind);
  ASSERT_EQ(1, as_num(bw->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(bw->lhs->rhs)->val);
  ASSERT_EQ(ND_BITXOR, bw->rhs->kind);
  ASSERT_EQ(3, as_num(bw->rhs->lhs)->val);
  ASSERT_EQ(4, as_num(bw->rhs->rhs)->val);

  // x+++y の式解析: (x++) + y
  parsed_code = parse_program_input("int main() { int x=1; int y=2; return x+++y; }");
  // パースが成功すればOK

  // キャストと単項マイナスのネスト: (int)-(char)5
    node_t *cn = parse_expr_input("(int)-(char)5");
  // (int)(0-(char)5) → ND_CAST(ND_SUB(0, ND_CAST(5)))のような構造
  // パースが壊れずに完了することを確認
  ASSERT_TRUE(cn != NULL);

  // シフトと比較の優先順位: 1<<2<8 → (1<<2)<8
    node_t *sh = parse_expr_input("1<<2<8");
  ASSERT_EQ(ND_LT, sh->kind);
  ASSERT_EQ(TK_LT, sh->source_op);
  ASSERT_EQ(ND_SHL, sh->lhs->kind);
  ASSERT_EQ(1, as_num(sh->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(sh->lhs->rhs)->val);
  ASSERT_EQ(8, as_num(sh->rhs)->val);

  // `>`/`>=` は AST では `<`/`<=` へ正規化されるが、後段 warning 用に元演算子を保持する。
  node_t *gt = parse_expr_input("1>2");
  ASSERT_EQ(ND_LT, gt->kind);
  ASSERT_EQ(TK_GT, gt->source_op);
  ASSERT_EQ(2, as_num(gt->lhs)->val);
  ASSERT_EQ(1, as_num(gt->rhs)->val);
  node_t *ge = parse_expr_input("1>=2");
  ASSERT_EQ(ND_LE, ge->kind);
  ASSERT_EQ(TK_GE, ge->source_op);
  ASSERT_EQ(2, as_num(ge->lhs)->val);
  ASSERT_EQ(1, as_num(ge->rhs)->val);

  // カンマ演算子と代入の優先順位: a=1,b=2 → (a=1),(b=2)
  parsed_code = parse_program_input("int main() { int a; int b; a=1,b=2; }");
  // パースが成功すればOK

  // 複雑な式文のパース
  expect_parse_ok("int main() { int x; x = 1 + 2 * 3 - 4 / 2 + (5 % 3); return x; }");
  expect_parse_ok_without_message("int main() { int x; *(&x) = 1; return x; }", "W3004");
  expect_parse_ok_without_message(
      "typedef _Atomic int atomic_int; int main(void){ atomic_int x; ((void)(*(&x)=10)); return x; }",
      "W3004");
  expect_parse_ok_without_message(
      "int main(void){ struct S { int a; int b; }; struct S s; s.a=2; s.b=5; return s.a+s.b; }",
      "W3004");
  expect_parse_ok_without_message(
      "int main(void){ struct B { unsigned a:3; unsigned b:5; }; struct B s; s.a=5; s.b=10; return s.a; }",
      "W3004");
  expect_parse_ok_without_message("int main(void){ int x; int *p=&x; return p==0; }", "W3004");
  expect_parse_ok_without_message("int main(void){ int x; int *p=&x; return p==0; }", "W3003");
  expect_parse_ok_without_message("int main(void){ int x; x=1; return 0; }", "W3003");
  expect_parse_ok_without_message("int main(void){ int x; return &x==0; }", "W3003");
  expect_parse_ok_without_message("int main(void){ int x; return &x==0; }", "W3004");
  expect_parse_ok_with_message("int main(void){ int x=1; return 0; }", "W3003");
  expect_parse_ok_with_message("int main(void){ int x; x=x; return 0; }", "W3012");
  expect_parse_ok_with_message("int main(void){ int x=1.5; return 0; }", "W3010");
  expect_parse_ok_with_message("int main(void){ int x; x=1.5; return 0; }", "W3010");
  expect_parse_ok_with_message("int main(void){ return 1.5; }", "W3010");
  expect_parse_ok_with_message("int main(void){ int x=1; return x==x; }", "W3013");
  expect_parse_ok_with_message("int main(void){ int x=1; return x&&x; }", "W3020");
  expect_parse_ok_with_message("int main(void){ unsigned int u=1; int s=-1; return s<u; }",
                               "W3018");
  expect_parse_ok_without_message("int main(void){ unsigned int u=1; long s=-1; return s<u; }",
                                  "W3018");
  expect_parse_ok_without_message(
      "int main(void){ unsigned char u=1; int s=-1; return s<u; }", "W3018");
  expect_parse_ok_with_message(
      "int main(void){ unsigned long u=1; long s=-1; return s<u; }", "W3018");
  expect_parse_ok_with_message("int main(void){ unsigned int u=1; return u>=0; }", "W3019");
  expect_parse_ok_with_message("int main(void){ unsigned int u=1; return 0>u; }", "W3019");
  expect_parse_ok_with_message("int main(void){ unsigned char u=1; return u<0; }", "W3019");
  expect_parse_ok_with_message("int main(void){ unsigned short u=1; return 0<=u; }", "W3019");
  expect_parse_ok_with_message(
      "unsigned char f(void){ return 1; } int main(void){ return f()<0; }", "W3019");
  expect_parse_ok_with_message("int main(void){ int x=1; return (unsigned char)x<0; }",
                               "W3019");
  expect_parse_ok_without_message("int main(void){ signed char s=1; return s<0; }", "W3019");
  expect_parse_ok_with_message("int main(void){ int *p; return p==5; }", "W3022");
  expect_parse_ok_with_message("int main(void){ int x=1; return !x==0; }", "W3021");
  expect_parse_ok_with_message("int main(void){ int x=0; if (x=1) return x; return 0; }",
                               "W3007");
  expect_parse_ok_with_message("int main(void){ int x=0; while (x=1) return x; return 0; }",
                               "W3007");
  expect_parse_ok_with_message("int main(void){ int x=0; if (x,1) return x; return 0; }",
                               "W3008");
  expect_parse_ok_with_message("int main(void){ int x=0; if (x); return x; }", "W3009");
  expect_parse_ok_with_message("int *f(void){ int x=0; return &x; }", "W3006");
  expect_parse_ok_without_message("int *f(void){ static int x; return &x; }", "W3006");
  expect_parse_ok_without_message("int g; int *f(void){ return &g; }", "W3006");
  expect_parse_ok_with_message(
      "int main(void){ int x=0; switch(x){ case 0: x=1; case 1: return x; } return 0; }",
      "W3017");
  expect_parse_ok_without_message(
      "int main(void){ int x=0; switch(x){ case 0: x=1; break; case 1: return x; } return 0; }",
      "W3017");
  expect_parse_ok_without_message(
      "int main(void){ int x=0; switch(x){ case 0: case 1: return 1; } return x; }",
      "W3017");
  expect_parse_ok_with_message("int main(void){ char c=200; return c; }", "W3011");
  expect_parse_ok_with_message("int main(void){ unsigned char c=300; return c; }", "W3011");
  expect_parse_ok_without_message("int main(void){ unsigned char c=-1; return c; }", "W3011");
  expect_parse_ok_without_message("int main(void){ _Bool b=300; return b; }", "W3011");
  expect_parse_ok_with_message("int main(void){ return 2147483647 + 1; }", "W3023");
  expect_parse_ok_with_message("int main(void){ return 2147483647 * 2; }", "W3023");
  expect_parse_ok_without_message("int main(void){ return 2147483647L + 1L; }", "W3023");
  expect_parse_ok_without_message("int main(void){ int a[4]; int *p=a; return *(p + 2147483647); }",
                                  "W3023");
  expect_parse_ok_with_message("int main(void){ return 1 << 32; }", "W3014");
  expect_parse_ok_with_message("long main(void){ return 1L << 64; }", "W3014");
  expect_parse_ok_with_message("int main(void){ return 1 / 0; }", "W3015");
  expect_parse_ok_with_message("int main(void){ return 1 % 0; }", "W3015");
  expect_parse_ok_with_message("int main(void){ return f(); } int f(void){ return 1; }", "W3016");
  expect_parse_ok_without_message("int f(void); int main(void){ return f(); }", "W3016");
  expect_parse_fail_with_message("main(void){ return 0; }", "E3088");
  expect_parse_ok_without_message("int main(void){ return 0; }", "W3001");
  expect_parse_ok_without_message("int main(void){ int x=2.0; return 0; }", "W3010");
  expect_parse_ok_without_message("int main(void){ int x; x=2.0; return 0; }", "W3010");
  expect_parse_ok_without_message("double main(void){ return 1.5; }", "W3010");
  expect_parse_ok_without_message("int f(int x){ return x; } int main(void){ return f(3); }", "W3004");
  expect_parse_ok_without_message("int main(void){ int x=7; return x; }", "W3004");
  expect_parse_ok_without_message("int main(void){ static int x; return x; }", "W3004");
  expect_parse_ok("int main(void){ while(1){ ({ continue; 0; }); } return 0; }");
  expect_parse_ok("int main(void){ int x=0; switch(x){ case 0: ({ break; 0; }); } return 0; }");
  expect_parse_ok("int main(void){ int x=0; switch(x){ case (0 ? 1 : 2147483648): return 1; } return 0; }");
  expect_parse_ok("int main(void){ int x=0; switch(x){ case 1: switch(x){ case 1: return 1; } return 0; } return 0; }");
  expect_parse_ok_without_message(
      "int main(void){ struct S { char c; int i; }; struct S s; long o=(char*)&s.i-(char*)&s; return o>0; }",
      "W3004");
  expect_parse_ok_with_message("int main(void){ int *p; return &*p == 0; }", "W3004");
  expect_parse_ok_with_message("int main(void){ goto L; int x; return x; L: return 0; }", "W3002");
  expect_parse_ok_without_message("int main(void){ goto L; int x; return x; L: return 0; }", "W3004");
  expect_parse_ok_without_message("int main(void){ goto L; int x; return x; L: return 0; }", "W3003");
  expect_parse_ok_with_message("int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3002");
  expect_parse_ok_without_message("int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3003");
  expect_parse_ok_without_message("int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3004");
  expect_parse_ok("int main() { int a; int b; int c; a = b = c = 42; return a; }");
  expect_parse_ok("int main() { return 1?2:3?4:5?6:7; }");
  expect_parse_ok("int main() { int x=1; return x<<1|x<<2|x<<3; }");
  expect_parse_ok("int main() { int x=1; return !!!!!x; }");
  expect_parse_ok("int main() { int x=255; return ~~~x; }");
  expect_parse_ok("struct S { int x; }; int f(struct S (*p)) { return p->x; } int main() { struct S s={3}; return f(&s); }");

  // sizeof内の型
  expect_parse_ok("int main() { return sizeof(int); }");
  expect_parse_ok("int main() { return sizeof(int*); }");
  expect_parse_ok("int main() { return sizeof(int (*[3])(int)); }");
  expect_parse_ok("int main() { return sizeof(int (*[2])[3]); }");
  expect_parse_ok("int main() { return sizeof(int (*(*[2])[3])); }");
  expect_parse_ok("int main() { return sizeof(int (*(*)(void))[3]); }");
  expect_parse_ok("int main() { return sizeof(int (*(*[2])(void))[3]); }");
  expect_parse_ok("int main() { return sizeof(int (*(*)(void))(int)); }");
  expect_parse_ok("int main() { return sizeof(int (*(*(*)(void))(int))[3]); }");
  expect_parse_ok("int main() { return _Generic((int (*)(int))0, int (*[3])(int): 1, default: 2); }");
  expect_parse_ok("int main() { return _Generic((int (*(*)(void))[3])0, int (*(*)(void))[3]: 1, default: 2); }");

  // 関数宣言のプロトタイプ
  expect_parse_ok("int f(int a, int b, int c); int main() { return f(1,2,3); }");
  expect_parse_ok("int (f)(int x) { return x; } int main() { return f(42); }");
  expect_parse_ok("int (*f(void))(int) { return 0; } int main() { return 0; }");
  expect_parse_ok("int (*f(int n))(int) { return 0; } int main() { return 0; }");
  expect_parse_ok("int (*(*f(void))(int))[3] { return 0; } int main() { return 0; }");
  expect_parse_ok("struct S { int x; } (f)(void){ struct S s; s.x=3; return s; } int main(){ return f().x; }");
  expect_parse_ok("union U { int x; } (f)(void){ union U u; return u; } int main(){ return 0; }");
  expect_parse_ok("int g=1; _Static_assert(sizeof(int)==4, \"ok\"); int main(){ return g; }");
  expect_parse_ok("typedef int myint; _Static_assert(1, \"ok\"); myint g=1; int main(){ return g; }");
  expect_parse_ok("typedef int myint; _Static_assert(sizeof(myint)==4, \"ok\"); int main(){ return 0; }");
  expect_parse_ok("typedef int A3[3]; _Static_assert(sizeof(A3)==12, \"ok\"); int main(){ return 0; }");

  // for文の複雑な初期化
  expect_parse_ok("int main() { int i; int s=0; for(i=0; i<10; i=i+1) s=s+i; return s; }");

  // do-while の後に式文
  expect_parse_ok("int main() { int x=0; do { x=x+1; } while(x<3); return x; }");

  // switch内のfall-through
  expect_parse_ok("int main() { int x=2; int r=0; switch(x) { case 1: r=10; case 2: r=r+20; case 3: r=r+30; default: r=r+1; } return r; }");

  // ネストしたブロック
  expect_parse_ok("int main() { { { { int x=42; return x; } } } }");

  // 意地悪テスト: 宣言・型の境界ケース

  // typedefで作った型名の使用
  expect_parse_ok("typedef int myint; myint add(myint a, myint b) { return a+b; } int main() { return add(20,22); }");
  expect_parse_ok("struct S { int x; } f(void){ struct S s; s.x=3; return s; } int main(){ return f().x; }");
  expect_parse_ok("union U { int x; } f(void){ union U u; return u; } int main(){ return 0; }");
  expect_parse_ok("typedef struct S S; struct S { int x; }; int main(){ S s; s.x=7; return s.x; }");
  expect_parse_ok("typedef struct { int x; } S; int main(){ S s; s.x=5; return s.x; }");
  expect_parse_ok("typedef union U U; union U { int x; }; int main(){ U u; u.x=8; return u.x; }");
  expect_parse_ok("typedef union { int x; } U; int main(){ U u; u.x=6; return u.x; }");
  expect_parse_ok("typedef int (*(*arr_t)[2])(int); int main() { arr_t p; return 0; }");
  expect_parse_ok("int main(){ typedef int (*(*fp_t))(int); return 0; }");
  expect_parse_ok("int main(){ typedef struct L L; return 0; }");
  expect_parse_ok("int main(){ typedef struct { int y; } L; L l; l.y=9; return l.y; }");
  expect_parse_ok("int main(){ typedef union L L; return 0; }");
  expect_parse_ok("int main(){ typedef union { int y; } L; L l; l.y=4; return l.y; }");
  expect_parse_ok("int main(){ typedef int A[]; A *p=0; return p==0; }");
  expect_parse_ok("int main(){ extern int (*fp)(int); return 0; }");
  expect_parse_ok("int main(){ extern int (*arr[2])(int); return 0; }");
  expect_parse_ok("int main(){ extern int a[]; return 0; }");
  expect_parse_ok("int (*arr[2])(int); int main(){ return 0; }");
  expect_parse_ok("int main(){ int (*arr[2])(int); return 0; }");

  // 複数の変数宣言（カンマ区切り）
  expect_parse_ok("int main() { int a=1, b=2, c=3; return a+b+c; }");

  // 関数ポインタ宣言
  expect_parse_ok("int add(int a, int b) { return a+b; } int main() { int (*f)(int,int) = add; return f(20,22); }");
  expect_parse_ok("int inc(int x){return x+1;} int apply(int (**pp)(int), int x){ return (*pp)(x); } int main(){ int (*p)(int)=inc; int (**pp)(int)=&p; return apply(pp,41); }");
  expect_parse_ok("int main(){ int (*(*pp))(int); return 0; }");
  expect_parse_ok("int main() { struct S { int (*fp)(int); }; return 0; }");
  expect_parse_ok("int main() { struct S { int (*arr[2])(int); }; return 0; }");

  // enumの値パース
  expect_parse_ok("int main() { enum Color { RED, GREEN, BLUE }; enum Color c = GREEN; return c; }");
  // 匿名enumの値指定は既知のバグ（enum初期化子パース未対応）で現在パースエラー
  // expect_parse_ok("int main() { enum { A=10, B=20, C=30 }; return B; }");

  // 構造体の前方参照と自己参照ポインタ
  // 自己参照ポインタメンバは現在パースエラー（不完全型ポインタ未対応の可能性）
  // expect_parse_ok("int main() { struct Node { int val; struct Node *next; }; struct Node n; n.val=42; n.next=0; return n.val; }");

  // void* の宣言と使用
  expect_parse_ok("int main() { void *p = 0; return p == 0 ? 42 : 0; }");

  // const修飾
  expect_parse_ok("int main() { const int x = 42; return x; }");
  expect_parse_ok("static const char *__const_leak_roots[]={\"\"}; typedef struct __ConstLeakFrame __ConstLeakFrame; struct __ConstLeakFrame{__ConstLeakFrame *next; const char *path;}; static __ConstLeakFrame *__const_leak_g; void f(void){ __ConstLeakFrame *p=0; __const_leak_g=p; }");
  expect_parse_ok("static const char *__const_ptr_tbl[4]; void f(const char *name){ __const_ptr_tbl[0]=name; }");
  expect_parse_ok("struct __ConstMemberPtr{const char *path;}; void f(struct __ConstMemberPtr *m,const char *path){ m->path=path; }");
  expect_parse_ok("struct __PtrSubSym814{char *name; int len;}; struct __PtrSubData814{struct __PtrSubSym814 *symbols;}; static struct __PtrSubData814 __ptr_sub_g814; int main(void){__ptr_sub_g814.symbols[0].name=\"main\";return __ptr_sub_g814.symbols[0].name[0];}");
  // 後置const (int const x) は変数宣言で現在パースエラー
  // expect_parse_ok("int main() { int const x = 42; return x; }");

  // 配列の宣言と初期化
  expect_parse_ok("int main() { int a[3] = {1, 2, 3}; return a[0] + a[1] + a[2]; }");

  // 構造体のネストした初期化
  expect_parse_ok("int main() { struct P { int x; int y; }; struct R { struct P p; int z; }; struct R r = {{1,2},3}; return r.p.x + r.p.y + r.z; }");

  // for文のスコープ付き変数宣言
  expect_parse_ok("int main() { int s=0; for (int i=0; i<5; i=i+1) s=s+i; return s; }");

  // 構造体のサイズオフ
  expect_parse_ok("int main() { struct S { char a; int b; char c; }; return sizeof(struct S); }");

  // union の基本使用
  expect_parse_ok("int main() { union U { int x; char c; }; union U u; u.x=42; return u.x; }");

  // _Static_assert 正常系
  // _Static_assert with sizeof==4 — 定数式評価で==未対応の可能性
  // expect_parse_ok("_Static_assert(sizeof(int)==4, \"int is 4 bytes\"); int main() { return 42; }");

  // _Generic の複雑なケース
  expect_parse_ok("int main() { double d=1.0; return _Generic(d, int:0, double:42, default:99); }");

  // 複合リテラルの使用 — 現在パースエラーの可能性があるため個別検証
  //expect_parse_ok("int main() { struct P { int x; int y; }; struct P p = (struct P){10, 32}; return p.x + p.y; }");

  // 意地悪テスト: 異常系の追加
  // 自己参照は不完全型エラーにならない（ポインタ非ポインタを問わずパース通過する可能性）
  // expect_parse_fail("int main() { struct S { int x; struct S s; }; return 0; }");
  // 負のサイズは現在エラーにならない
  // expect_parse_fail("int main() { int a[-1]; return 0; }");
}

static void test_parser_config_matrix() {
  printf("test_parser_config_matrix...\n");
  const char *struct_scalar_cast = "int main() { struct S { int x; int y; }; return ((struct S)7).x; }";
  const char *struct_pointer_cast = "int main() { struct S { int *p; int q; }; int x=3; return *((struct S)&x).p; }";
  const char *union_scalar_cast = "int main() { union U { int x; char y; }; return ((union U)7).x; }";
  const char *union_pointer_cast = "int main() { union U { int *p; int q; }; int x=3; return ((union U)&x).p==&x; }";
  const char *union_nonbrace_init = "int main() { union U { int a[2]; int z; }; union U u={1,2}; return 0; }";
  const char *same_size_nonscalar_cast =
      "int main() { struct A { int x; }; struct B { int x; }; struct A a={7}; return ((struct B)a).x; }";

  // baseline: all extensions enabled
  test_compilation_options()->enable_struct_scalar_pointer_cast = true;
  test_compilation_options()->enable_union_scalar_pointer_cast = true;
  test_compilation_options()->enable_union_array_member_nonbrace_init = true;
  test_compilation_options()->enable_size_compatible_nonscalar_cast = true;
  expect_parse_ok(struct_scalar_cast);
  expect_parse_ok(struct_pointer_cast);
  expect_parse_ok(union_scalar_cast);
  expect_parse_ok(union_pointer_cast);
  expect_parse_ok(union_nonbrace_init);
  expect_parse_ok(same_size_nonscalar_cast);

  // all extensions disabled: all extension snippets should fail
  test_compilation_options()->enable_struct_scalar_pointer_cast = false;
  test_compilation_options()->enable_union_scalar_pointer_cast = false;
  test_compilation_options()->enable_union_array_member_nonbrace_init = false;
  test_compilation_options()->enable_size_compatible_nonscalar_cast = false;
  expect_parse_fail(struct_scalar_cast);
  expect_parse_fail(struct_pointer_cast);
  expect_parse_fail(union_scalar_cast);
  expect_parse_fail(union_pointer_cast);
  expect_parse_fail(union_nonbrace_init);
  expect_parse_fail(same_size_nonscalar_cast);

  // restore defaults for subsequent tests
  test_compilation_options()->enable_struct_scalar_pointer_cast = true;
  test_compilation_options()->enable_union_scalar_pointer_cast = true;
  test_compilation_options()->enable_union_array_member_nonbrace_init = true;
  test_compilation_options()->enable_size_compatible_nonscalar_cast = true;
}

static char *build_nested_paren_program(int depth) {
  if (depth <= 0) return NULL;
  size_t cap = (size_t)depth * 2 + 64;
  char *buf = calloc(cap, 1);
  if (!buf) return NULL;
  size_t pos = 0;
  pos += (size_t)snprintf(buf + pos, cap - pos, "int main(){ return ");
  for (int i = 0; i < depth; i++) buf[pos++] = '(';
  buf[pos++] = '1';
  for (int i = 0; i < depth; i++) buf[pos++] = ')';
  pos += (size_t)snprintf(buf + pos, cap - pos, "; }\n");
  return buf;
}

static void test_expr_nest_limits() {
  printf("test_expr_nest_limits...\n");
  char *ok = build_nested_paren_program(256);
  ASSERT_TRUE(ok != NULL);
  expect_parse_ok(ok);
  free(ok);

  char *too_deep = build_nested_paren_program(1300);
  ASSERT_TRUE(too_deep != NULL);
  expect_parse_fail_with_message(too_deep, "深すぎます");
  free(too_deep);
}

static char *build_many_declarators_program(int ndecls) {
  if (ndecls <= 0) return NULL;
  size_t cap = (size_t)ndecls * 16 + 64;
  char *buf = calloc(cap, 1);
  if (!buf) return NULL;
  size_t pos = 0;
  pos += (size_t)snprintf(buf + pos, cap - pos, "int main(){ int ");
  for (int i = 0; i < ndecls; i++) {
    int n = snprintf(buf + pos, cap - pos, i == 0 ? "v%d" : ",v%d", i);
    if (n < 0 || (size_t)n >= cap - pos) {
      free(buf);
      return NULL;
    }
    pos += (size_t)n;
  }
  snprintf(buf + pos, cap - pos, "; return 0; }\n");
  return buf;
}

static char *build_many_array_init_elements_program(int nelems) {
  if (nelems <= 0) return NULL;
  size_t cap = (size_t)nelems * 4 + 128;
  char *buf = calloc(cap, 1);
  if (!buf) return NULL;
  size_t pos = 0;
  pos += (size_t)snprintf(buf + pos, cap - pos, "int main(){ int a[%d]={", nelems);
  for (int i = 0; i < nelems; i++) {
    int n = snprintf(buf + pos, cap - pos, i == 0 ? "1" : ",1");
    if (n < 0 || (size_t)n >= cap - pos) {
      free(buf);
      return NULL;
    }
    pos += (size_t)n;
  }
  snprintf(buf + pos, cap - pos, "}; return a[0]; }\n");
  return buf;
}

static void test_parser_width_limits() {
  printf("test_parser_width_limits...\n");

  char *ok_decls = build_many_declarators_program(64);
  ASSERT_TRUE(ok_decls != NULL);
  expect_parse_ok(ok_decls);
  free(ok_decls);

  char *too_many_decls = build_many_declarators_program(1300);
  ASSERT_TRUE(too_many_decls != NULL);
  expect_parse_fail_with_message(too_many_decls, "宣言子列が多すぎます");
  free(too_many_decls);

  char *ok_inits = build_many_array_init_elements_program(128);
  ASSERT_TRUE(ok_inits != NULL);
  expect_parse_ok(ok_inits);
  free(ok_inits);

  char *too_many_inits = build_many_array_init_elements_program(5000);
  ASSERT_TRUE(too_many_inits != NULL);
  expect_parse_fail_with_message(too_many_inits, "初期化子要素数が多すぎます");
  free(too_many_inits);
}

static void test_semantic_canonical_type_invariant() {
  printf("test_semantic_canonical_type_invariant...\n");
  reset_test_translation_unit_state();
  node_t **program = parse_program_input(
      "struct S { int x; int y; }; "
      "int inc(int x) { return x + 1; } "
      "int apply(int (*fp)(int), int value) { return fp(value); } "
      "int main(void) { "
      "  struct S s = (struct S){.x = 3, .y = 4}; "
      "  int values[2] = {s.x, s.y}; "
      "  int (*fp)(int) = inc; "
      "  int total = 0; "
      "  for (int i = 0; i < 2; i++) total += values[i]; "
      "  return _Generic(total, int: apply(fp, total), default: 0); "
      "}");
  ASSERT_TRUE(program != NULL);
  for (int i = 0; program[i]; i++) {
    node_function_definition_t *function =
        as_function_definition(program[i]);
    ASSERT_TRUE(function->base.type == NULL);
    ASSERT_TRUE(ps_function_definition_return_type(function) != NULL);
    psx_semantic_invariant_failure_t failure;
    ASSERT_TRUE(psx_semantic_tree_has_canonical_expression_types(
        program[i], &failure));
    ASSERT_EQ(PSX_SEMANTIC_INVARIANT_OK, failure.status);
  ASSERT_TRUE(failure.node == NULL);
  }

  psx_semantic_invariant_failure_t failure;
  node_t *va_arg_area = parse_expr_input("__va_arg_area");
  ASSERT_EQ(ND_VA_ARG_AREA, va_arg_area->kind);
  ASSERT_TRUE(va_arg_area->type != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER, va_arg_area->type->kind);
  ASSERT_TRUE(va_arg_area->type->base != NULL);
  ASSERT_EQ(PSX_TYPE_VOID, va_arg_area->type->base->kind);
  ASSERT_TRUE(psx_semantic_tree_has_canonical_expression_types(
      va_arg_area, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_OK, failure.status);

  node_function_definition_t duplicated_return_type = {0};
  duplicated_return_type.base.kind = ND_FUNCDEF;
  duplicated_return_type.signature = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0));
  duplicated_return_type.base.type =
      duplicated_return_type.signature->base;
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      (node_t *)&duplicated_return_type, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE,
            failure.status);
  ASSERT_TRUE(failure.node == (node_t *)&duplicated_return_type);

  node_num_t untyped = {0};
  untyped.base.kind = ND_NUM;
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      (node_t *)&untyped, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_MISSING_CANONICAL_TYPE, failure.status);
  ASSERT_TRUE(failure.node == (node_t *)&untyped);
  token_t *invariant_tok = tk_tokenize((char *)"untyped");
  expect_semantic_invariant_internal_error(
      (node_t *)&untyped, invariant_tok);

  node_t raw_subscript = {.kind = ND_SUBSCRIPT};
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      &raw_subscript, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_RAW_EXPRESSION, failure.status);
  ASSERT_TRUE(failure.node == &raw_subscript);

  node_t raw_initializer = {.kind = ND_INIT_LIST};
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      &raw_initializer, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INTERMEDIATE_INITIALIZER_SYNTAX,
            failure.status);
  ASSERT_TRUE(failure.node == &raw_initializer);

  node_t invalid_node_kind = {.kind = (node_kind_t)999};
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      &invalid_node_kind, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INVALID_NODE_KIND, failure.status);
  ASSERT_TRUE(failure.node == &invalid_node_kind);

  node_t invalid_vla_view = {
      .kind = ND_LVAR,
      .type = ps_type_new_pointer(ps_type_new_integer(TK_INT, 4, 0)),
  };
  invalid_vla_view.type_state.vla_runtime.row_stride_frame_off = 24;
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      &invalid_vla_view, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INVALID_VLA_RUNTIME_VIEW,
            failure.status);
  ASSERT_TRUE(failure.node == &invalid_vla_view);

  node_funcref_t invalid_function_reference = {0};
  invalid_function_reference.base.kind = ND_FUNCREF;
  invalid_function_reference.base.type =
      ps_type_new_integer(TK_INT, 4, 0);
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      (node_t *)&invalid_function_reference, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE,
            failure.status);
  ASSERT_TRUE(failure.node == (node_t *)&invalid_function_reference);

  node_function_call_t invalid_function_call = {0};
  invalid_function_call.base.kind = ND_FUNCALL;
  invalid_function_call.base.type =
      ps_type_new_integer(TK_INT, 4, 0);
  invalid_function_call.callee_type = ps_type_new_function(
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8));
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      (node_t *)&invalid_function_call, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE,
            failure.status);
  ASSERT_TRUE(failure.node == (node_t *)&invalid_function_call);

  psx_type_t *callee_function = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0));
  node_t non_callable_callee = {
      .kind = ND_LVAR,
      .type = ps_type_new_pointer(
          ps_type_new_pointer(ps_type_clone(callee_function))),
  };
  node_function_call_t invalid_indirect_function_call = {0};
  invalid_indirect_function_call.base.kind = ND_FUNCALL;
  invalid_indirect_function_call.base.type =
      ps_type_new_integer(TK_INT, 4, 0);
  invalid_indirect_function_call.callee = &non_callable_callee;
  invalid_indirect_function_call.callee_type = callee_function;
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      (node_t *)&invalid_indirect_function_call, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE,
            failure.status);
  ASSERT_TRUE(
      failure.node == (node_t *)&invalid_indirect_function_call);

  node_function_call_t invalid_implicit_function_call = {0};
  invalid_implicit_function_call.base.kind = ND_FUNCALL;
  invalid_implicit_function_call.base.type =
      ps_type_new_float(TK_FLOAT_KIND_DOUBLE, 8);
  invalid_implicit_function_call.base.is_implicit_func_decl = 1;
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      (node_t *)&invalid_implicit_function_call, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INVALID_CALLABLE_TYPE,
            failure.status);
  ASSERT_TRUE(
      failure.node == (node_t *)&invalid_implicit_function_call);

  node_function_call_t valid_implicit_function_call = {0};
  valid_implicit_function_call.base.kind = ND_FUNCALL;
  valid_implicit_function_call.base.type =
      ps_type_new_integer(TK_INT, 4, 0);
  valid_implicit_function_call.base.is_implicit_func_decl = 1;
  ASSERT_TRUE(psx_semantic_tree_has_canonical_expression_types(
      (node_t *)&valid_implicit_function_call, &failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_OK, failure.status);
}

static void test_recursive_declarator_capacity_boundary() {
  printf("test_recursive_declarator_capacity_boundary...\n");
  reset_test_translation_unit_state();

  char pointer_declarator[512] = {0};
  size_t used = 0;
  for (int i = 0; i < 96; i++) {
    pointer_declarator[used++] = '*';
    if (i == 5) {
      memcpy(pointer_declarator + used, " const ", 7);
      used += 7;
    } else if (i == 70) {
      memcpy(pointer_declarator + used, " volatile ", 10);
      used += 10;
    }
  }
  memcpy(pointer_declarator + used, "deep_pointer;", 14);
  token_t *tokens = tk_tokenize(pointer_declarator);
  tk_set_current_token(tokens);
  psx_parsed_declarator_t pointer_syntax =
      parse_test_declarator_syntax_tree();
  ASSERT_EQ(96, pointer_syntax.declarator_shape.count);
  ASSERT_TRUE(pointer_syntax.declarator_shape.capacity >= 96);
  psx_declarator_shape_t pointer_shape;
  ASSERT_TRUE(ps_declarator_shape_copy(
      &pointer_shape, &pointer_syntax.declarator_shape));
  ASSERT_TRUE(pointer_shape.ops != pointer_syntax.declarator_shape.ops);
  pointer_shape.ops[0].is_const_qualified = 1;
  ASSERT_TRUE(!pointer_syntax.declarator_shape.ops[0].is_const_qualified);
  psx_type_t *deep_pointer_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &pointer_shape);
  ASSERT_EQ(96, ps_type_pointer_depth(deep_pointer_type));
  for (int i = 0; i < 96; i++) {
    const psx_type_t *pointer = canonical_pointer_level(
        deep_pointer_type, i);
    ASSERT_TRUE(pointer != NULL);
    ASSERT_EQ(i == 0 || i == 90,
              ps_type_has_qualifier(pointer, PSX_TYPE_QUALIFIER_CONST));
    ASSERT_EQ(i == 25, ps_type_has_qualifier(pointer, PSX_TYPE_QUALIFIER_VOLATILE));
  }
  char deep_pointer_signature[512];
  ASSERT_EQ(294, ps_type_format_canonical_signature(
                     deep_pointer_type, deep_pointer_signature,
                     sizeof(deep_pointer_signature)));
  ASSERT_EQ('k', deep_pointer_signature[0]);
  ASSERT_EQ('p', deep_pointer_signature[1]);
  ASSERT_EQ('>', deep_pointer_signature[293]);
  node_t deep_pointer_node = {
      .kind = ND_LVAR,
      .type = deep_pointer_type,
  };
  psx_semantic_invariant_failure_t invariant_failure;
  ASSERT_TRUE(psx_semantic_tree_has_canonical_expression_types(
      &deep_pointer_node, &invariant_failure));

  psx_type_t *cyclic_pointer = ps_type_new_pointer(NULL);
  cyclic_pointer->base = cyclic_pointer;
  ASSERT_TRUE(!ps_type_is_well_formed(cyclic_pointer));
  ASSERT_EQ(-1, ps_type_format_canonical_signature(
                    cyclic_pointer, deep_pointer_signature,
                    sizeof(deep_pointer_signature)));
  node_t cyclic_pointer_node = {
      .kind = ND_LVAR,
      .type = cyclic_pointer,
  };
  ASSERT_TRUE(!psx_semantic_tree_has_canonical_expression_types(
      &cyclic_pointer_node, &invariant_failure));
  ASSERT_EQ(PSX_SEMANTIC_INVARIANT_INVALID_CANONICAL_TYPE,
            invariant_failure.status);
  psx_dispose_declarator_syntax(&pointer_syntax);

  char array_declarator[512] = "deep_array";
  used = strlen(array_declarator);
  for (int i = 0; i < 40; i++) {
    memcpy(array_declarator + used, "[1]", 3);
    used += 3;
  }
  array_declarator[used++] = ';';
  array_declarator[used] = '\0';
  tokens = tk_tokenize(array_declarator);
  tk_set_current_token(tokens);
  psx_parsed_declarator_t array_syntax =
      parse_test_declarator_syntax_tree();
  ASSERT_EQ(40, array_syntax.declarator_shape.count);
  ASSERT_EQ(40, array_syntax.array_bound_count);
  ASSERT_TRUE(array_syntax.array_bound_capacity >= 40);
  prepare_test_constant_declarator_expressions(&array_syntax);
  psx_declarator_shape_t array_shape;
  apply_test_parsed_declarator(&array_syntax, &array_shape, NULL);
  ASSERT_TRUE(array_shape.ops != array_syntax.declarator_shape.ops);
  psx_type_t *deep_array_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &array_shape);
  ASSERT_EQ(40, ps_type_array_rank(deep_array_type));
  ASSERT_EQ(1, ps_type_array_dimension(deep_array_type, 39));
  psx_dispose_declarator_syntax(&array_syntax);

  char deep_sizeof_expression[1024] = "sizeof(int";
  used = strlen(deep_sizeof_expression);
  for (int i = 0; i < 39; i++) {
    memcpy(deep_sizeof_expression + used, "[1]", 3);
    used += 3;
  }
  memcpy(deep_sizeof_expression + used, "[0])", 5);
  node_sizeof_query_t *deep_sizeof_query =
      (node_sizeof_query_t *)parse_expr_input_with_existing_locals(
          deep_sizeof_expression);
  psx_sizeof_query_resolution_t deep_sizeof_resolution;
  resolve_test_sizeof_query(deep_sizeof_query, &deep_sizeof_resolution);
  ASSERT_EQ(PSX_TYPE_QUERY_RESOLUTION_OK,
            deep_sizeof_resolution.status);
  ASSERT_EQ(1, deep_sizeof_resolution.zero_length_bound_count);
  ASSERT_EQ(39, deep_sizeof_resolution.zero_length_bound_indices[0]);
  ASSERT_TRUE(deep_sizeof_query->type_name.resolved_type != NULL);
  ASSERT_EQ(40, ps_type_array_rank(
                    deep_sizeof_query->type_name.resolved_type));

  char function_declarator[2048] = {0};
  used = 0;
  for (int i = 1; i < 26; i++) {
    memcpy(function_declarator + used, "(*", 2);
    used += 2;
  }
  memcpy(function_declarator + used, "deep_function(void)", 19);
  used += 19;
  for (int i = 1; i < 26; i++) {
    memcpy(function_declarator + used, ")(void)", 7);
    used += 7;
  }
  function_declarator[used++] = ';';
  function_declarator[used] = '\0';
  tokens = tk_tokenize(function_declarator);
  tk_set_current_token(tokens);
  psx_parsed_declarator_t function_syntax =
      parse_test_declarator_syntax_tree();
  ASSERT_EQ(26, function_syntax.function_suffix_count);
  ASSERT_TRUE(function_syntax.function_suffix_capacity >= 26);
  ASSERT_EQ(51, function_syntax.declarator_shape.count);
  psx_declarator_shape_t function_shape;
  apply_test_parsed_declarator(&function_syntax, &function_shape, NULL);
  psx_type_t *deep_function_type = ps_type_apply_declarator_shape(
      ps_type_new_integer(TK_INT, 4, 0), &function_shape);
  const psx_type_t *function_level = deep_function_type;
  for (int i = 0; i < 26; i++) {
    ASSERT_TRUE(function_level != NULL);
    ASSERT_EQ(PSX_TYPE_FUNCTION, function_level->kind);
    function_level = function_level->base;
    if (i + 1 < 26) {
      ASSERT_TRUE(function_level != NULL);
      ASSERT_EQ(PSX_TYPE_POINTER, function_level->kind);
      function_level = function_level->base;
    }
  }
  ASSERT_TRUE(function_level != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, function_level->kind);
  psx_dispose_declarator_syntax(&function_syntax);
}

static void test_arena_checkpoint_rollback() {
  printf("test_arena_checkpoint_rollback...\n");
  arena_context_t *arena_context = test_arena_context();
  arena_free_all_in(arena_context);
  int *retained = arena_alloc_in(arena_context, sizeof(*retained));
  *retained = 41;
  arena_checkpoint_t checkpoint = arena_checkpoint_in(arena_context);
  int *discarded = arena_alloc_in(arena_context, sizeof(*discarded));
  *discarded = 99;
  arena_rollback_in(arena_context, checkpoint);
  int *reused = arena_alloc_in(arena_context, sizeof(*reused));
  ASSERT_TRUE(reused == discarded);
  ASSERT_EQ(41, *retained);
  arena_free_all_in(arena_context);
}

static void test_semantic_type_identity() {
  printf("test_semantic_type_identity...\n");
  psx_semantic_context_t *context = ps_ctx_create(test_arena_context());
  ASSERT_TRUE(context != NULL);
  if (!context) return;

  psx_type_t *plain_int = ps_type_new_integer(TK_INT, 4, 0);
  psx_type_t *stale_wide_int = ps_type_new_integer(TK_INT, 8, 0);
  psx_type_t *stale_narrow_unsigned_int =
      ps_type_new_integer(TK_UNSIGNED, 1, 1);
  psx_type_t *stale_wide_unsigned_int =
      ps_type_new_integer(TK_UNSIGNED, 8, 1);
  ASSERT_EQ(3, ps_type_integer_rank(plain_int));
  ASSERT_EQ(3, ps_type_integer_rank(stale_wide_int));
  ASSERT_TRUE(ps_type_unqualified_semantic_matches(
      plain_int, stale_wide_int));
  ASSERT_EQ(TK_INT, stale_narrow_unsigned_int->scalar_kind);
  ASSERT_EQ(3, ps_type_integer_rank(stale_narrow_unsigned_int));
  ASSERT_TRUE(ps_type_integer_promotion_is_unsigned_for_target(
      stale_narrow_unsigned_int,
      ps_ctx_target_info(test_semantic_context())));
  ASSERT_EQ(TK_INT, stale_wide_unsigned_int->scalar_kind);
  ASSERT_EQ(3, ps_type_integer_rank(stale_wide_unsigned_int));
  ASSERT_TRUE(ps_type_integer_promotion_is_unsigned_for_target(
      stale_wide_unsigned_int,
      ps_ctx_target_info(test_semantic_context())));
  psx_type_t *stale_wide_short =
      ps_type_new_integer(TK_SHORT, 64, 1);
  psx_type_t *stale_narrow_int =
      ps_type_new_integer(TK_INT, 1, 0);
  psx_type_t *boolean = ps_type_new_integer(TK_BOOL, 1, 0);
  ASSERT_EQ(2, ps_type_character_code_unit_width(stale_wide_short));
  ASSERT_EQ(4, ps_type_character_code_unit_width(stale_narrow_int));
  ASSERT_EQ(4, ps_type_character_code_unit_width(
      stale_narrow_unsigned_int));
  ASSERT_EQ(0, ps_type_character_code_unit_width(boolean));
  node_string_t utf16_string = {0};
  utf16_string.base.kind = ND_STRING;
  utf16_string.char_width = TK_CHAR_WIDTH_CHAR16;
  utf16_string.byte_len = 2;
  psx_type_t *utf16_array = ps_type_new_array(
      stale_wide_short, 0, 0, 0);
  ASSERT_TRUE(psx_resolve_incomplete_array_initializer(
      test_semantic_context(), utf16_array, PSX_DECL_INIT_EXPR,
      (node_t *)&utf16_string));
  ASSERT_EQ(3, utf16_array->array_len);
  psx_type_t *boolean_array = ps_type_new_array(boolean, 0, 0, 0);
  ASSERT_TRUE(!psx_resolve_incomplete_array_initializer(
      test_semantic_context(), boolean_array, PSX_DECL_INIT_EXPR,
      (node_t *)&utf16_string));
  ASSERT_EQ(0, boolean_array->array_len);
  psx_type_t *const_int = ps_type_clone(plain_int);
  ps_type_add_qualifiers(const_int, PSX_TYPE_QUALIFIER_CONST);
  psx_qual_type_t plain_int_identity =
      ps_ctx_intern_qual_type_in(context, plain_int);
  psx_qual_type_t const_int_identity =
      ps_ctx_intern_qual_type_in(context, const_int);
  ASSERT_TRUE(plain_int_identity.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(plain_int_identity.type_id, const_int_identity.type_id);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_NONE, plain_int_identity.qualifiers);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST, const_int_identity.qualifiers);
  const psx_type_t *interned_int =
      ps_ctx_type_by_id_in(context, plain_int_identity.type_id);
  ASSERT_TRUE(interned_int != NULL);
  ASSERT_TRUE(interned_int != plain_int);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_NONE, ps_type_qualifiers(interned_int));

  psx_type_t *host_pointer = ps_type_new_pointer(plain_int);
  psx_type_t *wasm_pointer = ps_type_clone(host_pointer);
  psx_qual_type_t host_pointer_identity =
      ps_ctx_intern_qual_type_in(context, host_pointer);
  psx_qual_type_t wasm_pointer_identity =
      ps_ctx_intern_qual_type_in(context, wasm_pointer);
  ASSERT_EQ(host_pointer_identity.type_id, wasm_pointer_identity.type_id);
  psx_qual_type_t pointer_base_identity = psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(context), host_pointer_identity.type_id);
  ASSERT_EQ(plain_int_identity.type_id, pointer_base_identity.type_id);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_NONE, pointer_base_identity.qualifiers);
  ASSERT_TRUE(ps_ctx_find_interned_qual_type_in(context, plain_int).type_id !=
              PSX_TYPE_ID_INVALID);

  psx_type_t *pointer_to_const = ps_type_new_pointer(const_int);
  psx_qual_type_t pointer_to_const_identity =
      ps_ctx_intern_qual_type_in(context, pointer_to_const);
  ASSERT_TRUE(pointer_to_const_identity.type_id !=
              host_pointer_identity.type_id);
  psx_qual_type_t qualified_pointer_base = psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(context),
      pointer_to_const_identity.type_id);
  ASSERT_EQ(plain_int_identity.type_id, qualified_pointer_base.type_id);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST, qualified_pointer_base.qualifiers);

  psx_type_t *const_int_array = ps_type_new_array(const_int, 3, 12, 0);
  psx_type_t *nested_const_int_array = ps_type_new_array(
      const_int_array, 2, 24, 0);
  psx_type_t *nested_array_pointer = ps_type_new_pointer(
      nested_const_int_array);
  psx_qual_type_t nested_array_identity =
      ps_ctx_intern_qual_type_in(context, nested_const_int_array);
  psx_qual_type_t nested_array_pointer_identity =
      ps_ctx_intern_qual_type_in(context, nested_array_pointer);
  psx_qual_type_t array_leaf_identity =
      psx_semantic_type_table_array_leaf(
          ps_ctx_semantic_type_table_in(context),
          nested_array_identity.type_id);
  ASSERT_EQ(plain_int_identity.type_id, array_leaf_identity.type_id);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST, array_leaf_identity.qualifiers);
  psx_qual_type_t pointee_value_identity =
      psx_semantic_type_table_pointee_value(
          ps_ctx_semantic_type_table_in(context),
          nested_array_pointer_identity.type_id);
  ASSERT_EQ(array_leaf_identity.type_id, pointee_value_identity.type_id);
  ASSERT_EQ(array_leaf_identity.qualifiers,
            pointee_value_identity.qualifiers);
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            psx_semantic_type_table_pointee_value(
                ps_ctx_semantic_type_table_in(context),
                plain_int_identity.type_id).type_id);

  const psx_type_t *function_parameters[2] = {const_int, host_pointer};
  psx_type_t *function_type = ps_type_new_function(plain_int);
  ps_type_set_function_params(function_type, function_parameters, 2, 0);
  psx_qual_type_t function_identity =
      ps_ctx_intern_qual_type_in(context, function_type);
  ASSERT_TRUE(function_identity.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(plain_int_identity.type_id,
            psx_semantic_type_table_base(
                ps_ctx_semantic_type_table_in(context),
                function_identity.type_id).type_id);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST,
            psx_semantic_type_table_parameter(
                ps_ctx_semantic_type_table_in(context),
                function_identity.type_id, 0).qualifiers);
  ASSERT_EQ(host_pointer_identity.type_id,
            psx_semantic_type_table_parameter(
                ps_ctx_semantic_type_table_in(context),
                function_identity.type_id, 1).type_id);
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            psx_semantic_type_table_parameter(
                ps_ctx_semantic_type_table_in(context),
                function_identity.type_id, 2).type_id);

  char record_name[] = "IdentityRecord";
  psx_type_t *first_record = ps_type_new_tag(
      TK_STRUCT, record_name, 14, 1, 8);
  first_record->record_id = 41;
  psx_type_t *same_record = ps_type_clone(first_record);
  psx_type_t *other_record = ps_type_clone(first_record);
  other_record->record_id = 42;
  psx_qual_type_t first_record_identity =
      ps_ctx_intern_qual_type_in(context, first_record);
  psx_qual_type_t same_record_identity =
      ps_ctx_intern_qual_type_in(context, same_record);
  psx_qual_type_t other_record_identity =
      ps_ctx_intern_qual_type_in(context, other_record);
  ASSERT_EQ(first_record_identity.type_id, same_record_identity.type_id);
  ASSERT_TRUE(first_record_identity.type_id != other_record_identity.type_id);

  char recursive_name[] = "RecursiveIdentityRecord";
  psx_type_t *recursive_record = ps_type_new_tag(
      TK_STRUCT, recursive_name, 23, 1, 8);
  recursive_record->record_id = 43;
  psx_type_t *recursive_pointer = ps_type_new_pointer(recursive_record);
  tag_member_info_t recursive_member = {
      .name = (char *)"next",
      .len = 4,
      .decl_type = recursive_pointer,
  };
  psx_aggregate_definition_t recursive_definition = {
      .record_id = 43,
      .tag_kind = TK_STRUCT,
      .tag_name = recursive_name,
      .tag_len = 23,
      .is_complete = 1,
      .member_count = 1,
      .members = &recursive_member,
  };
  recursive_record->aggregate_definition = &recursive_definition;
  ASSERT_TRUE(psx_record_decl_table_define(
      (psx_record_decl_table_t *)ps_ctx_record_decl_table_in(context),
      &recursive_definition));
  psx_qual_type_t recursive_identity =
      ps_ctx_intern_qual_type_in(context, recursive_record);
  ASSERT_TRUE(recursive_identity.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(psx_semantic_type_table_lookup(
                  ps_ctx_semantic_type_table_in(context),
                  recursive_identity.type_id)->aggregate_definition == NULL);
  psx_qual_type_t recursive_pointer_identity =
      ps_ctx_find_interned_qual_type_in(context, recursive_pointer);
  ASSERT_TRUE(recursive_pointer_identity.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(recursive_pointer_identity.type_id,
            psx_semantic_type_table_record_member(
                ps_ctx_semantic_type_table_in(context),
                recursive_identity.type_id, 0).type_id);
  ASSERT_EQ(recursive_identity.type_id,
            psx_semantic_type_table_base(
                ps_ctx_semantic_type_table_in(context),
                recursive_pointer_identity.type_id).type_id);

  char completed_record_name[] = "CompletedIdentityRecord";
  psx_aggregate_definition_t completed_definition = {
      .record_id = 44,
      .tag_kind = TK_STRUCT,
      .tag_name = completed_record_name,
      .tag_len = 23,
  };
  psx_type_t *completed_record = ps_type_new_tag(
      TK_STRUCT, completed_record_name, 23, 1, 0);
  completed_record->record_id = completed_definition.record_id;
  completed_record->aggregate_definition = &completed_definition;
  ASSERT_TRUE(psx_record_decl_table_define(
      (psx_record_decl_table_t *)ps_ctx_record_decl_table_in(context),
      &completed_definition));
  psx_qual_type_t incomplete_record_identity =
      ps_ctx_intern_qual_type_in(context, completed_record);
  ASSERT_TRUE(incomplete_record_identity.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            psx_semantic_type_table_record_member(
                ps_ctx_semantic_type_table_in(context),
                incomplete_record_identity.type_id, 0).type_id);
  tag_member_info_t completed_member = {
      .name = (char *)"value",
      .len = 5,
      .decl_type = const_int,
  };
  completed_definition.is_complete = 1;
  completed_definition.member_count = 1;
  completed_definition.members = &completed_member;
  psx_qual_type_t completed_record_identity =
      ps_ctx_intern_qual_type_in(context, completed_record);
  ASSERT_EQ(incomplete_record_identity.type_id,
            completed_record_identity.type_id);
  ASSERT_EQ(plain_int_identity.type_id,
            psx_semantic_type_table_record_member(
                ps_ctx_semantic_type_table_in(context),
                completed_record_identity.type_id, 0).type_id);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST,
            psx_semantic_type_table_record_member(
                ps_ctx_semantic_type_table_in(context),
                completed_record_identity.type_id, 0).qualifiers);

  psx_type_id_t retained_id = pointer_to_const_identity.type_id;
  ASSERT_TRUE(ps_ctx_type_by_id_in(context, retained_id) != NULL);
  ps_ctx_reset_translation_unit_scope_in(context);
  ASSERT_TRUE(ps_ctx_type_by_id_in(context, retained_id) == NULL);

  node_t typed_expression = {
      .kind = ND_NUM,
      .type = plain_int,
  };
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            ps_ctx_find_interned_qual_type_in(
                context, typed_expression.type).type_id);
  psx_semantic_invariant_failure_t failure = {0};
  ASSERT_TRUE(psx_finalize_semantic_tree_type_identities(
      context, &typed_expression, &failure, 0));
  ASSERT_TRUE(ps_ctx_find_interned_qual_type_in(
                  context, typed_expression.type).type_id !=
              PSX_TYPE_ID_INVALID);
  ps_ctx_destroy(context);
}

static void test_semantic_context_isolation() {
  printf("test_semantic_context_isolation...\n");
  arena_context_t *arena_context =
      ag_compilation_session_arena_context(test_suite_session);
  psx_semantic_context_t *first = ps_ctx_create(arena_context);
  psx_semantic_context_t *second = ps_ctx_create(arena_context);
  ASSERT_TRUE(first != NULL);
  ASSERT_TRUE(second != NULL);
  if (!first || !second) {
    ps_ctx_destroy(first);
    ps_ctx_destroy(second);
    return;
  }
  ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(test_suite_session);
  ps_ctx_bind_diagnostic_context(first, diagnostics);
  ps_ctx_bind_diagnostic_context(second, diagnostics);

  char enum_name[] = "ContextValue";
  char tag_name[] = "ContextTag";
  long long value = 0;
  ASSERT_TRUE(ps_ctx_register_enum_const_in_contexts(
      first, test_local_registry(), enum_name, 12, 11, NULL));
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      first, test_local_registry(),
      TK_STRUCT, tag_name, 10, 0, 0, 0, 0));

  ASSERT_TRUE(!ps_ctx_find_enum_const_in(
      second, enum_name, 12, &value));
  ASSERT_TRUE(!ps_ctx_has_tag_type_in(
      second, TK_STRUCT, tag_name, 10));
  ASSERT_TRUE(ps_ctx_register_enum_const_in_contexts(
      second, test_local_registry(), enum_name, 12, 22, NULL));
  ASSERT_TRUE(ps_ctx_find_enum_const_in(
      second, enum_name, 12, &value));
  ASSERT_EQ(22, value);

  ASSERT_TRUE(ps_ctx_find_enum_const_in(first, enum_name, 12, &value));
  ASSERT_EQ(11, value);
  ASSERT_TRUE(ps_ctx_has_tag_type_in(
      first, TK_STRUCT, tag_name, 10));

  char function_name[] = "ContextFunction";
  psx_type_t *function_type = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_ctx_register_function_type_in(
                  second, function_name, 15, function_type) != NULL);
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(first,
                  function_name, 15) == NULL);
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
                  first, function_name, 15) == NULL);
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
                  second, function_name, 15) != NULL);
  psx_identifier_resolution_t function_resolution;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = second,
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = function_name,
          .name_len = 15,
          .is_call = 1,
      },
      &function_resolution);
  ASSERT_EQ(PSX_IDENTIFIER_FUNCTION, function_resolution.kind);
  ASSERT_TRUE(function_resolution.function ==
              ps_ctx_find_function_symbol_in(
                  second, function_name, 15));

  char direct_enum_name[] = "DirectEnum";
  psx_enum_constant_resolution_t direct_enum_resolution;
  psx_resolve_enum_constant(
      &(psx_enum_constant_resolution_request_t){
          .semantic_context = second,
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = direct_enum_name,
          .name_len = 10,
          .value = 37,
      },
      &direct_enum_resolution);
  ASSERT_EQ(PSX_ENUM_CONSTANT_OK, direct_enum_resolution.status);
  ASSERT_TRUE(!ps_ctx_find_enum_const_in(
      first, direct_enum_name, 10, &value));
  ASSERT_TRUE(ps_ctx_find_enum_const_in(
      second, direct_enum_name, 10, &value));
  ASSERT_EQ(37, value);
  psx_identifier_resolution_t direct_enum_identifier;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = second,
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = direct_enum_name,
          .name_len = 10,
      },
      &direct_enum_identifier);
  ASSERT_EQ(PSX_IDENTIFIER_ENUM_CONSTANT, direct_enum_identifier.kind);
  ASSERT_EQ(37, direct_enum_identifier.enum_value);
  psx_parsed_enum_expr_t direct_enum_expression = {
      .kind = PSX_ENUM_EXPR_IDENTIFIER,
      .identifier = direct_enum_name,
      .identifier_len = 10,
  };
  ASSERT_EQ(37, psx_resolve_prepared_enum_const_expr_in_context(
                    second, &direct_enum_expression));

  char direct_typedef_name[] = "DirectTypedef";
  psx_type_t *direct_typedef_type =
      ps_type_new_integer(TK_LONG, 8, 0);
  psx_typedef_declaration_resolution_t direct_typedef_resolution;
  psx_resolve_typedef_declaration(
      &(psx_typedef_declaration_resolution_request_t){
          .semantic_context = second,
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = direct_typedef_name,
          .name_len = 13,
          .type = direct_typedef_type,
      },
      &direct_typedef_resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK,
            direct_typedef_resolution.status);
  psx_typedef_info_t direct_typedef_info = {0};
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(
      first, direct_typedef_name, 13, &direct_typedef_info));
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(
      second, direct_typedef_name, 13, &direct_typedef_info));
  ASSERT_EQ(8, ps_type_sizeof(
                   ps_ctx_typedef_decl_type(&direct_typedef_info)));
  psx_global_declaration_resolution_t direct_global_resolution;
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = first,
          .global_registry = test_global_registry(),
          .name = direct_typedef_name,
          .name_len = 13,
          .type = ps_type_new_integer(TK_INT, 4, 0),
          .is_extern_decl = 1,
      },
      &direct_global_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_OK,
            direct_global_resolution.status);
  psx_resolve_global_declaration(
      &(psx_global_declaration_resolution_request_t){
          .semantic_context = second,
          .global_registry = test_global_registry(),
          .name = direct_typedef_name,
          .name_len = 13,
          .type = ps_type_new_integer(TK_INT, 4, 0),
          .is_extern_decl = 1,
      },
      &direct_global_resolution);
  ASSERT_EQ(PSX_GLOBAL_DECLARATION_TYPEDEF_NAME_CONFLICT,
            direct_global_resolution.status);

  char direct_tag_name[] = "DirectTag";
  char direct_member_name[] = "value";
  tag_member_info_t direct_member = {
      .name = direct_member_name,
      .len = 5,
      .offset = 0,
      .decl_type = ps_type_new_integer(TK_INT, 4, 0),
  };
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      second, test_local_registry(),
      TK_STRUCT, direct_tag_name, 9, 0, 0, 0, 0));
  ASSERT_TRUE(ps_ctx_register_tag_members_in(
      second, TK_STRUCT, direct_tag_name, 9,
      &direct_member, 1, NULL));
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      second, test_local_registry(),
      TK_STRUCT, direct_tag_name, 9, 1, 1, 4, 4));
  ASSERT_TRUE(!ps_ctx_has_tag_type_in(
      first, TK_STRUCT, direct_tag_name, 9));
  ASSERT_TRUE(ps_ctx_has_tag_type_in(
      second, TK_STRUCT, direct_tag_name, 9));
  ASSERT_EQ(-1, ps_ctx_get_tag_size_in(
      first, TK_STRUCT, direct_tag_name, 9));
  ASSERT_EQ(4, ps_ctx_get_tag_size_in(
      second, TK_STRUCT, direct_tag_name, 9));
  tag_member_info_t direct_member_result = {0};
  ASSERT_TRUE(!ps_ctx_find_tag_member_info_in(
      first, TK_STRUCT, direct_tag_name, 9,
      direct_member_name, 5, &direct_member_result));
  ASSERT_TRUE(ps_ctx_find_tag_member_info_in(
      second, TK_STRUCT, direct_tag_name, 9,
      direct_member_name, 5, &direct_member_result));
  ASSERT_EQ(0, direct_member_result.offset);
  ASSERT_EQ(4, ps_type_sizeof(direct_member_result.decl_type));
  psx_parsed_decl_specifier_t direct_tag_specifier = {
      .source = PSX_PARSED_DECL_TYPE_TAG,
      .tag_action = {
          .kind = TK_STRUCT,
          .name = direct_tag_name,
          .name_len = 9,
      },
  };
  const psx_type_t *direct_tag_type =
      psx_apply_parsed_decl_specifier_in_contexts(
          second, test_global_registry(),
          test_local_registry(), &direct_tag_specifier);
  ASSERT_TRUE(direct_tag_type != NULL);
  ASSERT_EQ(PSX_TYPE_STRUCT, direct_tag_type->kind);
  ASSERT_EQ(0, ps_type_sizeof(direct_tag_type));
  ASSERT_EQ(4, ps_ctx_type_sizeof_in(second, direct_tag_type));
  ASSERT_EQ(4, ps_ctx_type_alignof_in(second, direct_tag_type));
  const psx_record_decl_t *direct_record =
      test_record_decl_in(second, direct_tag_type);
  ASSERT_TRUE(direct_record != NULL);
  ASSERT_EQ(1, direct_record->member_count);

  psx_type_t *detached_tag_type = ps_type_new_tag(
      TK_STRUCT, direct_tag_name, 9, 1, 4);
  ASSERT_TRUE(detached_tag_type->aggregate_definition == NULL);
  node_t detached_base = {
      .kind = ND_LVAR,
      .type = detached_tag_type,
  };
  psx_member_access_resolution_t detached_resolution;
  psx_resolve_member_access(
      &(psx_member_access_resolution_request_t){
          .semantic_context = first,
          .base = &detached_base,
          .member_name = direct_member_name,
          .member_name_len = 5,
      },
      &detached_resolution);
  ASSERT_EQ(PSX_MEMBER_ACCESS_NOT_FOUND, detached_resolution.status);
  psx_resolve_member_access(
      &(psx_member_access_resolution_request_t){
          .semantic_context = second,
          .base = &detached_base,
          .member_name = direct_member_name,
          .member_name_len = 5,
      },
      &detached_resolution);
  ASSERT_EQ(PSX_MEMBER_ACCESS_OK, detached_resolution.status);
  ASSERT_EQ(4, ps_type_sizeof(detached_resolution.member.decl_type));

  node_member_access_t detached_access = {
      .base = {
          .kind = ND_MEMBER_ACCESS,
          .lhs = &detached_base,
      },
      .member_name = direct_member_name,
      .member_name_len = 5,
  };
  psx_semantic_resolve_tree_in_contexts(
      second, test_global_registry(), test_local_registry(),
      (node_t *)&detached_access, NULL, NULL);
  ASSERT_TRUE(detached_access.resolved_member != NULL);
  ASSERT_EQ(4, ps_type_sizeof(detached_access.base.type));

  psx_static_initializer_resolution_t detached_initializer;
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .semantic_context = first,
          .type = detached_tag_type,
          .kind = PSX_DECL_INIT_EXPR,
          .initializer = ps_node_new_num(0),
      },
      &detached_initializer);
  ASSERT_EQ(PSX_STATIC_INITIALIZER_OK, detached_initializer.status);
  ASSERT_EQ(PSX_RECORD_ID_INVALID,
            ps_type_record_id(detached_initializer.type));
  psx_resolve_static_initializer(
      &(psx_static_initializer_resolution_request_t){
          .semantic_context = second,
          .type = detached_tag_type,
          .kind = PSX_DECL_INIT_EXPR,
          .initializer = ps_node_new_num(0),
      },
      &detached_initializer);
  ASSERT_EQ(PSX_STATIC_INITIALIZER_OK, detached_initializer.status);
  const psx_record_decl_t *initializer_record = test_record_decl_in(
      second, detached_initializer.type);
  ASSERT_TRUE(initializer_record != NULL);
  ASSERT_EQ(1, initializer_record->member_count);

  char applied_tag_name[] = "AppliedTag";
  psx_apply_parsed_tag_declaration_in_contexts(
      second, test_local_registry(),
      TK_STRUCT, applied_tag_name, 10,
      PSX_TAG_DECLARATION_DEFINITION, 0, 8, 8, NULL);
  ASSERT_TRUE(!ps_ctx_has_tag_type_in(
      first, TK_STRUCT, applied_tag_name, 10));
  ASSERT_EQ(8, ps_ctx_get_tag_size_in(
      second, TK_STRUCT, applied_tag_name, 10));

  ASSERT_EQ(0, ps_ctx_current_tag_scope_depth_in(first));
  ASSERT_EQ(0, ps_ctx_current_tag_scope_depth_in(second));
  ps_ctx_enter_block_scope_in(second);
  ASSERT_EQ(0, ps_ctx_current_tag_scope_depth_in(first));
  ASSERT_EQ(1, ps_ctx_current_tag_scope_depth_in(second));
  ps_ctx_leave_block_scope_in(second);
  ASSERT_EQ(0, ps_ctx_current_tag_scope_depth_in(second));

  char direct_label_name[] = "direct_label";
  psx_ctx_register_goto_ref_in(
      second, direct_label_name, 12, NULL);
  psx_ctx_register_label_def_in(
      second, direct_label_name, 12, NULL);
  psx_ctx_validate_goto_refs_in(second);

  node_t *semantic_expression = ps_node_new_num(9);
  psx_semantic_expr_id_t semantic_expression_id =
      ps_ctx_register_semantic_expression_in(
          second, semantic_expression);
  ASSERT_TRUE(semantic_expression_id != PSX_SEMANTIC_EXPR_ID_INVALID);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  second, semantic_expression_id) == semantic_expression);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  first, semantic_expression_id) == NULL);

  ps_ctx_record_unsupported_gnu_extension_warning_in(
      second, NULL, "context-isolation");
  ASSERT_TRUE(ps_ctx_arena(second) == arena_context);
  ps_ctx_reset_translation_unit_scope_in(second);
  ASSERT_TRUE(ps_ctx_arena(second) == arena_context);
  ASSERT_TRUE(ps_ctx_diagnostics(second) == diagnostics);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  second, semantic_expression_id) == NULL);
  ASSERT_TRUE(!ps_ctx_find_enum_const_in(
      second, direct_enum_name, 10, &value));
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(
      second, direct_typedef_name, 13, &direct_typedef_info));
  ASSERT_TRUE(ps_ctx_find_enum_const_in(
      first, enum_name, 12, &value));
  ASSERT_EQ(11, value);

  char streamed_label_name[] = "streamed_label";
  ps_ctx_reset_function_scope_in(second);
  psx_typedef_declaration_resolution_t streamed_typedef_resolution;
  psx_resolve_typedef_declaration(
      &(psx_typedef_declaration_resolution_request_t){
          .semantic_context = second,
          .global_registry = test_global_registry(),
          .local_registry = test_local_registry(),
          .name = (char *)"StreamType",
          .name_len = 10,
          .type = ps_type_new_integer(TK_INT, 4, 0),
      },
      &streamed_typedef_resolution);
  ASSERT_EQ(PSX_TYPEDEF_DECLARATION_OK,
            streamed_typedef_resolution.status);
  psx_ctx_register_goto_ref_in(
      second, streamed_label_name, 14, NULL);
  psx_parser_stream_t parser_stream = {0};
  ps_parser_stream_begin_in_contexts(
      &parser_stream, second, test_global_registry(),
      test_local_registry(),
      ag_compilation_session_parser_runtime_context(test_suite_session), NULL,
      tk_tokenize((char *)
          "{ StreamType value = 0; "
          "{ streamed_label: return value; } }"),
      NULL);
  node_function_definition_t parsed_function = {0};
  parsed_function.base.kind = ND_FUNCDEF;
  psx_lowering_context_t *second_lowering_context =
      ps_lowering_context_create(arena_context, diagnostics);
  ASSERT_TRUE(second_lowering_context != NULL);
  ps_lowering_context_bind_target(
      second_lowering_context, ps_ctx_target_info(second));
  ps_lowering_context_bind_semantic_types(
      second_lowering_context, ps_ctx_semantic_type_table_in(second));
  ps_lowering_context_bind_record_decls(
      second_lowering_context, ps_ctx_record_decl_table_in(second));
  ps_lowering_context_bind_record_layouts(
      second_lowering_context, ps_ctx_record_layout_table_in(second));
  psx_local_declaration_callbacks_t local_declarations;
  psx_frontend_init_local_declaration_callbacks_in_contexts(
      &local_declarations, second, test_global_registry(),
      test_local_registry(),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      second_lowering_context,
      test_compilation_options());
  ASSERT_TRUE(ps_parse_function_definition_body(
                  &parser_stream, &parsed_function,
                  &local_declarations) != NULL);
  ASSERT_TRUE(find_func_lvar(&parsed_function, "value") != NULL);
  psx_ctx_validate_goto_refs_in(second);
  ASSERT_EQ(0, ps_ctx_current_tag_scope_depth_in(first));
  ASSERT_EQ(0, ps_ctx_current_tag_scope_depth_in(second));
  ps_parser_stream_end(&parser_stream);
  ps_lowering_context_destroy(second_lowering_context);

  ps_ctx_destroy(first);
  ps_ctx_destroy(second);
}

static void test_compilation_session_registry_isolation() {
  printf("test_compilation_session_registry_isolation...\n");
  ag_compilation_session_t first;
  ag_compilation_session_t second;
  ASSERT_TRUE(ag_compilation_session_init(&first, NULL));
  ASSERT_TRUE(ag_compilation_session_init(&second, NULL));

  global_var_t first_global = {
      .name = (char *)"shared_global",
      .name_len = 13,
      .decl_type = ps_type_new_integer(TK_INT, 4, 0),
      .init_val = 41,
      .has_init = 1,
  };
  global_var_t second_global = {
      .name = (char *)"shared_global",
      .name_len = 13,
      .decl_type = ps_type_new_integer(TK_LONG, 8, 0),
      .init_val = 99,
      .has_init = 1,
  };
  psx_type_t *session_callback_type = ps_type_new_pointer(
      ps_type_new_function(ps_type_new_integer(TK_INT, 4, 0)));
  global_var_t first_callback = {
      .name = (char *)"session_callback",
      .name_len = 16,
      .init_symbol = (char *)"session_fn",
      .init_symbol_len = 10,
      .has_init = 1,
      .decl_type = session_callback_type,
  };
  global_var_t second_callback = {
      .name = (char *)"session_callback",
      .name_len = 16,
      .init_symbol = (char *)"session_fn",
      .init_symbol_len = 10,
      .has_init = 1,
      .decl_type = session_callback_type,
  };
  char session_aggregate_name[] = "SessionAggregate";
  psx_type_t *first_aggregate_type = ps_type_new_tag(
      TK_STRUCT, session_aggregate_name, 16, 0, 8);
  psx_type_t *second_aggregate_type = ps_type_new_tag(
      TK_STRUCT, session_aggregate_name, 16, 0, 12);
  long long first_aggregate_values[2] = {11, 22};
  long long second_aggregate_values[2] = {33, 44};
  global_var_t first_aggregate = {
      .name = (char *)"session_aggregate",
      .name_len = 17,
      .init_values = first_aggregate_values,
      .init_count = 2,
      .has_init = 1,
      .decl_type = first_aggregate_type,
  };
  global_var_t second_aggregate = {
      .name = (char *)"session_aggregate",
      .name_len = 17,
      .init_values = second_aggregate_values,
      .init_count = 2,
      .has_init = 1,
      .decl_type = second_aggregate_type,
  };
  string_lit_t first_literal = {
      .label = (char *)".Lshared",
      .str = (char *)"first",
      .len = 5,
      .char_width = TK_CHAR_WIDTH_CHAR,
  };
  string_lit_t second_literal = {
      .label = (char *)".Lshared",
      .str = (char *)"second",
      .len = 6,
      .char_width = TK_CHAR_WIDTH_CHAR,
  };
  lvar_t first_local = {
      .name = (char *)"shared_local",
      .len = 12,
      .offset = 8,
  };
  lvar_t second_local = {
      .name = (char *)"shared_local",
      .len = 12,
      .offset = 16,
  };
  lvar_t explicit_first_local = {
      .name = (char *)"explicit_first_local",
      .len = 20,
      .offset = 24,
  };

  ps_register_global_var_in(first.global_registry, &first_global);
  ps_register_global_var_in(second.global_registry, &second_global);
  ps_register_global_var_in(first.global_registry, &first_callback);
  ps_register_global_var_in(second.global_registry, &second_callback);
  ps_register_global_var_in(first.global_registry, &first_aggregate);
  ps_register_global_var_in(second.global_registry, &second_aggregate);
  psx_register_string_lit_in(first.global_registry, &first_literal);
  psx_type_t *session_function_type = ps_type_new_function(
      ps_type_new_integer(TK_INT, 4, 0));
  ASSERT_TRUE(ps_ctx_register_function_type_in(
      first.semantic_context, (char *)"session_fn", 10,
      session_function_type) != NULL);
  tag_member_info_t first_aggregate_members[2] = {
      {.name = (char *)"left", .len = 4, .offset = 0,
       .decl_type = ps_type_new_integer(TK_INT, 4, 0)},
      {.name = (char *)"right", .len = 5, .offset = 4,
       .decl_type = ps_type_new_integer(TK_INT, 4, 0)},
  };
  tag_member_info_t second_aggregate_members[2] = {
      {.name = (char *)"left", .len = 4, .offset = 0,
       .decl_type = ps_type_new_integer(TK_INT, 4, 0)},
      {.name = (char *)"right", .len = 5, .offset = 8,
       .decl_type = ps_type_new_integer(TK_INT, 4, 0)},
  };
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      first.semantic_context, first.local_registry,
      TK_STRUCT, session_aggregate_name, 16, 0, 0, 0, 0));
  ASSERT_TRUE(ps_ctx_register_tag_members_in(
      first.semantic_context, TK_STRUCT,
      session_aggregate_name, 16,
      first_aggregate_members, 2, NULL));
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      first.semantic_context, first.local_registry,
      TK_STRUCT, session_aggregate_name, 16, 1, 2, 8, 4));
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      second.semantic_context, second.local_registry,
      TK_STRUCT, session_aggregate_name, 16, 0, 0, 0, 0));
  ASSERT_TRUE(ps_ctx_register_tag_members_in(
      second.semantic_context, TK_STRUCT,
      session_aggregate_name, 16,
      second_aggregate_members, 2, NULL));
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      second.semantic_context, second.local_registry,
      TK_STRUCT, session_aggregate_name, 16, 1, 2, 12, 4));
  first_aggregate_type = ps_ctx_clone_tag_type_at_in_contexts(
      first.semantic_context, first.local_registry,
      TK_STRUCT, session_aggregate_name, 16,
      ps_local_registry_capture_lookup_point_in(first.local_registry));
  second_aggregate_type = ps_ctx_clone_tag_type_at_in_contexts(
      second.semantic_context, second.local_registry,
      TK_STRUCT, session_aggregate_name, 16,
      ps_local_registry_capture_lookup_point_in(second.local_registry));
  ASSERT_TRUE(first_aggregate_type != NULL);
  ASSERT_TRUE(second_aggregate_type != NULL);
  first_aggregate.decl_type = first_aggregate_type;
  second_aggregate.decl_type = second_aggregate_type;
  first_global.decl_type_id = ps_ctx_intern_qual_type_in(
      first.semantic_context, first_global.decl_type).type_id;
  ASSERT_TRUE(first_global.decl_type_id != PSX_TYPE_ID_INVALID);
  first_callback.decl_type_id = ps_ctx_intern_qual_type_in(
      first.semantic_context, session_callback_type).type_id;
  ASSERT_TRUE(first_callback.decl_type_id != PSX_TYPE_ID_INVALID);
  first_aggregate.decl_type_id = ps_ctx_intern_qual_type_in(
      first.semantic_context, first_aggregate_type).type_id;
  ASSERT_TRUE(first_aggregate.decl_type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_ctx_intern_qual_type_in(
                  first.semantic_context,
                  first_aggregate_members[0].decl_type).type_id !=
              PSX_TYPE_ID_INVALID);
  second_global.decl_type_id = ps_ctx_intern_qual_type_in(
      second.semantic_context, second_global.decl_type).type_id;
  ASSERT_TRUE(second_global.decl_type_id != PSX_TYPE_ID_INVALID);
  second_callback.decl_type_id = ps_ctx_intern_qual_type_in(
      second.semantic_context, session_callback_type).type_id;
  ASSERT_TRUE(second_callback.decl_type_id != PSX_TYPE_ID_INVALID);
  second_aggregate.decl_type_id = ps_ctx_intern_qual_type_in(
      second.semantic_context, second_aggregate_type).type_id;
  ASSERT_TRUE(second_aggregate.decl_type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_ctx_intern_qual_type_in(
                  second.semantic_context,
                  second_aggregate_members[0].decl_type).type_id !=
              PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_find_global_var_in(
                  first.global_registry,
                  (char *)"shared_global", 13) == &first_global);
  ASSERT_TRUE(ps_find_global_var_in(
                  second.global_registry,
                  (char *)"shared_global", 13) == &second_global);
  ASSERT_TRUE(ps_find_string_lit_by_label_in(
                  first.global_registry,
                  (char *)".Lshared") == &first_literal);
  ASSERT_TRUE(ps_find_string_lit_by_label_in(
                  second.global_registry,
                  (char *)".Lshared") == NULL);

  ASSERT_TRUE(ag_compilation_session_activate(&second));
  ps_local_registry_reset_in(second.local_registry);
  ps_local_registry_reset_in(first.local_registry);
  ps_ctx_enter_block_scope_in(first.semantic_context);
  ps_decl_enter_scope_in(first.local_registry);
  const psx_typedef_info_t isolated_typedef = {
      .decl_type = ps_type_new_integer(TK_INT, 4, 0),
  };
  ASSERT_TRUE(ps_ctx_register_typedef_name_in_contexts(
      first.semantic_context, first.local_registry,
      (char *)"FirstType", 9, &isolated_typedef, NULL, NULL));
  ASSERT_TRUE(ps_ctx_register_enum_const_in_contexts(
      first.semantic_context, first.local_registry,
      (char *)"FIRST_ENUM", 10, 37, NULL));
  ASSERT_TRUE(ps_ctx_register_tag_type_in_contexts(
      first.semantic_context, first.local_registry,
      TK_STRUCT, (char *)"FirstTag", 8, 0, 0, 0, 0));
  psx_local_lookup_point_t first_namespace_point =
      ps_local_registry_capture_lookup_point_in(first.local_registry);
  const psx_type_t *isolated_typedef_type = NULL;
  long long isolated_enum_value = 0;
  ASSERT_TRUE(ps_ctx_find_typedef_decl_type_at_in_contexts(
      first.semantic_context, first.local_registry,
      (char *)"FirstType", 9, first_namespace_point,
      &isolated_typedef_type));
  ASSERT_TRUE(isolated_typedef_type != NULL);
  ASSERT_TRUE(ps_ctx_find_enum_const_at_in_contexts(
      first.semantic_context, first.local_registry,
      (char *)"FIRST_ENUM", 10, first_namespace_point,
      &isolated_enum_value));
  ASSERT_EQ(37, isolated_enum_value);
  ASSERT_TRUE(ps_ctx_clone_tag_type_at_in_contexts(
      first.semantic_context, first.local_registry,
      TK_STRUCT, (char *)"FirstTag", 8,
      first_namespace_point) != NULL);
  token_t *nested_context_tokens = tk_tokenize_ctx(
      &first.tokenizer,
      (char *)"int (*callback)(struct NestedContextParameter { "
               "FirstType member; "
               "_Static_assert(sizeof(FirstType) == 4, \"ok\"); "
               "} *); }");
  tk_set_current_token_ctx(&first.tokenizer, nested_context_tokens);
  ASSERT_TRUE(ag_compilation_session_is_active(&second));
  psx_parsed_aggregate_body_t nested_context_body;
  psx_parse_aggregate_body_with_options(
      &nested_context_body,
      &(psx_decl_specifier_syntax_options_t){
          .semantic_context = first.semantic_context,
          .global_registry = first.global_registry,
          .local_registry = first.local_registry,
          .runtime_context = first.parser_runtime_context,
      });
  ASSERT_TRUE(ag_compilation_session_is_active(&second));
  ASSERT_EQ(1, nested_context_body.item_count);
  psx_parsed_declarator_t *nested_context_callback =
      &nested_context_body.items[0].value.member_declaration.declarators[0];
  ASSERT_EQ(1, nested_context_callback->function_suffix_count);
  psx_parsed_function_parameters_t *nested_context_parameters =
      nested_context_callback->function_suffixes[0].parameters;
  ASSERT_TRUE(nested_context_parameters != NULL);
  ASSERT_EQ(1, nested_context_parameters->count);
  psx_parsed_aggregate_body_t *nested_parameter_body =
      nested_context_parameters->items[0].specifier
          .tag_action.aggregate_body;
  ASSERT_TRUE(nested_parameter_body != NULL);
  ASSERT_EQ(2, nested_parameter_body->item_count);
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            nested_parameter_body->items[0].value.member_declaration
                .specifier.source);
  ASSERT_EQ(PSX_PARSED_AGGREGATE_STATIC_ASSERT,
            nested_parameter_body->items[1].kind);
  ASSERT_TRUE(nested_parameter_body->items[1].value.static_assertion
                  .condition != NULL);
  psx_dispose_parsed_aggregate_body(&nested_context_body);
  token_ident_t isolated_typedef_token = {
      .str = (char *)"FirstType",
      .len = 9,
  };
  psx_parsed_type_name_t isolated_type_name_syntax = {
      .specifier = {
          .source = PSX_PARSED_DECL_TYPEDEF_NAME,
          .typedef_name = &isolated_typedef_token,
      },
  };
  psx_type_name_ref_t isolated_type_name = {
      .syntax = &isolated_type_name_syntax,
      .scope_seq = first_namespace_point.scope_seq,
      .declaration_seq = first_namespace_point.declaration_seq,
  };
  const psx_type_t *isolated_resolved_type =
      psx_resolve_bound_type_name_ref_in_contexts(
          first.semantic_context, first.global_registry,
          first.local_registry,
          &isolated_type_name);
  ASSERT_TRUE(isolated_resolved_type != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, isolated_resolved_type->kind);
  ASSERT_EQ(4, ps_ctx_type_sizeof_in(
                   first.semantic_context, isolated_resolved_type));
  psx_identifier_resolution_t isolated_global_resolution;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = first.semantic_context,
          .global_registry = first.global_registry,
          .local_registry = first.local_registry,
          .name = (char *)"shared_global",
          .name_len = 13,
      },
      &isolated_global_resolution);
  ASSERT_EQ(PSX_IDENTIFIER_GLOBAL_OBJECT,
            isolated_global_resolution.kind);
  ASSERT_TRUE(isolated_global_resolution.global == &first_global);
  node_identifier_t isolated_global_identifier = {
      .base = {.kind = ND_IDENTIFIER},
      .name = (char *)"shared_global",
      .name_len = 13,
      .scope_seq = first_namespace_point.scope_seq,
      .declaration_seq = first_namespace_point.declaration_seq,
  };
  node_t *isolated_global_node = psx_bind_identifier_tree_in_contexts(
      first.semantic_context, first.global_registry,
      first.local_registry, (node_t *)&isolated_global_identifier, NULL);
  ASSERT_TRUE(isolated_global_node != NULL);
  ASSERT_EQ(ND_GVAR, isolated_global_node->kind);
  ASSERT_TRUE(((node_gvar_t *)isolated_global_node)->symbol ==
              &first_global);
  ASSERT_EQ(4, ps_node_type_size(isolated_global_node));
  int isolated_global_constant_ok = 1;
  ASSERT_EQ(41, psx_eval_const_int(
                    isolated_global_node,
                    &isolated_global_constant_ok));
  ASSERT_TRUE(isolated_global_constant_ok);
  psx_parsed_initializer_t isolated_global_initializer = {0};
  psx_global_declaration_pipeline_result_t isolated_global_result;
  ASSERT_TRUE(psx_apply_global_declaration_pipeline(
      &(psx_global_declaration_pipeline_request_t){
          .semantic_context = first.semantic_context,
          .global_registry = first.global_registry,
          .local_registry = first.local_registry,
          .lowering_context = first.lowering_context,
          .options = ag_compilation_session_options_view(&first),
          .name = (char *)"pipeline_first",
          .name_len = 14,
          .type = ps_type_new_integer(TK_INT, 4, 0),
          .initializer = &isolated_global_initializer,
      },
      &isolated_global_result));
  ASSERT_TRUE(isolated_global_result.global != NULL);
  ASSERT_TRUE(ps_find_global_var_in(
      first.global_registry, (char *)"pipeline_first", 14) ==
      isolated_global_result.global);
  ASSERT_TRUE(ps_find_global_var_in(
      second.global_registry, (char *)"pipeline_first", 14) == NULL);
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(
      second.semantic_context, (char *)"FirstType", 9, NULL));
  ASSERT_TRUE(!ps_ctx_find_enum_const_in(
      second.semantic_context, (char *)"FIRST_ENUM", 10, NULL));
  ASSERT_TRUE(!ps_ctx_has_tag_type_in(
      second.semantic_context, TK_STRUCT, (char *)"FirstTag", 8));
  ps_decl_leave_scope_in(first.local_registry);
  ps_ctx_leave_block_scope_in(first.semantic_context);
  lvar_t *lowered_into_first = lower_complete_local_object(
      &(psx_local_object_request_t){
          .local_registry = first.local_registry,
          .lowering_context = first.lowering_context,
          .name = (char *)"lowered_into_first",
          .name_len = 18,
          .type = ps_type_new_integer(TK_INT, 4, 0),
      });
  ASSERT_TRUE(lowered_into_first != NULL);
  ASSERT_TRUE(ps_decl_find_lvar_in(second.local_registry,
                  (char *)"lowered_into_first", 18) == NULL);
  ASSERT_TRUE(ps_decl_find_lvar_in(
                  first.local_registry,
                  (char *)"lowered_into_first", 18) ==
              lowered_into_first);
  psx_local_registry_add_in(
      first.local_registry, &explicit_first_local);
  ASSERT_TRUE(ps_decl_find_lvar_in(second.local_registry,
                  (char *)"explicit_first_local", 20) == NULL);
  ASSERT_TRUE(ps_decl_find_lvar_in(
                  first.local_registry,
                  (char *)"explicit_first_local", 20) ==
              &explicit_first_local);
  psx_local_lookup_point_t explicit_first_point =
      ps_local_registry_capture_lookup_point_in(first.local_registry);
  ASSERT_TRUE(ps_local_registry_find_visible_in(
                  first.local_registry,
                  (char *)"explicit_first_local", 20,
                  explicit_first_point) == &explicit_first_local);
  psx_register_string_lit_in(second.global_registry, &second_literal);
  psx_local_registry_add_in(second.local_registry, &second_local);
  ASSERT_TRUE(ps_find_global_var_in(
                  second.global_registry,
                  (char *)"shared_global", 13) == &second_global);
  ASSERT_TRUE(ps_find_string_lit_by_label_in(
                  second.global_registry,
                  (char *)".Lshared") == &second_literal);
  ASSERT_TRUE(ps_decl_find_lvar_in(second.local_registry,
                  (char *)"shared_local", 12) == &second_local);
  ir_data_module_t *first_data =
      lower_ir_translation_unit_data_in_session(&first);
  ASSERT_TRUE(first_data != NULL);
  ir_data_object_t *first_global_data =
      ir_data_module_find_object(first_data, "shared_global", 13);
  ir_data_object_t *first_literal_data =
      ir_data_module_find_object(first_data, ".Lshared", 8);
  ir_data_object_t *first_callback_data =
      ir_data_module_find_object(first_data, "session_callback", 16);
  ir_data_object_t *first_aggregate_data =
      ir_data_module_find_object(first_data, "session_aggregate", 17);
  ASSERT_TRUE(first_global_data != NULL);
  ASSERT_TRUE(first_literal_data != NULL);
  ASSERT_TRUE(first_callback_data != NULL);
  ASSERT_TRUE(first_callback_data->relocs != NULL);
  ASSERT_EQ(IR_DATA_RELOC_FUNCTION, first_callback_data->relocs->kind);
  ASSERT_TRUE(first_aggregate_data != NULL);
  ASSERT_EQ(11, first_aggregate_data->bytes[0]);
  ASSERT_EQ(22, first_aggregate_data->bytes[4]);
  ASSERT_EQ(41, first_global_data->bytes[0]);
  ASSERT_TRUE(memcmp(first_literal_data->bytes, "first\0", 6) == 0);
  ir_data_module_free(first_data);
  ag_compilation_session_deactivate(&second);

  ASSERT_TRUE(ag_compilation_session_activate(&first));
  ps_local_registry_reset_in(first.local_registry);
  ASSERT_TRUE(ps_find_global_var_in(
                  first.global_registry,
                  (char *)"shared_global", 13) == &first_global);
  ASSERT_TRUE(ps_find_string_lit_by_label_in(
                  first.global_registry,
                  (char *)".Lshared") == &first_literal);
  ASSERT_TRUE(ps_decl_find_lvar_in(first.local_registry,
                  (char *)"shared_local", 12) == NULL);
  psx_local_registry_add_in(first.local_registry, &first_local);
  ASSERT_TRUE(ps_decl_find_lvar_in(first.local_registry,
                  (char *)"shared_local", 12) == &first_local);
  ir_data_module_t *second_data =
      lower_ir_translation_unit_data_in_session(&second);
  ASSERT_TRUE(second_data != NULL);
  ir_data_object_t *second_global_data =
      ir_data_module_find_object(second_data, "shared_global", 13);
  ir_data_object_t *second_literal_data =
      ir_data_module_find_object(second_data, ".Lshared", 8);
  ir_data_object_t *second_callback_data =
      ir_data_module_find_object(second_data, "session_callback", 16);
  ir_data_object_t *second_aggregate_data =
      ir_data_module_find_object(second_data, "session_aggregate", 17);
  ASSERT_TRUE(second_global_data != NULL);
  ASSERT_TRUE(second_literal_data != NULL);
  ASSERT_TRUE(second_callback_data != NULL);
  ASSERT_TRUE(second_callback_data->relocs != NULL);
  ASSERT_EQ(IR_DATA_RELOC_DATA, second_callback_data->relocs->kind);
  ASSERT_TRUE(second_aggregate_data != NULL);
  ASSERT_EQ(33, second_aggregate_data->bytes[0]);
  ASSERT_EQ(44, second_aggregate_data->bytes[8]);
  ASSERT_EQ(99, second_global_data->bytes[0]);
  ASSERT_TRUE(memcmp(second_literal_data->bytes, "second\0", 7) == 0);
  ir_data_module_free(second_data);
  ag_compilation_session_deactivate(&first);

  ASSERT_TRUE(ag_compilation_session_activate(&second));
  ASSERT_TRUE(ps_decl_find_lvar_in(second.local_registry,
                  (char *)"shared_local", 12) == &second_local);
  ag_compilation_session_deactivate(&second);

  ag_compilation_session_dispose(&first);
  ag_compilation_session_dispose(&second);
}

typedef struct {
  void *previous;
  int activate_count;
  int deactivate_count;
  int destroy_count;
} test_backend_context_t;

typedef struct {
  char bytes[64];
  size_t length;
} test_codegen_output_t;

static void *test_active_backend_context;

static void test_codegen_capture(
    const char *line, size_t length, void *user_data) {
  test_codegen_output_t *output = user_data;
  if (!output || output->length + length >= sizeof(output->bytes)) return;
  memcpy(output->bytes + output->length, line, length);
  output->length += length;
  output->bytes[output->length] = '\0';
}

static void test_backend_activate(void *context) {
  test_backend_context_t *backend = context;
  backend->previous = test_active_backend_context;
  test_active_backend_context = backend;
  backend->activate_count++;
}

static void test_backend_deactivate(void *context) {
  test_backend_context_t *backend = context;
  test_active_backend_context = backend->previous;
  backend->previous = NULL;
  backend->deactivate_count++;
}

static void test_backend_destroy(void *context) {
  test_backend_context_t *backend = context;
  backend->destroy_count++;
}

static void test_compilation_session_owns_target_and_tokenizer() {
  printf("test_compilation_session_owns_target_and_tokenizer...\n");
  ag_target_info_t host_target = ag_target_info_host();
  ag_target_info_t wasm_target = ag_target_info_wasm32();
  ag_compilation_session_t host;
  ag_compilation_session_t wasm;
  test_backend_context_t host_backend = {0};
  test_backend_context_t wasm_backend = {0};
  test_codegen_output_t host_output = {0};
  test_codegen_output_t wasm_output = {0};
  ASSERT_TRUE(ag_compilation_session_init(&host, &host_target));
  ASSERT_TRUE(ag_compilation_session_init(&wasm, &wasm_target));
  ASSERT_TRUE(ag_compilation_session_set_backend_context(
      &host, &host_backend, test_backend_activate,
      test_backend_deactivate, test_backend_destroy));
  ASSERT_TRUE(ag_compilation_session_set_backend_context(
      &wasm, &wasm_backend, test_backend_activate,
      test_backend_deactivate, test_backend_destroy));
  ASSERT_TRUE(ag_compilation_session_is_complete(&host));
  ASSERT_TRUE(ag_compilation_session_is_complete(&wasm));
  ASSERT_TRUE(ag_compilation_session_semantic_context(&host) ==
              host.semantic_context);
  ASSERT_TRUE(ag_compilation_session_global_registry(&host) ==
              host.global_registry);
  ASSERT_TRUE(ag_compilation_session_local_registry(&host) ==
              host.local_registry);
  ASSERT_TRUE(ag_compilation_session_preprocessor_context(&host) ==
              host.preprocessor_context);
  ASSERT_TRUE(ag_compilation_session_arena_context(&host) ==
              host.arena_context);
  ASSERT_TRUE(ag_compilation_session_diagnostic_context(&host) ==
              host.diagnostic_context);
  ASSERT_TRUE(ps_ctx_diagnostics(host.semantic_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(ps_parser_runtime_diagnostics(host.parser_runtime_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(pp_context_diagnostics(host.preprocessor_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(ps_lowering_diagnostics(host.lowering_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(ag_compilation_session_codegen_emit_context(&host) ==
              host.codegen_emit_context);
  ASSERT_TRUE(ag_compilation_session_parser_runtime_context(&host) ==
              host.parser_runtime_context);
  ASSERT_TRUE(ag_compilation_session_lowering_context(&host) ==
              host.lowering_context);
  ASSERT_TRUE(ps_ctx_arena(host.semantic_context) == host.arena_context);
  ASSERT_TRUE(ps_parser_runtime_arena(host.parser_runtime_context) ==
              host.arena_context);
  ASSERT_TRUE(ps_parser_runtime_tokenizer(host.parser_runtime_context) ==
              &host.tokenizer);
  ASSERT_TRUE(ps_lowering_arena(host.lowering_context) ==
              host.arena_context);
  ASSERT_TRUE(ag_compilation_session_options(&host) == &host.options);
  ASSERT_TRUE(ag_compilation_session_options(&wasm) == &wasm.options);
  ASSERT_TRUE(ag_compilation_session_options(&host) !=
              ag_compilation_session_options(&wasm));
  ASSERT_TRUE(ag_compilation_session_options_view(&host)
                  ->enable_size_compatible_nonscalar_cast);
  ASSERT_TRUE(ag_compilation_session_options_view(&wasm)
                  ->enable_union_array_member_nonbrace_init);
  ASSERT_TRUE(ag_compilation_session_semantic_context(&wasm) ==
              wasm.semantic_context);
  ASSERT_TRUE(ag_compilation_session_global_registry(&wasm) ==
              wasm.global_registry);
  ASSERT_TRUE(ag_compilation_session_local_registry(&wasm) ==
              wasm.local_registry);
  ASSERT_TRUE(ag_compilation_session_preprocessor_context(&wasm) ==
              wasm.preprocessor_context);
  ASSERT_TRUE(ps_ctx_diagnostics(wasm.semantic_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(ps_parser_runtime_diagnostics(wasm.parser_runtime_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(pp_context_diagnostics(wasm.preprocessor_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(ps_lowering_diagnostics(wasm.lowering_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(ag_compilation_session_codegen_emit_context(&wasm) ==
              wasm.codegen_emit_context);
  ASSERT_TRUE(ps_ctx_arena(wasm.semantic_context) == wasm.arena_context);
  ASSERT_TRUE(ps_parser_runtime_arena(wasm.parser_runtime_context) ==
              wasm.arena_context);
  ASSERT_TRUE(ps_parser_runtime_tokenizer(wasm.parser_runtime_context) ==
              &wasm.tokenizer);
  ASSERT_TRUE(ps_parser_runtime_tokenizer(host.parser_runtime_context) !=
              ps_parser_runtime_tokenizer(wasm.parser_runtime_context));
  ASSERT_TRUE(ps_lowering_arena(wasm.lowering_context) ==
              wasm.arena_context);
  ASSERT_EQ(8, ag_target_info_pointer_size(
                   ag_compilation_session_target(&host)));
  ASSERT_EQ(4, ag_target_info_pointer_size(
                   ag_compilation_session_target(&wasm)));
  ASSERT_EQ(8, ag_target_info_pointer_size(
                   ps_ctx_target_info(host.semantic_context)));
  ASSERT_EQ(4, ag_target_info_pointer_size(
                   ps_ctx_target_info(wasm.semantic_context)));
  ASSERT_EQ(8, ag_target_info_pointer_size(
                   ps_lowering_target(host.lowering_context)));
  ASSERT_EQ(4, ag_target_info_pointer_size(
                   ps_lowering_target(wasm.lowering_context)));
  ASSERT_TRUE(psx_frontend_reset_translation_unit_state_in_session(&host));
  ASSERT_TRUE(psx_frontend_reset_translation_unit_state_in_session(&wasm));
  ASSERT_EQ(8, ag_target_info_pointer_size(
                   ps_ctx_target_info(host.semantic_context)));
  ASSERT_EQ(4, ag_target_info_pointer_size(
                   ps_ctx_target_info(wasm.semantic_context)));
  ASSERT_EQ(8, ag_target_info_pointer_size(
                   ps_lowering_target(host.lowering_context)));
  ASSERT_EQ(4, ag_target_info_pointer_size(
                   ps_lowering_target(wasm.lowering_context)));
  ASSERT_TRUE(host.preprocessor_context != NULL);
  ASSERT_TRUE(wasm.preprocessor_context != NULL);
  ASSERT_TRUE(host.preprocessor_context != wasm.preprocessor_context);
  ASSERT_TRUE(host.arena_context != NULL);
  ASSERT_TRUE(wasm.arena_context != NULL);
  ASSERT_TRUE(host.arena_context != wasm.arena_context);
  ASSERT_TRUE(host.diagnostic_context != NULL);
  ASSERT_TRUE(wasm.diagnostic_context != NULL);
  ASSERT_TRUE(host.diagnostic_context != wasm.diagnostic_context);
  diag_context_set_locale(host.diagnostic_context, "en");
  diag_context_set_locale(wasm.diagnostic_context, "ja");
  ASSERT_TRUE(strcmp(
      diag_context_get_locale(host.diagnostic_context), "en") == 0);
  ASSERT_TRUE(strcmp(
      diag_context_get_locale(wasm.diagnostic_context), "ja") == 0);
  diag_report_internalf_in(
      host.diagnostic_context, DIAG_ERR_INTERNAL_USAGE,
      "%s", "host diagnostic isolation");
  ASSERT_TRUE(diag_has_error_records_in(host.diagnostic_context));
  ASSERT_TRUE(!diag_has_error_records_in(wasm.diagnostic_context));
  diag_reset_records_in(host.diagnostic_context);
  ASSERT_TRUE(!diag_has_error_records_in(host.diagnostic_context));
  ASSERT_TRUE(host.token_allocator_context != NULL);
  ASSERT_TRUE(wasm.token_allocator_context != NULL);
  ASSERT_TRUE(host.token_allocator_context != wasm.token_allocator_context);
  ASSERT_TRUE(ag_compilation_session_token_allocator_context(&host) ==
              host.token_allocator_context);
  ASSERT_TRUE(ag_compilation_session_token_allocator_context(&wasm) ==
              wasm.token_allocator_context);
  ASSERT_TRUE(tk_context_allocator(&host.tokenizer) ==
              host.token_allocator_context);
  ASSERT_TRUE(tk_context_allocator(&wasm.tokenizer) ==
              wasm.token_allocator_context);
  ASSERT_TRUE(host.parser_runtime_context != NULL);
  ASSERT_TRUE(wasm.parser_runtime_context != NULL);
  ASSERT_TRUE(host.parser_runtime_context != wasm.parser_runtime_context);
  ASSERT_TRUE(host.lowering_context != NULL);
  ASSERT_TRUE(wasm.lowering_context != NULL);
  ASSERT_TRUE(host.lowering_context != wasm.lowering_context);
  ASSERT_TRUE(host.codegen_emit_context != NULL);
  ASSERT_TRUE(wasm.codegen_emit_context != NULL);
  ASSERT_TRUE(host.codegen_emit_context != wasm.codegen_emit_context);
  token_t *active_cursor_before_explicit_statement = tk_get_current_token();
  token_t wasm_statement_eof = {.kind = TK_EOF};
  token_t wasm_statement = {
      .kind = TK_SEMI,
      .next = &wasm_statement_eof,
  };
  tk_set_current_token_ctx(&wasm.tokenizer, &wasm_statement);
  node_t *wasm_null_statement = psx_stmt_stmt_in_contexts(
      wasm.semantic_context, wasm.global_registry, wasm.local_registry,
      wasm.parser_runtime_context, NULL);
  ASSERT_TRUE(wasm_null_statement != NULL);
  ASSERT_EQ(ND_NUM, wasm_null_statement->kind);
  ASSERT_TRUE(tk_at_eof_ctx(&wasm.tokenizer));
  ASSERT_TRUE(tk_get_current_token() ==
              active_cursor_before_explicit_statement);
  ASSERT_TRUE(arena_alloc_in(host.arena_context, 16) != NULL);
  ASSERT_TRUE(arena_alloc_in(wasm.arena_context, 32) != NULL);
  ASSERT_TRUE(arena_current_reserved_bytes_in(host.arena_context) > 0);
  ASSERT_TRUE(arena_current_reserved_bytes_in(wasm.arena_context) > 0);
  tokenizer_context_t *previous_tokenizer = tk_context_active();
  ag_compilation_session_t *previous_session = test_suite_session;
  ASSERT_TRUE(ag_compilation_session_is_active(previous_session));
  ASSERT_EQ(8, ag_compilation_session_target(previous_session)->pointer_size);
  ASSERT_TRUE(!ag_compilation_session_is_active(&host));
  ASSERT_TRUE(!ag_compilation_session_is_active(&wasm));
  ASSERT_TRUE(ag_compilation_session_activate(&host));
  ASSERT_TRUE(ag_compilation_session_is_active(&host));
  ASSERT_TRUE(!ag_compilation_session_is_active(&wasm));
  ag_target_set_pointer_size(4);
  ASSERT_EQ(8, ag_compilation_session_target(&host)->pointer_size);
  ASSERT_TRUE(tk_context_active() == previous_tokenizer);
  gen_set_output_callback_in(
      ag_compilation_session_codegen_emit_context(&host),
      test_codegen_capture, &host_output);
  gen_set_output_callback_in(
      ag_compilation_session_codegen_emit_context(&wasm),
      test_codegen_capture, &wasm_output);
  cg_emitf_in(
      ag_compilation_session_codegen_emit_context(&host), "host-a");
  cg_emitf_in(
      ag_compilation_session_codegen_emit_context(&wasm),
      "wasm-explicit-");
  ASSERT_TRUE(test_active_backend_context == &host_backend);
  ASSERT_EQ(1, host_backend.activate_count);
  uint16_t host_filename = tk_filename_intern_ctx(
      &host.tokenizer, "host-session.c");
  ASSERT_TRUE(strcmp(tk_filename_lookup_ctx(
                         &host.tokenizer, host_filename),
                     "host-session.c") == 0);
  host.lowering_context->aggregate_cast_temp_sequence = 9;
  ASSERT_EQ(0, tk_allocator_total_chunks_in(host.token_allocator_context));
  ASSERT_TRUE(tk_allocator_calloc_in(
                  host.token_allocator_context, 1, 16) != NULL);
  ASSERT_EQ(1, tk_allocator_total_chunks_in(host.token_allocator_context));
  ASSERT_EQ(0, tk_allocator_total_chunks_in(wasm.token_allocator_context));
  tk_ctx_set_enable_c11_audit_extensions(&host.tokenizer, true);
  ASSERT_TRUE(host.tokenizer.enable_c11_audit_extensions);
  ASSERT_TRUE(!wasm.tokenizer.enable_c11_audit_extensions);
  pragma_pack_set_in(host.parser_runtime_context, 4);
  ag_compilation_session_options(&host)
      ->enable_union_scalar_pointer_cast = false;
  ASSERT_EQ(4, host.parser_runtime_context->pragma_pack_current);
  ASSERT_TRUE(!ag_compilation_session_options_view(&host)
                   ->enable_union_scalar_pointer_cast);
  ASSERT_EQ(0, wasm.parser_runtime_context->pragma_pack_current);
  ASSERT_TRUE(ag_compilation_session_options_view(&wasm)
                  ->enable_union_scalar_pointer_cast);
  tk_set_tolerate_untokenizable_ctx(&host.tokenizer, true);
  ASSERT_TRUE(host.tokenizer.tolerate_untokenizable);
  ASSERT_TRUE(!wasm.tokenizer.tolerate_untokenizable);
  ASSERT_TRUE(ag_compilation_session_activate(&wasm));
  ASSERT_TRUE(!ag_compilation_session_is_active(&host));
  ASSERT_TRUE(ag_compilation_session_is_active(&wasm));
  ag_target_set_pointer_size(8);
  ASSERT_EQ(4, ag_compilation_session_target(&wasm)->pointer_size);
  ASSERT_TRUE(tk_context_active() == previous_tokenizer);
  cg_emitf_in(
      ag_compilation_session_codegen_emit_context(&wasm), "wasm");
  cg_emitf_in(
      ag_compilation_session_codegen_emit_context(&host),
      "-host-explicit");
  ASSERT_TRUE(test_active_backend_context == &wasm_backend);
  ASSERT_EQ(1, wasm_backend.activate_count);
  uint16_t wasm_filename = tk_filename_intern_ctx(
      &wasm.tokenizer, "wasm-session.c");
  ASSERT_EQ(host_filename, wasm_filename);
  ASSERT_TRUE(strcmp(tk_filename_lookup_ctx(
                         &wasm.tokenizer, wasm_filename),
                     "wasm-session.c") == 0);
  tk_filename_reset_translation_unit_ctx(&wasm.tokenizer);
  ASSERT_TRUE(tk_filename_lookup_ctx(
                  &wasm.tokenizer, wasm_filename) == NULL);
  ASSERT_EQ(0, wasm.lowering_context->aggregate_cast_temp_sequence);
  ASSERT_EQ(0, pragma_pack_current_alignment_in(
                   wasm.parser_runtime_context));
  ASSERT_TRUE(ag_compilation_session_options_view(&wasm)
                  ->enable_union_scalar_pointer_cast);
  ASSERT_EQ(0, tk_allocator_total_chunks_in(wasm.token_allocator_context));
  ASSERT_EQ(1, tk_allocator_total_chunks_in(host.token_allocator_context));
  ASSERT_TRUE(!ag_compilation_session_deactivate(&host));
  ASSERT_TRUE(ag_compilation_session_is_active(&wasm));
  ASSERT_TRUE(tk_context_active() == previous_tokenizer);
  ASSERT_TRUE(test_active_backend_context == &wasm_backend);
  ASSERT_EQ(0, host_backend.deactivate_count);
  ASSERT_TRUE(!ag_compilation_session_dispose(&host));
  ASSERT_TRUE(ag_compilation_session_is_complete(&host));
  ASSERT_TRUE(ag_compilation_session_is_active(&wasm));
  ASSERT_EQ(0, host_backend.destroy_count);
  ag_compilation_session_t inherited_context;
  ASSERT_TRUE(ag_compilation_session_init(
      &inherited_context, ag_compilation_session_target(&wasm)));
  ASSERT_EQ(4, ag_compilation_session_target(&inherited_context)
                   ->pointer_size);
  ASSERT_TRUE(ag_compilation_session_dispose(&inherited_context));
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(&host));
  ASSERT_EQ(0, arena_current_reserved_bytes_in(host.arena_context));
  ASSERT_TRUE(arena_current_reserved_bytes_in(wasm.arena_context) > 0);
  ASSERT_TRUE(ag_compilation_session_deactivate(&wasm));
  ASSERT_TRUE(ag_compilation_session_is_active(&host));
  ASSERT_TRUE(!ag_compilation_session_is_active(&wasm));
  ASSERT_EQ(8, ag_compilation_session_target(&host)->pointer_size);
  ASSERT_TRUE(tk_context_active() == previous_tokenizer);
  cg_emitf_in(
      ag_compilation_session_codegen_emit_context(&host), "-host-b");
  ASSERT_TRUE(test_active_backend_context == &host_backend);
  ASSERT_EQ(1, wasm_backend.deactivate_count);
  ASSERT_TRUE(strcmp(tk_filename_lookup_ctx(
                         &host.tokenizer, host_filename),
                     "host-session.c") == 0);
  ASSERT_EQ(9, host.lowering_context->aggregate_cast_temp_sequence);
  ASSERT_EQ(4, pragma_pack_current_alignment_in(
                   host.parser_runtime_context));
  ASSERT_TRUE(!ag_compilation_session_options_view(&host)
                   ->enable_union_scalar_pointer_cast);
  ASSERT_EQ(1, tk_allocator_total_chunks_in(host.token_allocator_context));
  ASSERT_EQ(0, tk_allocator_total_chunks_in(wasm.token_allocator_context));
  ASSERT_TRUE(ag_compilation_session_deactivate(&host));
  ASSERT_TRUE(ag_compilation_session_is_active(previous_session));
  ASSERT_TRUE(!ag_compilation_session_is_active(&host));
  ASSERT_TRUE(tk_context_active() == previous_tokenizer);
  ASSERT_TRUE(strcmp(
      host_output.bytes, "host-a-host-explicit-host-b") == 0);
  ASSERT_TRUE(strcmp(wasm_output.bytes, "wasm-explicit-wasm") == 0);
  ASSERT_TRUE(test_active_backend_context == NULL);
  ASSERT_EQ(1, host_backend.deactivate_count);

  tokenizer_context_t *host_tokenizer =
      ag_compilation_session_tokenizer(&host);
  tokenizer_context_t *wasm_tokenizer =
      ag_compilation_session_tokenizer(&wasm);
  ASSERT_TRUE(host_tokenizer != NULL);
  ASSERT_TRUE(wasm_tokenizer != NULL);
  ASSERT_TRUE(host_tokenizer != wasm_tokenizer);
  tk_set_filename_ctx(host_tokenizer, "host.c");
  tk_set_filename_ctx(wasm_tokenizer, "wasm.c");
  tk_ctx_set_strict_c11_mode(host_tokenizer, true);
  ASSERT_TRUE(strcmp(tk_get_filename_ctx(host_tokenizer), "host.c") == 0);
  ASSERT_TRUE(strcmp(tk_get_filename_ctx(wasm_tokenizer), "wasm.c") == 0);
  ASSERT_TRUE(tk_ctx_get_strict_c11_mode(host_tokenizer));
  ASSERT_TRUE(!tk_ctx_get_strict_c11_mode(wasm_tokenizer));

  ag_target_set_pointer_size(4);
  ASSERT_EQ(4, ag_target_info_pointer_size(
                   ag_compilation_session_target(&wasm)));
  ASSERT_EQ(8, ag_compilation_session_target(previous_session)->pointer_size);
  ag_target_set_pointer_size(8);
  ASSERT_TRUE(ag_compilation_session_dispose(&host));
  ASSERT_TRUE(ag_compilation_session_dispose(&wasm));
  ASSERT_EQ(1, host_backend.destroy_count);
  ASSERT_EQ(1, wasm_backend.destroy_count);
}

int main() {
  ag_compilation_session_t suite_session;
  ag_target_info_t suite_target = ag_target_info_host();
  ASSERT_TRUE(ag_compilation_session_init(&suite_session, &suite_target));
  tokenizer_context_t *previous_test_tokenizer =
      tk_context_activate(&suite_session.tokenizer);
  ASSERT_TRUE(ag_compilation_session_activate(&suite_session));
  test_suite_session = &suite_session;
  printf("Running tests for Parser...\n");

  test_arena_checkpoint_rollback();
  test_semantic_type_identity();
  test_semantic_context_isolation();
  test_compilation_session_owns_target_and_tokenizer();
  test_compilation_session_registry_isolation();
  test_expr_number();
  test_expr_add_sub();
  test_additive_semantic_lowering_boundary();
  test_subscript_semantic_lowering_boundary();
  test_unary_deref_semantic_lowering_boundary();
  test_unary_operator_semantic_lowering_boundary();
  test_generic_selection_semantic_lowering_boundary();
  test_sizeof_semantic_lowering_boundary();
  test_expression_type_materialization_boundary();
  test_function_call_type_binding_boundary();
  test_cast_semantic_lowering_boundary();
  test_aggregate_cast_semantic_lowering_boundary();
  test_implicit_conversion_semantic_lowering_boundary();
  test_compound_assignment_semantic_lowering_boundary();
  test_translation_unit_frontend_boundary();
  test_toplevel_static_assert_frontend_boundary();
  test_toplevel_declaration_frontend_boundary();
  test_toplevel_callback_context_boundary();
  test_toplevel_compound_initializer_frontend_boundary();
  test_toplevel_point_of_declaration_boundary();
  test_toplevel_single_parse_classification_boundary();
  test_frontend_stream_lifecycle_boundary();
  test_local_declaration_frontend_boundary();
  test_function_parameter_point_of_declaration_boundary();
  test_identifier_resolution_boundary();
  test_persistent_local_scope_lookup_boundary();
  test_member_access_resolution_boundary();
  test_complex_initializer_semantic_lowering_boundary();
  test_local_declaration_storage_plan_boundary();
  test_target_type_layout_boundary();
  test_wasm_target_global_pointer_data_layout();
  test_vla_lowering_request_boundary();
  test_parameter_declaration_storage_plan_boundary();
  test_global_declaration_resolution_boundary();
  test_declaration_pipeline_order_boundary();
  test_tag_declaration_resolution_boundary();
  test_aggregate_definition_ownership_boundary();
  test_aggregate_body_phase_boundary();
  test_declaration_phase_boundary();
  test_type_name_phase_boundary();
  test_toplevel_declarator_phase_boundary();
  test_local_declarator_application_boundary();
  test_local_declaration_resolution_boundary();
  test_aggregate_member_resolution_boundary();
  test_static_assert_resolution_boundary();
  test_typedef_declaration_resolution_boundary();
  test_enum_constant_resolution_boundary();
  test_initializer_resolution_boundary();
  test_local_initializer_parse_lowering_boundary();
  test_static_data_initializer_boundary();
  test_expr_mul_div();
  test_expr_mod();
  test_expr_precedence();
  test_expr_parentheses();
  test_expr_eq_neq();
  test_expr_relational();
  test_expr_bitwise();
  test_expr_shift();
  test_expr_logical_and_or();
  test_expr_ternary();
  test_expr_unary_ops();
  test_expr_generic();
  test_expr_sizeof();
  test_expr_inc_dec();
  test_expr_assign();
  test_expr_compound_assign();
  test_expr_comma();
  test_program_funcdef();
  test_funcall();
  test_funcdef_with_params();
  test_stmt_if();
  test_stmt_if_else();
  test_stmt_while();
  test_stmt_do_while();
  test_stmt_break_continue();
  test_stmt_switch_case_default();
  test_stmt_for();
  test_stmt_for_with_decl_init();
  test_stmt_return();
  test_stmt_block();
  test_stmt_goto_label();
  test_expr_deref_addr();
  test_expr_member_access();
  test_expr_string();
  test_expr_concat_string();
  test_expr_float();
  test_expr_long_double_suffix_metadata();
  test_expr_compound_literal();
  test_expr_compound_literal_array_subscript();
  test_type_decl();
  test_type_metadata_bridge();
  test_recursive_declarator_capacity_boundary();
  test_translation_unit_reset_static_local_state();
  test_translation_unit_reset_anonymous_tag_state();
  test_translation_unit_reset_decl_locals_state();
  test_translation_unit_reset_pragma_pack_state();
  test_multiple_funcdefs();
  test_parse_invalid();
  test_parse_invalid_diagnostics();
  test_parse_evil_edge_cases();
  test_parser_config_matrix();
  test_expr_nest_limits();
  test_parser_width_limits();
  test_semantic_canonical_type_invariant();

  tk_context_activate(previous_test_tokenizer);
  ASSERT_TRUE(ag_compilation_session_dispose(&suite_session));
  test_suite_session = NULL;
  printf("OK: All unit tests passed!\n");
  return 0;
}

#include "../src/codegen_emit.h"
#include "../src/compilation_session_internal.h"
#include "../src/declaration_pipeline.h"
#include "../src/diag/diag.h"
#include "../src/frontend/local_declaration.h"
#include "../src/frontend/semantic_pipeline.h"
#include "../src/frontend/semantic_pipeline_internal.h"
#include "../src/frontend/toplevel_declaration.h"
#include "../src/frontend/translation_unit.h"
#include "../src/hir/hir.h"
#include "../src/lowering/abi_lowering.h"
#include "../src/lowering/abi_target_policy.h"
#include "../src/lowering/global_object_lowering.h"
#include "../src/lowering/hir_ir_builder.h"
#include "../src/lowering/local_object_lowering.h"
#include "../src/lowering/local_storage.h"
#include "../src/lowering/mir_type_lowering.h"
#include "../src/lowering/parameter_lowering.h"
#include "../src/lowering/parameter_storage_plan.h"
#include "../src/lowering/runtime_context.h"
#include "../src/lowering/static_data_initializer.h"
#include "../src/lowering/static_local_lowering.h"
#include "../src/lowering/translation_unit_data_lowering.h"
#include "../src/lowering/vla_lowering.h"
#include "../src/parser/aggregate_member_syntax.h"
#include "../src/parser/arena.h"
#include "../src/parser/decl.h"
#include "../src/parser/declaration_binding_events.h"
#include "../src/parser/declarator_shape_builder.h"
#include "../src/parser/expr.h"
#include "../src/parser/function_parameter_syntax.h"
#include "../src/parser/function_public.h"
#include "../src/parser/global_registry.h"
#include "../src/parser/gvar_public.h"
#include "../src/parser/literal_public.h"
#include "../src/parser/local_registry.h"
#include "../src/parser/lvar_internal.h"
#include "../src/parser/lvar_public.h"
#include "../src/parser/name_environment.h"
#include "../src/parser/node_utils.h"
#include "../src/parser/parser.h"
#include "../src/parser/runtime_context.h"
#include "../src/parser/semantic_ctx.h"
#include "../src/parser/stmt.h"
#include "../src/parser/statement_syntax_adapter.h"
#include "../src/parser/symtab.h"
#include "../src/pragma_pack.h"
#include "../src/preprocess/preprocess.h"
#include "../src/semantic/aggregate_member_resolution.h"
#include "../src/semantic/call_resolution.h"
#include "../src/semantic/compound_literal_semantics.h"
#include "../src/semantic/declaration_application.h"
#include "../src/semantic/declaration_registration.h"
#include "../src/semantic/declaration_resolution.h"
#include "../src/semantic/enum_constant_resolution.h"
#include "../src/semantic/expression_operand_resolution.h"
#include "../src/semantic/function_call_resolution.h"
#include "../src/semantic/function_declaration_resolution.h"
#include "../src/semantic/function_definition_resolution.h"
#include "../src/semantic/function_parameter_resolution.h"
#include "../src/semantic/generic_selection_resolution.h"
#include "../src/semantic/global_declaration_resolution.h"
#include "../src/semantic/hir_member_resolution.h"
#include "../src/semantic/identifier_resolution.h"
#include "../src/semantic/initializer_resolution.h"
#include "../src/semantic/literal_resolution.h"
#include "../src/semantic/local_declaration_plan.h"
#include "../src/semantic/local_declaration_resolution.h"
#include "../src/semantic/parameter_declaration_resolution.h"
#include "../src/semantic/prototype_parameter.h"
#include "../src/semantic/scope_graph.h"
#include "../src/semantic/semantic_node_builder.h"
#include "../src/semantic/semantic_tree_resolution.h"
#include "../src/semantic/static_assert_resolution.h"
#include "../src/semantic/static_initializer_resolution.h"
#include "../src/semantic/syntax_typed_hir_resolution.h"
#include "../src/semantic/tag_declaration_resolution.h"
#include "../src/semantic/type_name_resolution.h"
#include "../src/type_system/integer_conversion.h"
#include "../src/semantic/type_query_semantics.h"
#include "../src/semantic/typed_hir_materialization.h"
#include "../src/semantic/typed_hir_tree_internal.h"
#include "../src/semantic/typedef_declaration_resolution.h"
#include "../src/semantic/vla_runtime_plan.h"
#include "../src/source_manager.h"
#include "../src/tokenizer/allocator.h"
#include "../src/tokenizer/tokenizer.h"
#include "../src/type_layout.h"
#include "../src/type_signature.h"
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test_common.h"

static int test_target_pointer_size(const ag_target_info_t *target) {
  return ag_data_layout_pointer_size(ag_target_info_data_layout(target));
}

static int test_target_pointer_alignment(const ag_target_info_t *target) {
  return ag_data_layout_pointer_alignment(ag_target_info_data_layout(target));
}

static int test_target_scalar_size(const ag_target_info_t *target,
                                   ag_target_scalar_kind_t kind) {
  return ag_data_layout_scalar_size(ag_target_info_data_layout(target), kind);
}

static int test_target_scalar_alignment(const ag_target_info_t *target,
                                        ag_target_scalar_kind_t kind) {
  return ag_data_layout_scalar_alignment(ag_target_info_data_layout(target),
                                         kind);
}

static int resolve_program_input_hir(
    ag_compilation_session_t *test_suite_session, const char *input);
static void expect_parse_fail(
    ag_compilation_session_t *test_suite_session, const char *input);
static const psx_hir_node_t *find_test_hir_node_kind(
    const psx_hir_module_t *hir, psx_hir_node_kind_t kind,
    size_t occurrence);
static const psx_hir_node_t *find_test_named_hir_node(
    const psx_hir_module_t *hir, psx_hir_node_kind_t kind,
    const char *expected_name, size_t occurrence);

static ag_diagnostic_context_t *test_diagnostics(
    ag_compilation_session_t *test_suite_session) {
  return ag_compilation_session_diagnostic_context(test_suite_session);
}

static arena_context_t *test_arena_context(
    ag_compilation_session_t *test_suite_session) {
  return ag_compilation_session_arena_context(test_suite_session);
}

static tokenizer_context_t *test_tokenizer(
    ag_compilation_session_t *test_suite_session) {
  return ag_compilation_session_tokenizer(test_suite_session);
}

typedef struct {
  psx_semantic_context_t *context;
  psx_scope_graph_t *scope_graph;
} test_semantic_context_fixture_t;

static int test_semantic_context_fixture_init(
    ag_compilation_session_t *test_suite_session,
    test_semantic_context_fixture_t *fixture,
    const ag_target_info_t *target) {
  if (!fixture) return 0;
  memset(fixture, 0, sizeof(*fixture));
  fixture->scope_graph = psx_scope_graph_create();
  fixture->context = ps_ctx_create(
      test_arena_context(test_suite_session), test_diagnostics(test_suite_session),
      fixture->scope_graph, target);
  if (fixture->context) return 1;
  psx_scope_graph_destroy(fixture->scope_graph);
  memset(fixture, 0, sizeof(*fixture));
  return 0;
}

static void test_semantic_context_fixture_dispose(
    test_semantic_context_fixture_t *fixture) {
  if (!fixture) return;
  ps_ctx_destroy(fixture->context);
  psx_scope_graph_destroy(fixture->scope_graph);
  memset(fixture, 0, sizeof(*fixture));
}
#define ps_declarator_shape_append_pointer(...) \
  ps_declarator_shape_append_pointer_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define ps_declarator_shape_append_array(...) \
  ps_declarator_shape_append_array_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define ps_declarator_shape_append_array_ex(...) \
  ps_declarator_shape_append_array_ex_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define ps_declarator_shape_append_vla_array(...) \
  ps_declarator_shape_append_vla_array_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define ps_declarator_shape_append_function(...) \
  ps_declarator_shape_append_function_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define ps_declarator_op_set_function_param_qual_types(...) \
  ps_declarator_op_set_function_param_qual_types_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define ps_declarator_shape_copy(...) \
  ps_declarator_shape_copy_in(test_arena_context(test_suite_session), __VA_ARGS__)

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
#define psx_node_new_raw_binary(...) \
  psx_node_new_raw_binary_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define ps_node_compound_literal_array_size(...) \
  test_node_compound_literal_array_size(__VA_ARGS__)
static psx_semantic_context_t *test_semantic_context(
    ag_compilation_session_t *test_suite_session);

#define psx_node_new_source_cast(...) \
  psx_node_new_source_cast_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define psx_node_new_unary_deref_syntax_for(...) \
  psx_node_new_unary_deref_syntax_for_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define psx_node_new_subscript_syntax_for(...) \
  psx_node_new_subscript_syntax_for_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define psx_node_new_raw_assign(...) \
  psx_node_new_raw_assign_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define psx_node_new_raw_decl_initializer(...) \
  psx_node_new_raw_decl_initializer_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define psx_node_new_compound_literal(...) \
  psx_node_new_compound_literal_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define psx_node_new_raw_decl_initializer_list(...) \
  psx_node_new_raw_decl_initializer_list_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)
#define psx_node_new_initializer_list(...) \
  psx_node_new_initializer_list_in( \
      test_arena_context(test_suite_session), __VA_ARGS__)

static psx_semantic_context_t *test_semantic_context(
    ag_compilation_session_t *test_suite_session) {
  return ag_compilation_session_semantic_context(test_suite_session);
}

static psx_scope_graph_t *test_scope_graph(
    ag_compilation_session_t *test_suite_session) {
  return ps_ctx_scope_graph(test_semantic_context(test_suite_session));
}

static psx_scope_lookup_point_t test_scope_lookup_point(
    ag_compilation_session_t *test_suite_session) {
  return psx_scope_graph_capture_lookup_point(test_scope_graph(test_suite_session));
}

static int test_has_function_type_in(
    psx_semantic_context_t *semantic_context,
    char *name, int len) {
  return ps_ctx_get_function_qual_type_in(
             semantic_context, name, len).type_id !=
         PSX_TYPE_ID_INVALID;
}


static psx_global_registry_t *test_global_registry(
    ag_compilation_session_t *test_suite_session) {
  return ag_compilation_session_global_registry(test_suite_session);
}

static psx_local_registry_t *test_local_registry(
    ag_compilation_session_t *test_suite_session) {
  return ag_compilation_session_local_registry(test_suite_session);
}

static bool test_find_tag_member(
    psx_semantic_context_t *semantic_context,
    token_kind_t kind, char *name, int len,
    char *member_name, int member_len,
    psx_record_member_decl_t *out_declaration,
    psx_record_member_layout_t *out_layout) {
  if (!semantic_context || !out_declaration || !out_layout) return false;
  psx_record_id_t record_id = ps_ctx_resolve_tag_record_id_in(
      semantic_context, kind, name, len);
  int member_index = -1;
  if (record_id == PSX_RECORD_ID_INVALID ||
      !ps_ctx_find_record_member_in(
          semantic_context, record_id, member_name, member_len,
          &member_index, out_declaration))
    return false;
  const psx_record_layout_t *record_layout =
      psx_record_layout_table_lookup(
          ps_ctx_record_layout_table_in(semantic_context),
          record_id, ps_ctx_data_layout(semantic_context));
  const psx_record_member_layout_t *member_layout =
      psx_record_layout_member(record_layout, member_index);
  if (!member_layout) return false;
  *out_layout = *member_layout;
  return true;
}

static bool test_semantic_has_tag_type(
    ag_compilation_session_t *test_suite_session,
    token_kind_t kind, char *name, int len) {
  return ps_ctx_has_tag_type_in(
      test_semantic_context(test_suite_session), kind, name, len);
}

static int test_tag_member_count(
    ag_compilation_session_t *test_suite_session,
    token_kind_t kind, char *name, int len) {
  psx_semantic_context_t *context = test_semantic_context(test_suite_session);
  psx_qual_type_t tag_type = ps_ctx_tag_qual_type_at_in(
      context, kind, name, len,
      psx_scope_graph_capture_lookup_point(ps_ctx_scope_graph(context)));
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          ps_ctx_semantic_type_table_in(context), tag_type.type_id,
          &shape))
    return -1;
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      ps_ctx_record_decl_table_in(context), shape.record_id);
  return record ? record->member_count : -1;
}

static int test_semantic_register_tag_type(
    ag_compilation_session_t *test_suite_session,
    token_kind_t kind, char *name, int len,
    int is_complete, int member_count, int tag_size, int tag_align);

static void test_semantic_define_tag_type_with_layout(
    ag_compilation_session_t *test_suite_session,
    token_kind_t kind, char *name, int len,
    int member_count, int tag_size, int tag_align) {
  int is_complete = member_count > 0 || tag_size > 0 || tag_align > 0;
  ASSERT_TRUE(test_semantic_register_tag_type(test_suite_session,
      kind, name, len, is_complete, member_count, tag_size, tag_align));
}

static int test_semantic_register_tag_type(
    ag_compilation_session_t *test_suite_session,
    token_kind_t kind, char *name, int len,
    int is_complete, int member_count, int tag_size, int tag_align) {
  if (!ps_ctx_register_tag_type_in(
      test_semantic_context(test_suite_session),
      kind, name, len, is_complete, member_count))
    return 0;
  if (!is_complete || (kind != TK_STRUCT && kind != TK_UNION)) return 1;
  const psx_record_decl_t *record = ps_ctx_ensure_tag_record_decl_in(
      test_semantic_context(test_suite_session), kind, name, len);
  return record && ps_ctx_publish_record_layout_in(
      test_semantic_context(test_suite_session), record->record_id,
      tag_size, tag_align > 0 ? tag_align : 1);
}

static int test_semantic_define_enum_const(
    ag_compilation_session_t *test_suite_session,
    char *name, int len, long long value) {
  return ps_ctx_register_enum_const_in(
      test_semantic_context(test_suite_session),
      name, len, value, NULL);
}

static int test_semantic_define_typedef_name(
    ag_compilation_session_t *test_suite_session,
    char *name, int len, const psx_typedef_info_t *info) {
  return ps_ctx_register_typedef_name_in(
      test_semantic_context(test_suite_session),
      name, len, info, NULL, NULL);
}

static ir_abi_module_t *test_lower_ir_abi(
    ag_compilation_session_t *test_suite_session, const ir_module_t *module) {
  ir_abi_type_context_t context = {
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .target = ag_compilation_session_target(test_suite_session),
  };
  return ir_abi_lower_module(&context, module);
}

static long test_function_abi_value(
    ag_compilation_session_t *test_suite_session,
    const ir_module_t *module, int field, size_t index) {
  ir_abi_module_t *abi = test_lower_ir_abi(test_suite_session, module);
  const ir_abi_signature_t *signature = abi && module && module->funcs
      ? ir_abi_function_signature(abi, module->funcs) : NULL;
  long value = -1;
  if (signature) {
    if (field == 0) value = (long)signature->param_count;
    else if (field == 1 && index < signature->param_count)
      value = signature->param_pieces[index].type;
    else if (field == 2) value = signature->is_variadic;
    else if (field == 3)
      value = ir_abi_signature_result_source_size(signature);
    else if (field == 5 && index < signature->param_count)
      value = (long)signature->param_pieces[index].source_index;
    else if (field == 6 && index < signature->param_count)
      value = signature->param_pieces[index].byte_offset;
    else if (field == 7 && index < signature->param_count)
      value = signature->param_pieces[index].kind;
    else if (field == 8)
      value = ir_abi_signature_direct_result_type(signature);
    else if (field == 9) value = (long)signature->result_count;
    else if (field == 10 && index < signature->result_count)
      value = signature->result_pieces[index].type;
    else if (field == 11 && index < signature->result_count)
      value = signature->result_pieces[index].byte_offset;
    else if (field == 12 && index < signature->result_count)
      value = signature->result_pieces[index].kind;
  }
  ir_abi_module_free(abi);
  return value;
}

static long test_call_abi_value(
    ag_compilation_session_t *test_suite_session,
    const ir_module_t *module, const ir_inst_t *call,
    int field, size_t index) {
  ir_abi_module_t *abi = test_lower_ir_abi(test_suite_session, module);
  const ir_abi_signature_t *signature =
      abi ? ir_abi_call_signature(abi, call) : NULL;
  long value = -1;
  if (signature) {
    if (field == 0) value = (long)signature->fixed_param_count;
    else if (field == 1 && index < signature->param_count)
      value = signature->param_pieces[index].type;
    else if (field == 2) value = signature->is_variadic;
    else if (field == 3)
      value = ir_abi_signature_result_source_size(signature);
    else if (field == 4)
      value = ir_abi_signature_direct_result_type(signature);
  }
  ir_abi_module_free(abi);
  return value;
}

static long test_reference_abi_value(
    ag_compilation_session_t *test_suite_session,
    const ir_module_t *module, const ir_inst_t *reference,
    int field, size_t index) {
  ir_abi_module_t *abi = test_lower_ir_abi(test_suite_session, module);
  const ir_abi_signature_t *signature =
      abi ? ir_abi_reference_signature(abi, reference) : NULL;
  long value = -1;
  if (signature) {
    if (field == 0) value = (long)signature->param_count;
    else if (field == 1 && index < signature->param_count)
      value = signature->param_pieces[index].type;
    else if (field == 2)
      value = ir_abi_signature_direct_result_type(signature);
    else if (field == 3) value = signature->is_variadic;
  }
  ir_abi_module_free(abi);
  return value;
}

static global_var_t *find_test_global_var_in(
    psx_global_registry_t *registry, char *name, int len) {
  psx_scope_graph_t *scope_graph =
      ps_global_registry_scope_graph(registry);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_lookup_declaration_in_scope(
          scope_graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, name, len);
  return declaration && declaration->kind == PSX_DECL_GLOBAL_OBJECT
             ? declaration->payload
             : NULL;
}

static lvar_t *find_test_visible_local_var_in(
    const psx_local_registry_t *registry, char *name, int len,
    psx_scope_lookup_point_t point) {
  psx_scope_graph_t *scope_graph =
      ps_local_registry_scope_graph(registry);
  psx_decl_id_t declaration_id = psx_scope_graph_lookup(
      scope_graph, PSX_NAMESPACE_ORDINARY, name, len, point);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(scope_graph, declaration_id);
  return declaration && declaration->kind == PSX_DECL_LOCAL_OBJECT
             ? declaration->payload
             : NULL;
}

static lvar_t *find_test_local_var_in(
    const psx_local_registry_t *registry, char *name, int len) {
  return find_test_visible_local_var_in(
      registry, name, len,
      psx_scope_graph_capture_lookup_point(
          ps_local_registry_scope_graph(registry)));
}

static global_var_t *find_test_global_var(
    ag_compilation_session_t *test_suite_session, char *name, int len) {
  return find_test_global_var_in(test_global_registry(test_suite_session), name, len);
}

static bool iter_test_float_literals(
    ag_compilation_session_t *test_suite_session,
    float_lit_visitor_t visitor, void *user) {
  return ps_iter_float_literals_in(
      test_global_registry(test_suite_session), visitor, user);
}

static ag_compilation_options_t *test_compilation_options(
    ag_compilation_session_t *test_suite_session) {
  return ag_compilation_session_options(test_suite_session);
}

static psx_lowering_context_t *test_lowering_context(
    ag_compilation_session_t *test_suite_session) {
  return ag_compilation_session_lowering_context(test_suite_session);
}

static void set_test_typedef_fixture_type(
    ag_compilation_session_t *test_suite_session,
    psx_typedef_info_t *info, psx_qual_type_t type) {
  ASSERT_TRUE(info != NULL);
  ASSERT_TRUE(type.type_id != PSX_TYPE_ID_INVALID);
  info->decl_type_table = ps_ctx_semantic_type_table_in(
      test_semantic_context(test_suite_session));
  info->decl_qual_type = type;
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      info->decl_type_table, info->decl_qual_type));
}

static int test_type_size_id(
    ag_compilation_session_t *test_suite_session, psx_type_id_t type_id) {
  return psx_type_layout_sizeof(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session)), type_id,
      ag_target_info_data_layout(ps_ctx_target_info(test_semantic_context(test_suite_session))));
}
































static void reset_test_locals(
    ag_compilation_session_t *test_suite_session) {
  ps_decl_reset_locals_in(
      ag_compilation_session_local_registry(test_suite_session));
  local_storage_reset(test_lowering_context(test_suite_session));
}

static void set_test_current_funcname(
    ag_compilation_session_t *test_suite_session, char *name, int len) {
  ps_decl_set_current_funcname_in(
      ag_compilation_session_local_registry(test_suite_session), name, len);
}

static void reset_test_translation_unit_state(
    ag_compilation_session_t *test_suite_session) {
  ASSERT_TRUE(ag_compilation_session_reset_translation_unit(
      test_suite_session));
}

static int resolve_test_program_hir_from_in_session(
    ag_compilation_session_t *session, token_t *start) {
  psx_frontend_stream_t stream = {0};
  if (!psx_frontend_stream_begin(
          &stream, session, NULL, start))
    return 0;
  psx_frontend_function_t function;
  while (psx_frontend_next_function(&stream, &function)) {}
  return psx_frontend_stream_end(&stream);
}

static int resolve_test_program_hir_from(
    ag_compilation_session_t *test_suite_session, token_t *start) {
  return resolve_test_program_hir_from_in_session(
      test_suite_session, start);
}

static node_t *parse_test_expression_from(
    ag_compilation_session_t *test_suite_session, token_t *start) {
  psx_parser_runtime_context_t *runtime =
      ag_compilation_session_parser_runtime_context(test_suite_session);
  tokenizer_context_t *tokenizer =
      ag_compilation_session_tokenizer(test_suite_session);
  tokenizer_context_t *previous =
      ps_parser_runtime_bind_tokenizer(runtime, tokenizer);
  tk_set_current_token_ctx(tokenizer, start);
  psx_name_classifier_t classifier =
      ps_ctx_name_classifier(test_semantic_context(test_suite_session));
  node_t *node = psx_expr_expr_with_syntax_services(
      runtime, &classifier, NULL);
  if (previous)
    ps_parser_runtime_bind_tokenizer(runtime, previous);
  return node;
}

typedef struct {
  psx_parser_runtime_context_t *runtime_context;
  const psx_name_classifier_t *name_classifier;
} test_declaration_expression_service_t;

static node_t *parse_test_declaration_assignment_expression(
    void *context) {
  test_declaration_expression_service_t *service = context;
  return psx_expr_assign_with_syntax_services(
      service->runtime_context,
      service->name_classifier, NULL);
}

static node_t *parse_test_toplevel_assignment_expression(
    void *context) {
  psx_parser_stream_t *stream = context;
  return psx_expr_assign_with_syntax_services(
      stream ? stream->runtime_context : NULL,
      stream ? &stream->syntax.name_classifier : NULL, NULL);
}

static int parse_test_toplevel_static_assert(
    void *context,
    psx_parsed_static_assert_declaration_t *assertion,
    const psx_name_classifier_t *name_classifier) {
  (void)name_classifier;
  psx_parse_static_assert_syntax_with_context(
      assertion,
      &(psx_static_assert_syntax_context_t){
          .context = context,
          .runtime_context = context
              ? ((psx_parser_stream_t *)context)->runtime_context
              : NULL,
          .parse_assignment_expression =
              parse_test_toplevel_assignment_expression,
      });
  return 1;
}

static int parse_test_toplevel_declaration_head(
    void *context,
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_name_classifier_t *name_classifier) {
  return psx_parse_toplevel_declaration_head_syntax_with_context(
      declaration,
      &(psx_toplevel_declaration_syntax_context_t){
          .context = context,
          .name_classifier = *name_classifier,
          .runtime_context = context
              ? ((psx_parser_stream_t *)context)->runtime_context
              : NULL,
          .parse_assignment_expression =
              parse_test_toplevel_assignment_expression,
      });
}

static int finish_test_toplevel_declaration(
    void *context,
    psx_parsed_toplevel_declaration_t *declaration,
    const psx_name_classifier_t *name_classifier) {
  return psx_finish_toplevel_declaration_syntax_with_context(
      declaration,
      &(psx_toplevel_declaration_syntax_context_t){
          .context = context,
          .name_classifier = *name_classifier,
          .runtime_context = context
              ? ((psx_parser_stream_t *)context)->runtime_context
              : NULL,
          .parse_assignment_expression =
              parse_test_toplevel_assignment_expression,
      });
}

static void begin_test_parser_stream(
    ag_compilation_session_t *test_suite_session,
    psx_parser_stream_t *stream,
    tokenizer_context_t *tokenizer_context, token_t *start) {
  psx_parser_syntax_services_t syntax = {
      .context = stream,
      .runtime_context =
          ag_compilation_session_parser_runtime_context(
              test_suite_session),
      .name_classifier = ps_ctx_name_classifier(
          test_semantic_context(test_suite_session)),
      .parse_static_assert =
          parse_test_toplevel_static_assert,
      .parse_toplevel_declaration_head =
          parse_test_toplevel_declaration_head,
      .finish_toplevel_declaration =
          finish_test_toplevel_declaration,
  };
  ps_parser_stream_begin_with_syntax(
      stream, tokenizer_context, start, &syntax);
}

typedef struct {
  psx_name_classifier_t outer;
} test_aggregate_name_classifier_context_t;

static int test_aggregate_name_is_typedef(
    void *context, const token_t *token) {
  test_aggregate_name_classifier_context_t *classifier_context = context;
  if (token && token->kind == TK_IDENT) {
    const token_ident_t *identifier = (const token_ident_t *)token;
    if ((identifier->len == 13 &&
         strncmp(identifier->str, "DeferredAlias", 13) == 0) ||
        (identifier->len == 13 &&
         strncmp(identifier->str, "DeferredParam", 13) == 0))
      return 1;
  }
  return classifier_context &&
         ps_name_classifier_is_typedef_name(
             &classifier_context->outer, token);
}

static void parse_test_aggregate_body(
    ag_compilation_session_t *test_suite_session, psx_parsed_aggregate_body_t *body) {
  test_aggregate_name_classifier_context_t classifier_context = {
      .outer = ps_ctx_name_classifier(test_semantic_context(test_suite_session)),
  };
  psx_name_classifier_t name_classifier = {
      .context = &classifier_context,
      .is_typedef_name = test_aggregate_name_is_typedef,
  };
  test_declaration_expression_service_t expression_service = {
      .runtime_context =
          ag_compilation_session_parser_runtime_context(test_suite_session),
      .name_classifier = &name_classifier,
  };
  psx_parse_aggregate_body_with_options(
      body,
      &(psx_decl_specifier_syntax_options_t){
          .expression_context = &expression_service,
          .parse_assignment_expression =
              parse_test_declaration_assignment_expression,
          .name_classifier = &name_classifier,
          .runtime_context =
              ag_compilation_session_parser_runtime_context(test_suite_session),
      });
}

static int parse_test_toplevel_declaration_syntax(
    ag_compilation_session_t *test_suite_session,
    psx_parsed_toplevel_declaration_t *declaration) {
  psx_parser_name_environment_t name_environment;
  ps_parser_name_environment_init(
      &name_environment,
      ps_ctx_name_classifier(test_semantic_context(test_suite_session)));
  psx_name_classifier_t name_classifier =
      ps_parser_name_environment_classifier(&name_environment);
  test_declaration_expression_service_t expression_service = {
      .runtime_context =
          ag_compilation_session_parser_runtime_context(
              test_suite_session),
      .name_classifier = &name_classifier,
  };
  int parsed = psx_parse_toplevel_declaration_syntax_with_context(
      declaration,
      &(psx_toplevel_declaration_syntax_context_t){
          .context = &expression_service,
          .name_classifier = name_classifier,
          .runtime_context = expression_service.runtime_context,
          .parse_assignment_expression =
              parse_test_declaration_assignment_expression,
      });
  ps_parser_name_environment_dispose(&name_environment);
  return parsed;
}

static void parse_test_decl_specifier_syntax(
    ag_compilation_session_t *test_suite_session,
    psx_parsed_decl_specifier_t *specifier) {
  psx_name_classifier_t name_classifier =
      ps_ctx_name_classifier(test_semantic_context(test_suite_session));
  test_declaration_expression_service_t expression_service = {
      .runtime_context =
          ag_compilation_session_parser_runtime_context(test_suite_session),
      .name_classifier = &name_classifier,
  };
  psx_parse_decl_specifier_syntax_ex(
      specifier,
      &(psx_decl_specifier_syntax_options_t){
          .name_classifier = &name_classifier,
          .expression_context = &expression_service,
          .parse_assignment_expression =
              parse_test_declaration_assignment_expression,
          .runtime_context = expression_service.runtime_context,
      });
}

static int parse_test_type_name_syntax_at(
    ag_compilation_session_t *test_suite_session,
    token_t *start, psx_parsed_type_name_t *type_name) {
  psx_name_classifier_t name_classifier =
      ps_ctx_name_classifier(test_semantic_context(test_suite_session));
  test_declaration_expression_service_t expression_service = {
      .runtime_context =
          ag_compilation_session_parser_runtime_context(test_suite_session),
      .name_classifier = &name_classifier,
  };
  return psx_parse_type_name_syntax_at(
      start,
      &(psx_decl_specifier_syntax_options_t){
          .name_classifier = &name_classifier,
          .expression_context = &expression_service,
          .parse_assignment_expression =
              parse_test_declaration_assignment_expression,
          .runtime_context =
              ag_compilation_session_parser_runtime_context(test_suite_session),
      },
      type_name);
}

typedef struct {
  const char *name;
  int name_len;
  int call_count;
} test_name_classifier_context_t;

static int test_name_classifier_is_typedef(
    void *context, const token_t *token) {
  test_name_classifier_context_t *classifier_context = context;
  classifier_context->call_count++;
  if (!token || token->kind != TK_IDENT) return 0;
  const token_ident_t *identifier = (const token_ident_t *)token;
  return identifier->len == classifier_context->name_len &&
         strncmp(identifier->str, classifier_context->name,
                 (size_t)identifier->len) == 0;
}

static void test_parser_name_environment_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parser_name_environment_boundary...\n");
  test_name_classifier_context_t outer_context = {
      .name = "Alias",
      .name_len = 5,
  };
  psx_name_classifier_t outer = {
      .context = &outer_context,
      .is_typedef_name = test_name_classifier_is_typedef,
  };
  psx_parser_name_environment_t environment;
  ps_parser_name_environment_init(&environment, outer);
  psx_name_classifier_t classifier =
      ps_parser_name_environment_classifier(&environment);

  token_t *alias = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"Alias");
  token_t *local = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"Local");
  ASSERT_TRUE(ps_name_classifier_is_typedef_name(
      &classifier, alias));

  ps_name_classifier_declare(&classifier, alias, 0);
  ASSERT_TRUE(!ps_name_classifier_is_typedef_name(
      &classifier, alias));

  ps_name_classifier_enter_scope(&classifier);
  ps_name_classifier_declare(&classifier, alias, 1);
  ps_name_classifier_declare(&classifier, local, 1);
  ASSERT_TRUE(ps_name_classifier_is_typedef_name(
      &classifier, alias));
  ASSERT_TRUE(ps_name_classifier_is_typedef_name(
      &classifier, local));
  ps_name_classifier_leave_scope(&classifier);

  ASSERT_TRUE(!ps_name_classifier_is_typedef_name(
      &classifier, alias));
  ASSERT_TRUE(!ps_name_classifier_is_typedef_name(
      &classifier, local));
  ps_parser_name_environment_reset(&environment, outer);
  ASSERT_TRUE(ps_name_classifier_is_typedef_name(
      &classifier, alias));
  ps_parser_name_environment_dispose(&environment);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "typedef int T; int main(void) { "
      "int T = 3; "
      "{ typedef int T; T value = 4; if (value != 4) return 1; } "
      "return T == 3 ? 0 : 2; }"));
}

typedef struct {
  psx_parser_runtime_context_t *runtime_context;
  psx_name_classifier_t name_classifier;
} test_expression_syntax_services_t;

typedef struct {
  psx_expression_syntax_context_t expression;
  int block_scope_depth;
  int block_enter_count;
  int block_leave_count;
} test_statement_syntax_services_t;

static int test_expression_parse_type_name(
    void *context, token_t *start, int runtime_bounds,
    psx_parsed_type_name_t *out) {
  test_expression_syntax_services_t *services = context;
  psx_decl_specifier_syntax_options_t options = {
      .name_classifier = &services->name_classifier,
      .runtime_context = services->runtime_context,
  };
  return runtime_bounds
             ? psx_parse_runtime_type_name_syntax_at(
                   start, &options, out)
             : psx_parse_type_name_syntax_at(
                   start, &options, out);
}

static node_t *test_statement_parse_expression(void *context) {
  test_statement_syntax_services_t *services = context;
  return psx_expr_expr_syntax(&services->expression);
}

static void test_statement_enter_block_scope(void *context) {
  test_statement_syntax_services_t *services = context;
  services->block_scope_depth++;
  services->block_enter_count++;
}

static void test_statement_leave_block_scope(void *context) {
  test_statement_syntax_services_t *services = context;
  services->block_scope_depth--;
  services->block_leave_count++;
}

static void test_parser_name_classifier_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parser_name_classifier_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  token_t *tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"Alias value;");
  test_name_classifier_context_t classifier_context = {
      .name = "Alias",
      .name_len = 5,
  };
  psx_name_classifier_t classifier = {
      .context = &classifier_context,
      .is_typedef_name = test_name_classifier_is_typedef,
  };
  psx_parsed_decl_specifier_t specifier;
  ASSERT_TRUE(psx_try_parse_decl_specifier_syntax_ex(
      &specifier,
      &(psx_decl_specifier_syntax_options_t){
          .name_classifier = &classifier,
          .runtime_context =
              ag_compilation_session_parser_runtime_context(
                  test_suite_session),
      }));
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME, specifier.source);
  ASSERT_TRUE(specifier.typedef_name != NULL);
  ASSERT_EQ(5, specifier.typedef_name->len);
  ASSERT_EQ(1, classifier_context.call_count);
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(
      test_semantic_context(test_suite_session), (char *)"Alias", 5, NULL));
  ASSERT_TRUE(tk_get_current_token_ctx(
                  ag_compilation_session_tokenizer(test_suite_session)) ==
              tokens->next);
  ps_dispose_decl_specifier_syntax(&specifier);

  classifier_context.call_count = 0;
  tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"sizeof(Alias)");
  node_t *sizeof_alias = psx_expr_expr_with_syntax_services(
      ag_compilation_session_parser_runtime_context(test_suite_session),
      &classifier, NULL);
  ASSERT_TRUE(sizeof_alias != NULL);
  ASSERT_EQ(ND_SIZEOF_QUERY, sizeof_alias->kind);
  ASSERT_TRUE(((node_sizeof_query_t *)sizeof_alias)->is_type_name);
  ASSERT_TRUE(classifier_context.call_count > 0);
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(
      test_semantic_context(test_suite_session), (char *)"Alias", 5, NULL));

  classifier_context.call_count = 0;
  tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"sizeof(Alias)");
  test_expression_syntax_services_t syntax_services = {
      .runtime_context =
          ag_compilation_session_parser_runtime_context(
              test_suite_session),
      .name_classifier = classifier,
  };
  psx_expression_syntax_context_t expression_syntax = {
      .context = &syntax_services,
      .runtime_context = syntax_services.runtime_context,
      .name_classifier = classifier,
      .parse_type_name = test_expression_parse_type_name,
  };
  node_t *standalone_sizeof =
      psx_expr_expr_syntax(&expression_syntax);
  ASSERT_TRUE(standalone_sizeof != NULL);
  ASSERT_EQ(ND_SIZEOF_QUERY, standalone_sizeof->kind);
  ASSERT_TRUE(
      ((node_sizeof_query_t *)standalone_sizeof)->is_type_name);
  ASSERT_TRUE(classifier_context.call_count > 0);

  tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"__va_arg_area");
  node_t *va_arg_area_syntax = psx_expr_expr_with_syntax_services(
      ag_compilation_session_parser_runtime_context(test_suite_session),
      &classifier, NULL);
  ASSERT_TRUE(va_arg_area_syntax != NULL);
  ASSERT_EQ(ND_IDENTIFIER, va_arg_area_syntax->kind);
  ASSERT_EQ(13, ((node_identifier_t *)va_arg_area_syntax)->name_len);

  tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"&value");
  node_t *address_syntax = psx_expr_expr_with_syntax_services(
      ag_compilation_session_parser_runtime_context(test_suite_session),
      &classifier, NULL);
  ASSERT_TRUE(address_syntax != NULL);
  ASSERT_EQ(ND_ADDRESS_OF, address_syntax->kind);
  ASSERT_TRUE(address_syntax->lhs != NULL);
  ASSERT_EQ(ND_IDENTIFIER, address_syntax->lhs->kind);

  tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"{ if (0) ; }");
  test_statement_syntax_services_t statement_services = {
      .expression = {
          .runtime_context = syntax_services.runtime_context,
          .name_classifier = classifier,
      },
  };
  psx_statement_syntax_context_t statement_syntax = {
      .context = &statement_services,
      .runtime_context = syntax_services.runtime_context,
      .name_classifier = classifier,
      .parse_expression = test_statement_parse_expression,
      .enter_block_scope = test_statement_enter_block_scope,
      .leave_block_scope = test_statement_leave_block_scope,
  };
  node_t *standalone_statement =
      psx_stmt_stmt_syntax(&statement_syntax);
  ASSERT_TRUE(standalone_statement != NULL);
  ASSERT_EQ(ND_BLOCK, standalone_statement->kind);
  node_block_t *standalone_block =
      (node_block_t *)standalone_statement;
  ASSERT_TRUE(standalone_block->body != NULL);
  ASSERT_TRUE(standalone_block->body[0] != NULL);
  ASSERT_EQ(ND_IF, standalone_block->body[0]->kind);
  ASSERT_TRUE(standalone_block->body[0]->lhs != NULL);
  ASSERT_EQ(ND_NUM, standalone_block->body[0]->lhs->kind);
  ASSERT_TRUE(standalone_block->body[0]->rhs != NULL);
  ASSERT_EQ(ND_NULL_STMT, standalone_block->body[0]->rhs->kind);
  ASSERT_EQ(0, statement_services.block_scope_depth);
  ASSERT_EQ(1, statement_services.block_enter_count);
  ASSERT_EQ(1, statement_services.block_leave_count);
}

static psx_parsed_declarator_t parse_test_declarator_syntax_tree(
    ag_compilation_session_t *test_suite_session) {
  test_declaration_expression_service_t expression_service = {
      .runtime_context =
          ag_compilation_session_parser_runtime_context(test_suite_session),
  };
  psx_parsed_declarator_t declarator;
  psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts(
      &declarator,
      &(psx_decl_specifier_syntax_options_t){
          .expression_context = &expression_service,
          .parse_assignment_expression =
              parse_test_declaration_assignment_expression,
          .runtime_context = expression_service.runtime_context,
      });
  return declarator;
}

static psx_frontend_expression_hir_t resolve_test_expression_hir(
    ag_compilation_session_t *test_suite_session,
    const node_t *syntax) {
  psx_frontend_expression_hir_t result = {
      .root = PSX_HIR_NODE_ID_INVALID,
  };
  ASSERT_TRUE(psx_frontend_resolve_expression_to_hir_in_contexts(
      test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
      test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
      ag_compilation_session_options_view(test_suite_session),
      syntax, syntax ? syntax->tok : NULL, &result));
  ASSERT_TRUE(result.module != NULL);
  ASSERT_TRUE(result.root != PSX_HIR_NODE_ID_INVALID);
  return result;
}

static const psx_hir_node_t *test_expression_hir_root(
    const psx_frontend_expression_hir_t *expression) {
  return expression && expression->module
             ? psx_hir_module_lookup(expression->module, expression->root)
             : NULL;
}

static const psx_hir_node_t *test_hir_child(
    const psx_frontend_expression_hir_t *expression,
    const psx_hir_node_t *parent, size_t index) {
  ASSERT_TRUE(expression != NULL);
  ASSERT_TRUE(expression->module != NULL);
  ASSERT_TRUE(parent != NULL);
  ASSERT_TRUE(index < psx_hir_node_child_count(parent));
  const psx_hir_node_t *child = psx_hir_module_lookup(
      expression->module, psx_hir_node_child_at(parent, index));
  ASSERT_TRUE(child != NULL);
  return child;
}

static psx_type_shape_t test_hir_type_shape(
    ag_compilation_session_t *test_suite_session,
    const psx_hir_node_t *node) {
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(node != NULL);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      psx_hir_node_qual_type(node).type_id, &shape));
  return shape;
}

static psx_type_shape_t test_qual_type_shape(
    ag_compilation_session_t *test_suite_session, psx_qual_type_t type) {
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      type.type_id, &shape));
  return shape;
}

static psx_qual_type_t test_qual_type_base(
    ag_compilation_session_t *test_suite_session, psx_qual_type_t type) {
  ASSERT_TRUE(type.type_id != PSX_TYPE_ID_INVALID);
  psx_qual_type_t base = psx_semantic_type_table_base(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      type.type_id);
  ASSERT_TRUE(base.type_id != PSX_TYPE_ID_INVALID);
  return base;
}

static const psx_hir_node_t *test_hir_array_decay_source(
    ag_compilation_session_t *test_suite_session,
    const psx_frontend_expression_hir_t *expression) {
  const psx_hir_node_t *address =
      test_expression_hir_root(expression);
  ASSERT_TRUE(address != NULL);
  ASSERT_EQ(PSX_HIR_ADDRESS, psx_hir_node_kind(address));
  ASSERT_EQ(PSX_TYPE_POINTER, test_hir_type_shape(test_suite_session, address).kind);
  ASSERT_EQ(1, psx_hir_node_child_count(address));
  const psx_hir_node_t *source = psx_hir_module_lookup(
      expression->module, psx_hir_node_child_at(address, 0));
  ASSERT_TRUE(source != NULL);
  ASSERT_EQ(PSX_HIR_STMT_EXPR, psx_hir_node_kind(source));
  ASSERT_EQ(PSX_TYPE_ARRAY, test_hir_type_shape(test_suite_session, source).kind);
  return source;
}

static psx_type_shape_t test_hir_attached_type_shape(
    ag_compilation_session_t *test_suite_session,
    const psx_hir_node_t *node) {
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(node != NULL);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      psx_hir_node_attached_qual_type(node).type_id, &shape));
  return shape;
}

static void apply_test_toplevel_declaration(
    ag_compilation_session_t *test_suite_session,
    psx_parsed_toplevel_declaration_t *declaration) {
  psx_apply_toplevel_declaration_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      ag_compilation_session_parser_runtime_context(test_suite_session),
      test_lowering_context(test_suite_session),
      ag_compilation_session_options_view(test_suite_session),
      declaration);
}

static int apply_test_declaration_phase(
    ag_compilation_session_t *test_suite_session,
    psx_declaration_phase_t *phase, int standalone_tag) {
  return psx_apply_declaration_phase_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      phase, standalone_tag);
}

static int apply_test_parsed_aggregate_body_layout(
    ag_compilation_session_t *test_suite_session,
    psx_parsed_aggregate_body_t *body,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    int *out_size, int *out_align) {
  return psx_apply_parsed_aggregate_body_layout_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      body, tag_kind, tag_name, tag_len, out_size, out_align);
}

static void apply_test_runtime_parsed_declarator(
    ag_compilation_session_t *test_suite_session,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application) {
  psx_apply_runtime_parsed_declarator_in_contexts(
      ag_compilation_session_semantic_context(test_suite_session),
      ag_compilation_session_global_registry(test_suite_session),
      ag_compilation_session_local_registry(test_suite_session),
      declarator, application);
}


static void parse_test_initializer_syntax_value(
    ag_compilation_session_t *test_suite_session,
    psx_parsed_initializer_t *initializer, token_t *assign_tok) {
  psx_frontend_local_declaration_syntax_adapter_t adapter;
  psx_local_declaration_callbacks_t syntax;
  psx_name_classifier_t classifier =
      ps_ctx_name_classifier(test_semantic_context(test_suite_session));
  psx_frontend_init_local_declaration_syntax_adapter(
      &adapter, &syntax,
      ag_compilation_session_parser_runtime_context(test_suite_session),
      &classifier);
  syntax.parse_initializer(
      syntax.context, initializer, assign_tok);
}

/* Test-only storage fixtures may start with a simple scalar/array type and
 * replace it with the exact type under test. Production registration accepts
 * only an explicit canonical type. */
static lvar_t *register_test_qual_type_storage_fixture_in(
    ag_compilation_session_t *test_suite_session,
    psx_lowering_context_t *lowering_context,
    char *name, int len, int size, int align,
    psx_qual_type_t qual_type) {
  if (qual_type.type_id == PSX_TYPE_ID_INVALID) return NULL;
  int offset = local_storage_allocate(
      lowering_context, size, align);
  return ps_local_registry_create_storage_object_qual_type_in(
      ag_compilation_session_local_registry(test_suite_session),
      name, len, offset, size, align, qual_type, NULL);
}

static lvar_t *register_test_qual_type_storage_fixture(
    ag_compilation_session_t *test_suite_session,
    char *name, int len, int size, int align,
    psx_qual_type_t qual_type) {
  return register_test_qual_type_storage_fixture_in(test_suite_session,
      test_lowering_context(test_suite_session), name, len, size, align, qual_type);
}

static lvar_t *register_test_storage_fixture_in(
    ag_compilation_session_t *test_suite_session,
    psx_lowering_context_t *lowering_context,
    char *name, int len, int size, int elem_size, int is_array) {
  int scalar_size = elem_size > 0 ? elem_size : size;
  if (scalar_size <= 0) scalar_size = 1;
  psx_qual_type_t type = ps_ctx_intern_integer_qual_type_in(
      test_semantic_context(test_suite_session),
      scalar_size > 4 ? PSX_INTEGER_KIND_LONG : PSX_INTEGER_KIND_INT,
      0, 0);
  if (is_array) {
    int array_len = size > 0 && size % scalar_size == 0
                        ? size / scalar_size
                        : 0;
    type = ps_ctx_intern_array_of_qual_type_in(
        test_semantic_context(test_suite_session), type, array_len, 0);
  }
  return register_test_qual_type_storage_fixture_in(test_suite_session,
      lowering_context, name, len, size, 0, type);
}

static lvar_t *register_test_storage_fixture(
    ag_compilation_session_t *test_suite_session,
    char *name, int len, int size, int elem_size, int is_array) {
  return register_test_storage_fixture_in(test_suite_session,
      test_lowering_context(test_suite_session), name, len, size, elem_size, is_array);
}

static lvar_t *register_test_default_storage_fixture(
    ag_compilation_session_t *test_suite_session, char *name, int len) {
  return register_test_storage_fixture(test_suite_session, name, len, 8, 8, 0);
}

static void set_test_storage_fixture_type(
    ag_compilation_session_t *test_suite_session,
    lvar_t *var, psx_qual_type_t type) {
  ASSERT_TRUE(var != NULL);
  ASSERT_TRUE(type.type_id != PSX_TYPE_ID_INVALID);
  var->decl_qual_type = type;
  var->decl_type_table = ps_ctx_semantic_type_table_in(
      test_semantic_context(test_suite_session));
  ASSERT_TRUE(ps_lvar_decl_type_id(var) != PSX_TYPE_ID_INVALID);
}

static void find_long_double_float_literal(float_lit_t *lit, void *user) {
  bool *found = user;
  if (lit->float_suffix_kind == TK_FLOAT_SUFFIX_L) *found = true;
}


/* Standalone expression tests model a function body without parsing its
 * declaration block, so predeclare the short fixture names they use. */
static void preregister_test_locals(
    ag_compilation_session_t *test_suite_session) {
  static char names[] = "abcdefghijklmnopqrstuvwxyz";
  for (int i = 0; i < 26; i++) {
    register_test_default_storage_fixture(test_suite_session, &names[i], 1);
  }
}

static node_t *parse_expr_input_with_existing_locals(
    ag_compilation_session_t *test_suite_session, const char *input) {
  set_test_current_funcname(test_suite_session, (char *)"__test__", 8);
  token_t *head = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input);
  return parse_test_expression_from(test_suite_session, head);
}

static psx_frontend_expression_hir_t resolve_test_expression_input_hir(
    ag_compilation_session_t *test_suite_session,
    const char *input, node_t **out_syntax) {
  reset_test_locals(test_suite_session);
  preregister_test_locals(test_suite_session);
  node_t *syntax = parse_expr_input_with_existing_locals(test_suite_session, input);
  ASSERT_TRUE(syntax != NULL);
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_hir(test_suite_session, syntax);
  if (out_syntax) *out_syntax = syntax;
  return expression;
}

static psx_frontend_expression_hir_t resolve_test_cast_input_hir(
    ag_compilation_session_t *test_suite_session,
    const char *input, node_t **out_syntax) {
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, input, &syntax);
  ASSERT_EQ(ND_SOURCE_CAST, syntax->kind);
  ASSERT_TRUE(syntax->lhs != NULL);
  ASSERT_EQ(PSX_HIR_CAST,
            psx_hir_node_kind(test_expression_hir_root(&expression)));
  if (out_syntax) *out_syntax = syntax;
  return expression;
}

static void assert_test_integer_cast_hir(
    ag_compilation_session_t *test_suite_session,
    const char *input, psx_integer_kind_t expected_kind,
    int expected_unsigned, psx_type_qualifiers_t expected_qualifiers,
    long long expected_operand) {
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_cast_input_hir(test_suite_session, input, &syntax);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  psx_type_shape_t shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(expected_kind, shape.integer_kind);
  ASSERT_EQ(expected_unsigned, shape.is_unsigned);
  ASSERT_EQ(expected_qualifiers,
            psx_hir_node_qual_type(root).qualifiers);
  const psx_hir_node_t *operand = test_hir_child(&expression, root, 0);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(operand));
  ASSERT_EQ(expected_operand, psx_hir_node_integer_value(operand));
  psx_frontend_expression_hir_dispose(&expression);
}

static void assert_test_nested_integer_cast_hir(
    ag_compilation_session_t *test_suite_session,
    const char *input, psx_integer_kind_t expected_outer_kind,
    int expected_outer_unsigned, psx_integer_kind_t expected_inner_kind,
    int expected_inner_unsigned) {
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_cast_input_hir(test_suite_session, input, &syntax);
  const psx_hir_node_t *outer = test_expression_hir_root(&expression);
  const psx_hir_node_t *inner = test_hir_child(&expression, outer, 0);
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(inner));
  psx_type_shape_t outer_shape = test_hir_type_shape(test_suite_session, outer);
  psx_type_shape_t inner_shape = test_hir_type_shape(test_suite_session, inner);
  ASSERT_EQ(PSX_TYPE_INTEGER, outer_shape.kind);
  ASSERT_EQ(expected_outer_kind, outer_shape.integer_kind);
  ASSERT_EQ(expected_outer_unsigned, outer_shape.is_unsigned);
  ASSERT_EQ(PSX_TYPE_INTEGER, inner_shape.kind);
  ASSERT_EQ(expected_inner_kind, inner_shape.integer_kind);
  ASSERT_EQ(expected_inner_unsigned, inner_shape.is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);
}

static void assert_test_type_query_hir(
    ag_compilation_session_t *test_suite_session,
    const char *input, psx_syntax_node_kind_t expected_syntax_kind,
    long long expected_value) {
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, input, &syntax);
  ASSERT_EQ(expected_syntax_kind, syntax->kind);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(root));
  ASSERT_EQ(expected_value, psx_hir_node_integer_value(root));
  psx_type_shape_t shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, shape.integer_kind);
  ASSERT_TRUE(shape.is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);
}

static void assert_test_integer_expression_hir(
    ag_compilation_session_t *test_suite_session,
    const char *input, psx_hir_node_kind_t expected_hir_kind,
    psx_integer_kind_t expected_integer_kind, int expected_unsigned,
    int expected_size) {
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, input, &syntax);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(expected_hir_kind, psx_hir_node_kind(root));
  psx_type_shape_t shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(expected_integer_kind, shape.integer_kind);
  ASSERT_EQ(expected_unsigned, shape.is_unsigned);
  ASSERT_EQ(expected_size,
            test_type_size_id(test_suite_session, psx_hir_node_qual_type(root).type_id));
  psx_frontend_expression_hir_dispose(&expression);
}

static void assert_test_generic_number_hir(
    ag_compilation_session_t *test_suite_session,
    const char *input, long long expected_value) {
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, input, &syntax);
  ASSERT_EQ(ND_GENERIC_SELECTION, syntax->kind);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(root));
  ASSERT_EQ(expected_value, psx_hir_node_integer_value(root));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void assert_test_compound_assignment_hir(
    ag_compilation_session_t *test_suite_session,
    const char *input, token_kind_t expected_syntax_operator,
    psx_hir_compound_operator_t expected_hir_operator) {
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, input, &syntax);
  ASSERT_EQ(ND_COMPOUND_ASSIGN, syntax->kind);
  ASSERT_EQ(expected_syntax_operator, syntax->source_op);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_COMPOUND_ASSIGN, psx_hir_node_kind(root));
  ASSERT_EQ(expected_hir_operator,
            psx_hir_node_compound_operator(root));
  ASSERT_EQ(2, psx_hir_node_child_count(root));
  ASSERT_EQ(PSX_HIR_LOCAL,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  const psx_hir_node_t *rhs = test_hir_child(&expression, root, 1);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(rhs));
  ASSERT_EQ(3, psx_hir_node_integer_value(rhs));
  psx_frontend_expression_hir_dispose(&expression);
}


static void test_syntax_literal_type_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_syntax_literal_type_boundary...\n");
  reset_test_locals(test_suite_session);
  preregister_test_locals(test_suite_session);

  node_t *integer =
      parse_expr_input_with_existing_locals(test_suite_session, "0UL");
  ASSERT_EQ(ND_NUM, integer->kind);
  ASSERT_TRUE(integer->tok != NULL);
  ASSERT_EQ(TK_NUM, integer->tok->kind);

  node_t *floating =
      parse_expr_input_with_existing_locals(test_suite_session, "1.0f");
  ASSERT_EQ(ND_NUM, floating->kind);

  node_t *string =
      parse_expr_input_with_existing_locals(test_suite_session, "\"syntax\"");
  ASSERT_EQ(ND_STRING, string->kind);

  node_t *function_name =
      parse_expr_input_with_existing_locals(test_suite_session, "__func__");
  ASSERT_EQ(ND_IDENTIFIER, function_name->kind);
  const node_identifier_t *function_name_identifier =
      (const node_identifier_t *)function_name;
  ASSERT_EQ(8, function_name_identifier->name_len);
  ASSERT_TRUE(memcmp(
      function_name_identifier->name, "__func__", 8) == 0);

  node_t *unary_plus =
      parse_expr_input_with_existing_locals(test_suite_session, "+(unsigned char)1");
  ASSERT_EQ(ND_UNARY_PLUS, unary_plus->kind);
  ASSERT_EQ(ND_SOURCE_CAST, unary_plus->lhs->kind);
  ASSERT_TRUE(unary_plus->rhs == NULL);
  ASSERT_TRUE(unary_plus->tok != NULL);
  ASSERT_EQ(TK_PLUS, unary_plus->tok->kind);

  node_t *logical_not =
      parse_expr_input_with_existing_locals(test_suite_session, "!0");
  ASSERT_EQ(ND_LOGICAL_NOT, logical_not->kind);
  ASSERT_EQ(ND_NUM, logical_not->lhs->kind);
  ASSERT_TRUE(logical_not->rhs == NULL);
  ASSERT_TRUE(logical_not->tok != NULL);
  ASSERT_EQ(TK_BANG, logical_not->tok->kind);

  node_t *bitwise_not =
      parse_expr_input_with_existing_locals(test_suite_session, "~1");
  ASSERT_EQ(ND_BITWISE_NOT, bitwise_not->kind);
  ASSERT_EQ(ND_NUM, bitwise_not->lhs->kind);
  ASSERT_TRUE(bitwise_not->rhs == NULL);
  ASSERT_TRUE(bitwise_not->tok != NULL);
  ASSERT_EQ(TK_TILDE, bitwise_not->tok->kind);
}

static void test_direct_literal_typed_hir_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_direct_literal_typed_hir_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "enum DirectTypedEnum { DirectEnum = 41 }; "
      "long DirectGlobal = 9; int DirectArray[3]; "
      "double _Complex DirectComplex; "
      "int DirectFunction(int value) { return value; } "
      "struct DirectRecord { char prefix; int value; "
      "int (*callback)(int); }; "
      "int use_direct_values(int input) { "
      "  unsigned long integer = 17UL; "
      "  int local = input; int array[3] = {1, 2, 3}; "
      "  int (*callback)(int) = DirectFunction; "
      "  int unary = +(unsigned char)local; "
      "  int logical = !1.0; "
      "  int bits = ~(unsigned char)local; "
      "  int string_value = \"direct\"[0]; "
      "  long conditional = -(1 + 2) ? 4L : 5; "
      "  local = callback(array[0]) + (DirectGlobal > 2) "
      "      + DirectArray[0] "
      "      + (int)__real__ DirectComplex + DirectEnum; "
      "  return _Generic(local, int: local, default: 0) "
      "      + unary + logical + bits + string_value "
      "      + (int)conditional + (int)integer; "
      "}"));

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session));
  ASSERT_TRUE(hir != NULL);
  ASSERT_TRUE(types != NULL);
  ASSERT_EQ(2, psx_hir_module_root_count(hir));
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_FUNCTION,
                  "DirectFunction", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_FUNCTION,
                  "use_direct_values", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL,
                  "integer", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL,
                  "callback", 0) != NULL);

  const psx_hir_node_kind_t required_kinds[] = {
      PSX_HIR_NUMBER,
      PSX_HIR_STRING,
      PSX_HIR_UNARY_PLUS,
      PSX_HIR_LOGICAL_NOT,
      PSX_HIR_BITWISE_NOT,
      PSX_HIR_NEGATE,
      PSX_HIR_TERNARY,
      PSX_HIR_ASSIGN,
      PSX_HIR_CALL,
      PSX_HIR_SUBSCRIPT,
      PSX_HIR_GLOBAL,
      PSX_HIR_FUNCTION_REF,
      PSX_HIR_CREAL,
      PSX_HIR_CAST,
  };
  for (size_t i = 0;
       i < sizeof(required_kinds) /
           sizeof(required_kinds[0]); i++)
    ASSERT_TRUE(find_test_hir_node_kind(
                    hir, required_kinds[i], 0) != NULL);

  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(
            hir, (psx_hir_node_id_t)i);
    ASSERT_TRUE(node != NULL);
    if (psx_hir_node_role(node) ==
        PSX_HIR_ROLE_EXPRESSION)
      ASSERT_TRUE(
          psx_semantic_type_table_qual_type_is_valid(
              types, psx_hir_node_qual_type(node)));
  }

  ASSERT_TRUE(psx_hir_module_symbol_count(hir) >= 3);
  int found_direct_global = 0;
  int found_direct_array = 0;
  int found_direct_complex = 0;
  for (size_t i = 1;
       i <= psx_hir_module_symbol_count(hir); i++) {
    const psx_hir_symbol_t *symbol =
        psx_hir_module_symbol_lookup(
            hir, (psx_hir_symbol_id_t)i);
    size_t name_length = 0;
    const char *name =
        psx_hir_symbol_name(symbol, &name_length);
    ASSERT_TRUE(name != NULL);
    if (name_length == 12 &&
        memcmp(name, "DirectGlobal", 12) == 0)
      found_direct_global = 1;
    if (name_length == 11 &&
        memcmp(name, "DirectArray", 11) == 0)
      found_direct_array = 1;
    if (name_length == 13 &&
        memcmp(name, "DirectComplex", 13) == 0)
      found_direct_complex = 1;
  }
  ASSERT_TRUE(found_direct_global);
  ASSERT_TRUE(found_direct_array);
  ASSERT_TRUE(found_direct_complex);

  expect_parse_fail(test_suite_session,
      "int main(void) { struct S { int value; }; "
      "struct S object = {0}; return object.missing; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { int value = 1; return *value; }");
}

static node_t *parse_direct_test_statement_syntax(
    ag_compilation_session_t *test_suite_session,
    const char *input) {
  tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input);
  test_statement_syntax_services_t services = {
      .expression = {
          .runtime_context =
              ag_compilation_session_parser_runtime_context(
                  test_suite_session),
      },
  };
  psx_statement_syntax_context_t syntax = {
      .context = &services,
      .runtime_context = services.expression.runtime_context,
      .parse_expression = test_statement_parse_expression,
      .parse_case_expression = test_statement_parse_expression,
      .enter_block_scope = test_statement_enter_block_scope,
      .leave_block_scope = test_statement_leave_block_scope,
  };
  node_t *statement = psx_stmt_stmt_syntax(&syntax);
  ASSERT_EQ(0, services.block_scope_depth);
  return statement;
}

static void test_direct_statement_typed_hir_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_direct_statement_typed_hir_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  node_t *syntax = parse_direct_test_statement_syntax(test_suite_session,
      "{ if (1) { while (0) continue; } "
      "else do break; while (0); "
      "for (1; 1; 1) break; "
      "switch (2) { case 1: break; case 1 + 1: break; "
      "default: break; } goto done; done: return 3; }");
  ASSERT_TRUE(syntax != NULL);
  ASSERT_EQ(ND_BLOCK, syntax->kind);
  node_block_t *syntax_block = (node_block_t *)syntax;
  ASSERT_EQ(ND_IF, syntax_block->body[0]->kind);
  ASSERT_EQ(ND_FOR, syntax_block->body[1]->kind);
  ASSERT_EQ(ND_SWITCH, syntax_block->body[2]->kind);
  ASSERT_EQ(ND_GOTO, syntax_block->body[3]->kind);
  ASSERT_EQ(ND_LABEL, syntax_block->body[4]->kind);

  psx_scope_graph_t *scope_graph =
      ps_ctx_scope_graph(test_semantic_context(test_suite_session));
  psx_scope_id_t label_scope =
      psx_scope_graph_current_scope(scope_graph);
  int preexisting_label_payload = 1;
  psx_decl_id_t preexisting_label = psx_scope_graph_declare_at(
      scope_graph, label_scope, PSX_NAMESPACE_LABEL, PSX_DECL_LABEL,
      "preexisting", 11, &preexisting_label_payload);
  ASSERT_TRUE(preexisting_label != PSX_DECL_ID_INVALID);

  const psx_typed_hir_tree_t *typed_statement = NULL;
  psx_resolved_hir_build_failure_t failure = {0};
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), syntax, &typed_statement, &failure));
  ASSERT_TRUE(typed_statement != NULL);
  ASSERT_EQ(PSX_DECL_ID_INVALID,
            psx_scope_graph_lookup_in_scope(
                scope_graph, psx_scope_graph_current_scope(scope_graph),
                PSX_NAMESPACE_LABEL, "done", 4));
  ASSERT_EQ(preexisting_label,
            psx_scope_graph_lookup_in_scope(
                scope_graph, label_scope, PSX_NAMESPACE_LABEL,
                "preexisting", 11));

  psx_hir_module_t *hir = psx_hir_module_create();
  ASSERT_TRUE(hir != NULL);
  psx_hir_node_id_t root_id = psx_typed_hir_tree_emit(
      hir, typed_statement, &failure);
  ASSERT_TRUE(root_id != PSX_HIR_NODE_ID_INVALID);
  const psx_hir_node_t *root = psx_hir_module_lookup(hir, root_id);
  ASSERT_EQ(PSX_HIR_BLOCK, psx_hir_node_kind(root));
  ASSERT_EQ(5, psx_hir_node_child_count(root));
  ASSERT_EQ(PSX_HIR_EDGE_BLOCK_ITEM,
            psx_hir_node_child_edge_at(root, 0));
  ASSERT_EQ(PSX_HIR_EDGE_BLOCK_ITEM,
            psx_hir_node_child_edge_at(root, 1));
  ASSERT_EQ(PSX_HIR_EDGE_BLOCK_ITEM,
            psx_hir_node_child_edge_at(root, 2));
  ASSERT_EQ(PSX_HIR_EDGE_BLOCK_ITEM,
            psx_hir_node_child_edge_at(root, 3));
  ASSERT_EQ(PSX_HIR_EDGE_BLOCK_ITEM,
            psx_hir_node_child_edge_at(root, 4));

  const psx_hir_node_t *if_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(root, 0));
  ASSERT_EQ(PSX_HIR_IF, psx_hir_node_kind(if_hir));
  ASSERT_EQ(3, psx_hir_node_child_count(if_hir));
  ASSERT_EQ(PSX_HIR_EDGE_LHS,
            psx_hir_node_child_edge_at(if_hir, 0));
  ASSERT_EQ(PSX_HIR_EDGE_RHS,
            psx_hir_node_child_edge_at(if_hir, 1));
  ASSERT_EQ(PSX_HIR_EDGE_ELSE,
            psx_hir_node_child_edge_at(if_hir, 2));
  const psx_hir_node_t *if_condition = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(if_hir, 0));
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(if_condition));
  ASSERT_TRUE(psx_hir_node_qual_type(if_condition).type_id !=
              PSX_TYPE_ID_INVALID);

  const psx_hir_node_t *then_block = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(if_hir, 1));
  ASSERT_EQ(PSX_HIR_BLOCK, psx_hir_node_kind(then_block));
  const psx_hir_node_t *while_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(then_block, 0));
  ASSERT_EQ(PSX_HIR_WHILE, psx_hir_node_kind(while_hir));
  const psx_hir_node_t *continue_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(while_hir, 1));
  ASSERT_EQ(PSX_HIR_CONTINUE, psx_hir_node_kind(continue_hir));

  const psx_hir_node_t *do_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(if_hir, 2));
  ASSERT_EQ(PSX_HIR_DO_WHILE, psx_hir_node_kind(do_hir));
  const psx_hir_node_t *break_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(do_hir, 1));
  ASSERT_EQ(PSX_HIR_BREAK, psx_hir_node_kind(break_hir));

  const psx_hir_node_t *for_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(root, 1));
  ASSERT_EQ(PSX_HIR_FOR, psx_hir_node_kind(for_hir));
  ASSERT_EQ(4, psx_hir_node_child_count(for_hir));
  ASSERT_EQ(PSX_HIR_EDGE_INIT,
            psx_hir_node_child_edge_at(for_hir, 0));
  ASSERT_EQ(PSX_HIR_EDGE_LHS,
            psx_hir_node_child_edge_at(for_hir, 1));
  ASSERT_EQ(PSX_HIR_EDGE_INCREMENT,
            psx_hir_node_child_edge_at(for_hir, 2));
  ASSERT_EQ(PSX_HIR_EDGE_RHS,
            psx_hir_node_child_edge_at(for_hir, 3));
  const psx_hir_node_t *for_break_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(for_hir, 3));
  ASSERT_EQ(PSX_HIR_BREAK, psx_hir_node_kind(for_break_hir));

  const psx_hir_node_t *switch_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(root, 2));
  ASSERT_EQ(PSX_HIR_SWITCH, psx_hir_node_kind(switch_hir));
  ASSERT_EQ(2, psx_hir_node_child_count(switch_hir));
  const psx_hir_node_t *switch_body = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(switch_hir, 1));
  ASSERT_EQ(PSX_HIR_BLOCK, psx_hir_node_kind(switch_body));
  ASSERT_EQ(3, psx_hir_node_child_count(switch_body));
  const psx_hir_node_t *case_one = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(switch_body, 0));
  const psx_hir_node_t *case_two = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(switch_body, 1));
  const psx_hir_node_t *default_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(switch_body, 2));
  ASSERT_EQ(PSX_HIR_CASE, psx_hir_node_kind(case_one));
  ASSERT_EQ(1, psx_hir_node_integer_value(case_one));
  ASSERT_EQ(PSX_HIR_CASE, psx_hir_node_kind(case_two));
  ASSERT_EQ(2, psx_hir_node_integer_value(case_two));
  ASSERT_EQ(PSX_HIR_DEFAULT, psx_hir_node_kind(default_hir));

  const psx_hir_node_t *goto_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(root, 3));
  ASSERT_EQ(PSX_HIR_GOTO, psx_hir_node_kind(goto_hir));
  size_t jump_name_length = 0;
  const char *jump_name =
      psx_hir_node_name(goto_hir, &jump_name_length);
  ASSERT_EQ(4, jump_name_length);
  ASSERT_TRUE(memcmp(jump_name, "done", 4) == 0);

  const psx_hir_node_t *label_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(root, 4));
  ASSERT_EQ(PSX_HIR_LABEL, psx_hir_node_kind(label_hir));
  ASSERT_EQ(1, psx_hir_node_child_count(label_hir));
  const psx_hir_node_t *return_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(label_hir, 0));
  ASSERT_EQ(PSX_HIR_RETURN, psx_hir_node_kind(return_hir));
  ASSERT_EQ(1, psx_hir_node_child_count(return_hir));
  ASSERT_EQ(PSX_HIR_EDGE_LHS,
            psx_hir_node_child_edge_at(return_hir, 0));

  node_t *duplicate_case = parse_direct_test_statement_syntax(test_suite_session,
      "{ switch (0) { case 1: break; case 1: break; } }");
  const psx_typed_hir_tree_t *invalid_hir = NULL;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), duplicate_case, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_CASE,
            failure.rejection);
  ASSERT_EQ(ND_CASE, failure.source_node_kind);
  ASSERT_EQ(1, failure.source_integer_value);

  node_t *duplicate_default = parse_direct_test_statement_syntax(test_suite_session,
      "{ switch (0) { default: break; default: break; } }");
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), duplicate_default, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_DEFAULT,
            failure.rejection);
  ASSERT_EQ(ND_DEFAULT, failure.source_node_kind);

  node_t *duplicate_label = parse_direct_test_statement_syntax(test_suite_session,
      "{ duplicate: ; duplicate: ; }");
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), duplicate_label, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_LABEL,
            failure.rejection);
  ASSERT_EQ(9, failure.source_name_length);
  ASSERT_TRUE(memcmp(failure.source_name, "duplicate", 9) == 0);
  ASSERT_EQ(PSX_DECL_ID_INVALID,
            psx_scope_graph_lookup_in_scope(
                scope_graph, psx_scope_graph_current_scope(scope_graph),
                PSX_NAMESPACE_LABEL, "duplicate", 9));

  node_t *undefined_goto =
      parse_direct_test_statement_syntax(test_suite_session, "{ goto missing; }");
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), undefined_goto, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_UNDEFINED_GOTO,
            failure.rejection);
  ASSERT_EQ(7, failure.source_name_length);
  ASSERT_TRUE(memcmp(failure.source_name, "missing", 7) == 0);

  node_t *invalid_break =
      parse_direct_test_statement_syntax(test_suite_session, "{ break; }");
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), invalid_break, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_BREAK_OUTSIDE_LOOP_OR_SWITCH,
            failure.rejection);
  ASSERT_EQ(ND_BREAK, failure.source_node_kind);

  node_t *invalid_continue =
      parse_direct_test_statement_syntax(test_suite_session, "{ continue; }");
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), invalid_continue, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_CONTINUE_OUTSIDE_LOOP,
            failure.rejection);
  ASSERT_EQ(ND_CONTINUE, failure.source_node_kind);

  node_t *invalid_case =
      parse_direct_test_statement_syntax(test_suite_session, "{ case 1: ; }");
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), invalid_case, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_CASE_OUTSIDE_SWITCH,
            failure.rejection);
  ASSERT_EQ(ND_CASE, failure.source_node_kind);

  node_t *invalid_default =
      parse_direct_test_statement_syntax(test_suite_session, "{ default: ; }");
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), invalid_default, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_DEFAULT_OUTSIDE_SWITCH,
            failure.rejection);
  ASSERT_EQ(ND_DEFAULT, failure.source_node_kind);

  node_t *empty_if_syntax =
      parse_direct_test_statement_syntax(test_suite_session, "if (0);");
  ASSERT_TRUE(empty_if_syntax != NULL);
  ASSERT_EQ(ND_IF, empty_if_syntax->kind);
  ASSERT_TRUE(empty_if_syntax->rhs != NULL);
  ASSERT_EQ(ND_NULL_STMT, empty_if_syntax->rhs->kind);
  const psx_typed_hir_tree_t *typed_empty_if = NULL;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), empty_if_syntax,
          &typed_empty_if, &failure));
  psx_hir_node_id_t empty_if_id = psx_typed_hir_tree_emit(
      hir, typed_empty_if, &failure);
  const psx_hir_node_t *empty_if_hir =
      psx_hir_module_lookup(hir, empty_if_id);
  ASSERT_EQ(PSX_HIR_IF, psx_hir_node_kind(empty_if_hir));
  const psx_hir_node_t *empty_body_hir = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(empty_if_hir, 1));
  ASSERT_EQ(PSX_HIR_NOP, psx_hir_node_kind(empty_body_hir));

  psx_hir_module_destroy(hir);
  reset_test_translation_unit_state(test_suite_session);
}

static int resolve_program_input_hir(
    ag_compilation_session_t *test_suite_session, const char *input) {
  return resolve_test_program_hir_from(test_suite_session, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input));
}

static void test_typed_hir_ownership_and_type_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_ownership_and_type_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { return 1+2; }"));

  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  ASSERT_TRUE(hir != NULL);
  ASSERT_TRUE(types != NULL);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  ASSERT_TRUE(psx_hir_module_node_count(hir) > 0);

  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  const psx_hir_node_t *root = psx_hir_module_lookup(hir, root_id);
  ASSERT_TRUE(root != NULL);
  ASSERT_EQ(PSX_HIR_FUNCTION, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_ROLE_STATEMENT, psx_hir_node_role(root));
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            psx_hir_node_qual_type(root).type_id);
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, psx_hir_node_attached_qual_type(root)));

  size_t name_length = 0;
  ASSERT_TRUE(strcmp(psx_hir_node_name(root, &name_length), "main") == 0);
  ASSERT_EQ(4, name_length);
  int found_function_body = 0;
  int found_two = 0;
  for (size_t i = 0; i < psx_hir_node_child_count(root); i++) {
    if (psx_hir_node_child_edge_at(root, i) ==
        PSX_HIR_EDGE_FUNCTION_BODY)
      found_function_body = 1;
  }
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    ASSERT_TRUE(node != NULL);
    if (psx_hir_node_role(node) == PSX_HIR_ROLE_EXPRESSION) {
      ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
          types, psx_hir_node_qual_type(node)));
    } else {
      ASSERT_EQ(PSX_TYPE_ID_INVALID,
                psx_hir_node_qual_type(node).type_id);
    }
    if (psx_hir_node_kind(node) == PSX_HIR_NUMBER &&
        psx_hir_node_integer_value(node) == 2)
      found_two = 1;
  }
  ASSERT_TRUE(found_function_body);
  ASSERT_TRUE(found_two);

  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));
  root = psx_hir_module_lookup(hir, root_id);
  ASSERT_TRUE(root != NULL);
  ASSERT_EQ(PSX_HIR_FUNCTION, psx_hir_node_kind(root));
  ASSERT_TRUE(strcmp(psx_hir_node_name(root, &name_length), "main") == 0);
  ASSERT_TRUE(psx_hir_module_node_count(hir) > 0);

  ir_build_options_t ir_options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = types,
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t hir_ir_status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &ir_options, &hir_ir_status);
  ASSERT_EQ(IR_HIR_BUILD_OK, hir_ir_status);
  ASSERT_TRUE(ir != NULL);
  ASSERT_TRUE(ir->funcs != NULL);
  ASSERT_TRUE(strcmp(ir->funcs->name, "main") == 0);
  ASSERT_EQ(IR_TY_I32, test_function_abi_value(test_suite_session, ir, 8, 0));
  ASSERT_TRUE(ir->funcs->entry != NULL);
  ASSERT_TRUE(ir->funcs->entry->head != NULL);
  ASSERT_EQ(IR_ADD, ir->funcs->entry->head->op);
  ASSERT_TRUE(ir->funcs->entry->head->next != NULL);
  ASSERT_EQ(IR_RET, ir->funcs->entry->head->next->op);
  ir_module_free(ir);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_EQ(0, psx_hir_module_node_count(hir));
  ASSERT_EQ(0, psx_hir_module_root_count(hir));
}

static int count_ir_op(const ir_func_t *function, ir_op_t op) {
  int count = 0;
  for (const ir_block_t *block = function ? function->entry : NULL;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == op) count++;
    }
  }
  return count;
}

static int max_ir_alloca_size(const ir_func_t *function) {
  int maximum = 0;
  for (const ir_block_t *block = function ? function->entry : NULL;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_ALLOCA &&
          instruction->alloca_size > maximum)
        maximum = instruction->alloca_size;
    }
  }
  return maximum;
}

static void test_typed_hir_local_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_local_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int main(void) { int x = 3; int before = ++x; --x; "
      "x = x + 4; return before + x; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(ir->funcs->function_type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ir->funcs->function_type.result.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(0, ir->funcs->function_type.param_count);
  ASSERT_TRUE(ir->funcs->function_type.has_prototype);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ALLOCA) >= 1);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 5);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD) >= 5);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ADD) >= 3);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_SUB) >= 1);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_overaligned_local_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_overaligned_local_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int aligned_local(void) { "
      "_Alignas(64) int value = 7; "
      "return value + ((unsigned long)&value % 64); }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_ALIGN_PTR));
  int found_overaligned_alloca = 0;
  for (const ir_block_t *block = ir->funcs->entry;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_ALLOCA &&
          instruction->alloca_align == 64 &&
          instruction->alloca_size >= 68) {
        found_overaligned_alloca = 1;
      }
    }
  }
  ASSERT_TRUE(found_overaligned_alloca);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_parameter_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_parameter_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int add(int left, short right) { return left + right; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(ir->funcs->function_type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ir->funcs->function_type.result.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(2, ir->funcs->function_type.param_count);
  ASSERT_TRUE(ir->funcs->function_type.has_prototype);
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_PARAM_BIND));
  ASSERT_EQ(2, test_function_abi_value(test_suite_session, ir, 0, 0));
  ASSERT_EQ(IR_TY_I32, test_function_abi_value(test_suite_session, ir, 1, 0));
  ASSERT_EQ(IR_TY_I32, test_function_abi_value(test_suite_session, ir, 1, 1));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ALLOCA) >= 2);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD) >= 2);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_pointer_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_pointer_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int *touch(int *p) { int x = *p; int *q = &x; "
      "int *before = ++q; --q; *before = x + 1; "
      "long address = (long)q; q = (int *)address; "
      "*p = *q; return p; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(IR_TY_PTR, test_function_abi_value(test_suite_session, ir, 8, 0));
  ASSERT_EQ(1, test_function_abi_value(test_suite_session, ir, 0, 0));
  ASSERT_EQ(IR_TY_PTR, test_function_abi_value(test_suite_session, ir, 1, 0));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ALLOCA) >= 4);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 8);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD) >= 8);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ADD) >= 2);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_SUB) >= 1);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_post_inc_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_post_inc_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int assign_once(int *dst, int *index) { "
      "dst[(*index)++] = 7; return *index; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(2, test_function_abi_value(test_suite_session, ir, 0, 0));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_ADD));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_MUL));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_LEA));
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_STORE));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_vla_parameter_metadata_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_vla_parameter_metadata_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int rstride(int n, int a[n][n]) { "
      "return a[1][0] + a[2][1]; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  const psx_hir_node_t *root = psx_hir_module_lookup(hir, root_id);
  ASSERT_TRUE(root != NULL);
  const psx_hir_node_t *vla_parameter = NULL;
  for (size_t i = 0; i < psx_hir_node_child_count(root); i++) {
    if (psx_hir_node_child_edge_at(root, i) !=
        PSX_HIR_EDGE_PARAMETER)
      continue;
    const psx_hir_node_t *parameter = psx_hir_module_lookup(
        hir, psx_hir_node_child_at(root, i));
    if (psx_hir_node_vla_dimension_count(parameter) > 0)
      vla_parameter = parameter;
  }
  ASSERT_TRUE(vla_parameter != NULL);
  ASSERT_TRUE(psx_hir_node_vla_stride_frame_offset(vla_parameter) != 0);
  ASSERT_EQ(4, psx_hir_node_vla_stride_element_size(vla_parameter));
  ASSERT_EQ(8, psx_hir_node_vla_stride_slot_size(vla_parameter));
  ASSERT_EQ(1, psx_hir_node_vla_dimension_count(vla_parameter));
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));
  ASSERT_EQ(1, psx_hir_node_vla_dimension_count(vla_parameter));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MUL) >= 1);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ZEXT) >= 1);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_vla_allocation_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_vla_allocation_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int first(int outer, int middle, int inner) { "
      "int values[outer][middle][inner]; "
      "values[0][0][0] = 42; "
      "return values[0][0][0]; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  const psx_hir_node_t *vla_runtime = NULL;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *candidate =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (candidate &&
        psx_hir_node_kind(candidate) == PSX_HIR_VLA_ALLOC) {
      vla_runtime = candidate;
      break;
    }
  }
  ASSERT_TRUE(vla_runtime != NULL);
  ASSERT_EQ(3, psx_hir_node_child_count(vla_runtime));
  for (size_t i = 0; i < 3; i++) {
    ASSERT_EQ(
        PSX_HIR_EDGE_VLA_DIMENSION,
        psx_hir_node_child_edge_at(vla_runtime, i));
  }
  ASSERT_EQ(4, psx_hir_node_vla_stride_element_size(vla_runtime));
  ASSERT_EQ(2, psx_hir_node_vla_runtime_store_count(vla_runtime));
  ASSERT_EQ(1, psx_hir_node_vla_runtime_store_dimension(
                   vla_runtime, 0));
  ASSERT_EQ(2, psx_hir_node_vla_runtime_store_dimension(
                   vla_runtime, 1));
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_VLA_ALLOC));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MUL) >= 3);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ZEXT) >= 1);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 5);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_direct_call_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_direct_call_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int add(int left, int right) { return left + right; } "
      "int sink(int tag, ...) { return tag; } "
      "int twice(int value) { "
      "return add(value, value) + sink(1, (char)value); } "
      "int recur(int value) { "
      "return value ? recur(value - 1) : 0; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(4, psx_hir_module_root_count(hir));
  psx_hir_node_id_t variadic_root_id = psx_hir_module_root_at(hir, 1);
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 2);
  psx_hir_node_id_t recursive_root_id = psx_hir_module_root_at(hir, 3);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_CALL));
  const ir_inst_t *call = NULL;
  const ir_inst_t *variadic_call = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL && instruction->sym_len == 3 &&
          strncmp(instruction->sym, "add", 3) == 0) {
        call = instruction;
      } else if (instruction->op == IR_CALL &&
                 instruction->sym_len == 4 &&
                 strncmp(instruction->sym, "sink", 4) == 0) {
        variadic_call = instruction;
      }
    }
  }
  ASSERT_TRUE(call != NULL);
  ASSERT_EQ(3, call->sym_len);
  ASSERT_TRUE(strncmp(call->sym, "add", 3) == 0);
  ASSERT_EQ(2, call->nargs);
  ASSERT_EQ(2, test_call_abi_value(test_suite_session, ir, call, 0, 0));
  ASSERT_TRUE(call->has_function_type);
  ASSERT_TRUE(call->function_type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(2, call->function_type.param_count);
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, call, 4, 0));
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, call, 1, 0));
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, call, 1, 1));
  ir_abi_module_t *call_abi = test_lower_ir_abi(test_suite_session, ir);
  size_t lowered_argument_count = 0;
  const ir_abi_argument_t *lowered_arguments = ir_abi_call_arguments(
      call_abi, call, &lowered_argument_count);
  ASSERT_TRUE(call_abi != NULL);
  ASSERT_EQ(2, lowered_argument_count);
  ASSERT_TRUE(lowered_arguments != NULL);
  ASSERT_EQ(call->args[0].value.id, lowered_arguments[0].source.id);
  ASSERT_EQ(call->args[1].value.id, lowered_arguments[1].source.id);
  ir_val_t saved_argument = call->args[0].value;
  call->args[0].value = ir_val_none();
  ASSERT_EQ(saved_argument.id, lowered_arguments[0].source.id);
  call->args[0].value = saved_argument;
  ir_abi_module_free(call_abi);
  ASSERT_TRUE(variadic_call != NULL);
  ASSERT_EQ(2, variadic_call->nargs);
  ASSERT_EQ(1, test_call_abi_value(test_suite_session, ir, variadic_call, 0, 0));
  ASSERT_TRUE(test_call_abi_value(test_suite_session, ir, variadic_call, 2, 0));
  ASSERT_TRUE(variadic_call->has_function_type);
  ASSERT_EQ(1, variadic_call->function_type.param_count);
  ASSERT_TRUE(variadic_call->function_type.is_variadic);
  ASSERT_TRUE(test_call_abi_value(test_suite_session, ir, variadic_call, 2, 0));
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, variadic_call, 1, 0));
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, variadic_call, 1, 1));
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, variadic_root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(test_function_abi_value(test_suite_session, ir, 2, 0));
  ASSERT_EQ(1, test_function_abi_value(test_suite_session, ir, 0, 0));
  ASSERT_EQ(1, test_function_abi_value(test_suite_session, ir, 0, 0));
  ASSERT_EQ(IR_TY_I32, test_function_abi_value(test_suite_session, ir, 1, 0));
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, recursive_root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_CALL));
  const ir_inst_t *recursive_call = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block && !recursive_call; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL) {
        recursive_call = instruction;
        break;
      }
    }
  }
  ASSERT_TRUE(recursive_call != NULL);
  ASSERT_EQ(5, recursive_call->sym_len);
  ASSERT_TRUE(strncmp(recursive_call->sym, "recur", 5) == 0);
  ASSERT_TRUE(recursive_call->has_function_type);
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, recursive_call, 4, 0));
  ASSERT_EQ(1, test_call_abi_value(test_suite_session, ir, recursive_call, 0, 0));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_variadic_aggregate_call_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_variadic_aggregate_call_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct Payload { char bytes[9]; }; "
      "int sink(int marker, ...); "
      "int invoke_sink(void) { "
      "struct Payload payload; return sink(1, payload); }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  const ir_inst_t *call = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block && !call; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL) {
        call = instruction;
        break;
      }
    }
  }
  ASSERT_TRUE(call != NULL);
  ASSERT_TRUE(test_call_abi_value(test_suite_session, ir, call, 2, 0));
  ASSERT_EQ(1, test_call_abi_value(test_suite_session, ir, call, 0, 0));
  ASSERT_EQ(2, call->nargs);
  ASSERT_TRUE(call->args[0].type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(call->args[1].type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(IR_CALL_ARGUMENT_ADDRESS,
            call->args[1].representation);
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, call, 1, 0));
  ASSERT_EQ(IR_TY_I64, test_call_abi_value(test_suite_session, ir, call, 1, 1));
  ASSERT_EQ(IR_TY_I64, test_call_abi_value(test_suite_session, ir, call, 1, 2));
  ir_abi_module_t *call_abi = test_lower_ir_abi(test_suite_session, ir);
  size_t physical_argument_count = 0;
  const ir_abi_argument_t *physical_arguments = ir_abi_call_arguments(
      call_abi, call, &physical_argument_count);
  ASSERT_TRUE(call_abi != NULL && physical_arguments != NULL);
  ASSERT_EQ(3, physical_argument_count);
  ASSERT_EQ(IR_ABI_ARGUMENT_DIRECT, physical_arguments[0].access);
  ASSERT_EQ(IR_ABI_ARGUMENT_LOAD, physical_arguments[1].access);
  ASSERT_EQ(IR_ABI_ARGUMENT_LOAD, physical_arguments[2].access);
  ASSERT_EQ(0, physical_arguments[1].byte_offset);
  ASSERT_EQ(8, physical_arguments[2].byte_offset);
  ASSERT_EQ(call->args[1].value.id, physical_arguments[1].source.id);
  ASSERT_EQ(call->args[1].value.id, physical_arguments[2].source.id);
  ir_abi_module_free(call_abi);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_MEMCPY));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_atomic_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_atomic_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "long __ag_atomic_load(void *obj); "
      "long __ag_atomic_store(void *obj, long value); "
      "long __ag_atomic_fetch_add(void *obj, long value); "
      "int __ag_atomic_cas(void *obj, void *expected, long desired); "
      "int __ag_atomic_fence(void); "
      "long run(void) { "
      "_Atomic unsigned char small = 1; "
      "_Atomic long long wide = 2; "
      "long long expected = 2; "
      "long result = __ag_atomic_load(&small); "
      "__ag_atomic_store(&small, 3); "
      "result = result + __ag_atomic_fetch_add(&wide, 4); "
      "result = result + __ag_atomic_cas(&wide, &expected, 9); "
      "__ag_atomic_fence(); "
      "return result; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(5, count_ir_op(ir->funcs, IR_ATOMIC));
  int saw_unsigned_byte_load = 0;
  int saw_byte_store = 0;
  int saw_wide_rmw = 0;
  int saw_wide_cas = 0;
  int saw_fence = 0;
  for (const ir_block_t *block = ir->funcs->entry;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op != IR_ATOMIC) continue;
      if (instruction->atomic_kind == IR_ATOMIC_LOAD &&
          instruction->atomic_width == 1 &&
          instruction->is_unsigned) {
        saw_unsigned_byte_load = 1;
      } else if (instruction->atomic_kind == IR_ATOMIC_STORE &&
                 instruction->atomic_width == 1) {
        saw_byte_store = 1;
      } else if (instruction->atomic_kind == IR_ATOMIC_RMW &&
                 instruction->atomic_width == 8 &&
                 instruction->atomic_rmw_op == IR_ARMW_ADD) {
        saw_wide_rmw = 1;
      } else if (instruction->atomic_kind == IR_ATOMIC_CAS &&
                 instruction->atomic_width == 8) {
        saw_wide_cas = 1;
      } else if (instruction->atomic_kind == IR_ATOMIC_FENCE) {
        saw_fence = 1;
      }
    }
  }
  ASSERT_TRUE(saw_unsigned_byte_load);
  ASSERT_TRUE(saw_byte_store);
  ASSERT_TRUE(saw_wide_rmw);
  ASSERT_TRUE(saw_wide_cas);
  ASSERT_TRUE(saw_fence);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_va_arg_area_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_va_arg_area_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int has_args(int count, ...) { "
      "void *area = __va_arg_area; "
      "return count && area != (void *)0; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(test_function_abi_value(test_suite_session, ir, 2, 0));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_VARARG_CURSOR));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_register_aggregate_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_register_aggregate_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct Pair { int left; int right; }; "
      "struct Pair make_pair(int left, int right) { "
      "struct Pair pair; pair.left = left; pair.right = right; "
      "return pair; } "
      "int read_left(void) { return make_pair(3, 4).left; } "
      "int read_initialized(void) { "
      "struct Pair pair = make_pair(5, 6); return pair.right; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(3, psx_hir_module_root_count(hir));
  psx_hir_node_id_t make_root_id = psx_hir_module_root_at(hir, 0);
  psx_hir_node_id_t read_root_id = psx_hir_module_root_at(hir, 1);
  psx_hir_node_id_t initialized_root_id =
      psx_hir_module_root_at(hir, 2);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, make_root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(IR_TY_I64, test_function_abi_value(test_suite_session, ir, 8, 0));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, read_root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_CALL));
  ASSERT_EQ(0, count_ir_op(ir->funcs, IR_STORE));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD) >= 1);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, initialized_root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  const ir_inst_t *initialized_call = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block && !initialized_call; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL) {
        initialized_call = instruction;
        break;
      }
    }
  }
  ASSERT_TRUE(initialized_call != NULL);
  ASSERT_TRUE(initialized_call->result_storage.id != IR_VAL_NONE);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_MEMCPY));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_aggregate_parameter_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_aggregate_parameter_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct Pair { int left; int right; }; "
      "struct Triple { int first; int second; int third; }; "
      "int sum_pair(struct Pair value) { "
      "return value.left + value.right; } "
      "int sum_triple(struct Triple value) { "
      "return value.first + value.second + value.third; } "
      "int call_both(void) { "
      "struct Pair pair = {1, 2}; "
      "struct Triple triple = {3, 4, 5}; "
      "return sum_pair(pair) + sum_triple(triple); }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(3, psx_hir_module_root_count(hir));
  psx_hir_node_id_t pair_root = psx_hir_module_root_at(hir, 0);
  psx_hir_node_id_t triple_root = psx_hir_module_root_at(hir, 1);
  psx_hir_node_id_t caller_root = psx_hir_module_root_at(hir, 2);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, pair_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_PARAM_BIND));
  ASSERT_EQ(IR_TY_I64, test_function_abi_value(test_suite_session, ir, 1, 0));
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, triple_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_PARAM_BIND));
  ASSERT_EQ(0, count_ir_op(ir->funcs, IR_MEMCPY));
  ASSERT_EQ(IR_TY_PTR, test_function_abi_value(test_suite_session, ir, 1, 0));
  ASSERT_TRUE(max_ir_alloca_size(ir->funcs) >= 12);
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, caller_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_CALL));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MEMCPY) >= 1);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_indirect_aggregate_return_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_indirect_aggregate_return_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct Triple { int first; int second; int third; }; "
      "struct Triple make_triple(int value) { "
      "struct Triple result = {value, value + 1, value + 2}; "
      "return result; } "
      "int read_third(void) { "
      "struct Triple result = make_triple(40); "
      "return result.third; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(2, psx_hir_module_root_count(hir));
  psx_hir_node_id_t make_root = psx_hir_module_root_at(hir, 0);
  psx_hir_node_id_t read_root = psx_hir_module_root_at(hir, 1);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, make_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(12, test_function_abi_value(test_suite_session, ir, 3, 0));
  ASSERT_EQ(1, test_function_abi_value(test_suite_session, ir, 9, 0));
  ASSERT_EQ(IR_TY_PTR, test_function_abi_value(test_suite_session, ir, 10, 0));
  ASSERT_EQ(IR_ABI_PIECE_INDIRECT,
            test_function_abi_value(test_suite_session, ir, 12, 0));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_PARAM_BIND));
  ASSERT_EQ(0, count_ir_op(ir->funcs, IR_MEMCPY));
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, read_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  const ir_inst_t *call = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block && !call; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL) {
        call = instruction;
        break;
      }
    }
  }
  ASSERT_TRUE(call != NULL);
  ASSERT_EQ(12, test_call_abi_value(test_suite_session, ir, call, 3, 0));
  ASSERT_TRUE(call->result_storage.id != IR_VAL_NONE);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MEMCPY) >= 1);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_odd_sized_aggregate_abi_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_odd_sized_aggregate_abi_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct P3 { char x; char y; char z; }; "
      "struct P3 combine(struct P3 left, struct P3 right) { "
      "struct P3 result = { "
      "left.x + right.x, left.y + right.y, left.z + right.z }; "
      "return result; } "
      "int use_combine(void) { "
      "struct P3 result = combine("
      "(struct P3){1, 2, 3}, (struct P3){4, 5, 6}); "
      "return result.x + result.y + result.z; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(2, psx_hir_module_root_count(hir));
  psx_hir_node_id_t combine_root =
      psx_hir_module_root_at(hir, 0);
  psx_hir_node_id_t caller_root =
      psx_hir_module_root_at(hir, 1);
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = semantic_types,
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, combine_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(3, test_function_abi_value(test_suite_session, ir, 3, 0));
  ASSERT_EQ(1, test_function_abi_value(test_suite_session, ir, 9, 0));
  ASSERT_EQ(IR_ABI_PIECE_INDIRECT,
            test_function_abi_value(test_suite_session, ir, 12, 0));
  ASSERT_EQ(2, test_function_abi_value(test_suite_session, ir, 0, 0));
  ASSERT_EQ(IR_TY_PTR, test_function_abi_value(test_suite_session, ir, 1, 0));
  ASSERT_EQ(IR_TY_PTR, test_function_abi_value(test_suite_session, ir, 1, 1));
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_PARAM_BIND));
  ASSERT_EQ(0, count_ir_op(ir->funcs, IR_MEMCPY));
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, caller_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_CALL));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MEMCPY) >= 3);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_aggregate_ternary_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_aggregate_ternary_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct Triple { int first; int second; int third; }; "
      "struct Triple make_triple(int base) { "
      "struct Triple result = {base, base + 1, base + 2}; "
      "return result; } "
      "struct Triple choose_triple(int choose_call) { "
      "struct Triple fallback = {20, 21, 22}; "
      "return choose_call ? make_triple(10) : fallback; } "
      "int read_choice(int choose_call) { "
      "struct Triple selected = choose_triple(choose_call); "
      "return selected.first + selected.second + selected.third; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(3, psx_hir_module_root_count(hir));
  psx_hir_node_id_t choose_root =
      psx_hir_module_root_at(hir, 1);
  psx_hir_node_id_t caller_root =
      psx_hir_module_root_at(hir, 2);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, choose_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(12, test_function_abi_value(test_suite_session, ir, 3, 0));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_BR_COND));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MEMCPY) >= 2);
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, caller_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_CALL));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MEMCPY) >= 1);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_aggregate_member_storage_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_aggregate_member_storage_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct S { int x; int y; int z; }; "
      "int main(void) { "
      "struct S value = {1, 2, 3}; struct S *pointer = &value; "
      "return value.x + pointer->y * 10 + pointer->z * 100; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  int member_count = 0;
  int saw_pointer_member = 0;
  int saw_object_member = 0;
  int saw_member_offset_0 = 0;
  int saw_member_offset_4 = 0;
  int saw_member_offset_8 = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!node ||
        psx_hir_node_kind(node) != PSX_HIR_MEMBER_ACCESS)
      continue;
    member_count++;
    if (psx_hir_node_member_from_pointer(node))
      saw_pointer_member = 1;
    else
      saw_object_member = 1;
    int offset = psx_hir_node_member_offset(node);
    if (offset == 0) saw_member_offset_0 = 1;
    if (offset == 4) saw_member_offset_4 = 1;
    if (offset == 8) saw_member_offset_8 = 1;
  }
  ASSERT_EQ(3, member_count);
  ASSERT_TRUE(saw_pointer_member);
  ASSERT_TRUE(saw_object_member);
  ASSERT_TRUE(saw_member_offset_0);
  ASSERT_TRUE(saw_member_offset_4);
  ASSERT_TRUE(saw_member_offset_8);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_ALLOCA));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LEA) >= 4);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 4);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_floating_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_floating_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int convert(double input, int increment) { "
      "double value = input + (double)increment + 1.5; "
      "if (value) return (int)-value; "
      "return 0; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_PARAM_BIND));
  unsigned parameter_binding_mask = 0;
  for (const ir_block_t *block = ir->funcs->entry;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op != IR_PARAM_BIND) continue;
      ASSERT_EQ(IR_TY_PTR, instruction->src1.type);
      ASSERT_TRUE(instruction->src1.id >= 0);
      ASSERT_TRUE(instruction->parameter_index < 2);
      parameter_binding_mask |= 1u << instruction->parameter_index;
    }
  }
  ASSERT_EQ(3, parameter_binding_mask);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD_FP_IMM) >= 2);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_I2F));
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_FADD));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_FNE));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_FNEG));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_F2I));
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);

  ag_target_info_t wasm_target = ag_target_info_wasm32();
  options.target = &wasm_target;
  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  unsigned wasm_parameter_binding_mask = 0;
  for (const ir_block_t *block = ir->funcs->entry;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op != IR_PARAM_BIND) continue;
      ASSERT_EQ(IR_TY_PTR, instruction->src1.type);
      ASSERT_TRUE(instruction->src1.id >= 0);
      ASSERT_TRUE(instruction->parameter_index < 2);
      wasm_parameter_binding_mask |= 1u << instruction->parameter_index;
    }
  }
  ASSERT_EQ(parameter_binding_mask, wasm_parameter_binding_mask);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_float_inc_dec_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_float_inc_dec_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int update(void) { "
      "float first = 1.5f; double second = 4.0; "
      "first++; --second; "
      "return (int)(first + second); }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_FADD));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_FSUB));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD_FP_IMM) >= 4);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_structural_sequence_types_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_structural_sequence_types_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct N { int v; struct N *next; }; "
      "struct Holder { struct N *items[3]; }; "
      "int read_members(void) { "
      "struct N first = {1, 0}, second = {2, 0}, third = {3, 0}; "
      "first.next = &second; "
      "struct Holder holder; "
      "holder.items[0] = &first; "
      "holder.items[1] = &second; "
      "holder.items[2] = &third; "
      "return holder.items[0]->next->v + holder.items[2]->v; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  int assignment_count = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node = psx_hir_module_lookup(
        hir, (psx_hir_node_id_t)i);
    if (!node || psx_hir_node_kind(node) != PSX_HIR_ASSIGN) continue;
    const psx_hir_node_t *lhs = NULL;
    const psx_hir_node_t *rhs = NULL;
    for (size_t child_index = 0;
         child_index < psx_hir_node_child_count(node); child_index++) {
      psx_hir_edge_kind_t edge =
          psx_hir_node_child_edge_at(node, child_index);
      if (edge == PSX_HIR_EDGE_LHS)
        lhs = psx_hir_module_lookup(
            hir, psx_hir_node_child_at(node, child_index));
      if (edge == PSX_HIR_EDGE_RHS)
        rhs = psx_hir_module_lookup(
            hir, psx_hir_node_child_at(node, child_index));
    }
    ASSERT_TRUE(lhs != NULL);
    ASSERT_TRUE(rhs != NULL);
    ASSERT_EQ(PSX_HIR_ROLE_EXPRESSION, psx_hir_node_role(lhs));
    ASSERT_EQ(PSX_HIR_ROLE_EXPRESSION, psx_hir_node_role(rhs));
    ASSERT_EQ(psx_hir_node_qual_type(lhs).type_id,
              psx_hir_node_qual_type(node).type_id);
    ASSERT_TRUE(
        psx_hir_node_qual_type(rhs).type_id != PSX_TYPE_ID_INVALID);
    assignment_count++;
  }
  ASSERT_TRUE(assignment_count >= 6);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 9);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_complex_copy_comparison_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_complex_copy_comparison_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "double _Complex identity(double _Complex value) { return value; } "
      "int compare(void) { "
      "_Complex double first = 3.0; "
      "_Complex double copy = identity(first); "
      "_Complex double sum = copy + 1.0; "
      "_Complex double expected = 4.0; "
      "return sum == expected; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(2, psx_hir_module_root_count(hir));
  psx_hir_node_id_t identity_root_id = psx_hir_module_root_at(hir, 0);
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 1);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *identity_ir = ir_build_function_module_from_hir(
      hir, identity_root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(identity_ir != NULL && identity_ir->funcs != NULL);
  ASSERT_EQ(2, test_function_abi_value(test_suite_session, identity_ir, 0, 0));
  ASSERT_EQ(IR_TY_F64, test_function_abi_value(test_suite_session, identity_ir, 1, 0));
  ASSERT_EQ(IR_TY_F64, test_function_abi_value(test_suite_session, identity_ir, 1, 1));
  ASSERT_EQ(0, test_function_abi_value(test_suite_session, identity_ir, 5, 0));
  ASSERT_EQ(0, test_function_abi_value(test_suite_session, identity_ir, 5, 1));
  ASSERT_EQ(0, test_function_abi_value(test_suite_session, identity_ir, 6, 0));
  ASSERT_EQ(8, test_function_abi_value(test_suite_session, identity_ir, 6, 1));
  ASSERT_EQ(IR_ABI_PIECE_COMPLEX_REAL,
            test_function_abi_value(test_suite_session, identity_ir, 7, 0));
  ASSERT_EQ(IR_ABI_PIECE_COMPLEX_IMAGINARY,
            test_function_abi_value(test_suite_session, identity_ir, 7, 1));
  ASSERT_EQ(2, test_function_abi_value(test_suite_session, identity_ir, 9, 0));
  ASSERT_EQ(IR_TY_F64, test_function_abi_value(test_suite_session, identity_ir, 10, 0));
  ASSERT_EQ(IR_TY_F64, test_function_abi_value(test_suite_session, identity_ir, 10, 1));
  ASSERT_EQ(0, test_function_abi_value(test_suite_session, identity_ir, 11, 0));
  ASSERT_EQ(8, test_function_abi_value(test_suite_session, identity_ir, 11, 1));
  ASSERT_EQ(IR_ABI_PIECE_COMPLEX_REAL,
            test_function_abi_value(test_suite_session, identity_ir, 12, 0));
  ASSERT_EQ(IR_ABI_PIECE_COMPLEX_IMAGINARY,
            test_function_abi_value(test_suite_session, identity_ir, 12, 1));
  ir_module_free(identity_ir);
  status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  const ir_inst_t *identity_call = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block && !identity_call; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL) {
        identity_call = instruction;
        break;
      }
    }
  }
  ASSERT_TRUE(identity_call != NULL);
  ASSERT_EQ(1, identity_call->nargs);
  ASSERT_TRUE(identity_call->args[0].type.type_id !=
              PSX_TYPE_ID_INVALID);
  ASSERT_EQ(IR_CALL_ARGUMENT_ADDRESS,
            identity_call->args[0].representation);
  ir_abi_module_t *identity_call_abi = test_lower_ir_abi(test_suite_session, ir);
  size_t identity_piece_count = 0;
  const ir_abi_argument_t *identity_pieces = ir_abi_call_arguments(
      identity_call_abi, identity_call, &identity_piece_count);
  ASSERT_TRUE(identity_call_abi != NULL && identity_pieces != NULL);
  ASSERT_EQ(2, identity_piece_count);
  ASSERT_EQ(IR_ABI_ARGUMENT_LOAD, identity_pieces[0].access);
  ASSERT_EQ(IR_ABI_ARGUMENT_LOAD, identity_pieces[1].access);
  ASSERT_EQ(0, identity_pieces[0].byte_offset);
  ASSERT_EQ(8, identity_pieces[1].byte_offset);
  ir_abi_module_free(identity_call_abi);
  ASSERT_EQ(7, count_ir_op(ir->funcs, IR_ALLOCA));
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_MEMCPY));
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_FADD));
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_FEQ));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_AND));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_complex_width_conversion_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_complex_width_conversion_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int convert_complex(void) { "
      "double _Complex wide = {3.0, 4.0}; "
      "float _Complex narrow = wide; "
      "double _Complex restored = narrow; "
      "narrow = restored; "
      "return ((float *)&narrow)[1] > 3.9f; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_F2F) >= 6);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_indirect_call_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_indirect_call_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int apply(int (*callback)(int), int value) { "
      "return callback(value); }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_CALL));
  const ir_inst_t *call = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block && !call; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL) {
        call = instruction;
        break;
      }
    }
  }
  ASSERT_TRUE(call != NULL);
  ASSERT_TRUE(call->sym == NULL);
  ASSERT_TRUE(call->callee.id >= 0);
  ASSERT_EQ(IR_TY_PTR, call->callee.type);
  ASSERT_TRUE(call->has_function_type);
  ASSERT_EQ(1, test_call_abi_value(test_suite_session, ir, call, 0, 0));
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, call, 1, 0));
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, call, 4, 0));
  ASSERT_EQ(1, call->nargs);
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, call, 1, 0));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_unprototyped_indirect_call_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_unprototyped_indirect_call_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "typedef int (*callback_t)(); "
      "int apply_unprototyped(callback_t callback) { "
      "return callback(7); }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  const ir_inst_t *call = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block && !call; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL) {
        call = instruction;
        break;
      }
    }
  }
  ASSERT_TRUE(call != NULL);
  ASSERT_TRUE(call->sym == NULL);
  ASSERT_EQ(1, call->nargs);
  ASSERT_EQ(1, test_call_abi_value(test_suite_session, ir, call, 0, 0));
  ASSERT_TRUE(call->has_function_type);
  ASSERT_EQ(1, test_call_abi_value(test_suite_session, ir, call, 0, 0));
  ASSERT_EQ(IR_TY_I32, test_call_abi_value(test_suite_session, ir, call, 1, 0));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_void_function_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_void_function_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "void assign(int *pointer, int value) { *pointer = value; return; } "
      "void clear(int *pointer) { *pointer = 0; } "
      "int run(void) { int value = 1; assign(&value, 7); "
      "clear(&value); return value; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(3, psx_hir_module_root_count(hir));
  psx_hir_node_id_t assign_root = psx_hir_module_root_at(hir, 0);
  psx_hir_node_id_t clear_root = psx_hir_module_root_at(hir, 1);
  psx_hir_node_id_t run_root = psx_hir_module_root_at(hir, 2);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *assign_ir = ir_build_function_module_from_hir(
      hir, assign_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(assign_ir != NULL && assign_ir->funcs != NULL);
  ASSERT_EQ(IR_TY_VOID, test_function_abi_value(test_suite_session, assign_ir, 8, 0));
  ASSERT_EQ(1, count_ir_op(assign_ir->funcs, IR_RET));
  ir_module_free(assign_ir);

  status = IR_HIR_BUILD_INVALID;
  ir_module_t *clear_ir = ir_build_function_module_from_hir(
      hir, clear_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(clear_ir != NULL && clear_ir->funcs != NULL);
  ASSERT_EQ(IR_TY_VOID, test_function_abi_value(test_suite_session, clear_ir, 8, 0));
  ASSERT_EQ(1, count_ir_op(clear_ir->funcs, IR_RET));
  ir_module_free(clear_ir);

  status = IR_HIR_BUILD_INVALID;
  ir_module_t *run_ir = ir_build_function_module_from_hir(
      hir, run_root, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(run_ir != NULL && run_ir->funcs != NULL);
  ASSERT_EQ(2, count_ir_op(run_ir->funcs, IR_CALL));
  int void_call_count = 0;
  for (const ir_block_t *block = run_ir->funcs->entry;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_CALL && instruction->is_void_call &&
          instruction->dst.id == IR_VAL_NONE &&
          instruction->dst.type == IR_TY_VOID)
        void_call_count++;
    }
  }
  ASSERT_EQ(2, void_call_count);
  ASSERT_EQ(1, count_ir_op(run_ir->funcs, IR_RET));
  ir_module_free(run_ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_symbol_reference_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_symbol_reference_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int consume(const char *text, int (*callback)(int)); "
      "int increment(int value) { return value + 1; } "
      "int run(void) { return consume(\"x\", increment); }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(2, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 1);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_LOAD_STR));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_LOAD_SYM));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_CALL));
  const ir_inst_t *function_reference = NULL;
  for (const ir_block_t *block = ir->funcs->entry;
       block && !function_reference; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next) {
      if (instruction->op == IR_LOAD_SYM) {
        function_reference = instruction;
        break;
      }
    }
  }
  ASSERT_TRUE(function_reference != NULL);
  ASSERT_TRUE(function_reference->is_function_symbol);
  ASSERT_TRUE(!function_reference->is_external_symbol);
  ASSERT_TRUE(function_reference->has_function_type);
  ASSERT_TRUE(
      function_reference->function_type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_EQ(1, function_reference->function_type.param_count);
  ASSERT_EQ(1, test_reference_abi_value(test_suite_session,
                   ir, function_reference, 0, 0));
  ASSERT_EQ(IR_TY_I32, test_reference_abi_value(test_suite_session,
                            ir, function_reference, 2, 0));
  ir_symbol_t *callback_slots = ir_module_add_symbol(
      ir, "callback_slots", 14);
  ir_symbol_func_ref_t *callback_ref = ir_symbol_add_func_ref(
      callback_slots, 0, function_reference->sym,
      function_reference->sym_len, &function_reference->function_type);
  ir_abi_module_t *symbol_abi = test_lower_ir_abi(test_suite_session, ir);
  const ir_abi_signature_t *callback_ref_abi =
      ir_abi_symbol_reference_signature(symbol_abi, callback_ref);
  ASSERT_TRUE(callback_ref_abi != NULL);
  ASSERT_EQ(1, callback_ref_abi->param_count);
  ASSERT_EQ(IR_TY_I32, callback_ref_abi->param_pieces[0].type);
  ASSERT_EQ(IR_TY_I32,
            ir_abi_signature_direct_result_type(callback_ref_abi));
  ir_abi_module_free(symbol_abi);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_global_symbol_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_global_symbol_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "extern int external_value; static int internal_value; "
      "_Thread_local int tls_value; "
      "int use_globals(int value) { internal_value = value; "
      "tls_value = internal_value; return external_value + tls_value; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  ASSERT_EQ(3, psx_hir_module_symbol_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  const psx_hir_symbol_t *external = NULL;
  const psx_hir_symbol_t *internal = NULL;
  const psx_hir_symbol_t *tls = NULL;
  for (size_t i = 1; i <= psx_hir_module_symbol_count(hir); i++) {
    const psx_hir_symbol_t *symbol = psx_hir_module_symbol_lookup(
        hir, (psx_hir_symbol_id_t)i);
    size_t name_length = 0;
    const char *name = psx_hir_symbol_name(symbol, &name_length);
    ASSERT_TRUE(name != NULL);
    if (name_length == strlen("external_value") &&
        strncmp(name, "external_value", name_length) == 0)
      external = symbol;
    if (name_length == strlen("internal_value") &&
        strncmp(name, "internal_value", name_length) == 0)
      internal = symbol;
    if (name_length == strlen("tls_value") &&
        strncmp(name, "tls_value", name_length) == 0)
      tls = symbol;
  }
  ASSERT_TRUE(external != NULL && internal != NULL && tls != NULL);
  ASSERT_TRUE(psx_hir_symbol_is_extern(external));
  ASSERT_TRUE(!psx_hir_symbol_is_static(external));
  ASSERT_TRUE(psx_hir_symbol_is_static(internal));
  ASSERT_TRUE(!psx_hir_symbol_is_extern(internal));
  ASSERT_TRUE(psx_hir_symbol_is_thread_local(tls));
  ASSERT_EQ(4, psx_hir_symbol_byte_size(external));
  ASSERT_EQ(4, psx_hir_symbol_alignment(external));
  ASSERT_TRUE(
      psx_hir_symbol_qual_type(external).type_id != PSX_TYPE_ID_INVALID);

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD_SYM) >= 3);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD_TLS_SYM) >= 2);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD) >= 3);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_PARAM_BIND));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 2);
  ASSERT_TRUE(ir_module_find_symbol(ir, "external_value", 14) != NULL);
  ASSERT_TRUE(ir_module_find_symbol(ir, "internal_value", 14) != NULL);
  ASSERT_TRUE(ir_module_find_symbol(ir, "tls_value", 9) != NULL);
  ASSERT_TRUE(
      ir_module_find_symbol(ir, "external_value", 14)->is_extern);
  ASSERT_TRUE(
      ir_module_find_symbol(ir, "internal_value", 14)->is_static);
  ASSERT_TRUE(
      ir_module_find_symbol(ir, "tls_value", 9)->is_thread_local);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_EQ(0, psx_hir_module_symbol_count(hir));
}

static void test_typed_hir_bitfield_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_bitfield_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "struct Bits { unsigned int a:3; int b:5; }; "
      "struct Bits bits = {3, -2}; "
      "int set_bits(int value) { bits.a = value; "
      "return bits.a * 10 + bits.b; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  ASSERT_EQ(1, psx_hir_module_symbol_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  int bitfield_count = 0;
  int signed_count = 0;
  const psx_hir_node_t *signed_bitfield = NULL;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node = psx_hir_module_lookup(
        hir, (psx_hir_node_id_t)i);
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (!psx_hir_node_bitfield_info(
            node, &bit_width, &bit_offset, &bit_is_signed))
      continue;
    ASSERT_TRUE(bit_width > 0);
    ASSERT_TRUE(bit_offset >= 0);
    bitfield_count++;
    if (bit_is_signed) {
      signed_count++;
      signed_bitfield = node;
    }
  }
  ASSERT_TRUE(bitfield_count >= 3);
  ASSERT_TRUE(signed_count >= 1);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));
  ASSERT_TRUE(psx_hir_node_bitfield_info(
      signed_bitfield, NULL, NULL, NULL));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD_SYM) >= 3);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD) >= 3);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_PARAM_BIND));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 1);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_AND) >= 4);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_SHR) >= 1);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_XOR) >= 1);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_conditional_expr_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_conditional_expr_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "void fail(void); "
      "int select(int *pointer, int condition) { "
      "int a = condition && ((*pointer = *pointer + 1) != 0); "
      "int b = condition || ((*pointer = *pointer + 2) != 0); "
      "condition ? (void)0 : fail(); "
      "return condition ? a : b; }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_BR_COND) >= 4);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_BR) >= 5);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LABEL) >= 9);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ALLOCA) >= 7);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 9);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD) >= 7);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_CALL));
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_control_flow_lowering_without_ast(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_control_flow_lowering_without_ast...\n");
  reset_test_translation_unit_state(test_suite_session);
  int program_resolved = resolve_program_input_hir(test_suite_session,
      "int sum(int n) { int r = 0; int branch; "
      "if (n > 10) branch = 1; else branch = 2; r = r + branch; "
      "goto start; r = 99; start: "
      "while (n > 0) {"
      "if (n == 2) { n = n - 1; continue; }"
      "r = r + n; n = n - 1; } "
      "again: if (n > 3) { n = n - 1; goto again; } "
      "switch (n) { case 0: r = r + 10; "
      "case 1: r = r + 1; break; default: r = 99; } return r; } "
      "int terminal_switch(int n) { switch (n) { "
      "case 1: return 10; case 2: return 20; default: return 99; } }");
  ASSERT_TRUE(program_resolved);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(2, psx_hir_module_root_count(hir));
  psx_hir_node_id_t root_id = psx_hir_module_root_at(hir, 0);
  psx_hir_node_id_t terminal_root_id = psx_hir_module_root_at(hir, 1);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_BR_COND) >= 4);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_BR) >= 12);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LABEL) >= 12);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_EQ) >= 3);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  for (const ir_block_t *block = ir->funcs->entry->next;
       block; block = block->next) {
    for (const ir_inst_t *instruction = block->head;
         instruction; instruction = instruction->next)
      ASSERT_TRUE(instruction->op != IR_ALLOCA);
  }
  ir_module_free(ir);

  status = IR_HIR_BUILD_INVALID;
  ir = ir_build_function_module_from_hir(
      hir, terminal_root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(3, count_ir_op(ir->funcs, IR_RET));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_BR_COND) >= 2);
  ir_module_free(ir);
  reset_test_translation_unit_state(test_suite_session);
}

static int parse_raw_function_item(
    ag_compilation_session_t *test_suite_session,
    psx_parser_stream_t *stream, psx_parsed_toplevel_item_t *item) {
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item->kind);
  ps_decl_reset_locals_in(test_local_registry(test_suite_session));
  local_storage_reset(test_lowering_context(test_suite_session));
  token_ident_t *function_name =
      item->value.function_header.declarator.identifier;
  ps_decl_set_current_funcname_in(
      test_local_registry(test_suite_session),
      function_name ? function_name->str : NULL,
      function_name ? function_name->len : 0);
  psx_parser_name_environment_t name_environment;
  ps_parser_name_environment_init(
      &name_environment,
      ps_ctx_name_classifier(test_semantic_context(test_suite_session)));
  psx_scope_lookup_point_t function_lookup_point =
      test_scope_lookup_point(test_suite_session);
  ps_parser_name_environment_reset_at(
      &name_environment,
      ps_ctx_name_classifier(test_semantic_context(test_suite_session)),
      function_lookup_point.scope_id,
      psx_scope_graph_next_scope_id(test_scope_graph(test_suite_session)),
      function_lookup_point.declaration_order);
  psx_name_classifier_t local_classifier =
      ps_parser_name_environment_classifier(
          &name_environment);
  psx_frontend_local_declaration_syntax_adapter_t
      local_declaration_adapter;
  psx_local_declaration_callbacks_t local_declarations;
  psx_frontend_init_local_declaration_syntax_adapter(
      &local_declaration_adapter, &local_declarations,
      ag_compilation_session_parser_runtime_context(
          test_suite_session),
      &local_classifier);
  const psx_parsed_function_suffix_t *primary_suffix =
      psx_declarator_outermost_function_suffix(
          &item->value.function_header.declarator);
  ASSERT_TRUE(primary_suffix != NULL);
  for (int i = 0;
       primary_suffix->parameters &&
       i < primary_suffix->parameters->count; i++) {
    const psx_parsed_declarator_t *parameter =
        &primary_suffix->parameters->items[i].declarator;
    for (int b = 0; b < parameter->array_bound_count; b++) {
      ASSERT_TRUE(parameter->array_bounds[b].expression.node != NULL);
    }
  }
  psx_record_function_definition_declarator_binding_events(
      &item->value.function_header.declarator,
      &local_declarations.name_classifier);
  psx_statement_syntax_adapter_t statement_adapter;
  ASSERT_TRUE(psx_statement_syntax_adapter_init(
      &statement_adapter,
      ag_compilation_session_parser_runtime_context(
          test_suite_session),
      &local_declarations.name_classifier,
      &local_declarations));
  psx_statement_syntax_context_t statement_syntax =
      psx_statement_syntax_adapter_context(
          &statement_adapter);
  int parsed = ps_parse_function_definition_body(
      stream, &item->value.function_header, &statement_syntax);
  ps_decl_set_current_funcname_in(
      test_local_registry(test_suite_session), NULL, 0);
  ps_parser_name_environment_dispose(&name_environment);
  return parsed;
}

static void assert_direct_function_rejection(
    ag_compilation_session_t *test_suite_session,
    const char *source,
    psx_syntax_typed_hir_rejection_t expected_rejection,
    int expected_node_kind) {
  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));

  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_resolved_hir_build_failure_t failure;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
          ag_compilation_session_options_view(test_suite_session),
          &item.value.function_header, &typed_hir, &failure));
  ASSERT_TRUE(typed_hir == NULL);
  ASSERT_EQ(expected_rejection, failure.rejection);
  ASSERT_EQ(expected_node_kind, failure.source_node_kind);

  ps_dispose_function_definition_syntax(
      &item.value.function_header);
  ps_parser_stream_end(&stream);
  reset_test_translation_unit_state(test_suite_session);
}

static void assert_direct_function_resolution(
    ag_compilation_session_t *test_suite_session, const char *source) {
  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));

  const node_t *syntax_body = item.value.function_header.body;
  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_resolved_hir_build_failure_t failure;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
          ag_compilation_session_options_view(test_suite_session),
          &item.value.function_header, &typed_hir, &failure));
  ASSERT_TRUE(typed_hir != NULL);
  ASSERT_TRUE(item.value.function_header.body == syntax_body);

  ps_dispose_function_definition_syntax(
      &item.value.function_header);
  ps_parser_stream_end(&stream);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_typed_hir_build_failure_first_cause_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typed_hir_build_failure_first_cause_boundary...\n");
  token_t child_token = {.line_no = 3};
  token_t parent_token = {.line_no = 1};
  psx_resolved_hir_build_failure_t failure;
  psx_resolved_hir_build_failure_init(&failure);
  psx_resolved_hir_build_failure_note(
      &failure, PSX_RESOLVED_HIR_BUILD_INTERNAL_FAILURE,
      ND_IDENTIFIER, &child_token);
  psx_resolved_hir_build_failure_note(
      &failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
      ND_BLOCK, &parent_token);
  ASSERT_EQ(PSX_RESOLVED_HIR_BUILD_INTERNAL_FAILURE,
            failure.status);
  ASSERT_EQ(ND_IDENTIFIER, failure.source_node_kind);
  ASSERT_TRUE(failure.source_token == &child_token);

  arena_context_t *arena_context = arena_context_create();
  ASSERT_TRUE(arena_context != NULL);
  psx_semantic_node_builder_t builder;
  psx_resolved_hir_build_failure_init(&failure);
  psx_semantic_node_builder_init(
      &builder, arena_context, test_semantic_context(test_suite_session), &failure);
  arena_fail_allocations_after_in(arena_context, 0);
  const psx_hir_node_spec_t nop_spec = {
      .kind = PSX_HIR_NOP,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  ASSERT_TRUE(psx_semantic_node_builder_statement(
                  &builder, &nop_spec, NULL, NULL, 0,
                  ND_NULL_STMT) == NULL);
  ASSERT_EQ(PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
            failure.status);
  ASSERT_EQ(ND_NULL_STMT, failure.source_node_kind);
  arena_clear_allocation_failure_in(arena_context);
  arena_context_destroy(arena_context);
}

static void test_direct_const_aggregate_initializer_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_direct_const_aggregate_initializer_boundary...\n");
  assert_direct_function_resolution(test_suite_session,
      "int __direct_const_aggregate(void *semantic_context, "
      "char *function_name, int function_name_len, "
      "const void *fallback_diag_tok) { "
      "struct lookup_point { int scope_id; int declaration_order; }; "
      "struct binding_request { void *semantic_context; "
      "char *function_name; int function_name_len; "
      "const void *fallback_diag_tok; "
      "struct lookup_point lookup_point; unsigned char has_lookup; }; "
      "const struct binding_request request = {"
      ".semantic_context = semantic_context, "
      ".function_name = function_name, "
      ".function_name_len = function_name_len, "
      ".fallback_diag_tok = fallback_diag_tok}; "
      "return request.function_name_len + request.has_lookup; }");
}

static void test_predefined_function_name_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_predefined_function_name_typed_hir_boundary...\n");
  const char *source =
      "int direct_func_name(void) { "
      "return __func__[0] + sizeof __func__; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item.kind);
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));

  psx_parsed_function_definition_t *syntax_function =
      &item.value.function_header;
  node_block_t *body = (node_block_t *)syntax_function->body;
  ASSERT_TRUE(body != NULL);
  ASSERT_EQ(ND_RETURN, body->body[0]->kind);
  node_t *addition = body->body[0]->lhs;
  ASSERT_EQ(ND_ADD, addition->kind);
  ASSERT_EQ(ND_SUBSCRIPT, addition->lhs->kind);
  node_t *subscript_name = addition->lhs->lhs;
  ASSERT_EQ(ND_IDENTIFIER, subscript_name->kind);
  ASSERT_EQ(ND_SIZEOF_QUERY, addition->rhs->kind);
  node_t *sizeof_name =
      ((node_sizeof_query_t *)addition->rhs)->operand;
  ASSERT_EQ(ND_IDENTIFIER, sizeof_name->kind);

  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_resolved_hir_build_failure_t failure;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
          ag_compilation_session_options_view(test_suite_session),
          syntax_function, &typed_hir, &failure));
  ASSERT_TRUE(typed_hir != NULL);
  ASSERT_EQ(ND_IDENTIFIER, subscript_name->kind);
  ASSERT_EQ(ND_IDENTIFIER, sizeof_name->kind);

  psx_hir_module_t *hir = psx_hir_module_create();
  ASSERT_TRUE(hir != NULL);
  psx_hir_node_id_t root_id = psx_typed_hir_tree_emit(
      hir, typed_hir, &failure);
  ASSERT_TRUE(root_id != PSX_HIR_NODE_ID_INVALID);
  int found_function_name = 0;
  int found_function_name_size = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!node) continue;
    if (psx_hir_node_kind(node) == PSX_HIR_STRING) {
      size_t literal_length = 0;
      const char *contents = psx_hir_node_literal_contents(
          node, &literal_length);
      if (contents && literal_length == 16 &&
          memcmp(contents, "direct_func_name", 16) == 0 &&
          psx_hir_node_object_size(node) == 17)
        found_function_name = 1;
    }
    if (psx_hir_node_kind(node) == PSX_HIR_NUMBER &&
        psx_hir_node_integer_value(node) == 17)
      found_function_name_size = 1;
  }
  ASSERT_TRUE(found_function_name);
  ASSERT_TRUE(found_function_name_size);

  psx_hir_module_destroy(hir);
  ps_dispose_function_definition_syntax(syntax_function);
  ps_parser_stream_end(&stream);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_builtin_expect_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_builtin_expect_typed_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  node_t *syntax = parse_expr_input_with_existing_locals(test_suite_session,
      "__builtin_expect(1 + 2, 0)");
  ASSERT_EQ(ND_FUNCALL, syntax->kind);
  node_function_call_t *call = (node_function_call_t *)syntax;
  ASSERT_EQ(2, call->argument_count);
  ASSERT_TRUE(call->callee != NULL);
  ASSERT_EQ(ND_IDENTIFIER, call->callee->kind);
  const node_identifier_t *callee =
      (const node_identifier_t *)call->callee;
  ASSERT_EQ(16, callee->name_len);
  ASSERT_TRUE(memcmp(callee->name, "__builtin_expect", 16) == 0);
  ASSERT_EQ(ND_ADD, call->arguments[0]->kind);
  ASSERT_EQ(ND_NUM, call->arguments[1]->kind);

  node_t *value_syntax = call->arguments[0];
  node_t *expectation_syntax = call->arguments[1];
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_hir(test_suite_session, syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_ADD, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
  ASSERT_EQ(3, psx_hir_module_node_count(expression.module));
  ASSERT_EQ(ND_FUNCALL, syntax->kind);
  ASSERT_TRUE(call->arguments[0] == value_syntax);
  ASSERT_TRUE(call->arguments[1] == expectation_syntax);
  ASSERT_EQ(ND_ADD, call->arguments[0]->kind);
  ASSERT_EQ(ND_NUM, call->arguments[1]->kind);
  psx_frontend_expression_hir_dispose(&expression);

  psx_resolved_hir_build_failure_t failure = {0};
  const psx_typed_hir_tree_t *constant_hir = NULL;
  psx_syntax_integer_constant_result_t constant = {0};
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), NULL, syntax,
          &constant_hir, &constant, &failure));
  ASSERT_TRUE(constant_hir != NULL);
  ASSERT_TRUE(constant.is_constant);
  ASSERT_EQ(3, constant.value);

  node_t *invalid = parse_expr_input_with_existing_locals(test_suite_session,
      "__builtin_expect(1)");
  ASSERT_EQ(ND_FUNCALL, invalid->kind);
  const psx_typed_hir_tree_t *invalid_hir = NULL;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), invalid, &invalid_hir, &failure));
  ASSERT_TRUE(invalid_hir == NULL);
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_ARGUMENT_COUNT_MISMATCH,
      failure.rejection);

  reset_test_translation_unit_state(test_suite_session);
}

static node_num_t *as_num(node_t *n) { return (node_num_t *)n; }


static node_block_t *as_block(node_t *n) { return (node_block_t *)n; }
static node_ctrl_t *as_ctrl(node_t *n) { return (node_ctrl_t *)n; }
static node_case_t *as_case(node_t *n) { return (node_case_t *)n; }





static void assert_canonical_qual_type_signature(
    ag_compilation_session_t *test_suite_session,
    psx_qual_type_t type, const char *expected) {
  char actual[512];
  int len = psx_format_canonical_type_signature(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)), type,
      ps_ctx_data_layout(test_semantic_context(test_suite_session)), actual,
      sizeof(actual));
  ASSERT_TRUE(len >= 0);
  ASSERT_TRUE((size_t)len < sizeof(actual));
  if (strcmp(expected, actual) != 0) {
    fprintf(stderr, "canonical TypeId mismatch: expected %s, got %s\n",
            expected, actual);
    exit(1);
  }
}

static const psx_hir_node_t *find_test_hir_node_kind(
    const psx_hir_module_t *hir, psx_hir_node_kind_t kind,
    size_t occurrence);

static const psx_hir_node_t *find_test_named_hir_node(
    const psx_hir_module_t *hir, psx_hir_node_kind_t kind,
    const char *expected_name, size_t occurrence) {
  if (!hir || !expected_name) return NULL;
  size_t expected_length = strlen(expected_name);
  size_t found = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!node || psx_hir_node_kind(node) != kind) continue;
    size_t name_length = 0;
    const char *name = psx_hir_node_name(node, &name_length);
    if (!name || name_length != expected_length ||
        memcmp(name, expected_name, expected_length) != 0)
      continue;
    if (found++ == occurrence) return node;
  }
  return NULL;
}

static const psx_hir_node_t *find_test_hir_function_parameter(
    const psx_hir_module_t *hir, const psx_hir_node_t *function,
    const char *expected_name) {
  if (!hir || !function || !expected_name) return NULL;
  size_t expected_length = strlen(expected_name);
  for (size_t i = 0; i < psx_hir_node_child_count(function); i++) {
    if (psx_hir_node_child_edge_at(function, i) !=
        PSX_HIR_EDGE_PARAMETER)
      continue;
    const psx_hir_node_t *parameter = psx_hir_module_lookup(
        hir, psx_hir_node_child_at(function, i));
    size_t name_length = 0;
    const char *name = psx_hir_node_name(parameter, &name_length);
    if (name && name_length == expected_length &&
        memcmp(name, expected_name, expected_length) == 0)
      return parameter;
  }
  return NULL;
}

static const psx_scope_declaration_t *find_test_scope_declaration(
    const psx_scope_graph_t *scope_graph, const char *expected_name,
    psx_scope_decl_kind_t expected_kind, size_t occurrence) {
  if (!scope_graph || !expected_name) return NULL;
  size_t expected_length = strlen(expected_name);
  size_t found = 0;
  for (size_t i = 0;
       i < psx_scope_graph_declaration_count(scope_graph); i++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i);
    if (!declaration || declaration->kind != expected_kind ||
        declaration->name_len != (int)expected_length ||
        memcmp(declaration->name, expected_name, expected_length) != 0)
      continue;
    if (found++ == occurrence) return declaration;
  }
  return NULL;
}

static void test_local_declaration_frontend_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_local_declaration_frontend_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { int block_fn(int); "
      "int self = sizeof self, a = 2, b = a + 3; "
      "typedef int T, U; T value = b; U other = 1; "
      "static int s = 4, t = 5; "
      "return block_fn(value) + self + other + s + t; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "self", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "b", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "value", 0) != NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  test_scope_graph(test_suite_session), "s",
                  PSX_DECL_LOCAL_OBJECT, 0) != NULL);
  psx_qual_type_t block_function = ps_ctx_get_function_qual_type_in(
      test_semantic_context(test_suite_session), (char *)"block_fn", 8);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            test_qual_type_shape(test_suite_session, block_function).kind);
}

static void test_function_parameter_point_of_declaration_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_function_parameter_point_of_declaration_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __parameter_order(int m, int k, int t[][m][3][k]); "
      "int __parameter_order(int m, int k, int t[][m][3][k]) { "
      "return t[0][0][0][0]; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *function = find_test_named_hir_node(
      hir, PSX_HIR_FUNCTION, "__parameter_order", 0);
  ASSERT_TRUE(function != NULL);
  const psx_hir_node_t *m =
      find_test_hir_function_parameter(hir, function, "m");
  const psx_hir_node_t *k =
      find_test_hir_function_parameter(hir, function, "k");
  const psx_hir_node_t *t =
      find_test_hir_function_parameter(hir, function, "t");
  ASSERT_TRUE(m != NULL);
  ASSERT_TRUE(k != NULL);
  ASSERT_TRUE(t != NULL);
  psx_scope_graph_t *scope_graph = test_scope_graph(test_suite_session);
  const psx_scope_declaration_t *prototype_m = NULL;
  for (size_t i = 0;
       i < psx_scope_graph_declaration_count(scope_graph); i++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i);
    if (!declaration || declaration->name_len != 1 ||
        declaration->name[0] != 'm' ||
        psx_scope_graph_scope_kind(
            scope_graph, declaration->scope_id) !=
            PSX_SCOPE_FUNCTION_PROTOTYPE)
      continue;
    ASSERT_TRUE(declaration->kind != PSX_DECL_LOCAL_OBJECT);
    if (declaration->kind == PSX_DECL_PARAMETER)
      prototype_m = declaration;
  }
  ASSERT_TRUE(prototype_m != NULL);
  psx_qual_type_t prototype_m_type =
      psx_prototype_parameter_qual_type(prototype_m->payload);
  psx_type_shape_t prototype_m_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      prototype_m_type.type_id, &prototype_m_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, prototype_m_shape.kind);
  ASSERT_TRUE(psx_semantic_type_table_unqualified_types_match(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      prototype_m_type, psx_hir_node_qual_type(m)));
  ASSERT_EQ(3, psx_hir_node_vla_dimension_count(t));
  ASSERT_EQ(psx_hir_node_storage_offset(m),
            psx_hir_node_vla_dimension_source_offset(t, 0));
  ASSERT_EQ(3, psx_hir_node_vla_dimension_constant(t, 1));
  ASSERT_EQ(psx_hir_node_storage_offset(k),
            psx_hir_node_vla_dimension_source_offset(t, 2));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __deep_parameter_vla(int n, "
      "int t[][n][40000][n][n][n][n][n][n][n][n]) { return 0; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  function = find_test_named_hir_node(
      hir, PSX_HIR_FUNCTION, "__deep_parameter_vla", 0);
  const psx_hir_node_t *n =
      find_test_hir_function_parameter(hir, function, "n");
  t = find_test_hir_function_parameter(hir, function, "t");
  ASSERT_TRUE(n != NULL);
  ASSERT_TRUE(t != NULL);
  ASSERT_EQ(10, psx_hir_node_vla_dimension_count(t));
  ASSERT_EQ(psx_hir_node_storage_offset(n),
            psx_hir_node_vla_dimension_source_offset(t, 0));
  ASSERT_EQ(40000, psx_hir_node_vla_dimension_constant(t, 1));
  ASSERT_EQ(psx_hir_node_storage_offset(n),
            psx_hir_node_vla_dimension_source_offset(t, 9));
  ASSERT_EQ(0, psx_hir_node_vla_dimension_constant(t, 10));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __parameter_vla_scopes(int n, int m, int g[n][m]) { "
      "int sum = 0; "
      "for (int i = 0; i < n; i++) "
      "for (int j = 0; j < m; j++) sum += g[i][j]; "
      "return sum; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  function = find_test_named_hir_node(
      hir, PSX_HIR_FUNCTION, "__parameter_vla_scopes", 0);
  ASSERT_TRUE(find_test_hir_function_parameter(
                  hir, function, "g") != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "i", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "j", 0) != NULL);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __function_pointer_target(int c, int b) { return c - b; } "
      "int (*__function_pointer_factory(int a, int b))(int c, int b) { "
      "if (a != b) return __function_pointer_target; return 0; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  function = find_test_named_hir_node(
      hir, PSX_HIR_FUNCTION, "__function_pointer_factory", 0);
  ASSERT_TRUE(function != NULL);
  ASSERT_TRUE(find_test_hir_function_parameter(
                  hir, function, "a") != NULL);
  ASSERT_TRUE(find_test_hir_function_parameter(
                  hir, function, "b") != NULL);
}

static void assert_identifier_resolution_kind(
    ag_compilation_session_t *test_suite_session,
    char *name, int name_len, int is_call,
    psx_identifier_resolution_kind_t expected) {
  psx_identifier_resolution_t resolution;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .name = name,
          .name_len = name_len,
          .is_call = is_call,
      },
      &resolution);
  ASSERT_EQ(expected, resolution.kind);
}

static void test_identifier_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_identifier_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "enum __IdentifierEnum { __identifier_enum = 7 }; "
      "int __identifier_global; "
      "int __identifier_array[3]; "
      "int __identifier_function(int value);"));
  lvar_t *local = register_test_default_storage_fixture(test_suite_session,
      (char *)"__identifier_local", 18);
  ASSERT_TRUE(local != NULL);
  lvar_t *local_array = register_test_default_storage_fixture(test_suite_session,
      (char *)"__identifier_local_array", 24);
  ASSERT_TRUE(local_array != NULL);
  set_test_storage_fixture_type(test_suite_session,
      local_array,
      ps_ctx_intern_array_of_qual_type_in(
          test_semantic_context(test_suite_session),
          ps_ctx_intern_integer_qual_type_in(
              test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0),
          4, 0));

  assert_identifier_resolution_kind(test_suite_session,
      (char *)"__identifier_local", 18, 0, PSX_IDENTIFIER_LOCAL);
  assert_identifier_resolution_kind(test_suite_session,
      (char *)"__identifier_enum", 17, 0,
      PSX_IDENTIFIER_ENUM_CONSTANT);
  assert_identifier_resolution_kind(test_suite_session,
      (char *)"__identifier_global", 19, 0,
      PSX_IDENTIFIER_GLOBAL_OBJECT);
  assert_identifier_resolution_kind(test_suite_session,
      (char *)"__identifier_global", 19, 1,
      PSX_IDENTIFIER_GLOBAL_OBJECT);
  assert_identifier_resolution_kind(test_suite_session,
      (char *)"__identifier_function", 21, 0,
      PSX_IDENTIFIER_FUNCTION);
  assert_identifier_resolution_kind(test_suite_session,
      (char *)"__va_arg_area", 13, 0,
      PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA);
  psx_identifier_resolution_t function_call;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .name = (char *)"__identifier_function",
          .name_len = 21,
          .is_call = 1,
      },
      &function_call);
  ASSERT_EQ(PSX_IDENTIFIER_FUNCTION, function_call.kind);
  ASSERT_TRUE(function_call.function != NULL);
  ASSERT_TRUE(function_call.function ==
              ps_ctx_find_function_symbol_in(test_semantic_context(test_suite_session),
                  (char *)"__identifier_function", 21));
  psx_qual_type_t resolved_function_type =
      ps_function_symbol_qual_type(function_call.function);
  ASSERT_TRUE(resolved_function_type.type_id != PSX_TYPE_ID_INVALID);
  psx_type_shape_t resolved_function_shape =
      test_qual_type_shape(test_suite_session, resolved_function_type);
  ASSERT_EQ(PSX_TYPE_FUNCTION, resolved_function_shape.kind);
  ASSERT_EQ(1, resolved_function_shape.parameter_count);
  ASSERT_TRUE(!resolved_function_shape.is_variadic_function);
  psx_identifier_expression_resolution_t function_expression;
  psx_resolve_identifier_expression(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .name = (char *)"__identifier_function",
          .name_len = 21,
      },
      &function_expression);
  ASSERT_EQ(PSX_IDENTIFIER_FUNCTION,
            function_expression.symbol.kind);
  ASSERT_TRUE(function_expression.decays_function_to_pointer);
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  psx_type_shape_t function_declaration_shape = {0};
  psx_type_shape_t function_expression_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      semantic_types, function_expression.declaration_qual_type.type_id,
      &function_declaration_shape));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      semantic_types, function_expression.expression_qual_type.type_id,
      &function_expression_shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_declaration_shape.kind);
  ASSERT_EQ(PSX_TYPE_POINTER, function_expression_shape.kind);
  ASSERT_EQ(function_expression.declaration_qual_type.type_id,
            psx_semantic_type_table_base(
                semantic_types,
                function_expression.expression_qual_type.type_id).type_id);
  psx_identifier_expression_resolution_t array_expression;
  psx_resolve_identifier_expression(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .name = (char *)"__identifier_array",
          .name_len = 18,
      },
      &array_expression);
  ASSERT_EQ(PSX_IDENTIFIER_GLOBAL_OBJECT,
            array_expression.symbol.kind);
  ASSERT_TRUE(array_expression.decays_array_to_address);
  ASSERT_TRUE(array_expression.declaration_qual_type.type_id !=
              PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(array_expression.expression_qual_type.type_id !=
              PSX_TYPE_ID_INVALID);
  psx_type_shape_t array_declaration_shape = {0};
  psx_type_shape_t array_expression_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      semantic_types, array_expression.declaration_qual_type.type_id,
      &array_declaration_shape));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      semantic_types, array_expression.expression_qual_type.type_id,
      &array_expression_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, array_declaration_shape.kind);
  ASSERT_EQ(PSX_TYPE_POINTER, array_expression_shape.kind);
  psx_qual_type_t array_element = psx_semantic_type_table_base(
      semantic_types, array_expression.declaration_qual_type.type_id);
  psx_qual_type_t pointer_pointee = psx_semantic_type_table_base(
      semantic_types, array_expression.expression_qual_type.type_id);
  ASSERT_EQ(array_element.type_id, pointer_pointee.type_id);
  ASSERT_EQ(array_element.qualifiers, pointer_pointee.qualifiers);
  psx_identifier_expression_resolution_t local_array_expression;
  psx_resolve_identifier_expression(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .name = (char *)"__identifier_local_array",
          .name_len = 24,
      },
      &local_array_expression);
  ASSERT_EQ(PSX_IDENTIFIER_LOCAL,
            local_array_expression.symbol.kind);
  ASSERT_TRUE(local_array_expression.symbol.local == local_array);
  ASSERT_TRUE(local_array_expression.decays_array_to_address);
  ASSERT_TRUE(!local_array_expression.local_is_vla_object);
  ASSERT_TRUE(!local_array_expression.local_has_static_storage);
  psx_type_shape_t local_array_declaration_shape = {0};
  psx_type_shape_t local_array_expression_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      semantic_types,
      local_array_expression.declaration_qual_type.type_id,
      &local_array_declaration_shape));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      semantic_types,
      local_array_expression.expression_qual_type.type_id,
      &local_array_expression_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, local_array_declaration_shape.kind);
  ASSERT_EQ(PSX_TYPE_POINTER, local_array_expression_shape.kind);
  assert_identifier_resolution_kind(test_suite_session,
      (char *)"__identifier_missing", 20, 0,
      PSX_IDENTIFIER_UNDEFINED);
  assert_identifier_resolution_kind(test_suite_session,
      (char *)"__identifier_missing", 20, 1,
      PSX_IDENTIFIER_UNDECLARED_CALL);
}

static void test_persistent_local_scope_lookup_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_persistent_local_scope_lookup_boundary...\n");
  reset_test_locals(test_suite_session);

  psx_scope_graph_t *scope_graph =
      ps_local_registry_scope_graph(test_local_registry(test_suite_session));
  psx_scope_id_t function_scope =
      psx_scope_graph_current_scope(scope_graph);
  ASSERT_EQ(PSX_SCOPE_FUNCTION,
            psx_scope_graph_scope_kind(scope_graph, function_scope));
  ps_decl_leave_scope_in(test_local_registry(test_suite_session));
  ASSERT_EQ(function_scope,
            psx_scope_graph_current_scope(scope_graph));

  lvar_t *outer = register_test_storage_fixture(test_suite_session,
      (char *)"__scope_value", 13, 4, 4, 0);
  ASSERT_TRUE(outer != NULL);

  ps_decl_enter_scope_in(test_local_registry(test_suite_session));
  psx_scope_lookup_point_t before_inner =
      test_scope_lookup_point(test_suite_session);
  lvar_t *inner = register_test_storage_fixture(test_suite_session,
      (char *)"__scope_value", 13, 4, 4, 0);
  ASSERT_TRUE(inner != NULL);
  psx_scope_lookup_point_t after_inner =
      test_scope_lookup_point(test_suite_session);
  ASSERT_TRUE(find_test_visible_local_var_in(test_local_registry(test_suite_session),
                  (char *)"__scope_value", 13, before_inner) == outer);
  ASSERT_TRUE(find_test_visible_local_var_in(test_local_registry(test_suite_session),
                  (char *)"__scope_value", 13, after_inner) == inner);

  ps_decl_enter_scope_in(test_local_registry(test_suite_session));
  psx_scope_lookup_point_t nested =
      test_scope_lookup_point(test_suite_session);
  ASSERT_TRUE(find_test_visible_local_var_in(test_local_registry(test_suite_session),
                  (char *)"__scope_value", 13, nested) == inner);
  ps_decl_leave_scope_in(test_local_registry(test_suite_session));
  ps_decl_leave_scope_in(test_local_registry(test_suite_session));

  psx_identifier_resolution_t delayed_resolution;
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .name = (char *)"__scope_value",
          .name_len = 13,
          .has_lookup_point = 1,
          .lookup_point = before_inner,
      },
      &delayed_resolution);
  ASSERT_EQ(PSX_IDENTIFIER_LOCAL, delayed_resolution.kind);
  ASSERT_TRUE(delayed_resolution.local == outer);
  psx_resolve_identifier(
      &(psx_identifier_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .name = (char *)"__scope_value",
          .name_len = 13,
          .has_lookup_point = 1,
          .lookup_point = after_inner,
      },
      &delayed_resolution);
  ASSERT_EQ(PSX_IDENTIFIER_LOCAL, delayed_resolution.kind);
  ASSERT_TRUE(delayed_resolution.local == inner);

  ps_decl_enter_scope_in(test_local_registry(test_suite_session));
  psx_scope_lookup_point_t sibling_before_decl =
      test_scope_lookup_point(test_suite_session);
  lvar_t *sibling = register_test_storage_fixture(test_suite_session,
      (char *)"__sibling_only", 14, 4, 4, 0);
  ASSERT_TRUE(sibling != NULL);
  ASSERT_TRUE(find_test_visible_local_var_in(test_local_registry(test_suite_session),
                  (char *)"__sibling_only", 14,
                  sibling_before_decl) == NULL);
  ASSERT_TRUE(find_test_visible_local_var_in(test_local_registry(test_suite_session),
                  (char *)"__scope_value", 13,
                  sibling_before_decl) == outer);
  ps_decl_leave_scope_in(test_local_registry(test_suite_session));

  ps_decl_enter_scope_in(test_local_registry(test_suite_session));
  psx_scope_lookup_point_t other_sibling =
      test_scope_lookup_point(test_suite_session);
  ASSERT_TRUE(find_test_visible_local_var_in(test_local_registry(test_suite_session),
                  (char *)"__sibling_only", 14,
                  other_sibling) == NULL);
  ASSERT_TRUE(find_test_visible_local_var_in(test_local_registry(test_suite_session),
                  (char *)"__scope_value", 13,
                  other_sibling) == outer);
  ps_decl_leave_scope_in(test_local_registry(test_suite_session));

  ps_decl_enter_scope_in(test_local_registry(test_suite_session));
  psx_scope_lookup_point_t before_enum =
      test_scope_lookup_point(test_suite_session);
  ASSERT_TRUE(test_semantic_define_enum_const(test_suite_session,
      (char *)"__scoped_enum", 13, 29));
  psx_scope_lookup_point_t after_enum =
      test_scope_lookup_point(test_suite_session);
  ps_decl_leave_scope_in(test_local_registry(test_suite_session));
  long long enum_value = 0;
  ASSERT_TRUE(!ps_ctx_find_enum_const_at_in(test_semantic_context(test_suite_session),
      (char *)"__scoped_enum", 13, before_enum, &enum_value));
  ASSERT_TRUE(ps_ctx_find_enum_const_at_in(test_semantic_context(test_suite_session),
      (char *)"__scoped_enum", 13, after_enum, &enum_value));
  ASSERT_EQ(29, enum_value);

  ps_decl_enter_scope_in(test_local_registry(test_suite_session));
  psx_scope_lookup_point_t enum_sibling =
      test_scope_lookup_point(test_suite_session);
  ASSERT_TRUE(!ps_ctx_find_enum_const_at_in(test_semantic_context(test_suite_session),
      (char *)"__scoped_enum", 13, enum_sibling, &enum_value));
  ps_decl_leave_scope_in(test_local_registry(test_suite_session));
}

static void test_member_access_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_member_access_typed_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  psx_semantic_context_t *semantic_context = test_semantic_context(test_suite_session);
  const char member_tag_name[] = "__MemberBoundary";
  const int member_tag_len = (int)sizeof(member_tag_name) - 1;
  ASSERT_TRUE(test_semantic_register_tag_type(test_suite_session,
      TK_STRUCT, (char *)member_tag_name, member_tag_len,
      0, 0, 0, 0));

  psx_qual_type_t character_qual_type =
      ps_ctx_intern_integer_qual_type_in(
          semantic_context, PSX_INTEGER_KIND_CHAR, 0, 1);
  psx_qual_type_t integer_qual_type =
      ps_ctx_intern_integer_qual_type_in(
          semantic_context, PSX_INTEGER_KIND_INT, 0, 0);
  ASSERT_TRUE(character_qual_type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(integer_qual_type.type_id != PSX_TYPE_ID_INVALID);
  const psx_record_member_decl_t member_declarations[2] = {
      {
          .name = (char *)"prefix",
          .len = 6,
          .decl_qual_type = character_qual_type,
      },
      {
          .name = (char *)"value",
          .len = 5,
          .decl_qual_type = integer_qual_type,
      },
  };
  const psx_record_member_layout_t member_layouts[2] = {
      {.offset = 0},
      {.offset = 4},
  };
  const psx_record_decl_t *member_record =
      ps_ctx_ensure_tag_record_decl_in(
          semantic_context, TK_STRUCT, (char *)member_tag_name,
          member_tag_len);
  ASSERT_TRUE(member_record != NULL);
  int conflict_index = -1;
  ASSERT_TRUE(ps_ctx_register_record_members_in(
      semantic_context, member_record->record_id, member_declarations,
      member_layouts, 2, &conflict_index));
  ASSERT_EQ(-1, conflict_index);
  ASSERT_TRUE(test_semantic_register_tag_type(test_suite_session,
      TK_STRUCT, (char *)member_tag_name, member_tag_len,
      1, 2, 8, 4));

  ASSERT_EQ(2, member_record->member_count);
  const psx_record_layout_t *member_layout = psx_record_layout_table_lookup(
      ps_ctx_record_layout_table_in(semantic_context),
      member_record->record_id, ps_ctx_data_layout(semantic_context));
  ASSERT_TRUE(member_layout != NULL);
  ASSERT_EQ(4, psx_record_layout_member(member_layout, 1)->offset);

  psx_qual_type_t object_qual_type =
      ps_ctx_intern_record_qual_type_in(
          semantic_context, member_record->record_id);
  ASSERT_TRUE(object_qual_type.type_id != PSX_TYPE_ID_INVALID);
  psx_hir_member_resolution_t resolution;
  ASSERT_TRUE(psx_resolve_member_hir_node_spec_in(
      semantic_context, object_qual_type, "value", 5, 0,
      &resolution));
  ASSERT_EQ(PSX_MEMBER_ACCESS_OK, resolution.member.status);
  ASSERT_EQ(1, resolution.member.member_index);
  ASSERT_EQ(member_record->record_id, resolution.member.record_id);
  psx_type_shape_t member_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      ps_ctx_semantic_type_table_in(semantic_context),
      resolution.member.member_qual_type.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, member_shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_INT, member_shape.integer_kind);
  ASSERT_TRUE(resolution.member.base_object_qual_type.type_id !=
              PSX_TYPE_ID_INVALID);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_NONE,
            resolution.member.base_object_qual_type.qualifiers);
  psx_type_shape_t base_object_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      ps_ctx_semantic_type_table_in(semantic_context),
      resolution.member.base_object_qual_type.type_id, &base_object_shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, base_object_shape.kind);
  ASSERT_EQ(resolution.member.record_id, base_object_shape.record_id);

  psx_qual_type_t const_object_qual_type = object_qual_type;
  const_object_qual_type.qualifiers |= PSX_TYPE_QUALIFIER_CONST;
  ASSERT_TRUE(psx_resolve_member_hir_node_spec_in(
      semantic_context, const_object_qual_type, "value", 5, 0,
      &resolution));
  ASSERT_EQ(PSX_MEMBER_ACCESS_OK, resolution.member.status);
  ASSERT_TRUE((resolution.member.base_object_qual_type.qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);
  ASSERT_TRUE((resolution.member.member_qual_type.qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);

  psx_qual_type_t const_pointer_qual_type =
      ps_ctx_intern_pointer_to_qual_type_in(
          semantic_context, const_object_qual_type);
  ASSERT_TRUE(psx_resolve_member_hir_node_spec_in(
      semantic_context, const_pointer_qual_type, "value", 5, 1,
      &resolution));
  ASSERT_EQ(PSX_MEMBER_ACCESS_OK, resolution.member.status);
  ASSERT_TRUE((resolution.member.base_object_qual_type.qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);

  ASSERT_TRUE(!psx_resolve_member_hir_node_spec_in(
      semantic_context, object_qual_type, "value", 5, 1,
      &resolution));
  ASSERT_EQ(PSX_MEMBER_ACCESS_INVALID_BASE, resolution.member.status);
  ASSERT_TRUE(!psx_resolve_member_hir_node_spec_in(
      semantic_context, object_qual_type, "missing", 7, 0,
      &resolution));
  ASSERT_EQ(PSX_MEMBER_ACCESS_NOT_FOUND, resolution.member.status);

  reset_test_locals(test_suite_session);
  lvar_t *raw_object = register_test_qual_type_storage_fixture(test_suite_session,
      (char *)"object", 6, 8, 4, object_qual_type);
  ASSERT_TRUE(raw_object != NULL);
  node_t *raw_access = parse_expr_input_with_existing_locals(test_suite_session,
      "object.value");
  ASSERT_EQ(ND_MEMBER_ACCESS, raw_access->kind);
  node_member_access_t *member_syntax =
      (node_member_access_t *)raw_access;
  ASSERT_TRUE(!member_syntax->from_pointer);
  ASSERT_EQ(5, member_syntax->member_name_len);
  ASSERT_TRUE(strncmp(member_syntax->member_name, "value", 5) == 0);
  node_t *raw_base = member_syntax->base.lhs;
  psx_frontend_expression_hir_t access_expression =
      resolve_test_expression_hir(test_suite_session, raw_access);
  const psx_hir_node_t *access_hir =
      test_expression_hir_root(&access_expression);
  ASSERT_EQ(PSX_HIR_MEMBER_ACCESS,
            psx_hir_node_kind(access_hir));
  ASSERT_EQ(4, psx_hir_node_member_offset(access_hir));
  ASSERT_TRUE(!psx_hir_node_member_from_pointer(access_hir));
  ASSERT_EQ(1, psx_hir_node_child_count(access_hir));
  const psx_hir_node_t *base_hir = psx_hir_module_lookup(
      access_expression.module,
      psx_hir_node_child_at(access_hir, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(base_hir));
  ASSERT_EQ(ps_lvar_offset(raw_object),
            psx_hir_node_storage_offset(base_hir));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, access_hir).kind);
  ASSERT_EQ(ND_MEMBER_ACCESS, raw_access->kind);
  ASSERT_TRUE(member_syntax->base.lhs == raw_base);
  psx_frontend_expression_hir_dispose(&access_expression);

  psx_qual_type_t pointer_qual_type =
      ps_ctx_intern_pointer_to_qual_type_in(
          semantic_context, integer_qual_type);
  ASSERT_EQ(PSX_DEREF_OPERAND_OK,
            psx_resolve_deref_operand_qual_type_in(
                semantic_context, pointer_qual_type));
  ASSERT_EQ(PSX_DEREF_OPERAND_NOT_POINTER,
            psx_resolve_deref_operand_qual_type_in(
                semantic_context, integer_qual_type));
  psx_subscript_qual_types_resolution_t subscript;
  psx_resolve_subscript_qual_types_in(
      semantic_context, integer_qual_type,
      pointer_qual_type, &subscript);
  ASSERT_EQ(PSX_SUBSCRIPT_OPERANDS_OK, subscript.status);
  ASSERT_TRUE(subscript.swapped);
  ASSERT_EQ(pointer_qual_type.type_id,
            subscript.base_qual_type.type_id);
  psx_resolve_subscript_qual_types_in(
      semantic_context, integer_qual_type,
      integer_qual_type, &subscript);
  ASSERT_EQ(PSX_SUBSCRIPT_OPERANDS_INVALID, subscript.status);

  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  size_t hir_checkpoint = psx_hir_module_node_count(hir);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct __MemberProgram { char prefix; int value; }; "
      "int __typed_hir_member_access(struct __MemberProgram *pointer) { "
      "struct __MemberProgram object; object.value = 1; "
      "return pointer->value + object.value; }"));
  int direct_member_count = 0;
  int pointer_member_count = 0;
  for (size_t i = hir_checkpoint + 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *hir_node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!hir_node ||
        psx_hir_node_kind(hir_node) != PSX_HIR_MEMBER_ACCESS)
      continue;
    ASSERT_EQ(4, psx_hir_node_member_offset(hir_node));
    ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, hir_node).kind);
    if (psx_hir_node_member_from_pointer(hir_node))
      pointer_member_count++;
    else
      direct_member_count++;
  }
  ASSERT_TRUE(direct_member_count >= 2);
  ASSERT_TRUE(pointer_member_count >= 1);
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



static void expect_parse_fail(
    ag_compilation_session_t *test_suite_session, const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    diag_reset_records_in(test_diagnostics(test_suite_session));
    token_t *head = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input);
    int resolved = resolve_test_program_hir_from(test_suite_session, head);
    _exit(!resolved || diag_has_error_records_in(test_diagnostics(test_suite_session)) ? 1 : 0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    fprintf(stderr, "Expected parse failure: %s\n", input);
  ASSERT_TRUE(WEXITSTATUS(status) != 0);
}

static void expect_parse_ok(
    ag_compilation_session_t *test_suite_session, const char *input) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    diag_reset_records_in(test_diagnostics(test_suite_session));
    token_t *head = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input);
    int resolved = resolve_test_program_hir_from(test_suite_session, head);
    _exit(!resolved || diag_has_error_records_in(test_diagnostics(test_suite_session)) ? 1 : 0);
  }
  int status;
  waitpid(pid, &status, 0);
  ASSERT_TRUE(WIFEXITED(status));
  if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
    fprintf(stderr, "Expected parse success: %s\n", input);
  ASSERT_EQ(0, WEXITSTATUS(status));
}




static void expect_parse_fail_with_message(
    ag_compilation_session_t *test_suite_session, const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics(test_suite_session));
    token_t *head = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input);
    (void)resolve_test_program_hir_from(test_suite_session, head);
    _exit(diag_has_error_records_in(test_diagnostics(test_suite_session)) ? 1 : 0);
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

static void expect_parse_fail_without_message(
    ag_compilation_session_t *test_suite_session, const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics(test_suite_session));
    token_t *head = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input);
    (void)resolve_test_program_hir_from(test_suite_session, head);
    _exit(diag_has_error_records_in(test_diagnostics(test_suite_session)) ? 1 : 0);
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


static void expect_parse_ok_without_message(
    ag_compilation_session_t *test_suite_session, const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics(test_suite_session));
    token_t *head = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input);
    (void)resolve_test_program_hir_from(test_suite_session, head);
    _exit(diag_has_error_records_in(test_diagnostics(test_suite_session)) ? 1 : 0);
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
  if (strstr(buf, needle)) {
    fprintf(stderr,
            "Unexpected warning found\ninput: %s\nunexpected: %s\nactual: %s\n",
            input, needle, buf);
  }
  ASSERT_TRUE(strstr(buf, needle) == NULL);
}

static void expect_parse_ok_with_message(
    ag_compilation_session_t *test_suite_session, const char *input, const char *needle) {
  int fds[2];
  ASSERT_TRUE(pipe(fds) == 0);

  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    freopen("/dev/null", "w", stdout);
    diag_reset_records_in(test_diagnostics(test_suite_session));
    token_t *head = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)input);
    (void)resolve_test_program_hir_from(test_suite_session, head);
    _exit(diag_has_error_records_in(test_diagnostics(test_suite_session)) ? 1 : 0);
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
  if (!strstr(buf, needle)) {
    fprintf(stderr,
            "Expected warning not found\ninput: %s\nexpected: %s\nactual: %s\n",
            input, needle, buf);
  }
  ASSERT_TRUE(strstr(buf, needle) != NULL);
}

#if defined(DIAG_LANG_ALL)
static void collect_printf_signature(
    const char *format, char *signature, size_t capacity) {
  size_t used = 0;
  for (const char *p = format; p && *p; p++) {
    if (*p != '%') continue;
    if (p[1] == '%') {
      p++;
      continue;
    }
    const char *start = p++;
    while (*p && !strchr("diouxXfFeEgGaAcspn", *p)) p++;
    ASSERT_TRUE(*p != '\0');
    size_t length = (size_t)(p - start + 1);
    ASSERT_TRUE(used + length + 1 < capacity);
    memcpy(signature + used, start, length);
    used += length;
    signature[used++] = '|';
  }
  ASSERT_TRUE(used < capacity);
  signature[used] = '\0';
}

static void assert_catalog_format_parity(
    const char *ja_format, const char *en_format) {
  char ja_signature[128];
  char en_signature[128];
  ASSERT_TRUE(ja_format != NULL);
  ASSERT_TRUE(en_format != NULL);
  collect_printf_signature(
      ja_format, ja_signature, sizeof(ja_signature));
  collect_printf_signature(
      en_format, en_signature, sizeof(en_signature));
  ASSERT_TRUE(strcmp(ja_signature, en_signature) == 0);
}

static void test_diagnostic_catalog_localization(
    ag_compilation_session_t *test_suite_session) {
  static const diag_warn_id_t warning_ids[] = {
      DIAG_WARN_PARSER_MISSING_RETURN,
      DIAG_WARN_PARSER_RETURN_STACK_ADDRESS,
      DIAG_WARN_PARSER_ASSIGN_IN_CONDITION,
      DIAG_WARN_PARSER_COMMA_IN_CONDITION,
      DIAG_WARN_PARSER_EMPTY_BODY,
      DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING,
      DIAG_WARN_PARSER_CONSTANT_OVERFLOW,
      DIAG_WARN_PARSER_SELF_ASSIGN,
      DIAG_WARN_PARSER_SELF_COMPARE,
      DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE,
      DIAG_WARN_PARSER_DIVIDE_BY_ZERO,
      DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL,
      DIAG_WARN_PARSER_SIGN_COMPARE,
      DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO,
      DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS,
      DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES,
      DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE,
      DIAG_WARN_PARSER_INTEGER_OVERFLOW,
  };
  static const diag_error_id_t localized_error_ids[] = {
      DIAG_ERR_PARSER_CONTINUATION_ENTRY_TYPE,
      DIAG_ERR_PARSER_CONTINUATION_FRAME_CONDITION_TYPE,
      DIAG_ERR_PARSER_CONTINUATION_GOTO_LABEL_ACROSS_FRAMES,
      DIAG_ERR_PARSER_CONTINUATION_VLA_ACROSS_FRAMES,
      DIAG_ERR_PARSER_CONTINUATION_ALLOCA_ACROSS_FRAMES,
      DIAG_ERR_PARSER_CONTINUATION_FRAME_LOOP_REQUIRED,
      DIAG_ERR_PARSER_CONTINUATION_FRAME_CONDITION_CALL_COUNT,
      DIAG_ERR_CODEGEN_WASM_OBJECT_OPEN_FAILED,
      DIAG_ERR_CODEGEN_WASM_OBJECT_ADDRESSABLE_SIZE_EXCEEDED,
      DIAG_ERR_CODEGEN_WASM_OBJECT_WRITE_FAILED,
      DIAG_ERR_CODEGEN_WASM_OBJECT_OUTPUT_SINK_MISSING,
  };

  for (size_t i = 0;
       i < sizeof(warning_ids) / sizeof(warning_ids[0]); i++) {
    ASSERT_TRUE(strcmp(
        diag_warn_message_ja(warning_ids[i]),
        diag_warn_key(warning_ids[i])) != 0);
    ASSERT_TRUE(strcmp(
        diag_warn_message_en(warning_ids[i]),
        diag_warn_key(warning_ids[i])) != 0);
    assert_catalog_format_parity(
        diag_warn_message_ja(warning_ids[i]),
        diag_warn_message_en(warning_ids[i]));
  }
  for (size_t i = 0;
       i < sizeof(localized_error_ids) / sizeof(localized_error_ids[0]);
       i++) {
    ASSERT_TRUE(strcmp(
        diag_message_ja(localized_error_ids[i]),
        diag_error_key(localized_error_ids[i])) != 0);
    ASSERT_TRUE(strcmp(
        diag_message_en(localized_error_ids[i]),
        diag_error_key(localized_error_ids[i])) != 0);
    assert_catalog_format_parity(
        diag_message_ja(localized_error_ids[i]),
        diag_message_en(localized_error_ids[i]));
  }

  ag_source_manager_t *en_sources = ag_source_manager_create();
  ag_source_manager_t *ja_sources = ag_source_manager_create();
  ag_diagnostic_context_t *en = diag_context_create(en_sources);
  ag_diagnostic_context_t *ja = diag_context_create(ja_sources);
  ASSERT_TRUE(en != NULL);
  ASSERT_TRUE(ja != NULL);
  diag_context_set_locale(en, "en");
  diag_context_set_locale(ja, "ja");

  const char *en_format = diag_warn_message_for_in(
      en, DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL);
  const char *ja_format = diag_warn_message_for_in(
      ja, DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL);
  char en_message[256];
  char ja_message[256];
  snprintf(en_message, sizeof(en_message), en_format, 6, "printf");
  snprintf(ja_message, sizeof(ja_message), ja_format, 6, "printf");
  ASSERT_TRUE(strstr(en_message, "printf") != NULL);
  ASSERT_TRUE(strstr(en_message, "not declared") != NULL);
  ASSERT_TRUE(strstr(en_message, "関数") == NULL);
  ASSERT_TRUE(strstr(ja_message, "printf") != NULL);
  ASSERT_TRUE(strstr(ja_message, "関数") != NULL);
  ASSERT_TRUE(strstr(ja_message, "not declared") == NULL);

  diag_context_set_locale(en, "ja");
  ASSERT_TRUE(strstr(
      diag_warn_message_for_in(
          en, DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL),
      "関数") != NULL);
  diag_context_set_locale(en, "en");
  ASSERT_TRUE(strstr(
      diag_warn_message_for_in(
          en, DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL),
      "not declared") != NULL);
  ASSERT_TRUE(strstr(
      diag_warn_message_for_in(
          ja, DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL),
      "関数") != NULL);

  diag_context_destroy(en);
  diag_context_destroy(ja);
  ag_source_manager_destroy(en_sources);
  ag_source_manager_destroy(ja_sources);

  diag_context_set_locale(test_diagnostics(test_suite_session), "en");
  expect_parse_ok_with_message(test_suite_session,
      "int main(void){ return printf(); }", "function 'printf'");
  diag_context_set_locale(test_diagnostics(test_suite_session), "ja");
  expect_parse_ok_with_message(test_suite_session,
      "int main(void){ return printf(); }", "関数 'printf'");
}
#endif

static void test_expr_number(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_number...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "42", &syntax);
  const psx_hir_node_t *number =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_NUM, syntax->kind);
  ASSERT_EQ(42, as_num(syntax)->val);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(number));
  ASSERT_EQ(42, psx_hir_node_integer_value(number));
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_tokenizer(test_suite_session))->kind);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session, "0L", &syntax);
  number = test_expression_hir_root(&expression);
  psx_type_shape_t shape = test_hir_type_shape(test_suite_session, number);
  ASSERT_EQ(ND_NUM, syntax->kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, shape.integer_kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session,
                   psx_hir_node_qual_type(number).type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session, "0UL", &syntax);
  number = test_expression_hir_root(&expression);
  shape = test_hir_type_shape(test_suite_session, number);
  ASSERT_EQ(ND_NUM, syntax->kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, shape.integer_kind);
  ASSERT_TRUE(shape.is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session, "0LL", &syntax);
  number = test_expression_hir_root(&expression);
  shape = test_hir_type_shape(test_suite_session, number);
  ASSERT_EQ(ND_NUM, syntax->kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG_LONG, shape.integer_kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_float(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_float...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "3.14 + 1.5f", &syntax);
  const psx_hir_node_t *addition =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_ADD, syntax->kind);
  ASSERT_EQ(ND_NUM, syntax->lhs->kind);
  ASSERT_TRUE(as_num(syntax->lhs)->fval > 3.13 &&
              as_num(syntax->lhs)->fval < 3.15);
  ASSERT_EQ(ND_NUM, syntax->rhs->kind);
  ASSERT_TRUE(as_num(syntax->rhs)->fval > 1.49 &&
              as_num(syntax->rhs)->fval < 1.51);
  ASSERT_EQ(PSX_HIR_ADD, psx_hir_node_kind(addition));
  const psx_hir_node_t *lhs = psx_hir_module_lookup(
      expression.module, psx_hir_node_child_at(addition, 0));
  const psx_hir_node_t *rhs = psx_hir_module_lookup(
      expression.module, psx_hir_node_child_at(addition, 1));
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(lhs));
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(rhs));
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            test_hir_type_shape(test_suite_session, lhs).floating_kind);
  ASSERT_EQ(PSX_FLOATING_KIND_FLOAT,
            test_hir_type_shape(test_suite_session, rhs).floating_kind);
  ASSERT_TRUE(psx_hir_node_floating_value(lhs) > 3.13 &&
              psx_hir_node_floating_value(lhs) < 3.15);
  ASSERT_TRUE(psx_hir_node_floating_value(rhs) > 1.49 &&
              psx_hir_node_floating_value(rhs) < 1.51);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_long_double_suffix_metadata(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_long_double_suffix_metadata...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "4.0L", &syntax);
  const psx_hir_node_t *number =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_NUM, syntax->kind);
  ASSERT_EQ(TK_FLOAT_SUFFIX_L, as_num(syntax)->float_suffix_kind);
  ASSERT_TRUE(as_num(syntax)->fval > 3.9 &&
              as_num(syntax)->fval < 4.1);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(number));
  ASSERT_EQ(PSX_FLOATING_KIND_LONG_DOUBLE,
            test_hir_type_shape(test_suite_session, number).floating_kind);
  ASSERT_TRUE(psx_hir_node_floating_value(number) > 3.9 &&
              psx_hir_node_floating_value(number) < 4.1);
  psx_frontend_expression_hir_dispose(&expression);

  bool found = false;
  iter_test_float_literals(test_suite_session, find_long_double_float_literal, &found);
  ASSERT_TRUE(found);
}

static void test_expr_compound_literal_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_compound_literal_typed_hir_boundary...\n");
  reset_test_locals(test_suite_session);
  node_t *raw = parse_expr_input_with_existing_locals(test_suite_session, "(int){3}");
  ASSERT_EQ(ND_COMPOUND_LITERAL, raw->kind);
  node_compound_literal_t *compound = (node_compound_literal_t *)raw;
  ASSERT_TRUE(compound->type_name.syntax != NULL);
  ASSERT_EQ(PSX_SCOPE_ID_INVALID, compound->type_name.scope_seq);
  ASSERT_EQ(
      PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC,
      psx_compound_literal_storage_duration_in_scope_graph(
          ps_ctx_scope_graph(test_semantic_context(test_suite_session)),
          compound->type_name.scope_seq, 1));
  ASSERT_EQ(ND_INIT_LIST, raw->rhs->kind);
  psx_parsed_type_name_t *type_name_syntax = compound->type_name.syntax;
  node_t *initializer_syntax = raw->rhs;
  psx_frontend_expression_hir_t compound_expression =
      resolve_test_expression_hir(test_suite_session, raw);
  const psx_hir_node_t *compound_hir =
      test_expression_hir_root(&compound_expression);
  ASSERT_EQ(PSX_HIR_STMT_EXPR,
            psx_hir_node_kind(compound_hir));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_hir_type_shape(test_suite_session, compound_hir).kind);
  ASSERT_EQ(2, psx_hir_node_child_count(compound_hir));
  const psx_hir_node_t *initializer_hir = psx_hir_module_lookup(
      compound_expression.module,
      psx_hir_node_child_at(compound_hir, 0));
  const psx_hir_node_t *value_hir = psx_hir_module_lookup(
      compound_expression.module,
      psx_hir_node_child_at(compound_hir, 1));
  ASSERT_EQ(PSX_HIR_BLOCK, psx_hir_node_kind(initializer_hir));
  ASSERT_TRUE(psx_hir_node_child_count(initializer_hir) > 0);
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(value_hir));
  ASSERT_EQ(ND_COMPOUND_LITERAL, raw->kind);
  ASSERT_TRUE(compound->type_name.syntax == type_name_syntax);
  ASSERT_TRUE(raw->rhs == initializer_syntax);
  ASSERT_EQ(ND_INIT_LIST, raw->rhs->kind);
  psx_frontend_expression_hir_dispose(&compound_expression);

  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  size_t hir_checkpoint = psx_hir_module_node_count(hir);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __typed_hir_compound_literal(void) { return (int){3}; }"));
  int found_compound_sequence = 0;
  for (size_t i = hir_checkpoint + 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *hir_node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!hir_node ||
        psx_hir_node_kind(hir_node) != PSX_HIR_STMT_EXPR)
      continue;
    for (size_t child = 0;
         child < psx_hir_node_child_count(hir_node); child++) {
      const psx_hir_node_t *value = psx_hir_module_lookup(
          hir, psx_hir_node_child_at(hir_node, child));
      if (value && psx_hir_node_kind(value) == PSX_HIR_LOCAL) {
        found_compound_sequence = 1;
        break;
      }
    }
  }
  ASSERT_TRUE(found_compound_sequence);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct __CompoundValue {"
      "  int kind;"
      "  union {"
      "    const int *expression;"
      "    struct { void *local; int offset; } local;"
      "    struct { long long integer_value; double floating_value; } number;"
      "  };"
      "};"
      "const int *__typed_hir_promoted_union_compound(const int *value) {"
      "  return ((struct __CompoundValue){"
      "      .kind = 0, .expression = value}).expression;"
      "}"));
}

static void test_expr_compound_literal_array_subscript(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_compound_literal_array_subscript...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "(int[3]){1,2,3}", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  const psx_hir_node_t *array_value =
      test_hir_array_decay_source(test_suite_session, &expression);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax->kind);
  ASSERT_EQ(ND_INIT_LIST, syntax->rhs->kind);
  ASSERT_EQ(PSX_HIR_ADDRESS, psx_hir_node_kind(root));
  ASSERT_EQ(3, test_hir_type_shape(test_suite_session, array_value).array_len);
  ASSERT_EQ(12, test_type_size_id(test_suite_session,
                    psx_hir_node_qual_type(array_value).type_id));
  psx_frontend_expression_hir_dispose(&expression);

  // 配列型複合リテラルへの添字アクセス: ((int[2]){1,2})[1]
  expression = resolve_test_expression_input_hir(test_suite_session,
      "((int[2]){1,2})[1]", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(ND_SUBSCRIPT, syntax->kind);
  ASSERT_EQ(PSX_HIR_SUBSCRIPT, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(int[]){1,2,3}", &syntax);
  root = test_expression_hir_root(&expression);
  array_value = test_hir_array_decay_source(test_suite_session, &expression);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax->kind);
  ASSERT_EQ(PSX_HIR_ADDRESS, psx_hir_node_kind(root));
  ASSERT_EQ(3, test_hir_type_shape(test_suite_session, array_value).array_len);
  ASSERT_EQ(12, test_type_size_id(test_suite_session,
                    psx_hir_node_qual_type(array_value).type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(char[]){\"abc\"}", &syntax);
  root = test_expression_hir_root(&expression);
  array_value = test_hir_array_decay_source(test_suite_session, &expression);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax->kind);
  ASSERT_EQ(PSX_HIR_ADDRESS, psx_hir_node_kind(root));
  ASSERT_EQ(4, test_hir_type_shape(test_suite_session, array_value).array_len);
  ASSERT_EQ(4, test_type_size_id(test_suite_session,
                   psx_hir_node_qual_type(array_value).type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(int *[]){0,0}", &syntax);
  root = test_expression_hir_root(&expression);
  array_value = test_hir_array_decay_source(test_suite_session, &expression);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax->kind);
  ASSERT_EQ(PSX_HIR_ADDRESS, psx_hir_node_kind(root));
  ASSERT_EQ(2, test_hir_type_shape(test_suite_session, array_value).array_len);
  ASSERT_EQ(16, test_type_size_id(test_suite_session,
                    psx_hir_node_qual_type(array_value).type_id));
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_add_sub(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_add_sub...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "1 + 2 - 3", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  const psx_hir_node_t *addition =
      test_hir_child(&expression, root, 0);
  ASSERT_EQ(ND_SUB, syntax->kind);
  ASSERT_EQ(ND_ADD, syntax->lhs->kind);
  ASSERT_EQ(1, as_num(syntax->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(syntax->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(syntax->rhs)->val);
  ASSERT_EQ(PSX_HIR_SUB, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_ADD, psx_hir_node_kind(addition));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_additive_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_additive_typed_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "double _Complex additive_probe(int condition) { "
      "  unsigned char uc = 1; short s = 2; "
      "  unsigned int ui = 3; long sl = 4; "
      "  float _Complex cf = 1.0f; double d = 2.0; "
      "  int values[3] = {1,2,3}; int *p = values; "
      "  int *advanced = p + 1; "
      "  long difference = advanced - p; "
      "  int promoted = uc + s; "
      "  long ranked = ui + sl; "
      "  double _Complex complex_sum = cf + d; "
      "  double comma_value = (promoted, d); "
      "  long conditional = condition ? ui : sl; "
      "  return complex_sum + comma_value + difference "
      "      + ranked + conditional + *advanced; "
      "}"));

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(hir != NULL);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  const psx_hir_node_t *promoted =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "promoted", 0);
  const psx_hir_node_t *ranked =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "ranked", 0);
  const psx_hir_node_t *complex_sum =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "complex_sum", 0);
  const psx_hir_node_t *advanced =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "advanced", 0);
  const psx_hir_node_t *difference =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "difference", 0);
  ASSERT_TRUE(promoted != NULL);
  ASSERT_TRUE(ranked != NULL);
  ASSERT_TRUE(complex_sum != NULL);
  ASSERT_TRUE(advanced != NULL);
  ASSERT_TRUE(difference != NULL);

  psx_type_shape_t promoted_shape =
      test_hir_type_shape(test_suite_session, promoted);
  psx_type_shape_t ranked_shape =
      test_hir_type_shape(test_suite_session, ranked);
  psx_type_shape_t complex_shape =
      test_hir_type_shape(test_suite_session, complex_sum);
  ASSERT_EQ(PSX_TYPE_INTEGER, promoted_shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_INT,
            promoted_shape.integer_kind);
  ASSERT_TRUE(!promoted_shape.is_unsigned);
  ASSERT_EQ(PSX_TYPE_INTEGER, ranked_shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG,
            ranked_shape.integer_kind);
  ASSERT_TRUE(!ranked_shape.is_unsigned);
  ASSERT_EQ(PSX_TYPE_COMPLEX, complex_shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            complex_shape.floating_kind);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_hir_type_shape(test_suite_session, advanced).kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG,
            test_hir_type_shape(test_suite_session, difference).integer_kind);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_ADD, 0) != NULL);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_SUB, 0) != NULL);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_COMMA, 0) != NULL);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_TERNARY, 0) != NULL);
}

static void test_subscript_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_subscript_typed_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int subscript_probe(int rows, int columns, "
      "                    int matrix[rows][columns]) { "
      "  int fixed[2][3] = {{1,2,3},{4,5,6}}; "
      "  int *row = fixed[1]; "
      "  int fixed_value = fixed[1][2]; "
      "  int variable_value = matrix[rows-1][columns-1]; "
      "  return fixed_value + variable_value + row[0]; "
      "}"));

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(hir != NULL);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  const psx_hir_node_t *fixed =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "fixed", 0);
  const psx_hir_node_t *row =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "row", 0);
  const psx_hir_node_t *function =
      find_test_named_hir_node(
          hir, PSX_HIR_FUNCTION,
          "subscript_probe", 0);
  const psx_hir_node_t *matrix =
      find_test_hir_function_parameter(
          hir, function, "matrix");
  ASSERT_TRUE(fixed != NULL);
  ASSERT_TRUE(row != NULL);
  ASSERT_TRUE(function != NULL);
  ASSERT_TRUE(matrix != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_hir_type_shape(test_suite_session, row).kind);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_hir_type_shape(test_suite_session, matrix).kind);
  ASSERT_TRUE(psx_hir_node_vla_dimension_count(matrix) >= 1);

  size_t subscript_count = 0;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session));
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(
            hir, (psx_hir_node_id_t)i);
    if (!node ||
        psx_hir_node_kind(node) != PSX_HIR_SUBSCRIPT)
      continue;
    subscript_count++;
    ASSERT_TRUE(
        psx_semantic_type_table_qual_type_is_valid(
            types, psx_hir_node_qual_type(node)));
  }
  ASSERT_TRUE(subscript_count >= 5);
}

static void test_unary_deref_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_unary_deref_typed_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int dereference_probe(int *p, int **q) { "
      "  int value = **q; "
      "  *p = 7; "
      "  int *subscript_address = &p[0]; "
      "  return value + *subscript_address; "
      "}"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);
  ASSERT_TRUE(hir != NULL);

  int dereference_count = 0;
  int dereference_assignment_count = 0;
  int integer_pointer_address_count = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!node) continue;
    if (psx_hir_node_kind(node) == PSX_HIR_DEREF) {
      psx_type_shape_t shape = {0};
      ASSERT_TRUE(psx_semantic_type_table_describe(
          types, psx_hir_node_qual_type(node).type_id,
          &shape));
      ASSERT_TRUE(shape.kind == PSX_TYPE_INTEGER ||
                  shape.kind == PSX_TYPE_POINTER);
      dereference_count++;
    }
    if (psx_hir_node_kind(node) == PSX_HIR_ASSIGN) {
      for (size_t child = 0;
           child < psx_hir_node_child_count(node); child++) {
        if (psx_hir_node_child_edge_at(node, child) !=
            PSX_HIR_EDGE_LHS)
          continue;
        const psx_hir_node_t *lhs = psx_hir_module_lookup(
            hir, psx_hir_node_child_at(node, child));
        if (lhs && psx_hir_node_kind(lhs) == PSX_HIR_DEREF)
          dereference_assignment_count++;
      }
    }
    if (psx_hir_node_kind(node) == PSX_HIR_ADDRESS) {
      psx_qual_type_t pointee = psx_semantic_type_table_base(
          types, psx_hir_node_qual_type(node).type_id);
      psx_type_shape_t shape = {0};
      ASSERT_TRUE(psx_semantic_type_table_describe(
          types, pointee.type_id, &shape));
      if (shape.kind == PSX_TYPE_INTEGER) {
        ASSERT_EQ(4, psx_type_layout_sizeof(
                         types, record_layouts,
                         pointee.type_id, data_layout));
        integer_pointer_address_count++;
      }
    }
  }
  ASSERT_TRUE(dereference_count >= 4);
  ASSERT_EQ(1, dereference_assignment_count);
  ASSERT_TRUE(integer_pointer_address_count >= 1);
}

static void test_unary_operator_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_unary_operator_typed_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __typed_hir_neg_char(signed char x) { return -x; } "
      "int __typed_hir_neg_i(int x) { return -x; } "
      "double __typed_hir_neg_d(double x) { return -x; } "
      "double _Complex __typed_hir_neg_z(double _Complex x) { "
      "return -x; } "
      "double __typed_hir_real(double _Complex x) { "
      "return __real__ x; } "
      "int __typed_hir_imag_i(int x) { return __imag__ x; }"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(hir != NULL);
  int found_integer_negate = 0;
  int found_float_negate = 0;
  int found_complex_negate = 0;
  int found_real = 0;
  int found_imaginary_integer = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *hir_node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!hir_node) continue;
    psx_hir_node_kind_t kind = psx_hir_node_kind(hir_node);
    if (kind != PSX_HIR_NEGATE && kind != PSX_HIR_CREAL &&
        kind != PSX_HIR_CIMAG)
      continue;
    psx_type_shape_t shape = {0};
    ASSERT_TRUE(psx_semantic_type_table_describe(
        types, psx_hir_node_qual_type(hir_node).type_id,
        &shape));
    if (kind == PSX_HIR_NEGATE) {
      if (shape.kind == PSX_TYPE_INTEGER) {
        ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);
        found_integer_negate++;
      } else if (shape.kind == PSX_TYPE_FLOAT) {
        found_float_negate++;
      } else if (shape.kind == PSX_TYPE_COMPLEX) {
        found_complex_negate++;
      }
    } else if (kind == PSX_HIR_CREAL) {
      ASSERT_EQ(PSX_TYPE_FLOAT, shape.kind);
      found_real++;
    } else if (kind == PSX_HIR_CIMAG) {
      ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
      ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);
      found_imaginary_integer++;
    }
  }
  ASSERT_EQ(2, found_integer_negate);
  ASSERT_EQ(1, found_float_negate);
  ASSERT_EQ(1, found_complex_negate);
  ASSERT_EQ(1, found_real);
  ASSERT_EQ(1, found_imaginary_integer);
}

static void test_generic_selection_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_generic_selection_typed_hir_boundary...\n");
  psx_qual_type_t integer_type =
      ps_ctx_intern_integer_qual_type_in(
          test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0);
  psx_qual_type_t floating_type =
      ps_ctx_intern_floating_qual_type_in(
          test_semantic_context(test_suite_session), PSX_FLOATING_KIND_DOUBLE, 0);
  psx_qual_type_t association_types[2] = {
      integer_type,
      {PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  unsigned char is_default[2] = {0, 1};
  psx_generic_selection_resolution_t resolution;
  psx_resolve_generic_selection_qual_types_in(
      integer_type, association_types, is_default, 2, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_OK, resolution.status);
  ASSERT_EQ(0, resolution.selected_index);
  psx_type_shape_t selected_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      association_types[resolution.selected_index].type_id,
      &selected_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, selected_shape.kind);

  is_default[0] = 1;
  psx_resolve_generic_selection_qual_types_in(
      integer_type, association_types, is_default, 2, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_DEFAULT,
            resolution.status);
  ASSERT_EQ(1, resolution.conflict_index);

  is_default[0] = 0;
  is_default[1] = 0;
  association_types[1] = integer_type;
  psx_resolve_generic_selection_qual_types_in(
      integer_type, association_types, is_default, 2, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE,
            resolution.status);
  ASSERT_EQ(1, resolution.conflict_index);

  psx_resolve_generic_selection_qual_types_in(
      floating_type, association_types, is_default, 1, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_NO_MATCH,
            resolution.status);

  association_types[0] = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  psx_resolve_generic_selection_qual_types_in(
      integer_type, association_types, is_default, 1, &resolution);
  ASSERT_EQ(PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED,
            resolution.status);
  ASSERT_EQ(0, resolution.conflict_index);

  reset_test_locals(test_suite_session);
  lvar_t *value = register_test_storage_fixture(test_suite_session,
      (char *)"x", 1, 4, 4, 0);
  set_test_storage_fixture_type(test_suite_session,
      value, ps_ctx_intern_integer_qual_type_in(
                 test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0));

  node_t *raw = parse_expr_input_with_existing_locals(test_suite_session,
      "_Generic(x, int: x + 1, default: x + 2)");
  ASSERT_EQ(ND_GENERIC_SELECTION, raw->kind);
  node_generic_selection_t *selection =
      (node_generic_selection_t *)raw;
  ASSERT_EQ(2, selection->association_count);
  ASSERT_EQ(ND_IDENTIFIER, selection->control->kind);
  ASSERT_TRUE(selection->associations[0].type_name.syntax != NULL);
  ASSERT_EQ(ND_ADD, selection->associations[0].expression->kind);
  ASSERT_TRUE(selection->associations[1].is_default);

  psx_frontend_expression_hir_t expression =
      resolve_test_expression_hir(test_suite_session, raw);
  const psx_hir_node_t *selected =
      test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_ADD, psx_hir_node_kind(selected));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, selected).kind);
  ASSERT_EQ(2, psx_hir_node_child_count(selected));
  const psx_hir_node_t *selected_rhs = psx_hir_module_lookup(
      expression.module, psx_hir_node_child_at(selected, 1));
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(selected_rhs));
  ASSERT_EQ(1, psx_hir_node_integer_value(selected_rhs));
  psx_frontend_expression_hir_dispose(&expression);

  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  size_t hir_checkpoint = psx_hir_module_node_count(hir);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __typed_hir_generic(int x) { "
      "return _Generic(x, int: x + 1, default: x + 200); }"));
  int found_selected_literal = 0;
  int found_unselected_literal = 0;
  for (size_t i = hir_checkpoint + 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *hir_node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!hir_node || psx_hir_node_kind(hir_node) != PSX_HIR_NUMBER)
      continue;
    long long value = psx_hir_node_integer_value(hir_node);
    if (value == 1) found_selected_literal = 1;
    if (value == 200) found_unselected_literal = 1;
  }
  ASSERT_TRUE(found_selected_literal);
  ASSERT_TRUE(!found_unselected_literal);
}

static void test_sizeof_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_sizeof_typed_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "unsigned long sizeof_probe(int n, int i) { "
      "  int x = 0; int fixed[3] = {0}; int vla[n][n]; "
      "  return sizeof(x) + sizeof(int[3]) "
      "      + sizeof(int[n]) + sizeof(&fixed) + sizeof(vla[i]) "
      "      + _Alignof(int[3]) + _Alignof(void *); "
      "}"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);
  ASSERT_TRUE(hir != NULL);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));

  const psx_scope_declaration_t *fixed_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "fixed",
          PSX_DECL_LOCAL_OBJECT, 0);
  const psx_scope_declaration_t *vla_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "vla",
          PSX_DECL_LOCAL_OBJECT, 0);
  ASSERT_TRUE(fixed_declaration != NULL);
  ASSERT_TRUE(vla_declaration != NULL);
  lvar_t *fixed = (lvar_t *)fixed_declaration->payload;
  lvar_t *vla = (lvar_t *)vla_declaration->payload;
  ASSERT_TRUE(fixed != NULL);
  ASSERT_TRUE(vla != NULL);
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(fixed), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(3, shape.array_len);
  ASSERT_EQ(12, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_lvar_decl_type_id(fixed), data_layout));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(vla), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_TRUE(shape.is_vla);
  ASSERT_TRUE(ps_lvar_is_vla(vla));

  int found_size_literal = 0;
  int found_pointer_size_literal = 0;
  int found_alignment_literal = 0;
  int found_vla_size_query = 0;
  int found_unsigned_long_arithmetic = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *hir_node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!hir_node) continue;
    if (psx_hir_node_kind(hir_node) == PSX_HIR_NUMBER) {
      long long value = psx_hir_node_integer_value(hir_node);
      if (value == 12) found_size_literal = 1;
      if (value == ag_data_layout_pointer_size(data_layout))
        found_pointer_size_literal = 1;
      if (value == 4) found_alignment_literal = 1;
    }
    if (psx_hir_node_kind(hir_node) == PSX_HIR_ADD) {
      psx_type_shape_t add_shape = {0};
      ASSERT_TRUE(psx_semantic_type_table_describe(
          types, psx_hir_node_qual_type(hir_node).type_id,
          &add_shape));
      if (add_shape.kind == PSX_TYPE_INTEGER &&
          add_shape.integer_kind == PSX_INTEGER_KIND_LONG &&
          add_shape.is_unsigned)
        found_unsigned_long_arithmetic = 1;
    }
    if (!hir_node || psx_hir_node_kind(hir_node) != PSX_HIR_COMMA ||
        psx_hir_node_child_count(hir_node) != 2)
      continue;
    const psx_hir_node_t *prefix = psx_hir_module_lookup(
        hir, psx_hir_node_child_at(hir_node, 0));
    const psx_hir_node_t *runtime_size = psx_hir_module_lookup(
        hir, psx_hir_node_child_at(hir_node, 1));
    if (!prefix || !runtime_size ||
        psx_hir_node_kind(prefix) != PSX_HIR_LOCAL ||
        psx_hir_node_kind(runtime_size) != PSX_HIR_LOCAL ||
        psx_hir_node_object_size(runtime_size) !=
            PSX_VLA_RUNTIME_SLOT_SIZE)
      continue;
    ASSERT_EQ(PSX_HIR_EDGE_LHS,
              psx_hir_node_child_edge_at(hir_node, 0));
    ASSERT_EQ(PSX_HIR_EDGE_RHS,
              psx_hir_node_child_edge_at(hir_node, 1));
    ASSERT_EQ(4, psx_hir_node_object_size(prefix));
    ASSERT_EQ(PSX_VLA_RUNTIME_SLOT_SIZE,
              psx_hir_node_object_align(runtime_size));
    ASSERT_TRUE(psx_hir_node_storage_offset(runtime_size) != 0);
    ASSERT_TRUE(psx_hir_node_storage_offset(prefix) !=
                psx_hir_node_storage_offset(runtime_size));
    psx_type_shape_t vla_size_shape = test_hir_type_shape(test_suite_session, hir_node);
    ASSERT_EQ(PSX_TYPE_INTEGER, vla_size_shape.kind);
    ASSERT_EQ(PSX_INTEGER_KIND_LONG, vla_size_shape.integer_kind);
    ASSERT_TRUE(vla_size_shape.is_unsigned);
    found_vla_size_query = 1;
  }
  ASSERT_TRUE(found_size_literal);
  ASSERT_TRUE(found_pointer_size_literal);
  ASSERT_TRUE(found_alignment_literal);
  ASSERT_TRUE(found_vla_size_query);
  ASSERT_TRUE(found_unsigned_long_arithmetic);

  psx_hir_node_id_t root_id =
      psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));
  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = types,
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = record_layouts,
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(
              test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MUL) >= 2);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);

  expect_parse_fail(test_suite_session,
      "unsigned long invalid_sizeof(void) { "
      "return sizeof(int[-1]); }");
  expect_parse_fail_with_message(test_suite_session,
      "unsigned long zero_length_sizeof(void) { "
      "return sizeof(int[0]); }",
      "GNU 拡張は使用できません");
}

static void test_expression_typed_hir_type_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expression_typed_hir_type_boundary...\n");
  reset_test_locals(test_suite_session);
  node_t *ternary =
      parse_expr_input_with_existing_locals(test_suite_session, "(1, 2) ? 3 : 4");
  ASSERT_EQ(ND_TERNARY, ternary->kind);
  ASSERT_EQ(ND_COMMA, ternary->lhs->kind);

  psx_frontend_expression_hir_t expression =
      resolve_test_expression_hir(test_suite_session, ternary);
  const psx_hir_node_t *typed_ternary =
      test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_TERNARY, psx_hir_node_kind(typed_ternary));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_hir_type_shape(test_suite_session, typed_ternary).kind);
  const psx_hir_node_t *typed_comma = psx_hir_module_lookup(
      expression.module, psx_hir_node_child_at(typed_ternary, 0));
  ASSERT_EQ(PSX_HIR_COMMA, psx_hir_node_kind(typed_comma));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, typed_comma).kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_function_call_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_function_call_typed_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "double call_target(int value) { return value; } "
      "double call_probe(double (*fp)(int)) { "
      "  double (*saved)(int) = call_target; "
      "  return call_target(3) + fp(4) + saved(5); "
      "} "
      "int implicit_probe(void) { "
      "  return implicit_call_boundary(6); "
      "}"));

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  ASSERT_TRUE(hir != NULL);
  ASSERT_TRUE(types != NULL);

  int direct_call_count = 0;
  int indirect_call_count = 0;
  int implicit_call_count = 0;
  int function_reference_count = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(
            hir, (psx_hir_node_id_t)i);
    if (!node) continue;
    if (psx_hir_node_kind(node) ==
        PSX_HIR_FUNCTION_REF) {
      function_reference_count++;
      psx_type_shape_t reference_shape = {0};
      ASSERT_TRUE(psx_semantic_type_table_describe(
          types, psx_hir_node_qual_type(node).type_id,
          &reference_shape));
      ASSERT_EQ(PSX_TYPE_POINTER, reference_shape.kind);
      psx_qual_type_t referenced_function =
          psx_semantic_type_table_base(
              types, psx_hir_node_qual_type(node).type_id);
      ASSERT_TRUE(psx_semantic_type_table_describe(
          types, referenced_function.type_id,
          &reference_shape));
      ASSERT_EQ(PSX_TYPE_FUNCTION, reference_shape.kind);
      continue;
    }
    if (psx_hir_node_kind(node) != PSX_HIR_CALL)
      continue;

    psx_type_shape_t result_shape = {0};
    psx_type_shape_t callable_shape = {0};
    ASSERT_TRUE(psx_semantic_type_table_describe(
        types, psx_hir_node_qual_type(node).type_id,
        &result_shape));
    ASSERT_TRUE(psx_semantic_type_table_describe(
        types,
        psx_hir_node_attached_qual_type(node).type_id,
        &callable_shape));
    ASSERT_EQ(PSX_TYPE_FUNCTION, callable_shape.kind);

    if (psx_hir_node_is_implicit_call(node)) {
      implicit_call_count++;
      ASSERT_EQ(0, callable_shape.parameter_count);
      ASSERT_TRUE(!callable_shape.has_function_prototype);
      ASSERT_EQ(PSX_TYPE_INTEGER, result_shape.kind);
      ASSERT_EQ(PSX_INTEGER_KIND_INT,
                result_shape.integer_kind);
      continue;
    }

    ASSERT_EQ(1, callable_shape.parameter_count);
    ASSERT_TRUE(callable_shape.has_function_prototype);
    ASSERT_EQ(PSX_TYPE_FLOAT, result_shape.kind);
    ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
              result_shape.floating_kind);
    if (psx_hir_node_child_count(node) > 0 &&
        psx_hir_node_child_edge_at(node, 0) ==
            PSX_HIR_EDGE_CALLEE) {
      indirect_call_count++;
      const psx_hir_node_t *callee =
          psx_hir_module_lookup(
              hir, psx_hir_node_child_at(node, 0));
      ASSERT_TRUE(callee != NULL);
      ASSERT_EQ(PSX_HIR_LOCAL,
                psx_hir_node_kind(callee));
    } else {
      direct_call_count++;
      ASSERT_EQ(1, psx_hir_node_child_count(node));
      ASSERT_EQ(PSX_HIR_EDGE_ARGUMENT,
                psx_hir_node_child_edge_at(node, 0));
    }
  }

  ASSERT_EQ(1, direct_call_count);
  ASSERT_EQ(2, indirect_call_count);
  ASSERT_EQ(1, implicit_call_count);
  ASSERT_TRUE(function_reference_count >= 1);

  const psx_scope_declaration_t *target_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "call_target",
          PSX_DECL_FUNCTION, 0);
  const psx_scope_declaration_t *saved_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "saved",
          PSX_DECL_LOCAL_OBJECT, 0);
  ASSERT_TRUE(target_declaration != NULL);
  ASSERT_TRUE(saved_declaration != NULL);
  lvar_t *saved = (lvar_t *)saved_declaration->payload;
  ASSERT_TRUE(saved != NULL);
  psx_type_shape_t saved_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(saved), &saved_shape));
  ASSERT_EQ(PSX_TYPE_POINTER, saved_shape.kind);
  psx_qual_type_t saved_function =
      psx_semantic_type_table_base(
          types, ps_lvar_decl_type_id(saved));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, saved_function.type_id, &saved_shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, saved_shape.kind);
  ASSERT_EQ(1, saved_shape.parameter_count);
  ASSERT_TRUE(saved_shape.has_function_prototype);

  expect_parse_fail(test_suite_session,
      "int main(void) { int value = 0; return value(); }");
  expect_parse_fail(test_suite_session,
      "double target(int value) { return value; } "
      "int main(void) { return (int)target(); }");
  expect_parse_fail(test_suite_session,
      "double target(int value) { return value; } "
      "int main(void) { "
      "  double (**pointer)(int) = 0; "
      "  return (int)pointer(1); "
      "}");
  expect_parse_fail(test_suite_session,
      "double target(int value) { return value; } "
      "int main(void) { "
      "  double (*array[2])(int) = {target, target}; "
      "  return (int)array(1); "
      "}");
}
static void test_function_prototype_type_identity_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_function_prototype_type_identity_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __unprototyped(); int __void_prototype(void); "
      "int __prototype_use(void) { "
      "return __unprototyped(1) + __void_prototype(); }"));

  static char unprototyped_name[] = "__unprototyped";
  static char void_prototype_name[] = "__void_prototype";
  psx_qual_type_t unprototyped_type =
      ps_ctx_get_function_qual_type_in(
          test_semantic_context(test_suite_session), unprototyped_name,
          (int)sizeof(unprototyped_name) - 1);
  psx_qual_type_t void_prototype_type =
      ps_ctx_get_function_qual_type_in(
          test_semantic_context(test_suite_session), void_prototype_name,
          (int)sizeof(void_prototype_name) - 1);
  ASSERT_TRUE(unprototyped_type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(void_prototype_type.type_id != PSX_TYPE_ID_INVALID);
  psx_type_shape_t unprototyped_shape =
      test_qual_type_shape(test_suite_session, unprototyped_type);
  psx_type_shape_t void_prototype_shape =
      test_qual_type_shape(test_suite_session, void_prototype_type);
  ASSERT_EQ(PSX_TYPE_FUNCTION, unprototyped_shape.kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION, void_prototype_shape.kind);
  ASSERT_TRUE(!unprototyped_shape.has_function_prototype);
  ASSERT_TRUE(void_prototype_shape.has_function_prototype);
  ASSERT_EQ(0, unprototyped_shape.parameter_count);
  ASSERT_EQ(0, void_prototype_shape.parameter_count);
  ASSERT_TRUE(unprototyped_type.type_id != void_prototype_type.type_id);

  char signature[16];
  ASSERT_EQ(5, ps_ctx_format_function_signature_in(
                   test_semantic_context(test_suite_session), void_prototype_name,
                   (int)sizeof(void_prototype_name) - 1,
                   signature, sizeof(signature)));
  ASSERT_TRUE(strcmp(signature, "i32()") == 0);

  psx_call_types_resolution_t resolution;
  psx_resolve_call_qual_types_in(
      test_semantic_context(test_suite_session), unprototyped_type, 1, &resolution);
  ASSERT_EQ(PSX_CALL_TYPES_OK, resolution.status);
  psx_resolve_call_qual_types_in(
      test_semantic_context(test_suite_session), void_prototype_type, 1, &resolution);
  ASSERT_EQ(PSX_CALL_TYPES_ARGUMENT_COUNT_MISMATCH,
            resolution.status);
  psx_resolve_call_qual_types_in(
      test_semantic_context(test_suite_session), void_prototype_type, 0, &resolution);
  ASSERT_EQ(PSX_CALL_TYPES_OK, resolution.status);
}

static void test_cast_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_cast_typed_hir_boundary...\n");
  reset_test_locals(test_suite_session);
  preregister_test_locals(test_suite_session);
  node_t *node =
      parse_expr_input_with_existing_locals(test_suite_session, "(int)(unsigned long)a");
  ASSERT_EQ(ND_SOURCE_CAST, node->kind);
  node_source_cast_t *outer = (node_source_cast_t *)node;
  ASSERT_TRUE(outer->type_name.syntax != NULL);
  ASSERT_EQ(ND_SOURCE_CAST, node->lhs->kind);
  node_source_cast_t *inner = (node_source_cast_t *)node->lhs;
  ASSERT_TRUE(inner->type_name.syntax != NULL);

  psx_frontend_expression_hir_t expression =
      resolve_test_expression_hir(test_suite_session, node);
  const psx_hir_node_t *typed_outer =
      test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(typed_outer));
  psx_type_shape_t outer_shape = test_hir_type_shape(test_suite_session, typed_outer);
  ASSERT_EQ(PSX_TYPE_INTEGER, outer_shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_INT, outer_shape.integer_kind);
  ASSERT_EQ(1, psx_hir_node_child_count(typed_outer));
  const psx_hir_node_t *typed_inner = psx_hir_module_lookup(
      expression.module, psx_hir_node_child_at(typed_outer, 0));
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(typed_inner));
  psx_type_shape_t inner_shape = test_hir_type_shape(test_suite_session, typed_inner);
  ASSERT_EQ(PSX_TYPE_INTEGER, inner_shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, inner_shape.integer_kind);
  ASSERT_TRUE(inner_shape.is_unsigned);
  ASSERT_EQ(ND_SOURCE_CAST, node->kind);
  ASSERT_EQ(ND_SOURCE_CAST, node->lhs->kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_aggregate_cast_semantic_lowering_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_aggregate_cast_semantic_lowering_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { struct S { int x; int y; }; "
      "return ((struct S)7).x; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *temp = find_test_named_hir_node(
      hir, PSX_HIR_LOCAL, "__aggregate_cast_0", 0);
  ASSERT_TRUE(temp != NULL);
  const psx_hir_node_t *member =
      find_test_hir_node_kind(hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(member != NULL);
  const psx_hir_node_t *aggregate_value = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(member, 0));
  ASSERT_TRUE(aggregate_value != NULL);
  psx_qual_type_t temporary_qual_type =
      psx_hir_node_qual_type(aggregate_value);
  psx_type_shape_t temporary_shape =
      test_qual_type_shape(test_suite_session, temporary_qual_type);
  ASSERT_EQ(PSX_TYPE_STRUCT, temporary_shape.kind);
  ASSERT_TRUE(temporary_shape.record_id != PSX_RECORD_ID_INVALID);
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      test_semantic_context(test_suite_session), temporary_shape.record_id);
  ASSERT_TRUE(record != NULL);
  ASSERT_EQ(2, record->member_count);
  ASSERT_EQ(1, record->members[0].len);
  ASSERT_TRUE(strncmp(record->members[0].name, "x", 1) == 0);
  ASSERT_TRUE(!psx_hir_node_member_from_pointer(member));
  ASSERT_EQ(0, psx_hir_node_member_offset(member));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, member).kind);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __typed_hir_aggregate_address(void) { "
      "struct S { int x; }; return (&((struct S)7))->x; }"));
  hir =
      ag_compilation_session_hir_module(test_suite_session);
  temp = find_test_named_hir_node(
      hir, PSX_HIR_LOCAL, "__aggregate_cast_0", 0);
  ASSERT_TRUE(temp != NULL);
  member = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(member != NULL);
  ASSERT_TRUE(psx_hir_node_member_from_pointer(member));
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_ADDRESS, 0) != NULL);
}

static void test_implicit_conversion_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_implicit_conversion_hir_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "double id(double x) { return x; } "
      "double retconv(int x) { return x; } "
      "double same(double x) { double y=id(x); return y; } "
      "int main(void) { int x=3; double d=x; d=x; "
      "return (int)id(x); }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);

  const psx_hir_node_t *retconv_return =
      find_test_hir_node_kind(hir, PSX_HIR_RETURN, 1);
  ASSERT_TRUE(retconv_return != NULL);
  const psx_hir_node_t *retconv_value = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(retconv_return, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(retconv_value));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_hir_type_shape(test_suite_session, retconv_value).kind);

  const psx_hir_node_t *same_call =
      find_test_hir_node_kind(hir, PSX_HIR_CALL, 0);
  ASSERT_TRUE(same_call != NULL);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            test_hir_attached_type_shape(test_suite_session, same_call).kind);
  ASSERT_EQ(PSX_TYPE_FLOAT, test_hir_type_shape(test_suite_session, same_call).kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            test_hir_type_shape(test_suite_session, same_call).floating_kind);

  int declaration_initializer_count = 0;
  int source_assignment_count = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (psx_hir_node_is_declaration_initializer(node))
      declaration_initializer_count++;
    if (psx_hir_node_is_source_assignment(node))
      source_assignment_count++;
  }
  ASSERT_TRUE(declaration_initializer_count >= 3);
  ASSERT_TRUE(source_assignment_count >= 1);

  const psx_hir_node_t *cast =
      find_test_hir_node_kind(hir, PSX_HIR_CAST, 0);
  ASSERT_TRUE(cast != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, cast).kind);
  const psx_hir_node_t *call = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(cast, 0));
  ASSERT_EQ(PSX_HIR_CALL, psx_hir_node_kind(call));
  ASSERT_EQ(1, psx_hir_node_child_count(call));
  const psx_hir_node_t *argument = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(call, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(argument));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, argument).kind);
}

static void test_compound_assignment_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_compound_assignment_typed_hir_boundary...\n");
  reset_test_locals(test_suite_session);
  lvar_t *value = register_test_storage_fixture(test_suite_session, (char *)"a", 1, 4, 4, 0);
  set_test_storage_fixture_type(test_suite_session,
      value, ps_ctx_intern_integer_qual_type_in(
                 test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0));
  node_t *node = parse_expr_input_with_existing_locals(test_suite_session, "a += 2");
  ASSERT_EQ(ND_COMPOUND_ASSIGN, node->kind);
  ASSERT_EQ(TK_PLUSEQ, node->source_op);
  ASSERT_EQ(ND_NUM, node->rhs->kind);

  psx_frontend_expression_hir_t assignment_expression =
      resolve_test_expression_hir(test_suite_session, node);
  const psx_hir_node_t *assignment_hir =
      test_expression_hir_root(&assignment_expression);
  ASSERT_EQ(PSX_HIR_COMPOUND_ASSIGN,
            psx_hir_node_kind(assignment_hir));
  ASSERT_EQ(PSX_HIR_COMPOUND_ADD,
            psx_hir_node_compound_operator(assignment_hir));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_hir_type_shape(test_suite_session, assignment_hir).kind);
  ASSERT_EQ(2, psx_hir_node_child_count(assignment_hir));
  ASSERT_EQ(PSX_HIR_LOCAL,
            psx_hir_node_kind(psx_hir_module_lookup(
                assignment_expression.module,
                psx_hir_node_child_at(assignment_hir, 0))));
  ASSERT_EQ(PSX_HIR_NUMBER,
            psx_hir_node_kind(psx_hir_module_lookup(
                assignment_expression.module,
                psx_hir_node_child_at(assignment_hir, 1))));
  ASSERT_EQ(ND_COMPOUND_ASSIGN, node->kind);
  ASSERT_EQ(ND_NUM, node->rhs->kind);
  psx_frontend_expression_hir_dispose(&assignment_expression);

  reset_test_locals(test_suite_session);
  lvar_t *pointer = register_test_storage_fixture(test_suite_session, (char *)"p", 1, 8, 4, 0);
  set_test_storage_fixture_type(test_suite_session,
      pointer,
      ps_ctx_intern_pointer_to_qual_type_in(
          test_semantic_context(test_suite_session),
          ps_ctx_intern_integer_qual_type_in(
              test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0)));
  node = parse_expr_input_with_existing_locals(test_suite_session, "*p += 2");
  ASSERT_EQ(ND_COMPOUND_ASSIGN, node->kind);
  psx_frontend_expression_hir_t deref_assignment_expression =
      resolve_test_expression_hir(test_suite_session, node);
  const psx_hir_node_t *deref_assignment_hir =
      test_expression_hir_root(&deref_assignment_expression);
  ASSERT_EQ(PSX_HIR_COMPOUND_ASSIGN,
            psx_hir_node_kind(deref_assignment_hir));
  ASSERT_EQ(PSX_HIR_COMPOUND_ADD,
            psx_hir_node_compound_operator(deref_assignment_hir));
  ASSERT_EQ(PSX_HIR_DEREF,
            psx_hir_node_kind(psx_hir_module_lookup(
                deref_assignment_expression.module,
                psx_hir_node_child_at(deref_assignment_hir, 0))));
  ASSERT_EQ(ND_COMPOUND_ASSIGN, node->kind);
  ASSERT_EQ(ND_UNARY_DEREF, node->lhs->kind);
  psx_frontend_expression_hir_dispose(
      &deref_assignment_expression);

  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  size_t hir_checkpoint = psx_hir_module_node_count(hir);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __typed_hir_subscript_compound(int *values, int index) { "
      "values[index] += 2; return values[index]; }"));
  int found_subscript_compound = 0;
  for (size_t i = hir_checkpoint + 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *hir_node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (hir_node &&
        psx_hir_node_kind(hir_node) == PSX_HIR_COMPOUND_ASSIGN &&
        psx_hir_node_compound_operator(hir_node) ==
            PSX_HIR_COMPOUND_ADD) {
      found_subscript_compound = 1;
      break;
    }
  }
  ASSERT_TRUE(found_subscript_compound);

  hir_checkpoint = psx_hir_module_node_count(hir);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct __TypedHirMemberCompound { int value; }; "
      "int __typed_hir_member_compound("
      "struct __TypedHirMemberCompound *object) { "
      "object->value += 3; return object->value; }"));
  int found_member_compound = 0;
  for (size_t i = hir_checkpoint + 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *hir_node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!hir_node ||
        psx_hir_node_kind(hir_node) != PSX_HIR_COMPOUND_ASSIGN ||
        psx_hir_node_compound_operator(hir_node) !=
            PSX_HIR_COMPOUND_ADD)
      continue;
    for (size_t child = 0;
         child < psx_hir_node_child_count(hir_node); child++) {
      if (psx_hir_node_child_edge_at(hir_node, child) !=
          PSX_HIR_EDGE_LHS)
        continue;
      const psx_hir_node_t *target = psx_hir_module_lookup(
          hir, psx_hir_node_child_at(hir_node, child));
      if (target &&
          psx_hir_node_kind(target) == PSX_HIR_MEMBER_ACCESS) {
        found_member_compound = 1;
        break;
      }
    }
  }
  ASSERT_TRUE(found_member_compound);
}

static void test_translation_unit_frontend_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_translation_unit_frontend_boundary...\n");
  const char *source =
      "int __frontend_boundary(int input) { "
      "int x=input; x += 2; return x; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item.kind);
  ASSERT_TRUE(!test_has_function_type_in(test_semantic_context(test_suite_session),
                  (char *)"__frontend_boundary", 19));
  ASSERT_EQ(1, item.value.function_header.declarator
                   .function_suffixes[0].parameters->count);
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  psx_parsed_function_definition_t *syntax_function =
      &item.value.function_header;
  ASSERT_TRUE(!test_has_function_type_in(test_semantic_context(test_suite_session),
                  (char *)"__frontend_boundary", 19));
  ASSERT_TRUE(find_test_local_var_in(
                  test_local_registry(test_suite_session), (char *)"input", 5) == NULL);
  ASSERT_TRUE(find_test_local_var_in(
                  test_local_registry(test_suite_session), (char *)"x", 1) == NULL);
  node_block_t *parsed_body = as_block(syntax_function->body);
  ASSERT_EQ(ND_LOCAL_DECLARATION, parsed_body->body[0]->kind);
  ASSERT_EQ(ND_COMPOUND_ASSIGN, parsed_body->body[1]->kind);
  ASSERT_EQ(TK_PLUSEQ, parsed_body->body[1]->source_op);
  node_t *raw_compound_assignment = parsed_body->body[1];
  ASSERT_EQ(ND_NUM, raw_compound_assignment->rhs->kind);
  node_t *raw_literal = raw_compound_assignment->rhs;
  psx_hir_node_id_t hir_root = PSX_HIR_NODE_ID_INVALID;
  ASSERT_TRUE(psx_frontend_resolve_parsed_function_to_hir_in_session(
      test_suite_session, syntax_function,
      (token_t *)syntax_function->declarator.identifier, &hir_root));
  ASSERT_TRUE(hir_root != PSX_HIR_NODE_ID_INVALID);
  ASSERT_TRUE(test_has_function_type_in(test_semantic_context(test_suite_session),
                  (char *)"__frontend_boundary", 19));
  ASSERT_TRUE(find_test_local_var_in(
                  test_local_registry(test_suite_session), (char *)"input", 5) != NULL);
  ASSERT_TRUE(
      find_test_local_var_in(
          test_local_registry(test_suite_session), (char *)"x", 1) != NULL);
  ASSERT_EQ(ND_COMPOUND_ASSIGN, raw_compound_assignment->kind);
  ASSERT_EQ(TK_PLUSEQ, raw_compound_assignment->source_op);
  ASSERT_TRUE(parsed_body->body[1] == raw_compound_assignment);
  ASSERT_TRUE(raw_compound_assignment->rhs == raw_literal);
  psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *hir_function =
      psx_hir_module_lookup(hir, hir_root);
  ASSERT_TRUE(hir_function != NULL);
  ASSERT_EQ(PSX_HIR_FUNCTION, psx_hir_node_kind(hir_function));
  int found_typed_compound_assignment = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (node &&
        psx_hir_node_kind(node) == PSX_HIR_COMPOUND_ASSIGN &&
        psx_hir_node_compound_operator(node) ==
            PSX_HIR_COMPOUND_ADD)
      found_typed_compound_assignment = 1;
  }
  ASSERT_TRUE(found_typed_compound_assignment);
  ps_dispose_function_definition_syntax(syntax_function);
  ps_parser_stream_end(&stream);
}

static void test_function_definition_header_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_function_definition_header_resolution_boundary...\n");
  const char *source =
      "long __direct_header_value(int left, double right) { "
      "return left; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item.kind);
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  psx_parsed_function_definition_t *syntax_function =
      &item.value.function_header;
  node_block_t *syntax_body = as_block(syntax_function->body);
  ASSERT_TRUE(syntax_body != NULL);
  ASSERT_EQ(ND_RETURN, syntax_body->body[0]->kind);
  node_t *syntax_return_value = syntax_body->body[0]->lhs;
  ASSERT_TRUE(syntax_return_value != NULL);
  ASSERT_EQ(ND_IDENTIFIER, syntax_return_value->kind);

  psx_function_definition_header_resolution_t resolution;
  ASSERT_TRUE(psx_resolve_function_definition_header_in_contexts(
      test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
      test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
      syntax_function, &resolution));
  ASSERT_EQ((int)strlen("__direct_header_value"), resolution.name_len);
  ASSERT_TRUE(strncmp(
      resolution.name, "__direct_header_value",
      (size_t)resolution.name_len) == 0);
  ASSERT_TRUE(resolution.signature_qual_type.type_id !=
              PSX_TYPE_ID_INVALID);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  psx_qual_type_t registered_signature =
      ps_ctx_get_function_qual_type_in(
          test_semantic_context(test_suite_session), resolution.name,
          resolution.name_len);
  ASSERT_EQ(resolution.signature_qual_type.type_id,
            registered_signature.type_id);
  ASSERT_EQ(resolution.signature_qual_type.qualifiers,
            registered_signature.qualifiers);
  psx_type_shape_t function_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, resolution.signature_qual_type.type_id, &function_shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  psx_qual_type_t return_qual_type = psx_semantic_type_table_base(
      types, resolution.signature_qual_type.type_id);
  psx_type_shape_t return_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, return_qual_type.type_id, &return_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, return_shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, return_shape.integer_kind);
  ASSERT_EQ(2, function_shape.parameter_count);
  ASSERT_EQ(2, resolution.parameter_count);
  ASSERT_TRUE(resolution.parameters != NULL);
  ASSERT_TRUE(resolution.parameters[0] != NULL);
  ASSERT_TRUE(resolution.parameters[1] != NULL);
  ASSERT_TRUE(ps_lvar_is_param(resolution.parameters[0]));
  ASSERT_TRUE(ps_lvar_is_param(resolution.parameters[1]));
  ASSERT_EQ(4, ps_lvar_name_len(resolution.parameters[0]));
  ASSERT_TRUE(strncmp(
      ps_lvar_name(resolution.parameters[0]), "left", 4) == 0);
  ASSERT_EQ(5, ps_lvar_name_len(resolution.parameters[1]));
  ASSERT_TRUE(strncmp(
      ps_lvar_name(resolution.parameters[1]), "right", 5) == 0);
  ASSERT_EQ(
      psx_semantic_type_table_parameter(
          types, resolution.signature_qual_type.type_id, 0).type_id,
      ps_lvar_decl_type_id(resolution.parameters[0]));
  ASSERT_EQ(
      psx_semantic_type_table_parameter(
          types, resolution.signature_qual_type.type_id, 1).type_id,
      ps_lvar_decl_type_id(resolution.parameters[1]));
  ASSERT_TRUE(!resolution.is_static);
  ASSERT_TRUE(!resolution.is_variadic);

  ASSERT_EQ(ND_IDENTIFIER, syntax_return_value->kind);
  ps_dispose_function_definition_syntax(syntax_function);
  ps_parser_stream_end(&stream);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_global_registry_checkpoint_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_global_registry_checkpoint_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  global_var_t existing = {
      .name = (char *)"__checkpoint_existing",
      .name_len = (int)strlen("__checkpoint_existing"),
      .is_extern_decl = 1,
  };
  ps_register_global_var_in(test_global_registry(test_suite_session), &existing);
  ASSERT_EQ(0, ps_global_registry_next_string_literal_id(
                   test_global_registry(test_suite_session)));

  psx_global_registry_checkpoint_t checkpoint = {0};
  ASSERT_TRUE(psx_global_registry_checkpoint_begin(
      test_global_registry(test_suite_session), &checkpoint));
  ASSERT_TRUE(psx_global_registry_checkpoint_is_active(
      test_global_registry(test_suite_session)));
  ASSERT_TRUE(psx_global_registry_note_global_mutation(
      test_global_registry(test_suite_session), &existing));
  existing.is_extern_decl = 0;
  existing.is_static = 1;

  global_var_t added = {
      .name = (char *)"__checkpoint_added",
      .name_len = (int)strlen("__checkpoint_added"),
  };
  ps_register_global_var_in(test_global_registry(test_suite_session), &added);
  string_lit_t literal = {
      .label = (char *)".L.checkpoint",
      .str = (char *)"checkpoint",
      .len = 10,
  };
  psx_register_string_lit_in(test_global_registry(test_suite_session), &literal);
  ASSERT_EQ(1, ps_global_registry_next_string_literal_id(
                   test_global_registry(test_suite_session)));

  psx_global_registry_checkpoint_rollback(
      test_global_registry(test_suite_session), &checkpoint);
  ASSERT_TRUE(!psx_global_registry_checkpoint_is_active(
      test_global_registry(test_suite_session)));
  ASSERT_TRUE(find_test_global_var_in(
      test_global_registry(test_suite_session), existing.name, existing.name_len) ==
              &existing);
  ASSERT_TRUE(find_test_global_var_in(
      test_global_registry(test_suite_session), added.name, added.name_len) == NULL);
  ASSERT_TRUE(ps_find_string_lit_by_label_in(
      test_global_registry(test_suite_session), literal.label) == NULL);
  ASSERT_TRUE(existing.is_extern_decl);
  ASSERT_TRUE(!existing.is_static);
  ASSERT_EQ(1, ps_global_registry_next_string_literal_id(
                   test_global_registry(test_suite_session)));

  ASSERT_TRUE(psx_global_registry_checkpoint_begin(
      test_global_registry(test_suite_session), &checkpoint));
  ASSERT_TRUE(psx_global_registry_note_global_mutation(
      test_global_registry(test_suite_session), &existing));
  existing.is_static = 1;
  ps_register_global_var_in(test_global_registry(test_suite_session), &added);
  psx_global_registry_checkpoint_commit(
      test_global_registry(test_suite_session), &checkpoint);
  ASSERT_TRUE(!psx_global_registry_checkpoint_is_active(
      test_global_registry(test_suite_session)));
  ASSERT_TRUE(existing.is_static);
  ASSERT_TRUE(find_test_global_var_in(
      test_global_registry(test_suite_session), added.name, added.name_len) == &added);

  psx_local_registry_checkpoint_t local_checkpoint = {0};
  ASSERT_TRUE(psx_local_registry_checkpoint_begin(
      test_local_registry(test_suite_session), &local_checkpoint));
  ASSERT_TRUE(psx_local_registry_checkpoint_is_active(
      test_local_registry(test_suite_session)));
  psx_local_registry_checkpoint_rollback(
      test_local_registry(test_suite_session), &local_checkpoint);
  ASSERT_TRUE(!psx_local_registry_checkpoint_is_active(
      test_local_registry(test_suite_session)));
  reset_test_translation_unit_state(test_suite_session);
}

static void test_direct_function_typed_hir_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_direct_function_typed_hir_resolution_boundary...\n");
  const char *source =
      "static int __direct_function_hir(int left, int right) { "
      "int sum = left + right; int unused; "
      "{ int sum = right; int values[3] = {[1] = 2, "
      "[0] = values != 0}; "
      "int inferred[] = {4, 5}; "
      "int matrix[2][2] = {{1, 2}, {[0] = 3}}; "
      "int sparse[2][2] = {[1][0] = 8, [0][1] = 9}; "
      "struct __direct_pair { int x; int y; int cells[2]; "
      "unsigned flags : 3; } pair = "
      "{.y = 7, .x = 6, .cells = {8, 9}, .flags = 1}; "
      "struct __direct_marker { int value; }; "
      "struct __direct_marker marker = {.value = 12}; "
      "enum __direct_mode { __DIRECT_MODE_A = 3, "
      "__DIRECT_MODE_B } mode = __DIRECT_MODE_B; "
      "_Alignas(16) int aligned = 13; "
      "typedef int __DirectWord, __DirectFn(int); "
      "__DirectWord typed = right; "
      "int count = right + 1; int runtime_values[count]; "
      "int runtime_matrix[count][count][count]; "
      "static int __direct_static_counter; "
      "static int __direct_static_initialized = 17; "
      "static int __direct_static_values[2] = "
      "{[1] = 19, [0] = 18}; "
      "int aggregate_cast_value = ((struct __direct_pair)20).x; "
      "int aggregate_cast_address_value = "
      "(&((struct __direct_pair)21))->x; "
      "int compound_scalar_value = (int){22}; "
      "int compound_address_value = *&(int){23}; "
      "int compound_array_value = ((int[2]){24, 25})[1]; "
      "int compound_record_value = "
      "((struct __direct_pair){.x = 26, .y = 27}).y; "
      "int direct_expression_value = 28 + left; "
      "union __direct_union { int x; double wide; } "
      "union_value = {.x = 29}; "
      "extern int __direct_external_value; "
      "int __direct_declared_function(int); "
      "for (int index = 0, limit = 2; index < limit; index++) "
      "sum += index; "
      "int index = 30, scalar_braced = {{31}}; "
      "sum += index + scalar_braced; "
      "sum += sizeof((struct __direct_pair)pair); "
      "sum += values[0] + inferred[1] + matrix[1][0] + "
      "sparse[0][1] + pair.y + pair.cells[1] + pair.flags + "
      "marker.value + mode + aligned + typed + __direct_static_counter + "
      "__direct_static_initialized + "
      "__direct_static_values[0] + __direct_static_values[1] + "
      "aggregate_cast_value + "
      "aggregate_cast_address_value + "
      "compound_scalar_value + compound_address_value + "
      "compound_array_value + compound_record_value + "
      "direct_expression_value + "
      "runtime_values[0] + (runtime_matrix + 1)[0][0][0] + "
      "union_value.x + "
      "__direct_external_value + __direct_declared_function(left) + "
      "_Generic(left, int: 23, default: 24); "
      "sum += sizeof(int[3]) + _Alignof(void *) + "
      "sizeof(runtime_values) + sizeof(runtime_matrix[left]) + "
      "sizeof((int){99}) + sizeof(__DirectFn *) + "
      "(&runtime_values != 0); "
      "sum++, --sum, (void)sum, (void)pair; "
      "char direct_text[] = \"A\\n\"; "
      "unsigned short direct_wide[4] = {u\"A\\u03A9\"}; "
      "sum += direct_text[1] + direct_wide[1]; "
      "int compound_string_value = ((char[]){\"Q\"})[0]; "
      "sum += compound_string_value; "
      "int typedef_seed = right + 2; "
      "typedef int __DirectVla[typedef_seed]; "
      "typedef_seed = 1; __DirectVla typedef_values; "
      "sum += sizeof(__DirectVla) + sizeof(typedef_values); } "
      "if (left) return sum; return 0; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item.kind);
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  psx_parsed_function_definition_t *syntax_function =
      &item.value.function_header;
  node_block_t *syntax_body = as_block(syntax_function->body);
  ASSERT_EQ(ND_LOCAL_DECLARATION, syntax_body->body[0]->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, syntax_body->body[1]->kind);
  ASSERT_EQ(ND_BLOCK, syntax_body->body[2]->kind);
  ASSERT_EQ(ND_IF, syntax_body->body[3]->kind);
  node_block_t *nested_block = as_block(syntax_body->body[2]);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[1]->kind);
  node_local_declaration_t *array_declaration =
      (node_local_declaration_t *)nested_block->body[1];
  const node_t *syntax_array_bound =
      array_declaration->declaration->declarators[0]
          .array_bounds[0].expression.node;
  const node_init_list_t *syntax_array_initializer =
      (const node_init_list_t *)
          array_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_array_bound != NULL);
  ASSERT_TRUE(syntax_array_initializer != NULL);
  ASSERT_EQ(ND_INIT_LIST, syntax_array_initializer->base.kind);
  ASSERT_EQ(2, syntax_array_initializer->entry_count);
  ASSERT_EQ(1,
            syntax_array_initializer->entries[0].designator_count);
  const node_t *syntax_designator_index =
      syntax_array_initializer->entries[0]
          .designators[0].index_expr;
  ASSERT_TRUE(syntax_designator_index != NULL);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[2]->kind);
  node_local_declaration_t *inferred_declaration =
      (node_local_declaration_t *)nested_block->body[2];
  const psx_declarator_shape_t *syntax_inferred_shape =
      &inferred_declaration->declaration->declarators[0]
           .declarator_shape;
  ASSERT_EQ(1, syntax_inferred_shape->count);
  ASSERT_EQ(PSX_DECL_OP_ARRAY, syntax_inferred_shape->ops[0].kind);
  ASSERT_TRUE(syntax_inferred_shape->ops[0].is_incomplete_array);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[3]->kind);
  node_local_declaration_t *matrix_declaration =
      (node_local_declaration_t *)nested_block->body[3];
  const node_t *syntax_matrix_outer_bound =
      matrix_declaration->declaration->declarators[0]
          .array_bounds[0].expression.node;
  const node_t *syntax_matrix_inner_bound =
      matrix_declaration->declaration->declarators[0]
          .array_bounds[1].expression.node;
  const node_init_list_t *syntax_matrix_initializer =
      (const node_init_list_t *)
          matrix_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_matrix_outer_bound != NULL);
  ASSERT_TRUE(syntax_matrix_inner_bound != NULL);
  ASSERT_TRUE(syntax_matrix_initializer != NULL);
  ASSERT_EQ(2, syntax_matrix_initializer->entry_count);
  ASSERT_EQ(ND_INIT_LIST,
            syntax_matrix_initializer->entries[1].value->kind);
  const node_init_list_t *syntax_matrix_second_row =
      (const node_init_list_t *)
          syntax_matrix_initializer->entries[1].value;
  ASSERT_EQ(1, syntax_matrix_second_row->entry_count);
  ASSERT_EQ(1,
            syntax_matrix_second_row->entries[0].designator_count);
  const node_t *syntax_matrix_designator_index =
      syntax_matrix_second_row->entries[0].designators[0].index_expr;
  ASSERT_TRUE(syntax_matrix_designator_index != NULL);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[4]->kind);
  node_local_declaration_t *sparse_declaration =
      (node_local_declaration_t *)nested_block->body[4];
  const node_init_list_t *syntax_sparse_initializer =
      (const node_init_list_t *)
          sparse_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_sparse_initializer != NULL);
  ASSERT_EQ(2, syntax_sparse_initializer->entry_count);
  ASSERT_EQ(2,
            syntax_sparse_initializer->entries[0].designator_count);
  const node_t *syntax_sparse_nested_index =
      syntax_sparse_initializer->entries[0].designators[1].index_expr;
  ASSERT_TRUE(syntax_sparse_nested_index != NULL);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[5]->kind);
  node_local_declaration_t *pair_declaration =
      (node_local_declaration_t *)nested_block->body[5];
  const node_init_list_t *syntax_pair_initializer =
      (const node_init_list_t *)
          pair_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_pair_initializer != NULL);
  ASSERT_EQ(4, syntax_pair_initializer->entry_count);
  ASSERT_EQ(1, syntax_pair_initializer->entries[0].designator_count);
  const psx_parsed_aggregate_body_t *syntax_pair_body =
      pair_declaration->declaration->specifier.tag_action.aggregate_body;
  ASSERT_TRUE(syntax_pair_body != NULL);
  ASSERT_EQ(4, syntax_pair_body->item_count);
  const psx_parsed_declarator_t *syntax_cells_member =
      &syntax_pair_body->items[2].value.member_declaration.declarators[0];
  const psx_parsed_declarator_t *syntax_flags_member =
      &syntax_pair_body->items[3].value.member_declaration.declarators[0];
  ASSERT_EQ(1, syntax_cells_member->array_bound_count);
  ASSERT_TRUE(syntax_cells_member->array_bounds[0].expression.node != NULL);
  ASSERT_TRUE(syntax_flags_member->has_bitfield);
  ASSERT_TRUE(syntax_flags_member->bit_width_expression.node != NULL);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[6]->kind);
  node_local_declaration_t *standalone_tag_declaration =
      (node_local_declaration_t *)nested_block->body[6];
  ASSERT_TRUE(standalone_tag_declaration->declaration->is_standalone_tag);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[8]->kind);
  node_local_declaration_t *mode_declaration =
      (node_local_declaration_t *)nested_block->body[8];
  const node_t *syntax_mode_initializer =
      mode_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_mode_initializer != NULL);
  ASSERT_EQ(ND_IDENTIFIER, syntax_mode_initializer->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[9]->kind);
  node_local_declaration_t *aligned_declaration =
      (node_local_declaration_t *)nested_block->body[9];
  ASSERT_EQ(1, aligned_declaration->declaration->specifier
                   .alignas_specifier_count);
  const psx_parsed_alignas_t *syntax_alignas =
      &aligned_declaration->declaration->specifier.alignas_specifiers[0];
  ASSERT_EQ(PSX_PARSED_ALIGNAS_EXPRESSION, syntax_alignas->kind);
  ASSERT_TRUE(syntax_alignas->expression != NULL);
  ASSERT_EQ(ND_NUM, syntax_alignas->expression->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[10]->kind);
  node_local_declaration_t *typedef_declaration =
      (node_local_declaration_t *)nested_block->body[10];
  ASSERT_TRUE(typedef_declaration->declaration->is_typedef);
  ASSERT_EQ(2, typedef_declaration->declaration->declarator_count);
  ASSERT_EQ(1, typedef_declaration->declaration->declarators[1]
                   .function_suffix_count);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[11]->kind);
  node_local_declaration_t *typed_declaration =
      (node_local_declaration_t *)nested_block->body[11];
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            typed_declaration->declaration->specifier.source);
  const node_t *syntax_typed_initializer =
      typed_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_typed_initializer != NULL);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[13]->kind);
  node_local_declaration_t *vla_declaration =
      (node_local_declaration_t *)nested_block->body[13];
  const node_t *syntax_vla_bound =
      vla_declaration->declaration->declarators[0]
          .array_bounds[0].expression.node;
  ASSERT_TRUE(syntax_vla_bound != NULL);
  ASSERT_EQ(ND_IDENTIFIER, syntax_vla_bound->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[17]->kind);
  node_local_declaration_t *static_array_declaration =
      (node_local_declaration_t *)nested_block->body[17];
  const node_init_list_t *syntax_static_array_initializer =
      (const node_init_list_t *)
          static_array_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_static_array_initializer != NULL);
  ASSERT_EQ(2, syntax_static_array_initializer->entry_count);
  const node_t *syntax_static_array_index =
      syntax_static_array_initializer->entries[0]
          .designators[0].index_expr;
  ASSERT_TRUE(syntax_static_array_index != NULL);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[18]->kind);
  node_local_declaration_t *aggregate_cast_declaration =
      (node_local_declaration_t *)nested_block->body[18];
  const node_t *syntax_aggregate_cast_member =
      aggregate_cast_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_aggregate_cast_member != NULL);
  ASSERT_EQ(ND_MEMBER_ACCESS, syntax_aggregate_cast_member->kind);
  const node_t *syntax_aggregate_cast = syntax_aggregate_cast_member->lhs;
  ASSERT_TRUE(syntax_aggregate_cast != NULL);
  ASSERT_EQ(ND_SOURCE_CAST, syntax_aggregate_cast->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[20]->kind);
  node_local_declaration_t *compound_scalar_declaration =
      (node_local_declaration_t *)nested_block->body[20];
  const node_t *syntax_compound_scalar =
      compound_scalar_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_compound_scalar != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax_compound_scalar->kind);
  ASSERT_TRUE(
      ((const node_compound_literal_t *)syntax_compound_scalar)
          ->type_name.scope_seq != PSX_SCOPE_ID_TRANSLATION_UNIT);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[21]->kind);
  node_local_declaration_t *compound_address_declaration =
      (node_local_declaration_t *)nested_block->body[21];
  const node_t *syntax_compound_deref =
      compound_address_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_compound_deref != NULL);
  ASSERT_EQ(ND_UNARY_DEREF, syntax_compound_deref->kind);
  const node_t *syntax_compound_address_expr =
      syntax_compound_deref->lhs;
  ASSERT_TRUE(syntax_compound_address_expr != NULL);
  ASSERT_EQ(ND_ADDRESS_OF, syntax_compound_address_expr->kind);
  const node_t *syntax_compound_address =
      syntax_compound_address_expr->lhs;
  ASSERT_TRUE(syntax_compound_address != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax_compound_address->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[22]->kind);
  node_local_declaration_t *compound_array_declaration =
      (node_local_declaration_t *)nested_block->body[22];
  const node_t *syntax_compound_subscript =
      compound_array_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_compound_subscript != NULL);
  ASSERT_EQ(ND_SUBSCRIPT, syntax_compound_subscript->kind);
  const node_t *syntax_compound_array = syntax_compound_subscript->lhs;
  ASSERT_TRUE(syntax_compound_array != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax_compound_array->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[23]->kind);
  node_local_declaration_t *compound_record_declaration =
      (node_local_declaration_t *)nested_block->body[23];
  const node_t *syntax_compound_member =
      compound_record_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_compound_member != NULL);
  ASSERT_EQ(ND_MEMBER_ACCESS, syntax_compound_member->kind);
  const node_t *syntax_compound_record = syntax_compound_member->lhs;
  ASSERT_TRUE(syntax_compound_record != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax_compound_record->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[24]->kind);
  node_local_declaration_t *direct_expression_declaration =
      (node_local_declaration_t *)nested_block->body[24];
  const node_t *syntax_direct_expression =
      direct_expression_declaration->declaration
          ->initializers[0].value;
  ASSERT_TRUE(syntax_direct_expression != NULL);
  ASSERT_EQ(ND_ADD, syntax_direct_expression->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[25]->kind);
  node_local_declaration_t *union_declaration =
      (node_local_declaration_t *)nested_block->body[25];
  const node_init_list_t *syntax_union_initializer =
      (const node_init_list_t *)
          union_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_union_initializer != NULL);
  ASSERT_EQ(1, syntax_union_initializer->entry_count);
  ASSERT_EQ(1,
            syntax_union_initializer->entries[0].designator_count);
  const node_t *syntax_union_value =
      syntax_union_initializer->entries[0].value;
  ASSERT_TRUE(syntax_union_value != NULL);
  ASSERT_EQ(ND_FOR, nested_block->body[28]->kind);
  const node_ctrl_t *syntax_declaration_for =
      (const node_ctrl_t *)nested_block->body[28];
  ASSERT_TRUE(syntax_declaration_for->init != NULL);
  ASSERT_EQ(ND_LOCAL_DECLARATION,
            syntax_declaration_for->init->kind);
  const node_local_declaration_t *syntax_for_declaration =
      (const node_local_declaration_t *)syntax_declaration_for->init;
  ASSERT_EQ(2, syntax_for_declaration->declaration->declarator_count);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[29]->kind);
  const node_local_declaration_t *syntax_post_for_declaration =
      (const node_local_declaration_t *)nested_block->body[29];
  ASSERT_EQ(2,
            syntax_post_for_declaration->declaration->declarator_count);
  const psx_parsed_initializer_t *syntax_scalar_braced_initializer =
      &syntax_post_for_declaration->declaration->initializers[1];
  ASSERT_TRUE(syntax_scalar_braced_initializer->has_initializer);
  ASSERT_EQ(PSX_DECL_INIT_LIST,
            syntax_scalar_braced_initializer->kind);
  ASSERT_TRUE(syntax_scalar_braced_initializer->value != NULL);
  ASSERT_EQ(ND_INIT_LIST,
            syntax_scalar_braced_initializer->value->kind);
  const node_init_list_t *syntax_scalar_braced_outer =
      (const node_init_list_t *)syntax_scalar_braced_initializer->value;
  ASSERT_EQ(1, syntax_scalar_braced_outer->entry_count);
  ASSERT_EQ(ND_INIT_LIST,
            syntax_scalar_braced_outer->entries[0].value->kind);
  const node_init_list_t *syntax_scalar_braced_inner =
      (const node_init_list_t *)syntax_scalar_braced_outer
          ->entries[0].value;
  ASSERT_EQ(1, syntax_scalar_braced_inner->entry_count);
  const node_t *syntax_scalar_braced_value =
      syntax_scalar_braced_inner->entries[0].value;
  ASSERT_TRUE(syntax_scalar_braced_value != NULL);
  ASSERT_EQ(ND_NUM, syntax_scalar_braced_value->kind);
  ASSERT_EQ(ND_COMPOUND_ASSIGN, nested_block->body[31]->kind);
  const node_t *syntax_aggregate_sizeof =
      nested_block->body[31]->rhs;
  ASSERT_TRUE(syntax_aggregate_sizeof != NULL);
  ASSERT_EQ(ND_SIZEOF_QUERY, syntax_aggregate_sizeof->kind);
  const node_t *syntax_unevaluated_aggregate_cast =
      ((const node_sizeof_query_t *)syntax_aggregate_sizeof)->operand;
  ASSERT_TRUE(syntax_unevaluated_aggregate_cast != NULL);
  ASSERT_EQ(ND_SOURCE_CAST, syntax_unevaluated_aggregate_cast->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[35]->kind);
  const node_local_declaration_t *syntax_text_declaration =
      (const node_local_declaration_t *)nested_block->body[35];
  const psx_declarator_shape_t *syntax_text_shape =
      &syntax_text_declaration->declaration->declarators[0]
           .declarator_shape;
  ASSERT_EQ(1, syntax_text_shape->count);
  ASSERT_EQ(PSX_DECL_OP_ARRAY, syntax_text_shape->ops[0].kind);
  ASSERT_TRUE(syntax_text_shape->ops[0].is_incomplete_array);
  const node_t *syntax_text_string =
      syntax_text_declaration->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_text_string != NULL);
  ASSERT_EQ(ND_STRING, syntax_text_string->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[36]->kind);
  const node_local_declaration_t *syntax_wide_declaration =
      (const node_local_declaration_t *)nested_block->body[36];
  const node_init_list_t *syntax_wide_initializer =
      (const node_init_list_t *)syntax_wide_declaration
          ->declaration->initializers[0].value;
  ASSERT_TRUE(syntax_wide_initializer != NULL);
  ASSERT_EQ(ND_INIT_LIST, syntax_wide_initializer->base.kind);
  ASSERT_EQ(1, syntax_wide_initializer->entry_count);
  const node_t *syntax_wide_string =
      syntax_wide_initializer->entries[0].value;
  ASSERT_TRUE(syntax_wide_string != NULL);
  ASSERT_EQ(ND_STRING, syntax_wide_string->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[38]->kind);
  const node_local_declaration_t *syntax_compound_string_declaration =
      (const node_local_declaration_t *)nested_block->body[38];
  const node_t *syntax_compound_string_subscript =
      syntax_compound_string_declaration->declaration
          ->initializers[0].value;
  ASSERT_TRUE(syntax_compound_string_subscript != NULL);
  ASSERT_EQ(ND_SUBSCRIPT, syntax_compound_string_subscript->kind);
  const node_t *syntax_compound_string =
      syntax_compound_string_subscript->lhs;
  ASSERT_TRUE(syntax_compound_string != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax_compound_string->kind);
  const node_init_list_t *syntax_compound_string_initializer =
      (const node_init_list_t *)syntax_compound_string->rhs;
  ASSERT_TRUE(syntax_compound_string_initializer != NULL);
  ASSERT_EQ(1, syntax_compound_string_initializer->entry_count);
  const node_t *syntax_compound_string_value =
      syntax_compound_string_initializer->entries[0].value;
  ASSERT_TRUE(syntax_compound_string_value != NULL);
  ASSERT_EQ(ND_STRING, syntax_compound_string_value->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION, nested_block->body[41]->kind);
  const node_local_declaration_t *syntax_vla_typedef =
      (const node_local_declaration_t *)nested_block->body[41];
  ASSERT_TRUE(syntax_vla_typedef->declaration->is_typedef);
  const node_t *syntax_vla_typedef_bound =
      syntax_vla_typedef->declaration->declarators[0]
          .array_bounds[0].expression.node;
  ASSERT_TRUE(syntax_vla_typedef_bound != NULL);
  node_t *syntax_condition = syntax_body->body[3]->lhs;
  ASSERT_EQ(ND_IDENTIFIER, syntax_condition->kind);

  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_resolved_hir_build_failure_t failure;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
          ag_compilation_session_options_view(test_suite_session),
          syntax_function, &typed_hir, &failure));
  ASSERT_TRUE(typed_hir != NULL);
  ASSERT_EQ(PSX_HIR_FUNCTION,
            psx_typed_hir_tree_root_kind(typed_hir));
  ASSERT_EQ(ND_IDENTIFIER, syntax_condition->kind);
  ASSERT_TRUE(syntax_inferred_shape->ops[0].is_incomplete_array);
  ASSERT_TRUE(syntax_cells_member->array_bounds[0].expression.node != NULL);
  ASSERT_TRUE(syntax_flags_member->bit_width_expression.node != NULL);
  ASSERT_TRUE(syntax_alignas->expression != NULL);
  ASSERT_EQ(ND_IDENTIFIER, syntax_vla_bound->kind);
  ASSERT_EQ(ND_LOCAL_DECLARATION,
            syntax_declaration_for->init->kind);
  ASSERT_EQ(ND_INIT_LIST,
            syntax_scalar_braced_initializer->value->kind);
  ASSERT_EQ(ND_SOURCE_CAST, syntax_unevaluated_aggregate_cast->kind);
  ASSERT_TRUE(syntax_text_shape->ops[0].is_incomplete_array);
  ASSERT_EQ(ND_STRING, syntax_text_string->kind);
  ASSERT_EQ(ND_STRING, syntax_wide_string->kind);
  ASSERT_EQ(ND_COMPOUND_LITERAL, syntax_compound_string->kind);
  ASSERT_EQ(ND_STRING, syntax_compound_string_value->kind);
  global_var_t *direct_external = find_test_global_var_in(
      test_global_registry(test_suite_session), (char *)"__direct_external_value",
      (int)strlen("__direct_external_value"));
  ASSERT_TRUE(direct_external != NULL);
  ASSERT_TRUE(ps_gvar_is_extern_decl(direct_external));
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
      test_semantic_context(test_suite_session), (char *)"__direct_declared_function",
      (int)strlen("__direct_declared_function")) != NULL);
  global_var_t *direct_static = find_test_global_var_in(
      test_global_registry(test_suite_session),
      (char *)"__direct_function_hir.__direct_static_counter.0",
      (int)strlen(
          "__direct_function_hir.__direct_static_counter.0"));
  ASSERT_TRUE(direct_static != NULL);
  ASSERT_TRUE(ps_gvar_is_static_storage(direct_static));
  global_var_t *direct_static_initialized = find_test_global_var_in(
      test_global_registry(test_suite_session),
      (char *)"__direct_function_hir.__direct_static_initialized.1",
      (int)strlen(
          "__direct_function_hir.__direct_static_initialized.1"));
  ASSERT_TRUE(direct_static_initialized != NULL);
  ASSERT_TRUE(ps_gvar_is_static_storage(direct_static_initialized));
  ASSERT_TRUE(direct_static_initialized->has_init);
  ASSERT_EQ(17, direct_static_initialized->init_val);
  global_var_t *direct_static_values = find_test_global_var_in(
      test_global_registry(test_suite_session),
      (char *)"__direct_function_hir.__direct_static_values.ac0",
      (int)strlen(
          "__direct_function_hir.__direct_static_values.ac0"));
  ASSERT_TRUE(direct_static_values != NULL);
  ASSERT_TRUE(ps_gvar_is_static_storage(direct_static_values));
  ASSERT_TRUE(direct_static_values->has_init);
  ASSERT_EQ(2, direct_static_values->init_count);
  ASSERT_EQ(18, ps_gvar_init_slot_view(direct_static_values, 0).value);
  ASSERT_EQ(19, ps_gvar_init_slot_view(direct_static_values, 1).value);

  psx_hir_module_t *hir = psx_hir_module_create();
  ASSERT_TRUE(hir != NULL);
  psx_hir_node_id_t root_id = psx_typed_hir_tree_emit(
      hir, typed_hir, &failure);
  ASSERT_TRUE(root_id != PSX_HIR_NODE_ID_INVALID);
  const psx_hir_node_t *root = psx_hir_module_lookup(hir, root_id);
  ASSERT_TRUE(root != NULL);
  ASSERT_EQ(PSX_HIR_FUNCTION, psx_hir_node_kind(root));
  ASSERT_TRUE(psx_hir_node_is_static_function(root));
  ASSERT_TRUE(psx_hir_node_attached_qual_type(root).type_id !=
              PSX_TYPE_ID_INVALID);
  unsigned aggregate_cast_temporary_mask = 0;
  unsigned compound_literal_object_mask = 0;
  int compound_statement_expression_count = 0;
  int vla_subscript_stride_count = 0;
  int vla_pointer_add_stride_count = 0;
  int declaration_for_initializer_count = 0;
  int direct_text_assignment_count = 0;
  int direct_text_a_count = 0;
  int direct_text_newline_count = 0;
  int direct_text_zero_count = 0;
  int direct_wide_assignment_count = 0;
  int direct_wide_a_count = 0;
  int direct_wide_omega_count = 0;
  int direct_wide_zero_count = 0;
  int compound_string_assignment_count = 0;
  int compound_string_q_count = 0;
  int compound_string_zero_count = 0;
  int vla_typedef_bound_reference_count = 0;
  int vla_typedef_bound_capture_count = 0;
  int source_assignment_count = 0;
  int declaration_initializer_count = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *hir_node = psx_hir_module_lookup(
        hir, (psx_hir_node_id_t)i);
    size_t name_length = 0;
    const char *name = hir_node
                           ? psx_hir_node_name(hir_node, &name_length)
                           : NULL;
    if (hir_node && psx_hir_node_kind(hir_node) == PSX_HIR_LOCAL &&
        name && name_length >= strlen("__aggregate_cast_0") &&
        memcmp(name, "__aggregate_cast_", 17) == 0) {
      if (name[17] == '0') aggregate_cast_temporary_mask |= 1u;
      if (name[17] == '1') aggregate_cast_temporary_mask |= 2u;
    }
    if (hir_node && psx_hir_node_kind(hir_node) == PSX_HIR_LOCAL &&
        name && name_length >= strlen("__compound_object_0") &&
        memcmp(name, "__compound_object_", 18) == 0) {
      if (name[18] == '0') compound_literal_object_mask |= 1u;
      if (name[18] == '1') compound_literal_object_mask |= 2u;
      if (name[18] == '2') compound_literal_object_mask |= 4u;
      if (name[18] == '3') compound_literal_object_mask |= 8u;
      if (name[18] == '4') compound_literal_object_mask |= 16u;
    }
    if (hir_node && psx_hir_node_kind(hir_node) == PSX_HIR_LOCAL &&
        name &&
        name_length == strlen("__vla_typedef_bound_0") &&
        memcmp(name, "__vla_typedef_bound_0", name_length) == 0)
      vla_typedef_bound_reference_count++;
    if (hir_node &&
        psx_hir_node_kind(hir_node) == PSX_HIR_STMT_EXPR)
      compound_statement_expression_count++;
    if (hir_node &&
        psx_hir_node_kind(hir_node) == PSX_HIR_SUBSCRIPT &&
        psx_hir_node_vla_stride_frame_offset(hir_node) > 0)
      vla_subscript_stride_count++;
    if (hir_node && psx_hir_node_kind(hir_node) == PSX_HIR_ADD &&
        psx_hir_node_vla_stride_frame_offset(hir_node) > 0)
      vla_pointer_add_stride_count++;
    if (psx_hir_node_is_source_assignment(hir_node))
      source_assignment_count++;
    if (psx_hir_node_is_declaration_initializer(hir_node))
      declaration_initializer_count++;
    if (hir_node && psx_hir_node_kind(hir_node) == PSX_HIR_FOR) {
      for (size_t child = 0;
           child < psx_hir_node_child_count(hir_node); child++) {
        if (psx_hir_node_child_edge_at(hir_node, child) !=
            PSX_HIR_EDGE_INIT)
          continue;
        const psx_hir_node_t *initializer = psx_hir_module_lookup(
            hir, psx_hir_node_child_at(hir_node, child));
        if (initializer &&
            psx_hir_node_kind(initializer) == PSX_HIR_BLOCK &&
            psx_hir_node_child_count(initializer) == 2)
          declaration_for_initializer_count++;
      }
    }
    if (!hir_node || psx_hir_node_kind(hir_node) != PSX_HIR_ASSIGN)
      continue;
    const psx_hir_node_t *assignment_lhs = NULL;
    const psx_hir_node_t *assignment_rhs = NULL;
    for (size_t child = 0;
         child < psx_hir_node_child_count(hir_node); child++) {
      const psx_hir_node_t *value = psx_hir_module_lookup(
          hir, psx_hir_node_child_at(hir_node, child));
      if (psx_hir_node_child_edge_at(hir_node, child) ==
          PSX_HIR_EDGE_LHS)
        assignment_lhs = value;
      if (psx_hir_node_child_edge_at(hir_node, child) ==
          PSX_HIR_EDGE_RHS)
        assignment_rhs = value;
    }
    if (!assignment_lhs || !assignment_rhs ||
        psx_hir_node_kind(assignment_lhs) != PSX_HIR_LOCAL)
      continue;
    size_t lhs_name_length = 0;
    const char *lhs_name = psx_hir_node_name(
        assignment_lhs, &lhs_name_length);
    if (lhs_name &&
        lhs_name_length == strlen("__vla_typedef_bound_0") &&
        memcmp(lhs_name, "__vla_typedef_bound_0",
               lhs_name_length) == 0)
      vla_typedef_bound_capture_count++;
    if (psx_hir_node_kind(assignment_rhs) != PSX_HIR_NUMBER)
      continue;
    long long unit = psx_hir_node_integer_value(assignment_rhs);
    if (lhs_name && lhs_name_length == strlen("direct_text") &&
        memcmp(lhs_name, "direct_text", lhs_name_length) == 0) {
      direct_text_assignment_count++;
      if (unit == 'A') direct_text_a_count++;
      if (unit == '\n') direct_text_newline_count++;
      if (unit == 0) direct_text_zero_count++;
    }
    if (lhs_name && lhs_name_length == strlen("direct_wide") &&
        memcmp(lhs_name, "direct_wide", lhs_name_length) == 0) {
      direct_wide_assignment_count++;
      if (unit == 'A') direct_wide_a_count++;
      if (unit == 0x03A9) direct_wide_omega_count++;
      if (unit == 0) direct_wide_zero_count++;
    }
    if (lhs_name &&
        lhs_name_length == strlen("__compound_object_4") &&
        memcmp(lhs_name, "__compound_object_4",
               lhs_name_length) == 0) {
      compound_string_assignment_count++;
      if (unit == 'Q') compound_string_q_count++;
      if (unit == 0) compound_string_zero_count++;
    }
  }
  ASSERT_EQ(3, aggregate_cast_temporary_mask);
  ASSERT_EQ(31, compound_literal_object_mask);
  ASSERT_EQ(5, compound_statement_expression_count);
  ASSERT_TRUE(vla_subscript_stride_count >= 1);
  ASSERT_TRUE(vla_pointer_add_stride_count >= 1);
  ASSERT_EQ(1, declaration_for_initializer_count);
  ASSERT_TRUE(source_assignment_count >= 1);
  ASSERT_TRUE(declaration_initializer_count >= 1);
  ASSERT_EQ(3, direct_text_assignment_count);
  ASSERT_EQ(1, direct_text_a_count);
  ASSERT_EQ(1, direct_text_newline_count);
  ASSERT_EQ(1, direct_text_zero_count);
  ASSERT_EQ(4, direct_wide_assignment_count);
  ASSERT_EQ(1, direct_wide_a_count);
  ASSERT_EQ(1, direct_wide_omega_count);
  ASSERT_EQ(2, direct_wide_zero_count);
  ASSERT_EQ(2, compound_string_assignment_count);
  ASSERT_EQ(1, compound_string_q_count);
  ASSERT_EQ(1, compound_string_zero_count);
  ASSERT_TRUE(vla_typedef_bound_reference_count >= 2);
  ASSERT_EQ(1, vla_typedef_bound_capture_count);
  int parameter_edges = 0;
  int body_edges = 0;
  for (size_t i = 0; i < psx_hir_node_child_count(root); i++) {
    if (psx_hir_node_child_edge_at(root, i) ==
        PSX_HIR_EDGE_PARAMETER)
      parameter_edges++;
    if (psx_hir_node_child_edge_at(root, i) ==
        PSX_HIR_EDGE_FUNCTION_BODY)
      body_edges++;
  }
  ASSERT_EQ(2, parameter_edges);
  ASSERT_EQ(1, body_edges);

  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(
              test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_PARAM_BIND));
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_ALLOCA) >= 13);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 25);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_VLA_ALLOC) >= 1);
  ASSERT_EQ(2, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
  psx_hir_module_destroy(hir);
  ps_dispose_function_definition_syntax(syntax_function);
  ps_parser_stream_end(&stream);

  reset_test_translation_unit_state(test_suite_session);
  const char *rollback_source =
      "int __direct_function_rollback(int input) { "
      "int __direct_fallback_transient = input; "
      "extern int __direct_fallback_external; "
      "int __direct_fallback_declared(int); "
      "int count = input + 1; int values[count][count]; "
      "for (int index = 0; index < 1; index++) input += index; "
      "return __direct_missing_identifier + (values + 1)[0][0] + "
      "__direct_fallback_external + "
      "__direct_fallback_declared(input); }";
  stream = (psx_parser_stream_t){0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)rollback_source));
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  syntax_function = &item.value.function_header;
  typed_hir = NULL;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_REJECTED,
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
          ag_compilation_session_options_view(test_suite_session),
          syntax_function, &typed_hir, &failure));
  ASSERT_TRUE(typed_hir == NULL);
  ASSERT_EQ(PSX_SYNTAX_TYPED_HIR_REJECTION_UNDEFINED_IDENTIFIER,
            failure.rejection);
  ASSERT_EQ(ND_IDENTIFIER, failure.source_node_kind);
  ASSERT_EQ(27, failure.source_name_length);
  ASSERT_TRUE(memcmp(
      failure.source_name, "__direct_missing_identifier", 27) == 0);
  ASSERT_TRUE(!test_has_function_type_in(
      test_semantic_context(test_suite_session),
      (char *)"__direct_function_rollback",
      (int)strlen("__direct_function_rollback")));
  ASSERT_TRUE(find_test_global_var_in(
      test_global_registry(test_suite_session), (char *)"__direct_fallback_external",
      (int)strlen("__direct_fallback_external")) == NULL);
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
      test_semantic_context(test_suite_session), (char *)"__direct_fallback_declared",
      (int)strlen("__direct_fallback_declared")) == NULL);
  ASSERT_TRUE(find_test_local_var_in(
      test_local_registry(test_suite_session), (char *)"__direct_fallback_transient",
      (int)strlen("__direct_fallback_transient")) == NULL);
  ASSERT_EQ(0, test_lowering_context(test_suite_session)->local_frame_layout.next_offset);
  ps_dispose_function_definition_syntax(syntax_function);
  ps_parser_stream_end(&stream);
  reset_test_translation_unit_state(test_suite_session);

  assert_direct_function_resolution(test_suite_session,
      "int __direct_generic_address(int choose) { "
      "struct S { int value; }; int local=7; int helper(int); "
      "_Generic(choose, int: local, default: local)=12; "
      "++_Generic(choose, int: local, default: local); "
      "return *&_Generic(choose, int: local, default: local) + "
      "*&_Generic(choose, int: (int){8}, default: (int){9}) + "
      "(&_Generic(choose, int: (struct S)10, "
      "default: (struct S)11))->value + "
      "(&_Generic(choose, int: helper, default: helper) != 0); }");
  assert_direct_function_rejection(test_suite_session,
      "int __direct_return_value_required(void) { return; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_VALUE_REQUIRED,
      ND_RETURN);
  assert_direct_function_rejection(test_suite_session,
      "void __direct_return_value_forbidden(void) { return 1; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_VALUE_FORBIDDEN,
      ND_RETURN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_return_pointer_as_integer(void) { "
      "int *value = 0; return value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_TYPES_INCOMPATIBLE,
      ND_RETURN);
  assert_direct_function_rejection(test_suite_session,
      "int *__direct_return_nonzero_integer(void) { return 1; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_TYPES_INCOMPATIBLE,
      ND_RETURN);
  assert_direct_function_rejection(test_suite_session,
      "int *__direct_return_discards_qualifiers(const int *value) { "
      "return value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_DISCARDS_QUALIFIERS,
      ND_RETURN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_cast_aggregate_mismatch(void) { "
      "struct S { int x; }; union U { int y; }; union U value={1}; "
      "(struct S)value; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_AGGREGATE_TYPE_MISMATCH,
      ND_SOURCE_CAST);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_cast_target_array(void) { "
      "int value=1; (int[2])value; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_TARGET_NOT_VOID_OR_SCALAR,
      ND_SOURCE_CAST);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_cast_aggregate_operand(void) { "
      "struct S { int x; }; struct S value={1}; return (int)value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_OPERAND_NOT_SCALAR,
      ND_SOURCE_CAST);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_address_rvalue(void) { return &1 != 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_REQUIRES_ADDRESSABLE_VALUE,
      ND_ADDRESS_OF);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_address_comma(void) { "
      "int left=1, right=2; return &(left, right) != 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_REQUIRES_ADDRESSABLE_VALUE,
      ND_ADDRESS_OF);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_address_generic_rvalue(void) { "
      "return &_Generic(0, int: 1, default: 2) != 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_REQUIRES_ADDRESSABLE_VALUE,
      ND_ADDRESS_OF);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_address_bitfield(void) { "
      "struct S { int bits:3; } value={1}; return &value.bits != 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_OF_BITFIELD,
      ND_ADDRESS_OF);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_address_generic_bitfield(void) { "
      "struct S { int bits:3; } value={1}; "
      "return &_Generic(0, int: value.bits, default: value.bits) != 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_OF_BITFIELD,
      ND_ADDRESS_OF);
  test_compilation_options(test_suite_session)->enable_struct_scalar_pointer_cast = false;
  assert_direct_function_rejection(test_suite_session,
      "int __direct_cast_struct_extension_disabled(void) { "
      "struct S { int x; }; int value=1; (struct S)value; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_STRUCT_EXTENSION_DISABLED,
      ND_SOURCE_CAST);
  test_compilation_options(test_suite_session)->enable_struct_scalar_pointer_cast = true;
  test_compilation_options(test_suite_session)->enable_union_scalar_pointer_cast = false;
  assert_direct_function_rejection(test_suite_session,
      "int __direct_cast_union_extension_disabled(void) { "
      "union U { int x; }; int value=1; (union U)value; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_UNION_EXTENSION_DISABLED,
      ND_SOURCE_CAST);
  test_compilation_options(test_suite_session)->enable_union_scalar_pointer_cast = true;
  assert_direct_function_rejection(test_suite_session,
      "int __direct_static_assert_not_constant(void) { "
      "int value = 1; _Static_assert(value, \"ng\"); return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_STATIC_ASSERT_NOT_CONSTANT,
      ND_STATIC_ASSERT);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_static_assert_failed(void) { "
      "_Static_assert(0, \"ng\"); return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_STATIC_ASSERT_FAILED,
      ND_STATIC_ASSERT);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_case_not_constant(int value) { "
      "switch (value) { case value: return 1; } return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CASE_NOT_INTEGER_CONSTANT,
      ND_CASE);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_dot_base(void) { int value = 0; return value.x; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_DOT_BASE_NOT_AGGREGATE,
      ND_MEMBER_ACCESS);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_arrow_base(void) { int *value = 0; "
      "return value->x; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ARROW_BASE_NOT_AGGREGATE_POINTER,
      ND_MEMBER_ACCESS);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_member_not_found(void) { "
      "struct S { int x; } value = {0}; return value.y; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_MEMBER_NOT_FOUND,
      ND_MEMBER_ACCESS);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_deref_nonpointer(void) { "
      "int value = 1; return *value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_DEREF_REQUIRES_POINTER,
      ND_UNARY_DEREF);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_deref_void_pointer(void) { "
      "void *value = 0; return *value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_DEREF_VOID_POINTER,
      ND_UNARY_DEREF);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_invalid_subscript(void) { "
      "int value = 1; return value[0]; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_INVALID_SUBSCRIPT_OPERANDS,
      ND_SUBSCRIPT);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_plus_pointer(void) { "
      "int *value = 0; return +value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ARITHMETIC_UNARY_REQUIRES_ARITHMETIC,
      ND_UNARY_PLUS);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_negate_pointer(void) { "
      "int *value = 0; return -value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ARITHMETIC_UNARY_REQUIRES_ARITHMETIC,
      ND_UNARY_NEGATE);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_bitwise_not_float(void) { "
      "double value = 1.0; return ~value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_BITWISE_NOT_REQUIRES_INTEGER,
      ND_BITWISE_NOT);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_real_pointer(void) { "
      "int *value = 0; return __real__ value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ARITHMETIC_UNARY_REQUIRES_ARITHMETIC,
      ND_CREAL);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_imag_pointer(void) { "
      "int *value = 0; return __imag__ value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ARITHMETIC_UNARY_REQUIRES_ARITHMETIC,
      ND_CIMAG);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_incdec_non_lvalue(void) { return ++1; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_REQUIRES_LVALUE,
      ND_PRE_INC);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_post_inc_comma(void) { "
      "int left=1, right=2; return (left, right)++; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_REQUIRES_LVALUE,
      ND_POST_INC);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_incdec_const(void) { "
      "const int value = 0; return ++value; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_CONST_OPERAND,
      ND_PRE_INC);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_incdec_complex(void) { "
      "_Complex double value = 0; ++value; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_INVALID_OPERAND_TYPE,
      ND_PRE_INC);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_assign_non_lvalue(void) { 1 = 2; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_REQUIRES_LVALUE,
      ND_ASSIGN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_assign_const(void) { "
      "const int value = 0; value = 1; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_CONST_TARGET,
      ND_ASSIGN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_assign_function(void) { "
      "int target(void); target = target; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_FUNCTION_TARGET,
      ND_ASSIGN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_assign_array(void) { "
      "int values[1]; values = values; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_TARGET_NOT_MODIFIABLE,
      ND_ASSIGN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_assign_discards_qualifiers(void) { "
      "const int source = 0; const int *from = &source; "
      "int *to = 0; to = from; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_DISCARDS_QUALIFIERS,
      ND_ASSIGN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_assign_incompatible(void) { "
      "int value = 0; int *pointer = 0; value = pointer; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_INCOMPATIBLE_TYPES,
      ND_ASSIGN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_compound_assign_incompatible(void) { "
      "int *pointer = 0; pointer *= 2; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_INCOMPATIBLE_TYPES,
      ND_COMPOUND_ASSIGN);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_conditional_condition_type(void) { "
      "struct S { int value; } condition = {0}; "
      "return condition ? 1 : 2; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CONDITIONAL_CONDITION_NOT_SCALAR,
      ND_TERNARY);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_conditional_branch_types(int condition) { "
      "return condition ? (void)0 : 1; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE,
      ND_TERNARY);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_call_not_callable(void) { "
      "int value = 0; return value(); }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_NOT_CALLABLE,
      ND_FUNCALL);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_call_double_pointer(void) { "
      "int (**function)(void) = 0; return function(); }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_NOT_CALLABLE,
      ND_FUNCALL);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_call_argument_count(void) { "
      "int function(int value); return function(); }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_ARGUMENT_COUNT_MISMATCH,
      ND_FUNCALL);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_generic_duplicate_default(void) { "
      "return _Generic(1, default: 1, default: 2); }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_DUPLICATE_DEFAULT,
      ND_GENERIC_SELECTION);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_generic_duplicate_type(void) { "
      "return _Generic(1, int: 1, signed int: 2, default: 3); }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_DUPLICATE_COMPATIBLE_TYPE,
      ND_GENERIC_SELECTION);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_generic_no_match(void) { "
      "return _Generic(1, float: 2); }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_NO_MATCH,
      ND_GENERIC_SELECTION);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_if_condition_type(void) { "
      "struct S { int value; } condition = {0}; "
      "if (condition) return 1; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CONTROL_CONDITION_NOT_SCALAR,
      ND_IF);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_while_condition_type(void) { "
      "struct S { int value; } condition = {0}; "
      "while (condition) return 1; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CONTROL_CONDITION_NOT_SCALAR,
      ND_WHILE);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_do_while_condition_type(void) { "
      "struct S { int value; } condition = {0}; "
      "do { return 1; } while (condition); }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CONTROL_CONDITION_NOT_SCALAR,
      ND_DO_WHILE);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_for_condition_type(void) { "
      "struct S { int value; } condition = {0}; "
      "for (; condition;) return 1; return 0; }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_CONTROL_CONDITION_NOT_SCALAR,
      ND_FOR);
  assert_direct_function_rejection(test_suite_session,
      "int __direct_switch_condition_type(void) { "
      "switch (1.0) { default: return 1; } }",
      PSX_SYNTAX_TYPED_HIR_REJECTION_SWITCH_CONDITION_NOT_INTEGER,
      ND_SWITCH);
}

static void test_direct_string_pointer_initializer_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_direct_string_pointer_initializer_boundary...\n");
  const char *source =
      "int __direct_string_pointer_initializer(void) { "
      "const char *text = \"abc\"; return text[0]; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item.kind);
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  psx_parsed_function_definition_t *syntax_function =
      &item.value.function_header;
  const node_block_t *syntax_body = as_block(syntax_function->body);
  ASSERT_EQ(ND_LOCAL_DECLARATION, syntax_body->body[0]->kind);
  const node_local_declaration_t *declaration =
      (const node_local_declaration_t *)syntax_body->body[0];
  const node_t *string =
      declaration->declaration->initializers[0].value;
  ASSERT_TRUE(string != NULL);
  ASSERT_EQ(ND_STRING, string->kind);

  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_resolved_hir_build_failure_t failure;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
          ag_compilation_session_options_view(test_suite_session),
          syntax_function, &typed_hir, &failure));
  ASSERT_TRUE(typed_hir != NULL);
  ASSERT_EQ(PSX_HIR_FUNCTION,
            psx_typed_hir_tree_root_kind(typed_hir));

  ps_dispose_function_definition_syntax(syntax_function);
  ps_parser_stream_end(&stream);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_case_label_syntax_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_case_label_syntax_hir_boundary...\n");
  const char *source =
      "int __case_label_boundary(void) { "
      "enum E { A = 2 }; "
      "switch (4) { case A * 2: return 4; default: return 0; } }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item.kind);
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  psx_parsed_function_definition_t *syntax_function =
      &item.value.function_header;
  node_block_t *syntax_body =
      as_block(syntax_function->body);
  ASSERT_EQ(ND_LOCAL_DECLARATION, syntax_body->body[0]->kind);
  ASSERT_EQ(ND_SWITCH, syntax_body->body[1]->kind);
  node_block_t *switch_body = as_block(syntax_body->body[1]->rhs);
  node_case_t *syntax_case = as_case(switch_body->body[0]);
  node_t *syntax_expression = syntax_case->base.lhs;
  ASSERT_TRUE(syntax_expression != NULL);
  ASSERT_EQ(ND_MUL, syntax_expression->kind);
  ASSERT_EQ(ND_IDENTIFIER, syntax_expression->lhs->kind);

  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_resolved_hir_build_failure_t failure;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
          ag_compilation_session_options_view(test_suite_session),
          syntax_function, &typed_hir, &failure));
  ASSERT_TRUE(typed_hir != NULL);
  psx_hir_module_t *hir = psx_hir_module_create();
  ASSERT_TRUE(hir != NULL);
  psx_hir_node_id_t hir_root = psx_typed_hir_tree_emit(
      hir, typed_hir, &failure);
  ASSERT_TRUE(hir_root != PSX_HIR_NODE_ID_INVALID);
  ASSERT_TRUE(syntax_case->base.lhs == syntax_expression);
  ASSERT_EQ(ND_MUL, syntax_expression->kind);
  ASSERT_EQ(ND_IDENTIFIER, syntax_expression->lhs->kind);

  int found_resolved_case = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (node && psx_hir_node_kind(node) == PSX_HIR_CASE &&
        psx_hir_node_integer_value(node) == 4)
      found_resolved_case = 1;
  }
  ASSERT_TRUE(found_resolved_case);
  ps_dispose_function_definition_syntax(syntax_function);
  ps_parser_stream_end(&stream);
}

static void test_toplevel_static_assert_frontend_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_toplevel_static_assert_frontend_boundary...\n");
  const char *source =
      "_Static_assert(0, \"deferred\"); "
      "int __after_static_assert(void) { return 0; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));

  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_STATIC_ASSERT, item.kind);
  ASSERT_TRUE(item.value.static_assertion.condition != NULL);
  ASSERT_EQ(ND_NUM, item.value.static_assertion.condition->kind);
  ASSERT_EQ(0, as_num(item.value.static_assertion.condition)->val);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  ps_dispose_function_definition_syntax(
      &item.value.function_header);
  ASSERT_TRUE(!ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_EOF, item.kind);
  ps_parser_stream_end(&stream);
}

static void test_block_static_assert_syntax_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_block_static_assert_syntax_hir_boundary...\n");
  const char *source =
      "int __block_static_assert(void) { "
      "_Static_assert(1 + 1 == 2, \"ok\"); return 7; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, item.kind);
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  psx_parsed_function_definition_t *syntax_function =
      &item.value.function_header;
  node_block_t *syntax_body =
      as_block(syntax_function->body);
  ASSERT_EQ(ND_STATIC_ASSERT, syntax_body->body[0]->kind);
  node_static_assert_t *syntax_assertion =
      (node_static_assert_t *)syntax_body->body[0];
  node_t *syntax_condition = syntax_assertion->condition;
  ASSERT_TRUE(syntax_condition != NULL);

  const psx_typed_hir_tree_t *typed_hir = NULL;
  psx_resolved_hir_build_failure_t failure;
  ASSERT_EQ(
      PSX_SYNTAX_TYPED_HIR_RESOLVED,
      psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), test_lowering_context(test_suite_session),
          ag_compilation_session_options_view(test_suite_session),
          syntax_function, &typed_hir, &failure));
  ASSERT_TRUE(typed_hir != NULL);
  psx_hir_module_t *hir = psx_hir_module_create();
  ASSERT_TRUE(hir != NULL);
  psx_hir_node_id_t hir_root = psx_typed_hir_tree_emit(
      hir, typed_hir, &failure);
  ASSERT_TRUE(hir_root != PSX_HIR_NODE_ID_INVALID);
  ASSERT_EQ(ND_STATIC_ASSERT, syntax_body->body[0]->kind);
  ASSERT_TRUE(syntax_assertion->condition == syntax_condition);

  int found_nop = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (node && psx_hir_node_kind(node) == PSX_HIR_NOP)
      found_nop = 1;
  }
  ASSERT_TRUE(found_nop);
  psx_hir_module_destroy(hir);
  ps_dispose_function_definition_syntax(syntax_function);
  ps_parser_stream_end(&stream);
}

static void test_toplevel_declaration_frontend_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_toplevel_declaration_frontend_boundary...\n");
  const char *source =
      "typedef unsigned long __PhaseWord; "
      "struct __PhaseTag { int value; }; "
      "__PhaseWord __phase_proto(__PhaseWord); "
      "__PhaseWord __phase_global; "
      "__PhaseWord __phase_initialized = 7; "
      "int __phase_function(void) { return 0; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(item.value.declaration.is_typedef);
  ASSERT_EQ(1, item.value.declaration.declarator_count);
  psx_typedef_info_t typedef_info;
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(test_semantic_context(test_suite_session),
      (char *)"__PhaseWord", 11, &typedef_info));
  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(test_suite_session),
      (char *)"__PhaseWord", 11, &typedef_info));
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(item.value.declaration.is_standalone_tag);
  ASSERT_EQ(-1, test_tag_member_count(test_suite_session,
                    TK_STRUCT, (char *)"__PhaseTag", 10));
  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  ASSERT_EQ(1, test_tag_member_count(test_suite_session,
                   TK_STRUCT, (char *)"__PhaseTag", 10));
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_EQ(1, item.value.declaration.declarator_count);
  ASSERT_TRUE(!test_has_function_type_in(test_semantic_context(test_suite_session),
                  (char *)"__phase_proto", 13));
  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  ASSERT_TRUE(test_has_function_type_in(test_semantic_context(test_suite_session),
                  (char *)"__phase_proto", 13));
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(!item.value.declaration.is_typedef);
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            item.value.declaration.specifier.source);
  ASSERT_TRUE(find_test_global_var(test_suite_session, "__phase_global", 14) == NULL);
  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  ASSERT_TRUE(find_test_global_var(test_suite_session, "__phase_global", 14) != NULL);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(item.value.declaration.initializers[0].has_initializer);
  ASSERT_EQ(PSX_DECL_INIT_EXPR,
            item.value.declaration.initializers[0].kind);
  ASSERT_EQ(ND_NUM, item.value.declaration.initializers[0].value->kind);
  ASSERT_TRUE(find_test_global_var(test_suite_session, "__phase_initialized", 19) == NULL);
  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  ASSERT_TRUE(find_test_global_var(test_suite_session, "__phase_initialized", 19) != NULL);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  ps_dispose_function_definition_syntax(
      &item.value.function_header);
  ps_parser_stream_end(&stream);
}

static void test_parser_owned_typedef_classifier_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parser_owned_typedef_classifier_boundary...\n");
  const char *source =
      "typedef int __ParserOwnedWord; "
      "__ParserOwnedWord __parser_owned_value;";

  reset_test_translation_unit_state(test_suite_session);
  psx_frontend_stream_t stream = {0};
  ASSERT_TRUE(psx_frontend_stream_begin(
      &stream, test_suite_session, NULL,
      tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source)));

  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(
      &stream.parser, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(item.value.declaration.is_typedef);
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(
      test_semantic_context(test_suite_session), (char *)"__ParserOwnedWord", 17,
      NULL));
  ps_dispose_toplevel_declaration_syntax(
      &item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(
      &stream.parser, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_EQ(PSX_PARSED_DECL_TYPEDEF_NAME,
            item.value.declaration.specifier.source);
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(
      test_semantic_context(test_suite_session), (char *)"__ParserOwnedWord", 17,
      NULL));
  ps_dispose_toplevel_declaration_syntax(
      &item.value.declaration);
  ASSERT_TRUE(psx_frontend_stream_end(&stream));
}

static void test_toplevel_callback_context_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_toplevel_callback_context_boundary...\n");
  ASSERT_TRUE(test_tokenizer(test_suite_session) ==
              ag_compilation_session_tokenizer(test_suite_session));
  reset_test_translation_unit_state(test_suite_session);
  tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"int __callback_global = 23;");
  psx_parsed_toplevel_declaration_t declaration;
  ASSERT_TRUE(parse_test_toplevel_declaration_syntax(test_suite_session, &declaration));
  global_var_t *global = find_test_global_var(test_suite_session,
      (char *)"__callback_global", 17);
  ASSERT_TRUE(global == NULL);
  apply_test_toplevel_declaration(test_suite_session, &declaration);
  global = find_test_global_var(test_suite_session,
      (char *)"__callback_global", 17);
  ASSERT_TRUE(global != NULL);
  ASSERT_TRUE(global->has_init);
  ASSERT_EQ(23, global->init_val);
  ps_dispose_toplevel_declaration_syntax(&declaration);
}

static void test_toplevel_compound_initializer_frontend_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_toplevel_compound_initializer_frontend_boundary...\n");
  const char *source =
      "struct __phase_pair { int x; int y; }; "
      "int *__phase_scalar = &(int){31}; "
      "int *__phase_array = &((int[2]){35,36})[1]; "
      "int *__phase_member = "
      "    &((struct __phase_pair){33,34}).y; "
      "int __phase_compound_function(void) { return 0; }";

  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  ASSERT_TRUE(item.value.declaration.is_standalone_tag);
  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  psx_parsed_initializer_t *scalar_initializer =
      &item.value.declaration.initializers[0];
  ASSERT_TRUE(scalar_initializer->has_initializer);
  ASSERT_EQ(PSX_DECL_INIT_EXPR, scalar_initializer->kind);
  const node_t *scalar_address = scalar_initializer->value;
  ASSERT_TRUE(scalar_address != NULL);
  ASSERT_EQ(ND_ADDRESS_OF, scalar_address->kind);
  const node_t *scalar_compound = scalar_address->lhs;
  ASSERT_TRUE(scalar_compound != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, scalar_compound->kind);
  ASSERT_EQ(ND_INIT_LIST, scalar_compound->rhs->kind);
  ASSERT_EQ(PSX_SCOPE_ID_TRANSLATION_UNIT,
            ((const node_compound_literal_t *)scalar_compound)
                ->type_name.scope_seq);
  ASSERT_TRUE(find_test_global_var(test_suite_session, "__phase_scalar", 14) == NULL);
  ASSERT_TRUE(find_test_global_var(test_suite_session, "__compound_lit_0", 16) == NULL);

  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  global_var_t *scalar_pointer =
      find_test_global_var(test_suite_session, "__phase_scalar", 14);
  global_var_t *scalar_object =
      find_test_global_var(test_suite_session, "__compound_lit_0", 16);
  ASSERT_TRUE(scalar_pointer != NULL);
  ASSERT_TRUE(scalar_object != NULL);
  ASSERT_TRUE(ps_gvar_is_static_storage(scalar_object));
  ASSERT_TRUE(scalar_object->has_init);
  ASSERT_EQ(31, scalar_object->init_val);
  ASSERT_TRUE(scalar_pointer->init_symbol != NULL);
  ASSERT_EQ(16, scalar_pointer->init_symbol_len);
  ASSERT_TRUE(memcmp(
      scalar_pointer->init_symbol, "__compound_lit_0", 16) == 0);
  ASSERT_EQ(0, scalar_pointer->init_symbol_offset);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  psx_parsed_initializer_t *array_initializer =
      &item.value.declaration.initializers[0];
  ASSERT_TRUE(array_initializer->has_initializer);
  ASSERT_EQ(PSX_DECL_INIT_EXPR, array_initializer->kind);
  const node_t *array_address = array_initializer->value;
  ASSERT_TRUE(array_address != NULL);
  ASSERT_EQ(ND_ADDRESS_OF, array_address->kind);
  const node_t *array_subscript = array_address->lhs;
  ASSERT_TRUE(array_subscript != NULL);
  ASSERT_EQ(ND_SUBSCRIPT, array_subscript->kind);
  const node_t *array_compound = array_subscript->lhs;
  ASSERT_TRUE(array_compound != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, array_compound->kind);
  ASSERT_EQ(PSX_SCOPE_ID_TRANSLATION_UNIT,
            ((const node_compound_literal_t *)array_compound)
                ->type_name.scope_seq);
  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  global_var_t *array_pointer =
      find_test_global_var(test_suite_session, "__phase_array", 13);
  global_var_t *array_object =
      find_test_global_var(test_suite_session, "__compound_lit_1", 16);
  ASSERT_TRUE(array_pointer != NULL);
  ASSERT_TRUE(array_object != NULL);
  ASSERT_TRUE(ps_gvar_is_static_storage(array_object));
  ASSERT_TRUE(array_object->has_init);
  ASSERT_EQ(2, array_object->init_count);
  ASSERT_EQ(35, ps_gvar_init_slot_view(array_object, 0).value);
  ASSERT_EQ(36, ps_gvar_init_slot_view(array_object, 1).value);
  ASSERT_TRUE(array_pointer->init_symbol != NULL);
  ASSERT_EQ(16, array_pointer->init_symbol_len);
  ASSERT_TRUE(memcmp(
      array_pointer->init_symbol, "__compound_lit_1", 16) == 0);
  ASSERT_EQ(4, array_pointer->init_symbol_offset);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(PSX_TOPLEVEL_ITEM_DECLARATION, item.kind);
  psx_parsed_initializer_t *member_initializer =
      &item.value.declaration.initializers[0];
  ASSERT_TRUE(member_initializer->has_initializer);
  ASSERT_EQ(PSX_DECL_INIT_EXPR, member_initializer->kind);
  const node_t *member_address = member_initializer->value;
  ASSERT_TRUE(member_address != NULL);
  ASSERT_EQ(ND_ADDRESS_OF, member_address->kind);
  const node_t *member_access = member_address->lhs;
  ASSERT_TRUE(member_access != NULL);
  ASSERT_EQ(ND_MEMBER_ACCESS, member_access->kind);
  const node_t *member_compound = member_access->lhs;
  ASSERT_TRUE(member_compound != NULL);
  ASSERT_EQ(ND_COMPOUND_LITERAL, member_compound->kind);
  ASSERT_EQ(PSX_SCOPE_ID_TRANSLATION_UNIT,
            ((const node_compound_literal_t *)member_compound)
                ->type_name.scope_seq);
  apply_test_toplevel_declaration(test_suite_session, &item.value.declaration);
  global_var_t *member_pointer =
      find_test_global_var(test_suite_session, "__phase_member", 14);
  global_var_t *member_object =
      find_test_global_var(test_suite_session, "__compound_lit_2", 16);
  ASSERT_TRUE(member_pointer != NULL);
  ASSERT_TRUE(member_object != NULL);
  ASSERT_TRUE(ps_gvar_is_static_storage(member_object));
  ASSERT_TRUE(member_object->has_init);
  ASSERT_EQ(2, member_object->init_count);
  ASSERT_EQ(33, ps_gvar_init_slot_view(member_object, 0).value);
  ASSERT_EQ(34, ps_gvar_init_slot_view(member_object, 1).value);
  ASSERT_TRUE(member_pointer->init_symbol != NULL);
  ASSERT_EQ(16, member_pointer->init_symbol_len);
  ASSERT_TRUE(memcmp(
      member_pointer->init_symbol, "__compound_lit_2", 16) == 0);
  ASSERT_EQ(4, member_pointer->init_symbol_offset);
  ps_dispose_toplevel_declaration_syntax(&item.value.declaration);

  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_TRUE(parse_raw_function_item(test_suite_session, &stream, &item));
  ps_dispose_function_definition_syntax(
      &item.value.function_header);
  ps_parser_stream_end(&stream);
}

static void test_toplevel_point_of_declaration_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_toplevel_point_of_declaration_boundary...\n");
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __point_self = sizeof __point_self; "
      "int __point_first = sizeof __point_first, "
      "__point_second = sizeof __point_second; "
      "int main(void) { return 0; }"));
  global_var_t *self = find_test_global_var(test_suite_session, "__point_self", 12);
  global_var_t *first = find_test_global_var(test_suite_session, "__point_first", 13);
  global_var_t *second = find_test_global_var(test_suite_session, "__point_second", 14);
  ASSERT_TRUE(self != NULL);
  ASSERT_TRUE(first != NULL);
  ASSERT_TRUE(second != NULL);
  ASSERT_EQ(4, self->init_val);
  ASSERT_EQ(4, first->init_val);
  ASSERT_EQ(4, second->init_val);
}

static void assert_toplevel_syntax_kind(
    ag_compilation_session_t *test_suite_session,
    const char *source, psx_toplevel_item_kind_t expected_kind,
    int expected_declarator_count) {
  reset_test_translation_unit_state(test_suite_session);
  psx_parser_stream_t stream = {0};
  begin_test_parser_stream(test_suite_session,
      &stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)source));
  psx_parsed_toplevel_item_t item;
  ASSERT_TRUE(ps_parse_next_toplevel_item(&stream, &item));
  ASSERT_EQ(expected_kind, item.kind);
  if (item.kind == PSX_TOPLEVEL_ITEM_DECLARATION) {
    ASSERT_EQ(expected_declarator_count,
              item.value.declaration.declarator_count);
    ps_dispose_toplevel_declaration_syntax(&item.value.declaration);
  } else if (item.kind == PSX_TOPLEVEL_ITEM_FUNCTION_HEADER) {
    ASSERT_TRUE(item.value.function_header.declarator.identifier != NULL);
    ps_dispose_function_definition_syntax(
        &item.value.function_header);
  }
  ps_parser_stream_end(&stream);
}

static void test_toplevel_single_parse_classification_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_toplevel_single_parse_classification_boundary...\n");
  assert_toplevel_syntax_kind(test_suite_session,
      "int (*proto(void))(int);", PSX_TOPLEVEL_ITEM_DECLARATION, 1);
  assert_toplevel_syntax_kind(test_suite_session,
      "int (*definition(void))(int) { return 0; }",
      PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, 0);
  assert_toplevel_syntax_kind(test_suite_session,
      "struct R { int x; } (*tag_proto(void))[3];",
      PSX_TOPLEVEL_ITEM_DECLARATION, 1);
  assert_toplevel_syntax_kind(test_suite_session,
      "struct R { int x; } (*tag_definition(void))[3] { return 0; }",
      PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, 0);
  assert_toplevel_syntax_kind(test_suite_session,
      "int (parenthesized)(void) { return 0; }",
      PSX_TOPLEVEL_ITEM_FUNCTION_HEADER, 0);
  assert_toplevel_syntax_kind(test_suite_session,
      "int first(int), second(int), object;",
      PSX_TOPLEVEL_ITEM_DECLARATION, 3);
}

static void test_frontend_stream_lifecycle_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_frontend_stream_lifecycle_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_test_program_hir_from(test_suite_session,
      tk_tokenize_ctx(test_tokenizer(test_suite_session),
          (char *)"int __stream_previous(void) { return 0; }")));
  ASSERT_TRUE(test_has_function_type_in(test_semantic_context(test_suite_session),
                  (char *)"__stream_previous", 17));

  psx_parser_stream_t parser_stream = {0};
  begin_test_parser_stream(test_suite_session,
      &parser_stream, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)""));
  ASSERT_TRUE(test_has_function_type_in(test_semantic_context(test_suite_session),
                  (char *)"__stream_previous", 17));
  ps_parser_stream_end(&parser_stream);

  ag_compilation_session_t session_context;
  const ag_target_info_t *session_target =
      ag_compilation_session_target(test_suite_session);
  ASSERT_TRUE(ag_compilation_session_init(
      &session_context, session_target));
  ag_compilation_session_t *outer_session = test_suite_session;
  psx_frontend_stream_t frontend_stream = {0};
  ASSERT_TRUE(!psx_frontend_stream_begin(
      &frontend_stream, NULL, NULL, tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"")));
  psx_frontend_function_t frontend_function;
  ASSERT_TRUE(!psx_frontend_next_function(
      &frontend_stream, &frontend_function));
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
      tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"int __incomplete_session(void);")));
  session_context.diagnostic_context = session_diagnostic_context;
  ASSERT_TRUE(ag_compilation_session_is_complete(&session_context));

  ASSERT_TRUE(psx_frontend_stream_begin(
      &frontend_stream, &session_context, NULL,
      tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)
          "int __stream_explicit(void) { return 0; }")));
  ASSERT_TRUE(psx_frontend_next_function(
      &frontend_stream, &frontend_function));
  ASSERT_TRUE(frontend_function.hir_root != PSX_HIR_NODE_ID_INVALID);
  ASSERT_TRUE(psx_hir_module_lookup(
                  ag_compilation_session_hir_module(&session_context),
                  frontend_function.hir_root) != NULL);
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
                  session_context.semantic_context,
                  (char *)"__stream_explicit", 17) != NULL);
  ASSERT_TRUE(test_has_function_type_in(session_context.semantic_context,
                  (char *)"__stream_explicit", 17));
  ASSERT_TRUE(!test_has_function_type_in(session_context.semantic_context,
                  (char *)"__stream_previous", 17));
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
                  outer_session->semantic_context,
                  (char *)"__stream_previous", 17) != NULL);
  ag_compilation_session_t nested_context;
  ASSERT_TRUE(ag_compilation_session_init(
      &nested_context, session_target));
  psx_frontend_stream_t nested_stream = {0};
  ASSERT_TRUE(psx_frontend_stream_begin(
      &nested_stream, &nested_context, NULL,
      tk_tokenize_ctx(
          &nested_context.tokenizer,
          (char *)"int __stream_nested(void) { return 0; }")));
  ASSERT_TRUE(psx_frontend_next_function(
      &nested_stream, &frontend_function));
  ASSERT_TRUE(psx_frontend_stream_end(&nested_stream));
  ASSERT_TRUE(test_has_function_type_in(
                  nested_context.semantic_context,
                  (char *)"__stream_nested", 15));
  ASSERT_TRUE(!test_has_function_type_in(
                  session_context.semantic_context,
                  (char *)"__stream_nested", 15));
  ASSERT_TRUE(psx_frontend_stream_end(&frontend_stream));
  ASSERT_TRUE(!psx_frontend_next_function(
      &frontend_stream, &frontend_function));
  ASSERT_TRUE(ag_compilation_session_dispose(&nested_context));
  ASSERT_TRUE(ag_compilation_session_dispose(&session_context));
}

static void test_complex_initializer_semantic_lowering_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_complex_initializer_semantic_lowering_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int complex_initializer_probe(void) { "
      "  double _Complex z = {3.0, 4.0}; "
      "  float _Complex f = {1.0f, 2.0f}; "
      "  int scalar_array[3] = {7}; "
      "  int designated_array[3] = {[1] = 8, 9}; "
      "  return (int)__real__ z + (int)__imag__ z "
      "      + (int)__real__ f + (int)__imag__ f "
      "      + scalar_array[0] + scalar_array[1] "
      "      + designated_array[0] + designated_array[1] "
      "      + designated_array[2]; "
      "}"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);
  ASSERT_TRUE(hir != NULL);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));

  const char *names[] = {
      "z", "f", "scalar_array", "designated_array",
  };
  lvar_t *locals[4] = {0};
  for (size_t i = 0;
       i < sizeof(names) / sizeof(names[0]); i++) {
    const psx_scope_declaration_t *declaration =
        find_test_scope_declaration(
            test_scope_graph(test_suite_session), names[i],
            PSX_DECL_LOCAL_OBJECT, 0);
    ASSERT_TRUE(declaration != NULL);
    locals[i] = (lvar_t *)declaration->payload;
    ASSERT_TRUE(locals[i] != NULL);
  }

  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[0]), &shape));
  ASSERT_EQ(PSX_TYPE_COMPLEX, shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE, shape.floating_kind);
  ASSERT_EQ(16, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_lvar_decl_type_id(locals[0]),
                    data_layout));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[1]), &shape));
  ASSERT_EQ(PSX_TYPE_COMPLEX, shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_FLOAT, shape.floating_kind);
  ASSERT_EQ(8, psx_type_layout_sizeof(
                   types, record_layouts,
                   ps_lvar_decl_type_id(locals[1]),
                   data_layout));
  for (size_t i = 2; i < 4; i++) {
    ASSERT_TRUE(psx_semantic_type_table_describe(
        types, ps_lvar_decl_type_id(locals[i]), &shape));
    ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
    ASSERT_EQ(3, shape.array_len);
    ASSERT_EQ(12, psx_type_layout_sizeof(
                      types, record_layouts,
                      ps_lvar_decl_type_id(locals[i]),
                      data_layout));
  }

  int declaration_initializer_count = 0;
  int complex_part_assignment_count = 0;
  int array_assignment_count = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node = psx_hir_module_lookup(
        hir, (psx_hir_node_id_t)i);
    if (!psx_hir_node_is_declaration_initializer(node))
      continue;
    declaration_initializer_count++;
    const psx_hir_node_t *lhs = NULL;
    for (size_t child = 0;
         child < psx_hir_node_child_count(node); child++) {
      if (psx_hir_node_child_edge_at(node, child) ==
          PSX_HIR_EDGE_LHS)
        lhs = psx_hir_module_lookup(
            hir, psx_hir_node_child_at(node, child));
    }
    if (!lhs || psx_hir_node_kind(lhs) != PSX_HIR_LOCAL)
      continue;
    size_t name_length = 0;
    const char *name = psx_hir_node_name(lhs, &name_length);
    if (!name) continue;
    if ((name_length == 1 && (name[0] == 'z' || name[0] == 'f')))
      complex_part_assignment_count++;
    if ((name_length == 12 &&
         memcmp(name, "scalar_array", 12) == 0) ||
        (name_length == 16 &&
         memcmp(name, "designated_array", 16) == 0))
      array_assignment_count++;
  }
  ASSERT_EQ(10, declaration_initializer_count);
  ASSERT_EQ(4, complex_part_assignment_count);
  ASSERT_EQ(6, array_assignment_count);

  psx_hir_node_id_t root_id =
      psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));
  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = types,
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = record_layouts,
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(
              test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 10);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_LOAD_FP_IMM) >= 4);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
}

static void test_local_declaration_storage_plan_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_local_declaration_storage_plan_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  psx_semantic_type_table_t *types =
      (psx_semantic_type_table_t *)
          ps_ctx_semantic_type_table_in(
              test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);

  psx_qual_type_t integer =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_INT, 0, 0);
  psx_qual_type_t row =
      psx_semantic_type_table_intern_array_of(
          types, integer, 3, 0);
  psx_qual_type_t matrix =
      psx_semantic_type_table_intern_array_of(
          types, row, 2, 0);
  psx_local_storage_plan_t plan = {0};
  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts, matrix.type_id,
      data_layout, &plan));
  ASSERT_EQ(24, plan.storage_size);
  ASSERT_EQ(4, plan.alignment);

  psx_qual_type_t pointer =
      psx_semantic_type_table_intern_pointer_to(
          types, integer);
  psx_qual_type_t pointers =
      psx_semantic_type_table_intern_array_of(
          types, pointer, 3, 0);
  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts, pointers.type_id,
      data_layout, &plan));
  ASSERT_EQ(24, plan.storage_size);

  psx_qual_type_t incomplete =
      psx_semantic_type_table_intern_array_of(
          types, integer, 0, 0);
  ASSERT_TRUE(!psx_plan_local_storage_for_type_id(
      types, record_layouts, incomplete.type_id,
      data_layout, &plan));
  psx_qual_type_t completed_incomplete = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  ASSERT_TRUE(psx_resolve_completed_incomplete_array_qual_type_in(
      test_semantic_context(test_suite_session), incomplete,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 5,
      },
      &completed_incomplete));
  psx_type_shape_t completed_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, completed_incomplete.type_id,
      &completed_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, completed_shape.kind);
  ASSERT_EQ(5, completed_shape.array_len);
  ASSERT_EQ(20, psx_type_layout_sizeof(
                    types, record_layouts,
                    completed_incomplete.type_id,
                    data_layout));
  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts,
      completed_incomplete.type_id,
      data_layout, &plan));

  psx_qual_type_t partial_flat_matrix =
      psx_semantic_type_table_intern_array_of(
          types, row, 0, 0);
  psx_qual_type_t completed_partial_flat_matrix = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  ASSERT_TRUE(psx_resolve_completed_incomplete_array_qual_type_in(
      test_semantic_context(test_suite_session), partial_flat_matrix,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 5,
          .entries_initialize_outer_elements = 0,
      },
      &completed_partial_flat_matrix));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, completed_partial_flat_matrix.type_id,
      &completed_shape));
  ASSERT_EQ(2, completed_shape.array_len);
  ASSERT_EQ(24, psx_type_layout_sizeof(
                    types, record_layouts,
                    completed_partial_flat_matrix.type_id,
                    data_layout));

  psx_qual_type_t nested_matrix =
      psx_semantic_type_table_intern_array_of(
          types, row, 0, 0);
  psx_qual_type_t completed_nested_matrix = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  ASSERT_TRUE(psx_resolve_completed_incomplete_array_qual_type_in(
      test_semantic_context(test_suite_session), nested_matrix,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 2,
          .entries_initialize_outer_elements = 1,
      },
      &completed_nested_matrix));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, completed_nested_matrix.type_id,
      &completed_shape));
  ASSERT_EQ(2, completed_shape.array_len);
  ASSERT_EQ(24, psx_type_layout_sizeof(
                    types, record_layouts,
                    completed_nested_matrix.type_id,
                    data_layout));
  psx_qual_type_t vla =
      psx_semantic_type_table_intern_array_of(
          types, integer, 0, 1);
  ASSERT_TRUE(!psx_plan_local_storage_for_type_id(
      types, record_layouts, vla.type_id,
      data_layout, &plan));

  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts, pointer.type_id,
      data_layout, &plan));
  ASSERT_EQ(8, plan.storage_size);
  ASSERT_EQ(8, plan.alignment);
  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts, integer.type_id,
      data_layout, &plan));
  ASSERT_EQ(4, plan.storage_size);

  reset_test_locals(test_suite_session);
  lvar_t *lowered = lower_complete_local_object(
      &(psx_local_object_request_t){
          .local_registry = test_local_registry(test_suite_session),
          .lowering_context = test_lowering_context(test_suite_session),
          .name = (char *)"matrix",
          .name_len = 6,
          .type = matrix,
          .requested_alignment = 32,
      });
  ASSERT_TRUE(lowered != NULL);
  ASSERT_EQ(24, ps_lvar_frame_storage_size(lowered));
  ASSERT_TRUE(ps_lvar_is_array(lowered));
  ASSERT_EQ(0, ps_lvar_offset(lowered) % 32);
  ASSERT_EQ(32, ps_lvar_align_bytes(lowered));
  ASSERT_EQ(matrix.type_id,
            ps_lvar_decl_type_id(lowered));
  ASSERT_EQ(matrix.qualifiers,
            ps_lvar_decl_qual_type(lowered).qualifiers);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(lowered),
      &completed_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, completed_shape.kind);
  ASSERT_EQ(2, completed_shape.array_len);
  psx_qual_type_t stored_row =
      psx_semantic_type_table_base(
          types, ps_lvar_decl_type_id(lowered));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, stored_row.type_id, &completed_shape));
  ASSERT_EQ(3, completed_shape.array_len);
  ASSERT_EQ(24, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_lvar_decl_type_id(lowered),
                    data_layout));
  ASSERT_EQ(lowered, find_test_local_var_in(
                         test_local_registry(test_suite_session),
                         (char *)"matrix", 6));

  reset_test_locals(test_suite_session);
  lvar_t *declared = declare_incomplete_local_object(
      &(psx_local_object_request_t){
          .local_registry = test_local_registry(test_suite_session),
          .lowering_context = test_lowering_context(test_suite_session),
          .name = (char *)"deferred",
          .name_len = 8,
          .type = incomplete,
      });
  ASSERT_TRUE(declared != NULL);
  ASSERT_EQ(incomplete.type_id,
            ps_lvar_decl_type_id(declared));
  ASSERT_EQ(0, ps_lvar_frame_storage_size(declared));
  ASSERT_EQ(declared,
            find_test_local_var_in(
                test_local_registry(test_suite_session),
                (char *)"deferred", 8));
  ASSERT_TRUE(!ps_local_registry_complete_array_qual_type(
      test_local_registry(test_suite_session), declared, integer));
  psx_qual_type_t floating =
      psx_semantic_type_table_intern_floating(
          types, PSX_FLOATING_KIND_FLOAT, 0);
  psx_qual_type_t float_array =
      psx_semantic_type_table_intern_array_of(
          types, floating, 3, 0);
  ASSERT_TRUE(!ps_local_registry_complete_array_qual_type(
      test_local_registry(test_suite_session), declared, float_array));
  psx_qual_type_t complete_deferred_type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  ASSERT_TRUE(psx_resolve_completed_incomplete_array_qual_type_in(
      test_semantic_context(test_suite_session), incomplete,
      &(psx_incomplete_array_resolution_t){
          .initializer_count = 3,
      },
      &complete_deferred_type));
  psx_type_id_t complete_type_id = complete_deferred_type.type_id;
  ASSERT_TRUE(complete_type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(complete_type_id != incomplete.type_id);
  ASSERT_TRUE(complete_declared_local_object(
      declared,
      &(psx_local_object_request_t){
          .local_registry = test_local_registry(test_suite_session),
          .lowering_context = test_lowering_context(test_suite_session),
          .name = (char *)"deferred",
          .name_len = 8,
          .type = complete_deferred_type,
      }));
  ASSERT_EQ(12, ps_lvar_frame_storage_size(declared));
  ASSERT_EQ(complete_type_id, ps_lvar_decl_type_id(declared));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(declared),
      &completed_shape));
  ASSERT_EQ(3, completed_shape.array_len);
  ASSERT_TRUE(!ps_local_registry_complete_array_qual_type(
      test_local_registry(test_suite_session), declared,
      complete_deferred_type));

  reset_test_locals(test_suite_session);
  psx_qual_type_t qualified_local_type = integer;
  qualified_local_type.qualifiers =
      PSX_TYPE_QUALIFIER_CONST |
      PSX_TYPE_QUALIFIER_VOLATILE;
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, qualified_local_type));
  lvar_t *qualified_local = lower_complete_local_object(
      &(psx_local_object_request_t){
          .local_registry = test_local_registry(test_suite_session),
          .lowering_context = test_lowering_context(test_suite_session),
          .name = (char *)"qualified_local",
          .name_len = 15,
          .type = qualified_local_type,
      });
  ASSERT_TRUE(qualified_local != NULL);
  ASSERT_EQ(qualified_local_type.type_id,
            ps_lvar_decl_qual_type(qualified_local).type_id);
  ASSERT_EQ(qualified_local_type.qualifiers,
            ps_lvar_decl_qual_type(qualified_local).qualifiers);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_VOLATILE,
            ps_lvar_decl_qual_type(qualified_local).qualifiers);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int local_storage_pipeline(void) { "
      "  int pipeline_deferred[] = {1, 2, 3}; "
      "  int *pointers[] = {0}; "
      "  return sizeof(pipeline_deferred) == 12 "
      "      && sizeof(pointers) == 8; "
      "}"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(
          test_suite_session);
  ASSERT_TRUE(hir != NULL);
  const psx_scope_declaration_t *deferred_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "pipeline_deferred",
          PSX_DECL_LOCAL_OBJECT, 0);
  const psx_scope_declaration_t *pointers_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "pointers",
          PSX_DECL_LOCAL_OBJECT, 0);
  ASSERT_TRUE(deferred_declaration != NULL);
  ASSERT_TRUE(pointers_declaration != NULL);
  lvar_t *pipeline_deferred =
      (lvar_t *)deferred_declaration->payload;
  lvar_t *pipeline_pointers =
      (lvar_t *)pointers_declaration->payload;
  ASSERT_TRUE(pipeline_deferred != NULL);
  ASSERT_TRUE(pipeline_pointers != NULL);
  psx_type_shape_t pipeline_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types,
      ps_lvar_decl_type_id(pipeline_deferred),
      &pipeline_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, pipeline_shape.kind);
  ASSERT_EQ(3, pipeline_shape.array_len);
  ASSERT_EQ(12, ps_lvar_frame_storage_size(
                    pipeline_deferred));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types,
      ps_lvar_decl_type_id(pipeline_pointers),
      &pipeline_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, pipeline_shape.kind);
  ASSERT_EQ(1, pipeline_shape.array_len);
  ASSERT_EQ(8, ps_lvar_frame_storage_size(
                   pipeline_pointers));
}

static void test_target_type_layout_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_target_type_layout_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ag_target_info_t host = ag_target_info_host();
  ag_target_info_t wasm = ag_target_info_wasm32();
  ASSERT_TRUE(ag_target_info_is_valid(&host));
  ASSERT_TRUE(ag_target_info_is_valid(&wasm));
  ASSERT_TRUE(!ag_target_info_equal(&host, &wasm));
  ASSERT_TRUE(ag_data_layout_is_valid(
      ag_target_info_data_layout(&host)));
  ASSERT_TRUE(ag_data_layout_is_valid(
      ag_target_info_data_layout(&wasm)));

  const ir_abi_target_policy_t *host_abi_policy =
      ir_abi_target_policy_for(&host);
  const ir_abi_target_policy_t *wasm_abi_policy =
      ir_abi_target_policy_for(&wasm);
  ASSERT_TRUE(host_abi_policy != NULL);
  ASSERT_TRUE(wasm_abi_policy != NULL);
  ASSERT_TRUE(
      !ir_abi_policy_parameter_aggregate_is_indirect(
          host_abi_policy, 16));
  ASSERT_TRUE(
      ir_abi_policy_parameter_aggregate_is_indirect(
          host_abi_policy, 17));
  ASSERT_TRUE(
      !ir_abi_policy_parameter_aggregate_is_indirect(
          wasm_abi_policy, 16));
  ASSERT_TRUE(
      ir_abi_policy_parameter_aggregate_is_indirect(
          wasm_abi_policy, 17));

  ASSERT_TRUE(!ag_target_info_is_valid(NULL));
  ASSERT_EQ(0, test_target_pointer_size(NULL));
  ASSERT_EQ(0, test_target_pointer_alignment(NULL));
  ASSERT_EQ(AG_TARGET_CALL_ABI_INVALID,
            ag_target_info_call_abi(NULL));
  ASSERT_EQ(0, test_target_scalar_size(
                   NULL, AG_TARGET_SCALAR_INT));
  ASSERT_EQ(0, test_target_scalar_alignment(
                   NULL, AG_TARGET_SCALAR_INT));

  ag_target_info_t alternate_host_abi = host;
  alternate_host_abi.call_abi = AG_TARGET_CALL_ABI_WASM32;
  ASSERT_TRUE(ag_target_info_is_valid(&alternate_host_abi));
  ASSERT_TRUE(!ag_target_info_equal(
      &host, &alternate_host_abi));
  ASSERT_TRUE(ag_data_layout_equal(
      ag_target_info_data_layout(&host),
      ag_target_info_data_layout(&alternate_host_abi)));

  ag_target_info_t incomplete_target = host;
  incomplete_target.data_layout
      .scalar[AG_TARGET_SCALAR_INT].alignment = 0;
  ASSERT_TRUE(!ag_target_info_is_valid(&incomplete_target));
  ag_compilation_session_t invalid_session;
  ASSERT_TRUE(ag_compilation_session_create(NULL) == NULL);
  ASSERT_TRUE(!ag_compilation_session_init(
      &invalid_session, NULL));
  ASSERT_TRUE(!ag_compilation_session_init(
      &invalid_session, &incomplete_target));

  psx_semantic_type_table_t *types =
      (psx_semantic_type_table_t *)
          ps_ctx_semantic_type_table_in(
              test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *session_layouts =
      ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session));
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(session_layouts != NULL);

  psx_qual_type_t integer =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_INT, 0, 0);
  psx_qual_type_t signed_long =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_LONG, 0, 0);
  psx_qual_type_t unsigned_int =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_INT, 1, 0);
  psx_qual_type_t unsigned_short =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_SHORT, 1, 0);
  psx_qual_type_t float_complex =
      psx_semantic_type_table_intern_floating(
          types, PSX_FLOATING_KIND_FLOAT, 1);
  psx_qual_type_t pointer =
      psx_semantic_type_table_intern_pointer_to(
          types, integer);
  psx_qual_type_t pointer_array =
      psx_semantic_type_table_intern_array_of(
          types, pointer, 3, 0);

  ASSERT_EQ(8, psx_type_layout_sizeof(
                   types, session_layouts, pointer.type_id,
                   ag_target_info_data_layout(&host)));
  ASSERT_EQ(8, psx_type_layout_alignof(
                   types, session_layouts, pointer.type_id,
                   ag_target_info_data_layout(&host)));
  ASSERT_EQ(4, psx_type_layout_sizeof(
                   types, session_layouts, pointer.type_id,
                   ag_target_info_data_layout(&wasm)));
  ASSERT_EQ(4, psx_type_layout_alignof(
                   types, session_layouts, pointer.type_id,
                   ag_target_info_data_layout(&wasm)));
  ASSERT_EQ(24, psx_type_layout_sizeof(
                    types, session_layouts,
                    pointer_array.type_id,
                    ag_target_info_data_layout(&host)));
  ASSERT_EQ(12, psx_type_layout_sizeof(
                    types, session_layouts,
                    pointer_array.type_id,
                    ag_target_info_data_layout(&wasm)));

  ag_target_info_t wide_pointer_target = host;
  wide_pointer_target.data_layout.pointer_size = 16;
  wide_pointer_target.data_layout.pointer_alignment = 16;
  ASSERT_EQ(16, psx_type_layout_sizeof(
                    types, session_layouts, pointer.type_id,
                    ag_target_info_data_layout(
                        &wide_pointer_target)));
  ASSERT_EQ(16, psx_type_layout_alignof(
                    types, session_layouts, pointer.type_id,
                    ag_target_info_data_layout(
                        &wide_pointer_target)));
  ASSERT_EQ(48, psx_type_layout_sizeof(
                    types, session_layouts,
                    pointer_array.type_id,
                    ag_target_info_data_layout(
                        &wide_pointer_target)));

  ag_target_info_t narrow_int_target = host;
  narrow_int_target.data_layout
      .scalar[AG_TARGET_SCALAR_INT] =
      (ag_target_scalar_layout_t){2, 2};
  ASSERT_EQ(2, psx_type_layout_sizeof(
                   types, session_layouts, integer.type_id,
                   ag_target_info_data_layout(
                       &narrow_int_target)));
  ASSERT_EQ(2, psx_type_layout_alignof(
                   types, session_layouts, integer.type_id,
                   ag_target_info_data_layout(
                       &narrow_int_target)));

  char signature[64];
  ASSERT_EQ(3, psx_format_canonical_type_signature(
                   types, integer,
                   ag_target_info_data_layout(&host),
                   signature, sizeof(signature)));
  ASSERT_TRUE(strcmp("i32", signature) == 0);
  ASSERT_EQ(3, psx_format_canonical_type_signature(
                   types, integer,
                   ag_target_info_data_layout(
                       &narrow_int_target),
                   signature, sizeof(signature)));
  ASSERT_TRUE(strcmp("i16", signature) == 0);

  ag_target_info_t packed_complex_target = host;
  packed_complex_target.data_layout
      .scalar[AG_TARGET_SCALAR_FLOAT_COMPLEX] =
      (ag_target_scalar_layout_t){8, 2};
  ASSERT_EQ(8, psx_type_layout_sizeof(
                   types, session_layouts,
                   float_complex.type_id,
                   ag_target_info_data_layout(
                       &packed_complex_target)));
  ASSERT_EQ(2, psx_type_layout_alignof(
                   types, session_layouts,
                   float_complex.type_id,
                   ag_target_info_data_layout(
                       &packed_complex_target)));

  ag_target_info_t equal_width_integer_target = host;
  equal_width_integer_target.data_layout
      .scalar[AG_TARGET_SCALAR_LONG] =
      (ag_target_scalar_layout_t){4, 4};
  ag_target_info_t wide_short_target = host;
  wide_short_target.data_layout
      .scalar[AG_TARGET_SCALAR_SHORT] =
      (ag_target_scalar_layout_t){4, 4};
  ASSERT_EQ(2, psx_type_layout_character_code_unit_width(
                   types, unsigned_short.type_id,
                   ag_target_info_data_layout(&host)));
  ASSERT_EQ(4, psx_type_layout_character_code_unit_width(
                   types, unsigned_short.type_id,
                   ag_target_info_data_layout(&wide_short_target)));
  ASSERT_EQ(4, psx_type_layout_character_code_unit_width(
                   types, integer.type_id,
                   ag_target_info_data_layout(&host)));
  ASSERT_EQ(2, psx_type_layout_character_code_unit_width(
                   types, integer.type_id,
                   ag_target_info_data_layout(&narrow_int_target)));
  ir_mir_type_context_t mir_type_context = {
      .semantic_types = types,
      .record_layouts = session_layouts,
      .data_layout = ag_target_info_data_layout(&host),
  };
  ir_mir_type_info_t signed_long_mir =
      ir_mir_classify_type_id(
          &mir_type_context, signed_long.type_id);
  ir_mir_type_info_t unsigned_int_mir =
      ir_mir_classify_type_id(
          &mir_type_context, unsigned_int.type_id);
  ir_mir_type_info_t unsigned_short_mir =
      ir_mir_classify_type_id(
          &mir_type_context, unsigned_short.type_id);
  ASSERT_TRUE(!ir_mir_usual_arithmetic_result_is_unsigned(
      signed_long_mir, unsigned_int_mir,
      ag_target_info_data_layout(&host)));
  ASSERT_TRUE(ir_mir_usual_arithmetic_result_is_unsigned(
      signed_long_mir, unsigned_int_mir,
      ag_target_info_data_layout(
          &equal_width_integer_target)));
  ASSERT_TRUE(!ir_mir_integer_promotion_is_unsigned(
      unsigned_short_mir,
      ag_target_info_data_layout(&host)));
  ASSERT_TRUE(ir_mir_integer_promotion_is_unsigned(
      unsigned_short_mir,
      ag_target_info_data_layout(&wide_short_target)));

  psx_record_decl_t record = {
      .record_id = 0xfaceu,
      .record_kind = PSX_TYPE_STRUCT,
      .tag_name = (char *)"__TargetRecord",
      .tag_len = 14,
      .is_complete = 1,
  };
  ASSERT_TRUE(psx_record_decl_table_define(
      (psx_record_decl_table_t *)
          ps_ctx_record_decl_table_in(
              test_semantic_context(test_suite_session)),
      &record));
  psx_qual_type_t record_type =
      psx_semantic_type_table_intern_record(
          types, record.record_id);
  ASSERT_TRUE(record_type.type_id !=
              PSX_TYPE_ID_INVALID);

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
      record_layouts, record.record_id,
      ag_target_info_data_layout(&host),
      16, 8, host_members, 2));
  ASSERT_TRUE(psx_record_layout_table_define(
      record_layouts, record.record_id,
      ag_target_info_data_layout(&wasm),
      8, 4, wasm_members, 2));

  ASSERT_EQ(16, psx_type_layout_sizeof(
                    types, record_layouts,
                    record_type.type_id,
                    ag_target_info_data_layout(&host)));
  ASSERT_EQ(8, psx_type_layout_sizeof(
                   types, record_layouts,
                   record_type.type_id,
                   ag_target_info_data_layout(&wasm)));
  ASSERT_EQ(8, psx_type_layout_alignof(
                   types, record_layouts,
                   record_type.type_id,
                   ag_target_info_data_layout(&host)));
  ASSERT_EQ(4, psx_type_layout_alignof(
                   types, record_layouts,
                   record_type.type_id,
                   ag_target_info_data_layout(&wasm)));

  const psx_record_layout_t *host_record_layout =
      psx_record_layout_table_lookup(
          record_layouts, record.record_id,
          ag_target_info_data_layout(&host));
  const psx_record_layout_t *alternate_record_layout =
      psx_record_layout_table_lookup(
          record_layouts, record.record_id,
          ag_target_info_data_layout(
              &alternate_host_abi));
  const psx_record_layout_t *wasm_record_layout =
      psx_record_layout_table_lookup(
          record_layouts, record.record_id,
          ag_target_info_data_layout(&wasm));
  ASSERT_TRUE(host_record_layout != NULL);
  ASSERT_TRUE(alternate_record_layout ==
              host_record_layout);
  ASSERT_TRUE(wasm_record_layout != NULL);
  ASSERT_EQ(8, psx_record_layout_member(
                   host_record_layout, 1)->offset);
  ASSERT_EQ(4, psx_record_layout_member(
                   wasm_record_layout, 1)->offset);

  ir_abi_type_context_t host_abi = {
      .semantic_types = types,
      .record_layouts = record_layouts,
      .target = &host,
  };
  ir_abi_type_context_t wasm_abi = host_abi;
  wasm_abi.target = &wasm;
  ir_module_t *abi_probe_module = ir_module_new();
  ASSERT_TRUE(abi_probe_module != NULL);
  ir_func_t *abi_probe_function = ir_func_new(
      abi_probe_module, "record_result", 13);
  ASSERT_TRUE(abi_probe_function != NULL);
  ASSERT_TRUE(ir_function_type_set(
      &abi_probe_function->function_type,
      PSX_TYPE_ID_INVALID, record_type, NULL, 0, 0, 1));
  ir_abi_module_t *host_lowered =
      ir_abi_lower_module(&host_abi, abi_probe_module);
  ir_abi_module_t *wasm_lowered =
      ir_abi_lower_module(&wasm_abi, abi_probe_module);
  const ir_abi_signature_t *host_signature =
      ir_abi_function_signature(
          host_lowered, abi_probe_function);
  const ir_abi_signature_t *wasm_signature =
      ir_abi_function_signature(
          wasm_lowered, abi_probe_function);
  ASSERT_TRUE(host_signature != NULL);
  ASSERT_TRUE(wasm_signature != NULL);
  ASSERT_TRUE(ir_abi_signature_result_is_indirect(
      host_signature));
  ASSERT_EQ(16, ir_abi_signature_result_source_size(
                    host_signature));
  ASSERT_TRUE(ir_abi_signature_result_is_direct_aggregate(
      wasm_signature));
  ASSERT_EQ(8, ir_abi_signature_result_source_size(
                   wasm_signature));
  ir_abi_module_free(host_lowered);
  ir_abi_module_free(wasm_lowered);
  ir_module_free(abi_probe_module);

  psx_local_storage_plan_t local = {0};
  ASSERT_TRUE(psx_plan_local_storage_for_type_id(
      types, record_layouts, pointer_array.type_id,
      ag_target_info_data_layout(&wasm), &local));
  ASSERT_EQ(12, local.storage_size);
  ASSERT_EQ(4, local.alignment);

  psx_parameter_storage_plan_t parameter = {0};
  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, pointer.type_id,
      ag_target_info_data_layout(&wasm), &parameter));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_POINTER, parameter.kind);
  ASSERT_EQ(4, parameter.storage_size);
  ASSERT_EQ(4, parameter.alignment);

  const psx_lowering_context_dependencies_t dependencies = {
      .arena_context = test_arena_context(test_suite_session),
      .diagnostic_context = test_diagnostics(test_suite_session),
      .target = &wasm,
      .semantic_types = types,
      .record_decls =
          ps_ctx_record_decl_table_in(
              test_semantic_context(test_suite_session)),
      .record_layouts = record_layouts,
  };
  psx_lowering_context_t *lowering =
      ps_lowering_context_create(&dependencies);
  ASSERT_TRUE(lowering != NULL);
  ASSERT_TRUE(ag_target_info_equal(
      ps_lowering_target(lowering), &wasm));
  ASSERT_EQ(8, ps_lowering_type_id_size(
                   lowering, record_type.type_id));
  ASSERT_EQ(4, ps_lowering_type_id_alignment(
                   lowering, record_type.type_id));
  ASSERT_EQ(4, ps_lowering_type_id_size(
                   lowering, pointer.type_id));
  ps_lowering_context_destroy(lowering);
  psx_record_layout_table_destroy(record_layouts);
}

static void test_dynamic_abi_piece_capacity(
    ag_compilation_session_t *test_suite_session) {
  printf("test_dynamic_abi_piece_capacity...\n");
  reset_test_translation_unit_state(test_suite_session);
  enum { PARAM_COUNT = 40 };
  psx_qual_type_t integer = ps_ctx_intern_integer_qual_type_in(
      test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0);
  ASSERT_TRUE(integer.type_id != PSX_TYPE_ID_INVALID);
  psx_qual_type_t parameters[PARAM_COUNT];
  for (size_t i = 0; i < PARAM_COUNT; i++) parameters[i] = integer;

  ir_module_t *module = ir_module_new();
  ASSERT_TRUE(module != NULL);
  ir_func_t *function = ir_func_new(module, "many_parameters", 15);
  ASSERT_TRUE(function != NULL);
  ASSERT_TRUE(ir_function_type_set(
      &function->function_type, PSX_TYPE_ID_INVALID,
      integer, parameters, PARAM_COUNT, 0, 1));

  ir_abi_type_context_t context = {
      .semantic_types = ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session)),
      .target = ag_compilation_session_target(test_suite_session),
  };
  ir_abi_module_t *lowered = ir_abi_lower_module(&context, module);
  const ir_abi_signature_t *signature =
      ir_abi_function_signature(lowered, function);
  ASSERT_TRUE(signature != NULL);
  ASSERT_EQ(PARAM_COUNT, signature->param_count);
  ASSERT_EQ(PARAM_COUNT, signature->fixed_param_count);
  ASSERT_EQ(39, signature->param_pieces[39].source_index);
  ASSERT_EQ(IR_TY_I32, signature->param_pieces[39].type);
  ASSERT_EQ(IR_ABI_PIECE_DIRECT, signature->param_pieces[39].kind);
  ASSERT_EQ(1, signature->result_count);
  ASSERT_EQ(IR_TY_I32, signature->result_pieces[0].type);

  ir_abi_module_free(lowered);
  ir_module_free(module);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_dynamic_function_parameter_capacity(
    ag_compilation_session_t *test_suite_session) {
  printf("test_dynamic_function_parameter_capacity...\n");
  reset_test_translation_unit_state(test_suite_session);
  enum { PARAM_COUNT = PS_MAX_DECLARATOR_COUNT + 1 };
  size_t capacity = (size_t)PARAM_COUNT * 24 + 64;
  char *source = malloc(capacity);
  ASSERT_TRUE(source != NULL);
  size_t used = 0;
  int written = snprintf(
      source + used, capacity - used, "int many_parameters(");
  ASSERT_TRUE(written > 0 && (size_t)written < capacity - used);
  used += (size_t)written;
  for (int i = 0; i < PARAM_COUNT; i++) {
    written = snprintf(
        source + used, capacity - used,
        i == 0 ? "int p%d" : ", int p%d", i);
    ASSERT_TRUE(written > 0 && (size_t)written < capacity - used);
    used += (size_t)written;
  }
  written = snprintf(source + used, capacity - used, ");");
  ASSERT_TRUE(written > 0 && (size_t)written < capacity - used);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session, source));
  const psx_scope_declaration_t *declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "many_parameters",
          PSX_DECL_FUNCTION, 0);
  ASSERT_TRUE(declaration != NULL && declaration->payload != NULL);
  psx_qual_type_t function_type = ps_function_symbol_qual_type(
      (const psx_function_symbol_t *)declaration->payload);
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session)),
      function_type.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, shape.kind);
  ASSERT_EQ(PARAM_COUNT, shape.parameter_count);
  free(source);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_wasm_target_global_pointer_data_layout() {
  printf("test_wasm_target_global_pointer_data_layout...\n");
  ag_target_info_t wasm_target = ag_target_info_wasm32();
  ag_compilation_session_t session;
  ASSERT_TRUE(ag_compilation_session_init(&session, &wasm_target));

  ASSERT_TRUE(resolve_test_program_hir_from_in_session(
      &session,
      tk_tokenize_ctx(
          ag_compilation_session_tokenizer(&session),
          (char *)"int layout_first; "
                  "int layout_second; "
                  "int *layout_pointers[2] = {"
                  "  &layout_first, &layout_second"
                  "};")));

  const psx_scope_declaration_t *declaration =
      find_test_scope_declaration(
          ps_ctx_scope_graph(
              ag_compilation_session_semantic_context(
                  &session)),
          "layout_pointers", PSX_DECL_GLOBAL_OBJECT, 0);
  ASSERT_TRUE(declaration != NULL);
  global_var_t *pointers =
      (global_var_t *)declaration->payload;
  ASSERT_TRUE(pointers != NULL);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(
          ag_compilation_session_semantic_context(
              &session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(
          ag_compilation_session_semantic_context(
              &session));
  const ag_data_layout_t *data_layout =
      ag_target_info_data_layout(
          ag_compilation_session_target(&session));
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(pointers), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(2, shape.array_len);
  psx_qual_type_t pointer_element =
      psx_semantic_type_table_base(
          types, ps_gvar_decl_type_id(pointers));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointer_element.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_POINTER, shape.kind);
  ASSERT_EQ(4, psx_type_layout_sizeof(
                   types, record_layouts,
                   pointer_element.type_id, data_layout));
  ASSERT_EQ(8, psx_type_layout_sizeof(
                   types, record_layouts,
                   ps_gvar_decl_type_id(pointers), data_layout));

  ir_data_module_t *module =
      lower_ir_translation_unit_data_in_session(&session);
  ASSERT_TRUE(module != NULL);
  ir_data_object_t *object = ir_data_module_find_object(
      module, "layout_pointers", 15);
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
  ASSERT_TRUE(ag_compilation_session_dispose(&session));
}

static void test_vla_lowering_request_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_vla_lowering_request_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int vla_request_probe(int n, int k, "
      "    int tensor[][n][3][k]) { "
      "  _Alignas(16) int values[n]; "
      "  int *pointers[n]; "
      "  int matrix[n][3][4]; "
      "  _Alignas(32) int (*row_pointer)[n] = 0; "
      "  return sizeof(values) + sizeof(pointers) "
      "      + sizeof(matrix) + (row_pointer == 0) "
      "      + tensor[0][0][0][0]; "
      "}"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);
  ASSERT_TRUE(hir != NULL);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));

  const char *local_names[] = {
      "values", "pointers", "matrix", "row_pointer",
  };
  lvar_t *locals[4] = {0};
  for (size_t i = 0;
       i < sizeof(local_names) / sizeof(local_names[0]); i++) {
    const psx_scope_declaration_t *declaration =
        find_test_scope_declaration(
            test_scope_graph(test_suite_session), local_names[i],
            PSX_DECL_LOCAL_OBJECT, 0);
    ASSERT_TRUE(declaration != NULL);
    locals[i] = (lvar_t *)declaration->payload;
    ASSERT_TRUE(locals[i] != NULL);
    ASSERT_TRUE(ps_lvar_is_vla(locals[i]));
  }

  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[0]), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_TRUE(shape.is_vla);
  ASSERT_EQ(16, ps_lvar_align_bytes(locals[0]));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[1]), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_TRUE(shape.is_vla);
  psx_qual_type_t pointer_element =
      psx_semantic_type_table_base(
          types, ps_lvar_decl_type_id(locals[1]));
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, pointer_element).kind);
  ASSERT_EQ(8, psx_type_layout_sizeof(
                   types, record_layouts,
                   pointer_element.type_id, data_layout));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[2]), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_TRUE(shape.is_vla);
  psx_qual_type_t matrix_middle =
      psx_semantic_type_table_base(
          types, ps_lvar_decl_type_id(locals[2]));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, matrix_middle.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(3, shape.array_len);
  psx_qual_type_t matrix_inner =
      psx_semantic_type_table_base(
          types, matrix_middle.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, matrix_inner.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(4, shape.array_len);

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[3]), &shape));
  ASSERT_EQ(PSX_TYPE_POINTER, shape.kind);
  ASSERT_EQ(32, ps_lvar_align_bytes(locals[3]));
  ASSERT_TRUE(ps_lvar_vla_row_stride_frame_off(locals[3]) > 0);
  psx_qual_type_t pointed_row =
      psx_semantic_type_table_base(
          types, ps_lvar_decl_type_id(locals[3]));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointed_row.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_TRUE(shape.is_vla);

  const psx_hir_node_t *function =
      find_test_named_hir_node(
          hir, PSX_HIR_FUNCTION,
          "vla_request_probe", 0);
  const psx_hir_node_t *tensor =
      find_test_hir_function_parameter(
          hir, function, "tensor");
  ASSERT_TRUE(function != NULL);
  ASSERT_TRUE(tensor != NULL);
  ASSERT_EQ(3, psx_hir_node_vla_dimension_count(tensor));
  ASSERT_EQ(3, psx_hir_node_vla_dimension_constant(
                   tensor, 1));
  ASSERT_EQ(4, psx_hir_node_vla_stride_element_size(tensor));
  ASSERT_TRUE(psx_hir_node_vla_stride_frame_offset(tensor) != 0);

  int vla_alloc_count = 0;
  int matrix_stride_store_found = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(
            hir, (psx_hir_node_id_t)i);
    if (!node ||
        psx_hir_node_kind(node) != PSX_HIR_VLA_ALLOC)
      continue;
    vla_alloc_count++;
    if (psx_hir_node_vla_runtime_store_count(node) >= 2)
      matrix_stride_store_found = 1;
  }
  ASSERT_TRUE(vla_alloc_count >= 3);
  ASSERT_TRUE(matrix_stride_store_found);

  psx_hir_node_id_t root_id =
      psx_hir_module_root_at(hir, 0);
  ir_build_options_t options = {
      .target =
          ag_compilation_session_target(test_suite_session),
      .semantic_types = types,
      .record_decls =
          ps_ctx_record_decl_table_in(
              test_semantic_context(test_suite_session)),
      .record_layouts = record_layouts,
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(
              test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL);
  ASSERT_TRUE(ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(
                  ir->funcs, IR_VLA_ALLOC) >= 3);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MUL) >= 4);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 4);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
}
static void test_parameter_declaration_storage_plan_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parameter_declaration_storage_plan_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  psx_semantic_type_table_t *types =
      (psx_semantic_type_table_t *)
          ps_ctx_semantic_type_table_in(
              test_semantic_context(test_suite_session));
  psx_record_layout_table_t *record_layouts =
      (psx_record_layout_table_t *)
          ps_ctx_record_layout_table_in(
              test_semantic_context(test_suite_session));
  psx_record_decl_table_t *record_decls =
      (psx_record_decl_table_t *)
          ps_ctx_record_decl_table_in(
              test_semantic_context(test_suite_session));
  const ag_target_info_t *target =
      ps_ctx_target_info(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ag_target_info_data_layout(target);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(record_decls != NULL);
  ASSERT_TRUE(data_layout != NULL);

  psx_qual_type_t integer =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_INT, 0, 0);
  psx_qual_type_t pointer =
      psx_semantic_type_table_intern_pointer_to(
          types, integer);
  psx_qual_type_t complex =
      psx_semantic_type_table_intern_floating(
          types, PSX_FLOATING_KIND_DOUBLE, 1);

  const psx_record_id_t small_record_id = 0x501u;
  const psx_record_id_t large_record_id = 0x502u;
  const psx_record_decl_t small_record = {
      .record_id = small_record_id,
      .record_kind = PSX_TYPE_STRUCT,
      .tag_name = (char *)"SmallParam",
      .tag_len = 10,
      .is_complete = 1,
  };
  const psx_record_decl_t large_record = {
      .record_id = large_record_id,
      .record_kind = PSX_TYPE_STRUCT,
      .tag_name = (char *)"LargeParam",
      .tag_len = 10,
      .is_complete = 1,
  };
  ASSERT_TRUE(psx_record_decl_table_define(
      record_decls, &small_record));
  ASSERT_TRUE(psx_record_decl_table_define(
      record_decls, &large_record));
  ASSERT_TRUE(psx_record_layout_table_define(
      record_layouts, small_record_id, data_layout,
      12, 8, NULL, 0));
  ASSERT_TRUE(psx_record_layout_table_define(
      record_layouts, large_record_id, data_layout,
      24, 8, NULL, 0));
  psx_qual_type_t small_aggregate =
      psx_semantic_type_table_intern_record(
          types, small_record_id);
  psx_qual_type_t large_aggregate =
      psx_semantic_type_table_intern_record(
          types, large_record_id);

  psx_parameter_storage_plan_t plan = {0};
  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, integer.type_id,
      data_layout, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_SCALAR, plan.kind);
  ASSERT_EQ(4, plan.storage_size);

  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, pointer.type_id,
      data_layout, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_POINTER, plan.kind);
  ASSERT_EQ(8, plan.storage_size);
  ASSERT_EQ(8, plan.alignment);

  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, small_aggregate.type_id,
      data_layout, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_AGGREGATE_VALUE,
            plan.kind);
  ASSERT_EQ(12, plan.storage_size);
  ASSERT_EQ(8, plan.alignment);

  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, large_aggregate.type_id,
      data_layout, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_AGGREGATE_VALUE,
            plan.kind);
  ASSERT_EQ(24, plan.storage_size);
  ASSERT_EQ(8, plan.alignment);

  ASSERT_TRUE(psx_plan_parameter_storage_for_type_id(
      types, record_layouts, complex.type_id,
      data_layout, &plan));
  ASSERT_EQ(PSX_PARAMETER_STORAGE_COMPLEX, plan.kind);
  ASSERT_EQ(16, plan.storage_size);
  ASSERT_EQ(8, plan.alignment);
  ASSERT_TRUE(!psx_plan_parameter_storage_for_type_id(
      types, record_layouts, PSX_TYPE_ID_INVALID,
      data_layout, &plan));

  reset_test_locals(test_suite_session);
  psx_parameter_declaration_resolution_t large_resolution = {
      .declaration_qual_type = large_aggregate,
      .lowering_kind = PSX_PARAMETER_LOWER_NORMAL,
  };
  lvar_t *lowered = lower_resolved_parameter_declaration(
      &(psx_resolved_parameter_lowering_request_t){
          .local_registry = test_local_registry(test_suite_session),
          .lowering_context = test_lowering_context(test_suite_session),
          .name = (char *)"value",
          .name_len = 5,
          .resolution = &large_resolution,
      });
  ASSERT_TRUE(lowered != NULL);
  ASSERT_TRUE(ps_lvar_is_param(lowered));
  ASSERT_EQ(24, ps_lvar_frame_storage_size(lowered));
  ASSERT_EQ(8, ps_lvar_align_bytes(lowered));
  ASSERT_EQ(large_aggregate.type_id,
            ps_lvar_decl_type_id(lowered));
  ASSERT_EQ(24, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_lvar_decl_type_id(lowered),
                    data_layout));

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int *planned_function(int value, int *pointer) { "
      "  return value ? pointer : 0; "
      "} "
      "double planned_target(int value) { return value; } "
      "double (*planned_funcptr(int input))(int) { "
      "  (void)input; return planned_target; "
      "} "
      "int vla_parameter_probe(int n, int values[][n]) { "
      "  return values[0][n - 1]; "
      "}"));

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(
          test_suite_session);
  const psx_hir_node_t *planned =
      find_test_named_hir_node(
          hir, PSX_HIR_FUNCTION,
          "planned_function", 0);
  const psx_hir_node_t *funcptr =
      find_test_named_hir_node(
          hir, PSX_HIR_FUNCTION,
          "planned_funcptr", 0);
  const psx_hir_node_t *vla_function =
      find_test_named_hir_node(
          hir, PSX_HIR_FUNCTION,
          "vla_parameter_probe", 0);
  ASSERT_TRUE(planned != NULL);
  ASSERT_TRUE(funcptr != NULL);
  ASSERT_TRUE(vla_function != NULL);

  psx_qual_type_t planned_type =
      psx_hir_node_attached_qual_type(planned);
  psx_type_shape_t planned_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, planned_type.type_id, &planned_shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, planned_shape.kind);
  ASSERT_EQ(2, planned_shape.parameter_count);
  ASSERT_TRUE(planned_shape.has_function_prototype);
  ASSERT_TRUE(!planned_shape.is_variadic_function);
  psx_qual_type_t planned_result =
      psx_semantic_type_table_base(
          types, planned_type.type_id);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, planned_result).kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_qual_type_shape(test_suite_session,
                psx_semantic_type_table_parameter(
                    types, planned_type.type_id, 0)).kind);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session,
                psx_semantic_type_table_parameter(
                    types, planned_type.type_id, 1)).kind);

  psx_qual_type_t funcptr_type =
      psx_hir_node_attached_qual_type(funcptr);
  psx_qual_type_t returned_pointer =
      psx_semantic_type_table_base(
          types, funcptr_type.type_id);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, returned_pointer).kind);
  psx_qual_type_t returned_callable =
      psx_semantic_type_table_base(
          types, returned_pointer.type_id);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            test_qual_type_shape(test_suite_session, returned_callable).kind);
  ASSERT_EQ(1, test_qual_type_shape(test_suite_session,
                   returned_callable).parameter_count);
  ASSERT_EQ(PSX_TYPE_FLOAT,
            test_qual_type_shape(test_suite_session,
                psx_semantic_type_table_base(
                    types,
                    returned_callable.type_id)).kind);

  const psx_hir_node_t *values =
      find_test_hir_function_parameter(
          hir, vla_function, "values");
  ASSERT_TRUE(values != NULL);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_hir_type_shape(test_suite_session, values).kind);
  ASSERT_TRUE(psx_hir_node_vla_dimension_count(
                  values) >= 1);
  ASSERT_EQ(8, psx_hir_node_object_size(values));
}
static void test_global_declaration_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_global_declaration_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "extern int boundary_global[]; "
      "int boundary_global[3]; "
      "extern int boundary_inferred[]; "
      "int boundary_inferred[] = {1, 2, 3}; "
      "static int *boundary_static; "
      "struct BoundaryRecord; "
      "struct BoundaryRecord { int values[4]; }; "
      "struct BoundaryRecord boundary_record; "
      "int boundary_function_name(void) { return 0; } "
      "typedef int boundary_typedef_name; "
      "enum BoundaryEnum { boundary_enum_name = 5 };"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);

  const psx_scope_declaration_t *global_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "boundary_global",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *inferred_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "boundary_inferred",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *static_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "boundary_static",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *record_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "boundary_record",
          PSX_DECL_GLOBAL_OBJECT, 0);
  ASSERT_TRUE(global_declaration != NULL);
  ASSERT_TRUE(inferred_declaration != NULL);
  ASSERT_TRUE(static_declaration != NULL);
  ASSERT_TRUE(record_declaration != NULL);

  global_var_t *global =
      (global_var_t *)global_declaration->payload;
  global_var_t *inferred =
      (global_var_t *)inferred_declaration->payload;
  global_var_t *internal =
      (global_var_t *)static_declaration->payload;
  global_var_t *record =
      (global_var_t *)record_declaration->payload;
  ASSERT_TRUE(global != NULL);
  ASSERT_TRUE(inferred != NULL);
  ASSERT_TRUE(internal != NULL);
  ASSERT_TRUE(record != NULL);

  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(global), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(3, shape.array_len);
  ASSERT_EQ(12, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_gvar_decl_type_id(global),
                    data_layout));
  ASSERT_TRUE(!ps_gvar_is_extern_decl(global));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(inferred), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(3, shape.array_len);
  ASSERT_EQ(12, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_gvar_decl_type_id(inferred),
                    data_layout));
  ASSERT_TRUE(!ps_gvar_is_extern_decl(inferred));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(internal), &shape));
  ASSERT_EQ(PSX_TYPE_POINTER, shape.kind);
  ASSERT_EQ(8, psx_type_layout_sizeof(
                   types, record_layouts,
                   ps_gvar_decl_type_id(internal),
                   data_layout));
  ASSERT_TRUE(ps_gvar_is_static_storage(internal));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(record), &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  ASSERT_TRUE(shape.record_id !=
              PSX_RECORD_ID_INVALID);
  const psx_record_decl_t *record_definition =
      psx_record_decl_table_lookup(
          ps_ctx_record_decl_table_in(
              test_semantic_context(test_suite_session)),
          shape.record_id);
  ASSERT_TRUE(record_definition != NULL);
  ASSERT_TRUE(record_definition->is_complete);
  ASSERT_EQ(1, record_definition->member_count);
  ASSERT_EQ(16, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_gvar_decl_type_id(record),
                    data_layout));
  ASSERT_TRUE(!ps_gvar_is_extern_decl(record));

  const psx_scope_declaration_t *function_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "boundary_function_name",
          PSX_DECL_FUNCTION, 0);
  const psx_scope_declaration_t *typedef_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "boundary_typedef_name",
          PSX_DECL_TYPEDEF, 0);
  const psx_scope_declaration_t *enum_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "boundary_enum_name",
          PSX_DECL_ENUM_CONSTANT, 0);
  ASSERT_TRUE(function_declaration != NULL);
  ASSERT_TRUE(typedef_declaration != NULL);
  ASSERT_TRUE(enum_declaration != NULL);

  expect_parse_fail(test_suite_session,
      "struct Incomplete; "
      "struct Incomplete incomplete_object; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "extern int conflicting_global[]; "
      "int *conflicting_global; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "int conflicting_function(void); "
      "int conflicting_function; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "typedef int conflicting_typedef; "
      "int conflicting_typedef; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "enum ConflictEnum { conflicting_enum }; "
      "int conflicting_enum; "
      "int main(void) { return 0; }");
}

static void test_declaration_pipeline_order_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_declaration_pipeline_order_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  token_t *tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"= 37");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  psx_parsed_initializer_t initializer;
  psx_prepare_optional_initializer_syntax(
      &initializer,
      ag_compilation_session_parser_runtime_context(test_suite_session));
  char *name = (char *)"__pipeline_object";
  int name_len = 17;
  psx_global_declaration_pipeline_request_t request = {
      .semantic_context = test_semantic_context(test_suite_session),
      .global_registry = test_global_registry(test_suite_session),
      .local_registry = test_local_registry(test_suite_session),
      .lowering_context = test_lowering_context(test_suite_session),
      .options = test_compilation_options(test_suite_session),
      .name = name,
      .name_len = name_len,
      .type = ps_ctx_intern_integer_qual_type_in(
          test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0),
      .initializer = &initializer,
      .diag_tok = tokens,
  };
  psx_global_declaration_pipeline_result_t result;
  ASSERT_TRUE(psx_begin_global_declaration_pipeline(&request, &result));
  ASSERT_TRUE(find_test_global_var(test_suite_session, name, name_len) != NULL);
  token_t *assign_tok = tk_get_current_token_ctx(test_tokenizer(test_suite_session));
  ASSERT_EQ(TK_ASSIGN, assign_tok->kind);
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), assign_tok->next);
  parse_test_initializer_syntax_value(test_suite_session, &initializer, assign_tok);
  ASSERT_TRUE(psx_finish_global_declaration_pipeline(&request, &result));
  ASSERT_TRUE(result.global != NULL);
  ASSERT_TRUE(result.initialized);
  ASSERT_EQ(37, result.global->init_val);
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_tokenizer(test_suite_session))->kind);
}

static void test_tag_declaration_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_tag_declaration_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  psx_tag_declaration_resolution_request_t request = {
      .semantic_context = test_semantic_context(test_suite_session),
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
  const psx_record_decl_t *cached_definition =
      ps_ctx_ensure_tag_record_decl_in(test_semantic_context(test_suite_session),
          TK_STRUCT, (char *)"__TagBoundary", 13);
  ASSERT_TRUE(cached_definition != NULL);
  ASSERT_TRUE(cached_definition->record_id != PSX_RECORD_ID_INVALID);
  ASSERT_TRUE(!cached_definition->is_complete);
  psx_record_id_t outer_record_id = cached_definition->record_id;
  ASSERT_TRUE(psx_record_layout_table_lookup(
                  ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session)),
                  outer_record_id,
                  ag_target_info_data_layout(
                      ps_ctx_target_info(test_semantic_context(test_suite_session)))) == NULL);

  request.mode = PSX_TAG_DECLARATION_DEFINITION;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.registered);
  ASSERT_EQ(cached_definition,
            ps_ctx_ensure_tag_record_decl_in(test_semantic_context(test_suite_session),
                TK_STRUCT, (char *)"__TagBoundary", 13));
  ASSERT_TRUE(cached_definition->is_complete);
  ASSERT_EQ(outer_record_id, cached_definition->record_id);
  const psx_record_layout_t *outer_layout = psx_record_layout_table_lookup(
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session)), outer_record_id,
      ag_target_info_data_layout(ps_ctx_target_info(test_semantic_context(test_suite_session))));
  ASSERT_TRUE(outer_layout == NULL);
  ASSERT_TRUE(ps_ctx_publish_record_layout_in(
      test_semantic_context(test_suite_session), outer_record_id, 0, 1));
  outer_layout = psx_record_layout_table_lookup(
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session)), outer_record_id,
      ag_target_info_data_layout(ps_ctx_target_info(test_semantic_context(test_suite_session))));
  ASSERT_TRUE(outer_layout != NULL);
  ASSERT_EQ(1, outer_layout->alignment);
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_REDEFINITION, resolution.status);

  request.kind = TK_UNION;
  request.mode = PSX_TAG_DECLARATION_FORWARD;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_KIND_CONFLICT, resolution.status);

  ps_decl_enter_scope_in(test_local_registry(test_suite_session));
  request.kind = TK_STRUCT;
  request.mode = PSX_TAG_DECLARATION_FORWARD;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_TRUE(resolution.registered);
  ASSERT_EQ(1, resolution.scope_depth);
  const psx_record_decl_t *shadow_definition =
      ps_ctx_ensure_tag_record_decl_in(test_semantic_context(test_suite_session),
          TK_STRUCT, (char *)"__TagBoundary", 13);
  ASSERT_TRUE(shadow_definition != NULL);
  ASSERT_TRUE(shadow_definition->record_id != outer_record_id);
  request.mode = PSX_TAG_DECLARATION_DEFINITION;
  psx_resolve_tag_declaration(&request, &resolution);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, resolution.status);
  ASSERT_EQ(1, resolution.scope_depth);
  ps_decl_leave_scope_in(test_local_registry(test_suite_session));
  ASSERT_EQ(0, ps_ctx_get_tag_scope_depth_in(test_semantic_context(test_suite_session),
                   TK_STRUCT, (char *)"__TagBoundary", 13));
}

static void test_record_decl_ownership_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_record_decl_ownership_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct DefinitionOwner; "
      "struct DefinitionOwner *forward_owner; "
      "struct DefinitionOwner { const int value; }; "
      "struct DeclarationOrder { int first; int second; }; "
      "struct ParameterRecord { char marker; }; "
      "struct DefinitionOwner outer_owner; "
      "struct DeclarationOrder order_owner; "
      "struct DefinitionOwner *record_identity("
      "    struct ParameterRecord *parameter) { "
      "  (void)parameter; return &outer_owner; "
      "} "
      "int record_shadow_probe(void) { "
      "  struct DefinitionOwner { long inner; }; "
      "  struct DefinitionOwner inner_owner = {0}; "
      "  return sizeof(inner_owner); "
      "}"));

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_record_decl_table_t *records =
      ps_ctx_record_decl_table_in(test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  ASSERT_TRUE(hir != NULL);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(records != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);

  const psx_scope_declaration_t *forward_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "forward_owner",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *outer_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "outer_owner",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *order_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "order_owner",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *inner_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "inner_owner",
          PSX_DECL_LOCAL_OBJECT, 0);
  ASSERT_TRUE(forward_declaration != NULL);
  ASSERT_TRUE(outer_declaration != NULL);
  ASSERT_TRUE(order_declaration != NULL);
  ASSERT_TRUE(inner_declaration != NULL);

  global_var_t *forward =
      (global_var_t *)forward_declaration->payload;
  global_var_t *outer =
      (global_var_t *)outer_declaration->payload;
  global_var_t *order =
      (global_var_t *)order_declaration->payload;
  lvar_t *inner = (lvar_t *)inner_declaration->payload;
  ASSERT_TRUE(forward != NULL);
  ASSERT_TRUE(outer != NULL);
  ASSERT_TRUE(order != NULL);
  ASSERT_TRUE(inner != NULL);

  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(outer), &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  psx_record_id_t outer_record_id = shape.record_id;
  ASSERT_TRUE(outer_record_id != PSX_RECORD_ID_INVALID);

  psx_qual_type_t forward_base =
      psx_semantic_type_table_base(
          types, ps_gvar_decl_type_id(forward));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, forward_base.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  ASSERT_EQ(outer_record_id, shape.record_id);

  const psx_record_decl_t *outer_record =
      psx_record_decl_table_lookup(records, outer_record_id);
  ASSERT_TRUE(outer_record != NULL);
  ASSERT_TRUE(outer_record->is_complete);
  ASSERT_EQ(1, outer_record->member_count);
  ASSERT_TRUE(outer_record->members != NULL);
  ASSERT_TRUE(strncmp(
                  outer_record->members[0].name,
                  "value", 5) == 0);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST,
            outer_record->members[0]
                .decl_qual_type.qualifiers);
  ASSERT_EQ(4, psx_type_layout_sizeof(
                   types, record_layouts,
                   ps_gvar_decl_type_id(outer), data_layout));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(order), &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  const psx_record_decl_t *order_record =
      psx_record_decl_table_lookup(records, shape.record_id);
  const psx_record_layout_t *order_layout =
      psx_record_layout_table_lookup(
          record_layouts, shape.record_id, data_layout);
  ASSERT_TRUE(order_record != NULL);
  ASSERT_TRUE(order_layout != NULL);
  ASSERT_EQ(2, order_record->member_count);
  ASSERT_TRUE(strncmp(
                  order_record->members[0].name,
                  "first", 5) == 0);
  ASSERT_TRUE(strncmp(
                  order_record->members[1].name,
                  "second", 6) == 0);
  ASSERT_EQ(0, psx_record_layout_member(
                   order_layout, 0)->offset);
  ASSERT_EQ(4, psx_record_layout_member(
                   order_layout, 1)->offset);
  ASSERT_EQ(8, order_layout->size);
  ASSERT_EQ(4, order_layout->alignment);

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(inner), &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  ASSERT_TRUE(shape.record_id != PSX_RECORD_ID_INVALID);
  ASSERT_TRUE(shape.record_id != outer_record_id);
  const psx_record_decl_t *inner_record =
      psx_record_decl_table_lookup(records, shape.record_id);
  ASSERT_TRUE(inner_record != NULL);
  ASSERT_TRUE(inner_record->is_complete);
  ASSERT_EQ(1, inner_record->member_count);
  ASSERT_TRUE(strncmp(
                  inner_record->members[0].name,
                  "inner", 5) == 0);
  ASSERT_EQ(8, psx_type_layout_sizeof(
                   types, record_layouts,
                   ps_lvar_decl_type_id(inner), data_layout));

  const psx_hir_node_t *function =
      find_test_named_hir_node(
          hir, PSX_HIR_FUNCTION,
          "record_identity", 0);
  ASSERT_TRUE(function != NULL);
  psx_qual_type_t function_type =
      psx_hir_node_attached_qual_type(function);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, function_type.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, shape.kind);
  ASSERT_EQ(1, shape.parameter_count);

  psx_qual_type_t result_pointer =
      psx_semantic_type_table_base(
          types, function_type.type_id);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, result_pointer).kind);
  psx_qual_type_t result_record =
      psx_semantic_type_table_base(
          types, result_pointer.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, result_record.type_id, &shape));
  ASSERT_EQ(outer_record_id, shape.record_id);

  psx_qual_type_t parameter_pointer =
      psx_semantic_type_table_parameter(
          types, function_type.type_id, 0);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session,
                parameter_pointer).kind);
  psx_qual_type_t parameter_record =
      psx_semantic_type_table_base(
          types, parameter_pointer.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, parameter_record.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  ASSERT_TRUE(shape.record_id != PSX_RECORD_ID_INVALID);
  ASSERT_TRUE(shape.record_id != outer_record_id);
}

static void test_aggregate_body_phase_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_aggregate_body_phase_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  psx_tag_declaration_resolution_t tag;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .kind = TK_STRUCT,
          .name = (char *)"__ParsedBody",
          .name_len = 12,
          .mode = PSX_TAG_DECLARATION_FORWARD,
      },
      &tag);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag.status);

  token_t *tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session),
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
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  psx_parsed_aggregate_body_t body;
  parse_test_aggregate_body(test_suite_session, &body);
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
  ASSERT_EQ(ND_ADD,
            phase_enum_body->members[1].initializer->kind);
  ASSERT_EQ(ND_IDENTIFIER,
            phase_enum_body->members[1].initializer->lhs->kind);
  const node_identifier_t *phase_enum_identifier =
      (const node_identifier_t *)
          phase_enum_body->members[1].initializer->lhs;
  ASSERT_EQ(13, phase_enum_identifier->name_len);
  ASSERT_TRUE(strncmp(
                  phase_enum_identifier->name,
                  "PhaseEnumZero", 13) == 0);
  ASSERT_EQ(ND_NUM,
            phase_enum_body->members[1].initializer->rhs->kind);
  ASSERT_EQ(2, as_num(
                   phase_enum_body->members[1].initializer->rhs)->val);
  ASSERT_EQ(1, body.items[5].value.member_declaration.declarators[0]
                   .array_bound_count);
  ASSERT_TRUE(body.items[5].value.member_declaration.declarators[0]
                  .array_bounds[0].expression.node != NULL);
  ASSERT_TRUE(body.items[6].value.member_declaration.declarators[0]
                  .bit_width_expression.start != NULL);
  ASSERT_TRUE(body.items[6].value.member_declaration.declarators[0]
                  .bit_width_expression.node != NULL);
  ASSERT_EQ(1, body.items[7].value.member_declaration.specifier
                   .alignas_specifier_count);
  ASSERT_EQ(PSX_PARSED_ALIGNAS_EXPRESSION,
            body.items[7].value.member_declaration.specifier
                .alignas_specifiers[0].kind);
  ASSERT_TRUE(body.items[7].value.member_declaration.specifier
                  .alignas_specifiers[0].expression != NULL);
  ASSERT_EQ(ND_ADD,
            body.items[7].value.member_declaration.specifier
                .alignas_specifiers[0].expression->kind);
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
  ASSERT_EQ(TK_EOF, tk_get_current_token_ctx(test_tokenizer(test_suite_session))->kind);

  psx_record_member_decl_t member_declaration = {0};
  psx_record_member_layout_t member_layout = {0};
  ASSERT_TRUE(!test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"a", 1, &member_declaration, &member_layout));
  ASSERT_TRUE(!test_semantic_has_tag_type(test_suite_session,
      TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_TRUE(!test_semantic_has_tag_type(test_suite_session,
      TK_ENUM, (char *)"PhaseEnumTag", 12));
  long long enum_value = 0;
  ASSERT_TRUE(!ps_ctx_find_enum_const_in(test_semantic_context(test_suite_session),
      (char *)"PhaseEnumZero", 13, &enum_value));
  psx_typedef_info_t deferred_alias_lookup = {0};
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(test_semantic_context(test_suite_session),
      (char *)"DeferredAlias", 13, &deferred_alias_lookup));

  psx_typedef_info_t deferred_alias = {0};
  set_test_typedef_fixture_type(test_suite_session,
      &deferred_alias, ps_ctx_intern_integer_qual_type_in(
          test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0));
  ASSERT_TRUE(test_semantic_define_typedef_name(test_suite_session,
      (char *)"DeferredAlias", 13, &deferred_alias));
  psx_typedef_info_t deferred_parameter = {0};
  set_test_typedef_fixture_type(test_suite_session,
      &deferred_parameter,
      ps_ctx_intern_floating_qual_type_in(
          test_semantic_context(test_suite_session), PSX_FLOATING_KIND_DOUBLE, 0));
  ASSERT_TRUE(test_semantic_define_typedef_name(test_suite_session,
      (char *)"DeferredParam", 13, &deferred_parameter));

  int size = 0;
  int alignment = 0;
  ASSERT_EQ(10, apply_test_parsed_aggregate_body_layout(test_suite_session,
                   &body, TK_STRUCT, (char *)"__ParsedBody", 12,
                   &size, &alignment));
  ASSERT_EQ(72, size);
  ASSERT_EQ(8, alignment);
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .semantic_context = test_semantic_context(test_suite_session),
          .kind = TK_STRUCT,
          .name = (char *)"__ParsedBody",
          .name_len = 12,
          .mode = PSX_TAG_DECLARATION_DEFINITION,
          .member_count = 10,
      },
      &tag);
  ASSERT_EQ(PSX_TAG_DECLARATION_OK, tag.status);
  const psx_record_decl_t *parsed_record =
      ps_ctx_ensure_tag_record_decl_in(
          test_semantic_context(test_suite_session), TK_STRUCT,
          (char *)"__ParsedBody", 12);
  ASSERT_TRUE(parsed_record != NULL);
  ASSERT_TRUE(ps_ctx_publish_record_layout_in(
      test_semantic_context(test_suite_session), parsed_record->record_id,
      size, alignment));
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  psx_type_shape_t member_shape = {0};
  ASSERT_TRUE(body.items[5].value.member_declaration.declarators[0]
                  .array_bounds[0].expression.node != NULL);
  ASSERT_TRUE(body.items[6].value.member_declaration.declarators[0]
                  .bit_width_expression.node != NULL);
  ASSERT_TRUE(body.items[7].value.member_declaration.specifier
                  .alignas_specifiers[0].expression != NULL);
  ASSERT_TRUE(test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"a", 1, &member_declaration, &member_layout));
  ASSERT_EQ(0, member_layout.offset);
  ASSERT_TRUE(test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"b", 1, &member_declaration, &member_layout));
  ASSERT_EQ(8, member_layout.offset);
  ASSERT_TRUE(test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"c", 1, &member_declaration, &member_layout));
  ASSERT_EQ(16, member_layout.offset);
  ASSERT_TRUE(test_semantic_has_tag_type(test_suite_session,
      TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_EQ(4, ps_ctx_get_tag_size_in(test_semantic_context(test_suite_session),
                   TK_STRUCT, (char *)"PhaseInnerTag", 13));
  ASSERT_TRUE(test_semantic_has_tag_type(test_suite_session,
      TK_ENUM, (char *)"PhaseEnumTag", 12));
  ASSERT_TRUE(ps_ctx_find_enum_const_in(test_semantic_context(test_suite_session),
      (char *)"PhaseEnumZero", 13, &enum_value));
  ASSERT_EQ(3, enum_value);
  ASSERT_TRUE(ps_ctx_find_enum_const_in(test_semantic_context(test_suite_session),
      (char *)"PhaseEnumNext", 13, &enum_value));
  ASSERT_EQ(5, enum_value);
  ASSERT_TRUE(test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"arr", 3, &member_declaration, &member_layout));
  ASSERT_EQ(28, member_layout.offset);
  ASSERT_TRUE(member_declaration.decl_qual_type.type_id !=
              PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, member_declaration.decl_qual_type.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, member_shape.kind);
  ASSERT_EQ(5, member_shape.array_len);
  ASSERT_TRUE(test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"flags", 5, &member_declaration, &member_layout));
  ASSERT_EQ(48, member_layout.offset);
  ASSERT_EQ(3, member_declaration.bit_width);
  ASSERT_TRUE(test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"aligned", 7, &member_declaration, &member_layout));
  ASSERT_EQ(56, member_layout.offset);
  ASSERT_TRUE(test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"late", 4, &member_declaration, &member_layout));
  ASSERT_EQ(60, member_layout.offset);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, member_declaration.decl_qual_type.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, member_shape.kind);
  ASSERT_EQ(4, test_type_size_id(test_suite_session,
                   member_declaration.decl_qual_type.type_id));
  ASSERT_TRUE(test_find_tag_member(test_semantic_context(test_suite_session),
      TK_STRUCT, (char *)"__ParsedBody", 12,
      (char *)"callback", 8, &member_declaration, &member_layout));
  ASSERT_EQ(64, member_layout.offset);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, member_declaration.decl_qual_type.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_POINTER, member_shape.kind);
  psx_qual_type_t callback = psx_semantic_type_table_base(
      types, member_declaration.decl_qual_type.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, callback.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, member_shape.kind);
  ASSERT_EQ(3, member_shape.parameter_count);
  ASSERT_TRUE(member_shape.is_variadic_function);
  psx_qual_type_t callback_parameter =
      psx_semantic_type_table_parameter(types, callback.type_id, 0);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, callback_parameter.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_FLOAT, member_shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE, member_shape.floating_kind);
  callback_parameter =
      psx_semantic_type_table_parameter(types, callback.type_id, 1);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, callback_parameter.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_POINTER, member_shape.kind);
  psx_qual_type_t pointee = psx_semantic_type_table_base(
      types, callback_parameter.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointee.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, member_shape.kind);
  callback_parameter =
      psx_semantic_type_table_parameter(types, callback.type_id, 2);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, callback_parameter.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_POINTER, member_shape.kind);
  pointee = psx_semantic_type_table_base(
      types, callback_parameter.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointee.type_id, &member_shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, member_shape.kind);
  ASSERT_TRUE(!test_semantic_has_tag_type(test_suite_session,
      TK_STRUCT, (char *)"PrototypeOnly", 13));
  psx_dispose_parsed_aggregate_body(&body);
}

static void test_declaration_phase_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_declaration_phase_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  token_t *tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session),
      (char *)"struct __PhaseObject { int value; }");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  psx_declaration_phase_t phase;
  psx_parsed_decl_specifier_t syntax;
  parse_test_decl_specifier_syntax(test_suite_session, &syntax);
  psx_begin_declaration_phase(&phase, &syntax);
  ASSERT_EQ(PSX_PARSED_DECL_TYPE_NONE, syntax.source);
  ASSERT_EQ(PSX_DECLARATION_PHASE_SYNTAX, phase.state);
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            psx_declaration_phase_base_qual_type(&phase).type_id);
  ASSERT_EQ(-1, test_tag_member_count(test_suite_session,
                    TK_STRUCT, (char *)"__PhaseObject", 13));

  psx_qual_type_t unapplied_type =
      psx_resolve_decl_specifier_qual_type_in_context(
          test_semantic_context(test_suite_session), &phase.syntax);
  ASSERT_EQ(PSX_TYPE_ID_INVALID, unapplied_type.type_id);
  ASSERT_EQ(-1, test_tag_member_count(test_suite_session,
                    TK_STRUCT, (char *)"__PhaseObject", 13));

  ASSERT_TRUE(apply_test_declaration_phase(test_suite_session, &phase, 0));
  ASSERT_EQ(PSX_DECLARATION_PHASE_RESOLVED_TYPE, phase.state);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  psx_qual_type_t phase_base =
      psx_declaration_phase_base_qual_type(&phase);
  ASSERT_TRUE(phase_base.type_id != PSX_TYPE_ID_INVALID);
  psx_type_shape_t phase_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, phase_base.type_id, &phase_shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, phase_shape.kind);
  const psx_record_decl_t *phase_record =
      psx_record_decl_table_lookup(
          ps_ctx_record_decl_table_in(test_semantic_context(test_suite_session)),
          phase_shape.record_id);
  ASSERT_TRUE(phase_record != NULL);
  ASSERT_EQ(1, phase_record->member_count);
  ASSERT_EQ(1, test_tag_member_count(test_suite_session,
                   TK_STRUCT, (char *)"__PhaseObject", 13));
  psx_dispose_declaration_phase(&phase);

  tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"_Alignas(16) int");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  parse_test_decl_specifier_syntax(test_suite_session, &syntax);
  psx_begin_declaration_phase(&phase, &syntax);
  ASSERT_EQ(0, phase.requested_alignment);
  ASSERT_TRUE(apply_test_declaration_phase(test_suite_session, &phase, 0));
  ASSERT_EQ(16, phase.requested_alignment);
  phase_base = psx_declaration_phase_base_qual_type(&phase);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, phase_base.type_id, &phase_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, phase_shape.kind);
  psx_dispose_declaration_phase(&phase);

  tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"_Alignas(int *) char");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  parse_test_decl_specifier_syntax(test_suite_session, &syntax);
  ASSERT_EQ(1, syntax.alignas_specifier_count);
  ASSERT_EQ(PSX_PARSED_ALIGNAS_TYPE_NAME,
            syntax.alignas_specifiers[0].kind);
  ASSERT_TRUE(syntax.alignas_specifiers[0].type_name != NULL);
  ASSERT_TRUE(syntax.alignas_specifiers[0].expression == NULL);
  psx_begin_declaration_phase(&phase, &syntax);
  ASSERT_TRUE(apply_test_declaration_phase(test_suite_session, &phase, 0));
  ASSERT_EQ(test_target_pointer_alignment(
                ps_ctx_target_info(test_semantic_context(test_suite_session))),
            phase.requested_alignment);
  phase_base = psx_declaration_phase_base_qual_type(&phase);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, phase_base.type_id, &phase_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, phase_shape.kind);
  psx_dispose_declaration_phase(&phase);
}

static void test_type_name_phase_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_type_name_phase_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  token_t *tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"int (*)(double),");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  psx_parsed_type_name_t syntax;
  ASSERT_TRUE(parse_test_type_name_syntax_at(test_suite_session, tokens, &syntax));
  ASSERT_TRUE(tk_get_current_token_ctx(test_tokenizer(test_suite_session)) == tokens);
  ASSERT_TRUE(syntax.end != NULL);
  ASSERT_EQ(TK_COMMA, syntax.end->kind);
  ASSERT_TRUE(syntax.atomic_inner == NULL);

  psx_type_name_ref_t reference = {.syntax = &syntax};
  psx_type_name_base_resolution_t base_resolution = {0};
  ASSERT_TRUE(psx_resolve_type_name_base_in_contexts(
      test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
      test_local_registry(test_suite_session), &reference, &base_resolution));
  ASSERT_TRUE(base_resolution.base_qual_type.type_id !=
              PSX_TYPE_ID_INVALID);
  psx_qual_type_t resolved = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  ASSERT_TRUE(psx_resolve_type_name_qual_type_in_contexts(
      test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
      test_local_registry(test_suite_session), &reference, &resolved));
  ASSERT_TRUE(resolved.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(base_resolution.runtime_application == NULL);

  psx_qual_type_t type =
      psx_apply_parsed_type_name_qual_type_in_contexts(
          test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
          test_local_registry(test_suite_session), &syntax);
  ASSERT_TRUE(type.type_id != PSX_TYPE_ID_INVALID);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, type.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_POINTER, shape.kind);
  psx_qual_type_t function =
      psx_semantic_type_table_base(types, type.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, function.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, shape.kind);
  ASSERT_EQ(1, shape.parameter_count);
  psx_qual_type_t parameter = psx_semantic_type_table_parameter(
      types, function.type_id, 0);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, parameter.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_FLOAT, shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE, shape.floating_kind);
  psx_dispose_type_name_syntax(&syntax);

  tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"const int,");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  ASSERT_TRUE(parse_test_type_name_syntax_at(test_suite_session, tokens, &syntax));
  reference = (psx_type_name_ref_t){.syntax = &syntax};
  base_resolution = (psx_type_name_base_resolution_t){0};
  ASSERT_TRUE(psx_resolve_type_name_base_in_contexts(
      test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
      test_local_registry(test_suite_session), &reference, &base_resolution));
  psx_qual_type_t qualified_base =
      base_resolution.base_qual_type;
  ASSERT_TRUE((qualified_base.qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, qualified_base.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  psx_dispose_type_name_syntax(&syntax);

  tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"_Atomic(int),");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  ASSERT_TRUE(parse_test_type_name_syntax_at(test_suite_session, tokens, &syntax));
  reference = (psx_type_name_ref_t){.syntax = &syntax};
  base_resolution = (psx_type_name_base_resolution_t){0};
  ASSERT_TRUE(psx_resolve_type_name_base_in_contexts(
      test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
      test_local_registry(test_suite_session), &reference, &base_resolution));
  qualified_base = base_resolution.base_qual_type;
  ASSERT_TRUE((qualified_base.qualifiers &
               PSX_TYPE_QUALIFIER_ATOMIC) != 0);
  psx_dispose_type_name_syntax(&syntax);

  char tag_name[] = "__TypeNameTag";
  test_semantic_define_tag_type_with_layout(test_suite_session,
      TK_STRUCT, tag_name, 13, 0, 4, 4);
  tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"const struct __TypeNameTag,");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  ASSERT_TRUE(parse_test_type_name_syntax_at(test_suite_session, tokens, &syntax));
  psx_scope_lookup_point_t point = test_scope_lookup_point(test_suite_session);
  reference = (psx_type_name_ref_t){
      .syntax = &syntax,
      .scope_seq = point.scope_id,
      .declaration_seq = point.declaration_order,
  };
  base_resolution = (psx_type_name_base_resolution_t){0};
  ASSERT_TRUE(psx_resolve_type_name_base_in_contexts(
      test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
      test_local_registry(test_suite_session), &reference, &base_resolution));
  qualified_base = base_resolution.base_qual_type;
  ASSERT_TRUE((qualified_base.qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, qualified_base.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  psx_dispose_type_name_syntax(&syntax);

  const psx_typedef_info_t alias = {
      .decl_type_table =
          ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      .decl_qual_type = ps_ctx_intern_integer_qual_type_in(
          test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0),
  };
  char alias_name[] = "__TypeNameAlias";
  ASSERT_TRUE(ps_ctx_register_typedef_name_in(
      test_semantic_context(test_suite_session), alias_name, 15, &alias, NULL, NULL));
  tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"volatile __TypeNameAlias,");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  ASSERT_TRUE(parse_test_type_name_syntax_at(test_suite_session, tokens, &syntax));
  point = test_scope_lookup_point(test_suite_session);
  reference = (psx_type_name_ref_t){
      .syntax = &syntax,
      .scope_seq = point.scope_id,
      .declaration_seq = point.declaration_order,
  };
  base_resolution = (psx_type_name_base_resolution_t){0};
  ASSERT_TRUE(psx_resolve_type_name_base_in_contexts(
      test_semantic_context(test_suite_session), test_global_registry(test_suite_session),
      test_local_registry(test_suite_session), &reference, &base_resolution));
  qualified_base = base_resolution.base_qual_type;
  ASSERT_EQ(alias.decl_qual_type.type_id, qualified_base.type_id);
  ASSERT_TRUE((qualified_base.qualifiers &
               PSX_TYPE_QUALIFIER_VOLATILE) != 0);
  psx_dispose_type_name_syntax(&syntax);
}

static void test_toplevel_declarator_phase_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_toplevel_declarator_phase_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int __phase_fn(int), __phase_object;"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  psx_qual_type_t function = ps_ctx_get_function_qual_type_in(
      test_semantic_context(test_suite_session), (char *)"__phase_fn", 10);
  ASSERT_TRUE(function.type_id != PSX_TYPE_ID_INVALID);
  psx_type_shape_t function_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, function.type_id, &function_shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(1, function_shape.parameter_count);
  psx_qual_type_t parameter = psx_semantic_type_table_parameter(
      types, function.type_id, 0);
  psx_type_shape_t parameter_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, parameter.type_id, &parameter_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, parameter_shape.kind);

  global_var_t *object =
      find_test_global_var(test_suite_session, (char *)"__phase_object", 14);
  ASSERT_TRUE(object != NULL);
  psx_type_shape_t object_shape = {0};
  ASSERT_TRUE(ps_gvar_decl_type_shape(object, &object_shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, object_shape.kind);
}

static void test_local_declarator_application_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_local_declarator_application_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  register_test_default_storage_fixture(test_suite_session, (char *)"n", 1);

  token_t *tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), (char *)"matrix[n][4]");
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  psx_parsed_declarator_t syntax =
      parse_test_declarator_syntax_tree(test_suite_session);
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
  token_t *after_syntax = tk_get_current_token_ctx(test_tokenizer(test_suite_session));

  psx_runtime_declarator_application_t applied;
  apply_test_runtime_parsed_declarator(test_suite_session, &syntax, &applied);
  ASSERT_TRUE(tk_get_current_token_ctx(test_tokenizer(test_suite_session)) == after_syntax);
  ASSERT_EQ(2, applied.array_bound_count);
  ASSERT_TRUE(applied.shape.ops[0].is_vla_array);
  ASSERT_EQ(0, applied.shape.ops[0].array_len);
  ASSERT_TRUE(!applied.array_bounds[0].is_constant);
  ASSERT_TRUE(applied.array_bounds[0].expression_id !=
              PSX_SEMANTIC_EXPR_ID_INVALID);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  test_semantic_context(test_suite_session),
                  applied.array_bounds[0].expression_id) != NULL);
  ASSERT_TRUE(!applied.shape.ops[1].is_vla_array);
  ASSERT_EQ(4, applied.shape.ops[1].array_len);
  ASSERT_TRUE(applied.array_bounds[1].is_constant);
  ASSERT_EQ(4, applied.array_bounds[1].constant_value);
  psx_dispose_declarator_syntax(&syntax);
}

static void test_local_declaration_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_local_declaration_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct LocalRecordElement { int first; int second; }; "
      "int local_declaration_probe(int n) { "
      "  int scalar = 1; "
      "  int inferred[] = {1, 2, 3}; "
      "  int runtime_values[n]; "
      "  int (*pointer_to_runtime)[n] = 0; "
      "  struct LocalRecordElement records[] = "
      "      {{1, 2}, {3, 4}}; "
      "  return scalar + inferred[2] + runtime_values[0] "
      "      + (pointer_to_runtime == 0) + records[1].second; "
      "}"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);

  const char *names[] = {
      "scalar", "inferred", "runtime_values",
      "pointer_to_runtime", "records",
  };
  lvar_t *locals[5] = {0};
  for (size_t i = 0;
       i < sizeof(names) / sizeof(names[0]); i++) {
    const psx_scope_declaration_t *declaration =
        find_test_scope_declaration(
            test_scope_graph(test_suite_session), names[i],
            PSX_DECL_LOCAL_OBJECT, 0);
    ASSERT_TRUE(declaration != NULL);
    locals[i] = (lvar_t *)declaration->payload;
    ASSERT_TRUE(locals[i] != NULL);
    ASSERT_TRUE(ps_lvar_decl_type_id(locals[i]) !=
                PSX_TYPE_ID_INVALID);
  }

  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[0]), &shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(4, psx_type_layout_sizeof(
                   types, record_layouts,
                   ps_lvar_decl_type_id(locals[0]), data_layout));
  ASSERT_TRUE(!ps_lvar_is_vla(locals[0]));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[1]), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(3, shape.array_len);
  ASSERT_TRUE(!shape.is_vla);
  ASSERT_EQ(12, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_lvar_decl_type_id(locals[1]), data_layout));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[2]), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_TRUE(shape.is_vla);
  ASSERT_TRUE(ps_lvar_is_vla(locals[2]));
  ASSERT_TRUE(psx_semantic_type_table_contains_vla_array(
      types, ps_lvar_decl_type_id(locals[2])));

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[3]), &shape));
  ASSERT_EQ(PSX_TYPE_POINTER, shape.kind);
  ASSERT_TRUE(ps_lvar_is_vla(locals[3]));
  psx_qual_type_t pointed_runtime_array =
      psx_semantic_type_table_base(
          types, ps_lvar_decl_type_id(locals[3]));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointed_runtime_array.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_TRUE(shape.is_vla);

  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[4]), &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(2, shape.array_len);
  psx_qual_type_t record_element =
      psx_semantic_type_table_base(
          types, ps_lvar_decl_type_id(locals[4]));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, record_element.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  ASSERT_TRUE(shape.record_id != PSX_RECORD_ID_INVALID);
  ASSERT_EQ(16, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_lvar_decl_type_id(locals[4]), data_layout));

  expect_parse_fail(test_suite_session,
      "int main(void) { int incomplete[]; return 0; }");
}

static void test_aggregate_member_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_aggregate_member_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct LayoutBoundary { "
      "  char c; int a : 20; int b : 16; short tail; "
      "}; "
      "union UnionBoundary { int i; long l; }; "
      "struct PointerShape { "
      "  int (*p)[3]; int (*array[2])[3]; "
      "}; "
      "struct PromDst { "
      "  long prefix; struct { int pad; int b : 3; }; "
      "}; "
      "struct Incomplete; "
      "struct Constraint { struct Incomplete *pointer; }; "
      "int aggregate_member_probe(void) { "
      "  struct LayoutBoundary layout = {0}; "
      "  union UnionBoundary united = {0}; "
      "  struct PointerShape shapes = {0}; "
      "  struct PromDst promoted = {0}; "
      "  struct Constraint constraint = {0}; "
      "  return layout.c + layout.a + layout.b + layout.tail "
      "      + united.i + (shapes.p == 0) "
      "      + (shapes.array[0] == 0) + promoted.b "
      "      + (constraint.pointer == 0); "
      "}"));

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(
          test_suite_session);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  ASSERT_TRUE(hir != NULL);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);

  const char *local_names[] = {
      "layout", "united", "shapes", "promoted", "constraint",
  };
  psx_type_id_t local_type_ids[5] = {
      PSX_TYPE_ID_INVALID,
      PSX_TYPE_ID_INVALID,
      PSX_TYPE_ID_INVALID,
      PSX_TYPE_ID_INVALID,
      PSX_TYPE_ID_INVALID,
  };
  psx_record_id_t record_ids[5] = {
      PSX_RECORD_ID_INVALID,
      PSX_RECORD_ID_INVALID,
      PSX_RECORD_ID_INVALID,
      PSX_RECORD_ID_INVALID,
      PSX_RECORD_ID_INVALID,
  };
  for (size_t i = 0;
       i < sizeof(local_names) / sizeof(local_names[0]); i++) {
    const psx_scope_declaration_t *declaration =
        find_test_scope_declaration(
            test_scope_graph(test_suite_session), local_names[i],
            PSX_DECL_LOCAL_OBJECT, 0);
    ASSERT_TRUE(declaration != NULL);
    lvar_t *local = (lvar_t *)declaration->payload;
    ASSERT_TRUE(local != NULL);
    local_type_ids[i] = ps_lvar_decl_type_id(local);
    psx_type_shape_t shape = {0};
    ASSERT_TRUE(psx_semantic_type_table_describe(
        types, local_type_ids[i], &shape));
    ASSERT_TRUE(shape.kind == PSX_TYPE_STRUCT ||
                shape.kind == PSX_TYPE_UNION);
    ASSERT_TRUE(shape.record_id != PSX_RECORD_ID_INVALID);
    record_ids[i] = shape.record_id;
  }

  const psx_record_layout_t *layout_record =
      psx_record_layout_table_lookup(
          record_layouts, record_ids[0], data_layout);
  const psx_record_layout_t *union_record =
      psx_record_layout_table_lookup(
          record_layouts, record_ids[1], data_layout);
  const psx_record_layout_t *pointer_record =
      psx_record_layout_table_lookup(
          record_layouts, record_ids[2], data_layout);
  const psx_record_layout_t *promoted_record =
      psx_record_layout_table_lookup(
          record_layouts, record_ids[3], data_layout);
  const psx_record_layout_t *constraint_record =
      psx_record_layout_table_lookup(
          record_layouts, record_ids[4], data_layout);
  ASSERT_TRUE(layout_record != NULL);
  ASSERT_TRUE(union_record != NULL);
  ASSERT_TRUE(pointer_record != NULL);
  ASSERT_TRUE(promoted_record != NULL);
  ASSERT_TRUE(constraint_record != NULL);

  ASSERT_EQ(4, layout_record->member_count);
  ASSERT_EQ(12, layout_record->size);
  ASSERT_EQ(4, layout_record->alignment);
  ASSERT_EQ(2, union_record->member_count);
  ASSERT_EQ(8, union_record->size);
  ASSERT_EQ(8, union_record->alignment);
  ASSERT_EQ(2, pointer_record->member_count);
  ASSERT_EQ(24, pointer_record->size);
  ASSERT_EQ(8, pointer_record->alignment);
  ASSERT_EQ(4, promoted_record->member_count);
  ASSERT_EQ(16, promoted_record->size);
  ASSERT_EQ(8, promoted_record->alignment);
  ASSERT_EQ(1, constraint_record->member_count);
  ASSERT_EQ(8, constraint_record->size);
  ASSERT_EQ(8, constraint_record->alignment);

  psx_qual_type_t pointer_member =
      psx_semantic_type_table_record_member(
          types, local_type_ids[2], 0);
  psx_type_shape_t pointer_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointer_member.type_id, &pointer_shape));
  ASSERT_EQ(PSX_TYPE_POINTER, pointer_shape.kind);
  psx_qual_type_t pointed_array =
      psx_semantic_type_table_base(
          types, pointer_member.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointed_array.type_id, &pointer_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, pointer_shape.kind);
  ASSERT_EQ(3, pointer_shape.array_len);
  ASSERT_EQ(12, psx_type_layout_sizeof(
                    types, record_layouts,
                    pointed_array.type_id, data_layout));

  psx_qual_type_t pointer_array_member =
      psx_semantic_type_table_record_member(
          types, local_type_ids[2], 1);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointer_array_member.type_id,
      &pointer_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, pointer_shape.kind);
  ASSERT_EQ(2, pointer_shape.array_len);
  ASSERT_EQ(16, psx_type_layout_sizeof(
                    types, record_layouts,
                    pointer_array_member.type_id,
                    data_layout));
  psx_qual_type_t pointer_array_element =
      psx_semantic_type_table_base(
          types, pointer_array_member.type_id);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session,
                pointer_array_element).kind);

  psx_qual_type_t same_record =
      psx_semantic_type_table_intern_record(
          (psx_semantic_type_table_t *)types,
          record_ids[0]);
  psx_qual_type_t same_record_again =
      psx_semantic_type_table_intern_record(
          (psx_semantic_type_table_t *)types,
          record_ids[0]);
  psx_qual_type_t different_record =
      psx_semantic_type_table_intern_record(
          (psx_semantic_type_table_t *)types,
          record_ids[1]);
  ASSERT_EQ(same_record.type_id,
            same_record_again.type_id);
  ASSERT_TRUE(same_record.type_id !=
              different_record.type_id);

  int width20_count = 0;
  int width16_count = 0;
  int promoted_bitfield_count = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(
            hir, (psx_hir_node_id_t)i);
    if (!node ||
        psx_hir_node_kind(node) !=
            PSX_HIR_MEMBER_ACCESS)
      continue;
    int bit_width = 0;
    int bit_offset = 0;
    int is_signed = 0;
    if (!psx_hir_node_bitfield_info(
            node, &bit_width, &bit_offset,
            &is_signed))
      continue;
    ASSERT_TRUE(is_signed);
    if (bit_width == 20) width20_count++;
    if (bit_width == 16) width16_count++;
    if (bit_width == 3 &&
        psx_hir_node_member_offset(node) == 12)
      promoted_bitfield_count++;
  }
  ASSERT_TRUE(width20_count >= 1);
  ASSERT_TRUE(width16_count >= 1);
  ASSERT_TRUE(promoted_bitfield_count >= 1);

  expect_parse_fail(test_suite_session,
      "struct BadWidth { int value : 33; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "struct BadPointer { int *value : 1; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "struct BadFunction { int value(void); }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "struct Missing; "
      "struct BadIncomplete { struct Missing value; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "struct BadDuplicate { int value; long value; }; "
      "int main(void) { return 0; }");
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

static void test_typedef_declaration_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_typedef_declaration_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "typedef int TypeBoundary; "
      "typedef int TypeBoundary; "
      "TypeBoundary typedef_global; "
      "int typedef_vla_probe(int n) { "
      "  typedef int VlaTypeBoundary[n]; "
      "  VlaTypeBoundary values; "
      "  typedef long TypeBoundary; "
      "  TypeBoundary inner = 0; "
      "  return sizeof(values) + (int)inner; "
      "}"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  ASSERT_TRUE(types != NULL);

  psx_typedef_info_t global_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(
      test_semantic_context(test_suite_session), (char *)"TypeBoundary", 12,
      &global_info));
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, global_info.decl_qual_type.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);

  const psx_scope_declaration_t *global_typedef =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "TypeBoundary",
          PSX_DECL_TYPEDEF, 0);
  const psx_scope_declaration_t *local_typedef =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "TypeBoundary",
          PSX_DECL_TYPEDEF, 1);
  const psx_scope_declaration_t *vla_typedef =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "VlaTypeBoundary",
          PSX_DECL_TYPEDEF, 0);
  ASSERT_TRUE(global_typedef != NULL);
  ASSERT_TRUE(local_typedef != NULL);
  ASSERT_TRUE(vla_typedef != NULL);
  ASSERT_EQ(PSX_SCOPE_ID_TRANSLATION_UNIT,
            global_typedef->scope_id);
  ASSERT_TRUE(local_typedef->scope_id !=
              PSX_SCOPE_ID_TRANSLATION_UNIT);
  ASSERT_EQ(local_typedef->scope_id,
            vla_typedef->scope_id);

  psx_typedef_info_t local_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_at_in(
      test_semantic_context(test_suite_session), (char *)"TypeBoundary", 12,
      (psx_scope_lookup_point_t){
          .scope_id = local_typedef->scope_id,
          .declaration_order = UINT32_MAX,
      },
      &local_info));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, local_info.decl_qual_type.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, shape.integer_kind);
  ASSERT_TRUE(local_info.decl_qual_type.type_id !=
              global_info.decl_qual_type.type_id);

  psx_typedef_info_t vla_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_at_in(
      test_semantic_context(test_suite_session), (char *)"VlaTypeBoundary", 15,
      (psx_scope_lookup_point_t){
          .scope_id = vla_typedef->scope_id,
          .declaration_order = UINT32_MAX,
      },
      &vla_info));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, vla_info.decl_qual_type.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_TRUE(shape.is_vla);
  ASSERT_TRUE(psx_semantic_type_table_contains_vla_array(
      types, vla_info.decl_qual_type.type_id));
  ASSERT_TRUE(vla_info.runtime_application != NULL);
  ASSERT_EQ(1, vla_info.runtime_application->shape.count);
  ASSERT_EQ(PSX_DECL_OP_ARRAY,
            vla_info.runtime_application->shape.ops[0].kind);
  ASSERT_TRUE(vla_info.runtime_application->shape.ops[0]
                  .is_vla_array);
  ASSERT_EQ(1, vla_info.runtime_application->array_bound_count);

  const psx_scope_declaration_t *values_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "values",
          PSX_DECL_LOCAL_OBJECT, 0);
  ASSERT_TRUE(values_declaration != NULL);
  lvar_t *values = (lvar_t *)values_declaration->payload;
  ASSERT_TRUE(values != NULL);
  ASSERT_EQ(vla_info.decl_qual_type.type_id,
            ps_lvar_decl_type_id(values));

  expect_parse_fail(test_suite_session,
      "typedef int ConflictType; "
      "typedef long ConflictType; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "int TypeObject; typedef int TypeObject; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "int TypeFunction(void); typedef int TypeFunction; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "enum TypeEnumValues { TypeEnum = 1 }; "
      "typedef int TypeEnum; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { "
      "  int BlockTypeObject; "
      "  typedef int BlockTypeObject; "
      "  return 0; "
      "}");
}

static void test_enum_constant_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_enum_constant_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "enum { __EnumBoundary = 7 }; "
      "int enum_boundary_probe(void) { "
      "  enum { __EnumBoundary = 11 }; "
      "  return __EnumBoundary; "
      "}"));

  const psx_scope_graph_t *graph = test_scope_graph(test_suite_session);
  const psx_scope_declaration_t *outer =
      find_test_scope_declaration(
          graph, "__EnumBoundary",
          PSX_DECL_ENUM_CONSTANT, 0);
  const psx_scope_declaration_t *inner =
      find_test_scope_declaration(
          graph, "__EnumBoundary",
          PSX_DECL_ENUM_CONSTANT, 1);
  ASSERT_TRUE(outer != NULL);
  ASSERT_TRUE(inner != NULL);
  ASSERT_TRUE(outer->scope_id != inner->scope_id);
  ASSERT_EQ(PSX_SCOPE_TRANSLATION_UNIT,
            psx_scope_graph_scope_kind(
                graph, outer->scope_id));
  ASSERT_EQ(PSX_SCOPE_FUNCTION,
            psx_scope_graph_scope_kind(
                graph, inner->scope_id));
  long long value = 0;
  ASSERT_TRUE(ps_ctx_enum_const_value_by_declaration_id_in(
      test_semantic_context(test_suite_session), outer->id, &value));
  ASSERT_EQ(7, value);
  ASSERT_TRUE(ps_ctx_enum_const_value_by_declaration_id_in(
      test_semantic_context(test_suite_session), inner->id, &value));
  ASSERT_EQ(11, value);

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  int found_inner_value = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node = psx_hir_module_lookup(
        hir, (psx_hir_node_id_t)i);
    if (node && psx_hir_node_kind(node) == PSX_HIR_NUMBER &&
        psx_hir_node_integer_value(node) == 11)
      found_inner_value = 1;
  }
  ASSERT_TRUE(found_inner_value);

  expect_parse_fail(test_suite_session,
      "enum { DuplicateEnum = 1, DuplicateEnum = 2 }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "int __EnumObject; enum { __EnumObject = 1 }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "int __EnumFunction(void); enum { __EnumFunction = 1 }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "typedef int __EnumType; enum { __EnumType = 1 }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { int __EnumLocal; "
      "enum { __EnumLocal = 1 }; return 0; }");
}

static void test_initializer_resolution_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_initializer_resolution_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct InitBoundary { const int x; int a[2]; }; "
      "struct InitBoundary designated_init = {.a[1] = 9}; "
      "const struct InitBoundary qualified_init = "
      "    {.x = 1, .a = {2, 3}}; "
      "struct RecursiveInit { "
      "    struct RecursiveInit *next; int value; }; "
      "struct RecursiveInit recursive_init = {0, 4};"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_record_decl_table_t *records =
      ps_ctx_record_decl_table_in(test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(records != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);

  const psx_scope_declaration_t *designated_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "designated_init",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *qualified_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "qualified_init",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *recursive_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "recursive_init",
          PSX_DECL_GLOBAL_OBJECT, 0);
  ASSERT_TRUE(designated_declaration != NULL);
  ASSERT_TRUE(qualified_declaration != NULL);
  ASSERT_TRUE(recursive_declaration != NULL);
  global_var_t *designated =
      (global_var_t *)designated_declaration->payload;
  global_var_t *qualified =
      (global_var_t *)qualified_declaration->payload;
  global_var_t *recursive =
      (global_var_t *)recursive_declaration->payload;
  ASSERT_TRUE(designated != NULL);
  ASSERT_TRUE(qualified != NULL);
  ASSERT_TRUE(recursive != NULL);

  psx_qual_type_t designated_type =
      ps_gvar_decl_qual_type(designated);
  psx_type_shape_t aggregate_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, designated_type.type_id, &aggregate_shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, aggregate_shape.kind);
  ASSERT_TRUE(aggregate_shape.record_id !=
              PSX_RECORD_ID_INVALID);
  const psx_record_decl_t *aggregate_record =
      psx_record_decl_table_lookup(
          records, aggregate_shape.record_id);
  const psx_record_layout_t *aggregate_layout =
      psx_record_layout_table_lookup(
          record_layouts, aggregate_shape.record_id,
          data_layout);
  ASSERT_TRUE(aggregate_record != NULL);
  ASSERT_TRUE(aggregate_layout != NULL);
  ASSERT_EQ(2, aggregate_record->member_count);
  ASSERT_EQ(12, aggregate_layout->size);
  ASSERT_EQ(0, psx_record_layout_member(
                   aggregate_layout, 0)->offset);
  ASSERT_EQ(4, psx_record_layout_member(
                   aggregate_layout, 1)->offset);

  psx_initializer_scalar_leaf_list_t leaves = {0};
  ASSERT_TRUE(psx_collect_initializer_scalar_leaves_with_records(
      types, records, record_layouts, data_layout,
      designated_type, 0, &leaves));
  ASSERT_EQ(3, leaves.count);
  ASSERT_EQ(0, leaves.items[0].relative_offset);
  ASSERT_EQ(4, leaves.items[1].relative_offset);
  ASSERT_EQ(8, leaves.items[2].relative_offset);
  ASSERT_EQ(leaves.items[1].qual_type.type_id,
            leaves.items[2].qual_type.type_id);
  ASSERT_TRUE(leaves.items[2].qual_type.type_id != PSX_TYPE_ID_INVALID);
  ASSERT_TRUE((leaves.items[0].qual_type.qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_NONE,
            leaves.items[1].qual_type.qualifiers);
  psx_initializer_scalar_leaf_list_dispose(&leaves);

  ASSERT_TRUE(psx_collect_initializer_scalar_leaves_with_records(
      types, records, record_layouts, data_layout,
      ps_gvar_decl_qual_type(qualified), 0, &leaves));
  ASSERT_EQ(3, leaves.count);
  for (int i = 0; i < leaves.count; i++) {
    ASSERT_TRUE((leaves.items[i].qual_type.qualifiers &
                 PSX_TYPE_QUALIFIER_CONST) != 0);
  }
  psx_initializer_scalar_leaf_list_dispose(&leaves);

  ASSERT_TRUE(psx_collect_initializer_scalar_leaves_with_records(
      types, records, record_layouts, data_layout,
      ps_gvar_decl_qual_type(recursive), 0, &leaves));
  ASSERT_EQ(2, leaves.count);
  psx_type_shape_t recursive_pointer_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types,
      leaves.items[0].qual_type.type_id, &recursive_pointer_shape));
  ASSERT_EQ(PSX_TYPE_POINTER, recursive_pointer_shape.kind);
  ASSERT_EQ(8, leaves.items[1].relative_offset);
  psx_initializer_scalar_leaf_list_dispose(&leaves);

  ir_data_module_t *module =
      lower_ir_translation_unit_data_in_session(
          test_suite_session);
  ASSERT_TRUE(module != NULL);
  ir_data_object_t *designated_data =
      ir_data_module_find_object(
          module, "designated_init", 15);
  ir_data_object_t *qualified_data =
      ir_data_module_find_object(
          module, "qualified_init", 14);
  ir_data_object_t *recursive_data =
      ir_data_module_find_object(
          module, "recursive_init", 14);
  ASSERT_TRUE(designated_data != NULL);
  ASSERT_TRUE(qualified_data != NULL);
  ASSERT_TRUE(recursive_data != NULL);
  ASSERT_EQ(12, designated_data->byte_size);
  ASSERT_EQ(0, designated_data->bytes[4]);
  ASSERT_EQ(9, designated_data->bytes[8]);
  ASSERT_EQ(12, qualified_data->byte_size);
  ASSERT_EQ(1, qualified_data->bytes[0]);
  ASSERT_EQ(2, qualified_data->bytes[4]);
  ASSERT_EQ(3, qualified_data->bytes[8]);
  ASSERT_EQ(16, recursive_data->byte_size);
  ASSERT_EQ(0, recursive_data->bytes[0]);
  ASSERT_EQ(4, recursive_data->bytes[8]);
  ir_data_module_free(module);
}

static void test_local_initializer_parse_lowering_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_local_initializer_parse_lowering_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct LocalInitBoundary { "
      "    int (*fn)(void); int value; }; "
      "union LocalUnionBoundary { int value; }; "
      "int local_initializer_probe(void) { "
      "  struct LocalInitBoundary object = {0, 7}; "
      "  struct LocalInitBoundary source = {0, 8}; "
      "  object = source; "
      "  union LocalUnionBoundary u = {9}; "
      "  int scalar = {7,}; "
      "  double _Complex complex = {3, 4}; "
      "  return object.value + u.value + scalar "
      "      + (int)__real__ complex + (int)__imag__ complex; "
      "}"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);
  ASSERT_TRUE(hir != NULL);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));

  const char *names[] = {
      "object", "source", "u", "scalar", "complex",
  };
  lvar_t *locals[5] = {0};
  for (size_t i = 0;
       i < sizeof(names) / sizeof(names[0]); i++) {
    const psx_scope_declaration_t *declaration =
        find_test_scope_declaration(
            test_scope_graph(test_suite_session), names[i],
            PSX_DECL_LOCAL_OBJECT, 0);
    ASSERT_TRUE(declaration != NULL);
    locals[i] = (lvar_t *)declaration->payload;
    ASSERT_TRUE(locals[i] != NULL);
    ASSERT_TRUE(ps_lvar_decl_type_id(locals[i]) !=
                PSX_TYPE_ID_INVALID);
  }

  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[0]), &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  ASSERT_EQ(16, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_lvar_decl_type_id(locals[0]),
                    data_layout));
  ASSERT_EQ(ps_lvar_decl_type_id(locals[0]),
            ps_lvar_decl_type_id(locals[1]));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[2]), &shape));
  ASSERT_EQ(PSX_TYPE_UNION, shape.kind);
  ASSERT_EQ(4, psx_type_layout_sizeof(
                   types, record_layouts,
                   ps_lvar_decl_type_id(locals[2]),
                   data_layout));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[3]), &shape));
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_lvar_decl_type_id(locals[4]), &shape));
  ASSERT_EQ(PSX_TYPE_COMPLEX, shape.kind);
  ASSERT_EQ(16, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_lvar_decl_type_id(locals[4]),
                    data_layout));

  int declaration_initializer_count = 0;
  int source_assignment_count = 0;
  for (size_t i = 1;
       i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(
            hir, (psx_hir_node_id_t)i);
    if (psx_hir_node_is_declaration_initializer(node))
      declaration_initializer_count++;
    if (psx_hir_node_is_source_assignment(node))
      source_assignment_count++;
  }
  ASSERT_TRUE(declaration_initializer_count >= 7);
  ASSERT_TRUE(source_assignment_count >= 1);

  psx_hir_node_id_t root_id =
      psx_hir_module_root_at(hir, 0);
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(
      test_suite_session));
  ir_build_options_t options = {
      .target = ag_compilation_session_target(test_suite_session),
      .semantic_types = types,
      .record_decls = ps_ctx_record_decl_table_in(
          test_semantic_context(test_suite_session)),
      .record_layouts = record_layouts,
      .diagnostic_context =
          ag_compilation_session_diagnostic_context(
              test_suite_session),
  };
  ir_hir_build_status_t status = IR_HIR_BUILD_INVALID;
  ir_module_t *ir = ir_build_function_module_from_hir(
      hir, root_id, &options, &status);
  ASSERT_EQ(IR_HIR_BUILD_OK, status);
  ASSERT_TRUE(ir != NULL && ir->funcs != NULL);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_STORE) >= 8);
  ASSERT_TRUE(count_ir_op(ir->funcs, IR_MEMCPY) >= 1);
  ASSERT_EQ(1, count_ir_op(ir->funcs, IR_RET));
  ir_module_free(ir);
}

static void test_static_data_initializer_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_static_data_initializer_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "const int qualified_global = 5; "
      "int initialized_array[3] = {1, [2] = 7}; "
      "union InitUnion { long raw; int a[2]; }; "
      "union InitUnion initialized_union = {.a[1] = 7}; "
      "int inferred_array[] = {1, 2, 7}; "
      "char *string_pointers[] = {\"boundary\"}; "
      "struct InitPair { int first; int second; }; "
      "struct InitPair initialized_pair = "
      "    {.second = 22, .first = 11}; "
      "int callback_target(void) { return 3; } "
      "int (*initialized_callback)(void) = callback_target; "
      "int *initialized_address = &inferred_array[1]; "
      "static int internal_value = 9;"));

  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(
          test_semantic_context(test_suite_session));
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(
          test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  ASSERT_TRUE(types != NULL);
  ASSERT_TRUE(record_layouts != NULL);
  ASSERT_TRUE(data_layout != NULL);

  const psx_scope_declaration_t *inferred_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "inferred_array",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *pointer_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "string_pointers",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *qualified_declaration =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "qualified_global",
          PSX_DECL_GLOBAL_OBJECT, 0);
  ASSERT_TRUE(inferred_declaration != NULL);
  ASSERT_TRUE(pointer_declaration != NULL);
  ASSERT_TRUE(qualified_declaration != NULL);

  global_var_t *inferred_global =
      (global_var_t *)inferred_declaration->payload;
  global_var_t *pointer_global =
      (global_var_t *)pointer_declaration->payload;
  global_var_t *qualified_global =
      (global_var_t *)qualified_declaration->payload;
  ASSERT_TRUE(inferred_global != NULL);
  ASSERT_TRUE(pointer_global != NULL);
  ASSERT_TRUE(qualified_global != NULL);

  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(inferred_global),
      &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(3, shape.array_len);
  ASSERT_EQ(12, psx_type_layout_sizeof(
                    types, record_layouts,
                    ps_gvar_decl_type_id(inferred_global),
                    data_layout));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, ps_gvar_decl_type_id(pointer_global),
      &shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
  ASSERT_EQ(1, shape.array_len);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST,
            ps_gvar_decl_qual_type(
                qualified_global).qualifiers);

  ir_data_module_t *module =
      lower_ir_translation_unit_data_in_session(
          test_suite_session);
  ASSERT_TRUE(module != NULL);
  ir_data_object_t *qualified =
      ir_data_module_find_object(
          module, "qualified_global", 16);
  ir_data_object_t *array =
      ir_data_module_find_object(
          module, "initialized_array", 17);
  ir_data_object_t *united =
      ir_data_module_find_object(
          module, "initialized_union", 17);
  ir_data_object_t *inferred =
      ir_data_module_find_object(
          module, "inferred_array", 14);
  ir_data_object_t *pointers =
      ir_data_module_find_object(
          module, "string_pointers", 15);
  ir_data_object_t *pair =
      ir_data_module_find_object(
          module, "initialized_pair", 16);
  ir_data_object_t *callback =
      ir_data_module_find_object(
          module, "initialized_callback", 20);
  ir_data_object_t *address =
      ir_data_module_find_object(
          module, "initialized_address", 19);
  ir_data_object_t *internal =
      ir_data_module_find_object(
          module, "internal_value", 14);
  ASSERT_TRUE(qualified != NULL);
  ASSERT_TRUE(array != NULL);
  ASSERT_TRUE(united != NULL);
  ASSERT_TRUE(inferred != NULL);
  ASSERT_TRUE(pointers != NULL);
  ASSERT_TRUE(pair != NULL);
  ASSERT_TRUE(callback != NULL);
  ASSERT_TRUE(address != NULL);
  ASSERT_TRUE(internal != NULL);

  ASSERT_TRUE(qualified->has_explicit_initializer);
  ASSERT_EQ(4, qualified->byte_size);
  ASSERT_EQ(5, qualified->bytes[0]);

  ASSERT_EQ(12, array->byte_size);
  ASSERT_EQ(1, array->bytes[0]);
  ASSERT_EQ(0, array->bytes[4]);
  ASSERT_EQ(7, array->bytes[8]);

  ASSERT_EQ(8, united->byte_size);
  ASSERT_EQ(0, united->bytes[0]);
  ASSERT_EQ(7, united->bytes[4]);

  ASSERT_EQ(12, inferred->byte_size);
  ASSERT_EQ(1, inferred->bytes[0]);
  ASSERT_EQ(2, inferred->bytes[4]);
  ASSERT_EQ(7, inferred->bytes[8]);

  ASSERT_EQ(8, pointers->byte_size);
  ASSERT_TRUE(pointers->relocs != NULL);
  ASSERT_EQ(IR_DATA_RELOC_DATA,
            pointers->relocs->kind);
  ASSERT_EQ(0, pointers->relocs->offset);
  ASSERT_EQ(8, pointers->relocs->width);
  ASSERT_TRUE(pointers->relocs->target != NULL);

  ASSERT_EQ(8, pair->byte_size);
  ASSERT_EQ(11, pair->bytes[0]);
  ASSERT_EQ(22, pair->bytes[4]);

  ASSERT_EQ(8, callback->byte_size);
  ASSERT_TRUE(callback->relocs != NULL);
  ASSERT_EQ(IR_DATA_RELOC_FUNCTION,
            callback->relocs->kind);
  ASSERT_TRUE(callback->relocs->has_function_type);
  ASSERT_EQ(0, callback->relocs->addend);

  ASSERT_EQ(8, address->byte_size);
  ASSERT_TRUE(address->relocs != NULL);
  ASSERT_EQ(IR_DATA_RELOC_DATA,
            address->relocs->kind);
  ASSERT_EQ(4, address->relocs->addend);
  ASSERT_TRUE(address->relocs->target != NULL);
  ASSERT_EQ(14, address->relocs->target_len);
  ASSERT_TRUE(memcmp(
      address->relocs->target,
      "inferred_array", 14) == 0);

  ASSERT_TRUE(internal->is_static);
  ASSERT_TRUE(internal->has_explicit_initializer);
  ASSERT_EQ(9, internal->bytes[0]);

  ir_data_module_free(module);
}

static void test_expr_mul_div(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_mul_div...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "1 * 2 / 3", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_DIV, syntax->kind);
  ASSERT_EQ(ND_MUL, syntax->lhs->kind);
  ASSERT_EQ(1, as_num(syntax->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(syntax->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(syntax->rhs)->val);
  ASSERT_EQ(PSX_HIR_DIV, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_MUL,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_mod(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_mod...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "10 % 3", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_MOD, syntax->kind);
  ASSERT_EQ(10, as_num(syntax->lhs)->val);
  ASSERT_EQ(3, as_num(syntax->rhs)->val);
  ASSERT_EQ(PSX_HIR_MOD, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_precedence(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_precedence...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "1 + 2 * 3", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_ADD, syntax->kind);
  ASSERT_EQ(1, as_num(syntax->lhs)->val);
  ASSERT_EQ(ND_MUL, syntax->rhs->kind);
  ASSERT_EQ(2, as_num(syntax->rhs->lhs)->val);
  ASSERT_EQ(3, as_num(syntax->rhs->rhs)->val);
  ASSERT_EQ(PSX_HIR_ADD, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_MUL,
            psx_hir_node_kind(test_hir_child(&expression, root, 1)));
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_parentheses(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_parentheses...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "(1 + 2) * 3", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_MUL, syntax->kind);
  ASSERT_EQ(ND_ADD, syntax->lhs->kind);
  ASSERT_EQ(1, as_num(syntax->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(syntax->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(syntax->rhs)->val);
  ASSERT_EQ(PSX_HIR_MUL, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_ADD,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_eq_neq(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_eq_neq...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "1 == 2 != 3", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_NE, syntax->kind);
  ASSERT_EQ(ND_EQ, syntax->lhs->kind);
  ASSERT_EQ(1, as_num(syntax->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(syntax->lhs->rhs)->val);
  ASSERT_EQ(3, as_num(syntax->rhs)->val);
  ASSERT_EQ(PSX_HIR_NE, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_EQ,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  ASSERT_EQ(PSX_INTEGER_KIND_INT,
            test_hir_type_shape(test_suite_session, root).integer_kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_relational(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_relational...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session,
          "1 < 2 <= 3 > 4 >= 5", &syntax);
  const psx_hir_node_t *ge =
      test_expression_hir_root(&expression);
  const psx_hir_node_t *gt = test_hir_child(&expression, ge, 0);
  const psx_hir_node_t *le = test_hir_child(&expression, gt, 0);
  const psx_hir_node_t *lt = test_hir_child(&expression, le, 0);
  ASSERT_EQ(ND_GE, syntax->kind);
  ASSERT_EQ(5, as_num(syntax->rhs)->val);
  ASSERT_EQ(ND_GT, syntax->lhs->kind);
  ASSERT_EQ(4, as_num(syntax->lhs->rhs)->val);
  // <=
  ASSERT_EQ(ND_LE, syntax->lhs->lhs->kind);
  ASSERT_EQ(3, as_num(syntax->lhs->lhs->rhs)->val);
  // <
  ASSERT_EQ(ND_LT, syntax->lhs->lhs->lhs->kind);
  ASSERT_EQ(1, as_num(syntax->lhs->lhs->lhs->lhs)->val);
  ASSERT_EQ(2, as_num(syntax->lhs->lhs->lhs->rhs)->val);
  ASSERT_EQ(PSX_HIR_GE, psx_hir_node_kind(ge));
  ASSERT_EQ(PSX_HIR_GT, psx_hir_node_kind(gt));
  ASSERT_EQ(PSX_HIR_LE, psx_hir_node_kind(le));
  ASSERT_EQ(PSX_HIR_LT, psx_hir_node_kind(lt));
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_logical_and_or(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_logical_and_or...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "1 && 0 || 3", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_LOGOR, syntax->kind);
  ASSERT_EQ(ND_LOGAND, syntax->lhs->kind);
  ASSERT_EQ(3, as_num(syntax->rhs)->val);
  ASSERT_EQ(PSX_HIR_LOGOR, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_LOGAND,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  ASSERT_EQ(PSX_INTEGER_KIND_INT,
            test_hir_type_shape(test_suite_session, root).integer_kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_bitwise(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_bitwise...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "1 | 2 ^ 3 & 4", &syntax);
  const psx_hir_node_t *bit_or =
      test_expression_hir_root(&expression);
  const psx_hir_node_t *bit_xor =
      test_hir_child(&expression, bit_or, 1);
  ASSERT_EQ(ND_BITOR, syntax->kind);
  ASSERT_EQ(1, as_num(syntax->lhs)->val);
  ASSERT_EQ(ND_BITXOR, syntax->rhs->kind);
  ASSERT_EQ(2, as_num(syntax->rhs->lhs)->val);
  ASSERT_EQ(ND_BITAND, syntax->rhs->rhs->kind);
  ASSERT_EQ(PSX_HIR_BITOR, psx_hir_node_kind(bit_or));
  ASSERT_EQ(PSX_HIR_BITXOR, psx_hir_node_kind(bit_xor));
  ASSERT_EQ(
      PSX_HIR_BITAND,
      psx_hir_node_kind(test_hir_child(&expression, bit_xor, 1)));
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_shift(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_shift...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "1 + 2 << 3 >> 1", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  const psx_hir_node_t *left_shift =
      test_hir_child(&expression, root, 0);
  ASSERT_EQ(ND_SHR, syntax->kind);
  ASSERT_EQ(ND_SHL, syntax->lhs->kind);
  ASSERT_EQ(ND_ADD, syntax->lhs->lhs->kind);
  ASSERT_EQ(1, as_num(syntax->rhs)->val);
  ASSERT_EQ(PSX_HIR_SHR, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_SHL, psx_hir_node_kind(left_shift));
  ASSERT_EQ(
      PSX_HIR_ADD,
      psx_hir_node_kind(test_hir_child(&expression, left_shift, 0)));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(unsigned char)a >> 1", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(ND_SHR, syntax->kind);
  ASSERT_EQ(PSX_HIR_SHR, psx_hir_node_kind(root));
  psx_type_shape_t shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);
  ASSERT_TRUE(!shape.is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(unsigned int)a >> 1", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(ND_SHR, syntax->kind);
  ASSERT_EQ(PSX_HIR_SHR, psx_hir_node_kind(root));
  shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);
  ASSERT_TRUE(shape.is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(int)(unsigned long)a", &syntax);
  root = test_expression_hir_root(&expression);
  const psx_hir_node_t *inner_cast =
      test_hir_child(&expression, root, 0);
  ASSERT_EQ(ND_SOURCE_CAST, syntax->kind);
  ASSERT_EQ(ND_SOURCE_CAST, syntax->lhs->kind);
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(inner_cast));
  ASSERT_TRUE(!test_hir_type_shape(test_suite_session, root).is_unsigned);
  ASSERT_TRUE(test_hir_type_shape(test_suite_session, inner_cast).is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(signed)(unsigned long)a", &syntax);
  root = test_expression_hir_root(&expression);
  inner_cast = test_hir_child(&expression, root, 0);
  ASSERT_EQ(ND_SOURCE_CAST, syntax->kind);
  ASSERT_EQ(ND_SOURCE_CAST, syntax->lhs->kind);
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(inner_cast));
  ASSERT_TRUE(!test_hir_type_shape(test_suite_session, root).is_unsigned);
  ASSERT_TRUE(test_hir_type_shape(test_suite_session, inner_cast).is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(unsigned)(long)a", &syntax);
  root = test_expression_hir_root(&expression);
  inner_cast = test_hir_child(&expression, root, 0);
  ASSERT_EQ(ND_SOURCE_CAST, syntax->kind);
  ASSERT_EQ(ND_SOURCE_CAST, syntax->lhs->kind);
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(inner_cast));
  ASSERT_TRUE(test_hir_type_shape(test_suite_session, root).is_unsigned);
  ASSERT_TRUE(!test_hir_type_shape(test_suite_session, inner_cast).is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_ternary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_ternary...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session,
          "1 ? 2 : 3 ? 4 : 5", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_TERNARY, syntax->kind);
  ASSERT_EQ(1, as_num(syntax->lhs)->val);
  ASSERT_EQ(2, as_num(syntax->rhs)->val);
  ASSERT_EQ(ND_TERNARY, as_ctrl(syntax)->els->kind); // 右結合
  ASSERT_EQ(PSX_HIR_TERNARY, psx_hir_node_kind(root));
  ASSERT_EQ(3, psx_hir_node_child_count(root));
  ASSERT_EQ(
      PSX_HIR_TERNARY,
      psx_hir_node_kind(test_hir_child(&expression, root, 2)));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_unary_ops(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_unary_ops...\n");

  const struct {
    const char *input;
    psx_syntax_node_kind_t syntax_kind;
    psx_hir_node_kind_t hir_kind;
    long long operand;
  } unary_cases[] = {
      {"+42", ND_UNARY_PLUS, PSX_HIR_UNARY_PLUS, 42},
      {"-42", ND_UNARY_NEGATE, PSX_HIR_NEGATE, 42},
      {"!0", ND_LOGICAL_NOT, PSX_HIR_LOGICAL_NOT, 0},
      {"~5", ND_BITWISE_NOT, PSX_HIR_BITWISE_NOT, 5},
  };
  for (size_t i = 0; i < sizeof(unary_cases) / sizeof(unary_cases[0]); i++) {
    node_t *syntax = NULL;
    psx_frontend_expression_hir_t expression =
        resolve_test_expression_input_hir(test_suite_session,
            unary_cases[i].input, &syntax);
    const psx_hir_node_t *root =
        test_expression_hir_root(&expression);
    ASSERT_EQ(unary_cases[i].syntax_kind, syntax->kind);
    ASSERT_EQ(ND_NUM, syntax->lhs->kind);
    ASSERT_EQ(unary_cases[i].operand, as_num(syntax->lhs)->val);
    ASSERT_EQ(unary_cases[i].hir_kind, psx_hir_node_kind(root));
    psx_type_shape_t shape = test_hir_type_shape(test_suite_session, root);
    ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
    ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);
    psx_frontend_expression_hir_dispose(&expression);
  }

  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_cast_input_hir(test_suite_session, "(void)1", &syntax);
  const psx_hir_node_t *root =
      test_expression_hir_root(&expression);
  ASSERT_EQ(ND_NUM, syntax->lhs->kind);
  ASSERT_EQ(1, as_num(syntax->lhs)->val);
  ASSERT_EQ(PSX_TYPE_VOID, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(int *)0x1000", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(ND_NUM, syntax->lhs->kind);
  ASSERT_EQ(0x1000, as_num(syntax->lhs)->val);
  psx_qual_type_t type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, type.type_id));
  psx_qual_type_t base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_INTEGER, test_qual_type_shape(test_suite_session, base).kind);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(double (*)[2])0", &syntax);
  root = test_expression_hir_root(&expression);
  type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, type.type_id));
  base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_ARRAY, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(2, test_qual_type_shape(test_suite_session, base).array_len);
  ASSERT_EQ(16, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            test_qual_type_shape(test_suite_session, base).floating_kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, base.type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session, "(int **)0", &syntax);
  root = test_expression_hir_root(&expression);
  type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_INTEGER, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(4, test_type_size_id(test_suite_session, base.type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(long (*)[2])0", &syntax);
  root = test_expression_hir_root(&expression);
  type = psx_hir_node_qual_type(root);
  base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_ARRAY, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(16, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG,
            test_qual_type_shape(test_suite_session, base).integer_kind);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(long * (*)[2])0", &syntax);
  root = test_expression_hir_root(&expression);
  type = psx_hir_node_qual_type(root);
  base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_ARRAY, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(16, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, base).kind);
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG,
            test_qual_type_shape(test_suite_session, base).integer_kind);
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(int * (*)[2][3])0", &syntax);
  root = test_expression_hir_root(&expression);
  type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, type.type_id));
  base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_ARRAY, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(2, test_qual_type_shape(test_suite_session, base).array_len);
  ASSERT_EQ(48, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_ARRAY, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(3, test_qual_type_shape(test_suite_session, base).array_len);
  ASSERT_EQ(24, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_INTEGER, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(4, test_type_size_id(test_suite_session, base.type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(unsigned char (*)[3])0", &syntax);
  root = test_expression_hir_root(&expression);
  base = test_qual_type_base(test_suite_session, psx_hir_node_qual_type(root));
  ASSERT_EQ(PSX_TYPE_ARRAY, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(3, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_INTEGER_KIND_CHAR,
            test_qual_type_shape(test_suite_session, base).integer_kind);
  ASSERT_TRUE(test_qual_type_shape(test_suite_session, base).is_unsigned);
  ASSERT_EQ(1, test_type_size_id(test_suite_session, base.type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(_Bool (*)[2])0", &syntax);
  root = test_expression_hir_root(&expression);
  base = test_qual_type_base(test_suite_session, psx_hir_node_qual_type(root));
  ASSERT_EQ(PSX_TYPE_ARRAY, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(2, test_type_size_id(test_suite_session, base.type_id));
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_BOOL, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(1, test_type_size_id(test_suite_session, base.type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session, "(_Bool *)0", &syntax);
  root = test_expression_hir_root(&expression);
  base = test_qual_type_base(test_suite_session, psx_hir_node_qual_type(root));
  ASSERT_EQ(PSX_TYPE_BOOL, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(1, test_type_size_id(test_suite_session, base.type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session, "(_Bool)3", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(ND_NUM, syntax->lhs->kind);
  ASSERT_EQ(3, as_num(syntax->lhs)->val);
  ASSERT_EQ(PSX_TYPE_BOOL, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);

  assert_test_integer_cast_hir(test_suite_session,
      "(const int)7", PSX_INTEGER_KIND_INT, 0,
      PSX_TYPE_QUALIFIER_CONST, 7);
  assert_test_integer_cast_hir(test_suite_session,
      "(volatile int)8", PSX_INTEGER_KIND_INT, 0,
      PSX_TYPE_QUALIFIER_VOLATILE, 8);
  assert_test_integer_cast_hir(test_suite_session,
      "(int const)12", PSX_INTEGER_KIND_INT, 0,
      PSX_TYPE_QUALIFIER_CONST, 12);
  assert_test_integer_cast_hir(test_suite_session,
      "(int const const)21", PSX_INTEGER_KIND_INT, 0,
      PSX_TYPE_QUALIFIER_CONST, 21);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(int const * volatile * restrict)0", &syntax);
  root = test_expression_hir_root(&expression);
  type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_RESTRICT, type.qualifiers);
  base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_VOLATILE, base.qualifiers);
  base = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_INTEGER, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST, base.qualifiers);
  assert_canonical_qual_type_signature(test_suite_session,
      psx_hir_node_qual_type(root), "Rp<Vp<ki32>>");
  psx_frontend_expression_hir_dispose(&expression);

  assert_test_integer_cast_hir(test_suite_session,
      "(unsigned int const)13", PSX_INTEGER_KIND_INT, 1,
      PSX_TYPE_QUALIFIER_CONST, 13);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(int (*const)(int))0", &syntax);
  root = test_expression_hir_root(&expression);
  type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_CONST, type.qualifiers);
  base = test_qual_type_base(test_suite_session, type);
  psx_type_shape_t function_shape = test_qual_type_shape(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(1, function_shape.parameter_count);
  ASSERT_TRUE(function_shape.has_function_prototype);
  psx_qual_type_t function_result = test_qual_type_base(test_suite_session, base);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_qual_type_shape(test_suite_session, function_result).kind);
  const psx_hir_node_t *funcptr_operand =
      test_hir_child(&expression, root, 0);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(funcptr_operand));
  ASSERT_EQ(0, psx_hir_node_integer_value(funcptr_operand));
  psx_frontend_expression_hir_dispose(&expression);

  assert_test_integer_cast_hir(test_suite_session,
      "(long long)14", PSX_INTEGER_KIND_LONG_LONG, 0,
      PSX_TYPE_QUALIFIER_NONE, 14);
  assert_test_integer_cast_hir(test_suite_session,
      "(unsigned long)15", PSX_INTEGER_KIND_LONG, 1,
      PSX_TYPE_QUALIFIER_NONE, 15);

  assert_test_nested_integer_cast_hir(test_suite_session,
      "(long)(unsigned int)a", PSX_INTEGER_KIND_LONG, 0,
      PSX_INTEGER_KIND_INT, 1);
  assert_test_nested_integer_cast_hir(test_suite_session,
      "(long)(int)a", PSX_INTEGER_KIND_LONG, 0,
      PSX_INTEGER_KIND_INT, 0);

  assert_test_integer_cast_hir(test_suite_session,
      "(unsigned short int)16", PSX_INTEGER_KIND_SHORT, 1,
      PSX_TYPE_QUALIFIER_NONE, 16);
  assert_test_integer_cast_hir(test_suite_session,
      "(signed char)17", PSX_INTEGER_KIND_CHAR, 0,
      PSX_TYPE_QUALIFIER_NONE, 17);
  assert_test_integer_cast_hir(test_suite_session,
      "(unsigned char)18", PSX_INTEGER_KIND_CHAR, 1,
      PSX_TYPE_QUALIFIER_NONE, 18);

  assert_test_nested_integer_cast_hir(test_suite_session,
      "(long)(unsigned char)a", PSX_INTEGER_KIND_LONG, 0,
      PSX_INTEGER_KIND_CHAR, 1);
  assert_test_nested_integer_cast_hir(test_suite_session,
      "(long)(unsigned short)a", PSX_INTEGER_KIND_LONG, 0,
      PSX_INTEGER_KIND_SHORT, 1);
  assert_test_nested_integer_cast_hir(test_suite_session,
      "(unsigned)(short)a", PSX_INTEGER_KIND_INT, 1,
      PSX_INTEGER_KIND_SHORT, 0);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int cast_unsigned_short_compare(short s) { "
      "return (unsigned)s > 5; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *comparison =
      find_test_hir_node_kind(hir, PSX_HIR_GT, 0);
  ASSERT_TRUE(comparison != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, comparison).kind);
  ASSERT_TRUE(!test_hir_type_shape(test_suite_session, comparison).is_unsigned);
  const psx_hir_node_t *converted_short = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(comparison, 0));
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(converted_short));
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_hir_type_shape(test_suite_session, converted_short).kind);
  ASSERT_EQ(PSX_INTEGER_KIND_INT,
            test_hir_type_shape(test_suite_session, converted_short).integer_kind);
  ASSERT_TRUE(test_hir_type_shape(test_suite_session, converted_short).is_unsigned);

  assert_test_nested_integer_cast_hir(test_suite_session,
      "(long)(short)a", PSX_INTEGER_KIND_LONG, 0,
      PSX_INTEGER_KIND_SHORT, 0);

  const char *restrict_casts[] = {
      "(int *restrict)0",
      "(int *restrict restrict)0",
  };
  for (size_t i = 0;
       i < sizeof(restrict_casts) / sizeof(restrict_casts[0]); i++) {
    expression = resolve_test_cast_input_hir(test_suite_session,
        restrict_casts[i], &syntax);
    root = test_expression_hir_root(&expression);
    type = psx_hir_node_qual_type(root);
    ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
    ASSERT_EQ(PSX_TYPE_QUALIFIER_RESTRICT, type.qualifiers);
    assert_canonical_qual_type_signature(test_suite_session, type, "Rp<i32>");
    psx_frontend_expression_hir_dispose(&expression);
  }

  assert_test_integer_cast_hir(test_suite_session,
      "(_Atomic int)9", PSX_INTEGER_KIND_INT, 0,
      PSX_TYPE_QUALIFIER_ATOMIC, 9);
  assert_test_integer_cast_hir(test_suite_session,
      "(_Atomic const int)10", PSX_INTEGER_KIND_INT, 0,
      PSX_TYPE_QUALIFIER_ATOMIC | PSX_TYPE_QUALIFIER_CONST, 10);
  assert_test_integer_cast_hir(test_suite_session,
      "(_Atomic(_Atomic(int)))11", PSX_INTEGER_KIND_INT, 0,
      PSX_TYPE_QUALIFIER_ATOMIC, 11);

  const char *aggregate_cast_cases[] = {
      "int main() { struct S { int x; }; struct S a={1}, b={2}; "
      "int c=1; struct S s=c?a:(struct S){3}; return s.x; }",
      "int main() { struct S { int x; }; struct S a={1}, b={2}; "
      "int c=1; struct S s=(c?(struct S){3}:b); return s.x; }",
      "int main() { struct S { int x; }; struct S a={1}; "
      "struct S s=(a,(struct S){9}); return s.x; }",
      "int main() { struct S { int x; }; struct S a={1}, b={2}; "
      "int c=1; struct S t=(struct S)(c?a:b); return t.x; }",
      "int main() { union U { int x; char y; }; "
      "union U a={.x=1}, b={.x=2}; int c=1; "
      "union U t=(union U)(c?a:b); return t.x; }",
  };
  for (size_t i = 0;
       i < sizeof(aggregate_cast_cases) / sizeof(aggregate_cast_cases[0]);
       i++) {
    reset_test_translation_unit_state(test_suite_session);
    ASSERT_TRUE(resolve_program_input_hir(test_suite_session, aggregate_cast_cases[i]));
    hir = ag_compilation_session_hir_module(test_suite_session);
    ASSERT_EQ(1, psx_hir_module_root_count(hir));
    ASSERT_EQ(PSX_HIR_FUNCTION,
              psx_hir_node_kind(psx_hir_module_lookup(
                  hir, psx_hir_module_root_at(hir, 0))));
  }
}

static void assert_test_program_return_hir_integer_at(
    ag_compilation_session_t *test_suite_session,
    const char *source, size_t return_occurrence,
    long long expected_value,
    psx_integer_kind_t expected_kind, int expected_unsigned) {
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session, source));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *ret =
      find_test_hir_node_kind(
          hir, PSX_HIR_RETURN, return_occurrence);
  ASSERT_TRUE(ret != NULL);
  ASSERT_EQ(1, psx_hir_node_child_count(ret));
  const psx_hir_node_t *value = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(ret, 0));
  ASSERT_TRUE(value != NULL);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(value));
  ASSERT_EQ(expected_value, psx_hir_node_integer_value(value));
  psx_type_shape_t shape = test_hir_type_shape(test_suite_session, value);
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(expected_kind, shape.integer_kind);
  ASSERT_EQ(expected_unsigned, shape.is_unsigned);
}

static void assert_test_program_return_hir_int(
    ag_compilation_session_t *test_suite_session,
    const char *source, long long expected_value) {
  assert_test_program_return_hir_integer_at(test_suite_session,
      source, 0, expected_value, PSX_INTEGER_KIND_INT, 0);
}

static void assert_test_program_return_hir_int_at(
    ag_compilation_session_t *test_suite_session,
    const char *source, size_t return_occurrence,
    long long expected_value) {
  assert_test_program_return_hir_integer_at(test_suite_session,
      source, return_occurrence, expected_value,
      PSX_INTEGER_KIND_INT, 0);
}

static void assert_test_program_return_hir_number(
    ag_compilation_session_t *test_suite_session,
    const char *source, long long expected_value) {
  assert_test_program_return_hir_integer_at(test_suite_session,
      source, 0, expected_value, PSX_INTEGER_KIND_LONG, 1);
}

static void test_expr_generic(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_generic...\n");

  assert_test_program_return_hir_int(test_suite_session,
      "typedef double ExprCanonicalParam; "
      "int main(void){ return _Generic("
      "(int (*)(ExprCanonicalParam, int *, ...))0, "
      "int (*)(ExprCanonicalParam, int *, ...): 53, default: 7); }",
      53);
  node_t *canonical_expr_funcptr = parse_expr_input_with_existing_locals(test_suite_session,
      "(int (*)(ExprCanonicalParam, int *, ...))0");
  ASSERT_EQ(ND_SOURCE_CAST, canonical_expr_funcptr->kind);
  node_t *canonical_expr_operand = canonical_expr_funcptr->lhs;
  psx_frontend_expression_hir_t canonical_funcptr_expression =
      resolve_test_expression_hir(test_suite_session, canonical_expr_funcptr);
  const psx_hir_node_t *canonical_funcptr_hir =
      test_expression_hir_root(&canonical_funcptr_expression);
  ASSERT_EQ(PSX_HIR_CAST,
            psx_hir_node_kind(canonical_funcptr_hir));
  psx_type_shape_t pointer_shape =
      test_hir_type_shape(test_suite_session, canonical_funcptr_hir);
  ASSERT_EQ(PSX_TYPE_POINTER, pointer_shape.kind);
  const psx_semantic_type_table_t *canonical_types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  psx_qual_type_t canonical_function = psx_semantic_type_table_base(
      canonical_types,
      psx_hir_node_qual_type(canonical_funcptr_hir).type_id);
  psx_type_shape_t function_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      canonical_types, canonical_function.type_id, &function_shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(2, function_shape.parameter_count);
  ASSERT_TRUE(function_shape.is_variadic_function);
  psx_qual_type_t first_parameter =
      psx_semantic_type_table_parameter(
          canonical_types, canonical_function.type_id, 0);
  psx_qual_type_t second_parameter =
      psx_semantic_type_table_parameter(
          canonical_types, canonical_function.type_id, 1);
  psx_type_shape_t first_parameter_shape = {0};
  psx_type_shape_t second_parameter_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      canonical_types, first_parameter.type_id,
      &first_parameter_shape));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      canonical_types, second_parameter.type_id,
      &second_parameter_shape));
  ASSERT_EQ(PSX_TYPE_FLOAT, first_parameter_shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            first_parameter_shape.floating_kind);
  ASSERT_EQ(PSX_TYPE_POINTER, second_parameter_shape.kind);
  ASSERT_EQ(ND_SOURCE_CAST, canonical_expr_funcptr->kind);
  ASSERT_TRUE(canonical_expr_funcptr->lhs == canonical_expr_operand);
  psx_frontend_expression_hir_dispose(
      &canonical_funcptr_expression);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "typedef int (*unary_fn)(int); int generic_id(int x){return x;} "
      "int main(){return 0;}"));
  psx_typedef_info_t unary_info = {0};
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(test_semantic_context(test_suite_session), (char *)"unary_fn", 8, &unary_info));
  psx_qual_type_t generic_id_function =
      ps_ctx_get_function_qual_type_in(
          test_semantic_context(test_suite_session), (char *)"generic_id", 10);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            test_qual_type_shape(test_suite_session, generic_id_function).kind);
  psx_qual_type_t unary_type =
      ps_ctx_typedef_decl_qual_type(&unary_info);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, unary_type).kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            test_qual_type_shape(test_suite_session,
                test_qual_type_base(test_suite_session, unary_type)).kind);
  assert_canonical_qual_type_signature(test_suite_session,
      generic_id_function, "i32(i32)");
  assert_canonical_qual_type_signature(test_suite_session, unary_type, "p<i32(i32)>");
  node_t *generic_id_syntax = NULL;
  psx_frontend_expression_hir_t generic_id_expression =
      resolve_test_expression_input_hir(test_suite_session,
          "generic_id", &generic_id_syntax);
  const psx_hir_node_t *generic_id_ref =
      test_expression_hir_root(&generic_id_expression);
  ASSERT_EQ(ND_IDENTIFIER, generic_id_syntax->kind);
  ASSERT_EQ(PSX_HIR_FUNCTION_REF, psx_hir_node_kind(generic_id_ref));
  psx_qual_type_t generic_id_ref_type =
      psx_hir_node_qual_type(generic_id_ref);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, generic_id_ref_type).kind);
  ASSERT_TRUE(psx_semantic_type_table_unqualified_types_match(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      generic_id_ref_type, ps_ctx_typedef_decl_qual_type(&unary_info)));
  psx_frontend_expression_hir_dispose(&generic_id_expression);
  assert_test_program_return_hir_int_at(test_suite_session,
      "typedef int (*unary_fn)(int); int id(int x){return x;} "
      "int main(){return _Generic(id, unary_fn:9, int:4, default:5);}",
      1, 9);

  assert_test_generic_number_hir(test_suite_session,
      "_Generic(1, int: 11, default: 22)", 11);
  assert_test_generic_number_hir(test_suite_session,
      "_Generic(1.0, float: 11, double: 33, default: 22)", 33);
  assert_test_generic_number_hir(test_suite_session,
      "_Generic(1, _Atomic(int): 41, default: 42)", 41);
  expect_parse_ok(test_suite_session,
      "int main(){ int *p=0; return _Generic(p, _Atomic(int *):1, default:2); }");
  expect_parse_ok(test_suite_session,
      "int main(){ return _Generic(1, _Atomic(_Atomic(int)):1, default:2); }");

  assert_test_program_return_hir_int(test_suite_session,
      "int main() { int *p=0; "
      "return _Generic(p, int*: 3, default: 7); }",
      3);

  assert_test_program_return_hir_int_at(test_suite_session,
      "typedef int (*fp_t)(int); "
      "int f(int x){ return x; } "
      "int main(){ fp_t p=f; "
      "return _Generic(p, int (*)(int): 13, default: 7); }",
      1, 13);

  assert_test_program_return_hir_int_at(test_suite_session,
      "double fd(double x){ return x; } "
      "int main(){ return _Generic("
      "fd, double (*)(double): 17, default: 7); }",
      1, 17);

  assert_test_program_return_hir_int_at(test_suite_session,
      "int fg(int x){ return x; } "
      "int main(){ int (*p)(int)=fg; "
      "return _Generic((p), int (*)(int): 19, default: 7); }",
      1, 19);

  assert_test_program_return_hir_int_at(test_suite_session,
      "int (*__tm_gen_rowfn(void))[3] { return 0; } "
      "int main(){ int (*(*p)(void))[3]=__tm_gen_rowfn; "
      "return _Generic((p), int (*(*)(void))[3]: 23, default: 7); }",
      1, 23);

  assert_test_program_return_hir_int_at(test_suite_session,
      "int (*__tm_gen_growfn(void))[3] { return 0; } "
      "int (*(*__tm_gen_gfp)(void))[3]; "
      "int main(){ return _Generic((__tm_gen_gfp), "
      "int (*(*)(void))[3]: 29, default: 7); }",
      1, 29);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(void) { int (*(*p)(void))[3]; "
      "return _Generic(p, int (*(*)(void))[3]: 31, default: 7); }",
      31);

  const char *returned_function_pointer_cases[] = {
      "int main(void) { int (*(*q)(void))(int); "
      "return _Generic(q, int (*(*)(void))(int): 37, default: 7); }",
      "int main(void) { int (*(*q)(void))(int); "
      "return _Generic(q, int (*(*)(void))(double): 41, default: 7); }",
      "int main(void) { int (*(*q)(void))(int); "
      "return _Generic(q, double (*(*)(void))(int): 43, default: 7); }",
      "int main(void) { double (*(*r)(void))(int); "
      "return _Generic(r, double (*(*)(void))(int): 47, default: 7); }",
      "int main(void) { double (*(*r)(void))(int); "
      "return _Generic(r, int (*(*)(void))(int): 49, default: 7); }",
  };
  const int returned_function_pointer_expected[] = {37, 7, 7, 47, 7};
  for (size_t i = 0;
       i < sizeof(returned_function_pointer_cases) /
               sizeof(returned_function_pointer_cases[0]);
       i++) {
    assert_test_program_return_hir_int(test_suite_session,
        returned_function_pointer_cases[i],
        returned_function_pointer_expected[i]);
  }

  expect_parse_ok(test_suite_session,
      "int main(){ struct S{int x;}; return _Generic((struct S){1}, struct S: 1, default: 2); }");
  expect_parse_ok(test_suite_session,
      "int main(){ union U{int x;}; return _Generic((union U){.x=1}, union U: 1, default: 2); }");
  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ struct S{int x;}; "
      "return _Generic((struct S){1}, struct S: 1, default: 2); }",
      1);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ struct S{int x;}; struct T{int x;}; "
      "return _Generic((struct S){1}, struct T: 1, default: 2); }",
      2);
  expect_parse_ok(test_suite_session,
      "int main(){ int *p=0; return _Generic(p, int[3]: 1, default: 2); }");
  expect_parse_ok(test_suite_session,
      "int main(){ double d=1.0; double *p=&d; return _Generic(*p, double:42, default:99); }");
  expect_parse_ok(test_suite_session,
      "int main(){ float f=1.0f; float *p=&f; return _Generic(*p, float:11, default:99); }");
  expect_parse_ok(test_suite_session,
      "int main(){ double a[1]={1.0}; double *p=a; return _Generic(p[0], double:42, default:99); }");
  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ int x=0; char c=0; int *pi=&x; char *pc=&c; "
      "return _Generic(pc, int*:1, char*:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ double d=1.0; double *pd=&d; "
      "return _Generic(pd, int*:1, double*:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ struct S{int x;}; struct T{int x;}; "
      "struct S s={1}; struct S *ps=&s; "
      "return _Generic(ps, struct T*:1, struct S*:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ int x=0; const int *p=&x; "
      "return _Generic(p, int*:1, const int*:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "typedef const int *cip_t; int main(){ int x=0; cip_t p=&x; "
      "return _Generic(p, int*:1, const int*:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "typedef volatile int *vip_t; int main(){ int x=0; vip_t p=&x; "
      "return _Generic(p, volatile int*:2, int*:1, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ int x=0; char c=0; int *pi=&x; char *pc=&c; "
      "int **ppi=&pi; "
      "return _Generic(ppi, char**:1, int**:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ int x=0; unsigned int u=0; unsigned int *pu=&u; "
      "return _Generic(pu, int*:1, unsigned int*:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "typedef unsigned int *uip_t; int main(){ unsigned int u=0; "
      "uip_t pu=&u; "
      "return _Generic(pu, int*:1, unsigned int*:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ int x=0; int *p=&x; int * const *pp=&p; "
      "return _Generic(pp, int**:1, int * const *:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ int x=0; int *p=&x; int * volatile *pp=&p; "
      "return _Generic(pp, int**:1, int * volatile *:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ unsigned long ul=1; "
      "return _Generic(ul, unsigned long:2, unsigned int:1, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ long l=1; "
      "return _Generic(l, unsigned long:1, long:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ return _Generic((1 ? (char)1 : (char)2), "
      "char:1, int:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ return _Generic("
      "(1 ? (long double)1.0 : (double)2.0), "
      "long double:4, double:5, default:6); }",
      4);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ return _Generic((long double){1.0}, "
      "long double:4, double:5, default:6); }",
      4);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ return _Generic((_Complex double)1, "
      "double:5, _Complex double:4, default:6); }",
      4);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ _Complex double z=1; return _Generic(z, "
      "double:5, _Complex double:4, default:6); }",
      4);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ return _Generic(1, int const:2, default:3); }",
      2);

  assert_test_program_return_hir_int(test_suite_session,
      "int main(){ int x=0; int const *p=&x; "
      "return _Generic(p, int const *:2, int *:1, default:3); }",
      2);
}

static void test_expr_sizeof(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_sizeof...\n");

  const struct {
    const char *input;
    psx_syntax_node_kind_t syntax_kind;
    long long value;
  } type_queries[] = {
      {"sizeof(int)", ND_SIZEOF_QUERY, 4},
      {"sizeof(void)", ND_SIZEOF_QUERY, 1},
      {"sizeof(int*)", ND_SIZEOF_QUERY, 8},
      {"sizeof(int * const)", ND_SIZEOF_QUERY, 8},
      {"sizeof(int * volatile)", ND_SIZEOF_QUERY, 8},
      {"sizeof(int * restrict)", ND_SIZEOF_QUERY, 8},
      {"sizeof(int[10])", ND_SIZEOF_QUERY, 40},
      {"sizeof(int (*)[3])", ND_SIZEOF_QUERY, 8},
      {"sizeof((int[3]))", ND_SIZEOF_QUERY, 12},
      {"sizeof(int (*)(int))", ND_SIZEOF_QUERY, 8},
      {"sizeof(_Complex double)", ND_SIZEOF_QUERY, 16},
      {"sizeof(float _Imaginary)", ND_SIZEOF_QUERY, 8},
      {"_Alignof(int)", ND_ALIGNOF_QUERY, 4},
      {"_Alignof(int*)", ND_ALIGNOF_QUERY, 8},
      {"_Alignof(int * const)", ND_ALIGNOF_QUERY, 8},
      {"_Alignof(int * volatile)", ND_ALIGNOF_QUERY, 8},
      {"_Alignof(int * restrict)", ND_ALIGNOF_QUERY, 8},
      {"_Alignof(int[10])", ND_ALIGNOF_QUERY, 4},
      {"_Alignof(int (*)[3])", ND_ALIGNOF_QUERY, 8},
      {"_Alignof((int[3]))", ND_ALIGNOF_QUERY, 4},
      {"_Alignof(_Imaginary double)", ND_ALIGNOF_QUERY, 8},
  };
  for (size_t i = 0;
       i < sizeof(type_queries) / sizeof(type_queries[0]); i++) {
    assert_test_type_query_hir(test_suite_session,
        type_queries[i].input, type_queries[i].syntax_kind,
        type_queries[i].value);
  }

  assert_test_program_return_hir_number(test_suite_session,
      "int main() { int x; return sizeof(x); }", 4);
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x; return sizeof(x); }", "W3004");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int a[3]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int n=3; int a[n]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int n=2,m=4; int v[n][m]; int idx=1; return sizeof(v[idx]); }", "W3003");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ static int a[3]; return sizeof(a); }", "W3003");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int (*p)[3][4]; return sizeof(*p); }", "W3004");

  assert_test_program_return_hir_number(test_suite_session,
      "int main() { struct S { int x; }; return sizeof(struct S); }",
      4);

  assert_test_program_return_hir_number(test_suite_session,
      "typedef struct Forward Forward; "
      "struct Forward { void *items; int count; int capacity; }; "
      "int main(void) { Forward *body = 0; return sizeof(*body); }",
      16);

  assert_test_program_return_hir_number(test_suite_session,
      "int main() { struct S { int x; }; "
      "return _Alignof(struct S); }",
      4);

  assert_test_program_return_hir_number(test_suite_session,
      "int main() { struct S { int x; }; "
      "return sizeof(struct S (*)[3]); }",
      8);

  assert_test_program_return_hir_number(test_suite_session,
      "typedef int A3[3]; int main() { return sizeof(A3 (*)[2]); }",
      8);

  assert_test_program_return_hir_number(test_suite_session,
      "int main() { struct S { int x; }; return sizeof(struct S[3]); }",
      12);

  assert_test_program_return_hir_number(test_suite_session,
      "typedef int A3[3]; int main() { return sizeof(A3[2]); }",
      24);

  assert_test_integer_cast_hir(test_suite_session,
      "(char)300", PSX_INTEGER_KIND_CHAR, 0,
      PSX_TYPE_QUALIFIER_NONE, 300);

  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_cast_input_hir(test_suite_session, "(_Complex double)1", &syntax);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  psx_type_shape_t shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_COMPLEX, shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE, shape.floating_kind);
  ASSERT_EQ(16,
            test_type_size_id(test_suite_session, psx_hir_node_qual_type(root).type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session,
      "(float _Imaginary)1", &syntax);
  root = test_expression_hir_root(&expression);
  shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_COMPLEX, shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_FLOAT, shape.floating_kind);
  ASSERT_EQ(8,
            test_type_size_id(test_suite_session, psx_hir_node_qual_type(root).type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session, "(long double)1", &syntax);
  root = test_expression_hir_root(&expression);
  shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_FLOAT, shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_LONG_DOUBLE, shape.floating_kind);
  psx_frontend_expression_hir_dispose(&expression);

  assert_test_integer_cast_hir(test_suite_session,
      "(_Atomic(int))1", PSX_INTEGER_KIND_INT, 0,
      PSX_TYPE_QUALIFIER_ATOMIC, 1);

  expression = resolve_test_cast_input_hir(test_suite_session, "(_Atomic(int*))0", &syntax);
  root = test_expression_hir_root(&expression);
  psx_qual_type_t type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  ASSERT_EQ(PSX_TYPE_QUALIFIER_ATOMIC, type.qualifiers);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, type.type_id));
  psx_qual_type_t base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_INTEGER, test_qual_type_shape(test_suite_session, base).kind);
  psx_frontend_expression_hir_dispose(&expression);

  const struct {
    const char *input;
    psx_integer_kind_t integer_kind;
    int is_unsigned;
    int size;
  } integer_casts[] = {
      {"(int)a", PSX_INTEGER_KIND_INT, 0, 4},
      {"(unsigned short)a", PSX_INTEGER_KIND_SHORT, 1, 2},
      {"(unsigned long)a", PSX_INTEGER_KIND_LONG, 1, 8},
  };
  for (size_t i = 0;
       i < sizeof(integer_casts) / sizeof(integer_casts[0]); i++) {
    expression = resolve_test_cast_input_hir(test_suite_session,
        integer_casts[i].input, &syntax);
    root = test_expression_hir_root(&expression);
    shape = test_hir_type_shape(test_suite_session, root);
    ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
    ASSERT_EQ(integer_casts[i].integer_kind, shape.integer_kind);
    ASSERT_EQ(integer_casts[i].is_unsigned, shape.is_unsigned);
    ASSERT_EQ(integer_casts[i].size,
              test_type_size_id(test_suite_session, psx_hir_node_qual_type(root).type_id));
    psx_frontend_expression_hir_dispose(&expression);
  }

  expression = resolve_test_cast_input_hir(test_suite_session, "(float)a", &syntax);
  root = test_expression_hir_root(&expression);
  shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_FLOAT, shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_FLOAT, shape.floating_kind);
  ASSERT_EQ(4,
            test_type_size_id(test_suite_session, psx_hir_node_qual_type(root).type_id));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_cast_input_hir(test_suite_session, "(double*)a", &syntax);
  root = test_expression_hir_root(&expression);
  type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, type.type_id));
  base = test_qual_type_base(test_suite_session, type);
  ASSERT_EQ(PSX_TYPE_FLOAT, test_qual_type_shape(test_suite_session, base).kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            test_qual_type_shape(test_suite_session, base).floating_kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, base.type_id));
  psx_frontend_expression_hir_dispose(&expression);

  assert_test_integer_expression_hir(test_suite_session,
      "(unsigned char)1 + (short)2", PSX_HIR_ADD,
      PSX_INTEGER_KIND_INT, 0, 4);
  assert_test_integer_expression_hir(test_suite_session,
      "(unsigned int)1 + (long)-1", PSX_HIR_ADD,
      PSX_INTEGER_KIND_LONG, 0, 8);
  assert_test_integer_expression_hir(test_suite_session,
      "(unsigned long)1 + (long)-1", PSX_HIR_ADD,
      PSX_INTEGER_KIND_LONG, 1, 8);
  assert_test_integer_expression_hir(test_suite_session,
      "((unsigned long long)9ULL) ^ ((unsigned short)3)",
      PSX_HIR_BITXOR, PSX_INTEGER_KIND_LONG_LONG, 1, 8);
  assert_test_integer_expression_hir(test_suite_session,
      "1 ? (unsigned int)1 : (long)-1", PSX_HIR_TERNARY,
      PSX_INTEGER_KIND_LONG, 0, 8);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "(unsigned int)1 < (long)-1", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_LT, psx_hir_node_kind(root));
  shape = test_hir_type_shape(test_suite_session, root);
  ASSERT_EQ(PSX_TYPE_INTEGER, shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);
  ASSERT_TRUE(!shape.is_unsigned);
  const psx_hir_node_t *left = test_hir_child(&expression, root, 0);
  const psx_hir_node_t *right = test_hir_child(&expression, root, 1);
  psx_type_shape_t left_shape = test_hir_type_shape(test_suite_session, left);
  psx_type_shape_t right_shape = test_hir_type_shape(test_suite_session, right);
  ASSERT_EQ(PSX_INTEGER_KIND_INT, left_shape.integer_kind);
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, right_shape.integer_kind);
  ASSERT_TRUE(left_shape.is_unsigned);
  ASSERT_TRUE(!right_shape.is_unsigned);
  psx_integer_conversion_t comparison_type =
      psx_usual_integer_conversion_for_data_layout(
          psx_integer_conversion_from_shape(&left_shape),
          psx_integer_conversion_from_shape(&right_shape),
          ps_ctx_data_layout(test_semantic_context(test_suite_session)));
  ASSERT_EQ(4, comparison_type.rank);
  ASSERT_TRUE(!comparison_type.is_unsigned);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_inc_dec_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_inc_dec_typed_hir_boundary...\n");
  reset_test_locals(test_suite_session);
  lvar_t *integer = register_test_storage_fixture(test_suite_session,
      (char *)"a", 1, 4, 4, 0);
  set_test_storage_fixture_type(test_suite_session,
      integer, ps_ctx_intern_integer_qual_type_in(
                   test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0));

  const struct {
    const char *source;
    psx_syntax_node_kind_t syntax_kind;
    psx_hir_node_kind_t hir_kind;
  } cases[] = {
      {"++a", ND_PRE_INC, PSX_HIR_PRE_INC},
      {"--a", ND_PRE_DEC, PSX_HIR_PRE_DEC},
      {"a++", ND_POST_INC, PSX_HIR_POST_INC},
      {"a--", ND_POST_DEC, PSX_HIR_POST_DEC},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    node_t *syntax =
        parse_expr_input_with_existing_locals(test_suite_session, cases[i].source);
    ASSERT_EQ(cases[i].syntax_kind, syntax->kind);
    ASSERT_EQ(ND_IDENTIFIER, syntax->lhs->kind);
    ASSERT_TRUE(syntax->tok != NULL);
    node_t *operand_syntax = syntax->lhs;
    psx_frontend_expression_hir_t expression =
        resolve_test_expression_hir(test_suite_session, syntax);
    const psx_hir_node_t *root =
        test_expression_hir_root(&expression);
    ASSERT_EQ(cases[i].hir_kind, psx_hir_node_kind(root));
    ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
    ASSERT_EQ(1, psx_hir_node_child_count(root));
    ASSERT_EQ(PSX_HIR_LOCAL,
              psx_hir_node_kind(psx_hir_module_lookup(
                  expression.module,
                  psx_hir_node_child_at(root, 0))));
    ASSERT_TRUE(syntax->lhs == operand_syntax);
    psx_frontend_expression_hir_dispose(&expression);
  }
}

static void test_expr_assign(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_assign...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "a = 3", &syntax);
  ASSERT_EQ(ND_ASSIGN, syntax->kind);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_ASSIGN, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_LOCAL,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  const psx_hir_node_t *rhs = test_hir_child(&expression, root, 1);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(rhs));
  ASSERT_EQ(3, psx_hir_node_integer_value(rhs));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, root).kind);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_compound_assign(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_compound_assign...\n");
  const struct {
    const char *input;
    token_kind_t syntax_operator;
    psx_hir_compound_operator_t hir_operator;
  } cases[] = {
      {"a += 3", TK_PLUSEQ, PSX_HIR_COMPOUND_ADD},
      {"a -= 3", TK_MINUSEQ, PSX_HIR_COMPOUND_SUB},
      {"a *= 3", TK_MULEQ, PSX_HIR_COMPOUND_MUL},
      {"a /= 3", TK_DIVEQ, PSX_HIR_COMPOUND_DIV},
      {"a %= 3", TK_MODEQ, PSX_HIR_COMPOUND_MOD},
      {"a <<= 3", TK_SHLEQ, PSX_HIR_COMPOUND_SHL},
      {"a >>= 3", TK_SHREQ, PSX_HIR_COMPOUND_SHR},
      {"a &= 3", TK_ANDEQ, PSX_HIR_COMPOUND_BITAND},
      {"a ^= 3", TK_XOREQ, PSX_HIR_COMPOUND_BITXOR},
      {"a |= 3", TK_OREQ, PSX_HIR_COMPOUND_BITOR},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    assert_test_compound_assignment_hir(test_suite_session,
        cases[i].input, cases[i].syntax_operator,
        cases[i].hir_operator);
  }
}

static void test_expr_comma(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_comma...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session,
          "a=1, b=2, a+b", &syntax);
  ASSERT_EQ(ND_COMMA, syntax->kind);
  ASSERT_EQ(ND_COMMA, syntax->lhs->kind);
  ASSERT_EQ(ND_ADD, syntax->rhs->kind);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_COMMA, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_COMMA,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  ASSERT_EQ(PSX_HIR_ADD,
            psx_hir_node_kind(test_hir_child(&expression, root, 1)));
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_program_funcdef(
    ag_compilation_session_t *test_suite_session) {
  printf("test_program_funcdef...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { int a=1; int b=2; a+b; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  const psx_hir_node_t *function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, 0));
  ASSERT_TRUE(function != NULL);
  ASSERT_EQ(PSX_HIR_FUNCTION, psx_hir_node_kind(function));
  ASSERT_EQ(1, psx_hir_node_child_count(function));
  ASSERT_EQ(PSX_HIR_EDGE_FUNCTION_BODY,
            psx_hir_node_child_edge_at(function, 0));
  const psx_hir_node_t *body = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(function, 0));
  ASSERT_TRUE(body != NULL);
  ASSERT_EQ(PSX_HIR_BLOCK, psx_hir_node_kind(body));
  ASSERT_EQ(3, psx_hir_node_child_count(body));
  int assignment_count = 0;
  int addition_count = 0;
  int found_first_local = 0;
  int found_second_local = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    ASSERT_TRUE(node != NULL);
    if (psx_hir_node_kind(node) == PSX_HIR_ASSIGN)
      assignment_count++;
    if (psx_hir_node_kind(node) == PSX_HIR_ADD)
      addition_count++;
    if (psx_hir_node_kind(node) == PSX_HIR_LOCAL) {
      if (psx_hir_node_storage_offset(node) == 0)
        found_first_local = 1;
      if (psx_hir_node_storage_offset(node) == 4)
        found_second_local = 1;
    }
  }
  ASSERT_EQ(2, assignment_count);
  ASSERT_EQ(1, addition_count);
  ASSERT_TRUE(found_first_local);
  ASSERT_TRUE(found_second_local);
}

static void test_funcall(
    ag_compilation_session_t *test_suite_session) {
  printf("test_funcall...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "add(1, 2)", &syntax);
  ASSERT_EQ(ND_FUNCALL, syntax->kind);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_CALL, psx_hir_node_kind(root));
  ASSERT_EQ(2, psx_hir_node_child_count(root));
  ASSERT_EQ(1, psx_hir_node_integer_value(
                   test_hir_child(&expression, root, 0)));
  ASSERT_EQ(2, psx_hir_node_integer_value(
                   test_hir_child(&expression, root, 1)));
  psx_frontend_expression_hir_dispose(&expression);

  expression = resolve_test_expression_input_hir(test_suite_session,
      "foo((1,2), 3)", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(ND_FUNCALL, syntax->kind);
  ASSERT_EQ(PSX_HIR_CALL, psx_hir_node_kind(root));
  ASSERT_EQ(2, psx_hir_node_child_count(root));
  ASSERT_EQ(PSX_HIR_COMMA,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  const psx_hir_node_t *second_argument =
      test_hir_child(&expression, root, 1);
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(second_argument));
  ASSERT_EQ(3, psx_hir_node_integer_value(second_argument));
  psx_frontend_expression_hir_dispose(&expression);
}

// --- ここから追加テスト ---

static void test_funcdef_with_params(
    ag_compilation_session_t *test_suite_session) {
  printf("test_funcdef_with_params...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int add(int a, int b) { return a+b; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  const psx_hir_node_t *function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, 0));
  ASSERT_EQ(PSX_HIR_FUNCTION, psx_hir_node_kind(function));
  psx_type_shape_t function_shape = test_qual_type_shape(test_suite_session,
      psx_hir_node_attached_qual_type(function));
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(2, function_shape.parameter_count);
  ASSERT_EQ(3, psx_hir_node_child_count(function));
  for (size_t i = 0; i < 2; i++) {
    ASSERT_EQ(PSX_HIR_EDGE_PARAMETER,
              psx_hir_node_child_edge_at(function, i));
    ASSERT_EQ(PSX_HIR_LOCAL,
              psx_hir_node_kind(psx_hir_module_lookup(
                  hir, psx_hir_node_child_at(function, i))));
  }

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int apply(int (*fp)(int), int x) { return x; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, 0));
  function_shape = test_qual_type_shape(test_suite_session,
      psx_hir_node_attached_qual_type(function));
  ASSERT_EQ(2, function_shape.parameter_count);
  psx_qual_type_t first_parameter = psx_semantic_type_table_parameter(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      psx_hir_node_attached_qual_type(function).type_id, 0);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, first_parameter).kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            test_qual_type_shape(test_suite_session, test_qual_type_base(test_suite_session, first_parameter)).kind);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int sum(int a[], int n) { return n; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, 0));
  function_shape = test_qual_type_shape(test_suite_session,
      psx_hir_node_attached_qual_type(function));
  ASSERT_EQ(2, function_shape.parameter_count);
  first_parameter = psx_semantic_type_table_parameter(
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session)),
      psx_hir_node_attached_qual_type(function).type_id, 0);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, first_parameter).kind);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int sum_variadic(int first, ...) { return first; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, 0));
  function_shape = test_qual_type_shape(test_suite_session,
      psx_hir_node_attached_qual_type(function));
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(1, function_shape.parameter_count);
  ASSERT_TRUE(function_shape.is_variadic_function);

  // プロトタイプ宣言では名前なし仮引数を許容
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int proto(int); int main() { return 0; }"));
  psx_qual_type_t prototype = ps_ctx_get_function_qual_type_in(
      test_semantic_context(test_suite_session), (char *)"proto", 5);
  ASSERT_TRUE(prototype.type_id != PSX_TYPE_ID_INVALID);
  function_shape = test_qual_type_shape(test_suite_session, prototype);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(1, function_shape.parameter_count);
  hir = ag_compilation_session_hir_module(test_suite_session);
  function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, 0));
  ASSERT_EQ(1, psx_hir_node_child_count(function));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "typedef void VoidAlias; int no_args(VoidAlias); "
      "int main(void) { return 0; }"));
  prototype = ps_ctx_get_function_qual_type_in(
      test_semantic_context(test_suite_session), (char *)"no_args", 7);
  function_shape = test_qual_type_shape(test_suite_session, prototype);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(0, function_shape.parameter_count);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "typedef int (*F0)(); typedef int (*F1)(F0); "
      "int nested(int (int()), F0); "
      "int nested(F1 fp, F0 arg) { return fp(arg); }"));
  prototype = ps_ctx_get_function_qual_type_in(
      test_semantic_context(test_suite_session), (char *)"nested", 6);
  function_shape = test_qual_type_shape(test_suite_session, prototype);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(2, function_shape.parameter_count);

  expect_parse_fail_with_message(test_suite_session,
      "typedef union __attribute__((packed)) AttrUnion { int value; } "
      "AttrUnion; int attr_fn(void) { AttrUnion value; return 0; }",
      "E3096");
}

static void test_stmt_return(
    ag_compilation_session_t *test_suite_session) {
  printf("test_stmt_return...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session, "int main() { return 42; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *ret =
      find_test_hir_node_kind(hir, PSX_HIR_RETURN, 0);
  ASSERT_TRUE(ret != NULL);
  ASSERT_EQ(1, psx_hir_node_child_count(ret));
  const psx_hir_node_t *value = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(ret, 0));
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(value));
  ASSERT_EQ(42, psx_hir_node_integer_value(value));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session, "void noop() { return; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  ret = find_test_hir_node_kind(hir, PSX_HIR_RETURN, 0);
  ASSERT_TRUE(ret != NULL);
  ASSERT_EQ(0, psx_hir_node_child_count(ret));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "_Bool flag(void) { return 200; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  ret = find_test_hir_node_kind(hir, PSX_HIR_RETURN, 0);
  value = psx_hir_module_lookup(hir, psx_hir_node_child_at(ret, 0));
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(value));
  ASSERT_EQ(200, psx_hir_node_integer_value(value));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "char narrow(int x) { return x; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  ret = find_test_hir_node_kind(hir, PSX_HIR_RETURN, 0);
  value = psx_hir_module_lookup(hir, psx_hir_node_child_at(ret, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(value));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, value).kind);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int cast_unsigned_local(void) { unsigned u; return (int)u; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *cast =
      find_test_hir_node_kind(hir, PSX_HIR_CAST, 0);
  ASSERT_TRUE(cast != NULL);
  psx_type_shape_t cast_shape = test_hir_type_shape(test_suite_session, cast);
  ASSERT_EQ(PSX_TYPE_INTEGER, cast_shape.kind);
  ASSERT_TRUE(!cast_shape.is_unsigned);
  const psx_hir_node_t *cast_operand = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(cast, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(cast_operand));
  ASSERT_TRUE(test_hir_type_shape(test_suite_session, cast_operand).is_unsigned);

  const struct {
    const char *source;
    int expected_size;
    psx_integer_kind_t expected_integer_kind;
  } pointer_cast_cases[] = {
      {"int cast_pointer_int(int *p) { return (int)p; }",
       4, PSX_INTEGER_KIND_INT},
      {"long cast_pointer_long(int *p) { return (long)p; }",
       8, PSX_INTEGER_KIND_LONG},
  };
  for (size_t i = 0;
       i < sizeof(pointer_cast_cases) / sizeof(pointer_cast_cases[0]);
       i++) {
    reset_test_translation_unit_state(test_suite_session);
    ASSERT_TRUE(resolve_program_input_hir(test_suite_session, pointer_cast_cases[i].source));
    hir = ag_compilation_session_hir_module(test_suite_session);
    cast = find_test_hir_node_kind(hir, PSX_HIR_CAST, 0);
    ASSERT_TRUE(cast != NULL);
    cast_shape = test_hir_type_shape(test_suite_session, cast);
    ASSERT_EQ(PSX_TYPE_INTEGER, cast_shape.kind);
    ASSERT_EQ(pointer_cast_cases[i].expected_integer_kind,
              cast_shape.integer_kind);
    ASSERT_EQ(pointer_cast_cases[i].expected_size,
              test_type_size_id(test_suite_session, psx_hir_node_qual_type(cast).type_id));
    cast_operand = psx_hir_module_lookup(
        hir, psx_hir_node_child_at(cast, 0));
    ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(cast_operand));
    ASSERT_EQ(PSX_TYPE_POINTER,
              test_hir_type_shape(test_suite_session, cast_operand).kind);
  }

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int deref_intptr_cast(long addr) { return *(int *)addr; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *deref =
      find_test_hir_node_kind(hir, PSX_HIR_DEREF, 0);
  ASSERT_TRUE(deref != NULL);
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, deref).kind);
  cast = psx_hir_module_lookup(hir, psx_hir_node_child_at(deref, 0));
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(cast));
  ASSERT_EQ(PSX_TYPE_POINTER, test_hir_type_shape(test_suite_session, cast).kind);
  ASSERT_EQ(4, test_type_size_id(test_suite_session,
      test_qual_type_base(test_suite_session, psx_hir_node_qual_type(cast)).type_id));
  cast_operand = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(cast, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(cast_operand));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, cast_operand).kind);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "double void_cast_keeps_operand_fp(double d) { "
      "(void)d; return d; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  cast = find_test_hir_node_kind(hir, PSX_HIR_CAST, 0);
  ASSERT_TRUE(cast != NULL);
  ASSERT_EQ(PSX_TYPE_VOID, test_hir_type_shape(test_suite_session, cast).kind);
  cast_operand = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(cast, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(cast_operand));
  ASSERT_EQ(PSX_TYPE_FLOAT, test_hir_type_shape(test_suite_session, cast_operand).kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            test_hir_type_shape(test_suite_session, cast_operand).floating_kind);
  ret = find_test_hir_node_kind(hir, PSX_HIR_RETURN, 0);
  value = psx_hir_module_lookup(hir, psx_hir_node_child_at(ret, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(value));
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            test_hir_type_shape(test_suite_session, value).floating_kind);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "unsigned char unarrow(int x) { return x; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  ret = find_test_hir_node_kind(hir, PSX_HIR_RETURN, 0);
  value = psx_hir_module_lookup(hir, psx_hir_node_child_at(ret, 0));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(value));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct __ret_meta_s { int a; int b; } __ret_meta_struct(void) { "
      "struct __ret_meta_s r; return r; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, 0));
  psx_qual_type_t return_type = test_qual_type_base(test_suite_session,
      psx_hir_node_attached_qual_type(function));
  ASSERT_EQ(PSX_TYPE_STRUCT, test_qual_type_shape(test_suite_session, return_type).kind);
  ASSERT_EQ(8, test_type_size_id(test_suite_session, return_type.type_id));
  ret = find_test_hir_node_kind(hir, PSX_HIR_RETURN, 0);
  value = psx_hir_module_lookup(hir, psx_hir_node_child_at(ret, 0));
  ASSERT_EQ(return_type.type_id,
            psx_hir_node_qual_type(value).type_id);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "_Complex double __ret_meta_complex(void) { return 1; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, 0));
  return_type = test_qual_type_base(test_suite_session,
      psx_hir_node_attached_qual_type(function));
  psx_type_shape_t return_shape = test_qual_type_shape(test_suite_session, return_type);
  ASSERT_EQ(PSX_TYPE_COMPLEX, return_shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE, return_shape.floating_kind);
}

static void test_expr_deref_address_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_deref_address_typed_hir_boundary...\n");
  reset_test_locals(test_suite_session);
  lvar_t *integer = register_test_storage_fixture(test_suite_session,
      (char *)"a", 1, 4, 4, 0);
  set_test_storage_fixture_type(test_suite_session,
      integer, ps_ctx_intern_integer_qual_type_in(
                   test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0));
  node_t *address_syntax =
      parse_expr_input_with_existing_locals(test_suite_session, "&a");
  ASSERT_EQ(ND_ADDRESS_OF, address_syntax->kind);
  ASSERT_EQ(ND_IDENTIFIER, address_syntax->lhs->kind);
  node_t *address_operand_syntax = address_syntax->lhs;
  psx_frontend_expression_hir_t address_expression =
      resolve_test_expression_hir(test_suite_session, address_syntax);
  const psx_hir_node_t *address_hir =
      test_expression_hir_root(&address_expression);
  ASSERT_EQ(PSX_HIR_ADDRESS, psx_hir_node_kind(address_hir));
  ASSERT_EQ(PSX_TYPE_POINTER, test_hir_type_shape(test_suite_session, address_hir).kind);
  ASSERT_EQ(PSX_HIR_LOCAL,
            psx_hir_node_kind(psx_hir_module_lookup(
                address_expression.module,
                psx_hir_node_child_at(address_hir, 0))));
  ASSERT_TRUE(address_syntax->lhs == address_operand_syntax);
  psx_frontend_expression_hir_dispose(&address_expression);

  reset_test_locals(test_suite_session);
  lvar_t *pointer = register_test_storage_fixture(test_suite_session,
      (char *)"a", 1, 8, 4, 0);
  set_test_storage_fixture_type(test_suite_session,
      pointer,
      ps_ctx_intern_pointer_to_qual_type_in(
          test_semantic_context(test_suite_session),
          ps_ctx_intern_integer_qual_type_in(
              test_semantic_context(test_suite_session), PSX_INTEGER_KIND_INT, 0, 0)));
  node_t *deref = parse_expr_input_with_existing_locals(test_suite_session, "*a");
  ASSERT_EQ(ND_UNARY_DEREF, deref->kind);
  ASSERT_EQ(ND_IDENTIFIER, deref->lhs->kind);
  node_t *deref_operand_syntax = deref->lhs;
  psx_frontend_expression_hir_t deref_expression =
      resolve_test_expression_hir(test_suite_session, deref);
  const psx_hir_node_t *deref_hir =
      test_expression_hir_root(&deref_expression);
  ASSERT_EQ(PSX_HIR_DEREF, psx_hir_node_kind(deref_hir));
  ASSERT_EQ(PSX_TYPE_INTEGER, test_hir_type_shape(test_suite_session, deref_hir).kind);
  ASSERT_EQ(PSX_HIR_LOCAL,
            psx_hir_node_kind(psx_hir_module_lookup(
                deref_expression.module,
                psx_hir_node_child_at(deref_hir, 0))));
  ASSERT_EQ(ND_UNARY_DEREF, deref->kind);
  ASSERT_TRUE(deref->lhs == deref_operand_syntax);
  psx_frontend_expression_hir_dispose(&deref_expression);
}

static const psx_hir_node_t *find_test_hir_node_kind(
    const psx_hir_module_t *hir, psx_hir_node_kind_t kind,
    size_t occurrence) {
  if (!hir) return NULL;
  size_t found = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (!node || psx_hir_node_kind(node) != kind) continue;
    if (found++ == occurrence) return node;
  }
  return NULL;
}

static void test_expr_member_access(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_member_access...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main() { struct S { int a; int b; }; struct S s; "
      "s.b = 3; return s.b; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *assignment =
      find_test_hir_node_kind(hir, PSX_HIR_ASSIGN, 0);
  ASSERT_TRUE(assignment != NULL);
  const psx_hir_node_t *assigned_member = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(assignment, 0));
  ASSERT_EQ(PSX_HIR_MEMBER_ACCESS,
            psx_hir_node_kind(assigned_member));
  ASSERT_EQ(4, psx_hir_node_member_offset(assigned_member));
  ASSERT_EQ(4, test_type_size_id(test_suite_session,
      psx_hir_node_qual_type(assigned_member).type_id));
  ASSERT_EQ(PSX_HIR_LOCAL,
            psx_hir_node_kind(psx_hir_module_lookup(
                hir, psx_hir_node_child_at(assigned_member, 0))));
  ASSERT_TRUE(find_test_hir_node_kind(
      hir, PSX_HIR_RETURN, 0) != NULL);
  ASSERT_TRUE(find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 1) != NULL);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int f(void) { static struct { int n; int m; } s = {3, 4}; "
      "s.n += 1; return s.n + s.m; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *anonymous_member = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(anonymous_member != NULL);
  const psx_hir_node_t *anonymous_base = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(anonymous_member, 0));
  psx_type_shape_t anonymous_shape = test_qual_type_shape(test_suite_session,
      psx_hir_node_qual_type(anonymous_base));
  ASSERT_EQ(PSX_TYPE_STRUCT, anonymous_shape.kind);
  const psx_record_decl_t *anonymous_record =
      ps_ctx_get_record_decl_in(
          test_semantic_context(test_suite_session), anonymous_shape.record_id);
  ASSERT_TRUE(anonymous_record != NULL);
  ASSERT_TRUE(anonymous_record->member_count == 2);
  ASSERT_TRUE(anonymous_record->members[0].name != NULL);
  ASSERT_TRUE(anonymous_record->members[0].len == 1);
  ASSERT_TRUE(memcmp(anonymous_record->members[0].name, "n", 1) == 0);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "typedef unsigned char u8; "
      "int main() { struct S { u8 a; }; struct S s; return s.a; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *unsigned_member = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(unsigned_member != NULL);
  psx_type_shape_t unsigned_member_shape =
      test_hir_type_shape(test_suite_session, unsigned_member);
  ASSERT_EQ(PSX_TYPE_INTEGER, unsigned_member_shape.kind);
  ASSERT_EQ(PSX_INTEGER_KIND_CHAR,
            unsigned_member_shape.integer_kind);
  ASSERT_TRUE(unsigned_member_shape.is_unsigned);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "typedef unsigned char u8; "
      "int main() { struct S { u8 a; }; struct S s; "
      "return (int)s.a; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  unsigned_member = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(unsigned_member != NULL);
  ASSERT_TRUE(test_hir_type_shape(test_suite_session, unsigned_member).is_unsigned);
  ASSERT_TRUE(find_test_hir_node_kind(hir, PSX_HIR_CAST, 0) != NULL);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "typedef unsigned char u8; "
      "int main() { struct S { u8 a; }; struct S s; "
      "return (signed)s.a; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  unsigned_member = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(unsigned_member != NULL);
  ASSERT_TRUE(test_hir_type_shape(test_suite_session, unsigned_member).is_unsigned);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main() { struct S { int a; int b; }; struct S s; "
      "struct S *p; p=&s; p->b=5; return p->b; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  int pointer_member_count = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (node && psx_hir_node_kind(node) == PSX_HIR_MEMBER_ACCESS &&
        psx_hir_node_member_from_pointer(node)) {
      ASSERT_EQ(4, psx_hir_node_member_offset(node));
      pointer_member_count++;
    }
  }
  ASSERT_EQ(2, pointer_member_count);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main() { struct S { int a[2]; }; "
      "struct S s={{1,2}}; return s.a[0]; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *array_member = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(array_member != NULL);
  psx_type_shape_t array_member_shape =
      test_hir_type_shape(test_suite_session, array_member);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_member_shape.kind);
  ASSERT_EQ(2, array_member_shape.array_len);
  ASSERT_TRUE(find_test_hir_node_kind(
      hir, PSX_HIR_SUBSCRIPT, 0) != NULL);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int first(char *p) { return p[0]; } "
      "int main() { struct C { char x[1]; }; "
      "struct C c = {\"Z\"}; return first(c.x); }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  ASSERT_TRUE(find_test_hir_node_kind(hir, PSX_HIR_CALL, 0) != NULL);
  array_member = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(array_member != NULL);
  array_member_shape = test_hir_type_shape(test_suite_session, array_member);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_member_shape.kind);
  ASSERT_EQ(1, array_member_shape.array_len);
  ASSERT_EQ(1, test_type_size_id(test_suite_session,
      psx_hir_node_qual_type(array_member).type_id));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { struct B { signed int x:3; }; "
      "struct B b; return b.x; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *bitfield = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(bitfield != NULL);
  int bit_width = 0;
  int bit_offset = 0;
  int bit_is_signed = 0;
  ASSERT_TRUE(psx_hir_node_bitfield_info(
      bitfield, &bit_width, &bit_offset, &bit_is_signed));
  ASSERT_EQ(3, bit_width);
  ASSERT_EQ(1, bit_is_signed);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "enum CanonicalBitfieldEnum { CanonicalBitfieldValue }; "
      "typedef enum CanonicalBitfieldEnum CanonicalBitfieldEnumType; "
      "int main(void) { struct B { CanonicalBitfieldEnumType x:3; }; "
      "struct B b; return b.x; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  bitfield = find_test_hir_node_kind(
      hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(bitfield != NULL);
  ASSERT_TRUE(psx_hir_node_bitfield_info(
      bitfield, &bit_width, &bit_offset, &bit_is_signed));
  ASSERT_EQ(3, bit_width);
  ASSERT_EQ(0, bit_is_signed);
}

static void test_expr_string(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_string...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "\"hello\"", &syntax);
  ASSERT_EQ(ND_STRING, syntax->kind);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_STRING, psx_hir_node_kind(root));
  psx_qual_type_t type = psx_hir_node_qual_type(root);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, type).kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_qual_type_shape(test_suite_session, test_qual_type_base(test_suite_session, type)).kind);
  ASSERT_EQ(PSX_INTEGER_KIND_CHAR,
            test_qual_type_shape(test_suite_session, test_qual_type_base(test_suite_session, type)).integer_kind);
  size_t literal_length = 0;
  const char *literal =
      psx_hir_node_literal_contents(root, &literal_length);
  ASSERT_EQ(5, literal_length);
  ASSERT_TRUE(literal != NULL);
  ASSERT_TRUE(strncmp(literal, "hello", 5) == 0);
  size_t label_length = 0;
  ASSERT_TRUE(psx_hir_node_name(root, &label_length) != NULL);
  ASSERT_TRUE(label_length > 0);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_expr_concat_string(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_concat_string...\n");
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session, "\"he\" \"llo\"", &syntax);
  ASSERT_EQ(ND_STRING, syntax->kind);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_STRING, psx_hir_node_kind(root));
  size_t literal_length = 0;
  const char *literal =
      psx_hir_node_literal_contents(root, &literal_length);
  ASSERT_EQ(5, literal_length);
  ASSERT_TRUE(literal != NULL);
  ASSERT_TRUE(strncmp(literal, "hello", 5) == 0);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_type_decl(
    ag_compilation_session_t *test_suite_session) {
  printf("test_type_decl...\n");
  const char *declaration_cases[] = {
      "int main() { int x = 5; }",
      "int main() { int x; }",
      "int main() { int a, b=1; }",
      "int main() { int a=1, b=2; }",
      "int main() { struct S; union U; enum E; return 0; }",
      "int main() { struct S; struct S *p; p=0; return p==0; }",
      "int main() { struct S { int x; }; return 0; }",
      "int main() { struct S { int x; } *p; p=0; return p==0; }",
      "int main() { union U { int x; char y; }; "
      "enum E { A=1, B=2 }; return 0; }",
      "int main() { enum E { A=1, B, C=10 }; return A+B+C; }",
      "int main() { enum E { A=1, B=A+2, C=(B*2)-1 }; return C; }",
      "int main() { enum E { A=1, B=~A, C=(A<<3)|2, "
      "D=(C&10)^1 }; return D; }",
      "int main() { enum E { A=1, B=(A<2), "
      "C=(A==1)&&(B||0), D=C?7:9 }; return D; }",
      "int main() { unsigned u=3; _Bool b=1; signed s=2; "
      "return u+b+s; }",
      "typedef int myint; int main() { myint x=3; return x; }",
      "typedef int *intptr; int main() { int a=7; intptr p=&a; "
      "return *p; }",
      "typedef int (*fp_t)(int); int f(int x){ return x+1; } "
      "int main() { fp_t p; return 0; }",
      "typedef int (((*fp_t)))(int); int f(int x){ return x+1; } "
      "int main() { fp_t p; return 0; }",
      "extern int a[]; int main(){ return 0; }",
      "int main() { unsigned long long v=13; signed char c=7; "
      "return v+c; }",
      "typedef unsigned long long ull; int main() { ull v=5; return v; }",
      "int main() { static int x=3; register int r=2; auto int a=1; "
      "int *restrict p=0; return a+r+x+(p==0); }",
      "int main() { const const int x=3; volatile volatile int y=4; "
      "int *restrict restrict p=0; return x+y+(p==0); }",
      "int sumq(const const int a, volatile volatile int b, "
      "int *restrict restrict p){ return a+b+(p==0); } "
      "int main(){ return sumq(3,4,0); }",
      "int main() { _Alignas(16) int x=3; _Atomic int y=4; "
      "return x+y; }",
      "int main() { _Atomic(int) z=5; return z; }",
      "int main() { _Atomic(int*) p=0; _Atomic int q=1; "
      "return q+(p==0); }",
      "int main() { _Complex double cx=1.0; _Imaginary float iy=2.0f; "
      "return (cx!=0)+(iy!=0); }",
      "int main() { _Complex double a=1.0; _Complex double b=2.0; "
      "_Complex double c=a+b; return c!=0; }",
      "int main() { int a[1+2]; a[0]=3; return a[0]; }",
      "int main() { int a[2][3]; return 0; }",
      "int main() { struct S { int x:3; int y; }; return 0; }",
      "int main() { struct S { struct { int x; }; int y; }; return 0; }",
      "int main() { struct S { union { int x; char c; }; int y; }; "
      "return 0; }",
      "int main() { _Static_assert(1, \"ok\"); int x=3; return x; }",
      "int main() { int x={3}; return x; }",
      "int main() { enum E { A=1 }; return (enum E)42; }",
      "int main() { const int cx=1; volatile int vx=2; return cx+vx; }",
      "int main() { int *const pc=0; int *volatile pv=0; "
      "return (pc==0)+(pv==0); }",
      "int main() { int *const *volatile pp=0; return pp==0; }",
      "int main() { int x=42; int *p=&x; int **pp=&p; return **pp; }",
      "int main() { int a[3]={1,2,3}; return a[2]; }",
      "int main() { int a[2]={1}; return a[0]+a[1]; }",
      "int main() { char s[4]=\"abc\"; return s[2]; }",
      "int main() { struct S { int x; int y; }; "
      "struct S s={1,2}; return 0; }",
      "int main() { struct S { int x; int y; }; struct S t={1,2}; "
      "struct S s=t; return 0; }",
      "int main() { struct S { int x; int y; }; struct S a={1,2}; "
      "struct S b={3,4}; struct S s=(0?a:b); return 0; }",
      "int main() { struct S { int x; int y; }; struct S t={1,2}; "
      "struct S s=(t.y=9,t); return 0; }",
      "int main() { struct S { int a[2]; int z; }; "
      "struct S t={{1,2},3}; struct S s=t; return 0; }",
      "int main() { struct I { int x; int y; }; "
      "struct S { struct I i; int z; }; "
      "struct S t={{1,2},3}; struct S s=t; return 0; }",
      "int main() { union U { int x; char y; }; union U u={7}; "
      "return 0; }",
      "int main() { union U { int x; char y; }; union U v={7}; "
      "union U u=v; return 0; }",
      "int main() { union U { int x; char y; }; union U v={7}; "
      "union U u=(v.x=9,v); return 0; }",
      "int main() { union U { int x; char y; }; union U u={.x=7}; "
      "return 0; }",
      "int main() { union U { int x; char y; }; "
      "union U u={.x=7,.y=2}; return 0; }",
      "int main() { struct S { int x; int y; }; "
      "struct S s={.y=2,.x=1}; return 0; }",
      "int main() { struct S { int x; }; "
      "struct S s=(struct S)(struct S){1}; return 0; }",
      "int main() { struct A { int x; }; struct B { int x; }; "
      "struct A a={1}; struct B b=(struct B)a; return 0; }",
      "int main() { union U { int x; char y; }; "
      "union U u=(union U)(union U){.x=7}; return 0; }",
      "int main() { union A { int x; }; union B { int x; }; "
      "union A a={.x=1}; union B b=(union B)a; return 0; }",
      "int main() { struct I { int x; int y; }; "
      "struct O { struct I i; int z; }; struct O o={{1,2},3}; "
      "return 0; }",
      "int main() { struct I { int x; int y; }; "
      "union U { struct I i; int raw; }; union U u={{4,5}}; "
      "return 0; }",
      "int main() { union U { int x; char y; }; "
      "union U u={.x=1,.y=2}; return u.x; }",
      "int main() { struct S { int a[2]; int z; }; "
      "struct S s={{1,2},3}; return 0; }",
      "int main() { struct S { int a[2]; int z; }; "
      "struct S s={1,2,3}; return 0; }",
      "int main() { int src[2]={5,6}; "
      "struct S { int a[2]; int z; }; struct S s={src,7}; return 0; }",
      "int main() { struct S { char a[4]; int z; }; "
      "struct S s={\"ab\",7}; return 0; }",
      "int main() { int a[4]={[2]=7,[0]=1}; return 0; }",
      "int main() { struct S { int a[2]; }; "
      "struct S s={.a[1]=3}; return 0; }",
      "int main() { struct S { int a[2]; }; "
      "struct S s={.a[0]=1,.a[1]=2}; return 0; }",
      "int main() { union U { int a[2]; int z; }; "
      "union U u={.a[1]=3}; return 0; }",
  };

  for (size_t i = 0;
       i < sizeof(declaration_cases) / sizeof(declaration_cases[0]);
       i++) {
    reset_test_translation_unit_state(test_suite_session);
    if (!resolve_program_input_hir(test_suite_session, declaration_cases[i])) {
      fprintf(stderr, "type declaration HIR case %zu failed: %s\n",
              i, declaration_cases[i]);
      exit(1);
    }
    const psx_hir_module_t *hir =
        ag_compilation_session_hir_module(test_suite_session);
    ASSERT_TRUE(psx_hir_module_root_count(hir) >= 1);
    const psx_hir_node_t *function = psx_hir_module_lookup(
        hir, psx_hir_module_root_at(hir, 0));
    ASSERT_TRUE(function != NULL);
    ASSERT_EQ(PSX_HIR_FUNCTION, psx_hir_node_kind(function));
  }

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { int x=5; int a, b=1; return x+b; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  int declaration_initializer_count = 0;
  int found_five = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    if (psx_hir_node_is_declaration_initializer(node))
      declaration_initializer_count++;
    if (node && psx_hir_node_kind(node) == PSX_HIR_NUMBER &&
        psx_hir_node_integer_value(node) == 5)
      found_five = 1;
  }
  ASSERT_EQ(2, declaration_initializer_count);
  ASSERT_TRUE(found_five);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "x", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "b", 0) != NULL);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { const int cx=1; volatile int vx=2; "
      "int *const *volatile pp=0; return cx+vx+(pp==0); }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *cx =
      find_test_named_hir_node(hir, PSX_HIR_LOCAL, "cx", 0);
  const psx_hir_node_t *vx =
      find_test_named_hir_node(hir, PSX_HIR_LOCAL, "vx", 0);
  const psx_hir_node_t *pp =
      find_test_named_hir_node(hir, PSX_HIR_LOCAL, "pp", 0);
  ASSERT_TRUE(cx != NULL);
  ASSERT_TRUE(vx != NULL);
  ASSERT_TRUE(pp != NULL);
  ASSERT_TRUE((psx_hir_node_qual_type(cx).qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);
  ASSERT_TRUE((psx_hir_node_qual_type(vx).qualifiers &
               PSX_TYPE_QUALIFIER_VOLATILE) != 0);
  psx_qual_type_t pp_type = psx_hir_node_qual_type(pp);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, pp_type).kind);
  ASSERT_TRUE((pp_type.qualifiers & PSX_TYPE_QUALIFIER_VOLATILE) != 0);
  psx_qual_type_t pp_base = test_qual_type_base(test_suite_session, pp_type);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, pp_base).kind);
  ASSERT_TRUE((pp_base.qualifiers & PSX_TYPE_QUALIFIER_CONST) != 0);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { int x=42; int *p=&x; int **pp=&p; "
      "return **pp; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *outer_deref =
      find_test_hir_node_kind(hir, PSX_HIR_DEREF, 0);
  const psx_hir_node_t *inner_deref =
      find_test_hir_node_kind(hir, PSX_HIR_DEREF, 1);
  ASSERT_TRUE(outer_deref != NULL);
  ASSERT_TRUE(inner_deref != NULL);
  psx_type_shape_t first_deref_shape = test_hir_type_shape(test_suite_session, outer_deref);
  psx_type_shape_t second_deref_shape = test_hir_type_shape(test_suite_session, inner_deref);
  ASSERT_TRUE(
      (first_deref_shape.kind == PSX_TYPE_POINTER &&
       second_deref_shape.kind == PSX_TYPE_INTEGER) ||
      (second_deref_shape.kind == PSX_TYPE_POINTER &&
       first_deref_shape.kind == PSX_TYPE_INTEGER));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main(void) { struct S { int x; int y; }; "
      "struct S s={.y=2,.x=1}; return s.y; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *member =
      find_test_hir_node_kind(hir, PSX_HIR_MEMBER_ACCESS, 0);
  ASSERT_TRUE(member != NULL);
  ASSERT_EQ(4, psx_hir_node_member_offset(member));
  const psx_hir_node_t *member_base = psx_hir_module_lookup(
      hir, psx_hir_node_child_at(member, 0));
  psx_type_shape_t record_shape = test_hir_type_shape(test_suite_session, member_base);
  ASSERT_EQ(PSX_TYPE_STRUCT, record_shape.kind);
  const psx_record_decl_t *record = ps_ctx_get_record_decl_in(
      test_semantic_context(test_suite_session), record_shape.record_id);
  ASSERT_TRUE(record != NULL);
  ASSERT_EQ(2, record->member_count);
  ASSERT_EQ(8, test_type_size_id(test_suite_session,
                   psx_hir_node_qual_type(member_base).type_id));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "extern int a[]; int main(void) { return 0; }"));
  const psx_scope_declaration_t *external =
      find_test_scope_declaration(
          test_scope_graph(test_suite_session), "a", PSX_DECL_GLOBAL_OBJECT, 0);
  ASSERT_TRUE(external != NULL);
  ASSERT_TRUE(external->payload != NULL);
  psx_qual_type_t external_type =
      ps_gvar_decl_qual_type(external->payload);
  psx_type_shape_t external_shape =
      test_qual_type_shape(test_suite_session, external_type);
  ASSERT_EQ(PSX_TYPE_ARRAY, external_shape.kind);
  ASSERT_EQ(0, external_shape.array_len);
}

static void test_bool_assignment_typed_hir_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_bool_assignment_typed_hir_boundary...\n");
  reset_test_locals(test_suite_session);
  lvar_t *target = register_test_storage_fixture(test_suite_session,
      (char *)"typed_bool_target", 17, 1, 1, 0);
  set_test_storage_fixture_type(test_suite_session,
      target, ps_ctx_intern_integer_qual_type_in(
                  test_semantic_context(test_suite_session), PSX_INTEGER_KIND_BOOL, 0, 0));
  node_t *syntax = parse_expr_input_with_existing_locals(test_suite_session,
      "typed_bool_target = 3");
  ASSERT_EQ(ND_ASSIGN, syntax->kind);
  ASSERT_TRUE(syntax->lhs != NULL);
  ASSERT_TRUE(syntax->rhs != NULL);
  ASSERT_EQ(ND_IDENTIFIER, syntax->lhs->kind);
  ASSERT_EQ(ND_NUM, syntax->rhs->kind);
  node_t *lhs_syntax = syntax->lhs;
  node_t *rhs_syntax = syntax->rhs;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_hir(test_suite_session, syntax);
  const psx_hir_node_t *assignment_hir =
      test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_ASSIGN,
            psx_hir_node_kind(assignment_hir));
  ASSERT_EQ(PSX_TYPE_BOOL,
            test_hir_type_shape(test_suite_session, assignment_hir).kind);
  ASSERT_EQ(2, psx_hir_node_child_count(assignment_hir));
  const psx_hir_node_t *lhs_hir = psx_hir_module_lookup(
      expression.module, psx_hir_node_child_at(assignment_hir, 0));
  const psx_hir_node_t *rhs_hir = psx_hir_module_lookup(
      expression.module, psx_hir_node_child_at(assignment_hir, 1));
  ASSERT_EQ(PSX_HIR_LOCAL, psx_hir_node_kind(lhs_hir));
  ASSERT_EQ(PSX_HIR_NUMBER, psx_hir_node_kind(rhs_hir));
  ASSERT_EQ(3, psx_hir_node_integer_value(rhs_hir));
  ASSERT_TRUE(syntax->lhs == lhs_syntax);
  ASSERT_TRUE(syntax->rhs == rhs_syntax);
  psx_frontend_expression_hir_dispose(&expression);
}

static void test_type_metadata_bridge(
    ag_compilation_session_t *test_suite_session) {
  printf("test_type_metadata_bridge...\n");

  psx_semantic_type_table_t *types =
      psx_semantic_type_table_create();
  ASSERT_TRUE(types != NULL);
  psx_qual_type_t int_type =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_INT, 0, 0);
  psx_qual_type_t unsigned_char_type =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_CHAR, 1, 0);
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, int_type));
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, unsigned_char_type));

  psx_qual_type_t qualified_int = int_type;
  qualified_int.qualifiers =
      PSX_TYPE_QUALIFIER_CONST | PSX_TYPE_QUALIFIER_ATOMIC;
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, qualified_int));
  ASSERT_TRUE(psx_semantic_type_table_unqualified_types_match(
      types, int_type, qualified_int));
  ASSERT_TRUE((qualified_int.qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);
  ASSERT_TRUE((qualified_int.qualifiers &
               PSX_TYPE_QUALIFIER_ATOMIC) != 0);
  ASSERT_TRUE((qualified_int.qualifiers &
               PSX_TYPE_QUALIFIER_VOLATILE) == 0);

  psx_qual_type_t deep_array = int_type;
  for (int i = 0; i < 10; i++) {
    deep_array = psx_semantic_type_table_intern_array_of(
        types, deep_array, 2, 0);
    ASSERT_TRUE(deep_array.type_id != PSX_TYPE_ID_INVALID);
  }
  ASSERT_EQ(1024, psx_semantic_type_table_array_flat_element_count(
                      types, deep_array.type_id));
  ASSERT_EQ(512,
            psx_semantic_type_table_array_subscript_stride_elements(
                types, deep_array.type_id, 0));
  ASSERT_EQ(2,
            psx_semantic_type_table_array_subscript_stride_elements(
                types, deep_array.type_id, 8));
  ASSERT_EQ(1,
            psx_semantic_type_table_array_subscript_stride_elements(
                types, deep_array.type_id, 9));
  ASSERT_EQ(0,
            psx_semantic_type_table_array_subscript_stride_elements(
                types, deep_array.type_id, 10));
  psx_qual_type_t deep_leaf =
      psx_semantic_type_table_array_leaf(
          types, deep_array.type_id);
  ASSERT_EQ(int_type.type_id, deep_leaf.type_id);
  psx_qual_type_t array_cursor = deep_array;
  for (int depth = 0; depth < 10; depth++) {
    psx_type_shape_t shape = {0};
    ASSERT_TRUE(psx_semantic_type_table_describe(
        types, array_cursor.type_id, &shape));
    ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
    ASSERT_EQ(2, shape.array_len);
    array_cursor = psx_semantic_type_table_base(
        types, array_cursor.type_id);
  }
  ASSERT_EQ(int_type.type_id, array_cursor.type_id);

  psx_qual_type_t incomplete_array =
      psx_semantic_type_table_intern_array_of(
          types, int_type, 0, 0);
  psx_qual_type_t vla_array =
      psx_semantic_type_table_intern_array_of(
          types, int_type, 0, 1);
  psx_type_shape_t incomplete_shape = {0};
  psx_type_shape_t vla_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, incomplete_array.type_id, &incomplete_shape));
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, vla_array.type_id, &vla_shape));
  ASSERT_EQ(PSX_TYPE_ARRAY, incomplete_shape.kind);
  ASSERT_EQ(0, incomplete_shape.array_len);
  ASSERT_TRUE(!incomplete_shape.is_vla);
  ASSERT_TRUE(vla_shape.is_vla);
  ASSERT_TRUE(!psx_semantic_type_table_contains_vla_array(
      types, incomplete_array.type_id));
  ASSERT_TRUE(psx_semantic_type_table_contains_vla_array(
      types, vla_array.type_id));

  psx_qual_type_t pointer_to_qualified_int =
      psx_semantic_type_table_intern_pointer_to(
          types, qualified_int);
  psx_qual_type_t pointer_base =
      psx_semantic_type_table_base(
          types, pointer_to_qualified_int.type_id);
  ASSERT_EQ(int_type.type_id, pointer_base.type_id);
  ASSERT_EQ(qualified_int.qualifiers, pointer_base.qualifiers);
  psx_type_shape_t pointer_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, pointer_to_qualified_int.type_id, &pointer_shape));
  ASSERT_EQ(PSX_TYPE_POINTER, pointer_shape.kind);
  ASSERT_EQ(pointer_base.type_id,
            psx_semantic_type_table_pointee_value(
                types, pointer_to_qualified_int.type_id).type_id);

  const int dynamic_parameter_count = 96;
  psx_qual_type_t *dynamic_parameters = calloc(
      (size_t)dynamic_parameter_count, sizeof(*dynamic_parameters));
  ASSERT_TRUE(dynamic_parameters != NULL);
  for (int i = 0; i < dynamic_parameter_count; i++)
    dynamic_parameters[i] =
        (i & 1) ? unsigned_char_type : int_type;
  psx_qual_type_t dynamic_function =
      psx_semantic_type_table_intern_function(
          types, int_type, dynamic_parameters,
          dynamic_parameter_count, 1, 1);
  ASSERT_TRUE(dynamic_function.type_id != PSX_TYPE_ID_INVALID);
  psx_type_shape_t function_shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, dynamic_function.type_id, &function_shape));
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(dynamic_parameter_count, function_shape.parameter_count);
  ASSERT_TRUE(function_shape.has_function_prototype);
  ASSERT_TRUE(function_shape.is_variadic_function);
  ASSERT_EQ(unsigned_char_type.type_id,
            psx_semantic_type_table_parameter(
                types, dynamic_function.type_id, 95).type_id);
  ASSERT_TRUE(psx_semantic_type_table_parameter(
                  types, dynamic_function.type_id,
                  dynamic_parameter_count).type_id ==
              PSX_TYPE_ID_INVALID);
  psx_qual_type_t dynamic_function_pointer =
      psx_semantic_type_table_intern_pointer_to(
          types, dynamic_function);
  ASSERT_EQ(dynamic_function.type_id,
            psx_semantic_type_table_callable_function(
                types, dynamic_function_pointer).type_id);
  free(dynamic_parameters);

  psx_qual_type_t no_parameter_function =
      psx_semantic_type_table_intern_function(
          types, int_type, NULL, 0, 1, 0);
  ASSERT_TRUE(psx_semantic_type_is_exact_int_void_function(
      types, no_parameter_function));
  ASSERT_TRUE(!psx_semantic_type_is_exact_int_void_function(
      types, dynamic_function));

  const psx_record_layout_table_t *session_record_layouts =
      ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session));
  const ag_data_layout_t *data_layout =
      ps_ctx_data_layout(test_semantic_context(test_suite_session));
  ASSERT_EQ(4, psx_type_layout_sizeof(
                   types, session_record_layouts,
                   int_type.type_id, data_layout));
  ASSERT_EQ(4, psx_type_layout_alignof(
                   types, session_record_layouts,
                   int_type.type_id, data_layout));
  ASSERT_EQ(ag_data_layout_pointer_size(data_layout),
            psx_type_layout_sizeof(
                types, session_record_layouts,
                pointer_to_qualified_int.type_id, data_layout));
  ASSERT_EQ(4096, psx_type_layout_sizeof(
                      types, session_record_layouts,
                      deep_array.type_id, data_layout));
  ASSERT_EQ(0, psx_type_layout_sizeof(
                   types, session_record_layouts,
                   incomplete_array.type_id, data_layout));

  char canonical_signature[256];
  int canonical_length = psx_format_canonical_type_signature(
      types, pointer_to_qualified_int, data_layout,
      canonical_signature, sizeof(canonical_signature));
  ASSERT_TRUE(canonical_length > 0);
  ASSERT_TRUE(strcmp(canonical_signature, "p<kAi32>") == 0);
  psx_semantic_type_table_destroy(types);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "struct MetaPair { int x; char y; }; "
      "union MetaValue { long long i; double d; }; "
      "int gmatrix[2][3]; const int gconst=7; "
      "struct MetaPair gpair={1,2}; "
      "int metadata_signature(int (*cb)(double), const int *p, ...); "
      "int main(void) { return gmatrix[0][0] + gconst + gpair.x; }"));

  psx_scope_graph_t *scope_graph = test_scope_graph(test_suite_session);
  const psx_scope_declaration_t *matrix_declaration =
      find_test_scope_declaration(
          scope_graph, "gmatrix", PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *const_declaration =
      find_test_scope_declaration(
          scope_graph, "gconst", PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *pair_declaration =
      find_test_scope_declaration(
          scope_graph, "gpair", PSX_DECL_GLOBAL_OBJECT, 0);
  ASSERT_TRUE(matrix_declaration != NULL);
  ASSERT_TRUE(const_declaration != NULL);
  ASSERT_TRUE(pair_declaration != NULL);
  psx_qual_type_t matrix_type =
      ps_gvar_decl_qual_type(matrix_declaration->payload);
  psx_type_shape_t matrix_shape =
      test_qual_type_shape(test_suite_session, matrix_type);
  ASSERT_EQ(PSX_TYPE_ARRAY, matrix_shape.kind);
  ASSERT_EQ(2, matrix_shape.array_len);
  psx_qual_type_t matrix_row =
      test_qual_type_base(test_suite_session, matrix_type);
  ASSERT_EQ(PSX_TYPE_ARRAY,
            test_qual_type_shape(test_suite_session, matrix_row).kind);
  ASSERT_EQ(3, test_qual_type_shape(test_suite_session, matrix_row).array_len);
  ASSERT_EQ(24, test_type_size_id(test_suite_session, matrix_type.type_id));
  psx_qual_type_t const_global_type =
      ps_gvar_decl_qual_type(const_declaration->payload);
  ASSERT_TRUE((const_global_type.qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);
  ASSERT_EQ(PSX_TYPE_STRUCT,
            test_qual_type_shape(test_suite_session,
                ps_gvar_decl_qual_type(
                    pair_declaration->payload)).kind);

  psx_record_id_t pair_record_id =
      ps_ctx_resolve_tag_record_id_in(
          test_semantic_context(test_suite_session), TK_STRUCT,
          (char *)"MetaPair", 8);
  psx_record_id_t value_record_id =
      ps_ctx_resolve_tag_record_id_in(
          test_semantic_context(test_suite_session), TK_UNION,
          (char *)"MetaValue", 9);
  ASSERT_TRUE(pair_record_id != PSX_RECORD_ID_INVALID);
  ASSERT_TRUE(value_record_id != PSX_RECORD_ID_INVALID);
  ASSERT_TRUE(pair_record_id != value_record_id);
  const psx_record_decl_t *pair_record =
      ps_ctx_get_record_decl_in(
          test_semantic_context(test_suite_session), pair_record_id);
  const psx_record_decl_t *value_record =
      ps_ctx_get_record_decl_in(
          test_semantic_context(test_suite_session), value_record_id);
  ASSERT_TRUE(pair_record != NULL);
  ASSERT_TRUE(value_record != NULL);
  ASSERT_TRUE(pair_record->is_complete);
  ASSERT_TRUE(value_record->is_complete);
  ASSERT_EQ(2, pair_record->member_count);
  ASSERT_EQ(2, value_record->member_count);
  const psx_record_layout_t *pair_layout =
      psx_record_layout_table_lookup(
          ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session)),
          pair_record_id, data_layout);
  const psx_record_layout_t *value_layout =
      psx_record_layout_table_lookup(
          ps_ctx_record_layout_table_in(test_semantic_context(test_suite_session)),
          value_record_id, data_layout);
  ASSERT_TRUE(pair_layout != NULL);
  ASSERT_TRUE(value_layout != NULL);
  ASSERT_EQ(8, pair_layout->size);
  ASSERT_EQ(4, pair_layout->alignment);
  ASSERT_EQ(0, psx_record_layout_member(
                   pair_layout, 0)->offset);
  ASSERT_EQ(4, psx_record_layout_member(
                   pair_layout, 1)->offset);
  ASSERT_EQ(8, value_layout->size);
  ASSERT_EQ(8, value_layout->alignment);

  psx_qual_type_t signature =
      ps_ctx_get_function_qual_type_in(
          test_semantic_context(test_suite_session),
          (char *)"metadata_signature", 18);
  psx_type_shape_t signature_shape =
      test_qual_type_shape(test_suite_session, signature);
  ASSERT_EQ(PSX_TYPE_FUNCTION, signature_shape.kind);
  ASSERT_EQ(2, signature_shape.parameter_count);
  ASSERT_TRUE(signature_shape.is_variadic_function);
  const psx_semantic_type_table_t *session_types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  psx_qual_type_t callback_parameter =
      psx_semantic_type_table_parameter(
          session_types, signature.type_id, 0);
  psx_qual_type_t pointer_parameter =
      psx_semantic_type_table_parameter(
          session_types, signature.type_id, 1);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, callback_parameter).kind);
  ASSERT_EQ(PSX_TYPE_FUNCTION,
            test_qual_type_shape(test_suite_session,
                test_qual_type_base(test_suite_session, callback_parameter)).kind);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, pointer_parameter).kind);
  ASSERT_TRUE((test_qual_type_base(test_suite_session, pointer_parameter).qualifiers &
               PSX_TYPE_QUALIFIER_CONST) != 0);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int metadata_vla(int n, int rows[][n][3]) { "
      "int matrix[2][3]={{1,2,3},{4,5,6}}; "
      "int *const *volatile pp=0; "
      "_Atomic int atomic_value=1; "
      "return rows[0][0][0] + matrix[1][2] + "
      "atomic_value + (pp==0); }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *function =
      find_test_named_hir_node(
          hir, PSX_HIR_FUNCTION, "metadata_vla", 0);
  ASSERT_TRUE(function != NULL);
  const psx_hir_node_t *n_parameter =
      find_test_hir_function_parameter(
          hir, function, "n");
  const psx_hir_node_t *rows_parameter =
      find_test_hir_function_parameter(
          hir, function, "rows");
  ASSERT_TRUE(n_parameter != NULL);
  ASSERT_TRUE(rows_parameter != NULL);
  ASSERT_EQ(2, psx_hir_node_vla_dimension_count(rows_parameter));
  ASSERT_EQ(psx_hir_node_storage_offset(n_parameter),
            psx_hir_node_vla_dimension_source_offset(
                rows_parameter, 0));
  ASSERT_EQ(3, psx_hir_node_vla_dimension_constant(
                   rows_parameter, 1));
  const psx_hir_node_t *pp =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "pp", 0);
  const psx_hir_node_t *atomic_value =
      find_test_named_hir_node(
          hir, PSX_HIR_LOCAL, "atomic_value", 0);
  ASSERT_TRUE(pp != NULL);
  ASSERT_TRUE(atomic_value != NULL);
  psx_qual_type_t pp_type = psx_hir_node_qual_type(pp);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, pp_type).kind);
  ASSERT_TRUE((pp_type.qualifiers &
               PSX_TYPE_QUALIFIER_VOLATILE) != 0);
  ASSERT_TRUE((psx_hir_node_qual_type(atomic_value).qualifiers &
               PSX_TYPE_QUALIFIER_ATOMIC) != 0);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_SUBSCRIPT, 0) != NULL);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_RETURN, 0) != NULL);
}

static void test_translation_unit_reset_static_local_state(
    ag_compilation_session_t *test_suite_session) {
  printf("test_translation_unit_reset_static_local_state...\n");

  const char *input = "int f(void) { static int x=1; return x; }";
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session, input));
  ASSERT_EQ(1, psx_hir_module_root_count(
                   ag_compilation_session_hir_module(
                       test_suite_session)));
  ASSERT_TRUE(find_test_global_var(test_suite_session, "f.x.0", 5) != NULL);
  ASSERT_TRUE(find_test_global_var(test_suite_session, "f.x.1", 5) == NULL);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session, input));
  ASSERT_EQ(1, psx_hir_module_root_count(
                   ag_compilation_session_hir_module(
                       test_suite_session)));
  ASSERT_TRUE(find_test_global_var(test_suite_session, "f.x.0", 5) != NULL);
  ASSERT_TRUE(find_test_global_var(test_suite_session, "f.x.1", 5) == NULL);
  reset_test_translation_unit_state(test_suite_session);
}

static void test_translation_unit_reset_anonymous_tag_state(
    ag_compilation_session_t *test_suite_session) {
  printf("test_translation_unit_reset_anonymous_tag_state...\n");

  const char *input = "struct { int x; } g; int main(void) { return 0; }";
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session, input));
  ASSERT_TRUE(test_semantic_has_tag_type(test_suite_session, TK_STRUCT, "__anon_tag_0", 12));
  ASSERT_TRUE(!test_semantic_has_tag_type(test_suite_session, TK_STRUCT, "__anon_tag_1", 12));

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session, input));
  ASSERT_TRUE(test_semantic_has_tag_type(test_suite_session, TK_STRUCT, "__anon_tag_0", 12));
  ASSERT_TRUE(!test_semantic_has_tag_type(test_suite_session, TK_STRUCT, "__anon_tag_1", 12));
  reset_test_translation_unit_state(test_suite_session);
}

static void test_translation_unit_reset_decl_locals_state(
    ag_compilation_session_t *test_suite_session) {
  printf("test_translation_unit_reset_decl_locals_state...\n");

  char name[] = "x";
  reset_test_locals(test_suite_session);
  ASSERT_TRUE(register_test_default_storage_fixture(test_suite_session, name, 1) != NULL);
  ASSERT_TRUE(find_test_local_var_in(test_local_registry(test_suite_session), name, 1) != NULL);
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(find_test_local_var_in(test_local_registry(test_suite_session), name, 1) == NULL);
}

static void test_translation_unit_reset_pragma_pack_state(
    ag_compilation_session_t *test_suite_session) {
  printf("test_translation_unit_reset_pragma_pack_state...\n");

  psx_parser_runtime_context_t *runtime_context =
      ag_compilation_session_parser_runtime_context(test_suite_session);
  pragma_pack_reset_in(runtime_context);
  pragma_pack_set_in(runtime_context, 8);
  pragma_pack_push_in(runtime_context, 1);
  ASSERT_EQ(1, pragma_pack_current_alignment_in(runtime_context));
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_EQ(0, pragma_pack_current_alignment_in(runtime_context));
  pragma_pack_pop_in(runtime_context);
  ASSERT_EQ(0, pragma_pack_current_alignment_in(runtime_context));
}

static const psx_hir_node_t *assert_test_hir_function_root(
    const psx_hir_module_t *hir, size_t index,
    const char *expected_name) {
  ASSERT_TRUE(hir != NULL);
  ASSERT_TRUE(index < psx_hir_module_root_count(hir));
  const psx_hir_node_t *function = psx_hir_module_lookup(
      hir, psx_hir_module_root_at(hir, index));
  ASSERT_TRUE(function != NULL);
  ASSERT_EQ(PSX_HIR_FUNCTION, psx_hir_node_kind(function));
  size_t name_length = 0;
  const char *name = psx_hir_node_name(function, &name_length);
  ASSERT_TRUE(name != NULL);
  ASSERT_EQ(strlen(expected_name), name_length);
  ASSERT_TRUE(strncmp(name, expected_name, name_length) == 0);
  return function;
}

static void test_multiple_funcdefs(
    ag_compilation_session_t *test_suite_session) {
  printf("test_multiple_funcdefs...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int foo(void) { 1; } int bar(void) { 2; }"));
  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(2, psx_hir_module_root_count(hir));
  assert_test_hir_function_root(hir, 0, "foo");
  assert_test_hir_function_root(hir, 1, "bar");

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int add(int a, int b); "
      "int add(int a, int b) { return a+b; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  assert_test_hir_function_root(hir, 0, "add");

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int log(const char *fmt, ...); int main() { return 0; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(1, psx_hir_module_root_count(hir));
  assert_test_hir_function_root(hir, 0, "main");

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int **sig_proto_pp(void); int main(void) { return 0; }"));
  psx_qual_type_t signature = ps_ctx_get_function_qual_type_in(
      test_semantic_context(test_suite_session), (char *)"sig_proto_pp", 12);
  psx_qual_type_t derived = test_qual_type_base(test_suite_session, signature);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, derived).kind);
  derived = test_qual_type_base(test_suite_session, derived);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, derived).kind);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_qual_type_shape(test_suite_session, test_qual_type_base(test_suite_session, derived)).kind);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int **sig_def_pp(void) { return 0; }"));
  signature = ps_ctx_get_function_qual_type_in(
      test_semantic_context(test_suite_session), (char *)"sig_def_pp", 10);
  derived = test_qual_type_base(test_suite_session, signature);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, derived).kind);
  derived = test_qual_type_base(test_suite_session, derived);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, derived).kind);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int (*(*(*sig_deep(void))(int))(double))[3] { return 0; }"));
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  signature = ps_ctx_get_function_qual_type_in(
      test_semantic_context(test_suite_session), (char *)"sig_deep", 8);
  psx_qual_type_t int_function_pointer =
      psx_semantic_type_table_base(types, signature.type_id);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, int_function_pointer).kind);
  psx_qual_type_t int_function =
      test_qual_type_base(test_suite_session, int_function_pointer);
  psx_type_shape_t function_shape = test_qual_type_shape(test_suite_session, int_function);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(1, function_shape.parameter_count);
  ASSERT_EQ(PSX_TYPE_INTEGER,
            test_qual_type_shape(test_suite_session, psx_semantic_type_table_parameter(
                types, int_function.type_id, 0)).kind);
  psx_qual_type_t double_function_pointer =
      test_qual_type_base(test_suite_session, int_function);
  ASSERT_EQ(PSX_TYPE_POINTER,
            test_qual_type_shape(test_suite_session, double_function_pointer).kind);
  psx_qual_type_t double_function =
      test_qual_type_base(test_suite_session, double_function_pointer);
  function_shape = test_qual_type_shape(test_suite_session, double_function);
  ASSERT_EQ(PSX_TYPE_FUNCTION, function_shape.kind);
  ASSERT_EQ(1, function_shape.parameter_count);
  psx_type_shape_t double_parameter = test_qual_type_shape(test_suite_session,
      psx_semantic_type_table_parameter(
          types, double_function.type_id, 0));
  ASSERT_EQ(PSX_TYPE_FLOAT, double_parameter.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_DOUBLE,
            double_parameter.floating_kind);
  psx_qual_type_t array_pointer = test_qual_type_base(test_suite_session, double_function);
  ASSERT_EQ(PSX_TYPE_POINTER, test_qual_type_shape(test_suite_session, array_pointer).kind);
  psx_qual_type_t array = test_qual_type_base(test_suite_session, array_pointer);
  psx_type_shape_t array_shape = test_qual_type_shape(test_suite_session, array);
  ASSERT_EQ(PSX_TYPE_ARRAY, array_shape.kind);
  ASSERT_EQ(3, array_shape.array_len);

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int variadic(...){ return 0; } "
      "int main() { return variadic(); }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  ASSERT_EQ(2, psx_hir_module_root_count(hir));
  const psx_hir_node_t *variadic =
      assert_test_hir_function_root(hir, 0, "variadic");
  ASSERT_TRUE(test_qual_type_shape(test_suite_session,
      psx_hir_node_attached_qual_type(variadic)).is_variadic_function);
  assert_test_hir_function_root(hir, 1, "main");

  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int f(int a[static 3], int b[restrict static 2]) { return 7; }"));
  hir = ag_compilation_session_hir_module(test_suite_session);
  const psx_hir_node_t *array_function =
      assert_test_hir_function_root(hir, 0, "f");
  function_shape = test_qual_type_shape(test_suite_session,
      psx_hir_node_attached_qual_type(array_function));
  ASSERT_EQ(2, function_shape.parameter_count);

  const struct {
    const char *source;
    const char *first_function;
    const char *second_function;
  } root_cases[] = {
      {"struct S { int x; }; int main() { return 0; }", "main", NULL},
      {"struct S { int x; } *gp; int main() { return 0; }", "main", NULL},
      {"int g=1; int main() { return 0; }", "main", NULL},
      {"extern int g; inline int add(int a, int b) { return a+b; } "
       "int main() { return add(3,4); }", "add", "main"},
      {"_Noreturn void die() { return; } int main() { return 0; }",
       "die", "main"},
      {"_Static_assert(1, \"ok\"); int main() { return 0; }",
       "main", NULL},
      {"struct SA { _Static_assert(sizeof(int)==4, \"ok\"); int x; }; "
       "int main(void) { return 0; }", "main", NULL},
  };
  for (size_t i = 0;
       i < sizeof(root_cases) / sizeof(root_cases[0]); i++) {
    reset_test_translation_unit_state(test_suite_session);
    ASSERT_TRUE(resolve_program_input_hir(test_suite_session, root_cases[i].source));
    hir = ag_compilation_session_hir_module(test_suite_session);
    size_t expected_roots = root_cases[i].second_function ? 2 : 1;
    ASSERT_EQ(expected_roots, psx_hir_module_root_count(hir));
    assert_test_hir_function_root(
        hir, 0, root_cases[i].first_function);
    if (root_cases[i].second_function)
      assert_test_hir_function_root(
          hir, 1, root_cases[i].second_function);
  }
}

static void test_parse_invalid(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parse_invalid...\n");
  expect_parse_fail(test_suite_session, "int main(void { return 0; }"); // ')' がない
  expect_parse_fail(test_suite_session, "int main() { return 0; ");     // '}' がない
  expect_parse_fail(test_suite_session, "int main() { return 0 }");     // ';' がない
  expect_parse_fail(test_suite_session, "int main() { if (1) return 1; else }"); // else ブロック不正
  expect_parse_fail(test_suite_session, "int main() { int ; return 0; }");       // 変数名なし
  expect_parse_fail(test_suite_session, "int main() { int a[; return 0; }");     // 配列サイズ不正
  expect_parse_fail(test_suite_session, "int main() { int a[1 return 0; }");     // ']' がない
  expect_parse_fail(test_suite_session, "int main() { return (1+2; }");          // ')' がない
  expect_parse_fail(test_suite_session, "int main() { if 1) return 0; }");       // '(' がない
  expect_parse_fail(test_suite_session, "int main() { for (i=0 i<3; i=i+1) return 0; }"); // ';' 不足
  expect_parse_fail(test_suite_session, "int main() { ++1; }");                  // lvalueでない
  expect_parse_fail(test_suite_session, "int main() { 1++; }");                  // lvalueでない
  expect_parse_fail(test_suite_session,
      "int main(void) { const int value=0; value++; return 0; }");
  expect_parse_fail(test_suite_session,
      "typedef int *P; int main(void) { int x=0; const P p=&x; "
      "p=&x; return 0; }");
  expect_parse_fail(test_suite_session,
      "struct S { const int c; }; int main(void) { "
      "struct S s={0}; s.c=1; return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { struct S { int x; } value; value++; return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { _Complex double value=0; ++value; return 0; }");
  expect_parse_ok(test_suite_session, "int main() { float f=1.0; ++f; }");       // C11: 浮動小数点の ++ は合法
  expect_parse_ok(test_suite_session, "int main() { double d=1.0; d--; }");      // C11: 浮動小数点の -- は合法
  expect_parse_fail(test_suite_session, "int main() { 1 += 2; }");               // lvalueでない
  expect_parse_fail(test_suite_session, "int main() { return; }");               // 非void関数で式なしreturn
  expect_parse_fail(test_suite_session, "void f() { return 1; }");           // void関数で値return
  expect_parse_fail(test_suite_session,
      "int f(void) { int *value = 0; return value; }");
  expect_parse_fail(test_suite_session, "int *f(void) { return 1; }");
  expect_parse_fail(test_suite_session,
      "int *f(const int *value) { return value; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { int value=1; (int[2])value; return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { struct S { int x; }; struct S value={1}; "
      "return (int)value; }");
  expect_parse_fail(test_suite_session, "int main(void) { return &1 != 0; }");
  expect_parse_ok(test_suite_session,
      "int main(void) { struct S { int x; }; struct S value={1}; "
      "(void)value; return 0; }");
  expect_parse_fail(test_suite_session, "int main() { return missing_value; }"); // 未定義変数
  expect_parse_fail(test_suite_session, "int main() { goto MISSING; return 0; }"); // 未定義ラベル
  expect_parse_fail(test_suite_session, "int main() { struct T x; return 0; }");   // 未定義タグ参照
  expect_parse_ok(test_suite_session,
      "int main(void) { struct Forward (*p); (void)p; return 0; }");
  expect_parse_ok(test_suite_session,
      "typedef struct Forward Forward; struct Holder { Forward *p; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "struct Holder { struct Forward values[2]; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "typedef int FunctionType(int); struct Holder { FunctionType f; }; "
      "int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "struct S; union S; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "struct E {}; struct E {}; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "struct S { int x; int x; }; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "struct S { int x; struct { int x; }; }; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session,
      "struct S { unsigned int x : 33; }; int main(void) { return 0; }");
  expect_parse_ok(test_suite_session,
      "struct S { struct { int x; }; struct { int y; }; }; "
      "int main(void) { struct S s = {{1}, {2}}; return s.x + s.y; }");
  expect_parse_ok(test_suite_session, "typedef int X; typedef int X; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "typedef int X; typedef long X; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "int X; typedef int X; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "int f(void); typedef int f; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "enum E { X }; typedef int X; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "int main(void) { int x; typedef int x; return 0; }");
  expect_parse_fail(test_suite_session, "int X; enum E { X }; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "int f(void); enum E { f }; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "typedef int T; enum E { T }; int main(void) { return 0; }");
  expect_parse_fail(test_suite_session, "int main(void) { int x; enum E { x }; return 0; }");
  expect_parse_ok(test_suite_session, "int X; int main(void) { enum E { X }; return 0; }");
  expect_parse_ok(test_suite_session, "typedef int T; int main(void) { enum E { T }; return 0; }");
  expect_parse_ok(test_suite_session, "int main() { { struct T { int x; }; } struct T *p; return 0; }"); // 外側スコープで新規前方宣言
  expect_parse_fail(test_suite_session, "int main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }"); // 非同種非スカラ型cast未対応
  expect_parse_fail(test_suite_session, "int main() { struct A { int x; }; struct B { int x; }; struct A a={1}; struct B b=a; return 0; }"); // 同サイズでも別タグは互換structではない
  expect_parse_fail(test_suite_session, "int main() { short double x; return 0; }");   // 不正な型指定子組み合わせ
  expect_parse_fail(test_suite_session, "int main() { _Complex int x; return 0; }");   // 浮動小数型以外との組み合わせは不正
  expect_parse_fail(test_suite_session, "int main() { _Imaginary int x; return 0; }"); // 浮動小数型以外との組み合わせは不正
  expect_parse_fail(test_suite_session, "int main() { return (_Thread_local int)1; }"); // cast型名のストレージ指定は未対応
  expect_parse_fail(test_suite_session, "int main() { int a[-1]; return 0; }");         // 配列サイズは負数不可
  expect_parse_fail(test_suite_session, "int main() { return sizeof(int[-1]); }");      // sizeof 型名も同じ制約
  expect_parse_fail(test_suite_session, "int main() { return _Generic(1, float:2); }"); // 一致なし + defaultなし
  expect_parse_fail(test_suite_session,
      "int main() { return _Generic(1, int:1, signed int:2, default:3); }");
  expect_parse_fail(test_suite_session,
      "int main() { return _Generic(1, int:1, default:2, default:3); }");
  expect_parse_fail(test_suite_session, "int bad(int a, ..., int b) { return 0; }"); // ... は末尾のみ
  expect_parse_fail(test_suite_session, "int bad(int) { return 0; }"); // 関数定義の仮引数には名前が必要
  expect_parse_fail(test_suite_session, "int main() { _Static_assert(0, \"ng\"); return 0; }"); // static_assert失敗
  expect_parse_fail(test_suite_session, "int main() { _Static_assert(x, \"ng\"); return 0; }"); // 非定数式
  expect_parse_fail(test_suite_session,
      "struct SA { _Static_assert(0, \"ng\"); int x; }; int main(void){return 0;}");
  expect_parse_fail(test_suite_session, "int main() { int x; x.y=1; }");            // 非構造体への .
  expect_parse_fail(test_suite_session, "int main() { int *p; p->y=1; }");          // 非構造体ポインタへの ->
  expect_parse_fail(test_suite_session,
      "int main() { struct S { int x; } s={0}; return s.y; }");
  expect_parse_fail(test_suite_session, "int main() { int x; int *p=&x; return *(void *)p; }"); // void* deref
  expect_parse_fail(test_suite_session, "int main(void) { int *p=0; return -p; }");
  expect_parse_fail(test_suite_session, "int main(void) { int *p=0; return __real__ p; }");
  expect_parse_fail(test_suite_session, "int main(void) { int value=0; return value(); }");
  expect_parse_fail(test_suite_session, "int main(void) { int (**pp)(void)=0; return pp(); }");
  expect_parse_fail(test_suite_session, "int main(void) { struct S { int bits:3; } s; return &s.bits != 0; }");
  expect_parse_fail(test_suite_session, "int main() { break; }");                // ループ/switch外
  expect_parse_fail(test_suite_session, "int main() { continue; }");             // ループ外
  expect_parse_fail(test_suite_session, "int main(void) { int x = ({ continue; 0; }); return x; }");
  expect_parse_fail(test_suite_session, "int main(void) { while (({ continue; 1; })) return 0; return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { struct S { int value; } s = {0}; if (s) return 1; return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { struct S { int value; } s = {0}; while (s) return 1; return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { struct S { int value; } s = {0}; do {} while (s); return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { struct S { int value; } s = {0}; for (; s;) return 1; return 0; }");
  expect_parse_fail(test_suite_session,
      "int main(void) { switch (1.0) { default: return 0; } }");
  expect_parse_fail(test_suite_session, "int main() { case 1: return 0; }");     // switch外のcase
  expect_parse_fail(test_suite_session, "int main() { default: return 0; }");    // switch外のdefault
  expect_parse_fail(test_suite_session, "int main() { switch (1) { case 1: 0; case 1: 0; } }"); // case 重複
  expect_parse_fail(test_suite_session, "int main() { switch (0) { case 1+2: 0; case 3: 0; } }"); // 定数式評価後のcase重複
  expect_parse_fail(test_suite_session, "int main() { switch (1) { default: 0; default: 1; } }"); // default 重複
  expect_parse_fail(test_suite_session, "enum E { A = 2147483648 }; int main(void){ return 0; }"); // enum定数はint幅
  expect_parse_fail(test_suite_session, "int main() { int x={1,2}; return x; }"); // スカラ波括弧初期化は単一要素のみ
  expect_parse_fail(test_suite_session, "int main() { int a[2]={1,2,3}; return 0; }"); // 配列初期化子過多
  expect_parse_fail(test_suite_session, "int main() { struct S { int x; }; struct S s=1; return 0; }"); // 構造体単一式初期化は未対応
  expect_parse_fail(test_suite_session, "int main() { struct S { int x; }; struct S t={1}; struct S s=(t,1); return 0; }"); // 最終値が同型オブジェクトでない
  expect_parse_fail(test_suite_session, "int main() { union U { int x; char y; }; union U u={1,2}; return 0; }"); // 共用体は1要素のみ
  expect_parse_fail(test_suite_session, "int main() { union U { int x; char y; }; union U u={.x=1,2}; return 0; }"); // designatedでも1要素のみ
  expect_parse_fail(test_suite_session, "int main() { int a[2]={[3]=1}; return 0; }"); // array designator 範囲外
  // C11 6.7.9p19: 同一 subobject への複数指定初期化子は後勝ちで受理される。
  expect_parse_ok(test_suite_session, "int main() { struct S { int x; int y; }; struct S s={.x=1,.x=2}; return 0; }"); // struct重複designator (後勝ち)
  expect_parse_ok(test_suite_session, "int main() { struct __BraceDup { int a[2]; int z; }; struct __BraceDup s={1,2,.a={3,4}}; return 0; }"); // brace elision後の上書き
  expect_parse_ok(test_suite_session, "int main() { int a[2]={[0]=1,[0]=2}; return 0; }"); // array重複designator (後勝ち)
}

static void test_parse_invalid_diagnostics(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parse_invalid_diagnostics...\n");
  expect_parse_fail_with_message(test_suite_session, "int main() { goto MISSING; return 0; }", "[goto] 未定義ラベル 'MISSING'");
  expect_parse_fail_with_message(test_suite_session, "int main() { L1: return 0; L1: return 1; }", "識別子が重複しています (ラベル): 'L1'");
  expect_parse_fail_with_message(test_suite_session, "int main() { struct T x; return 0; }", "不完全型のオブジェクトは宣言できません");
  expect_parse_fail_with_message(test_suite_session, "int main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }", "[cast] struct 値へのキャストは未対応です（型不整合）");
  expect_parse_fail_with_message(test_suite_session, "int main() { union U { int x; char y; }; struct S { int z; } s={1}; return (union U)s; }", "[cast] union 値へのキャストは未対応です（型不整合）");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int value=1; (int[2])value; return 0; }",
      "キャスト先の型は void またはスカラ型である必要があります");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { struct S { int x; }; struct S value={1}; "
      "return (int)value; }",
      "void 以外へのキャストのオペランドはスカラ型である必要があります");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { return &1 != 0; }",
      "& のオペランドは関数指示子またはオブジェクトを指す左辺値である必要があります");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { struct S { int bits:3; } value={1}; "
      "return &value.bits != 0; }",
      "ビットフィールドのアドレスは取得できません");
  expect_parse_fail_with_message(test_suite_session, "int main() { struct A { int x; }; struct B { int x; }; struct A a={1}; struct B b=a; return 0; }", "E3099");
  expect_parse_fail_with_message(test_suite_session, "int main() { struct S { int x; }; struct S s=1; return 0; }", "E3099");
  expect_parse_fail_with_message(test_suite_session, "int main() { struct S { int x; }; struct S t={1}; struct S s=(t,1); return 0; }", "E3099");
  expect_parse_fail_with_message(test_suite_session, "int main() { struct S { int x; }; int y=(0,(struct S){1}); return y; }", "E3099");
  expect_parse_fail_with_message(test_suite_session, "int main() { union U { int x; char y; }; union U u={1,2}; return 0; }", "E3036");
  expect_parse_fail_with_message(test_suite_session, "int main() { union U { int x; char y; }; union U u={.x=1,2}; return 0; }", "E3036");
  expect_parse_fail_with_message(test_suite_session, "int main() { _Complex int x; return 0; }", "_Complex/_Imaginary は浮動小数型にのみ指定できます");
  expect_parse_fail_with_message(test_suite_session, "int main() { return (_Complex int)1; }", "_Complex/_Imaginary cast は浮動小数型のみ対応です");
  expect_parse_fail_with_message(test_suite_session, "int main() { return (_Thread_local int)1; }", "[cast] cast 型名にストレージ指定子は使えません");
  expect_parse_fail_with_message(test_suite_session, "int main() { struct __IncOnly; struct __HasInc { struct __IncOnly m; }; return 0; }", "[decl] 不完全型のメンバは定義できません");
  expect_parse_fail_with_message(test_suite_session, "int main() { struct T { int f(int); }; return 0; }", "[decl] 関数型のメンバは定義できません");
  expect_parse_fail_with_message(test_suite_session,
      "int main() { struct S { int *p:1; }; return 0; }",
      "bit-field has non-integer canonical type");
  expect_parse_fail(test_suite_session, "int main() { struct S { int a[2]:1; }; return 0; }");
  expect_parse_fail(test_suite_session, "int main() { struct S { float f:1; }; return 0; }");
  expect_parse_fail_with_message(test_suite_session, "int bad(int) { return 0; }", "必要な項目がありません: 仮引数");
  expect_parse_fail_with_message(test_suite_session, "int main() { int x; int *p=&x; return *(void *)p; }",
                                 "void* の deref はできません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int value=1; return *value; }",
      "deref のオペランドはポインタ型でなければなりません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int value=1; return value[0]; }",
      "サブスクリプトの両辺ともポインタ/配列ではありません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int *value=0; return -value; }",
      "単項 - のオペランドは算術型でなければなりません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int *value=0; return __real__ value; }",
      "__real__ のオペランドは算術型でなければなりません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int *value=0; return __imag__ value; }",
      "__imag__ のオペランドは算術型でなければなりません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { return ++1; }",
      "++ の対象は左辺値である必要があります");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { const int value=0; return ++value; }",
      "const修飾された変数への代入はできません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { _Complex double value=0; ++value; return 0; }",
      "++ のオペランドは実数型またはポインタ型でなければなりません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { 1=2; return 0; }",
      "= の対象は左辺値である必要があります");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { const int value=0; value=1; return 0; }",
      "const修飾された変数への代入はできません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int target(void); target=target; return 0; }",
      "関数識別子に代入することはできません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { const int source=0; const int *from=&source; "
      "int *to=0; to=from; return 0; }",
      "const修飾されたポインタからconst無しポインタへの暗黙変換はできません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int value=0; int *pointer=0; "
      "value=pointer; return 0; }",
      "代入する型に互換性がありません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { int *pointer=0; pointer*=2; return 0; }",
      "代入する型に互換性がありません");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void) { struct S { int value; } condition={0}; "
      "return condition ? 1 : 2; }",
      "条件演算子の第1オペランドはスカラ型である必要があります");
  expect_parse_fail_with_message(test_suite_session,
      "int main(int condition) { return condition ? (void)0 : 1; }",
      "条件演算子の第2・第3オペランドの型に互換性がありません");
  expect_parse_fail_with_message(test_suite_session, "void f(void); int main(void){ int x; x=f(); return 0; }",
                                 "E3099");
  expect_parse_fail_with_message(test_suite_session, "void f(void); int main(void){ void (*fp)(void)=f; int x; x=fp(); return 0; }",
                                 "E3099");

  // 汎用cast未対応診断（"この型へのキャストは未対応です"）は現状到達しないことを固定する。
  expect_parse_fail_without_message(test_suite_session, "int main() { return (_Thread_local int)1; }", "[cast] この型へのキャストは未対応です");
  expect_parse_fail_without_message(test_suite_session, "int main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }", "[cast] この型へのキャストは未対応です");

  // Parser拡張設定: 同種同サイズの非スカラcast受理を無効化できること。
  test_compilation_options(test_suite_session)->enable_size_compatible_nonscalar_cast = false;
  expect_parse_fail_with_message(test_suite_session,
      "int main() { struct A { int x; }; struct B { int x; }; struct A a={7}; return ((struct B)a).x; }",
      "[cast] struct 値へのキャストは未対応です（型不整合）");
  expect_parse_fail_with_message(test_suite_session,
      "struct S { int x; }; int main() { struct S outer={1}; { struct S { int y; }; return ((struct S)outer).y; } }",
      "[cast] struct 値へのキャストは未対応です（型不整合）");
  test_compilation_options(test_suite_session)->enable_size_compatible_nonscalar_cast = true;

  // Parser拡張設定: struct への scalar/pointer cast 受理を無効化できること。
  test_compilation_options(test_suite_session)->enable_struct_scalar_pointer_cast = false;
  expect_parse_fail_with_message(test_suite_session,
      "int main() { struct S { int x; }; int a=0; return (struct S)a; }",
      "[cast] struct への scalar/pointer cast は設定で無効です");
  test_compilation_options(test_suite_session)->enable_struct_scalar_pointer_cast = true;

  // Parser拡張設定: union への scalar/pointer cast 受理を無効化できること。
  test_compilation_options(test_suite_session)->enable_union_scalar_pointer_cast = false;
  expect_parse_fail_with_message(test_suite_session,
      "int main() { union U { int x; char y; }; int a=0; return (union U)a; }",
      "[cast] union への scalar/pointer cast は設定で無効です");
  test_compilation_options(test_suite_session)->enable_union_scalar_pointer_cast = true;

  // Parser拡張設定: union 先頭配列メンバの非波括弧初期化受理を無効化できること。
  test_compilation_options(test_suite_session)->enable_union_array_member_nonbrace_init = false;
  expect_parse_fail_with_message(test_suite_session,
      "int main() { union U { int a[2]; int z; }; union U u={1,2}; return 0; }",
      "E3075");
  test_compilation_options(test_suite_session)->enable_union_array_member_nonbrace_init = true;

  // 入れ子 designator: 非配列メンバに .member[idx]=val は診断エラー
  expect_parse_fail_with_message(test_suite_session, "int main() { struct S { int x; }; struct S s={.x[0]=3}; return 0; }",
                                 "入れ子designatorの対象が配列メンバではありません");

  // decl.c の「1/2/4/8 byte スカラのみ」診断は、現行型セットでは到達不能であることを固定する。
  // 将来 16-byte などの新スカラ型導入時は、ここを陽性診断テストへ置き換える。
  expect_parse_fail_without_message(test_suite_session, "int main() { struct __IncOnly; struct __HasInc { struct __IncOnly m; }; return 0; }",
                                    "[decl] 構造体/共用体初期化は現在 1/2/4/8 byte スカラのみ対応です");
  expect_parse_fail_without_message(test_suite_session, "int main() { struct T { int f(int); }; return 0; }",
                                    "[decl] 構造体/共用体初期化は現在 1/2/4/8 byte スカラのみ対応です");
}

// 意地悪テスト: パーサーの境界ケース
static void test_parse_evil_edge_cases(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parse_evil_edge_cases...\n");

  // ネストした三項演算子: a?b?c:d:e は a?(b?c:d):e
  node_t *syntax = NULL;
  psx_frontend_expression_hir_t expression =
      resolve_test_expression_input_hir(test_suite_session,
          "1 ? 2 ? 3 : 4 : 5", &syntax);
  const psx_hir_node_t *root = test_expression_hir_root(&expression);
  ASSERT_EQ(ND_TERNARY, syntax->kind);
  ASSERT_EQ(PSX_HIR_TERNARY, psx_hir_node_kind(root));
  const psx_hir_node_t *inner = test_hir_child(&expression, root, 1);
  ASSERT_EQ(PSX_HIR_TERNARY, psx_hir_node_kind(inner));
  ASSERT_EQ(2, psx_hir_node_integer_value(
                   test_hir_child(&expression, inner, 0)));
  ASSERT_EQ(3, psx_hir_node_integer_value(
                   test_hir_child(&expression, inner, 1)));
  ASSERT_EQ(4, psx_hir_node_integer_value(
                   test_hir_child(&expression, inner, 2)));
  ASSERT_EQ(5, psx_hir_node_integer_value(
                   test_hir_child(&expression, root, 2)));
  psx_frontend_expression_hir_dispose(&expression);

  // 複雑な優先順位: 1+2*3==7&&1||0 → (((1+(2*3))==7)&&1)||0
  expression = resolve_test_expression_input_hir(test_suite_session,
      "1+2*3==7&&1||0", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_LOGOR, psx_hir_node_kind(root));
  const psx_hir_node_t *logical_and =
      test_hir_child(&expression, root, 0);
  const psx_hir_node_t *equality =
      test_hir_child(&expression, logical_and, 0);
  const psx_hir_node_t *addition =
      test_hir_child(&expression, equality, 0);
  ASSERT_EQ(PSX_HIR_LOGAND, psx_hir_node_kind(logical_and));
  ASSERT_EQ(PSX_HIR_EQ, psx_hir_node_kind(equality));
  ASSERT_EQ(PSX_HIR_ADD, psx_hir_node_kind(addition));
  ASSERT_EQ(PSX_HIR_MUL,
            psx_hir_node_kind(test_hir_child(
                &expression, addition, 1)));
  psx_frontend_expression_hir_dispose(&expression);

  // ビット演算と論理演算の優先順位: 1&2|3^4
  // → (1&2) | (3^4) → ND_BITOR( ND_BITAND(1,2), ND_BITXOR(3,4) )
  expression = resolve_test_expression_input_hir(test_suite_session, "1&2|3^4", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_BITOR, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_BITAND,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  ASSERT_EQ(PSX_HIR_BITXOR,
            psx_hir_node_kind(test_hir_child(&expression, root, 1)));
  psx_frontend_expression_hir_dispose(&expression);

  // x+++y の式解析: (x++) + y
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main() { int x=1; int y=2; return x+++y; }"));
  // パースが成功すればOK

  // キャストと単項マイナスのネスト: (int)-(char)5
  expression = resolve_test_cast_input_hir(test_suite_session, "(int)-(char)5", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(PSX_HIR_CAST, psx_hir_node_kind(root));
  const psx_hir_node_t *negation = test_hir_child(&expression, root, 0);
  ASSERT_EQ(PSX_HIR_NEGATE, psx_hir_node_kind(negation));
  ASSERT_EQ(PSX_HIR_CAST,
            psx_hir_node_kind(test_hir_child(
                &expression, negation, 0)));
  psx_frontend_expression_hir_dispose(&expression);

  // シフトと比較の優先順位: 1<<2<8 → (1<<2)<8
  expression = resolve_test_expression_input_hir(test_suite_session, "1<<2<8", &syntax);
  root = test_expression_hir_root(&expression);
  ASSERT_EQ(ND_LT, syntax->kind);
  ASSERT_EQ(TK_LT, syntax->source_op);
  ASSERT_EQ(PSX_HIR_LT, psx_hir_node_kind(root));
  ASSERT_EQ(PSX_HIR_SHL,
            psx_hir_node_kind(test_hir_child(&expression, root, 0)));
  ASSERT_EQ(8, psx_hir_node_integer_value(
                   test_hir_child(&expression, root, 1)));
  psx_frontend_expression_hir_dispose(&expression);

  // 比較演算子とオペランド順を Syntax AST にそのまま保持する。
  const struct {
    const char *input;
    psx_syntax_node_kind_t syntax_kind;
    token_kind_t source_operator;
    psx_hir_node_kind_t hir_kind;
  } comparisons[] = {
      {"1>2", ND_GT, TK_GT, PSX_HIR_GT},
      {"1>=2", ND_GE, TK_GE, PSX_HIR_GE},
  };
  for (size_t i = 0;
       i < sizeof(comparisons) / sizeof(comparisons[0]); i++) {
    expression = resolve_test_expression_input_hir(test_suite_session,
        comparisons[i].input, &syntax);
    root = test_expression_hir_root(&expression);
    ASSERT_EQ(comparisons[i].syntax_kind, syntax->kind);
    ASSERT_EQ(comparisons[i].source_operator, syntax->source_op);
    ASSERT_EQ(comparisons[i].hir_kind, psx_hir_node_kind(root));
    ASSERT_EQ(1, psx_hir_node_integer_value(
                     test_hir_child(&expression, root, 0)));
    ASSERT_EQ(2, psx_hir_node_integer_value(
                     test_hir_child(&expression, root, 1)));
    psx_frontend_expression_hir_dispose(&expression);
  }

  // カンマ演算子と代入の優先順位: a=1,b=2 → (a=1),(b=2)
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
      "int main() { int a; int b; a=1,b=2; }"));
  // パースが成功すればOK

  // 複雑な式文のパース
  expect_parse_ok(test_suite_session, "int main() { int x; x = 1 + 2 * 3 - 4 / 2 + (5 % 3); return x; }");
  expect_parse_ok_without_message(test_suite_session, "int main() { int x; *(&x) = 1; return x; }", "W3004");
  expect_parse_ok_without_message(test_suite_session,
      "typedef _Atomic int atomic_int; int main(void){ atomic_int x; ((void)(*(&x)=10)); return x; }",
      "W3004");
  expect_parse_ok_without_message(test_suite_session,
      "int main(void){ struct S { int a; int b; }; struct S s; s.a=2; s.b=5; return s.a+s.b; }",
      "W3004");
  expect_parse_ok_without_message(test_suite_session,
      "int main(void){ struct B { unsigned a:3; unsigned b:5; }; struct B s; s.a=5; s.b=10; return s.a; }",
      "W3004");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x; int *p=&x; return p==0; }", "W3004");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x; int *p=&x; return p==0; }", "W3003");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x; x=1; return 0; }", "W3003");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x; return &x==0; }", "W3003");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x; return &x==0; }", "W3004");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=1; return 0; }", "W3003");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x; x=x; return 0; }", "W3012");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=1.5; return 0; }", "W3010");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x; x=1.5; return 0; }", "W3010");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ return 1.5; }", "W3010");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=1; return x==x; }", "W3013");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=1; return x&&x; }", "W3020");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ unsigned int u=1; int s=-1; return s<u; }",
                               "W3018");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ unsigned int u=1; long s=-1; return s<u; }",
                                  "W3018");
  expect_parse_ok_without_message(test_suite_session,
      "int main(void){ unsigned char u=1; int s=-1; return s<u; }", "W3018");
  expect_parse_ok_with_message(test_suite_session,
      "int main(void){ unsigned long u=1; long s=-1; return s<u; }", "W3018");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ unsigned int u=1; return u>=0; }", "W3019");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ unsigned int u=1; return 0>u; }", "W3019");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ unsigned char u=1; return u<0; }", "W3019");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ unsigned short u=1; return 0<=u; }", "W3019");
  expect_parse_ok_with_message(test_suite_session,
      "unsigned char f(void){ return 1; } int main(void){ return f()<0; }", "W3019");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=1; return (unsigned char)x<0; }",
                               "W3019");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ signed char s=1; return s<0; }", "W3019");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int *p; return p==5; }", "W3022");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=1; return !x==0; }", "W3021");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=0; if (x=1) return x; return 0; }",
                               "W3007");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=0; while (x=1) return x; return 0; }",
                               "W3007");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=0; if (x,1) return x; return 0; }",
                               "W3008");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int x=0; if (x); return x; }", "W3009");
  expect_parse_ok_with_message(test_suite_session, "int *f(void){ int x=0; return &x; }", "W3006");
  expect_parse_ok_without_message(test_suite_session, "int *f(void){ static int x; return &x; }", "W3006");
  expect_parse_ok_without_message(test_suite_session, "int g; int *f(void){ return &g; }", "W3006");
  expect_parse_ok_with_message(test_suite_session,
      "int main(void){ int x=0; switch(x){ case 0: x=1; case 1: return x; } return 0; }",
      "W3017");
  expect_parse_ok_without_message(test_suite_session,
      "int main(void){ int x=0; switch(x){ case 0: x=1; break; case 1: return x; } return 0; }",
      "W3017");
  expect_parse_ok_without_message(test_suite_session,
      "int main(void){ int x=0; switch(x){ case 0: case 1: return 1; } return x; }",
      "W3017");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ char c=200; return c; }", "W3011");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ unsigned char c=300; return c; }", "W3011");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ unsigned char c=-1; return c; }", "W3011");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ _Bool b=300; return b; }", "W3011");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ return 2147483647 + 1; }", "W3023");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ return 2147483647 * 2; }", "W3023");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ return 2147483647L + 1L; }", "W3023");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int a[4]; int *p=a; return *(p + 2147483647); }",
                                  "W3023");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ return 1 << 32; }", "W3014");
  expect_parse_ok_with_message(test_suite_session, "long main(void){ return 1L << 64; }", "W3014");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ return 1 / 0; }", "W3015");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ return 1 % 0; }", "W3015");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ return f(); } int f(void){ return 1; }", "W3016");
  expect_parse_ok_without_message(test_suite_session, "int f(void); int main(void){ return f(); }", "W3016");
  expect_parse_fail_with_message(test_suite_session, "main(void){ return 0; }", "E3088");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ return 0; }", "W3001");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x=2.0; return 0; }", "W3010");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x; x=2.0; return 0; }", "W3010");
  expect_parse_ok_without_message(test_suite_session, "double main(void){ return 1.5; }", "W3010");
  expect_parse_ok_without_message(test_suite_session, "int f(int x){ return x; } int main(void){ return f(3); }", "W3004");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ int x=7; return x; }", "W3004");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ static int x; return x; }", "W3004");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void){ while(1){ ({ continue; 0; }); } return 0; }",
      "E3096");
  expect_parse_fail_with_message(test_suite_session,
      "int main(void){ int x=0; switch(x){ case 0: ({ break; 0; }); } return 0; }",
      "E3096");
  expect_parse_ok(test_suite_session, "int main(void){ int x=0; switch(x){ case (0 ? 1 : 2147483648): return 1; } return 0; }");
  expect_parse_ok(test_suite_session, "int main(void){ int x=0; switch(x){ case 1: switch(x){ case 1: return 1; } return 0; } return 0; }");
  expect_parse_ok_without_message(test_suite_session,
      "int main(void){ struct S { char c; int i; }; struct S s; long o=(char*)&s.i-(char*)&s; return o>0; }",
      "W3004");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ int *p; return &*p == 0; }", "W3004");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ goto L; int x; return x; L: return 0; }", "W3002");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ goto L; int x; return x; L: return 0; }", "W3004");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ goto L; int x; return x; L: return 0; }", "W3003");
  expect_parse_ok_with_message(test_suite_session, "int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3002");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3003");
  expect_parse_ok_without_message(test_suite_session, "int main(void){ goto L; { int x; x=1; return x; } L: return 0; }", "W3004");
  expect_parse_ok(test_suite_session, "int main() { int a; int b; int c; a = b = c = 42; return a; }");
  expect_parse_ok(test_suite_session, "int main() { return 1?2:3?4:5?6:7; }");
  expect_parse_ok(test_suite_session, "int main() { int x=1; return x<<1|x<<2|x<<3; }");
  expect_parse_ok(test_suite_session, "int main() { int x=1; return !!!!!x; }");
  expect_parse_ok(test_suite_session, "int main() { int x=255; return ~~~x; }");
  expect_parse_ok(test_suite_session, "struct S { int x; }; int f(struct S (*p)) { return p->x; } int main() { struct S s={3}; return f(&s); }");

  // sizeof内の型
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int); }");
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int*); }");
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int (*[3])(int)); }");
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int (*[2])[3]); }");
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int (*(*[2])[3])); }");
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int (*(*)(void))[3]); }");
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int (*(*[2])(void))[3]); }");
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int (*(*)(void))(int)); }");
  expect_parse_ok(test_suite_session, "int main() { return sizeof(int (*(*(*)(void))(int))[3]); }");
  expect_parse_ok(test_suite_session, "int main() { return _Generic((int (*)(int))0, int (*[3])(int): 1, default: 2); }");
  expect_parse_ok(test_suite_session, "int main() { return _Generic((int (*(*)(void))[3])0, int (*(*)(void))[3]: 1, default: 2); }");

  // 関数宣言のプロトタイプ
  expect_parse_ok(test_suite_session, "int f(int a, int b, int c); int main() { return f(1,2,3); }");
  expect_parse_ok(test_suite_session, "int (f)(int x) { return x; } int main() { return f(42); }");
  expect_parse_ok(test_suite_session, "int (*f(void))(int) { return 0; } int main() { return 0; }");
  expect_parse_ok(test_suite_session, "int (*f(int n))(int) { return 0; } int main() { return 0; }");
  expect_parse_ok(test_suite_session, "int (*(*f(void))(int))[3] { return 0; } int main() { return 0; }");
  expect_parse_ok(test_suite_session, "struct S { int x; } (f)(void){ struct S s; s.x=3; return s; } int main(){ return f().x; }");
  expect_parse_ok(test_suite_session, "union U { int x; } (f)(void){ union U u; return u; } int main(){ return 0; }");
  expect_parse_ok(test_suite_session, "int g=1; _Static_assert(sizeof(int)==4, \"ok\"); int main(){ return g; }");
  expect_parse_ok(test_suite_session, "typedef int myint; _Static_assert(1, \"ok\"); myint g=1; int main(){ return g; }");
  expect_parse_ok(test_suite_session, "typedef int myint; _Static_assert(sizeof(myint)==4, \"ok\"); int main(){ return 0; }");
  expect_parse_ok(test_suite_session, "typedef int A3[3]; _Static_assert(sizeof(A3)==12, \"ok\"); int main(){ return 0; }");

  // for文の複雑な初期化
  expect_parse_ok(test_suite_session, "int main() { int i; int s=0; for(i=0; i<10; i=i+1) s=s+i; return s; }");

  // do-while の後に式文
  expect_parse_ok(test_suite_session, "int main() { int x=0; do { x=x+1; } while(x<3); return x; }");

  // switch内のfall-through
  expect_parse_ok(test_suite_session, "int main() { int x=2; int r=0; switch(x) { case 1: r=10; case 2: r=r+20; case 3: r=r+30; default: r=r+1; } return r; }");

  // ネストしたブロック
  expect_parse_ok(test_suite_session, "int main() { { { { int x=42; return x; } } } }");

  // 意地悪テスト: 宣言・型の境界ケース

  // typedefで作った型名の使用
  expect_parse_ok(test_suite_session, "typedef int myint; myint add(myint a, myint b) { return a+b; } int main() { return add(20,22); }");
  expect_parse_ok(test_suite_session, "struct S { int x; } f(void){ struct S s; s.x=3; return s; } int main(){ return f().x; }");
  expect_parse_ok(test_suite_session, "union U { int x; } f(void){ union U u; return u; } int main(){ return 0; }");
  expect_parse_ok(test_suite_session, "typedef struct S S; struct S { int x; }; int main(){ S s; s.x=7; return s.x; }");
  expect_parse_ok(test_suite_session, "typedef struct { int x; } S; int main(){ S s; s.x=5; return s.x; }");
  expect_parse_ok(test_suite_session, "typedef union U U; union U { int x; }; int main(){ U u; u.x=8; return u.x; }");
  expect_parse_ok(test_suite_session, "typedef union { int x; } U; int main(){ U u; u.x=6; return u.x; }");
  expect_parse_ok(test_suite_session, "typedef int (*(*arr_t)[2])(int); int main() { arr_t p; return 0; }");
  expect_parse_ok(test_suite_session, "int main(){ typedef int (*(*fp_t))(int); return 0; }");
  expect_parse_ok(test_suite_session, "int main(){ typedef struct L L; return 0; }");
  expect_parse_ok(test_suite_session, "int main(){ typedef struct { int y; } L; L l; l.y=9; return l.y; }");
  expect_parse_ok(test_suite_session, "int main(){ typedef union L L; return 0; }");
  expect_parse_ok(test_suite_session, "int main(){ typedef union { int y; } L; L l; l.y=4; return l.y; }");
  expect_parse_ok(test_suite_session, "int main(){ typedef int A[]; A *p=0; return p==0; }");
  expect_parse_ok(test_suite_session, "int main(){ extern int (*fp)(int); return 0; }");
  expect_parse_ok(test_suite_session, "int main(){ extern int (*arr[2])(int); return 0; }");
  expect_parse_ok(test_suite_session, "int main(){ extern int a[]; return 0; }");
  expect_parse_ok(test_suite_session, "int (*arr[2])(int); int main(){ return 0; }");
  expect_parse_ok(test_suite_session, "int main(){ int (*arr[2])(int); return 0; }");

  // 複数の変数宣言（カンマ区切り）
  expect_parse_ok(test_suite_session, "int main() { int a=1, b=2, c=3; return a+b+c; }");

  // 関数ポインタ宣言
  expect_parse_ok(test_suite_session, "int add(int a, int b) { return a+b; } int main() { int (*f)(int,int) = add; return f(20,22); }");
  expect_parse_ok(test_suite_session, "int inc(int x){return x+1;} int apply(int (**pp)(int), int x){ return (*pp)(x); } int main(){ int (*p)(int)=inc; int (**pp)(int)=&p; return apply(pp,41); }");
  expect_parse_ok(test_suite_session, "int main(){ int (*(*pp))(int); return 0; }");
  expect_parse_ok(test_suite_session, "int main() { struct S { int (*fp)(int); }; return 0; }");
  expect_parse_ok(test_suite_session, "int main() { struct S { int (*arr[2])(int); }; return 0; }");

  // enumの値パース
  expect_parse_ok(test_suite_session, "int main() { enum Color { RED, GREEN, BLUE }; enum Color c = GREEN; return c; }");
  // 匿名enumの値指定は既知のバグ（enum初期化子パース未対応）で現在パースエラー
  // expect_parse_ok("int main() { enum { A=10, B=20, C=30 }; return B; }");

  // 構造体の前方参照と自己参照ポインタ
  // 自己参照ポインタメンバは現在パースエラー（不完全型ポインタ未対応の可能性）
  // expect_parse_ok("int main() { struct Node { int val; struct Node *next; }; struct Node n; n.val=42; n.next=0; return n.val; }");

  // void* の宣言と使用
  expect_parse_ok(test_suite_session, "int main() { void *p = 0; return p == 0 ? 42 : 0; }");

  // const修飾
  expect_parse_ok(test_suite_session, "int main() { const int x = 42; return x; }");
  expect_parse_ok(test_suite_session, "static const char *__const_leak_roots[]={\"\"}; typedef struct __ConstLeakFrame __ConstLeakFrame; struct __ConstLeakFrame{__ConstLeakFrame *next; const char *path;}; static __ConstLeakFrame *__const_leak_g; void f(void){ __ConstLeakFrame *p=0; __const_leak_g=p; }");
  expect_parse_ok(test_suite_session, "static const char *__const_ptr_tbl[4]; void f(const char *name){ __const_ptr_tbl[0]=name; }");
  expect_parse_ok(test_suite_session, "struct __ConstMemberPtr{const char *path;}; void f(struct __ConstMemberPtr *m,const char *path){ m->path=path; }");
  expect_parse_ok(test_suite_session, "void f(const char *); void f(const char *p){(void)p;}");
  expect_parse_ok(test_suite_session, "void f(const int *); void f(const int *p){(void)p;}");
  expect_parse_ok(test_suite_session, "void f(volatile char *); void f(volatile char *p){(void)p;}");
  expect_parse_ok(test_suite_session, "void f(char *); void f(char * const p){(void)p;}");
  expect_parse_fail(test_suite_session, "void f(char *); void f(const char *p){(void)p;}");
  expect_parse_fail(test_suite_session, "void f(const char **); void f(char **p){(void)p;}");
  expect_parse_ok(test_suite_session, "struct __PtrSubSym814{char *name; int len;}; struct __PtrSubData814{struct __PtrSubSym814 *symbols;}; static struct __PtrSubData814 __ptr_sub_g814; int main(void){__ptr_sub_g814.symbols[0].name=\"main\";return __ptr_sub_g814.symbols[0].name[0];}");
  // 後置const (int const x) は変数宣言で現在パースエラー
  // expect_parse_ok("int main() { int const x = 42; return x; }");

  // 配列の宣言と初期化
  expect_parse_ok(test_suite_session, "int main() { int a[3] = {1, 2, 3}; return a[0] + a[1] + a[2]; }");

  // 構造体のネストした初期化
  expect_parse_ok(test_suite_session, "int main() { struct P { int x; int y; }; struct R { struct P p; int z; }; struct R r = {{1,2},3}; return r.p.x + r.p.y + r.z; }");

  // for文のスコープ付き変数宣言
  expect_parse_ok(test_suite_session, "int main() { int s=0; for (int i=0; i<5; i=i+1) s=s+i; return s; }");

  // 構造体のサイズオフ
  expect_parse_ok(test_suite_session, "int main() { struct S { char a; int b; char c; }; return sizeof(struct S); }");

  // union の基本使用
  expect_parse_ok(test_suite_session, "int main() { union U { int x; char c; }; union U u; u.x=42; return u.x; }");

  // _Static_assert 正常系
  // _Static_assert with sizeof==4 — 定数式評価で==未対応の可能性
  // expect_parse_ok("_Static_assert(sizeof(int)==4, \"int is 4 bytes\"); int main() { return 42; }");

  // _Generic の複雑なケース
  expect_parse_ok(test_suite_session, "int main() { double d=1.0; return _Generic(d, int:0, double:42, default:99); }");

  // 複合リテラルの使用 — 現在パースエラーの可能性があるため個別検証
  //expect_parse_ok("int main() { struct P { int x; int y; }; struct P p = (struct P){10, 32}; return p.x + p.y; }");

  // 意地悪テスト: 異常系の追加
  // 自己参照は不完全型エラーにならない（ポインタ非ポインタを問わずパース通過する可能性）
  // expect_parse_fail("int main() { struct S { int x; struct S s; }; return 0; }");
  // 負のサイズは現在エラーにならない
  // expect_parse_fail("int main() { int a[-1]; return 0; }");
}

static void test_parser_config_matrix(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parser_config_matrix...\n");
  const char *struct_scalar_cast = "int main() { struct S { int x; int y; }; return ((struct S)7).x; }";
  const char *struct_pointer_cast = "int main() { struct S { int *p; int q; }; int x=3; return *((struct S)&x).p; }";
  const char *union_scalar_cast = "int main() { union U { int x; char y; }; return ((union U)7).x; }";
  const char *union_pointer_cast = "int main() { union U { int *p; int q; }; int x=3; return ((union U)&x).p==&x; }";
  const char *union_nonbrace_init = "int main() { union U { int a[2]; int z; }; union U u={1,2}; return 0; }";
  const char *same_size_nonscalar_cast =
      "int main() { struct A { int x; }; struct B { int x; }; struct A a={7}; return ((struct B)a).x; }";

  // baseline: all extensions enabled
  test_compilation_options(test_suite_session)->enable_struct_scalar_pointer_cast = true;
  test_compilation_options(test_suite_session)->enable_union_scalar_pointer_cast = true;
  test_compilation_options(test_suite_session)->enable_union_array_member_nonbrace_init = true;
  test_compilation_options(test_suite_session)->enable_size_compatible_nonscalar_cast = true;
  expect_parse_ok(test_suite_session, struct_scalar_cast);
  expect_parse_ok(test_suite_session, struct_pointer_cast);
  expect_parse_ok(test_suite_session, union_scalar_cast);
  expect_parse_ok(test_suite_session, union_pointer_cast);
  expect_parse_ok(test_suite_session, union_nonbrace_init);
  expect_parse_ok(test_suite_session, same_size_nonscalar_cast);

  // all extensions disabled: all extension snippets should fail
  test_compilation_options(test_suite_session)->enable_struct_scalar_pointer_cast = false;
  test_compilation_options(test_suite_session)->enable_union_scalar_pointer_cast = false;
  test_compilation_options(test_suite_session)->enable_union_array_member_nonbrace_init = false;
  test_compilation_options(test_suite_session)->enable_size_compatible_nonscalar_cast = false;
  expect_parse_fail(test_suite_session, struct_scalar_cast);
  expect_parse_fail(test_suite_session, struct_pointer_cast);
  expect_parse_fail(test_suite_session, union_scalar_cast);
  expect_parse_fail(test_suite_session, union_pointer_cast);
  expect_parse_fail(test_suite_session, union_nonbrace_init);
  expect_parse_fail(test_suite_session, same_size_nonscalar_cast);

  // restore defaults for subsequent tests
  test_compilation_options(test_suite_session)->enable_struct_scalar_pointer_cast = true;
  test_compilation_options(test_suite_session)->enable_union_scalar_pointer_cast = true;
  test_compilation_options(test_suite_session)->enable_union_array_member_nonbrace_init = true;
  test_compilation_options(test_suite_session)->enable_size_compatible_nonscalar_cast = true;
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

static void test_expr_nest_limits(
    ag_compilation_session_t *test_suite_session) {
  printf("test_expr_nest_limits...\n");
  char *ok = build_nested_paren_program(256);
  ASSERT_TRUE(ok != NULL);
  expect_parse_ok(test_suite_session, ok);
  free(ok);

  char *too_deep = build_nested_paren_program(1300);
  ASSERT_TRUE(too_deep != NULL);
  expect_parse_fail_with_message(test_suite_session, too_deep, "深すぎます");
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

static void test_parser_width_limits(
    ag_compilation_session_t *test_suite_session) {
  printf("test_parser_width_limits...\n");

  char *ok_decls = build_many_declarators_program(64);
  ASSERT_TRUE(ok_decls != NULL);
  expect_parse_ok(test_suite_session, ok_decls);
  free(ok_decls);

  char *too_many_decls = build_many_declarators_program(1300);
  ASSERT_TRUE(too_many_decls != NULL);
  expect_parse_fail_with_message(test_suite_session, too_many_decls, "宣言子列が多すぎます");
  free(too_many_decls);

  char *ok_inits = build_many_array_init_elements_program(128);
  ASSERT_TRUE(ok_inits != NULL);
  expect_parse_ok(test_suite_session, ok_inits);
  free(ok_inits);

  char *too_many_inits = build_many_array_init_elements_program(5000);
  ASSERT_TRUE(too_many_inits != NULL);
  expect_parse_fail_with_message(test_suite_session, too_many_inits, "初期化子要素数が多すぎます");
  free(too_many_inits);
}

static void test_semantic_canonical_type_invariant(
    ag_compilation_session_t *test_suite_session) {
  printf("test_semantic_canonical_type_invariant...\n");
  reset_test_translation_unit_state(test_suite_session);
  ASSERT_TRUE(resolve_program_input_hir(test_suite_session,
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
      "}"));

  const psx_hir_module_t *hir =
      ag_compilation_session_hir_module(test_suite_session);
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(test_semantic_context(test_suite_session));
  ASSERT_TRUE(hir != NULL);
  ASSERT_TRUE(types != NULL);
  ASSERT_EQ(3, psx_hir_module_root_count(hir));

  const char *function_names[] = {"inc", "apply", "main"};
  for (size_t i = 0;
       i < sizeof(function_names) / sizeof(function_names[0]); i++) {
    const psx_hir_node_t *function = find_test_named_hir_node(
        hir, PSX_HIR_FUNCTION, function_names[i], 0);
    ASSERT_TRUE(function != NULL);
    psx_qual_type_t signature =
        psx_hir_node_attached_qual_type(function);
    ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
        types, signature));
    ASSERT_EQ(PSX_TYPE_FUNCTION,
              test_qual_type_shape(test_suite_session, signature).kind);
  }

  size_t expression_count = 0;
  for (size_t i = 1; i <= psx_hir_module_node_count(hir); i++) {
    const psx_hir_node_t *node =
        psx_hir_module_lookup(hir, (psx_hir_node_id_t)i);
    ASSERT_TRUE(node != NULL);
    if (psx_hir_node_role(node) != PSX_HIR_ROLE_EXPRESSION)
      continue;
    expression_count++;
    ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
        types, psx_hir_node_qual_type(node)));
  }
  ASSERT_TRUE(expression_count > 0);

  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "s", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "values", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "fp", 0) != NULL);
  ASSERT_TRUE(find_test_named_hir_node(
                  hir, PSX_HIR_LOCAL, "total", 0) != NULL);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_MEMBER_ACCESS, 0) != NULL);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_SUBSCRIPT, 0) != NULL);
  ASSERT_TRUE(find_test_hir_node_kind(
                  hir, PSX_HIR_COMPOUND_ASSIGN, 0) != NULL);

  const psx_hir_node_t *call =
      find_test_hir_node_kind(hir, PSX_HIR_CALL, 0);
  ASSERT_TRUE(call != NULL);
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, psx_hir_node_attached_qual_type(call)));
}

static void test_recursive_declarator_capacity_boundary(
    ag_compilation_session_t *test_suite_session) {
  printf("test_recursive_declarator_capacity_boundary...\n");
  reset_test_translation_unit_state(test_suite_session);

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
  token_t *tokens = tk_tokenize_ctx(
      test_tokenizer(test_suite_session), pointer_declarator);
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  psx_parsed_declarator_t pointer_syntax =
      parse_test_declarator_syntax_tree(test_suite_session);
  ASSERT_EQ(96, pointer_syntax.declarator_shape.count);
  ASSERT_TRUE(pointer_syntax.declarator_shape.capacity >= 96);
  psx_declarator_shape_t pointer_shape;
  ASSERT_TRUE(ps_declarator_shape_copy(
      &pointer_shape, &pointer_syntax.declarator_shape));
  ASSERT_TRUE(pointer_shape.ops !=
              pointer_syntax.declarator_shape.ops);
  pointer_shape.ops[0].is_const_qualified = 1;
  ASSERT_TRUE(
      !pointer_syntax.declarator_shape.ops[0].is_const_qualified);
  psx_dispose_declarator_syntax(&pointer_syntax);

  char array_declarator[512] = "deep_array";
  used = strlen(array_declarator);
  for (int i = 0; i < 40; i++) {
    memcpy(array_declarator + used, "[1]", 3);
    used += 3;
  }
  array_declarator[used++] = ';';
  array_declarator[used] = '\0';
  tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), array_declarator);
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  psx_parsed_declarator_t array_syntax =
      parse_test_declarator_syntax_tree(test_suite_session);
  ASSERT_EQ(40, array_syntax.declarator_shape.count);
  ASSERT_EQ(40, array_syntax.array_bound_count);
  ASSERT_TRUE(array_syntax.array_bound_capacity >= 40);
  psx_dispose_declarator_syntax(&array_syntax);

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
  tokens = tk_tokenize_ctx(test_tokenizer(test_suite_session), function_declarator);
  tk_set_current_token_ctx(test_tokenizer(test_suite_session), tokens);
  psx_parsed_declarator_t function_syntax =
      parse_test_declarator_syntax_tree(test_suite_session);
  ASSERT_EQ(26, function_syntax.function_suffix_count);
  ASSERT_TRUE(function_syntax.function_suffix_capacity >= 26);
  ASSERT_EQ(51, function_syntax.declarator_shape.count);
  psx_dispose_declarator_syntax(&function_syntax);

  psx_semantic_type_table_t *types =
      psx_semantic_type_table_create();
  ASSERT_TRUE(types != NULL);
  psx_qual_type_t int_type =
      psx_semantic_type_table_intern_integer(
          types, PSX_INTEGER_KIND_INT, 0, 0);

  psx_qual_type_t deep_pointer = int_type;
  for (int i = 0; i < 96; i++) {
    if (i == 5)
      deep_pointer.qualifiers |= PSX_TYPE_QUALIFIER_CONST;
    if (i == 70)
      deep_pointer.qualifiers |= PSX_TYPE_QUALIFIER_VOLATILE;
    deep_pointer = psx_semantic_type_table_intern_pointer_to(
        types, deep_pointer);
  }
  psx_qual_type_t cursor = deep_pointer;
  for (int i = 0; i < 96; i++) {
    psx_type_shape_t shape = {0};
    ASSERT_TRUE(psx_semantic_type_table_describe(
        types, cursor.type_id, &shape));
    ASSERT_EQ(PSX_TYPE_POINTER, shape.kind);
    cursor = psx_semantic_type_table_base(
        types, cursor.type_id);
  }
  ASSERT_EQ(int_type.type_id, cursor.type_id);

  psx_qual_type_t deep_array = int_type;
  for (int i = 0; i < 40; i++)
    deep_array = psx_semantic_type_table_intern_array_of(
        types, deep_array, 1, 0);
  cursor = deep_array;
  for (int i = 0; i < 40; i++) {
    psx_type_shape_t shape = {0};
    ASSERT_TRUE(psx_semantic_type_table_describe(
        types, cursor.type_id, &shape));
    ASSERT_EQ(PSX_TYPE_ARRAY, shape.kind);
    ASSERT_EQ(1, shape.array_len);
    cursor = psx_semantic_type_table_base(
        types, cursor.type_id);
  }
  ASSERT_EQ(int_type.type_id, cursor.type_id);

  char signature[1024];
  int signature_length = psx_format_canonical_type_signature(
      types, deep_pointer,
      ag_target_info_data_layout(
          ag_compilation_session_target(test_suite_session)),
      signature, sizeof(signature));
  ASSERT_TRUE(signature_length > 0);
  ASSERT_TRUE((size_t)signature_length < sizeof(signature));
  psx_semantic_type_table_destroy(types);
}

static void count_arena_cleanup(void *data) {
  int *count = data;
  (*count)++;
}

static void test_arena_checkpoint_rollback(
    ag_compilation_session_t *test_suite_session) {
  printf("test_arena_checkpoint_rollback...\n");
  arena_context_t *arena_context = test_arena_context(test_suite_session);
  arena_free_all_in(arena_context);
  int *retained = arena_alloc_in(arena_context, sizeof(*retained));
  *retained = 41;
  int retained_cleanup_count = 0;
  ASSERT_TRUE(arena_register_cleanup_in(
      arena_context, count_arena_cleanup,
      &retained_cleanup_count));
  arena_checkpoint_t checkpoint = arena_checkpoint_in(arena_context);
  int *discarded = arena_alloc_in(arena_context, sizeof(*discarded));
  *discarded = 99;
  int discarded_cleanup_count = 0;
  ASSERT_TRUE(arena_register_cleanup_in(
      arena_context, count_arena_cleanup,
      &discarded_cleanup_count));
  arena_rollback_in(arena_context, checkpoint);
  ASSERT_EQ(0, retained_cleanup_count);
  ASSERT_EQ(1, discarded_cleanup_count);
  int *reused = arena_alloc_in(arena_context, sizeof(*reused));
  ASSERT_TRUE(reused == discarded);
  ASSERT_EQ(41, *retained);
  arena_free_all_in(arena_context);
  ASSERT_EQ(1, retained_cleanup_count);
}

static void test_semantic_type_identity(
    ag_compilation_session_t *test_suite_session) {
  printf("test_semantic_type_identity...\n");
  test_semantic_context_fixture_t fixture;
  ASSERT_TRUE(test_semantic_context_fixture_init(test_suite_session,
      &fixture, ag_compilation_session_target(test_suite_session)));
  psx_semantic_context_t *context = fixture.context;
  ASSERT_TRUE(context != NULL);
  psx_semantic_type_table_t *types =
      (psx_semantic_type_table_t *)
          ps_ctx_semantic_type_table_in(context);
  ASSERT_TRUE(types != NULL);

  psx_qual_type_t plain_int =
      ps_ctx_intern_integer_qual_type_in(
          context, PSX_INTEGER_KIND_INT, 0, 0);
  psx_qual_type_t unsigned_int =
      ps_ctx_intern_integer_qual_type_in(
          context, PSX_INTEGER_KIND_INT, 1, 0);
  psx_qual_type_t boolean =
      ps_ctx_intern_integer_qual_type_in(
          context, PSX_INTEGER_KIND_BOOL, 0, 0);
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, plain_int));
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, unsigned_int));
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, boolean));
  ASSERT_TRUE(plain_int.type_id != unsigned_int.type_id);
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            ps_ctx_intern_integer_qual_type_in(
                context, PSX_INTEGER_KIND_ENUM, 0, 0).type_id);

  psx_qual_type_t const_int = plain_int;
  const_int.qualifiers = PSX_TYPE_QUALIFIER_CONST;
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, const_int));
  ASSERT_EQ(plain_int.type_id, const_int.type_id);
  ASSERT_TRUE(psx_semantic_type_table_unqualified_types_match(
      types, plain_int, const_int));

  psx_qual_type_t const_array =
      ps_ctx_intern_array_of_qual_type_in(
          context, const_int, 4, 0);
  psx_qual_type_t repeated_const_array =
      ps_ctx_intern_array_of_qual_type_in(
          context, const_int, 4, 0);
  ASSERT_EQ(const_array.type_id,
            repeated_const_array.type_id);
  psx_qual_type_t array_element =
      psx_semantic_type_table_base(
          types, const_array.type_id);
  ASSERT_EQ(const_int.type_id, array_element.type_id);
  ASSERT_EQ(const_int.qualifiers, array_element.qualifiers);

  psx_qual_type_t matrix_row =
      ps_ctx_intern_array_of_qual_type_in(
          context, const_int, 3, 0);
  psx_qual_type_t matrix =
      ps_ctx_intern_array_of_qual_type_in(
          context, matrix_row, 2, 0);
  ASSERT_EQ(6, psx_semantic_type_table_array_flat_element_count(
                   types, matrix.type_id));
  ASSERT_EQ(3, psx_semantic_type_table_array_subscript_stride_elements(
                   types, matrix.type_id, 0));
  ASSERT_EQ(1, psx_semantic_type_table_array_subscript_stride_elements(
                   types, matrix.type_id, 1));
  ASSERT_EQ(const_int.type_id,
            psx_semantic_type_table_array_leaf(
                types, matrix.type_id).type_id);
  ASSERT_EQ(const_int.qualifiers,
            psx_semantic_type_table_array_leaf(
                types, matrix.type_id).qualifiers);

  psx_qual_type_t pointer_to_int =
      ps_ctx_intern_pointer_to_qual_type_in(
          context, plain_int);
  psx_qual_type_t pointer_to_const =
      ps_ctx_intern_pointer_to_qual_type_in(
          context, const_int);
  ASSERT_TRUE(pointer_to_int.type_id !=
              pointer_to_const.type_id);
  ASSERT_EQ(const_int.type_id,
            psx_semantic_type_table_base(
                types, pointer_to_const.type_id).type_id);
  ASSERT_EQ(const_int.qualifiers,
            psx_semantic_type_table_base(
                types, pointer_to_const.type_id).qualifiers);
  ASSERT_TRUE(!psx_semantic_type_table_unqualified_types_match(
      types, pointer_to_int, pointer_to_const));

  psx_qual_type_t vla =
      ps_ctx_intern_array_of_qual_type_in(
          context, plain_int, 0, 1);
  psx_qual_type_t pointer_to_vla =
      ps_ctx_intern_pointer_to_qual_type_in(
          context, vla);
  ASSERT_TRUE(psx_semantic_type_table_contains_vla_array(
      types, vla.type_id));
  ASSERT_TRUE(psx_semantic_type_table_contains_vla_array(
      types, pointer_to_vla.type_id));

  psx_qual_type_t direct_float =
      ps_ctx_intern_floating_qual_type_in(
          context, PSX_FLOATING_KIND_FLOAT, 0);
  psx_qual_type_t direct_complex =
      ps_ctx_intern_floating_qual_type_in(
          context, PSX_FLOATING_KIND_FLOAT, 1);
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, direct_complex.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_COMPLEX, shape.kind);
  ASSERT_EQ(PSX_FLOATING_KIND_FLOAT, shape.floating_kind);
  ASSERT_EQ(direct_float.type_id,
            psx_semantic_type_table_base(
                types, direct_complex.type_id).type_id);
  psx_qual_type_t direct_void =
      ps_ctx_intern_void_qual_type_in(context);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, direct_void.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_VOID, shape.kind);

  psx_qual_type_t first_enum =
      psx_semantic_type_table_intern_enum(
          types, 0x1001u, "SemanticMode", 12);
  psx_qual_type_t equivalent_enum =
      psx_semantic_type_table_intern_enum(
          types, 0x1001u, "SemanticMode", 12);
  psx_qual_type_t shadowed_enum =
      psx_semantic_type_table_intern_enum(
          types, 0x1002u, "SemanticMode", 12);
  ASSERT_EQ(first_enum.type_id, equivalent_enum.type_id);
  ASSERT_TRUE(first_enum.type_id != shadowed_enum.type_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, first_enum.type_id, &shape));
  ASSERT_EQ(PSX_INTEGER_KIND_ENUM, shape.integer_kind);
  ASSERT_EQ(12, shape.enum_tag_length);
  ASSERT_EQ(0x1001u, shape.enum_decl_id);

  char owned_enum_name[] = "OwnedSemanticMode";
  psx_qual_type_t owned_enum =
      psx_semantic_type_table_intern_enum(
          types, 0x1003u, owned_enum_name, 17);
  owned_enum_name[0] = 'X';
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, owned_enum.type_id, &shape));
  ASSERT_TRUE(strncmp("OwnedSemanticMode", shape.enum_tag_name,
                      (size_t)shape.enum_tag_length) == 0);

  char record_name[] = "DirectSemanticRecord";
  ASSERT_TRUE(ps_ctx_register_tag_type_in(
      context, TK_STRUCT, record_name, 20, 0, 0));
  const psx_record_decl_t *record =
      ps_ctx_ensure_tag_record_decl_in(
          context, TK_STRUCT, record_name, 20);
  ASSERT_TRUE(record != NULL);
  psx_qual_type_t record_type =
      ps_ctx_intern_record_qual_type_in(
          context, record->record_id);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, record_type.type_id, &shape));
  ASSERT_EQ(PSX_TYPE_STRUCT, shape.kind);
  ASSERT_EQ(record->record_id, shape.record_id);
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            ps_ctx_intern_record_qual_type_in(
                context, PSX_RECORD_ID_INVALID).type_id);

  const psx_qual_type_t parameters[2] = {
      const_int, pointer_to_int};
  psx_qual_type_t function =
      ps_ctx_intern_function_qual_type_in(
          context, plain_int, parameters, 2, 1, 0);
  ASSERT_TRUE(psx_semantic_type_table_qual_type_is_valid(
      types, function));
  ASSERT_EQ(plain_int.type_id,
            psx_semantic_type_table_base(
                types, function.type_id).type_id);
  ASSERT_EQ(const_int.qualifiers,
            psx_semantic_type_table_parameter(
                types, function.type_id, 0).qualifiers);
  ASSERT_EQ(pointer_to_int.type_id,
            psx_semantic_type_table_parameter(
                types, function.type_id, 1).type_id);

  psx_qual_type_t exact_int_void =
      ps_ctx_intern_function_qual_type_in(
          context, plain_int, NULL, 0, 1, 0);
  psx_qual_type_t exact_int_void_pointer =
      ps_ctx_intern_pointer_to_qual_type_in(
          context, exact_int_void);
  ASSERT_TRUE(psx_semantic_type_is_exact_int_void_function(
      types, exact_int_void));
  ASSERT_TRUE(psx_semantic_type_is_exact_int_void_function(
      types, exact_int_void_pointer));
  ASSERT_TRUE(!psx_semantic_type_is_exact_int_void_function(
      types, function));

  psx_qual_type_t invalid = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            ps_ctx_intern_pointer_to_qual_type_in(
                context, invalid).type_id);
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            ps_ctx_intern_array_of_qual_type_in(
                context, invalid, 1, 0).type_id);
  ASSERT_EQ(PSX_TYPE_ID_INVALID,
            ps_ctx_intern_function_qual_type_in(
                context, invalid, NULL, 0, 1, 0).type_id);

  psx_type_id_t retained_id = pointer_to_const.type_id;
  ASSERT_TRUE(psx_semantic_type_table_describe(
      types, retained_id, &shape));
  psx_scope_graph_reset(ps_ctx_scope_graph(context));
  ps_ctx_reset_translation_unit_scope_in(context);
  ASSERT_TRUE(!psx_semantic_type_table_describe(
      types, retained_id, &shape));

  test_semantic_context_fixture_dispose(&fixture);
}

static void test_semantic_context_isolation(
    ag_compilation_session_t *test_suite_session) {
  printf("test_semantic_context_isolation...\n");
  const ag_target_info_t *target =
      ag_compilation_session_target(test_suite_session);
  test_semantic_context_fixture_t first_fixture;
  test_semantic_context_fixture_t second_fixture;
  ASSERT_TRUE(test_semantic_context_fixture_init(test_suite_session,
      &first_fixture, target));
  ASSERT_TRUE(test_semantic_context_fixture_init(test_suite_session,
      &second_fixture, target));
  psx_semantic_context_t *first = first_fixture.context;
  psx_semantic_context_t *second = second_fixture.context;
  ASSERT_TRUE(first != NULL);
  ASSERT_TRUE(second != NULL);
  ASSERT_TRUE(first != second);
  ASSERT_TRUE(ps_ctx_semantic_type_table_in(first) !=
              ps_ctx_semantic_type_table_in(second));
  ASSERT_TRUE(ps_ctx_scope_graph(first) !=
              ps_ctx_scope_graph(second));

  ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(
          test_suite_session);
  ASSERT_TRUE(ps_global_registry_create(
                  NULL, ps_ctx_scope_graph(first)) == NULL);
  ASSERT_TRUE(ps_global_registry_create(
                  ps_ctx_semantic_type_table_in(first),
                  NULL) == NULL);
  ASSERT_TRUE(ps_local_registry_create(
                  diagnostics, NULL,
                  ps_ctx_scope_graph(first)) == NULL);
  ASSERT_TRUE(ps_local_registry_create(
                  diagnostics,
                  ps_ctx_semantic_type_table_in(first),
                  NULL) == NULL);

  psx_global_registry_t *first_globals =
      ps_global_registry_create(
          ps_ctx_semantic_type_table_in(first),
          ps_ctx_scope_graph(first));
  psx_global_registry_t *second_globals =
      ps_global_registry_create(
          ps_ctx_semantic_type_table_in(second),
          ps_ctx_scope_graph(second));
  psx_local_registry_t *first_locals =
      ps_local_registry_create(
          diagnostics,
          ps_ctx_semantic_type_table_in(first),
          ps_ctx_scope_graph(first));
  psx_local_registry_t *second_locals =
      ps_local_registry_create(
          diagnostics,
          ps_ctx_semantic_type_table_in(second),
          ps_ctx_scope_graph(second));
  ASSERT_TRUE(first_globals != NULL);
  ASSERT_TRUE(second_globals != NULL);
  ASSERT_TRUE(first_locals != NULL);
  ASSERT_TRUE(second_locals != NULL);

  psx_qual_type_t first_integer =
      ps_ctx_intern_integer_qual_type_in(
          first, PSX_INTEGER_KIND_INT, 0, 0);
  psx_qual_type_t second_long =
      ps_ctx_intern_integer_qual_type_in(
          second, PSX_INTEGER_KIND_LONG, 0, 0);
  ASSERT_TRUE(first_integer.type_id !=
              PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(second_long.type_id !=
              PSX_TYPE_ID_INVALID);

  const psx_typedef_info_t first_alias = {
      .decl_type_table =
          ps_ctx_semantic_type_table_in(first),
      .decl_qual_type = first_integer,
  };
  ASSERT_TRUE(ps_ctx_register_typedef_name_in(
      first, (char *)"ContextType", 11,
      &first_alias, NULL, NULL));
  ASSERT_TRUE(ps_ctx_register_enum_const_in(
      first, (char *)"ContextValue", 12, 11, NULL));
  ASSERT_TRUE(ps_ctx_register_enum_const_in(
      second, (char *)"ContextValue", 12, 22, NULL));
  ASSERT_TRUE(ps_ctx_register_tag_type_in(
      first, TK_STRUCT, (char *)"ContextTag", 10,
      0, 0));

  psx_qual_type_t second_function =
      ps_ctx_intern_function_qual_type_in(
          second, second_long, NULL, 0, 1, 0);
  ASSERT_TRUE(second_function.type_id !=
              PSX_TYPE_ID_INVALID);
  ASSERT_TRUE(ps_ctx_register_function_qual_type_in(
      second, (char *)"ContextFunction", 15,
      second_function) != NULL);

  long long value = 0;
  ASSERT_TRUE(ps_ctx_find_enum_const_in(
      first, (char *)"ContextValue", 12, &value));
  ASSERT_EQ(11, value);
  ASSERT_TRUE(ps_ctx_find_enum_const_in(
      second, (char *)"ContextValue", 12, &value));
  ASSERT_EQ(22, value);
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(
      first, (char *)"ContextType", 11, NULL));
  ASSERT_TRUE(!ps_ctx_find_typedef_name_in(
      second, (char *)"ContextType", 11, NULL));
  ASSERT_TRUE(ps_ctx_has_tag_type_in(
      first, TK_STRUCT, (char *)"ContextTag", 10));
  ASSERT_TRUE(!ps_ctx_has_tag_type_in(
      second, TK_STRUCT, (char *)"ContextTag", 10));
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
                  first, (char *)"ContextFunction", 15) == NULL);
  ASSERT_TRUE(ps_ctx_find_function_symbol_in(
                  second, (char *)"ContextFunction", 15) != NULL);

  ASSERT_TRUE(find_test_scope_declaration(
                  ps_ctx_scope_graph(first), "ContextType",
                  PSX_DECL_TYPEDEF, 0) != NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  ps_ctx_scope_graph(second), "ContextType",
                  PSX_DECL_TYPEDEF, 0) == NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  ps_ctx_scope_graph(first), "ContextTag",
                  PSX_DECL_TAG, 0) != NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  ps_ctx_scope_graph(second), "ContextTag",
                  PSX_DECL_TAG, 0) == NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  ps_ctx_scope_graph(first), "ContextFunction",
                  PSX_DECL_FUNCTION, 0) == NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  ps_ctx_scope_graph(second), "ContextFunction",
                  PSX_DECL_FUNCTION, 0) != NULL);

  psx_typed_hir_tree_t semantic_expression = {0};
  psx_semantic_expr_id_t expression_id =
      ps_ctx_register_semantic_expression_in(
          second, &semantic_expression);
  ASSERT_TRUE(expression_id !=
              PSX_SEMANTIC_EXPR_ID_INVALID);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  second, expression_id) ==
              &semantic_expression);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  first, expression_id) == NULL);

  psx_scope_graph_reset(ps_ctx_scope_graph(second));
  ps_ctx_reset_translation_unit_scope_in(second);
  ASSERT_TRUE(ps_ctx_semantic_expression_in(
                  second, expression_id) == NULL);
  ASSERT_TRUE(!ps_ctx_find_enum_const_in(
      second, (char *)"ContextValue", 12, &value));
  ASSERT_TRUE(ps_ctx_find_enum_const_in(
      first, (char *)"ContextValue", 12, &value));
  ASSERT_EQ(11, value);
  ASSERT_TRUE(ps_ctx_find_typedef_name_in(
      first, (char *)"ContextType", 11, NULL));

  ps_global_registry_destroy(first_globals);
  ps_global_registry_destroy(second_globals);
  ps_local_registry_destroy(first_locals);
  ps_local_registry_destroy(second_locals);
  test_semantic_context_fixture_dispose(&first_fixture);
  test_semantic_context_fixture_dispose(&second_fixture);
}
static void test_compilation_session_registry_isolation(
    ag_compilation_session_t *test_suite_session) {
  printf("test_compilation_session_registry_isolation...\n");
  ag_compilation_session_t first;
  ag_compilation_session_t second;
  const ag_target_info_t *target =
      ag_compilation_session_target(test_suite_session);
  ASSERT_TRUE(ag_compilation_session_init(&first, target));
  ASSERT_TRUE(ag_compilation_session_init(&second, target));
  ASSERT_TRUE(ag_compilation_session_is_complete(&first));
  ASSERT_TRUE(ag_compilation_session_is_complete(&second));
  ASSERT_TRUE(first.scope_graph != second.scope_graph);
  ASSERT_TRUE(first.semantic_context != second.semantic_context);
  ASSERT_TRUE(first.global_registry != second.global_registry);
  ASSERT_TRUE(first.local_registry != second.local_registry);
  ASSERT_TRUE(ps_global_registry_scope_graph(
                  first.global_registry) == first.scope_graph);
  ASSERT_TRUE(ps_global_registry_scope_graph(
                  second.global_registry) == second.scope_graph);

  const char *first_source =
      "typedef int FirstType; "
      "enum FirstValues { FIRST_ENUM = 37 }; "
      "struct FirstTag { int value; }; "
      "struct SessionAggregate { int left; int right; }; "
      "int shared_global = 41; "
      "int FirstOnlyGlobal = 7; "
      "int session_fn(void) { return 1; } "
      "int (*session_callback)(void) = session_fn; "
      "struct SessionAggregate session_aggregate = {11, 22}; "
      "char *shared_string = \"first\"; "
      "int session_probe(void) { "
      "  FirstType shared_local = FIRST_ENUM; "
      "  struct FirstTag tag = {0}; "
      "  return shared_local + tag.value; "
      "}";
  const char *second_source =
      "struct SessionAggregate { int left; long right; }; "
      "long shared_global = 99; "
      "long *session_callback = &shared_global; "
      "struct SessionAggregate session_aggregate = {33, 44}; "
      "char *shared_string = \"second\"; "
      "long session_probe(void) { "
      "  long shared_local = shared_global; "
      "  return shared_local; "
      "}";

  ASSERT_TRUE(resolve_test_program_hir_from_in_session(
      &first, tk_tokenize_ctx(
                  ag_compilation_session_tokenizer(&first),
                  (char *)first_source)));
  ASSERT_TRUE(resolve_test_program_hir_from_in_session(
      &second, tk_tokenize_ctx(
                   ag_compilation_session_tokenizer(&second),
                   (char *)second_source)));

  const psx_scope_graph_t *first_graph =
      ps_ctx_scope_graph(first.semantic_context);
  const psx_scope_graph_t *second_graph =
      ps_ctx_scope_graph(second.semantic_context);
  const psx_scope_declaration_t *first_global_declaration =
      find_test_scope_declaration(
          first_graph, "shared_global",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *second_global_declaration =
      find_test_scope_declaration(
          second_graph, "shared_global",
          PSX_DECL_GLOBAL_OBJECT, 0);
  const psx_scope_declaration_t *first_local_declaration =
      find_test_scope_declaration(
          first_graph, "shared_local",
          PSX_DECL_LOCAL_OBJECT, 0);
  const psx_scope_declaration_t *second_local_declaration =
      find_test_scope_declaration(
          second_graph, "shared_local",
          PSX_DECL_LOCAL_OBJECT, 0);
  ASSERT_TRUE(first_global_declaration != NULL);
  ASSERT_TRUE(second_global_declaration != NULL);
  ASSERT_TRUE(first_local_declaration != NULL);
  ASSERT_TRUE(second_local_declaration != NULL);
  ASSERT_TRUE(first_global_declaration->payload !=
              second_global_declaration->payload);
  ASSERT_TRUE(first_local_declaration->payload !=
              second_local_declaration->payload);

  global_var_t *first_global =
      (global_var_t *)first_global_declaration->payload;
  global_var_t *second_global =
      (global_var_t *)second_global_declaration->payload;
  lvar_t *first_local =
      (lvar_t *)first_local_declaration->payload;
  lvar_t *second_local =
      (lvar_t *)second_local_declaration->payload;
  const psx_semantic_type_table_t *first_types =
      ps_ctx_semantic_type_table_in(first.semantic_context);
  const psx_semantic_type_table_t *second_types =
      ps_ctx_semantic_type_table_in(second.semantic_context);
  psx_type_shape_t shape = {0};
  ASSERT_TRUE(psx_semantic_type_table_describe(
      first_types, ps_gvar_decl_type_id(first_global), &shape));
  ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      second_types, ps_gvar_decl_type_id(second_global), &shape));
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, shape.integer_kind);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      first_types, ps_lvar_decl_type_id(first_local), &shape));
  ASSERT_EQ(PSX_INTEGER_KIND_INT, shape.integer_kind);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      second_types, ps_lvar_decl_type_id(second_local), &shape));
  ASSERT_EQ(PSX_INTEGER_KIND_LONG, shape.integer_kind);

  ASSERT_TRUE(find_test_scope_declaration(
                  first_graph, "FirstType",
                  PSX_DECL_TYPEDEF, 0) != NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  second_graph, "FirstType",
                  PSX_DECL_TYPEDEF, 0) == NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  first_graph, "FIRST_ENUM",
                  PSX_DECL_ENUM_CONSTANT, 0) != NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  second_graph, "FIRST_ENUM",
                  PSX_DECL_ENUM_CONSTANT, 0) == NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  first_graph, "FirstTag",
                  PSX_DECL_TAG, 0) != NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  second_graph, "FirstTag",
                  PSX_DECL_TAG, 0) == NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  first_graph, "FirstOnlyGlobal",
                  PSX_DECL_GLOBAL_OBJECT, 0) != NULL);
  ASSERT_TRUE(find_test_scope_declaration(
                  second_graph, "FirstOnlyGlobal",
                  PSX_DECL_GLOBAL_OBJECT, 0) == NULL);

  global_var_t *first_aggregate =
      (global_var_t *)find_test_scope_declaration(
          first_graph, "session_aggregate",
          PSX_DECL_GLOBAL_OBJECT, 0)->payload;
  global_var_t *second_aggregate =
      (global_var_t *)find_test_scope_declaration(
          second_graph, "session_aggregate",
          PSX_DECL_GLOBAL_OBJECT, 0)->payload;
  ASSERT_TRUE(first_aggregate != NULL);
  ASSERT_TRUE(second_aggregate != NULL);
  ASSERT_TRUE(psx_semantic_type_table_describe(
      first_types, ps_gvar_decl_type_id(first_aggregate), &shape));
  psx_record_id_t first_record_id = shape.record_id;
  ASSERT_TRUE(psx_semantic_type_table_describe(
      second_types, ps_gvar_decl_type_id(second_aggregate), &shape));
  psx_record_id_t second_record_id = shape.record_id;
  const psx_record_decl_t *first_record =
      psx_record_decl_table_lookup(
          ps_ctx_record_decl_table_in(first.semantic_context),
          first_record_id);
  const psx_record_decl_t *second_record =
      psx_record_decl_table_lookup(
          ps_ctx_record_decl_table_in(second.semantic_context),
          second_record_id);
  ASSERT_TRUE(first_record != NULL);
  ASSERT_TRUE(second_record != NULL);
  ASSERT_TRUE(first_record != second_record);
  ASSERT_EQ(8, psx_type_layout_sizeof(
                   first_types,
                   ps_ctx_record_layout_table_in(
                       first.semantic_context),
                   ps_gvar_decl_type_id(first_aggregate),
                   ag_target_info_data_layout(&first.target)));
  ASSERT_EQ(16, psx_type_layout_sizeof(
                    second_types,
                    ps_ctx_record_layout_table_in(
                        second.semantic_context),
                    ps_gvar_decl_type_id(second_aggregate),
                    ag_target_info_data_layout(&second.target)));

  ir_data_module_t *first_data =
      lower_ir_translation_unit_data_in_session(&first);
  ir_data_module_t *second_data =
      lower_ir_translation_unit_data_in_session(&second);
  ASSERT_TRUE(first_data != NULL);
  ASSERT_TRUE(second_data != NULL);

  ir_data_object_t *first_global_data =
      ir_data_module_find_object(
          first_data, "shared_global", 13);
  ir_data_object_t *second_global_data =
      ir_data_module_find_object(
          second_data, "shared_global", 13);
  ir_data_object_t *first_callback_data =
      ir_data_module_find_object(
          first_data, "session_callback", 16);
  ir_data_object_t *second_callback_data =
      ir_data_module_find_object(
          second_data, "session_callback", 16);
  ir_data_object_t *first_aggregate_data =
      ir_data_module_find_object(
          first_data, "session_aggregate", 17);
  ir_data_object_t *second_aggregate_data =
      ir_data_module_find_object(
          second_data, "session_aggregate", 17);
  ir_data_object_t *first_string_pointer =
      ir_data_module_find_object(
          first_data, "shared_string", 13);
  ir_data_object_t *second_string_pointer =
      ir_data_module_find_object(
          second_data, "shared_string", 13);
  ASSERT_TRUE(first_global_data != NULL);
  ASSERT_TRUE(second_global_data != NULL);
  ASSERT_TRUE(first_callback_data != NULL);
  ASSERT_TRUE(second_callback_data != NULL);
  ASSERT_TRUE(first_aggregate_data != NULL);
  ASSERT_TRUE(second_aggregate_data != NULL);
  ASSERT_TRUE(first_string_pointer != NULL);
  ASSERT_TRUE(second_string_pointer != NULL);

  ASSERT_EQ(41, first_global_data->bytes[0]);
  ASSERT_EQ(99, second_global_data->bytes[0]);
  ASSERT_EQ(IR_DATA_RELOC_FUNCTION,
            first_callback_data->relocs->kind);
  ASSERT_TRUE(first_callback_data->relocs->has_function_type);
  ASSERT_EQ(IR_DATA_RELOC_DATA,
            second_callback_data->relocs->kind);
  ASSERT_EQ(11, first_aggregate_data->bytes[0]);
  ASSERT_EQ(22, first_aggregate_data->bytes[4]);
  ASSERT_EQ(33, second_aggregate_data->bytes[0]);
  ASSERT_EQ(44, second_aggregate_data->bytes[8]);

  ASSERT_TRUE(first_string_pointer->relocs != NULL);
  ASSERT_TRUE(second_string_pointer->relocs != NULL);
  ir_data_object_t *first_literal = ir_data_module_find_object(
      first_data, first_string_pointer->relocs->target,
      first_string_pointer->relocs->target_len);
  ir_data_object_t *second_literal = ir_data_module_find_object(
      second_data, second_string_pointer->relocs->target,
      second_string_pointer->relocs->target_len);
  ASSERT_TRUE(first_literal != NULL);
  ASSERT_TRUE(second_literal != NULL);
  ASSERT_TRUE(memcmp(first_literal->bytes, "first\0", 6) == 0);
  ASSERT_TRUE(memcmp(second_literal->bytes, "second\0", 7) == 0);

  ir_abi_type_context_t first_data_abi_context = {
      .semantic_types = first_types,
      .record_layouts =
          ps_ctx_record_layout_table_in(first.semantic_context),
      .target = ag_compilation_session_target(&first),
  };
  ir_abi_data_module_t *first_data_abi =
      ir_abi_lower_data_module(
          &first_data_abi_context, first_data);
  const ir_abi_signature_t *first_callback_abi =
      ir_abi_data_relocation_signature(
          first_data_abi, first_callback_data->relocs);
  ASSERT_TRUE(first_callback_abi != NULL);
  ASSERT_EQ(0, first_callback_abi->param_count);
  ASSERT_EQ(IR_TY_I32,
            ir_abi_signature_direct_result_type(
                first_callback_abi));

  ir_abi_data_module_free(first_data_abi);
  ir_data_module_free(first_data);
  ir_data_module_free(second_data);
  ASSERT_TRUE(ag_compilation_session_dispose(&first));
  ASSERT_TRUE(ag_compilation_session_dispose(&second));
}
typedef struct {
  int destroy_count;
} test_backend_context_t;

typedef struct {
  char bytes[64];
  size_t length;
} test_codegen_output_t;

static void test_codegen_capture(
    const char *line, size_t length, void *user_data) {
  test_codegen_output_t *output = user_data;
  if (!output || output->length + length >= sizeof(output->bytes)) return;
  memcpy(output->bytes + output->length, line, length);
  output->length += length;
  output->bytes[output->length] = '\0';
}

static void test_backend_destroy(void *context) {
  test_backend_context_t *backend = context;
  backend->destroy_count++;
}

static void test_compilation_session_owns_target_and_tokenizer(
    ag_compilation_session_t *test_suite_session) {
  printf("test_compilation_session_owns_target_and_tokenizer...\n");
  ag_target_info_t host_target = ag_target_info_host();
  ag_target_info_t wasm_target = ag_target_info_wasm32();
  ag_target_info_t wide_pointer_target = host_target;
  wide_pointer_target.data_layout.pointer_size = 16;
  wide_pointer_target.data_layout.pointer_alignment = 16;
  ag_compilation_session_t host;
  ag_compilation_session_t wasm;
  ag_compilation_session_t wide_pointer;
  test_backend_context_t host_backend = {0};
  test_backend_context_t wasm_backend = {0};
  test_codegen_output_t host_output = {0};
  test_codegen_output_t wasm_output = {0};
  ASSERT_TRUE(ag_compilation_session_init(&host, &host_target));
  ASSERT_TRUE(ag_compilation_session_init(&wasm, &wasm_target));
  ASSERT_TRUE(ag_compilation_session_init(
      &wide_pointer, &wide_pointer_target));
  ASSERT_EQ(16, test_target_pointer_size(&wide_pointer.target));
  ASSERT_TRUE(ps_ctx_target_info(wide_pointer.semantic_context) ==
              &wide_pointer.target);
  ASSERT_TRUE(ps_lowering_target(wide_pointer.lowering_context) ==
              &wide_pointer.target);
  ASSERT_EQ(16, test_target_pointer_size(
                    ps_ctx_target_info(wide_pointer.semantic_context)));
  ASSERT_EQ(16, test_target_pointer_size(
                    ps_lowering_target(wide_pointer.lowering_context)));
  ASSERT_TRUE(ag_compilation_session_dispose(&wide_pointer));
  ASSERT_TRUE(ag_compilation_session_set_backend_context(
      &host, &host_backend, test_backend_destroy));
  ASSERT_TRUE(ag_compilation_session_set_backend_context(
      &wasm, &wasm_backend, test_backend_destroy));
  ASSERT_TRUE(ag_compilation_session_backend_context(&host) ==
              &host_backend);
  ASSERT_TRUE(ag_compilation_session_backend_context(&wasm) ==
              &wasm_backend);
  ASSERT_TRUE(ag_compilation_session_is_complete(&host));
  ASSERT_TRUE(ag_compilation_session_is_complete(&wasm));
  ASSERT_TRUE(pp_context_create(
      host.diagnostic_context, &wasm.tokenizer, &host.target) == NULL);
  ASSERT_TRUE(pp_context_create(
      host.diagnostic_context, &host.tokenizer, NULL) == NULL);
  tokenizer_context_t invalid_tokenizer;
  ASSERT_TRUE(!tk_context_init(
      &invalid_tokenizer, NULL, host.token_allocator_context,
      host.source_manager));
  ASSERT_TRUE(!tk_context_init(
      &invalid_tokenizer, host.diagnostic_context, NULL,
      host.source_manager));
  ASSERT_TRUE(!tk_context_init(
      &invalid_tokenizer, host.diagnostic_context,
      host.token_allocator_context, NULL));
  ag_source_manager_t *foreign_sources = ag_source_manager_create();
  ASSERT_TRUE(foreign_sources != NULL);
  ASSERT_TRUE(!tk_context_init(
      &invalid_tokenizer, host.diagnostic_context,
      host.token_allocator_context, foreign_sources));
  ag_source_manager_destroy(foreign_sources);
  ASSERT_TRUE(diag_context_create(NULL) == NULL);
  ASSERT_TRUE(ag_compilation_session_ir_allocation_stats(&host) ==
              &host.ir_allocation_stats);
  ASSERT_TRUE(ag_compilation_session_ir_allocation_stats(&wasm) ==
              &wasm.ir_allocation_stats);
  ASSERT_TRUE(ag_compilation_session_ir_allocation_stats(&host) !=
              ag_compilation_session_ir_allocation_stats(&wasm));
  ASSERT_EQ(0, ir_allocation_stats_instruction_peak(
                   ag_compilation_session_ir_allocation_stats_view(
                       &host)));
  ASSERT_EQ(0, ir_allocation_stats_block_peak(
                   ag_compilation_session_ir_allocation_stats_view(
                       &wasm)));
  ASSERT_TRUE(ag_compilation_session_semantic_context(&host) ==
              host.semantic_context);
  ASSERT_TRUE(ag_compilation_session_global_registry(&host) ==
              host.global_registry);
  ASSERT_TRUE(ps_global_registry_semantic_types(host.global_registry) ==
              ps_ctx_semantic_type_table_in(host.semantic_context));
  ASSERT_TRUE(ag_compilation_session_local_registry(&host) ==
              host.local_registry);
  ASSERT_TRUE(ps_local_registry_semantic_types(host.local_registry) ==
              ps_ctx_semantic_type_table_in(host.semantic_context));
  ASSERT_TRUE(ag_compilation_session_preprocessor_context(&host) ==
              host.preprocessor_context);
  ASSERT_TRUE(ag_compilation_session_arena_context(&host) ==
              host.arena_context);
  ASSERT_TRUE(ag_compilation_session_diagnostic_context(&host) ==
              host.diagnostic_context);
  ASSERT_TRUE(tk_context_diagnostics(&host.tokenizer) ==
              host.diagnostic_context);
  ASSERT_TRUE(tk_context_allocator(&host.tokenizer) ==
              host.token_allocator_context);
  ASSERT_TRUE(tk_allocator_diagnostics(host.token_allocator_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(ps_ctx_diagnostics(host.semantic_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(ps_local_registry_diagnostics(host.local_registry) ==
              host.diagnostic_context);
  ASSERT_TRUE(ps_parser_runtime_diagnostics(host.parser_runtime_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(pp_context_diagnostics(host.preprocessor_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(pp_context_tokenizer(host.preprocessor_context) ==
              &host.tokenizer);
  ASSERT_TRUE(pp_context_target(host.preprocessor_context) ==
              &host.target);
  ASSERT_TRUE(ps_lowering_diagnostics(host.lowering_context) ==
              host.diagnostic_context);
  ASSERT_TRUE(cg_context_diagnostics(host.codegen_emit_context) ==
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
  ASSERT_TRUE(ps_global_registry_semantic_types(wasm.global_registry) ==
              ps_ctx_semantic_type_table_in(wasm.semantic_context));
  ASSERT_TRUE(ag_compilation_session_local_registry(&wasm) ==
              wasm.local_registry);
  ASSERT_TRUE(ps_local_registry_semantic_types(wasm.local_registry) ==
              ps_ctx_semantic_type_table_in(wasm.semantic_context));
  ASSERT_TRUE(ag_compilation_session_preprocessor_context(&wasm) ==
              wasm.preprocessor_context);
  ASSERT_TRUE(ps_ctx_diagnostics(wasm.semantic_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(tk_context_diagnostics(&wasm.tokenizer) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(tk_allocator_diagnostics(wasm.token_allocator_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(ps_local_registry_diagnostics(wasm.local_registry) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(ps_parser_runtime_diagnostics(wasm.parser_runtime_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(pp_context_diagnostics(wasm.preprocessor_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(pp_context_tokenizer(wasm.preprocessor_context) ==
              &wasm.tokenizer);
  ASSERT_TRUE(pp_context_target(wasm.preprocessor_context) ==
              &wasm.target);
  ASSERT_TRUE(ps_lowering_diagnostics(wasm.lowering_context) ==
              wasm.diagnostic_context);
  ASSERT_TRUE(cg_context_diagnostics(wasm.codegen_emit_context) ==
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
  ASSERT_TRUE(ps_lowering_semantic_types(host.lowering_context) ==
              ps_ctx_semantic_type_table_in(host.semantic_context));
  ASSERT_TRUE(ps_lowering_semantic_types(wasm.lowering_context) ==
              ps_ctx_semantic_type_table_in(wasm.semantic_context));
  ASSERT_TRUE(ps_lowering_record_decls(host.lowering_context) ==
              ps_ctx_record_decl_table_in(host.semantic_context));
  ASSERT_TRUE(ps_lowering_record_decls(wasm.lowering_context) ==
              ps_ctx_record_decl_table_in(wasm.semantic_context));
  ASSERT_TRUE(ps_lowering_record_layouts(host.lowering_context) ==
              ps_ctx_record_layout_table_in(host.semantic_context));
  ASSERT_TRUE(ps_lowering_record_layouts(wasm.lowering_context) ==
              ps_ctx_record_layout_table_in(wasm.semantic_context));
  ASSERT_EQ(8, test_target_pointer_size(ag_compilation_session_target(&host)));
  ASSERT_EQ(4, test_target_pointer_size(ag_compilation_session_target(&wasm)));
  ASSERT_EQ(
      8, test_target_pointer_size(ps_ctx_target_info(host.semantic_context)));
  ASSERT_EQ(
      4, test_target_pointer_size(ps_ctx_target_info(wasm.semantic_context)));
  ASSERT_EQ(
      8, test_target_pointer_size(ps_lowering_target(host.lowering_context)));
  ASSERT_EQ(
      4, test_target_pointer_size(ps_lowering_target(wasm.lowering_context)));
  ASSERT_TRUE(ag_compilation_session_reset_translation_unit(&host));
  ASSERT_TRUE(ag_compilation_session_reset_translation_unit(&wasm));
  ASSERT_EQ(
      8, test_target_pointer_size(ps_ctx_target_info(host.semantic_context)));
  ASSERT_EQ(
      4, test_target_pointer_size(ps_ctx_target_info(wasm.semantic_context)));
  ASSERT_EQ(
      8, test_target_pointer_size(ps_lowering_target(host.lowering_context)));
  ASSERT_EQ(
      4, test_target_pointer_size(ps_lowering_target(wasm.lowering_context)));
  ASSERT_TRUE(host.preprocessor_context != NULL);
  ASSERT_TRUE(wasm.preprocessor_context != NULL);
  ASSERT_TRUE(host.preprocessor_context != wasm.preprocessor_context);
  ASSERT_TRUE(host.arena_context != NULL);
  ASSERT_TRUE(wasm.arena_context != NULL);
  ASSERT_TRUE(host.arena_context != wasm.arena_context);
  ASSERT_TRUE(host.diagnostic_context != NULL);
  ASSERT_TRUE(wasm.diagnostic_context != NULL);
  ASSERT_TRUE(host.diagnostic_context != wasm.diagnostic_context);
  ASSERT_TRUE(host.source_manager != NULL);
  ASSERT_TRUE(wasm.source_manager != NULL);
  ASSERT_TRUE(host.source_manager != wasm.source_manager);
  ASSERT_TRUE(ag_compilation_session_source_manager(&host) ==
              host.source_manager);
  ASSERT_TRUE(diag_context_source_manager(host.diagnostic_context) ==
              host.source_manager);
  ASSERT_TRUE(tk_context_source_manager(&host.tokenizer) ==
              host.source_manager);
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
  tokenizer_context_t *suite_tokenizer = test_tokenizer(test_suite_session);
  token_t *suite_cursor_before_explicit_statement =
      tk_get_current_token_ctx(suite_tokenizer);
  token_t wasm_statement_eof = {.kind = TK_EOF};
  token_t wasm_statement = {
      .kind = TK_SEMI,
      .next = &wasm_statement_eof,
  };
  tk_set_current_token_ctx(&wasm.tokenizer, &wasm_statement);
  psx_name_classifier_t wasm_classifier =
      ps_ctx_name_classifier(wasm.semantic_context);
  psx_statement_syntax_adapter_t wasm_statement_adapter;
  ASSERT_TRUE(psx_statement_syntax_adapter_init(
      &wasm_statement_adapter, wasm.parser_runtime_context,
      &wasm_classifier, NULL));
  psx_statement_syntax_context_t wasm_statement_syntax =
      psx_statement_syntax_adapter_context(
          &wasm_statement_adapter);
  node_t *wasm_null_statement =
      psx_stmt_stmt_syntax(&wasm_statement_syntax);
  ASSERT_TRUE(wasm_null_statement != NULL);
  ASSERT_EQ(ND_NULL_STMT, wasm_null_statement->kind);
  ASSERT_TRUE(tk_at_eof_ctx(&wasm.tokenizer));
  ASSERT_TRUE(tk_get_current_token_ctx(suite_tokenizer) ==
              suite_cursor_before_explicit_statement);
  ASSERT_TRUE(arena_alloc_in(host.arena_context, 16) != NULL);
  ASSERT_TRUE(arena_alloc_in(wasm.arena_context, 32) != NULL);
  ASSERT_TRUE(arena_current_reserved_bytes_in(host.arena_context) > 0);
  ASSERT_TRUE(arena_current_reserved_bytes_in(wasm.arena_context) > 0);
  ag_compilation_session_t *previous_session = test_suite_session;
  ASSERT_EQ(8, ag_compilation_session_target(previous_session)->data_layout.pointer_size);
  ASSERT_EQ(8, ag_compilation_session_target(&host)->data_layout.pointer_size);
  ASSERT_TRUE(tk_get_current_token_ctx(suite_tokenizer) ==
              suite_cursor_before_explicit_statement);
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
  ASSERT_EQ(4, ag_compilation_session_target(&wasm)->data_layout.pointer_size);
  ASSERT_TRUE(tk_get_current_token_ctx(suite_tokenizer) ==
              suite_cursor_before_explicit_statement);
  cg_emitf_in(
      ag_compilation_session_codegen_emit_context(&wasm), "wasm");
  cg_emitf_in(
      ag_compilation_session_codegen_emit_context(&host),
      "-host-explicit");
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
  ASSERT_TRUE(tk_get_current_token_ctx(suite_tokenizer) ==
              suite_cursor_before_explicit_statement);
  ASSERT_TRUE(ag_compilation_session_is_complete(&host));
  ASSERT_EQ(0, host_backend.destroy_count);
  ag_compilation_session_t inherited_context;
  ASSERT_TRUE(ag_compilation_session_init(
      &inherited_context, ag_compilation_session_target(&wasm)));
  ASSERT_EQ(4, ag_compilation_session_target(&inherited_context)
                   ->data_layout.pointer_size);
  ASSERT_TRUE(ag_compilation_session_dispose(&inherited_context));
  ASSERT_TRUE(psx_frontend_free_processed_ast_in_session(&host));
  ASSERT_EQ(0, arena_current_reserved_bytes_in(host.arena_context));
  ASSERT_TRUE(arena_current_reserved_bytes_in(wasm.arena_context) > 0);
  ASSERT_EQ(8, ag_compilation_session_target(&host)->data_layout.pointer_size);
  ASSERT_TRUE(tk_get_current_token_ctx(suite_tokenizer) ==
              suite_cursor_before_explicit_statement);
  cg_emitf_in(
      ag_compilation_session_codegen_emit_context(&host), "-host-b");
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
  ASSERT_TRUE(tk_get_current_token_ctx(suite_tokenizer) ==
              suite_cursor_before_explicit_statement);
  ASSERT_TRUE(strcmp(
      host_output.bytes, "host-a-host-explicit-host-b") == 0);
  ASSERT_TRUE(strcmp(wasm_output.bytes, "wasm-explicit-wasm") == 0);

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

  ASSERT_EQ(4, test_target_pointer_size(ag_compilation_session_target(&wasm)));
  ASSERT_EQ(8, ag_compilation_session_target(previous_session)->data_layout.pointer_size);
  ASSERT_TRUE(ag_compilation_session_dispose(&host));
  ASSERT_TRUE(ag_compilation_session_dispose(&wasm));
  ASSERT_EQ(1, host_backend.destroy_count);
  ASSERT_EQ(1, wasm_backend.destroy_count);
}

static void test_compilation_session_owns_preprocessor_roots() {
  printf("test_compilation_session_owns_preprocessor_roots...\n");
  char original_cwd[PATH_MAX];
  char first_root[] = "/tmp/ag_c_pp_root_first_XXXXXX";
  char second_root[] = "/tmp/ag_c_pp_root_second_XXXXXX";
  char first_include[PATH_MAX];
  char second_include[PATH_MAX];
  char first_resolved[PATH_MAX];
  char second_resolved[PATH_MAX];
  char first_include_resolved[PATH_MAX];
  char second_include_resolved[PATH_MAX];
  ASSERT_TRUE(getcwd(original_cwd, sizeof(original_cwd)) != NULL);
  ASSERT_TRUE(mkdtemp(first_root) != NULL);
  ASSERT_TRUE(mkdtemp(second_root) != NULL);
  snprintf(first_include, sizeof(first_include), "%s/include", first_root);
  snprintf(second_include, sizeof(second_include), "%s/include", second_root);
  ASSERT_TRUE(mkdir(first_include, 0700) == 0);
  ASSERT_TRUE(mkdir(second_include, 0700) == 0);
  ASSERT_TRUE(realpath(first_root, first_resolved) != NULL);
  ASSERT_TRUE(realpath(second_root, second_resolved) != NULL);
  ASSERT_TRUE(realpath(first_include, first_include_resolved) != NULL);
  ASSERT_TRUE(realpath(second_include, second_include_resolved) != NULL);

  ag_target_info_t target = ag_target_info_host();
  ag_compilation_session_t first = {0};
  ag_compilation_session_t second = {0};
  ASSERT_TRUE(chdir(first_root) == 0);
  ASSERT_TRUE(ag_compilation_session_init(&first, &target));
  ASSERT_TRUE(chdir(second_root) == 0);
  ASSERT_TRUE(ag_compilation_session_init(&second, &target));
  ASSERT_TRUE(strcmp(first_resolved,
                     pp_context_project_root(first.preprocessor_context)) ==
              0);
  ASSERT_TRUE(strcmp(second_resolved,
                     pp_context_project_root(second.preprocessor_context)) ==
              0);
  ASSERT_TRUE(strcmp(first_include_resolved,
                     pp_context_include_root(first.preprocessor_context)) ==
              0);
  ASSERT_TRUE(strcmp(second_include_resolved,
                     pp_context_include_root(second.preprocessor_context)) ==
              0);
  ASSERT_TRUE(strcmp(pp_context_project_root(first.preprocessor_context),
                     pp_context_project_root(second.preprocessor_context)) !=
              0);

  ASSERT_TRUE(chdir(original_cwd) == 0);
  ASSERT_TRUE(ag_compilation_session_dispose(&first));
  ASSERT_TRUE(ag_compilation_session_dispose(&second));
  ASSERT_TRUE(rmdir(first_include) == 0);
  ASSERT_TRUE(rmdir(second_include) == 0);
  ASSERT_TRUE(rmdir(first_root) == 0);
  ASSERT_TRUE(rmdir(second_root) == 0);
}

static void test_scope_graph_namespace_and_transaction_boundary(void) {
  printf("test_scope_graph_namespace_and_transaction_boundary...\n");
  psx_scope_graph_t *graph = psx_scope_graph_create();
  ASSERT_TRUE(graph != NULL);
  ASSERT_EQ(PSX_SCOPE_ID_TRANSLATION_UNIT,
            psx_scope_graph_current_scope(graph));
  ASSERT_EQ(0, psx_scope_graph_scope_depth(
                   graph, PSX_SCOPE_ID_TRANSLATION_UNIT));
  ASSERT_EQ(
      PSX_COMPOUND_LITERAL_STORAGE_STATIC,
      psx_compound_literal_storage_duration_in_scope_graph(
          graph, PSX_SCOPE_ID_TRANSLATION_UNIT, 0));
  ASSERT_EQ(
      PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC,
      psx_compound_literal_storage_duration_in_scope_graph(
          graph, PSX_SCOPE_ID_INVALID, 1));

  int global_payload = 1;
  int tag_payload = 2;
  psx_decl_id_t global_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_ORDINARY, PSX_DECL_GLOBAL_OBJECT,
      "same", 4, &global_payload);
  psx_decl_id_t tag_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_TAG, PSX_DECL_TAG,
      "same", 4, &tag_payload);
  ASSERT_TRUE(global_id != PSX_DECL_ID_INVALID);
  ASSERT_TRUE(tag_id != PSX_DECL_ID_INVALID);
  ASSERT_TRUE(global_id != tag_id);

  char borrowed_name[] = "borrowed";
  int borrowed_payload = 12;
  psx_decl_id_t borrowed_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_ORDINARY, PSX_DECL_GLOBAL_OBJECT,
      borrowed_name, 8, &borrowed_payload);
  ASSERT_TRUE(borrowed_id != PSX_DECL_ID_INVALID);
  const psx_scope_declaration_t *borrowed_declaration =
      psx_scope_graph_declaration(graph, borrowed_id);
  ASSERT_TRUE(borrowed_declaration != NULL);
  ASSERT_TRUE(borrowed_declaration->name != borrowed_name);
  borrowed_name[0] = 'x';
  ASSERT_EQ(borrowed_id, psx_scope_graph_lookup_in_scope(
      graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_ORDINARY, "borrowed", 8));

  psx_scope_lookup_point_t before_synthetic =
      psx_scope_graph_capture_lookup_point(graph);
  int synthetic_payload = 8;
  psx_decl_id_t synthetic_id = psx_scope_graph_declare_synthetic_at(
      graph, PSX_SCOPE_ID_TRANSLATION_UNIT, PSX_NAMESPACE_ORDINARY,
      PSX_DECL_GLOBAL_OBJECT, "__synthetic", 11, &synthetic_payload);
  psx_scope_lookup_point_t after_synthetic =
      psx_scope_graph_capture_lookup_point(graph);
  ASSERT_TRUE(synthetic_id != PSX_DECL_ID_INVALID);
  ASSERT_EQ(before_synthetic.declaration_order,
            after_synthetic.declaration_order);
  ASSERT_EQ(synthetic_id, psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_ORDINARY, "__synthetic", 11,
      after_synthetic));

  psx_scope_id_t prototype_scope = psx_scope_graph_enter_scope(
      graph, PSX_SCOPE_FUNCTION_PROTOTYPE);
  ASSERT_TRUE(prototype_scope != PSX_SCOPE_ID_INVALID);
  ASSERT_EQ(1, psx_scope_graph_scope_depth(graph, prototype_scope));
  ASSERT_EQ(
      PSX_COMPOUND_LITERAL_STORAGE_STATIC,
      psx_compound_literal_storage_duration_in_scope_graph(
          graph, prototype_scope, 0));
  int prototype_parameter_payload = 9;
  ASSERT_TRUE(psx_scope_graph_declare(
      graph, PSX_NAMESPACE_ORDINARY, PSX_DECL_LOCAL_OBJECT,
      "parameter", 9, &prototype_parameter_payload) !=
      PSX_DECL_ID_INVALID);
  ASSERT_TRUE(psx_scope_graph_leave_scope(graph));
  psx_scope_lookup_point_t after_prototype =
      psx_scope_graph_capture_lookup_point(graph);
  ASSERT_EQ(after_synthetic.declaration_order,
            after_prototype.declaration_order);

  psx_scope_id_t function_scope = psx_scope_graph_enter_scope(
      graph, PSX_SCOPE_FUNCTION);
  ASSERT_TRUE(function_scope != PSX_SCOPE_ID_INVALID);
  ASSERT_EQ(1, psx_scope_graph_scope_depth(graph, function_scope));
  ASSERT_EQ(
      PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC,
      psx_compound_literal_storage_duration_in_scope_graph(
          graph, function_scope, 0));
  int local_payload = 3;
  psx_decl_id_t local_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_ORDINARY, PSX_DECL_LOCAL_OBJECT,
      "same", 4, &local_payload);
  psx_scope_lookup_point_t function_point =
      psx_scope_graph_capture_lookup_point(graph);
  ASSERT_EQ(local_id, psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_ORDINARY, "same", 4, function_point));
  ASSERT_EQ(tag_id, psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_TAG, "same", 4, function_point));

  psx_scope_id_t block_scope = psx_scope_graph_enter_scope(
      graph, PSX_SCOPE_BLOCK);
  ASSERT_TRUE(block_scope != PSX_SCOPE_ID_INVALID);
  ASSERT_EQ(2, psx_scope_graph_scope_depth(graph, block_scope));
  ASSERT_EQ(
      PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC,
      psx_compound_literal_storage_duration_in_scope_graph(
          graph, block_scope, 0));
  ASSERT_EQ(function_scope, psx_scope_graph_nearest_scope_of_kind(
      graph, block_scope, PSX_SCOPE_FUNCTION));
  psx_scope_lookup_point_t before_inner =
      psx_scope_graph_capture_lookup_point(graph);
  int inner_payload = 4;
  psx_decl_id_t inner_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_ORDINARY, PSX_DECL_LOCAL_OBJECT,
      "same", 4, &inner_payload);
  psx_scope_lookup_point_t after_inner =
      psx_scope_graph_capture_lookup_point(graph);
  ASSERT_EQ(local_id, psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_ORDINARY, "same", 4, before_inner));
  ASSERT_EQ(inner_id, psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_ORDINARY, "same", 4, after_inner));

  int promoted_tag_payload = 10;
  psx_decl_id_t promoted_tag_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_TAG, PSX_DECL_TAG,
      "__promoted", 10, &promoted_tag_payload);
  ASSERT_TRUE(promoted_tag_id != PSX_DECL_ID_INVALID);
  ASSERT_TRUE(psx_scope_graph_rehome_declaration_at(
      graph, promoted_tag_id, PSX_SCOPE_ID_TRANSLATION_UNIT));

  psx_scope_lookup_point_t before_label =
      psx_scope_graph_capture_lookup_point(graph);
  int label_payload = 5;
  psx_decl_id_t label_id = psx_scope_graph_declare_synthetic_at(
      graph, function_scope, PSX_NAMESPACE_LABEL, PSX_DECL_LABEL,
      "same", 4, &label_payload);
  psx_scope_lookup_point_t after_label =
      psx_scope_graph_capture_lookup_point(graph);
  ASSERT_EQ(before_label.declaration_order,
            after_label.declaration_order);
  ASSERT_EQ(label_id, psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_LABEL, "same", 4, after_inner));

  psx_scope_graph_checkpoint_t checkpoint = {0};
  ASSERT_TRUE(psx_scope_graph_checkpoint_begin(graph, &checkpoint));
  int rolled_back_payload = 6;
  psx_decl_id_t rolled_back_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_ORDINARY, PSX_DECL_LOCAL_OBJECT,
      "temporary", 9, &rolled_back_payload);
  ASSERT_TRUE(rolled_back_id != PSX_DECL_ID_INVALID);
  psx_scope_graph_checkpoint_rollback(graph, &checkpoint);
  ASSERT_EQ(PSX_DECL_ID_INVALID, psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_ORDINARY, "temporary", 9,
      psx_scope_graph_capture_lookup_point(graph)));

  ASSERT_TRUE(psx_scope_graph_leave_scope(graph));
  ASSERT_TRUE(psx_scope_graph_leave_scope(graph));
  ASSERT_EQ(promoted_tag_id, psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_TAG, "__promoted", 10,
      psx_scope_graph_capture_lookup_point(graph)));
  psx_scope_id_t record_scope = psx_scope_graph_enter_scope(
      graph, PSX_SCOPE_RECORD);
  ASSERT_TRUE(record_scope != PSX_SCOPE_ID_INVALID);
  ASSERT_EQ(1, psx_scope_graph_scope_depth(graph, record_scope));
  int member_payload = 7;
  psx_decl_id_t member_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_MEMBER, PSX_DECL_MEMBER,
      "same", 4, &member_payload);
  ASSERT_EQ(member_id, psx_scope_graph_lookup_in_scope(
      graph, record_scope, PSX_NAMESPACE_MEMBER, "same", 4));
  int unnamed_member_payload = 11;
  psx_decl_id_t unnamed_member_id = psx_scope_graph_declare(
      graph, PSX_NAMESPACE_MEMBER, PSX_DECL_MEMBER,
      NULL, 0, &unnamed_member_payload);
  ASSERT_TRUE(unnamed_member_id != PSX_DECL_ID_INVALID);
  const psx_scope_declaration_t *unnamed_member =
      psx_scope_graph_declaration(graph, unnamed_member_id);
  ASSERT_TRUE(unnamed_member != NULL);
  ASSERT_EQ(record_scope, unnamed_member->scope_id);
  ASSERT_EQ(PSX_NAMESPACE_MEMBER, unnamed_member->name_space);
  ASSERT_EQ(PSX_DECL_MEMBER, unnamed_member->kind);
  ASSERT_TRUE(unnamed_member->name == NULL);
  ASSERT_EQ(0, unnamed_member->name_len);
  ASSERT_EQ(PSX_DECL_ID_INVALID, psx_scope_graph_declare_at(
      graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_MEMBER, PSX_DECL_MEMBER,
      NULL, 0, &unnamed_member_payload));
  ASSERT_EQ(global_id, psx_scope_graph_lookup_in_scope(
      graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
      PSX_NAMESPACE_ORDINARY, "same", 4));
  const psx_scope_declaration_t *global_declaration =
      psx_scope_graph_lookup_declaration_in_scope(
          graph, PSX_SCOPE_ID_TRANSLATION_UNIT,
          PSX_NAMESPACE_ORDINARY, "same", 4);
  ASSERT_TRUE(global_declaration != NULL);
  ASSERT_EQ(global_id, global_declaration->id);
  ASSERT_EQ(PSX_DECL_GLOBAL_OBJECT, global_declaration->kind);
  psx_scope_graph_destroy(graph);
}

int main() {
  ag_compilation_session_t suite_session;
  ag_target_info_t suite_target = ag_target_info_host();
  ASSERT_TRUE(ag_compilation_session_init(&suite_session, &suite_target));
  ag_compilation_session_t *test_suite_session = &suite_session;
  printf("Running tests for Parser...\n");

  test_scope_graph_namespace_and_transaction_boundary();
  test_arena_checkpoint_rollback(test_suite_session);
  test_parser_name_environment_boundary(test_suite_session);
  test_parser_name_classifier_boundary(test_suite_session);
  test_typed_hir_ownership_and_type_boundary(test_suite_session);
  test_typed_hir_local_lowering_without_ast(test_suite_session);
  test_typed_hir_overaligned_local_without_ast(test_suite_session);
  test_typed_hir_parameter_lowering_without_ast(test_suite_session);
  test_typed_hir_pointer_lowering_without_ast(test_suite_session);
  test_typed_hir_post_inc_lowering_without_ast(test_suite_session);
  test_typed_hir_vla_parameter_metadata_without_ast(test_suite_session);
  test_typed_hir_vla_allocation_without_ast(test_suite_session);
  test_typed_hir_direct_call_lowering_without_ast(test_suite_session);
  test_typed_hir_variadic_aggregate_call_without_ast(test_suite_session);
  test_typed_hir_atomic_lowering_without_ast(test_suite_session);
  test_typed_hir_va_arg_area_lowering_without_ast(test_suite_session);
  test_typed_hir_register_aggregate_lowering_without_ast(test_suite_session);
  test_typed_hir_aggregate_parameter_lowering_without_ast(test_suite_session);
  test_typed_hir_indirect_aggregate_return_without_ast(test_suite_session);
  test_typed_hir_odd_sized_aggregate_abi_without_ast(test_suite_session);
  test_typed_hir_aggregate_ternary_without_ast(test_suite_session);
  test_typed_hir_aggregate_member_storage_without_ast(test_suite_session);
  test_typed_hir_floating_lowering_without_ast(test_suite_session);
  test_typed_hir_float_inc_dec_without_ast(test_suite_session);
  test_typed_hir_structural_sequence_types_without_ast(test_suite_session);
  test_typed_hir_complex_copy_comparison_without_ast(test_suite_session);
  test_typed_hir_complex_width_conversion_without_ast(test_suite_session);
  test_typed_hir_indirect_call_lowering_without_ast(test_suite_session);
  test_typed_hir_unprototyped_indirect_call_without_ast(test_suite_session);
  test_typed_hir_void_function_lowering_without_ast(test_suite_session);
  test_typed_hir_symbol_reference_lowering_without_ast(test_suite_session);
  test_typed_hir_global_symbol_lowering_without_ast(test_suite_session);
  test_typed_hir_bitfield_lowering_without_ast(test_suite_session);
  test_typed_hir_conditional_expr_lowering_without_ast(test_suite_session);
  test_typed_hir_control_flow_lowering_without_ast(test_suite_session);
  test_semantic_type_identity(test_suite_session);
  test_semantic_context_isolation(test_suite_session);
#if defined(DIAG_LANG_ALL)
  test_diagnostic_catalog_localization(test_suite_session);
#endif
  test_compilation_session_owns_target_and_tokenizer(test_suite_session);
  test_compilation_session_owns_preprocessor_roots();
  test_compilation_session_registry_isolation(test_suite_session);
  test_syntax_literal_type_boundary(test_suite_session);
  test_direct_literal_typed_hir_resolution_boundary(test_suite_session);
  test_predefined_function_name_typed_hir_boundary(test_suite_session);
  test_builtin_expect_typed_hir_boundary(test_suite_session);
  test_direct_statement_typed_hir_resolution_boundary(test_suite_session);
  test_typed_hir_build_failure_first_cause_boundary(test_suite_session);
  test_expr_number(test_suite_session);
  test_expr_add_sub(test_suite_session);
  test_additive_typed_hir_boundary(test_suite_session);
  test_subscript_typed_hir_boundary(test_suite_session);
  test_unary_deref_typed_hir_boundary(test_suite_session);
  test_unary_operator_typed_hir_boundary(test_suite_session);
  test_generic_selection_typed_hir_boundary(test_suite_session);
  test_sizeof_typed_hir_boundary(test_suite_session);
  test_expression_typed_hir_type_boundary(test_suite_session);
  test_function_call_typed_hir_boundary(test_suite_session);
  test_function_prototype_type_identity_boundary(test_suite_session);
  test_cast_typed_hir_boundary(test_suite_session);
  test_aggregate_cast_semantic_lowering_boundary(test_suite_session);
  test_implicit_conversion_hir_boundary(test_suite_session);
  test_compound_assignment_typed_hir_boundary(test_suite_session);
  test_translation_unit_frontend_boundary(test_suite_session);
  test_function_definition_header_resolution_boundary(test_suite_session);
  test_global_registry_checkpoint_boundary(test_suite_session);
  test_direct_function_typed_hir_resolution_boundary(test_suite_session);
  test_direct_const_aggregate_initializer_boundary(test_suite_session);
  test_direct_string_pointer_initializer_boundary(test_suite_session);
  test_case_label_syntax_hir_boundary(test_suite_session);
  test_toplevel_static_assert_frontend_boundary(test_suite_session);
  test_block_static_assert_syntax_hir_boundary(test_suite_session);
  test_toplevel_declaration_frontend_boundary(test_suite_session);
  test_parser_owned_typedef_classifier_boundary(test_suite_session);
  test_toplevel_callback_context_boundary(test_suite_session);
  test_toplevel_compound_initializer_frontend_boundary(test_suite_session);
  test_toplevel_point_of_declaration_boundary(test_suite_session);
  test_toplevel_single_parse_classification_boundary(test_suite_session);
  test_frontend_stream_lifecycle_boundary(test_suite_session);
  test_local_declaration_frontend_boundary(test_suite_session);
  test_function_parameter_point_of_declaration_boundary(test_suite_session);
  test_identifier_resolution_boundary(test_suite_session);
  test_persistent_local_scope_lookup_boundary(test_suite_session);
  test_member_access_typed_hir_boundary(test_suite_session);
  test_complex_initializer_semantic_lowering_boundary(test_suite_session);
  test_local_declaration_storage_plan_boundary(test_suite_session);
  test_target_type_layout_boundary(test_suite_session);
  test_dynamic_function_parameter_capacity(test_suite_session);
  test_dynamic_abi_piece_capacity(test_suite_session);
  test_wasm_target_global_pointer_data_layout();
  test_vla_lowering_request_boundary(test_suite_session);
  test_parameter_declaration_storage_plan_boundary(test_suite_session);
  test_global_declaration_resolution_boundary(test_suite_session);
  test_declaration_pipeline_order_boundary(test_suite_session);
  test_tag_declaration_resolution_boundary(test_suite_session);
  test_record_decl_ownership_boundary(test_suite_session);
  test_aggregate_body_phase_boundary(test_suite_session);
  test_declaration_phase_boundary(test_suite_session);
  test_type_name_phase_boundary(test_suite_session);
  test_toplevel_declarator_phase_boundary(test_suite_session);
  test_local_declarator_application_boundary(test_suite_session);
  test_local_declaration_resolution_boundary(test_suite_session);
  test_aggregate_member_resolution_boundary(test_suite_session);
  test_static_assert_resolution_boundary();
  test_typedef_declaration_resolution_boundary(test_suite_session);
  test_enum_constant_resolution_boundary(test_suite_session);
  test_initializer_resolution_boundary(test_suite_session);
  test_local_initializer_parse_lowering_boundary(test_suite_session);
  test_static_data_initializer_boundary(test_suite_session);
  test_expr_mul_div(test_suite_session);
  test_expr_mod(test_suite_session);
  test_expr_precedence(test_suite_session);
  test_expr_parentheses(test_suite_session);
  test_expr_eq_neq(test_suite_session);
  test_expr_relational(test_suite_session);
  test_expr_bitwise(test_suite_session);
  test_expr_shift(test_suite_session);
  test_expr_logical_and_or(test_suite_session);
  test_expr_ternary(test_suite_session);
  test_expr_unary_ops(test_suite_session);
  test_expr_generic(test_suite_session);
  test_expr_sizeof(test_suite_session);
  test_expr_inc_dec_typed_hir_boundary(test_suite_session);
  test_expr_assign(test_suite_session);
  test_expr_compound_assign(test_suite_session);
  test_expr_comma(test_suite_session);
  test_program_funcdef(test_suite_session);
  test_funcall(test_suite_session);
  test_funcdef_with_params(test_suite_session);
  test_stmt_return(test_suite_session);
  test_expr_deref_address_typed_hir_boundary(test_suite_session);
  test_expr_member_access(test_suite_session);
  test_expr_string(test_suite_session);
  test_expr_concat_string(test_suite_session);
  test_expr_float(test_suite_session);
  test_expr_long_double_suffix_metadata(test_suite_session);
  test_expr_compound_literal_typed_hir_boundary(test_suite_session);
  test_expr_compound_literal_array_subscript(test_suite_session);
  test_type_decl(test_suite_session);
  test_bool_assignment_typed_hir_boundary(test_suite_session);
  test_type_metadata_bridge(test_suite_session);
  test_recursive_declarator_capacity_boundary(test_suite_session);
  test_translation_unit_reset_static_local_state(test_suite_session);
  test_translation_unit_reset_anonymous_tag_state(test_suite_session);
  test_translation_unit_reset_decl_locals_state(test_suite_session);
  test_translation_unit_reset_pragma_pack_state(test_suite_session);
  test_multiple_funcdefs(test_suite_session);
  test_parse_invalid(test_suite_session);
  test_parse_invalid_diagnostics(test_suite_session);
  test_parse_evil_edge_cases(test_suite_session);
  test_parser_config_matrix(test_suite_session);
  test_expr_nest_limits(test_suite_session);
  test_parser_width_limits(test_suite_session);
  test_semantic_canonical_type_invariant(test_suite_session);

  ASSERT_TRUE(ag_compilation_session_dispose(&suite_session));
  printf("OK: All unit tests passed!\n");
  return 0;
}

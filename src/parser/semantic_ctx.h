#ifndef PARSER_SEMANTIC_CTX_H
#define PARSER_SEMANTIC_CTX_H

#include "core.h"
#include "function_public.h"
#include "local_registry.h"
#include "name_classifier.h"
#include "../semantic/expression_identity.h"
#include "../semantic/declarator_application_types.h"
#include "../semantic/record_decl_table.h"
#include "../semantic/record_layout.h"
#include "../semantic/type_identity.h"
#include "../tokenizer/token.h"
#include <stdbool.h>

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct arena_context_t arena_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;
typedef struct ag_target_info_t ag_target_info_t;
typedef struct ag_data_layout_t ag_data_layout_t;
typedef struct node_t node_t;
typedef struct psx_function_registration_checkpoint_t
    psx_function_registration_checkpoint_t;

/* All dependencies must outlive the semantic context. */
psx_semantic_context_t *ps_ctx_create(
    arena_context_t *arena_context,
    ag_diagnostic_context_t *diagnostic_context,
    psx_scope_graph_t *scope_graph,
    const ag_target_info_t *target);
void ps_ctx_destroy(psx_semantic_context_t *context);
arena_context_t *ps_ctx_arena(
    const psx_semantic_context_t *context);
psx_scope_graph_t *ps_ctx_scope_graph(
    const psx_semantic_context_t *context);
ag_diagnostic_context_t *ps_ctx_diagnostics(
    const psx_semantic_context_t *context);
const ag_target_info_t *ps_ctx_target_info(
    const psx_semantic_context_t *context);
const ag_data_layout_t *
ps_ctx_data_layout(const psx_semantic_context_t *context);
psx_semantic_expr_id_t ps_ctx_register_semantic_expression_in(
    psx_semantic_context_t *context,
    const psx_typed_hir_tree_t *expression);
const psx_typed_hir_tree_t *ps_ctx_semantic_expression_in(
    const psx_semantic_context_t *context,
    psx_semantic_expr_id_t expression_id);
const psx_semantic_expression_table_t *
ps_ctx_semantic_expression_table_in(
    const psx_semantic_context_t *context);
psx_qual_type_t ps_ctx_intern_integer_qual_type_in(
    psx_semantic_context_t *context,
    psx_integer_kind_t integer_kind, int is_unsigned,
    int is_plain_char);
psx_qual_type_t ps_ctx_intern_floating_qual_type_in(
    psx_semantic_context_t *context,
    psx_floating_kind_t floating_kind, int is_complex);
psx_qual_type_t ps_ctx_intern_void_qual_type_in(
    psx_semantic_context_t *context);
psx_qual_type_t ps_ctx_intern_enum_qual_type_in(
    psx_semantic_context_t *context, psx_decl_id_t declaration_id);
psx_qual_type_t ps_ctx_intern_record_qual_type_in(
    psx_semantic_context_t *context, psx_record_id_t record_id);
psx_qual_type_t ps_ctx_intern_pointer_to_qual_type_in(
    psx_semantic_context_t *context, psx_qual_type_t pointee);
psx_qual_type_t ps_ctx_intern_array_of_qual_type_in(
    psx_semantic_context_t *context, psx_qual_type_t element,
    int array_len, int is_vla);
psx_qual_type_t ps_ctx_intern_function_qual_type_in(
    psx_semantic_context_t *context, psx_qual_type_t result,
    const psx_qual_type_t *parameters, int parameter_count,
    int has_prototype, int is_variadic);
psx_qual_type_t ps_ctx_intern_implicit_function_qual_type_in(
    psx_semantic_context_t *context);
const psx_semantic_type_table_t *ps_ctx_semantic_type_table_in(
    const psx_semantic_context_t *context);
const psx_record_decl_table_t *ps_ctx_record_decl_table_in(
    const psx_semantic_context_t *context);
const psx_record_layout_table_t *ps_ctx_record_layout_table_in(
    const psx_semantic_context_t *context);
int ps_ctx_publish_record_layout_in(
    psx_semantic_context_t *context, psx_record_id_t record_id,
    int size, int alignment);

/* Explicit-context lifecycle and deferred diagnostic operations. */
void ps_ctx_reset_translation_unit_scope_in(
    psx_semantic_context_t *context);
void ps_ctx_reset_function_diag_state_in(
    psx_semantic_context_t *context);
void ps_ctx_reset_tag_diag_state_in(
    psx_semantic_context_t *context);
void ps_ctx_record_unsupported_gnu_extension_in(
    psx_semantic_context_t *context,
    const token_t *tok, const char *name);
void ps_ctx_emit_deferred_parser_diagnostics_in(
    psx_semantic_context_t *context);

/* Explicit-context function symbol operations. */
void ps_ctx_reset_function_names_in(psx_semantic_context_t *context);
const psx_function_symbol_t *ps_ctx_find_function_symbol_in(
    psx_semantic_context_t *context, char *name, int len);
int ps_ctx_track_function_defined_in(
    psx_semantic_context_t *context, char *name, int len);
const psx_function_symbol_t *ps_ctx_register_function_qual_type_in(
    psx_semantic_context_t *context, char *name, int len,
    psx_qual_type_t function_type);
int ps_ctx_mark_function_internal_linkage_in(
    psx_semantic_context_t *context, char *name, int len);
void ps_ctx_checkpoint_function_registration_in(
    psx_semantic_context_t *context, char *name, int len,
    psx_function_registration_checkpoint_t *checkpoint);
void ps_ctx_rollback_function_registration_in(
    psx_semantic_context_t *context, char *name, int len,
    const psx_function_registration_checkpoint_t *checkpoint);
void psx_ctx_define_function_name_in(
    psx_semantic_context_t *context, char *name, int len);
int ps_ctx_is_function_defined_in(
    psx_semantic_context_t *context, char *name, int len);
psx_qual_type_t psx_ctx_get_function_return_qual_type_in(
    psx_semantic_context_t *context, char *name, int len);
psx_qual_type_t ps_ctx_get_function_qual_type_in(
    psx_semantic_context_t *context, char *name, int len);
int ps_ctx_format_function_signature_in(
    psx_semantic_context_t *context, char *name, int len,
    char *out, size_t out_size);

/* Explicit-context tag and aggregate-layout operations. */
bool ps_ctx_has_tag_type_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
psx_qual_type_t ps_ctx_tag_qual_type_at_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    psx_scope_lookup_point_t point);
int ps_ctx_register_tag_type_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    int is_complete, int member_count);
int ps_ctx_current_tag_scope_depth_in(psx_semantic_context_t *context);
int ps_ctx_find_tag_kind_at_current_scope_in(
    psx_semantic_context_t *context,
    char *name, int len, token_kind_t *out_kind);
const psx_record_decl_t *ps_ctx_ensure_tag_record_decl_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
int ps_ctx_mark_tag_record_anonymous_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
psx_record_id_t ps_ctx_resolve_tag_record_id_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
const psx_record_decl_t *ps_ctx_get_record_decl_in(
    psx_semantic_context_t *context, psx_record_id_t record_id);
int ps_ctx_get_tag_size_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
int ps_ctx_get_tag_align_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
int ps_ctx_register_record_members_in(
    psx_semantic_context_t *context, psx_record_id_t record_id,
    const psx_record_member_decl_t *declarations,
    const psx_record_member_layout_t *layouts, int member_count,
    int *out_conflict_index);
bool ps_ctx_find_record_member_in(
    psx_semantic_context_t *context, psx_record_id_t record_id,
    const char *member_name, int member_len, int *out_member_index,
    psx_record_member_decl_t *out_declaration);
int ps_ctx_get_tag_scope_depth_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
void ps_ctx_promote_tag_to_file_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);

/* Explicit-context ordinary namespace operations. */
int ps_ctx_register_enum_const_in(
    psx_semantic_context_t *context,
    char *name, int len, long long value, int *out_created);
bool ps_ctx_find_enum_const_in(
    psx_semantic_context_t *context,
    char *name, int len, long long *out_value);
bool ps_ctx_find_enum_const_at_in(
    psx_semantic_context_t *context,
    char *name, int len, psx_scope_lookup_point_t point,
    long long *out_value);
bool ps_ctx_enum_const_value_by_declaration_id_in(
    psx_semantic_context_t *context, psx_decl_id_t declaration_id,
    long long *out_value);

typedef struct {
  const psx_semantic_type_table_t *decl_type_table;
  psx_qual_type_t decl_qual_type;
  const psx_runtime_declarator_application_t *runtime_application;
} psx_typedef_info_t;

static inline psx_qual_type_t ps_ctx_typedef_decl_qual_type(
    const psx_typedef_info_t *info) {
  return info ? info->decl_qual_type
              : (psx_qual_type_t){PSX_TYPE_ID_INVALID,
                                  PSX_TYPE_QUALIFIER_NONE};
}

/* typedef 名を登録する。decl_type_table + decl_qual_type が正本。
 * 戻り値 1 = 成功 (新規 or 互換な再宣言)、0 = 型欠落または型衝突。 */
int ps_ctx_register_typedef_name_in(
    psx_semantic_context_t *context,
    char *name, int len, const psx_typedef_info_t *info,
    int *out_created, int *out_redeclared);
/* typedef 名を引く。見つかれば true を返し *out に記述子を書く。
 * out が NULL のときは存在判定のみ (記述子は書かない)。 */
bool ps_ctx_find_typedef_name_in(
    psx_semantic_context_t *context,
    char *name, int len, psx_typedef_info_t *out);
bool ps_ctx_find_typedef_name_at_in(
    psx_semantic_context_t *context,
    char *name, int len, psx_scope_lookup_point_t point,
    psx_typedef_info_t *out);
bool psx_ctx_find_typedef_layout_in(
    psx_semantic_context_t *context,
    char *name, int len, int *out_size, int *out_alignment);
bool psx_ctx_is_typedef_name_token_in(
    psx_semantic_context_t *context, token_t *tok);
psx_name_classifier_t ps_ctx_name_classifier(
    psx_semantic_context_t *context);
struct psx_function_registration_checkpoint_t {
  psx_qual_type_t function_qual_type;
  int existed;
  int is_defined;
  int has_internal_linkage;
};
bool psx_ctx_is_type_token(token_kind_t kind);
bool psx_ctx_is_tag_keyword(token_kind_t kind);
bool ps_ctx_is_tag_aggregate_kind(token_kind_t kind);
const char *ps_ctx_tag_kind_spelling(token_kind_t kind);
bool psx_ctx_get_type_token_layout_in(
    const psx_semantic_context_t *context, token_kind_t kind,
    int *out_size, int *out_alignment);

#endif

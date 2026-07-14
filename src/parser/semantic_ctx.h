#ifndef PARSER_SEMANTIC_CTX_H
#define PARSER_SEMANTIC_CTX_H

#include "core.h"
#include "function_public.h"
#include "local_registry.h"
#include "tag_member_public.h"
#include "type.h"
#include "../tokenizer/token.h"
#include <stdbool.h>

typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_function_registration_checkpoint_t
    psx_function_registration_checkpoint_t;

psx_semantic_context_t *ps_ctx_create(void);
void ps_ctx_destroy(psx_semantic_context_t *context);
psx_semantic_context_t *ps_ctx_activate(psx_semantic_context_t *context);
psx_semantic_context_t *ps_ctx_active(void);

/* Explicit-context lifecycle and deferred diagnostic operations. */
void ps_ctx_reset_translation_unit_scope_in(
    psx_semantic_context_t *context);
void ps_ctx_reset_function_diag_state_in(
    psx_semantic_context_t *context);
void ps_ctx_reset_tag_diag_state_in(
    psx_semantic_context_t *context);
void ps_ctx_reset_function_scope_in(
    psx_semantic_context_t *context);
void ps_ctx_enter_block_scope_in(
    psx_semantic_context_t *context);
void ps_ctx_leave_block_scope_in(
    psx_semantic_context_t *context);
void ps_ctx_record_unsupported_gnu_extension_warning_in(
    psx_semantic_context_t *context,
    const token_t *tok, const char *name);
void ps_ctx_emit_deferred_parser_warnings_in(
    psx_semantic_context_t *context);
void psx_ctx_register_goto_ref_in(
    psx_semantic_context_t *context,
    char *name, int len, token_t *tok);
void psx_ctx_register_label_def_in(
    psx_semantic_context_t *context,
    char *name, int len, token_t *tok);
void psx_ctx_validate_goto_refs_in(
    psx_semantic_context_t *context);

/* Explicit-context function symbol operations. New phase code should use
 * these APIs; the context-free variants remain as parser compatibility. */
void ps_ctx_reset_function_names_in(psx_semantic_context_t *context);
const psx_function_symbol_t *ps_ctx_find_function_symbol_in(
    psx_semantic_context_t *context, char *name, int len);
bool ps_ctx_has_function_name_in(
    psx_semantic_context_t *context, char *name, int len);
int ps_ctx_track_function_defined_in(
    psx_semantic_context_t *context, char *name, int len);
const psx_function_symbol_t *ps_ctx_register_function_type_in(
    psx_semantic_context_t *context, char *name, int len,
    const psx_type_t *function_type);
void ps_ctx_checkpoint_function_registration_in(
    psx_semantic_context_t *context, char *name, int len,
    psx_function_registration_checkpoint_t *checkpoint);
void ps_ctx_rollback_function_registration_in(
    psx_semantic_context_t *context, char *name, int len,
    const psx_function_registration_checkpoint_t *checkpoint);

/* Explicit-context tag and aggregate-layout operations. */
bool ps_ctx_has_tag_type_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
psx_type_t *ps_ctx_clone_tag_type_at_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    psx_local_lookup_point_t point);
int ps_ctx_register_tag_type_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    int is_complete, int member_count, int tag_size, int tag_align);
int ps_ctx_current_tag_scope_depth_in(psx_semantic_context_t *context);
int ps_ctx_find_tag_kind_at_current_scope_in(
    psx_semantic_context_t *context,
    char *name, int len, token_kind_t *out_kind);
const psx_aggregate_definition_t *ps_ctx_get_tag_definition_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
int ps_ctx_get_tag_size_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
int ps_ctx_get_tag_align_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
int ps_ctx_register_tag_members_in(
    psx_semantic_context_t *context,
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const tag_member_info_t *members, int member_count,
    int *out_conflict_index);
void ps_ctx_attach_aggregate_definitions_in(
    psx_semantic_context_t *context, psx_type_t *type);
void ps_ctx_refresh_type_completeness_in(
    psx_semantic_context_t *context, psx_type_t *type);
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
    char *name, int len, psx_local_lookup_point_t point,
    long long *out_value);
int ps_ctx_has_enum_const_in_current_scope_in(
    psx_semantic_context_t *context, char *name, int len);

void ps_ctx_reset_function_scope(void);
void ps_ctx_reset_translation_unit_scope(void);
void ps_ctx_reset_function_names(void);
/* 各 parse 開始時に呼ぶソフトリセット: 関数情報は残し、is_defined のみクリア。 */
void ps_ctx_reset_function_diag_state(void);
/* タグの完全型定義状態をソフトリセット (member_count を 0 に戻す)。 */
void ps_ctx_reset_tag_diag_state(void);
void ps_ctx_enter_block_scope(void);
void ps_ctx_leave_block_scope(void);
void psx_ctx_register_goto_ref(char *name, int len, token_t *tok);
void psx_ctx_register_label_def(char *name, int len, token_t *tok);
void psx_ctx_validate_goto_refs(void);
void ps_ctx_record_unsupported_gnu_extension_warning(const token_t *tok, const char *name);
void ps_ctx_emit_deferred_parser_warnings(void);

bool ps_ctx_has_tag_type(token_kind_t kind, char *name, int len);
psx_type_t *ps_ctx_clone_tag_type_at(
    token_kind_t kind, char *name, int len,
    psx_local_lookup_point_t point);
void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count);
void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len,
                                         int member_count, int tag_size, int tag_align);
int ps_ctx_register_tag_type(token_kind_t kind, char *name, int len,
                              int is_complete, int member_count,
                              int tag_size, int tag_align);
int ps_ctx_current_tag_scope_depth(void);
int ps_ctx_find_tag_kind_at_current_scope(
    char *name, int len, token_kind_t *out_kind);
int ps_ctx_get_tag_size(token_kind_t kind, char *name, int len);
void ps_ctx_refresh_type_completeness(psx_type_t *type);
int ps_ctx_get_tag_align(token_kind_t kind, char *name, int len);
const psx_aggregate_definition_t *ps_ctx_get_tag_definition(
    token_kind_t kind, char *name, int len);
void ps_ctx_attach_aggregate_definitions(psx_type_t *type);
/* 現在見えている tag とそのメンバを file scope に昇格する。関数内 static aggregate を
 * global lowering した後も codegen が匿名タグのレイアウトを参照できるようにする。 */
void ps_ctx_promote_tag_to_file_scope(token_kind_t kind, char *name, int len);
/* canonical member descriptorを現在のtag scopeへ新規登録する。
 * 名前付きmemberの同一scope重複は0を返し、既存descriptorを変更しない。
 * 無名aggregate placeholder (m->len == 0) は複数登録できる。 */
int psx_ctx_register_tag_member(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const tag_member_info_t *m, int *out_created);
/* canonical member descriptor群を原子的に登録する。全descriptorと重複を
 * 事前検査し、失敗時は1件も登録しない。 */
int ps_ctx_register_tag_members(
    token_kind_t tag_kind, char *tag_name, int tag_len,
    const tag_member_info_t *members, int member_count,
    int *out_conflict_index);
/* enum 定数を登録する。重複なら 0、新規なら 1 を返す。
 * 呼び出し元で 0 のとき診断を出す。 */
int psx_ctx_define_enum_const(char *name, int len, long long value);
int ps_ctx_register_enum_const(
    char *name, int len, long long value, int *out_created);
bool ps_ctx_find_enum_const(char *name, int len, long long *out_value);
bool ps_ctx_find_enum_const_at(
    char *name, int len, psx_local_lookup_point_t point,
    long long *out_value);
int ps_ctx_has_enum_const_in_current_scope(char *name, int len);
typedef struct {
  const psx_type_t *decl_type;
} psx_typedef_info_t;

static inline const psx_type_t *ps_ctx_typedef_decl_type(
    const psx_typedef_info_t *info) {
  return info ? info->decl_type : NULL;
}

/* typedef 名を登録する。info->decl_type は正本として必須。
 * 戻り値 1 = 成功 (新規 or 互換な再宣言)、0 = decl_type欠落または型衝突。 */
int psx_ctx_define_typedef_name(char *name, int len, const psx_typedef_info_t *info);
int ps_ctx_register_typedef_name_in(
    psx_semantic_context_t *context,
    char *name, int len, const psx_typedef_info_t *info,
    int *out_created, int *out_redeclared);
int ps_ctx_register_typedef_name(
    char *name, int len, const psx_typedef_info_t *info,
    int *out_created, int *out_redeclared);
/* typedef 名を引く。見つかれば true を返し *out に記述子を書く。
 * out が NULL のときは存在判定のみ (記述子は書かない)。 */
bool ps_ctx_find_typedef_name(char *name, int len, psx_typedef_info_t *out);
bool ps_ctx_find_typedef_name_in(
    psx_semantic_context_t *context,
    char *name, int len, psx_typedef_info_t *out);
bool ps_ctx_find_typedef_decl_type(
    char *name, int len, const psx_type_t **out_type);
bool ps_ctx_find_typedef_decl_type_in(
    psx_semantic_context_t *context,
    char *name, int len, const psx_type_t **out_type);
bool ps_ctx_find_typedef_decl_type_at(
    char *name, int len, psx_local_lookup_point_t point,
    const psx_type_t **out_type);
bool ps_ctx_find_typedef_decl_type_at_in(
    psx_semantic_context_t *context,
    char *name, int len, psx_local_lookup_point_t point,
    const psx_type_t **out_type);
int ps_ctx_has_typedef_in_current_scope(char *name, int len);
int ps_ctx_has_typedef_in_current_scope_in(
    psx_semantic_context_t *context, char *name, int len);
bool psx_ctx_find_typedef_sizeof(char *name, int len, int *out_sizeof_size);
bool psx_ctx_find_typedef_sizeof_in(
    psx_semantic_context_t *context,
    char *name, int len, int *out_sizeof_size);
bool psx_ctx_is_typedef_name_token(token_t *tok);
bool psx_ctx_is_typedef_name_token_in(
    psx_semantic_context_t *context, token_t *tok);
struct psx_function_registration_checkpoint_t {
  const psx_type_t *function_type;
  int existed;
  int is_defined;
};
void ps_ctx_checkpoint_function_registration(
    char *name, int len, psx_function_registration_checkpoint_t *checkpoint);
void ps_ctx_rollback_function_registration(
    char *name, int len,
    const psx_function_registration_checkpoint_t *checkpoint);
void psx_ctx_define_function_name(char *name, int len);
void psx_ctx_define_function_name_with_ret(char *name, int len, int ret_struct_size);
// 関数が variadic (`...` を持つ) かどうかと固定引数の個数を保持する。
// Apple ARM64 ABI で variadic 引数を stack に積むため、呼び出し側 codegen が
// `nargs_fixed` を境に register / stack を切り替えるのに使う。
/* 同名関数の本体定義が初回かどうかを track する (C11 6.9p3)。
 * 初回なら 1 を返して定義済みフラグを立てる、すでに定義済みなら 0。 */
int ps_ctx_track_function_defined(char *name, int len);
const psx_type_t *psx_ctx_get_function_ret_type(char *name, int len);
/* 完全な canonical 関数型を初回登録し、再宣言時は同じ型か照合する。 */
const psx_function_symbol_t *ps_ctx_register_function_type(
    char *name, int len, const psx_type_t *function_type);
int psx_ctx_track_function_type(char *name, int len,
                                const psx_type_t *function_type);
const psx_type_t *ps_ctx_get_function_type(char *name, int len);

bool psx_ctx_is_type_token(token_kind_t kind);
bool psx_ctx_is_tag_keyword(token_kind_t kind);
bool ps_ctx_is_tag_aggregate_kind(token_kind_t kind);
const char *ps_ctx_tag_kind_spelling(token_kind_t kind);
void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size);

#endif

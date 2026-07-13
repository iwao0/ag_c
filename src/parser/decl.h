#ifndef PARSER_DECL_H
#define PARSER_DECL_H

/* decl.h は AST node 型 (node_t) と シンボルテーブル (global_var_t) の
 * 両方を使う。Phase C1-2: 両ヘッダを明示的に include する。 */
#include "ast.h"
#include "core.h"
#include "lvar_public.h"
#include "symtab.h"

typedef struct {
  char *name;
  int name_len;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int type_size;
  int init_count;
  int has_init;
  long long init_val;
  char *init_symbol;
  int init_symbol_len;
  long long init_symbol_offset;
  double fval;
  tk_float_kind_t fp_kind;
  int is_extern_decl;
  int is_static;
  int is_thread_local;
  int is_tag_pointer;
  int has_init_fvalues;
} psx_gvar_view_t;

psx_gvar_view_t ps_gvar_view(const global_var_t *gv);

typedef enum {
  PSX_LVAR_USAGE_EVALUATED,
  PSX_LVAR_USAGE_UNEVALUATED,
  PSX_LVAR_USAGE_ADDRESS_TAKEN,
  PSX_LVAR_USAGE_INITIALIZED,
} psx_lvar_usage_kind_t;

/* lvar_t / global_var_t の tag 4 フィールド (kind/name/len/is_tag_pointer)
 * を 1 行で設定するヘルパ (Phase A2 リファクタリング)。
 * decl.c / parser.c で 4 行のパターンが 9 箇所重複していたのを集約する。 */

void ps_decl_reset_locals(void);
void ps_decl_enter_scope(void);
void ps_decl_leave_scope(void);
lvar_t *ps_decl_get_locals(void);
void ps_decl_reserve_variadic_regs(void);
lvar_t *ps_decl_find_lvar(char *name, int len);
lvar_t *psx_decl_find_lvar_by_offset(int offset);
void ps_decl_replay_lvar_usage_events(lvar_t *all_locals);
void ps_decl_reset_translation_unit_state(void);
psx_lvar_usage_region_t *psx_decl_begin_lvar_usage_region(void);
void psx_decl_end_lvar_usage_region(psx_lvar_usage_region_t *region);
void ps_decl_suppress_lvar_usage_region(psx_lvar_usage_region_t *region);
void psx_decl_attach_lvar_current_region(lvar_t *var);
lvar_t *ps_decl_register_lvar(char *name, int len);
lvar_t *ps_decl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array);
lvar_t *ps_decl_register_lvar_sized_align(char *name, int len, int size, int elem_size, int is_array, int align);
void ps_decl_set_gvar_type_size(global_var_t *gv, int type_size);
void ps_decl_set_gvar_decl_type(global_var_t *gv,
                                 const psx_type_t *decl_type);
void ps_decl_set_current_funcname(char *name, int len);
void ps_decl_get_current_funcname(char **out_name, int *out_len);

node_t *psx_decl_parse_initializer_for_var(lvar_t *var, int is_pointer);
node_t *ps_decl_bind_initializer_for_var(
    lvar_t *var, int is_pointer, node_t *initializer,
    psx_decl_init_kind_t initializer_kind, token_t *init_tok);


void ps_decl_record_lvar_usage_in_region(lvar_t *var, psx_lvar_usage_kind_t kind,
                                          psx_lvar_usage_region_t *region);

#endif

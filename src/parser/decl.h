#ifndef PARSER_DECL_H
#define PARSER_DECL_H

/* decl.h は AST node 型 (node_t) と シンボルテーブル (global_var_t) の
 * 両方を使う。Phase C1-2: 両ヘッダを明示的に include する。 */
#include "ast.h"
#include "arena.h"
#include "core.h"
#include "lvar_public.h"
#include "symtab.h"

typedef enum {
  PSX_LVAR_USAGE_EVALUATED,
  PSX_LVAR_USAGE_UNEVALUATED,
  PSX_LVAR_USAGE_ADDRESS_TAKEN,
  PSX_LVAR_USAGE_INITIALIZED,
} psx_lvar_usage_kind_t;

typedef struct psx_local_registry_t psx_local_registry_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct psx_local_declaration_callbacks_t
    psx_local_declaration_callbacks_t;

void ps_decl_reset_locals_in(psx_local_registry_t *registry);
void ps_decl_enter_scope_in(psx_local_registry_t *registry);
void ps_decl_leave_scope_in(psx_local_registry_t *registry);
lvar_t *ps_decl_get_storage_objects_in(
    const psx_local_registry_t *registry);
void ps_decl_replay_lvar_usage_events_in(
    psx_local_registry_t *registry, lvar_t *storage_objects);
psx_lvar_usage_region_t *psx_decl_begin_lvar_usage_region_in(
    psx_local_registry_t *registry);
void psx_decl_end_lvar_usage_region_in(
    psx_local_registry_t *registry, psx_lvar_usage_region_t *region);
void psx_decl_attach_lvar_current_region_in(
    const psx_local_registry_t *registry, lvar_t *var);
void ps_decl_record_lvar_usage_in_region_in(
    psx_local_registry_t *registry, lvar_t *var,
    psx_lvar_usage_kind_t kind, psx_lvar_usage_region_t *region);
void ps_decl_suppress_lvar_usage_region(psx_lvar_usage_region_t *region);
void ps_decl_suppress_lvar_warnings_by_offset_in(
    psx_local_registry_t *registry, int offset);
void ps_decl_set_current_funcname_in(
    psx_local_registry_t *registry, char *name, int len);
void ps_decl_get_current_funcname_in(
    const psx_local_registry_t *registry,
    char **out_name, int *out_len);

#endif

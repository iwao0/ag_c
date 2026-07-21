#include "local_usage_diagnostics.h"

#include "../diag/diag.h"
#include "../parser/decl.h"
#include "../parser/local_registry.h"
#include "../parser/lvar_public.h"

static void record_preinitialized_locals(
    psx_local_registry_t *local_registry,
    lvar_t *storage_objects) {
  for (lvar_t *var = storage_objects; var;
       var = ps_lvar_next_storage(var)) {
    psx_lvar_registry_view_t view = ps_lvar_registry_view(var);
    if (view.is_param) {
      ps_decl_record_lvar_usage_in_region_in(
          local_registry, var, PSX_LVAR_USAGE_INITIALIZED, NULL);
    } else if (view.is_static_local) {
      ps_decl_record_lvar_usage_in_region_in(
          local_registry, var, PSX_LVAR_USAGE_INITIALIZED,
          view.decl_region);
    }
  }
}

void psx_prepare_recorded_local_usage_in(
    psx_local_registry_t *local_registry,
    lvar_t *storage_objects) {
  if (!local_registry) return;
  record_preinitialized_locals(local_registry, storage_objects);
  ps_decl_replay_lvar_usage_events_in(
      local_registry, storage_objects);
}

void psx_emit_recorded_local_usage_warnings_in(
    ag_diagnostic_context_t *diagnostics,
    lvar_t *storage_objects,
    const token_t *fallback_diag_tok) {
  for (lvar_t *var = storage_objects; var;
       var = ps_lvar_next_storage(var)) {
    psx_lvar_registry_view_t view = ps_lvar_registry_view(var);
    if (view.suppress_unreachable_warnings) continue;
    if (!view.is_used && !view.is_unevaluated_used &&
        !view.is_address_taken && !view.is_param &&
        view.name && view.name[0] != '_') {
      diag_warn_tokf_in(
          diagnostics, DIAG_WARN_PARSER_UNUSED_VARIABLE,
          fallback_diag_tok,
          diag_warn_message_for_in(
              diagnostics, DIAG_WARN_PARSER_UNUSED_VARIABLE),
          view.name_len, view.name);
    } else if (view.is_used && !view.is_initialized &&
               !view.is_param && !view.is_array) {
      diag_warn_tokf_in(
          diagnostics, DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE,
          fallback_diag_tok,
          diag_warn_message_for_in(
              diagnostics,
              DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE),
          view.name_len, view.name);
    }
  }
}

#include "translation_unit_compat.h"

#include "../compilation_session_compat.h"

void psx_frontend_reset_translation_unit_state(void) {
  (void)psx_frontend_reset_translation_unit_state_in_session(
      ag_compilation_session_active_compat());
}

void psx_frontend_free_processed_ast(void) {
  (void)psx_frontend_free_processed_ast_in_session(
      ag_compilation_session_active_compat());
}

node_t **psx_frontend_program_ctx(
    tokenizer_context_t *tk_ctx, token_t *start) {
  return psx_frontend_program_in_session(
      ag_compilation_session_active_compat(), tk_ctx, start);
}

node_t **psx_frontend_program_from(token_t *start) {
  return psx_frontend_program_ctx(NULL, start);
}

node_t **psx_frontend_program(void) {
  return psx_frontend_program_ctx(NULL, tk_get_current_token());
}

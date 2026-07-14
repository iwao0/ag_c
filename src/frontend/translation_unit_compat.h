#ifndef FRONTEND_TRANSLATION_UNIT_COMPAT_H
#define FRONTEND_TRANSLATION_UNIT_COMPAT_H

#include "translation_unit.h"

void psx_frontend_reset_translation_unit_state(void);
void psx_frontend_free_processed_ast(void);
node_t **psx_frontend_program(void);
node_t **psx_frontend_program_from(token_t *start);
node_t **psx_frontend_program_ctx(
    tokenizer_context_t *tk_ctx, token_t *start);

#endif

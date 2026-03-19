#ifndef DIAG_DIAG_H
#define DIAG_DIAG_H

#include "error_catalog.h"
#include "../tokenizer/token.h"

void diag_set_locale(const char *locale);
const char *diag_get_locale(void);
const char *diag_message_for(diag_error_id_t id);

void diag_emit_atf(diag_error_id_t id, const char *input, const char *loc, const char *fmt, ...)
    __attribute__((noreturn));
void diag_emit_tokf(diag_error_id_t id, const token_t *tok, const char *fmt, ...)
    __attribute__((noreturn));
void diag_emit_internalf(diag_error_id_t id, const char *fmt, ...) __attribute__((noreturn));

#endif

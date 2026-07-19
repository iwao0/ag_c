#ifndef TOKENIZER_TEST_H
#define TOKENIZER_TEST_H

#include "tokenizer.h"

#include <stddef.h>

/* Legacy contextless surface retained only inside tokenizer/parser tests. */
tokenizer_context_t *tk_get_default_context(void);
tokenizer_context_t *tk_context_activate(tokenizer_context_t *ctx);
tokenizer_context_t *tk_context_active(void);
void tk_test_context_shutdown(void);

token_t *tk_get_current_token(void);
void tk_set_current_token(token_t *tok);
bool tk_consume(char op);
bool tk_consume_str(const char *op);
token_ident_t *tk_consume_ident(void);
void tk_expect(char op);
int tk_expect_number(void);
bool tk_at_eof(void);
token_t *tk_tokenize(const char *p);

void tk_set_strict_c11_mode(bool strict);
bool tk_get_strict_c11_mode(void);
void tk_set_enable_trigraphs(bool enable);
bool tk_get_enable_trigraphs(void);
void tk_set_enable_binary_literals(bool enable);
bool tk_get_enable_binary_literals(void);
void tk_set_enable_c11_audit_extensions(bool enable);
bool tk_get_enable_c11_audit_extensions(void);

void tk_reset_tokenizer_stats(void);
tokenizer_stats_t tk_get_tokenizer_stats(void);
void tk_set_max_token_len_for_test(size_t max_len);

#endif

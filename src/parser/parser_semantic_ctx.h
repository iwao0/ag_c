#ifndef PARSER_SEMANTIC_CTX_H
#define PARSER_SEMANTIC_CTX_H

#include "../tokenizer/token.h"
#include <stdbool.h>

void pctx_reset_function_scope(void);
void pctx_register_goto_ref(char *name, int len, token_t *tok);
void pctx_register_label_def(char *name, int len, token_t *tok);
void pctx_validate_goto_refs(void);

bool pctx_has_tag_type(token_kind_t kind, char *name, int len);
void pctx_define_tag_type(token_kind_t kind, char *name, int len);

bool pctx_is_type_token(token_kind_t kind);
bool pctx_is_tag_keyword(token_kind_t kind);
int pctx_scalar_type_size(token_kind_t kind);
void pctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size);

#endif

#ifndef PARSER_SEMANTIC_CTX_H
#define PARSER_SEMANTIC_CTX_H

#include "../../tokenizer/token.h"
#include <stdbool.h>

void psx_ctx_reset_function_scope(void);
void psx_ctx_enter_block_scope(void);
void psx_ctx_leave_block_scope(void);
void psx_ctx_register_goto_ref(char *name, int len, token_t *tok);
void psx_ctx_register_label_def(char *name, int len, token_t *tok);
void psx_ctx_validate_goto_refs(void);

bool psx_ctx_has_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count);
int psx_ctx_get_tag_member_count(token_kind_t kind, char *name, int len);
void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len, int member_count, int tag_size);
int psx_ctx_get_tag_size(token_kind_t kind, char *name, int len);
void psx_ctx_add_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                            char *member_name, int member_len, int offset,
                            int type_size, int deref_size,
                            token_kind_t member_tag_kind, char *member_tag_name,
                            int member_tag_len, int member_is_tag_pointer);
bool psx_ctx_find_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                             char *member_name, int member_len,
                             int *out_offset, int *out_type_size, int *out_deref_size,
                             token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                             int *out_member_tag_len, int *out_member_is_tag_pointer);
void psx_ctx_define_enum_const(char *name, int len, long long value);
bool psx_ctx_find_enum_const(char *name, int len, long long *out_value);

bool psx_ctx_is_type_token(token_kind_t kind);
bool psx_ctx_is_tag_keyword(token_kind_t kind);
int psx_ctx_scalar_type_size(token_kind_t kind);
void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size);

#endif

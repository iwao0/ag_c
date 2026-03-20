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
                            int type_size, int deref_size, int array_len,
                            token_kind_t member_tag_kind, char *member_tag_name,
                            int member_tag_len, int member_is_tag_pointer);
void psx_ctx_add_tag_member_bf(token_kind_t tag_kind, char *tag_name, int tag_len,
                               char *member_name, int member_len, int offset,
                               int type_size, int deref_size, int array_len,
                               token_kind_t member_tag_kind, char *member_tag_name,
                               int member_tag_len, int member_is_tag_pointer,
                               int bit_width, int bit_offset, int bit_is_signed);
bool psx_ctx_get_tag_member_bf(token_kind_t tag_kind, char *tag_name, int tag_len,
                               char *member_name, int member_len,
                               int *out_bit_width, int *out_bit_offset, int *out_bit_is_signed);
bool psx_ctx_find_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                             char *member_name, int member_len,
                             int *out_offset, int *out_type_size, int *out_deref_size, int *out_array_len,
                             token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                             int *out_member_tag_len, int *out_member_is_tag_pointer);
bool psx_ctx_get_tag_member_at(token_kind_t tag_kind, char *tag_name, int tag_len, int index,
                               char **out_member_name, int *out_member_len,
                               int *out_offset, int *out_type_size, int *out_deref_size, int *out_array_len,
                               token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                               int *out_member_tag_len, int *out_member_is_tag_pointer);
void psx_ctx_define_enum_const(char *name, int len, long long value);
bool psx_ctx_find_enum_const(char *name, int len, long long *out_value);
void psx_ctx_define_typedef_name(char *name, int len, token_kind_t base_kind, int elem_size,
                                 tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                 char *tag_name, int tag_len, int is_pointer);
bool psx_ctx_find_typedef_name(char *name, int len, token_kind_t *out_base_kind,
                               int *out_elem_size, tk_float_kind_t *out_fp_kind,
                               token_kind_t *out_tag_kind, char **out_tag_name,
                               int *out_tag_len, int *out_is_pointer);
bool psx_ctx_is_typedef_name_token(token_t *tok);
void psx_ctx_define_function_name(char *name, int len);
bool psx_ctx_has_function_name(char *name, int len);

bool psx_ctx_is_type_token(token_kind_t kind);
bool psx_ctx_is_tag_keyword(token_kind_t kind);
int psx_ctx_scalar_type_size(token_kind_t kind);
void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size);

#endif

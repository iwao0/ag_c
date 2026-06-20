#ifndef PARSER_INTERNAL_CORE_H
#define PARSER_INTERNAL_CORE_H

/* core.h は AST node 型を使わない (token_kind_t と bool のみ)。
 * Phase C1-2: ast.h ではなく token.h を直接 include する。 */
#include "../../tokenizer/token.h"
#include <stdbool.h>

#define PS_MAX_DECLARATOR_COUNT 1024
#define PS_MAX_INITIALIZER_ELEMENTS 4096

token_kind_t psx_consume_type_kind(void);
int psx_last_type_is_unsigned(void);
int psx_last_type_is_complex(void);
int psx_last_type_is_long_long(void);
int psx_last_type_is_plain_char(void);
int psx_last_type_is_atomic(void);
void psx_take_type_qualifiers(int *is_const_qualified, int *is_volatile_qualified);
void psx_take_alignas_value(int *align);
void psx_take_extern_flag(int *is_extern);
void psx_take_static_flag(int *is_static);
void psx_set_static_flag(int is_static);
void psx_set_alignas_value(int align);
void psx_consume_pointer_prefix(int *is_ptr);
bool psx_is_decl_prefix_token(token_kind_t k);
void psx_skip_func_suffix_groups(int *out_has_func_suffix);
bool psx_try_consume_pragma_pack_marker(void);

#endif

#ifndef PARSER_INTERNAL_CORE_H
#define PARSER_INTERNAL_CORE_H

#include "../ast.h"

#define PS_MAX_DECLARATOR_COUNT 1024
#define PS_MAX_INITIALIZER_ELEMENTS 4096

token_kind_t psx_consume_type_kind(void);
int psx_last_type_is_unsigned(void);
int psx_last_type_is_complex(void);
int psx_last_type_is_atomic(void);
void psx_take_type_qualifiers(int *is_const_qualified, int *is_volatile_qualified);
void psx_take_alignas_value(int *align);
void psx_take_extern_flag(int *is_extern);
void psx_consume_pointer_prefix(int *is_ptr);

#endif

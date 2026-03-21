#ifndef PARSER_INTERNAL_CORE_H
#define PARSER_INTERNAL_CORE_H

#include "../ast.h"

token_kind_t psx_consume_type_kind(void);
void psx_take_type_qualifiers(int *is_const_qualified, int *is_volatile_qualified);
void psx_take_alignas_value(int *align);
void psx_take_extern_flag(int *is_extern);

#endif

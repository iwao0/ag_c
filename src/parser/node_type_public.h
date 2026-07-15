#ifndef PARSER_NODE_TYPE_PUBLIC_H
#define PARSER_NODE_TYPE_PUBLIC_H

#include "../tokenizer/token.h"
#include "../type_system/type_ids.h"
#include "node_fwd.h"

typedef struct psx_type_t psx_type_t;

const psx_type_t *ps_node_get_type(const node_t *node);
psx_qual_type_t ps_node_qual_type(const node_t *node);
const psx_type_t *ps_function_definition_return_type(
    const node_function_definition_t *function);
int ps_node_conversion_value_is_unsigned(node_t *n);
int ps_node_shift_operation_is_unsigned(node_t *n);
tk_float_kind_t ps_node_value_fp_kind(node_t *node);
int ps_node_value_is_complex(node_t *node);
int ps_node_value_is_void(node_t *node);
int ps_node_bitfield_info(node_t *node, int *bit_width, int *bit_offset,
                           int *bit_is_signed);
int ps_node_value_is_pointer_like(node_t *node);
int ps_node_is_unsigned_type(node_t *node);
int ps_node_deref_decays_to_address(node_t *node);

#endif

#ifndef PARSER_NODE_RESOLUTION_STATE_H
#define PARSER_NODE_RESOLUTION_STATE_H

#include "type.h"
#include "vla_runtime.h"

struct global_var_t;
struct lvar_t;
struct node_t;

typedef struct {
  psx_vla_runtime_view_t vla_runtime;
  unsigned char is_scalar_ptr_member_lvalue;
  unsigned char subscript_uses_base_address;
  unsigned char bit_width;
  unsigned char bit_offset;
  unsigned char bit_is_signed;
} psx_expr_type_state_t;

typedef struct {
  struct lvar_t *local_object;
  struct global_var_t *global_object;
  struct node_t *runtime_initialization;
  struct node_t *direct_value;
  unsigned char is_planned;
} psx_compound_literal_resolution_t;

typedef struct psx_node_resolution_state_t {
  const psx_type_t *type;
  psx_qual_type_t qual_type;
  psx_expr_type_state_t expr;
  psx_compound_literal_resolution_t compound_literal;
} psx_node_resolution_state_t;

#endif

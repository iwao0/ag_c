#ifndef PARSER_NODE_RESOLUTION_STATE_H
#define PARSER_NODE_RESOLUTION_STATE_H

#include "type.h"
#include "vla_runtime.h"

struct global_var_t;
struct lvar_t;
struct node_t;
struct psx_runtime_initializer_plan_t;
struct psx_sizeof_runtime_plan_t;

typedef struct {
  psx_vla_runtime_view_t vla_runtime;
  unsigned char is_scalar_ptr_member_lvalue;
  unsigned char subscript_uses_base_address;
  unsigned char bit_width;
  unsigned char bit_offset;
  unsigned char bit_is_signed;
} psx_expr_type_state_t;

typedef enum {
  PSX_COMPOUND_LITERAL_UNPLANNED = 0,
  PSX_COMPOUND_LITERAL_DIRECT_INITIALIZER,
  PSX_COMPOUND_LITERAL_LOCAL_OBJECT,
  PSX_COMPOUND_LITERAL_GLOBAL_OBJECT,
} psx_compound_literal_resolution_kind_t;

typedef struct {
  struct lvar_t *local_object;
  struct global_var_t *global_object;
  struct psx_runtime_initializer_plan_t *runtime_initializer;
  int direct_initializer_index;
  psx_compound_literal_resolution_kind_t kind;
} psx_compound_literal_resolution_t;

typedef struct {
  int selected_index;
  unsigned char is_resolved;
} psx_generic_selection_resolution_state_t;

typedef struct {
  struct psx_sizeof_runtime_plan_t *runtime_plan;
  int resolved_size;
  int runtime_size_slot;
  unsigned char evaluates_vla_operand;
} psx_sizeof_query_resolution_state_t;

typedef struct {
  int resolved_alignment;
} psx_alignof_query_resolution_state_t;

typedef enum {
  PSX_SOURCE_CAST_UNRESOLVED = 0,
  PSX_SOURCE_CAST_DIRECT_HIR,
  PSX_SOURCE_CAST_AGGREGATE_DIRECT_HIR,
  PSX_SOURCE_CAST_AGGREGATE_TEMPORARY,
} psx_source_cast_resolution_kind_t;

typedef struct {
  struct lvar_t *aggregate_temporary;
  psx_qual_type_t aggregate_member_qual_type;
  int aggregate_member_offset;
  unsigned char aggregate_member_bit_width;
  unsigned char aggregate_member_bit_offset;
  unsigned char aggregate_member_bit_is_signed;
  psx_source_cast_resolution_kind_t kind;
} psx_source_cast_resolution_t;

typedef struct psx_node_resolution_state_t {
  const psx_type_t *type;
  psx_qual_type_t qual_type;
  psx_expr_type_state_t expr;
  psx_compound_literal_resolution_t compound_literal;
  psx_generic_selection_resolution_state_t generic_selection;
  psx_sizeof_query_resolution_state_t sizeof_query;
  psx_alignof_query_resolution_state_t alignof_query;
  psx_source_cast_resolution_t source_cast;
} psx_node_resolution_state_t;

#endif

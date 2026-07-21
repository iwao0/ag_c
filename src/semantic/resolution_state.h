#ifndef SEMANTIC_RESOLUTION_STATE_H
#define SEMANTIC_RESOLUTION_STATE_H

#include "../parser/vla_runtime.h"
#include "../type_system/type_ids.h"
#include "declarator_application_types.h"
#include "record_decl.h"
#include "resolved_node_kind.h"

struct global_var_t;
struct lvar_t;
struct node_t;
struct psx_lvar_usage_region_t;
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
  PSX_RESOLVED_REFERENCE_NONE = 0,
  PSX_RESOLVED_REFERENCE_LOCAL,
  PSX_RESOLVED_REFERENCE_GLOBAL,
  PSX_RESOLVED_REFERENCE_FUNCTION,
  PSX_RESOLVED_REFERENCE_VARARG_CURSOR,
} psx_resolved_reference_kind_t;

typedef struct {
  struct lvar_t *local;
  struct global_var_t *global;
  char *name;
  int name_len;
  int storage_offset;
  psx_resolved_reference_kind_t kind;
  unsigned char is_thread_local;
} psx_resolved_reference_state_t;

typedef enum {
  PSX_NODE_TYPE_NONE = 0,
  PSX_NODE_TYPE_CANONICAL,
} psx_node_type_binding_kind_t;

typedef struct {
  psx_node_type_binding_kind_t kind;
  psx_qual_type_t canonical_type;
} psx_node_type_binding_t;

typedef struct psx_node_resolution_state_t {
  psx_resolved_node_kind_t node_kind;
  psx_node_type_binding_t type_binding;
  psx_expr_type_state_t expr;
  psx_resolved_reference_state_t reference;
} psx_node_resolution_state_t;

#endif

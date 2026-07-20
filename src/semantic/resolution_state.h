#ifndef SEMANTIC_RESOLUTION_STATE_H
#define SEMANTIC_RESOLUTION_STATE_H

#include "../parser/type.h"
#include "../parser/vla_runtime.h"
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
  unsigned char requires_addressable_storage;
} psx_compound_literal_resolution_t;

typedef struct {
  int selected_index;
  struct psx_type_name_resolution_state_t *association_type_names;
  int association_type_name_count;
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

typedef struct {
  psx_record_member_decl_t declaration;
  psx_record_id_t record_id;
  psx_qual_type_t base_address_qual_type;
  int member_index;
  unsigned char is_resolved;
} psx_member_access_state_t;

typedef struct {
  psx_qual_type_t callee_qual_type;
  char *direct_name;
  int direct_name_len;
  unsigned char is_implicit_declaration;
} psx_function_call_resolution_state_t;

typedef struct {
  long long value;
  unsigned char is_resolved;
} psx_case_label_resolution_state_t;

typedef struct {
  char *string_label;
} psx_literal_resolution_state_t;

typedef struct {
  struct psx_lvar_usage_region_t *region;
  struct lvar_t *local;
  unsigned char records_usage;
  unsigned char is_unevaluated;
} psx_lvar_usage_resolution_state_t;

typedef struct {
  unsigned char is_source_assignment;
  unsigned char is_source_cast;
  unsigned char is_decl_initializer;
  unsigned char is_implicit_int_return;
  unsigned char widen_zext_i64;
} psx_node_semantic_flags_t;

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
  PSX_TYPE_NAME_UNBOUND = 0,
  PSX_TYPE_NAME_BOUND,
  PSX_TYPE_NAME_RESOLVED,
} psx_type_name_resolution_kind_t;

typedef struct psx_type_name_resolution_state_t {
  psx_type_name_resolution_kind_t kind;
  union {
    struct {
      const psx_type_t *base_type;
      const psx_runtime_declarator_application_t *runtime_application;
    } bound;
    struct {
      const psx_semantic_type_table_t *type_table;
      psx_qual_type_t qual_type;
    } resolved;
  } value;
} psx_type_name_resolution_state_t;

typedef enum {
  PSX_NODE_TYPE_NONE = 0,
  PSX_NODE_TYPE_PENDING,
  PSX_NODE_TYPE_CANONICAL,
} psx_node_type_binding_kind_t;

typedef struct {
  psx_node_type_binding_kind_t kind;
  union {
    const psx_type_t *pending_type;
    psx_qual_type_t canonical_type;
  } value;
} psx_node_type_binding_t;

typedef struct psx_node_resolution_state_t {
  psx_resolved_node_kind_t node_kind;
  psx_node_type_binding_t type_binding;
  psx_expr_type_state_t expr;
  psx_compound_literal_resolution_t compound_literal;
  psx_generic_selection_resolution_state_t generic_selection;
  psx_sizeof_query_resolution_state_t sizeof_query;
  psx_alignof_query_resolution_state_t alignof_query;
  psx_source_cast_resolution_t source_cast;
  psx_member_access_state_t member_access;
  psx_function_call_resolution_state_t function_call;
  psx_case_label_resolution_state_t case_label;
  psx_literal_resolution_state_t literal;
  psx_lvar_usage_resolution_state_t lvar_usage;
  psx_node_semantic_flags_t flags;
  psx_resolved_reference_state_t reference;
  psx_type_name_resolution_state_t type_name;
} psx_node_resolution_state_t;

#endif

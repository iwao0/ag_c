#ifndef HIR_HIR_INTERNAL_H
#define HIR_HIR_INTERNAL_H

#include "hir.h"

typedef struct {
  psx_hir_node_kind_t kind;
  psx_qual_type_t attached_qual_type;
  const psx_hir_node_id_t *children;
  const psx_hir_edge_kind_t *child_edges;
  size_t child_count;
  const char *name;
  size_t name_length;
  const char *literal_contents;
  size_t literal_length;
  long long integer_value;
  double floating_value;
  int storage_offset;
  int object_offset;
  int object_size;
  int object_align;
  int member_offset;
  int initializer_union_offset;
  int initializer_union_member_index;
  int vla_stride_frame_offset;
  int vla_stride_source_offset;
  int vla_stride_element_size;
  int vla_stride_slot_size;
  const int *vla_dimension_constants;
  const int *vla_dimension_source_offsets;
  size_t vla_dimension_count;
  const int *vla_runtime_store_offsets;
  const int *vla_runtime_store_dimensions;
  size_t vla_runtime_store_count;
  int label_id;
  psx_hir_symbol_id_t symbol_id;
  unsigned char bit_width;
  unsigned char bit_offset;
  unsigned char bit_is_signed;
  unsigned char member_from_pointer;
  unsigned char is_static_function;
  unsigned char is_implicit_call;
  unsigned char is_source_assignment;
  unsigned char is_declaration_initializer;
  unsigned char is_resolved_initializer_entry;
  unsigned char has_initializer_union_member;
} psx_hir_node_spec_t;

typedef struct {
  psx_hir_node_spec_t node;
  psx_qual_type_t qual_type;
} psx_hir_expression_spec_t;

typedef struct {
  psx_hir_node_spec_t node;
} psx_hir_statement_spec_t;

typedef struct {
  const char *name;
  size_t name_length;
  psx_decl_id_t declaration_id;
  psx_qual_type_t qual_type;
  int byte_size;
  int alignment;
  unsigned char is_extern;
  unsigned char is_static;
  unsigned char is_thread_local;
} psx_hir_symbol_spec_t;

int psx_hir_kind_is_expression(psx_hir_node_kind_t kind);
size_t psx_hir_module_checkpoint(const psx_hir_module_t *module);
size_t psx_hir_module_symbol_checkpoint(const psx_hir_module_t *module);
void psx_hir_module_rollback(
    psx_hir_module_t *module, size_t node_checkpoint,
    size_t root_checkpoint, size_t symbol_checkpoint);
psx_hir_node_id_t psx_hir_module_add_expression(
    psx_hir_module_t *module,
    const psx_hir_expression_spec_t *spec);
psx_hir_node_id_t psx_hir_module_add_statement(
    psx_hir_module_t *module,
    const psx_hir_statement_spec_t *spec);
int psx_hir_module_add_root(
    psx_hir_module_t *module, psx_hir_node_id_t root);
psx_hir_symbol_id_t psx_hir_module_intern_symbol(
    psx_hir_module_t *module, const psx_hir_symbol_spec_t *spec);

#endif

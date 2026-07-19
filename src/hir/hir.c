#include "hir_internal.h"

#include <stdlib.h>
#include <string.h>

struct psx_hir_node_t {
  psx_hir_node_kind_t kind;
  psx_qual_type_t attached_qual_type;
  psx_hir_node_id_t *children;
  psx_hir_edge_kind_t *child_edges;
  size_t child_count;
  char *name;
  size_t name_length;
  char *literal_contents;
  size_t literal_length;
  long long integer_value;
  double floating_value;
  int storage_offset;
  int object_offset;
  int object_size;
  int object_align;
  int member_offset;
  int vla_stride_frame_offset;
  int vla_stride_source_offset;
  int vla_stride_element_size;
  int vla_stride_slot_size;
  int *vla_dimension_constants;
  int *vla_dimension_source_offsets;
  size_t vla_dimension_count;
  int *vla_runtime_store_offsets;
  int *vla_runtime_store_dimensions;
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
};

typedef struct {
  psx_hir_node_t node;
  psx_qual_type_t qual_type;
} psx_hir_expression_node_t;

struct psx_hir_symbol_t {
  char *name;
  size_t name_length;
  psx_qual_type_t qual_type;
  int byte_size;
  int alignment;
  unsigned char is_extern;
  unsigned char is_static;
  unsigned char is_thread_local;
};

struct psx_hir_module_t {
  psx_hir_node_t **nodes;
  size_t node_count;
  size_t node_capacity;
  psx_hir_node_id_t *roots;
  size_t root_count;
  size_t root_capacity;
  psx_hir_symbol_t **symbols;
  size_t symbol_count;
  size_t symbol_capacity;
};

static psx_qual_type_t invalid_qual_type(void) {
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

static char *copy_text(const char *text, size_t length) {
  if (!text) return NULL;
  char *copy = malloc(length + 1);
  if (!copy) return NULL;
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

static void destroy_node(psx_hir_node_t *node) {
  if (!node) return;
  free(node->children);
  free(node->child_edges);
  free(node->name);
  free(node->literal_contents);
  free(node->vla_dimension_constants);
  free(node->vla_dimension_source_offsets);
  free(node->vla_runtime_store_offsets);
  free(node->vla_runtime_store_dimensions);
  free(node);
}

static void destroy_symbol(psx_hir_symbol_t *symbol) {
  if (!symbol) return;
  free(symbol->name);
  free(symbol);
}

psx_hir_module_t *psx_hir_module_create(void) {
  return calloc(1, sizeof(psx_hir_module_t));
}

void psx_hir_module_reset(psx_hir_module_t *module) {
  if (!module) return;
  for (size_t i = 0; i < module->node_count; i++)
    destroy_node(module->nodes[i]);
  for (size_t i = 0; i < module->symbol_count; i++)
    destroy_symbol(module->symbols[i]);
  module->node_count = 0;
  module->root_count = 0;
  module->symbol_count = 0;
}

void psx_hir_module_destroy(psx_hir_module_t *module) {
  if (!module) return;
  psx_hir_module_reset(module);
  free(module->nodes);
  free(module->roots);
  free(module->symbols);
  free(module);
}

size_t psx_hir_module_node_count(const psx_hir_module_t *module) {
  return module ? module->node_count : 0;
}

size_t psx_hir_module_root_count(const psx_hir_module_t *module) {
  return module ? module->root_count : 0;
}

size_t psx_hir_module_symbol_count(const psx_hir_module_t *module) {
  return module ? module->symbol_count : 0;
}

psx_hir_node_id_t psx_hir_module_root_at(
    const psx_hir_module_t *module, size_t index) {
  return module && index < module->root_count
             ? module->roots[index] : PSX_HIR_NODE_ID_INVALID;
}

const psx_hir_node_t *psx_hir_module_lookup(
    const psx_hir_module_t *module, psx_hir_node_id_t id) {
  if (!module || id == PSX_HIR_NODE_ID_INVALID ||
      (size_t)id > module->node_count)
    return NULL;
  return module->nodes[id - 1];
}

const psx_hir_symbol_t *psx_hir_module_symbol_lookup(
    const psx_hir_module_t *module, psx_hir_symbol_id_t id) {
  if (!module || id == PSX_HIR_SYMBOL_ID_INVALID ||
      (size_t)id > module->symbol_count)
    return NULL;
  return module->symbols[id - 1];
}

size_t psx_hir_module_checkpoint(const psx_hir_module_t *module) {
  return module ? module->node_count : 0;
}

size_t psx_hir_module_symbol_checkpoint(const psx_hir_module_t *module) {
  return module ? module->symbol_count : 0;
}

void psx_hir_module_rollback(
    psx_hir_module_t *module, size_t node_checkpoint,
    size_t root_checkpoint, size_t symbol_checkpoint) {
  if (!module) return;
  if (node_checkpoint > module->node_count)
    node_checkpoint = module->node_count;
  for (size_t i = node_checkpoint; i < module->node_count; i++)
    destroy_node(module->nodes[i]);
  module->node_count = node_checkpoint;
  if (root_checkpoint < module->root_count)
    module->root_count = root_checkpoint;
  if (symbol_checkpoint > module->symbol_count)
    symbol_checkpoint = module->symbol_count;
  for (size_t i = symbol_checkpoint; i < module->symbol_count; i++)
    destroy_symbol(module->symbols[i]);
  module->symbol_count = symbol_checkpoint;
}

static int reserve_nodes(psx_hir_module_t *module, size_t needed) {
  if (needed <= module->node_capacity) return 1;
  size_t capacity = module->node_capacity ? module->node_capacity * 2 : 64;
  while (capacity < needed) capacity *= 2;
  psx_hir_node_t **nodes = realloc(
      module->nodes, capacity * sizeof(*nodes));
  if (!nodes) return 0;
  module->nodes = nodes;
  module->node_capacity = capacity;
  return 1;
}

static int reserve_roots(psx_hir_module_t *module, size_t needed) {
  if (needed <= module->root_capacity) return 1;
  size_t capacity = module->root_capacity ? module->root_capacity * 2 : 16;
  while (capacity < needed) capacity *= 2;
  psx_hir_node_id_t *roots = realloc(
      module->roots, capacity * sizeof(*roots));
  if (!roots) return 0;
  module->roots = roots;
  module->root_capacity = capacity;
  return 1;
}

static int reserve_symbols(psx_hir_module_t *module, size_t needed) {
  if (needed <= module->symbol_capacity) return 1;
  size_t capacity = module->symbol_capacity
                        ? module->symbol_capacity * 2 : 32;
  while (capacity < needed) capacity *= 2;
  psx_hir_symbol_t **symbols = realloc(
      module->symbols, capacity * sizeof(*symbols));
  if (!symbols) return 0;
  module->symbols = symbols;
  module->symbol_capacity = capacity;
  return 1;
}

psx_hir_symbol_id_t psx_hir_module_intern_symbol(
    psx_hir_module_t *module, const psx_hir_symbol_spec_t *spec) {
  if (!module || !spec || !spec->name || spec->name_length == 0 ||
      spec->qual_type.type_id == PSX_TYPE_ID_INVALID ||
      spec->byte_size <= 0 || spec->alignment <= 0)
    return PSX_HIR_SYMBOL_ID_INVALID;
  for (size_t i = 0; i < module->symbol_count; i++) {
    const psx_hir_symbol_t *symbol = module->symbols[i];
    if (symbol->name_length != spec->name_length ||
        memcmp(symbol->name, spec->name, spec->name_length) != 0)
      continue;
    if (symbol->qual_type.type_id != spec->qual_type.type_id ||
        symbol->qual_type.qualifiers != spec->qual_type.qualifiers ||
        symbol->byte_size != spec->byte_size ||
        symbol->alignment != spec->alignment ||
        symbol->is_thread_local != spec->is_thread_local)
      return PSX_HIR_SYMBOL_ID_INVALID;
    return (psx_hir_symbol_id_t)(i + 1);
  }
  if (!reserve_symbols(module, module->symbol_count + 1))
    return PSX_HIR_SYMBOL_ID_INVALID;
  psx_hir_symbol_t *symbol = calloc(1, sizeof(*symbol));
  if (!symbol) return PSX_HIR_SYMBOL_ID_INVALID;
  symbol->name = copy_text(spec->name, spec->name_length);
  if (!symbol->name) {
    destroy_symbol(symbol);
    return PSX_HIR_SYMBOL_ID_INVALID;
  }
  symbol->name_length = spec->name_length;
  symbol->qual_type = spec->qual_type;
  symbol->byte_size = spec->byte_size;
  symbol->alignment = spec->alignment;
  symbol->is_extern = spec->is_extern;
  symbol->is_static = spec->is_static;
  symbol->is_thread_local = spec->is_thread_local;
  module->symbols[module->symbol_count++] = symbol;
  return (psx_hir_symbol_id_t)module->symbol_count;
}

int psx_hir_kind_is_expression(psx_hir_node_kind_t kind) {
  switch (kind) {
    case PSX_HIR_ADD:
    case PSX_HIR_SUB:
    case PSX_HIR_MUL:
    case PSX_HIR_DIV:
    case PSX_HIR_MOD:
    case PSX_HIR_EQ:
    case PSX_HIR_NE:
    case PSX_HIR_LT:
    case PSX_HIR_LE:
    case PSX_HIR_BITAND:
    case PSX_HIR_BITXOR:
    case PSX_HIR_BITOR:
    case PSX_HIR_SHL:
    case PSX_HIR_SHR:
    case PSX_HIR_LOGAND:
    case PSX_HIR_LOGOR:
    case PSX_HIR_TERNARY:
    case PSX_HIR_COMMA:
    case PSX_HIR_ASSIGN:
    case PSX_HIR_OBJECT_COPY:
    case PSX_HIR_COMPOUND_ASSIGN:
    case PSX_HIR_LOCAL:
    case PSX_HIR_PRE_INC:
    case PSX_HIR_PRE_DEC:
    case PSX_HIR_POST_INC:
    case PSX_HIR_POST_DEC:
    case PSX_HIR_CALL:
    case PSX_HIR_FUNCTION_REF:
    case PSX_HIR_DEREF:
    case PSX_HIR_SUBSCRIPT:
    case PSX_HIR_MEMBER_ACCESS:
    case PSX_HIR_ADDRESS:
    case PSX_HIR_STRING:
    case PSX_HIR_NUMBER:
    case PSX_HIR_GLOBAL:
    case PSX_HIR_FP_TO_INT:
    case PSX_HIR_INT_TO_FP:
    case PSX_HIR_NEGATE:
    case PSX_HIR_LOGICAL_NOT:
    case PSX_HIR_VARARG_CURSOR:
    case PSX_HIR_CAST:
    case PSX_HIR_CREAL:
    case PSX_HIR_CIMAG:
    case PSX_HIR_STMT_EXPR:
      return 1;
    default:
      return 0;
  }
}

static psx_hir_node_id_t add_node(
    psx_hir_module_t *module, const psx_hir_node_spec_t *spec,
    size_t node_size) {
  if (!module || !spec) return PSX_HIR_NODE_ID_INVALID;
  if (!reserve_nodes(module, module->node_count + 1))
    return PSX_HIR_NODE_ID_INVALID;
  psx_hir_node_t *node = calloc(1, node_size);
  if (!node) return PSX_HIR_NODE_ID_INVALID;
  node->kind = spec->kind;
  node->attached_qual_type = spec->attached_qual_type;
  node->child_count = spec->child_count;
  node->name_length = spec->name_length;
  node->literal_length = spec->literal_length;
  node->integer_value = spec->integer_value;
  node->floating_value = spec->floating_value;
  node->storage_offset = spec->storage_offset;
  node->object_offset = spec->object_offset;
  node->object_size = spec->object_size;
  node->object_align = spec->object_align;
  node->member_offset = spec->member_offset;
  node->vla_stride_frame_offset = spec->vla_stride_frame_offset;
  node->vla_stride_source_offset = spec->vla_stride_source_offset;
  node->vla_stride_element_size = spec->vla_stride_element_size;
  node->vla_stride_slot_size = spec->vla_stride_slot_size;
  node->vla_dimension_count = spec->vla_dimension_count;
  node->vla_runtime_store_count = spec->vla_runtime_store_count;
  node->label_id = spec->label_id;
  node->symbol_id = spec->symbol_id;
  node->bit_width = spec->bit_width;
  node->bit_offset = spec->bit_offset;
  node->bit_is_signed = spec->bit_is_signed;
  node->member_from_pointer = spec->member_from_pointer;
  node->is_static_function = spec->is_static_function;
  node->is_implicit_call = spec->is_implicit_call;
  node->is_source_assignment = spec->is_source_assignment;
  node->is_declaration_initializer =
      spec->is_declaration_initializer;
  if (spec->child_count) {
    node->children = malloc(spec->child_count * sizeof(*node->children));
    node->child_edges = malloc(
        spec->child_count * sizeof(*node->child_edges));
    if (!node->children || !node->child_edges || !spec->child_edges) {
      destroy_node(node);
      return PSX_HIR_NODE_ID_INVALID;
    }
    memcpy(node->children, spec->children,
           spec->child_count * sizeof(*node->children));
    memcpy(node->child_edges, spec->child_edges,
           spec->child_count * sizeof(*node->child_edges));
  }
  if (spec->vla_dimension_count) {
    node->vla_dimension_constants = malloc(
        spec->vla_dimension_count *
        sizeof(*node->vla_dimension_constants));
    node->vla_dimension_source_offsets = malloc(
        spec->vla_dimension_count *
        sizeof(*node->vla_dimension_source_offsets));
    if (!node->vla_dimension_constants ||
        !node->vla_dimension_source_offsets ||
        !spec->vla_dimension_constants ||
        !spec->vla_dimension_source_offsets) {
      destroy_node(node);
      return PSX_HIR_NODE_ID_INVALID;
    }
    memcpy(node->vla_dimension_constants,
           spec->vla_dimension_constants,
           spec->vla_dimension_count *
               sizeof(*node->vla_dimension_constants));
    memcpy(node->vla_dimension_source_offsets,
           spec->vla_dimension_source_offsets,
           spec->vla_dimension_count *
               sizeof(*node->vla_dimension_source_offsets));
  }
  if (spec->vla_runtime_store_count) {
    node->vla_runtime_store_offsets = malloc(
        spec->vla_runtime_store_count *
        sizeof(*node->vla_runtime_store_offsets));
    node->vla_runtime_store_dimensions = malloc(
        spec->vla_runtime_store_count *
        sizeof(*node->vla_runtime_store_dimensions));
    if (!node->vla_runtime_store_offsets ||
        !node->vla_runtime_store_dimensions ||
        !spec->vla_runtime_store_offsets ||
        !spec->vla_runtime_store_dimensions) {
      destroy_node(node);
      return PSX_HIR_NODE_ID_INVALID;
    }
    memcpy(
        node->vla_runtime_store_offsets,
        spec->vla_runtime_store_offsets,
        spec->vla_runtime_store_count *
            sizeof(*node->vla_runtime_store_offsets));
    memcpy(
        node->vla_runtime_store_dimensions,
        spec->vla_runtime_store_dimensions,
        spec->vla_runtime_store_count *
            sizeof(*node->vla_runtime_store_dimensions));
  }
  node->name = copy_text(spec->name, spec->name_length);
  node->literal_contents = copy_text(
      spec->literal_contents, spec->literal_length);
  if ((spec->name_length && !node->name) ||
      (spec->literal_length && !node->literal_contents)) {
    destroy_node(node);
    return PSX_HIR_NODE_ID_INVALID;
  }
  module->nodes[module->node_count++] = node;
  return (psx_hir_node_id_t)module->node_count;
}

psx_hir_node_id_t psx_hir_module_add_expression(
    psx_hir_module_t *module,
    const psx_hir_expression_spec_t *spec) {
  if (!spec || !psx_hir_kind_is_expression(spec->node.kind) ||
      spec->qual_type.type_id == PSX_TYPE_ID_INVALID)
    return PSX_HIR_NODE_ID_INVALID;
  psx_hir_node_id_t id = add_node(
      module, &spec->node, sizeof(psx_hir_expression_node_t));
  if (id != PSX_HIR_NODE_ID_INVALID) {
    psx_hir_expression_node_t *expression =
        (psx_hir_expression_node_t *)module->nodes[id - 1];
    expression->qual_type = spec->qual_type;
  }
  return id;
}

psx_hir_node_id_t psx_hir_module_add_statement(
    psx_hir_module_t *module,
    const psx_hir_statement_spec_t *spec) {
  if (!spec || psx_hir_kind_is_expression(spec->node.kind))
    return PSX_HIR_NODE_ID_INVALID;
  return add_node(module, &spec->node, sizeof(psx_hir_node_t));
}

int psx_hir_module_add_root(
    psx_hir_module_t *module, psx_hir_node_id_t root) {
  if (!psx_hir_module_lookup(module, root) ||
      !reserve_roots(module, module->root_count + 1))
    return 0;
  module->roots[module->root_count++] = root;
  return 1;
}

psx_hir_node_kind_t psx_hir_node_kind(const psx_hir_node_t *node) {
  return node ? node->kind : PSX_HIR_BLOCK;
}

psx_hir_node_role_t psx_hir_node_role(const psx_hir_node_t *node) {
  return node && psx_hir_kind_is_expression(node->kind)
             ? PSX_HIR_ROLE_EXPRESSION
             : PSX_HIR_ROLE_STATEMENT;
}

psx_qual_type_t psx_hir_node_qual_type(const psx_hir_node_t *node) {
  if (!node || !psx_hir_kind_is_expression(node->kind))
    return invalid_qual_type();
  return ((const psx_hir_expression_node_t *)node)->qual_type;
}

psx_qual_type_t psx_hir_node_attached_qual_type(
    const psx_hir_node_t *node) {
  return node ? node->attached_qual_type : invalid_qual_type();
}

size_t psx_hir_node_child_count(const psx_hir_node_t *node) {
  return node ? node->child_count : 0;
}

psx_hir_node_id_t psx_hir_node_child_at(
    const psx_hir_node_t *node, size_t index) {
  return node && index < node->child_count
             ? node->children[index] : PSX_HIR_NODE_ID_INVALID;
}

psx_hir_edge_kind_t psx_hir_node_child_edge_at(
    const psx_hir_node_t *node, size_t index) {
  return node && index < node->child_count
             ? node->child_edges[index] : PSX_HIR_EDGE_LHS;
}

const char *psx_hir_node_name(
    const psx_hir_node_t *node, size_t *length) {
  if (length) *length = node ? node->name_length : 0;
  return node ? node->name : NULL;
}

const char *psx_hir_node_literal_contents(
    const psx_hir_node_t *node, size_t *length) {
  if (length) *length = node ? node->literal_length : 0;
  return node ? node->literal_contents : NULL;
}

long long psx_hir_node_integer_value(const psx_hir_node_t *node) {
  return node ? node->integer_value : 0;
}

double psx_hir_node_floating_value(const psx_hir_node_t *node) {
  return node ? node->floating_value : 0.0;
}

psx_hir_compound_operator_t psx_hir_node_compound_operator(
    const psx_hir_node_t *node) {
  return node
             ? (psx_hir_compound_operator_t)node->integer_value
             : PSX_HIR_COMPOUND_ADD;
}

int psx_hir_node_storage_offset(const psx_hir_node_t *node) {
  return node ? node->storage_offset : 0;
}

int psx_hir_node_object_offset(const psx_hir_node_t *node) {
  return node ? node->object_offset : 0;
}

int psx_hir_node_object_size(const psx_hir_node_t *node) {
  return node ? node->object_size : 0;
}

int psx_hir_node_object_align(const psx_hir_node_t *node) {
  return node ? node->object_align : 0;
}

int psx_hir_node_member_offset(const psx_hir_node_t *node) {
  return node ? node->member_offset : 0;
}

int psx_hir_node_member_from_pointer(const psx_hir_node_t *node) {
  return node && node->member_from_pointer;
}

int psx_hir_node_is_source_assignment(const psx_hir_node_t *node) {
  return node && node->is_source_assignment;
}

int psx_hir_node_is_declaration_initializer(
    const psx_hir_node_t *node) {
  return node && node->is_declaration_initializer;
}

int psx_hir_node_vla_stride_frame_offset(const psx_hir_node_t *node) {
  return node ? node->vla_stride_frame_offset : 0;
}

int psx_hir_node_vla_stride_source_offset(const psx_hir_node_t *node) {
  return node ? node->vla_stride_source_offset : 0;
}

int psx_hir_node_vla_stride_element_size(const psx_hir_node_t *node) {
  return node ? node->vla_stride_element_size : 0;
}

int psx_hir_node_vla_stride_slot_size(const psx_hir_node_t *node) {
  return node ? node->vla_stride_slot_size : 0;
}

size_t psx_hir_node_vla_dimension_count(const psx_hir_node_t *node) {
  return node ? node->vla_dimension_count : 0;
}

int psx_hir_node_vla_dimension_constant(
    const psx_hir_node_t *node, size_t index) {
  return node && index < node->vla_dimension_count
             ? node->vla_dimension_constants[index] : 0;
}

int psx_hir_node_vla_dimension_source_offset(
    const psx_hir_node_t *node, size_t index) {
  return node && index < node->vla_dimension_count
             ? node->vla_dimension_source_offsets[index] : 0;
}

size_t psx_hir_node_vla_runtime_store_count(
    const psx_hir_node_t *node) {
  return node ? node->vla_runtime_store_count : 0;
}

int psx_hir_node_vla_runtime_store_offset(
    const psx_hir_node_t *node, size_t index) {
  return node && index < node->vla_runtime_store_count
             ? node->vla_runtime_store_offsets[index] : 0;
}

int psx_hir_node_vla_runtime_store_dimension(
    const psx_hir_node_t *node, size_t index) {
  return node && index < node->vla_runtime_store_count
             ? node->vla_runtime_store_dimensions[index] : -1;
}

int psx_hir_node_label_id(const psx_hir_node_t *node) {
  return node ? node->label_id : 0;
}

int psx_hir_node_is_static_function(const psx_hir_node_t *node) {
  return node && node->kind == PSX_HIR_FUNCTION
             ? node->is_static_function != 0 : 0;
}

int psx_hir_node_is_implicit_call(const psx_hir_node_t *node) {
  return node && node->kind == PSX_HIR_CALL
             ? node->is_implicit_call != 0 : 0;
}

psx_hir_symbol_id_t psx_hir_node_symbol_id(const psx_hir_node_t *node) {
  return node ? node->symbol_id : PSX_HIR_SYMBOL_ID_INVALID;
}

int psx_hir_node_bitfield_info(
    const psx_hir_node_t *node, int *bit_width, int *bit_offset,
    int *is_signed) {
  if (!node || node->bit_width == 0) return 0;
  if (bit_width) *bit_width = node->bit_width;
  if (bit_offset) *bit_offset = node->bit_offset;
  if (is_signed) *is_signed = node->bit_is_signed != 0;
  return 1;
}

const char *psx_hir_symbol_name(
    const psx_hir_symbol_t *symbol, size_t *length) {
  if (length) *length = symbol ? symbol->name_length : 0;
  return symbol ? symbol->name : NULL;
}

psx_qual_type_t psx_hir_symbol_qual_type(const psx_hir_symbol_t *symbol) {
  return symbol ? symbol->qual_type : invalid_qual_type();
}

int psx_hir_symbol_byte_size(const psx_hir_symbol_t *symbol) {
  return symbol ? symbol->byte_size : 0;
}

int psx_hir_symbol_alignment(const psx_hir_symbol_t *symbol) {
  return symbol ? symbol->alignment : 0;
}

int psx_hir_symbol_is_extern(const psx_hir_symbol_t *symbol) {
  return symbol ? symbol->is_extern != 0 : 0;
}

int psx_hir_symbol_is_static(const psx_hir_symbol_t *symbol) {
  return symbol ? symbol->is_static != 0 : 0;
}

int psx_hir_symbol_is_thread_local(const psx_hir_symbol_t *symbol) {
  return symbol ? symbol->is_thread_local != 0 : 0;
}

#ifndef HIR_HIR_H
#define HIR_HIR_H

#include <stddef.h>

#include "../type_system/type_ids.h"

typedef unsigned int psx_hir_node_id_t;
typedef unsigned int psx_hir_symbol_id_t;

#define PSX_HIR_NODE_ID_INVALID ((psx_hir_node_id_t)0)
#define PSX_HIR_SYMBOL_ID_INVALID ((psx_hir_symbol_id_t)0)

typedef enum {
  PSX_HIR_ADD,
  PSX_HIR_SUB,
  PSX_HIR_MUL,
  PSX_HIR_DIV,
  PSX_HIR_MOD,
  PSX_HIR_EQ,
  PSX_HIR_NE,
  PSX_HIR_LT,
  PSX_HIR_LE,
  PSX_HIR_BITAND,
  PSX_HIR_BITXOR,
  PSX_HIR_BITOR,
  PSX_HIR_SHL,
  PSX_HIR_SHR,
  PSX_HIR_LOGAND,
  PSX_HIR_LOGOR,
  PSX_HIR_TERNARY,
  PSX_HIR_COMMA,
  PSX_HIR_ASSIGN,
  PSX_HIR_OBJECT_COPY,
  PSX_HIR_COMPOUND_ASSIGN,
  PSX_HIR_LOCAL,
  PSX_HIR_IF,
  PSX_HIR_WHILE,
  PSX_HIR_DO_WHILE,
  PSX_HIR_FOR,
  PSX_HIR_SWITCH,
  PSX_HIR_CASE,
  PSX_HIR_DEFAULT,
  PSX_HIR_BREAK,
  PSX_HIR_CONTINUE,
  PSX_HIR_GOTO,
  PSX_HIR_LABEL,
  PSX_HIR_PRE_INC,
  PSX_HIR_PRE_DEC,
  PSX_HIR_POST_INC,
  PSX_HIR_POST_DEC,
  PSX_HIR_RETURN,
  PSX_HIR_NOP,
  PSX_HIR_BLOCK,
  PSX_HIR_FUNCTION,
  PSX_HIR_CALL,
  PSX_HIR_FUNCTION_REF,
  PSX_HIR_DEREF,
  PSX_HIR_SUBSCRIPT,
  PSX_HIR_MEMBER_ACCESS,
  PSX_HIR_ADDRESS,
  PSX_HIR_STRING,
  PSX_HIR_NUMBER,
  PSX_HIR_GLOBAL,
  PSX_HIR_VLA_ALLOC,
  PSX_HIR_FP_TO_INT,
  PSX_HIR_INT_TO_FP,
  PSX_HIR_NEGATE,
  PSX_HIR_LOGICAL_NOT,
  /* Target-independent request for the current C variadic cursor. */
  PSX_HIR_VARARG_CURSOR,
  PSX_HIR_CAST,
  PSX_HIR_CREAL,
  PSX_HIR_CIMAG,
  PSX_HIR_STMT_EXPR,
  PSX_HIR_INITIALIZER_LIST,
  PSX_HIR_INITIALIZER_ENTRY,
  PSX_HIR_MEMBER_DESIGNATOR,
  PSX_HIR_INDEX_DESIGNATOR,
} psx_hir_node_kind_t;

typedef enum {
  PSX_HIR_COMPOUND_ADD,
  PSX_HIR_COMPOUND_SUB,
  PSX_HIR_COMPOUND_MUL,
  PSX_HIR_COMPOUND_DIV,
  PSX_HIR_COMPOUND_MOD,
  PSX_HIR_COMPOUND_SHL,
  PSX_HIR_COMPOUND_SHR,
  PSX_HIR_COMPOUND_BITAND,
  PSX_HIR_COMPOUND_BITXOR,
  PSX_HIR_COMPOUND_BITOR,
} psx_hir_compound_operator_t;

typedef enum {
  PSX_HIR_ROLE_STATEMENT,
  PSX_HIR_ROLE_EXPRESSION,
} psx_hir_node_role_t;

typedef enum {
  PSX_HIR_EDGE_LHS,
  PSX_HIR_EDGE_RHS,
  PSX_HIR_EDGE_BLOCK_ITEM,
  PSX_HIR_EDGE_PARAMETER,
  PSX_HIR_EDGE_FUNCTION_BODY,
  PSX_HIR_EDGE_CALLEE,
  PSX_HIR_EDGE_ARGUMENT,
  PSX_HIR_EDGE_INIT,
  PSX_HIR_EDGE_INCREMENT,
  PSX_HIR_EDGE_ELSE,
  PSX_HIR_EDGE_VLA_DIMENSION,
  PSX_HIR_EDGE_INITIALIZER_ENTRY,
  PSX_HIR_EDGE_DESIGNATOR,
  PSX_HIR_EDGE_INITIALIZER_VALUE,
  PSX_HIR_EDGE_DESIGNATOR_INDEX,
  PSX_HIR_EDGE_DESIGNATOR_RANGE_END,
} psx_hir_edge_kind_t;

typedef struct psx_hir_node_t psx_hir_node_t;
typedef struct psx_hir_module_t psx_hir_module_t;
typedef struct psx_hir_symbol_t psx_hir_symbol_t;

psx_hir_module_t *psx_hir_module_create(void);
void psx_hir_module_destroy(psx_hir_module_t *module);
void psx_hir_module_reset(psx_hir_module_t *module);

size_t psx_hir_module_node_count(const psx_hir_module_t *module);
size_t psx_hir_module_root_count(const psx_hir_module_t *module);
size_t psx_hir_module_symbol_count(const psx_hir_module_t *module);
psx_hir_node_id_t psx_hir_module_root_at(
    const psx_hir_module_t *module, size_t index);
const psx_hir_node_t *psx_hir_module_lookup(
    const psx_hir_module_t *module, psx_hir_node_id_t id);
const psx_hir_symbol_t *psx_hir_module_symbol_lookup(
    const psx_hir_module_t *module, psx_hir_symbol_id_t id);

psx_hir_node_kind_t psx_hir_node_kind(const psx_hir_node_t *node);
psx_hir_node_role_t psx_hir_node_role(const psx_hir_node_t *node);
psx_qual_type_t psx_hir_node_qual_type(const psx_hir_node_t *node);
psx_qual_type_t psx_hir_node_attached_qual_type(
    const psx_hir_node_t *node);
size_t psx_hir_node_child_count(const psx_hir_node_t *node);
psx_hir_node_id_t psx_hir_node_child_at(
    const psx_hir_node_t *node, size_t index);
psx_hir_edge_kind_t psx_hir_node_child_edge_at(
    const psx_hir_node_t *node, size_t index);
const char *psx_hir_node_name(
    const psx_hir_node_t *node, size_t *length);
const char *psx_hir_node_literal_contents(
    const psx_hir_node_t *node, size_t *length);
long long psx_hir_node_integer_value(const psx_hir_node_t *node);
double psx_hir_node_floating_value(const psx_hir_node_t *node);
psx_hir_compound_operator_t psx_hir_node_compound_operator(
    const psx_hir_node_t *node);
int psx_hir_node_storage_offset(const psx_hir_node_t *node);
int psx_hir_node_object_offset(const psx_hir_node_t *node);
int psx_hir_node_object_size(const psx_hir_node_t *node);
int psx_hir_node_object_align(const psx_hir_node_t *node);
int psx_hir_node_member_offset(const psx_hir_node_t *node);
int psx_hir_node_member_from_pointer(const psx_hir_node_t *node);
int psx_hir_node_vla_stride_frame_offset(const psx_hir_node_t *node);
int psx_hir_node_vla_stride_source_offset(const psx_hir_node_t *node);
int psx_hir_node_vla_stride_element_size(const psx_hir_node_t *node);
int psx_hir_node_vla_stride_slot_size(const psx_hir_node_t *node);
size_t psx_hir_node_vla_dimension_count(const psx_hir_node_t *node);
int psx_hir_node_vla_dimension_constant(
    const psx_hir_node_t *node, size_t index);
int psx_hir_node_vla_dimension_source_offset(
    const psx_hir_node_t *node, size_t index);
size_t psx_hir_node_vla_runtime_store_count(
    const psx_hir_node_t *node);
int psx_hir_node_vla_runtime_store_offset(
    const psx_hir_node_t *node, size_t index);
int psx_hir_node_vla_runtime_store_dimension(
    const psx_hir_node_t *node, size_t index);
int psx_hir_node_label_id(const psx_hir_node_t *node);
int psx_hir_node_is_static_function(const psx_hir_node_t *node);
int psx_hir_node_is_implicit_call(const psx_hir_node_t *node);
int psx_hir_node_is_source_assignment(const psx_hir_node_t *node);
int psx_hir_node_is_declaration_initializer(
    const psx_hir_node_t *node);
psx_hir_symbol_id_t psx_hir_node_symbol_id(const psx_hir_node_t *node);
int psx_hir_node_bitfield_info(
    const psx_hir_node_t *node, int *bit_width, int *bit_offset,
    int *is_signed);

const char *psx_hir_symbol_name(
    const psx_hir_symbol_t *symbol, size_t *length);
psx_qual_type_t psx_hir_symbol_qual_type(const psx_hir_symbol_t *symbol);
int psx_hir_symbol_byte_size(const psx_hir_symbol_t *symbol);
int psx_hir_symbol_alignment(const psx_hir_symbol_t *symbol);
int psx_hir_symbol_is_extern(const psx_hir_symbol_t *symbol);
int psx_hir_symbol_is_static(const psx_hir_symbol_t *symbol);
int psx_hir_symbol_is_thread_local(const psx_hir_symbol_t *symbol);

#endif

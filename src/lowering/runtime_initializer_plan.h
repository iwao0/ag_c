#ifndef LOWERING_RUNTIME_INITIALIZER_PLAN_H
#define LOWERING_RUNTIME_INITIALIZER_PLAN_H

#include "../parser/ast.h"
#include "../type_system/type_ids.h"

typedef struct psx_lowering_context_t psx_lowering_context_t;

typedef struct {
  struct lvar_t *local;
  int relative_offset;
  psx_qual_type_t qual_type;
  unsigned char bit_width;
  unsigned char bit_offset;
  unsigned char bit_is_signed;
} psx_runtime_initializer_local_ref_t;

typedef enum {
  PSX_RUNTIME_INITIALIZER_VALUE_EXPRESSION = 0,
  PSX_RUNTIME_INITIALIZER_VALUE_LOCAL,
  PSX_RUNTIME_INITIALIZER_VALUE_NUMBER,
} psx_runtime_initializer_value_kind_t;

typedef struct {
  psx_runtime_initializer_value_kind_t kind;
  psx_qual_type_t resolved_qual_type;
  union {
    const node_t *expression;
    psx_runtime_initializer_local_ref_t local;
    struct {
      long long integer_value;
      double floating_value;
    } number;
  };
} psx_runtime_initializer_value_t;

typedef enum {
  PSX_RUNTIME_INITIALIZER_EVALUATE = 0,
  PSX_RUNTIME_INITIALIZER_ASSIGN,
} psx_runtime_initializer_item_kind_t;

typedef struct {
  psx_runtime_initializer_item_kind_t kind;
  psx_qual_type_t result_qual_type;
  psx_runtime_initializer_local_ref_t target;
  psx_runtime_initializer_value_t value;
} psx_runtime_initializer_item_t;

typedef struct psx_runtime_initializer_plan_t {
  psx_runtime_initializer_item_t *items;
  int item_count;
} psx_runtime_initializer_plan_t;

psx_runtime_initializer_plan_t *psx_build_runtime_initializer_plan(
    psx_lowering_context_t *lowering_context,
    const node_t *lowered_initialization);

#endif

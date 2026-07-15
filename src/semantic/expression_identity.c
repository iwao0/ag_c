#include "expression_identity.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct psx_semantic_expression_table_t {
  node_t **expressions;
  size_t capacity;
  psx_semantic_expr_id_t next_id;
};

psx_semantic_expression_table_t *psx_semantic_expression_table_create(void) {
  return calloc(1, sizeof(psx_semantic_expression_table_t));
}

void psx_semantic_expression_table_destroy(
    psx_semantic_expression_table_t *table) {
  if (!table) return;
  free(table->expressions);
  free(table);
}

void psx_semantic_expression_table_reset(
    psx_semantic_expression_table_t *table) {
  if (!table) return;
  free(table->expressions);
  memset(table, 0, sizeof(*table));
}

psx_semantic_expr_id_t psx_semantic_expression_table_register(
    psx_semantic_expression_table_t *table, node_t *expression) {
  if (!table || !expression) return PSX_SEMANTIC_EXPR_ID_INVALID;
  for (psx_semantic_expr_id_t id = 1; id <= table->next_id; id++) {
    if (table->expressions[id] == expression) return id;
  }
  if (table->next_id == UINT_MAX) return PSX_SEMANTIC_EXPR_ID_INVALID;
  psx_semantic_expr_id_t id = table->next_id + 1;
  if ((size_t)id >= table->capacity) {
    size_t capacity = table->capacity ? table->capacity * 2 : 16;
    while (capacity <= (size_t)id) {
      if (capacity > SIZE_MAX / 2)
        return PSX_SEMANTIC_EXPR_ID_INVALID;
      capacity *= 2;
    }
    node_t **expressions = realloc(
        table->expressions, capacity * sizeof(*expressions));
    if (!expressions) return PSX_SEMANTIC_EXPR_ID_INVALID;
    memset(expressions + table->capacity, 0,
           (capacity - table->capacity) * sizeof(*expressions));
    table->expressions = expressions;
    table->capacity = capacity;
  }
  table->expressions[id] = expression;
  table->next_id = id;
  return id;
}

node_t *psx_semantic_expression_table_lookup(
    const psx_semantic_expression_table_t *table,
    psx_semantic_expr_id_t expression_id) {
  if (!table || expression_id == PSX_SEMANTIC_EXPR_ID_INVALID ||
      expression_id > table->next_id ||
      (size_t)expression_id >= table->capacity) {
    return NULL;
  }
  return table->expressions[expression_id];
}

#ifndef SEMANTIC_RESOLVED_FUNCTION_H
#define SEMANTIC_RESOLVED_FUNCTION_H

#include "../parser/ast.h"
#include "../parser/type.h"
#include "../type_system/type_ids.h"

typedef struct node_function_definition_t node_function_definition_t;

struct node_function_definition_t {
  node_t base;
  node_t **parameters;
  int parameter_count;
  const psx_type_t *signature;
  psx_qual_type_t signature_qual_type;
  char *name;
  int name_len;
  int is_static;
  struct lvar_t *lvars;
};

psx_qual_type_t ps_function_definition_signature_qual_type(
    const node_function_definition_t *function);
const psx_type_t *ps_function_definition_return_type(
    const node_function_definition_t *function);

#endif

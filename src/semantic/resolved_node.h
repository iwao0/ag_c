#ifndef SEMANTIC_RESOLVED_NODE_H
#define SEMANTIC_RESOLVED_NODE_H

#include "../parser/ast.h"
#include "resolved_node_kind.h"

typedef struct {
  node_t base;
  struct psx_vla_runtime_plan_t *runtime_plan;
} node_vla_alloc_t;

typedef struct node_lvar_t node_lvar_t;
struct node_lvar_t {
  node_t base;
  int offset;
  struct lvar_t *var;
};

typedef struct node_funcref_t node_funcref_t;
struct node_funcref_t {
  node_t base;
  char *funcname;
  int funcname_len;
};

typedef struct node_gvar_t node_gvar_t;
struct node_gvar_t {
  node_t base;
  struct global_var_t *symbol;
  char *name;
  int name_len;
  unsigned int is_thread_local : 1;
};

#endif

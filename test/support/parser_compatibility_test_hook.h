#ifndef TEST_SUPPORT_PARSER_COMPATIBILITY_TEST_HOOK_H
#define TEST_SUPPORT_PARSER_COMPATIBILITY_TEST_HOOK_H

#include "../../src/frontend/translation_unit.h"

typedef struct node_t node_t;
typedef struct psx_type_t psx_type_t;
typedef struct psx_global_registry_t psx_global_registry_t;
typedef struct global_var_t global_var_t;

int ps_global_registry_bind_decl_type(
    psx_global_registry_t *registry, global_var_t *global,
    const psx_type_t *type);
int ps_global_registry_complete_array_type(
    psx_global_registry_t *registry, global_var_t *global,
    const psx_type_t *complete_type);

int psx_test_frontend_next_function_compatibility_tree(
    psx_frontend_stream_t *stream,
    psx_frontend_function_t *function,
    node_t **compatibility_root);

#endif

#ifndef SEMANTIC_PARSER_TYPE_COMPATIBILITY_H
#define SEMANTIC_PARSER_TYPE_COMPATIBILITY_H

#include "../parser/node_fwd.h"
#include "../type_system/type_ids.h"

typedef struct psx_resolution_store_t psx_resolution_store_t;
typedef struct psx_type_t psx_type_t;

psx_qual_type_t psx_resolution_store_intern_type(
    psx_resolution_store_t *store, const psx_type_t *type);
const psx_type_t *ps_node_get_type(
    const psx_resolution_store_t *store, const node_t *node);
void ps_node_bind_type(
    psx_resolution_store_t *store, node_t *node,
    const psx_type_t *type);

#endif

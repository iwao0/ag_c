#ifndef SEMANTIC_CASE_LABEL_RESOLUTION_H
#define SEMANTIC_CASE_LABEL_RESOLUTION_H

#include "../parser/node_fwd.h"

typedef struct psx_resolution_store_t psx_resolution_store_t;

void psx_case_label_bind_value(
    psx_resolution_store_t *store,
    node_case_t *case_node, long long value);
int psx_case_label_is_resolved(
    const psx_resolution_store_t *store,
    const node_case_t *case_node);
long long psx_case_label_value(
    const psx_resolution_store_t *store,
    const node_case_t *case_node);

#endif

#ifndef SEMANTIC_CASE_LABEL_RESOLUTION_H
#define SEMANTIC_CASE_LABEL_RESOLUTION_H

#include "../parser/node_fwd.h"

void psx_case_label_bind_value(node_case_t *case_node, long long value);
int psx_case_label_is_resolved(const node_case_t *case_node);
long long psx_case_label_value(const node_case_t *case_node);

#endif

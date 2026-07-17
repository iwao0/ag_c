#ifndef FRONTEND_TRANSLATION_UNIT_INTERNAL_H
#define FRONTEND_TRANSLATION_UNIT_INTERNAL_H

#include "translation_unit.h"

typedef struct psx_resolution_work_tree_t psx_resolution_work_tree_t;

int psx_frontend_next_function_work_tree(
    psx_frontend_stream_t *stream,
    psx_frontend_function_t *function,
    psx_resolution_work_tree_t **work_tree);

#endif

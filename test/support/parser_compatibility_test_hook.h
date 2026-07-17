#ifndef TEST_SUPPORT_PARSER_COMPATIBILITY_TEST_HOOK_H
#define TEST_SUPPORT_PARSER_COMPATIBILITY_TEST_HOOK_H

#include "../../src/frontend/translation_unit.h"

typedef struct node_t node_t;

int psx_test_frontend_next_function_compatibility_tree(
    psx_frontend_stream_t *stream,
    psx_frontend_function_t *function,
    node_t **compatibility_root);

#endif

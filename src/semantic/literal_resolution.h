#ifndef SEMANTIC_LITERAL_RESOLUTION_H
#define SEMANTIC_LITERAL_RESOLUTION_H

typedef struct node_string_t node_string_t;

void psx_string_literal_bind_label(
    node_string_t *literal, char *label);
char *psx_string_literal_label(const node_string_t *literal);

#endif

#ifndef PARSER_ANON_TAG_H
#define PARSER_ANON_TAG_H

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

void psx_make_anonymous_tag_name_in(
    psx_parser_runtime_context_t *runtime_context,
    char **out_name, int *out_len);

#endif

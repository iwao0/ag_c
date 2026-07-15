#include "anon_tag.h"
#include "runtime_context.h"
#include <stdio.h>
#include <stdlib.h>

void psx_make_anonymous_tag_name_in(
    psx_parser_runtime_context_t *runtime_context,
    char **out_name, int *out_len) {
  if (!runtime_context || !out_name || !out_len) return;
  int seq = runtime_context->anonymous_tag_seq++;
  int len = snprintf(NULL, 0, "__anon_tag_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__anon_tag_%d", seq);
  *out_name = name;
  *out_len = len;
}

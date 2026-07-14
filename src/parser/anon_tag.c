#include "anon_tag.h"
#include "runtime_context.h"
#include <stdio.h>
#include <stdlib.h>

void ps_anon_tag_reset_translation_unit_state(void) {
  ps_parser_runtime_context_active()->anonymous_tag_seq = 0;
}

void psx_make_anonymous_tag_name(char **out_name, int *out_len) {
  int seq = ps_parser_runtime_context_active()->anonymous_tag_seq++;
  int len = snprintf(NULL, 0, "__anon_tag_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__anon_tag_%d", seq);
  *out_name = name;
  *out_len = len;
}

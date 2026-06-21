#include "anon_tag.h"
#include <stdio.h>
#include <stdlib.h>

static int anonymous_tag_seq = 0;

void psx_make_anonymous_tag_name(char **out_name, int *out_len) {
  int seq = anonymous_tag_seq++;
  int len = snprintf(NULL, 0, "__anon_tag_%d", seq);
  char *name = calloc((size_t)len + 1, 1);
  snprintf(name, (size_t)len + 1, "__anon_tag_%d", seq);
  *out_name = name;
  *out_len = len;
}

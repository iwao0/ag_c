#ifndef PARSER_DYNARRAY_H
#define PARSER_DYNARRAY_H

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../diag/diag.h"

static inline int pda_next_cap(int current_cap, int required_cap) {
  if (required_cap < 0) {
    diag_emit_internalf(DIAG_ERR_PARSER_DYNARRAY_INVALID_SIZE, "%s",
                        diag_message_for(DIAG_ERR_PARSER_DYNARRAY_INVALID_SIZE));
  }
  int cap = current_cap;
  if (cap < 16) cap = 16;
  while (cap < required_cap) {
    if (cap > INT_MAX / 2) {
      diag_emit_internalf(DIAG_ERR_PARSER_DYNARRAY_TOO_LARGE, "%s",
                          diag_message_for(DIAG_ERR_PARSER_DYNARRAY_TOO_LARGE));
    }
    cap *= 2;
  }
  return cap;
}

static inline void *pda_xreallocarray(void *ptr, size_t n, size_t size) {
  if (n != 0 && size > SIZE_MAX / n) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s",
                        diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  void *p = realloc(ptr, n * size);
  if (!p) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s",
                        diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  return p;
}

#endif

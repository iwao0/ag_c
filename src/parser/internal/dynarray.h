#ifndef PARSER_DYNARRAY_H
#define PARSER_DYNARRAY_H

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static inline int pda_next_cap(int current_cap, int required_cap) {
  if (required_cap < 0) {
    fprintf(stderr, "配列サイズが不正です\n");
    exit(1);
  }
  int cap = current_cap;
  if (cap < 16) cap = 16;
  while (cap < required_cap) {
    if (cap > INT_MAX / 2) {
      fprintf(stderr, "配列サイズが大きすぎます\n");
      exit(1);
    }
    cap *= 2;
  }
  return cap;
}

static inline void *pda_xreallocarray(void *ptr, size_t n, size_t size) {
  if (n != 0 && size > SIZE_MAX / n) {
    fprintf(stderr, "メモリ確保に失敗しました\n");
    exit(1);
  }
  void *p = realloc(ptr, n * size);
  if (!p) {
    fprintf(stderr, "メモリ確保に失敗しました\n");
    exit(1);
  }
  return p;
}

#endif

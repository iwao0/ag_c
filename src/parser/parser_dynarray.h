#ifndef PARSER_DYNARRAY_H
#define PARSER_DYNARRAY_H

static inline int pda_next_cap(int current_cap, int required_cap) {
  int cap = current_cap;
  if (cap < 16) cap = 16;
  while (cap < required_cap) {
    cap *= 2;
  }
  return cap;
}

#endif

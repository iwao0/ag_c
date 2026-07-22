#ifndef PARSER_INIT_SLOT_H
#define PARSER_INIT_SLOT_H

#include "core.h"

typedef struct psx_gvar_init_slot_t {
  int in_range;
  int relative_offset;
  char *symbol;
  int symbol_len;
  long long value;
  double fvalue;
  tk_float_kind_t fp_sentinel_kind;
} psx_gvar_init_slot_t;

#endif

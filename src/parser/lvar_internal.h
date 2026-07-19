#ifndef PARSER_LVAR_INTERNAL_H
#define PARSER_LVAR_INTERNAL_H

#include "lvar_public.h"
#include "vla_runtime.h"

struct lvar_t {
  lvar_t *next_all;
  lvar_t *next_binding;
  lvar_t *next_offhash;
  char *name;
  int len;
  int offset;
  int size;
  unsigned int is_byref_param : 1;
  unsigned int is_used : 1;
  unsigned int is_unevaluated_used : 1;
  unsigned int is_address_taken : 1;
  unsigned int suppress_unreachable_warnings : 1;
  unsigned int is_param : 1;
  unsigned int is_initialized : 1;
  unsigned int is_static_local : 1;
  struct global_var_t *static_global;
  char *static_global_name;
  int static_global_name_len;
  int align_bytes;
  int used_count;
  const psx_semantic_type_table_t *decl_type_table;
  psx_qual_type_t decl_qual_type;
  psx_vla_runtime_descriptor_t vla_runtime;
  psx_lvar_usage_region_t *decl_region;
};

#endif

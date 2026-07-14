#ifndef PARSER_TAG_MEMBER_PUBLIC_H
#define PARSER_TAG_MEMBER_PUBLIC_H

#include "core.h"
#include "type.h"
#include <stdbool.h>

/* struct/union member descriptor. Type-derived properties are queried from
 * decl_type so this object cannot become a second source of type truth. */
typedef struct tag_member_info_t {
  char *name;
  int len;
  int offset;
  int bit_width;
  int bit_offset;
  int bit_is_signed;
  const psx_type_t *decl_type;
} tag_member_info_t;

static inline const psx_type_t *ps_tag_member_decl_type(
    const tag_member_info_t *m) {
  return m ? m->decl_type : NULL;
}

static inline const psx_type_t *ps_tag_member_decl_value_type(
    const tag_member_info_t *m) {
  return ps_type_array_leaf_type(ps_tag_member_decl_type(m));
}

static inline int ps_tag_member_decl_value_size(const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) {
    int size = ps_type_sizeof(type);
    if (size > 0) return size;
  }
  return 0;
}

static inline int ps_tag_member_decl_storage_size(const tag_member_info_t *m) {
  if (!m) return 0;
  const psx_type_t *decl_type = ps_tag_member_decl_type(m);
  if (decl_type) {
    int size = ps_type_sizeof(decl_type);
    if (size > 0) return size;
  }
  return 0;
}

static inline tk_float_kind_t ps_tag_member_decl_fp_kind(
    const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) {
    return (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)
               ? type->fp_kind
               : TK_FLOAT_KIND_NONE;
  }
  return TK_FLOAT_KIND_NONE;
}

static inline int ps_tag_member_decl_is_bool(const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) return type->kind == PSX_TYPE_BOOL;
  return 0;
}

static inline int ps_tag_member_decl_is_unsigned(const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) return ps_type_is_unsigned(type);
  return 0;
}

static inline const psx_type_t *ps_tag_member_decl_tag_type(
    const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_type(m);
  while (type &&
         (type->kind == PSX_TYPE_ARRAY || type->kind == PSX_TYPE_POINTER)) {
    type = type->base;
  }
  return ps_type_is_tag_aggregate(type) ? type : NULL;
}

bool ps_ctx_get_tag_member_info(token_kind_t kind, char *name, int len, int index,
                                  tag_member_info_t *out);
bool ps_ctx_find_tag_member_info(token_kind_t kind, char *name, int len,
                                   char *member_name, int member_len,
                                   tag_member_info_t *out);
bool ps_ctx_get_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                          int scope_depth, int index,
                                          tag_member_info_t *out);
bool ps_ctx_find_tag_member_info_at_scope(token_kind_t kind, char *name, int len,
                                           int scope_depth,
                                           char *member_name, int member_len,
                                           tag_member_info_t *out);
int ps_ctx_get_tag_member_count(token_kind_t kind, char *name, int len);
int ps_ctx_get_tag_member_count_at_scope(token_kind_t kind, char *name, int len, int scope_depth);
int ps_ctx_get_tag_scope_depth(token_kind_t kind, char *name, int len);

#endif

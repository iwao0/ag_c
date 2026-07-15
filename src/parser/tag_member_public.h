#ifndef PARSER_TAG_MEMBER_PUBLIC_H
#define PARSER_TAG_MEMBER_PUBLIC_H

#include "core.h"
#include "type.h"
#include <stdbool.h>

typedef struct psx_semantic_context_t psx_semantic_context_t;

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

static inline tag_member_info_t ps_tag_member_declaration_view(
    const psx_record_member_decl_t *member) {
  return member
             ? (tag_member_info_t){
                   .name = member->name,
                   .len = member->len,
                   .bit_width = member->bit_width,
                   .bit_is_signed = member->bit_is_signed,
                   .decl_type = member->decl_type,
               }
             : (tag_member_info_t){0};
}

static inline const psx_type_t *ps_tag_member_decl_type(
    const tag_member_info_t *m) {
  return m ? m->decl_type : NULL;
}

static inline const psx_type_t *ps_tag_member_decl_value_type(
    const tag_member_info_t *m) {
  return ps_type_array_leaf_type(ps_tag_member_decl_type(m));
}

static inline tk_float_kind_t ps_tag_member_decl_fp_kind(
    const tag_member_info_t *m) {
  const psx_type_t *type = ps_tag_member_decl_value_type(m);
  if (type) {
    return (type->kind == PSX_TYPE_FLOAT || type->kind == PSX_TYPE_COMPLEX)
               ? ps_type_floating_token_kind(type)
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

bool ps_ctx_get_tag_member_info_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int index,
    tag_member_info_t *out);
bool ps_ctx_find_tag_member_info_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    char *member_name, int member_len, tag_member_info_t *out);
bool ps_ctx_get_tag_member_info_at_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len,
    int scope_depth, int index, tag_member_info_t *out);
bool ps_ctx_find_tag_member_info_at_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int scope_depth,
    char *member_name, int member_len, tag_member_info_t *out);
int ps_ctx_get_tag_member_count_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
int ps_ctx_get_tag_member_count_at_scope_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len, int scope_depth);
int ps_ctx_get_tag_scope_depth_in(
    psx_semantic_context_t *context,
    token_kind_t kind, char *name, int len);
#endif

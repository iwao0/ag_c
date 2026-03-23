#include "internal/semantic_ctx.h"
#include "internal/diag.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>
#include <string.h>

#define PCTX_HASH_BUCKETS 256

typedef struct goto_ref_t goto_ref_t;
struct goto_ref_t {
  goto_ref_t *next_all;
  char *name;
  int len;
  token_t *tok;
};

typedef struct label_def_t label_def_t;
struct label_def_t {
  label_def_t *next_hash;
  char *name;
  int len;
  token_t *tok;
};

typedef struct tag_type_t tag_type_t;
struct tag_type_t {
  tag_type_t *next_hash;
  token_kind_t kind;
  char *name;
  int len;
  int member_count;
  int size;
  int scope_depth;
};
typedef struct tag_member_t tag_member_t;
struct tag_member_t {
  tag_member_t *next_hash;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  char *member_name;
  int member_len;
  int offset;
  int type_size;
  int deref_size;
  int array_len;
  token_kind_t member_tag_kind;
  char *member_tag_name;
  int member_tag_len;
  int member_is_tag_pointer;
  int bit_width;    // ビットフィールド幅（0: 非ビットフィールド）
  int bit_offset;   // ストレージユニット内ビット位置
  int bit_is_signed;
  int decl_order;
  int scope_depth;
};

typedef struct enum_const_t enum_const_t;
struct enum_const_t {
  enum_const_t *next_hash;
  char *name;
  int len;
  long long value;
  int scope_depth;
};
typedef struct typedef_name_t typedef_name_t;
struct typedef_name_t {
  typedef_name_t *next_hash;
  char *name;
  int len;
  token_kind_t base_kind;
  int elem_size;
  tk_float_kind_t fp_kind;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int is_pointer;
  int scope_depth;
};
typedef struct func_name_t func_name_t;
struct func_name_t {
  func_name_t *next_hash;
  char *name;
  int len;
  int ret_struct_size; // 構造体戻り値サイズ（0: 非構造体）
};

static goto_ref_t *goto_refs_all = NULL;
static label_def_t *label_defs_by_bucket[PCTX_HASH_BUCKETS];
static tag_type_t *tag_types_by_bucket[PCTX_HASH_BUCKETS];
static tag_member_t *tag_members_by_bucket[PCTX_HASH_BUCKETS];
static enum_const_t *enum_consts_by_bucket[PCTX_HASH_BUCKETS];
static typedef_name_t *typedefs_by_bucket[PCTX_HASH_BUCKETS];
static func_name_t *func_names_by_bucket[PCTX_HASH_BUCKETS];
static int tag_scope_depth = 0;
static int tag_member_decl_order = 0;

static unsigned psx_ctx_hash_name(const char *name, int len) {
  // djb2 variant
  unsigned h = 5381u;
  for (int i = 0; i < len; i++) {
    h = ((h << 5) + h) ^ (unsigned char)name[i];
  }
  return h & (PCTX_HASH_BUCKETS - 1u);
}

static unsigned psx_ctx_hash_tag(token_kind_t kind, const char *name, int len) {
  unsigned h = (unsigned)kind * 131u;
  for (int i = 0; i < len; i++) {
    h = (h * 33u) ^ (unsigned char)name[i];
  }
  return h & (PCTX_HASH_BUCKETS - 1u);
}

void psx_ctx_reset_function_scope(void) {
  goto_refs_all = NULL;
  memset(label_defs_by_bucket, 0, sizeof(label_defs_by_bucket));
  tag_scope_depth = 0;
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_type_t **tt = &tag_types_by_bucket[i];
    while (*tt) {
      if ((*tt)->scope_depth > 0) {
        *tt = (*tt)->next_hash;
        continue;
      }
      tt = &(*tt)->next_hash;
    }
    tag_member_t **tm = &tag_members_by_bucket[i];
    while (*tm) {
      if ((*tm)->scope_depth > 0) {
        *tm = (*tm)->next_hash;
        continue;
      }
      tm = &(*tm)->next_hash;
    }
    enum_const_t **ec = &enum_consts_by_bucket[i];
    while (*ec) {
      if ((*ec)->scope_depth > 0) {
        *ec = (*ec)->next_hash;
        continue;
      }
      ec = &(*ec)->next_hash;
    }
    typedef_name_t **td = &typedefs_by_bucket[i];
    while (*td) {
      if ((*td)->scope_depth > 0) {
        *td = (*td)->next_hash;
        continue;
      }
      td = &(*td)->next_hash;
    }
  }
}

void psx_ctx_enter_block_scope(void) {
  tag_scope_depth++;
}

void psx_ctx_leave_block_scope(void) {
  if (tag_scope_depth <= 0) return;
  int old_depth = tag_scope_depth;
  tag_scope_depth--;
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_type_t **pp = &tag_types_by_bucket[i];
    while (*pp) {
      tag_type_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    typedef_name_t **pp = &typedefs_by_bucket[i];
    while (*pp) {
      typedef_name_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    tag_member_t **pp = &tag_members_by_bucket[i];
    while (*pp) {
      tag_member_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    enum_const_t **pp = &enum_consts_by_bucket[i];
    while (*pp) {
      enum_const_t *cur = *pp;
      if (cur->scope_depth >= old_depth) {
        *pp = cur->next_hash;
        continue;
      }
      pp = &cur->next_hash;
    }
  }
}

void psx_ctx_register_goto_ref(char *name, int len, token_t *tok) {
  goto_ref_t *g = calloc(1, sizeof(goto_ref_t));
  g->name = name;
  g->len = len;
  g->tok = tok;
  g->next_all = goto_refs_all;
  goto_refs_all = g;
}

void psx_ctx_register_label_def(char *name, int len, token_t *tok) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (label_def_t *d = label_defs_by_bucket[bucket]; d; d = d->next_hash) {
    if (d->len == len && strncmp(d->name, name, (size_t)len) == 0) {
      psx_diag_duplicate_with_name(tok, diag_text_for(DIAG_TEXT_LABEL), name, len);
    }
  }
  label_def_t *d = calloc(1, sizeof(label_def_t));
  d->name = name;
  d->len = len;
  d->tok = tok;
  d->next_hash = label_defs_by_bucket[bucket];
  label_defs_by_bucket[bucket] = d;
}

void psx_ctx_validate_goto_refs(void) {
  for (goto_ref_t *g = goto_refs_all; g; g = g->next_all) {
    unsigned bucket = psx_ctx_hash_name(g->name, g->len);
    int found = 0;
    for (label_def_t *d = label_defs_by_bucket[bucket]; d; d = d->next_hash) {
      if (d->len == g->len && strncmp(d->name, g->name, (size_t)g->len) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) {
      psx_diag_ctx(g->tok, "goto", diag_message_for(DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED),
                   g->len, g->name);
    }
  }
}

bool psx_ctx_has_tag_type(token_kind_t kind, char *name, int len) {
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  for (tag_type_t *t = tag_types_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->kind == kind && t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      return true;
    }
  }
  return false;
}

void psx_ctx_define_tag_type(token_kind_t kind, char *name, int len) {
  psx_ctx_define_tag_type_with_layout(kind, name, len, 0, 0);
}

void psx_ctx_define_tag_type_with_members(token_kind_t kind, char *name, int len, int member_count) {
  psx_ctx_define_tag_type_with_layout(kind, name, len, member_count, member_count > 0 ? 8 : 0);
}

void psx_ctx_define_tag_type_with_layout(token_kind_t kind, char *name, int len, int member_count, int tag_size) {
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  for (tag_type_t *t = tag_types_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->kind == kind && t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      if (member_count > t->member_count) t->member_count = member_count;
      if (tag_size > t->size) t->size = tag_size;
      return;
    }
  }
  tag_type_t *t = calloc(1, sizeof(tag_type_t));
  t->kind = kind;
  t->name = name;
  t->len = len;
  t->member_count = member_count;
  t->size = tag_size;
  t->scope_depth = tag_scope_depth;
  t->next_hash = tag_types_by_bucket[bucket];
  tag_types_by_bucket[bucket] = t;
}

int psx_ctx_get_tag_member_count(token_kind_t kind, char *name, int len) {
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  for (tag_type_t *t = tag_types_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->kind == kind && t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      return t->member_count;
    }
  }
  return -1;
}

int psx_ctx_get_tag_size(token_kind_t kind, char *name, int len) {
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  for (tag_type_t *t = tag_types_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->kind == kind && t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      return t->size;
    }
  }
  return -1;
}

void psx_ctx_add_tag_member_bf(token_kind_t tag_kind, char *tag_name, int tag_len,
                               char *member_name, int member_len, int offset,
                               int type_size, int deref_size, int array_len,
                               token_kind_t member_tag_kind, char *member_tag_name,
                               int member_tag_len, int member_is_tag_pointer,
                               int bit_width, int bit_offset, int bit_is_signed) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0 &&
        m->scope_depth == tag_scope_depth) {
      m->offset = offset;
      m->type_size = type_size;
      m->deref_size = deref_size;
      m->array_len = array_len;
      m->member_tag_kind = member_tag_kind;
      m->member_tag_name = member_tag_name;
      m->member_tag_len = member_tag_len;
      m->member_is_tag_pointer = member_is_tag_pointer;
      m->bit_width = bit_width;
      m->bit_offset = bit_offset;
      m->bit_is_signed = bit_is_signed;
      return;
    }
  }
  tag_member_t *m = calloc(1, sizeof(tag_member_t));
  m->tag_kind = tag_kind;
  m->tag_name = tag_name;
  m->tag_len = tag_len;
  m->member_name = member_name;
  m->member_len = member_len;
  m->offset = offset;
  m->type_size = type_size;
  m->deref_size = deref_size;
  m->array_len = array_len;
  m->member_tag_kind = member_tag_kind;
  m->member_tag_name = member_tag_name;
  m->member_tag_len = member_tag_len;
  m->member_is_tag_pointer = member_is_tag_pointer;
  m->bit_width = bit_width;
  m->bit_offset = bit_offset;
  m->bit_is_signed = bit_is_signed;
  m->decl_order = tag_member_decl_order++;
  m->scope_depth = tag_scope_depth;
  m->next_hash = tag_members_by_bucket[bucket];
  tag_members_by_bucket[bucket] = m;
}

void psx_ctx_add_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                            char *member_name, int member_len, int offset,
                            int type_size, int deref_size, int array_len,
                            token_kind_t member_tag_kind, char *member_tag_name,
                            int member_tag_len, int member_is_tag_pointer) {
  psx_ctx_add_tag_member_bf(tag_kind, tag_name, tag_len,
                            member_name, member_len, offset,
                            type_size, deref_size, array_len,
                            member_tag_kind, member_tag_name, member_tag_len,
                            member_is_tag_pointer, 0, 0, 0);
}

bool psx_ctx_get_tag_member_bf(token_kind_t tag_kind, char *tag_name, int tag_len,
                               char *member_name, int member_len,
                               int *out_bit_width, int *out_bit_offset, int *out_bit_is_signed) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      if (out_bit_width) *out_bit_width = m->bit_width;
      if (out_bit_offset) *out_bit_offset = m->bit_offset;
      if (out_bit_is_signed) *out_bit_is_signed = m->bit_is_signed;
      return true;
    }
  }
  return false;
}

static int cmp_tag_member_ptr(const void *a, const void *b) {
  const tag_member_t *ma = *(const tag_member_t * const *)a;
  const tag_member_t *mb = *(const tag_member_t * const *)b;
  if (ma->offset != mb->offset) return (ma->offset < mb->offset) ? -1 : 1;
  if (ma->decl_order != mb->decl_order) return (ma->decl_order < mb->decl_order) ? -1 : 1;
  return 0;
}

bool psx_ctx_get_tag_member_at(token_kind_t tag_kind, char *tag_name, int tag_len, int index,
                               char **out_member_name, int *out_member_len,
                               int *out_offset, int *out_type_size, int *out_deref_size, int *out_array_len,
                               token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                               int *out_member_tag_len, int *out_member_is_tag_pointer) {
  int cap = 8;
  int n = 0;
  tag_member_t **members = calloc((size_t)cap, sizeof(tag_member_t *));
  for (int i = 0; i < PCTX_HASH_BUCKETS; i++) {
    for (tag_member_t *m = tag_members_by_bucket[i]; m; m = m->next_hash) {
      if (m->tag_kind != tag_kind || m->tag_len != tag_len) continue;
      if (strncmp(m->tag_name, tag_name, (size_t)tag_len) != 0) continue;
      if (n >= cap) {
        cap *= 2;
        members = realloc(members, (size_t)cap * sizeof(tag_member_t *));
      }
      members[n++] = m;
    }
  }
  if (n == 0 || index < 0 || index >= n) {
    free(members);
    return false;
  }
  qsort(members, (size_t)n, sizeof(tag_member_t *), cmp_tag_member_ptr);
  tag_member_t *m = members[index];
  if (out_member_name) *out_member_name = m->member_name;
  if (out_member_len) *out_member_len = m->member_len;
  if (out_offset) *out_offset = m->offset;
  if (out_type_size) *out_type_size = m->type_size;
  if (out_deref_size) *out_deref_size = m->deref_size;
  if (out_array_len) *out_array_len = m->array_len;
  if (out_member_tag_kind) *out_member_tag_kind = m->member_tag_kind;
  if (out_member_tag_name) *out_member_tag_name = m->member_tag_name;
  if (out_member_tag_len) *out_member_tag_len = m->member_tag_len;
  if (out_member_is_tag_pointer) *out_member_is_tag_pointer = m->member_is_tag_pointer;
  free(members);
  return true;
}

bool psx_ctx_find_tag_member(token_kind_t tag_kind, char *tag_name, int tag_len,
                             char *member_name, int member_len,
                             int *out_offset, int *out_type_size, int *out_deref_size, int *out_array_len,
                             token_kind_t *out_member_tag_kind, char **out_member_tag_name,
                             int *out_member_tag_len, int *out_member_is_tag_pointer) {
  unsigned bucket = (psx_ctx_hash_tag(tag_kind, tag_name, tag_len) ^
                     psx_ctx_hash_name(member_name, member_len)) & (PCTX_HASH_BUCKETS - 1u);
  for (tag_member_t *m = tag_members_by_bucket[bucket]; m; m = m->next_hash) {
    if (m->tag_kind == tag_kind && m->tag_len == tag_len &&
        m->member_len == member_len &&
        strncmp(m->tag_name, tag_name, (size_t)tag_len) == 0 &&
        strncmp(m->member_name, member_name, (size_t)member_len) == 0) {
      if (out_offset) *out_offset = m->offset;
      if (out_type_size) *out_type_size = m->type_size;
      if (out_deref_size) *out_deref_size = m->deref_size;
      if (out_array_len) *out_array_len = m->array_len;
      if (out_member_tag_kind) *out_member_tag_kind = m->member_tag_kind;
      if (out_member_tag_name) *out_member_tag_name = m->member_tag_name;
      if (out_member_tag_len) *out_member_tag_len = m->member_tag_len;
      if (out_member_is_tag_pointer) *out_member_is_tag_pointer = m->member_is_tag_pointer;
      return true;
    }
  }
  return false;
}

void psx_ctx_define_enum_const(char *name, int len, long long value) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (enum_const_t *e = enum_consts_by_bucket[bucket]; e; e = e->next_hash) {
    if (e->scope_depth == tag_scope_depth && e->len == len &&
        strncmp(e->name, name, (size_t)len) == 0) {
      e->value = value;
      return;
    }
  }
  enum_const_t *e = calloc(1, sizeof(enum_const_t));
  e->name = name;
  e->len = len;
  e->value = value;
  e->scope_depth = tag_scope_depth;
  e->next_hash = enum_consts_by_bucket[bucket];
  enum_consts_by_bucket[bucket] = e;
}

bool psx_ctx_find_enum_const(char *name, int len, long long *out_value) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (enum_const_t *e = enum_consts_by_bucket[bucket]; e; e = e->next_hash) {
    if (e->len == len && strncmp(e->name, name, (size_t)len) == 0) {
      if (out_value) *out_value = e->value;
      return true;
    }
  }
  return false;
}

void psx_ctx_define_typedef_name(char *name, int len, token_kind_t base_kind, int elem_size,
                                 tk_float_kind_t fp_kind, token_kind_t tag_kind,
                                 char *tag_name, int tag_len, int is_pointer) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (typedef_name_t *t = typedefs_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->scope_depth == tag_scope_depth && t->len == len &&
        strncmp(t->name, name, (size_t)len) == 0) {
      t->base_kind = base_kind;
      t->elem_size = elem_size;
      t->fp_kind = fp_kind;
      t->tag_kind = tag_kind;
      t->tag_name = tag_name;
      t->tag_len = tag_len;
      t->is_pointer = is_pointer;
      return;
    }
  }
  typedef_name_t *t = calloc(1, sizeof(typedef_name_t));
  t->name = name;
  t->len = len;
  t->base_kind = base_kind;
  t->elem_size = elem_size;
  t->fp_kind = fp_kind;
  t->tag_kind = tag_kind;
  t->tag_name = tag_name;
  t->tag_len = tag_len;
  t->is_pointer = is_pointer;
  t->scope_depth = tag_scope_depth;
  t->next_hash = typedefs_by_bucket[bucket];
  typedefs_by_bucket[bucket] = t;
}

bool psx_ctx_find_typedef_name(char *name, int len, token_kind_t *out_base_kind,
                               int *out_elem_size, tk_float_kind_t *out_fp_kind,
                               token_kind_t *out_tag_kind, char **out_tag_name,
                               int *out_tag_len, int *out_is_pointer) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (typedef_name_t *t = typedefs_by_bucket[bucket]; t; t = t->next_hash) {
    if (t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      if (out_base_kind) *out_base_kind = t->base_kind;
      if (out_elem_size) *out_elem_size = t->elem_size;
      if (out_fp_kind) *out_fp_kind = t->fp_kind;
      if (out_tag_kind) *out_tag_kind = t->tag_kind;
      if (out_tag_name) *out_tag_name = t->tag_name;
      if (out_tag_len) *out_tag_len = t->tag_len;
      if (out_is_pointer) *out_is_pointer = t->is_pointer;
      return true;
    }
  }
  return false;
}

bool psx_ctx_is_typedef_name_token(token_t *tok) {
  if (!tok || tok->kind != TK_IDENT) return false;
  token_ident_t *id = (token_ident_t *)tok;
  return psx_ctx_find_typedef_name(id->str, id->len, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

void psx_ctx_define_function_name(char *name, int len) {
  psx_ctx_define_function_name_with_ret(name, len, 0);
}

void psx_ctx_define_function_name_with_ret(char *name, int len, int ret_struct_size) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (func_name_t *f = func_names_by_bucket[bucket]; f; f = f->next_hash) {
    if (f->len == len && strncmp(f->name, name, (size_t)len) == 0) {
      f->ret_struct_size = ret_struct_size; // 更新
      return;
    }
  }
  func_name_t *f = calloc(1, sizeof(func_name_t));
  f->name = name;
  f->len = len;
  f->ret_struct_size = ret_struct_size;
  f->next_hash = func_names_by_bucket[bucket];
  func_names_by_bucket[bucket] = f;
}

bool psx_ctx_has_function_name(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (func_name_t *f = func_names_by_bucket[bucket]; f; f = f->next_hash) {
    if (f->len == len && strncmp(f->name, name, (size_t)len) == 0) {
      return true;
    }
  }
  return false;
}

int psx_ctx_get_function_ret_struct_size(char *name, int len) {
  unsigned bucket = psx_ctx_hash_name(name, len);
  for (func_name_t *f = func_names_by_bucket[bucket]; f; f = f->next_hash) {
    if (f->len == len && strncmp(f->name, name, (size_t)len) == 0) {
      return f->ret_struct_size;
    }
  }
  return 0;
}

bool psx_ctx_is_type_token(token_kind_t kind) {
  return kind == TK_INT || kind == TK_CHAR || kind == TK_VOID || kind == TK_SHORT ||
         kind == TK_LONG || kind == TK_FLOAT || kind == TK_DOUBLE ||
         kind == TK_BOOL || kind == TK_SIGNED || kind == TK_UNSIGNED ||
         kind == TK_COMPLEX || kind == TK_IMAGINARY;
}

bool psx_ctx_is_tag_keyword(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION || kind == TK_ENUM;
}

int psx_ctx_scalar_type_size(token_kind_t kind) {
  switch (kind) {
    case TK_CHAR: return 1;
    case TK_BOOL: return 1;
    case TK_SHORT: return 2;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_FLOAT:
      return 4;
    case TK_LONG:
    case TK_DOUBLE:
      return 8;
    default:
      return 8;
  }
}

void psx_ctx_get_type_info(token_kind_t kind, bool *is_type_token, int *scalar_size) {
  bool is_type = false;
  int size = 8;
  switch (kind) {
    case TK_CHAR:
    case TK_BOOL:
      is_type = true;
      size = 1;
      break;
    case TK_SHORT:
      is_type = true;
      size = 2;
      break;
    case TK_INT:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_FLOAT:
      is_type = true;
      size = 4;
      break;
    case TK_LONG:
    case TK_DOUBLE:
      is_type = true;
      size = 8;
      break;
    case TK_VOID:
      is_type = true;
      size = 8;
      break;
    default:
      break;
  }
  if (is_type_token) *is_type_token = is_type;
  if (scalar_size) *scalar_size = size;
}

#include "semantic_ctx.h"
#include "diag.h"
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
};

static goto_ref_t *goto_refs_all = NULL;
static label_def_t *label_defs_by_bucket[PCTX_HASH_BUCKETS];
static tag_type_t *tag_types_by_bucket[PCTX_HASH_BUCKETS];

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
  memset(tag_types_by_bucket, 0, sizeof(tag_types_by_bucket));
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
      psx_diag_duplicate_with_name(tok, "ラベル", name, len);
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
      psx_diag_ctx(g->tok, "goto", "未定義ラベル '%.*s' への goto です", g->len, g->name);
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
  if (psx_ctx_has_tag_type(kind, name, len)) return;
  tag_type_t *t = calloc(1, sizeof(tag_type_t));
  unsigned bucket = psx_ctx_hash_tag(kind, name, len);
  t->kind = kind;
  t->name = name;
  t->len = len;
  t->next_hash = tag_types_by_bucket[bucket];
  tag_types_by_bucket[bucket] = t;
}

bool psx_ctx_is_type_token(token_kind_t kind) {
  return kind == TK_INT || kind == TK_CHAR || kind == TK_VOID || kind == TK_SHORT ||
         kind == TK_LONG || kind == TK_FLOAT || kind == TK_DOUBLE;
}

bool psx_ctx_is_tag_keyword(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION || kind == TK_ENUM;
}

int psx_ctx_scalar_type_size(token_kind_t kind) {
  switch (kind) {
    case TK_CHAR: return 1;
    case TK_SHORT: return 2;
    case TK_INT:
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
      is_type = true;
      size = 1;
      break;
    case TK_SHORT:
      is_type = true;
      size = 2;
      break;
    case TK_INT:
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

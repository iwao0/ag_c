#include "parser_semantic_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>
#include <string.h>

typedef struct goto_ref_t goto_ref_t;
struct goto_ref_t {
  goto_ref_t *next;
  char *name;
  int len;
  token_t *tok;
};

typedef struct label_def_t label_def_t;
struct label_def_t {
  label_def_t *next;
  char *name;
  int len;
  token_t *tok;
};

typedef struct tag_type_t tag_type_t;
struct tag_type_t {
  tag_type_t *next;
  token_kind_t kind;
  char *name;
  int len;
};

static goto_ref_t *goto_refs = NULL;
static label_def_t *label_defs = NULL;
static tag_type_t *tag_types = NULL;

void pctx_reset_function_scope(void) {
  goto_refs = NULL;
  label_defs = NULL;
  tag_types = NULL;
}

void pctx_register_goto_ref(char *name, int len, token_t *tok) {
  goto_ref_t *g = calloc(1, sizeof(goto_ref_t));
  g->name = name;
  g->len = len;
  g->tok = tok;
  g->next = goto_refs;
  goto_refs = g;
}

void pctx_register_label_def(char *name, int len, token_t *tok) {
  for (label_def_t *d = label_defs; d; d = d->next) {
    if (d->len == len && strncmp(d->name, name, (size_t)len) == 0) {
      tk_error_tok(tok, "ラベル '%.*s' が重複しています", len, name);
    }
  }
  label_def_t *d = calloc(1, sizeof(label_def_t));
  d->name = name;
  d->len = len;
  d->tok = tok;
  d->next = label_defs;
  label_defs = d;
}

void pctx_validate_goto_refs(void) {
  for (goto_ref_t *g = goto_refs; g; g = g->next) {
    int found = 0;
    for (label_def_t *d = label_defs; d; d = d->next) {
      if (d->len == g->len && strncmp(d->name, g->name, (size_t)g->len) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) {
      tk_error_tok(g->tok, "未定義ラベル '%.*s' への goto です", g->len, g->name);
    }
  }
}

bool pctx_has_tag_type(token_kind_t kind, char *name, int len) {
  for (tag_type_t *t = tag_types; t; t = t->next) {
    if (t->kind == kind && t->len == len && strncmp(t->name, name, (size_t)len) == 0) {
      return true;
    }
  }
  return false;
}

void pctx_define_tag_type(token_kind_t kind, char *name, int len) {
  if (pctx_has_tag_type(kind, name, len)) return;
  tag_type_t *t = calloc(1, sizeof(tag_type_t));
  t->kind = kind;
  t->name = name;
  t->len = len;
  t->next = tag_types;
  tag_types = t;
}

bool pctx_is_type_token(token_kind_t kind) {
  return kind == TK_INT || kind == TK_CHAR || kind == TK_VOID || kind == TK_SHORT ||
         kind == TK_LONG || kind == TK_FLOAT || kind == TK_DOUBLE;
}

bool pctx_is_tag_keyword(token_kind_t kind) {
  return kind == TK_STRUCT || kind == TK_UNION || kind == TK_ENUM;
}

int pctx_scalar_type_size(token_kind_t kind) {
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


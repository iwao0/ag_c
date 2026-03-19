#include "preprocess.h"
#include "../diag/diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>

typedef struct macro macro_t;
struct macro {
  macro_t *next;
  char *name;
  token_t *body;
  bool is_funclike;
  char **params;
  int num_params;
};

static macro_t *macros;

static token_pp_t *as_pp(token_t *tok) { return (token_pp_t *)tok; }
static token_ident_t *as_ident(token_t *tok) { return (token_ident_t *)tok; }
static token_string_t *as_string(token_t *tok) { return (token_string_t *)tok; }
static token_num_t *as_num(token_t *tok) { return (token_num_t *)tok; }

#define PP_MAX_INCLUDE_DEPTH 64

typedef struct include_frame include_frame_t;
struct include_frame {
  include_frame_t *next;
  const char *path;
};

static include_frame_t *include_stack = NULL;
static int include_depth = 0;

static void *xrealloc(void *ptr, size_t size) {
  void *p = realloc(ptr, size);
  if (!p) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  return p;
}

static void *xreallocarray(void *ptr, size_t n, size_t size) {
  if (n != 0 && size > SIZE_MAX / n) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  return xrealloc(ptr, n * size);
}

static void pp_error(const char *fmt, const char *arg) __attribute__((noreturn));
static void pp_error(const char *fmt, const char *arg) {
  if (arg) diag_emit_internalf(DIAG_ERR_PREPROCESS_GENERIC, fmt, arg);
  diag_emit_internalf(DIAG_ERR_PREPROCESS_GENERIC, "%s", fmt);
}

static void validate_include_path_or_die(const char *path) {
  if (!path || !*path) {
    pp_error("不正な include ファイル名です", NULL);
  }
  if (path[0] == '/' || path[0] == '\\') {
    pp_error("許可されない include パスです: %s", path);
  }
  for (const char *p = path; *p; p++) {
    if ((p == path || p[-1] == '/') && p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
      pp_error("親ディレクトリ参照を含む include は禁止です: %s", path);
    }
  }
}

static void push_include_or_die(const char *path) {
  if (include_depth >= PP_MAX_INCLUDE_DEPTH) {
    pp_error("include のネストが深すぎます", NULL);
  }
  for (include_frame_t *f = include_stack; f; f = f->next) {
    if (!strcmp(f->path, path)) {
      pp_error("循環 include を検出しました: %s", path);
    }
  }
  include_frame_t *f = calloc(1, sizeof(include_frame_t));
  if (!f) {
    pp_error("メモリ確保に失敗しました", NULL);
  }
  f->path = path;
  f->next = include_stack;
  include_stack = f;
  include_depth++;
}

static void pop_include(void) {
  if (!include_stack) return;
  include_frame_t *f = include_stack;
  include_stack = f->next;
  free(f);
  if (include_depth > 0) include_depth--;
}

static bool ident_is(token_t *tok, const char *s) {
  if (!tok || tok->kind != TK_IDENT) return false;
  token_ident_t *id = as_ident(tok);
  int len = (int)strlen(s);
  return id->len == len && !strncmp(id->str, s, len);
}

static const char *token_text(token_t *tok, int *len) {
  if (!tok) {
    if (len) *len = 0;
    return NULL;
  }
  if (tok->kind == TK_IDENT) {
    token_ident_t *id = as_ident(tok);
    if (len) *len = id->len;
    return id->str;
  }
  if (tok->kind == TK_STRING) {
    token_string_t *st = as_string(tok);
    if (len) *len = st->len;
    return st->str;
  }
  if (tok->kind == TK_NUM) {
    token_num_t *num = as_num(tok);
    if (len) *len = num->len;
    return num->str;
  }
  return tk_token_kind_str(tok->kind, len);
}

static char *my_strndup(const char *s, size_t n) {
  char *p = malloc(n + 1);
  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

static void add_macro(char *name, bool is_funclike, char **params, int num_params, token_t *body) {
  macro_t *m = calloc(1, sizeof(macro_t));
  m->name = name;
  m->body = body;
  m->is_funclike = is_funclike;
  m->params = params;
  m->num_params = num_params;
  m->next = macros;
  macros = m;
}

static macro_t *find_macro(char *name) {
  for (macro_t *m = macros; m; m = m->next) {
    if (!strcmp(m->name, name))
      return m;
  }
  return NULL;
}

static hideset_t *new_hideset(char *name) {
  hideset_t *hs = calloc(1, sizeof(hideset_t));
  hs->name = my_strndup(name, strlen(name));
  return hs;
}

static hideset_t *hideset_union(hideset_t *hs1, hideset_t *hs2) {
  hideset_t head;
  hideset_t *cur = &head;
  for (hideset_t *hs = hs1; hs; hs = hs->next) {
    cur->next = new_hideset(hs->name);
    cur = cur->next;
  }
  for (hideset_t *hs = hs2; hs; hs = hs->next) {
    cur->next = new_hideset(hs->name);
    cur = cur->next;
  }
  cur->next = NULL;
  return head.next;
}

static bool hideset_contains(hideset_t *hs, char *name) {
  for (; hs; hs = hs->next) {
    if (!strcmp(hs->name, name))
      return true;
  }
  return false;
}

static token_t *copy_token(token_t *tok);

static token_t *copy_token_list(token_t *tok) {
  token_t head;
  token_t *cur = &head;
  for (token_t *t = tok; t; t = t->next) {
    cur->next = copy_token(t);
    cur = cur->next;
  }
  cur->next = NULL;
  return head.next;
}

// 新しいトークンを複製して作成するヘルパー
static token_t *copy_token(token_t *tok) {
  if (!tok) return NULL;
  token_t *t = NULL;

  switch (tok->kind) {
    case TK_IDENT: {
      token_ident_t *src = as_ident(tok);
      token_ident_t *dst = calloc(1, sizeof(token_ident_t));
      dst->pp.base = src->pp.base;
      dst->pp.hideset = src->pp.hideset;
      dst->str = src->str;
      dst->len = src->len;
      t = (token_t *)dst;
      break;
    }
    case TK_STRING: {
      token_string_t *src = as_string(tok);
      token_string_t *dst = calloc(1, sizeof(token_string_t));
      dst->pp.base = src->pp.base;
      dst->pp.hideset = src->pp.hideset;
      dst->str = src->str;
      dst->len = src->len;
      dst->char_width = src->char_width;
      dst->str_prefix_kind = src->str_prefix_kind;
      t = (token_t *)dst;
      break;
    }
    case TK_NUM: {
      token_num_t *src = as_num(tok);
      if (src->num_kind == TK_NUM_KIND_INT) {
        token_num_int_t *src_i = tk_as_num_int(tok);
        token_num_int_t *dst = calloc(1, sizeof(token_num_int_t));
        dst->base.pp.base = src_i->base.pp.base;
        dst->base.pp.hideset = src_i->base.pp.hideset;
        dst->base.str = src_i->base.str;
        dst->base.len = src_i->base.len;
        dst->base.num_kind = TK_NUM_KIND_INT;
        dst->val = src_i->val;
        dst->uval = src_i->uval;
        dst->is_unsigned = src_i->is_unsigned;
        dst->int_size = src_i->int_size;
        dst->int_base = src_i->int_base;
        dst->char_width = src_i->char_width;
        dst->char_prefix_kind = src_i->char_prefix_kind;
        t = (token_t *)dst;
      } else {
        token_num_float_t *src_f = tk_as_num_float(tok);
        token_num_float_t *dst = calloc(1, sizeof(token_num_float_t));
        dst->base.pp.base = src_f->base.pp.base;
        dst->base.pp.hideset = src_f->base.pp.hideset;
        dst->base.str = src_f->base.str;
        dst->base.len = src_f->base.len;
        dst->base.num_kind = TK_NUM_KIND_FLOAT;
        dst->fval = src_f->fval;
        dst->fp_kind = src_f->fp_kind;
        dst->float_suffix_kind = src_f->float_suffix_kind;
        t = (token_t *)dst;
      }
      break;
    }
    default: {
      token_pp_t *src = as_pp(tok);
      token_pp_t *dst = calloc(1, sizeof(token_pp_t));
      dst->base = src->base;
      dst->hideset = src->hideset;
      t = (token_t *)dst;
      break;
    }
  }

  t->next = NULL;
  return t;
}

static char *read_file(char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    return NULL;

  if (fseek(fp, 0, SEEK_END) == -1) {
    fclose(fp);
    return NULL;
  }
  long file_size = ftell(fp);
  if (file_size < 0) {
    fclose(fp);
    return NULL;
  }
  size_t size = (size_t)file_size;
  if (size > SIZE_MAX - 2) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) == -1) {
    fclose(fp);
    return NULL;
  }

  char *buf = calloc(1, size + 2);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  if (fread(buf, 1, size, fp) != size) {
    fclose(fp);
    free(buf);
    return NULL;
  }

  // ファイルが必ず「\n\0」で終わるようにする
  if (size == 0 || buf[size - 1] != '\n')
    buf[size++] = '\n';
  buf[size] = '\0';
  fclose(fp);
  return buf;
}

// === Conditional Compiliation State ===
typedef enum {
  IN_THEN,
  IN_ELIF,
  IN_ELSE,
} cond_incl_ctx_t;

typedef struct cond_incl cond_incl_t;
struct cond_incl {
  cond_incl_t *next;
  cond_incl_ctx_t ctx;
  bool included;
};

static cond_incl_t *cond_incl;

static bool is_dir(token_t *tok, const char *name) {
  if (!tok) return false;
  int len = 0;
  const char *s = token_text(tok, &len);
  int nlen = (int)strlen(name);
  return s && len == nlen && !strncmp(s, name, nlen) && isalpha(s[0]);
}

static token_t *skip_cond_incl(token_t *tok) {
  int nest = 0;
  while (tok->kind != TK_EOF) {
    if (tok->at_bol && tok->kind == TK_HASH) {
      token_t *hash = tok;
      token_t *next = tok->next;
      if (is_dir(next, "if") || is_dir(next, "ifdef") || is_dir(next, "ifndef")) {
        nest++;
      } else if (is_dir(next, "endif")) {
        if (nest == 0) return hash;
        nest--;
      } else if (is_dir(next, "else") || is_dir(next, "elif")) {
        if (nest == 0) return hash;
      }
    }
    tok = tok->next;
  }
  return tok;
}

static long const_expr(token_t **rest, token_t *tok);

static long primary(token_t **rest, token_t *tok) {
  if (tok->kind == TK_LPAREN) {
    long val = const_expr(&tok, tok->next);
    if (!(tok->kind == TK_RPAREN)) {
      pp_error("期待される )", NULL);
    }
    *rest = tok->next;
    return val;
  }
  if (tok->kind == TK_NUM) {
    if (tk_as_num(tok)->num_kind != TK_NUM_KIND_INT) {
      pp_error("#if の定数式では整数リテラルが必要です", NULL);
    }
    long val = tk_as_num_int(tok)->val;
    *rest = tok->next;
    return val;
  }
  if (tok->kind == TK_IDENT) {
    *rest = tok->next;
    return 0; // undefined macro to 0
  }
  pp_error("定数式のエラー: 予期しないトークンです", NULL);
}

static long unary(token_t **rest, token_t *tok) {
  if (tok->kind == TK_PLUS) {
    return unary(rest, tok->next);
  }
  if (tok->kind == TK_MINUS) {
    return -unary(rest, tok->next);
  }
  if (tok->kind == TK_BANG) {
    return !unary(rest, tok->next);
  }
  if (tok->kind == TK_TILDE) {
    return ~unary(rest, tok->next);
  }
  return primary(rest, tok);
}

static long mul(token_t **rest, token_t *tok) {
  long val = unary(&tok, tok);
  for (;;) {
    if (tok->kind == TK_MUL) {
      val *= unary(&tok, tok->next);
    } else if (tok->kind == TK_DIV) {
      long rhs = unary(&tok, tok->next);
      if (rhs == 0) pp_error("ゼロ除算", NULL);
      val /= rhs;
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long add(token_t **rest, token_t *tok) {
  long val = mul(&tok, tok);
  for (;;) {
    if (tok->kind == TK_PLUS) {
      val += mul(&tok, tok->next);
    } else if (tok->kind == TK_MINUS) {
      val -= mul(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long relational(token_t **rest, token_t *tok) {
  long val = add(&tok, tok);
  for (;;) {
    if (tok->kind == TK_LT) {
      val = val < add(&tok, tok->next);
    } else if (tok->kind == TK_LE) {
      val = val <= add(&tok, tok->next);
    } else if (tok->kind == TK_GT) {
      val = val > add(&tok, tok->next);
    } else if (tok->kind == TK_GE) {
      val = val >= add(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long equality(token_t **rest, token_t *tok) {
  long val = relational(&tok, tok);
  for (;;) {
    if (tok->kind == TK_EQEQ) {
      val = val == relational(&tok, tok->next);
    } else if (tok->kind == TK_NEQ) {
      val = val != relational(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long logand(token_t **rest, token_t *tok) {
  long val = equality(&tok, tok);
  for (;;) {
    if (tok->kind == TK_ANDAND) {
      long rhs = equality(&tok, tok->next);
      val = val && rhs;
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long logor(token_t **rest, token_t *tok) {
  long val = logand(&tok, tok);
  for (;;) {
    if (tok->kind == TK_OROR) {
      long rhs = logand(&tok, tok->next);
      val = val || rhs;
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long const_expr(token_t **rest, token_t *tok) {
  return logor(rest, tok);
}

token_t *preprocess(token_t *tok); // front declaration

static bool evaluate_constexpr(token_t **rest_tok, token_t *tok) {
   token_t head;
   head.next = NULL;
   token_t *cur = &head;
   
   while (tok->kind != TK_EOF && !tok->at_bol) {
      cur->next = copy_token(tok);
      cur->next->at_bol = false; 
      cur = cur->next;
      tok = tok->next;
   }
   cur->next = calloc(1, sizeof(token_t));
   cur->next->kind = TK_EOF;
   *rest_tok = tok;

   token_t *t = head.next;
   token_t head2; head2.next = NULL;
   token_t *cur2 = &head2;
   while (t->kind != TK_EOF) {
      if (ident_is(t, "defined")) {
         t = t->next;
         bool has_paren = false;
         if (t->kind == TK_LPAREN) {
            has_paren = true;
            t = t->next;
         }
         if (t->kind != TK_IDENT) pp_error("defined の後にマクロ名が必要です", NULL);
         token_ident_t *id = as_ident(t);
         char *name = my_strndup(id->str, id->len);
         bool is_def = find_macro(name) != NULL;
         free(name);
         t = t->next;
         if (has_paren) {
            if (!(t->kind == TK_RPAREN)) {
              pp_error("defined の閉じ括弧がありません", NULL);
            }
            t = t->next;
         }
         
         token_num_int_t *num = calloc(1, sizeof(token_num_int_t));
         num->base.pp.base.kind = TK_NUM;
         num->base.num_kind = TK_NUM_KIND_INT;
         num->base.str = "0"; // just dummy
         num->base.len = 1;
         num->val = is_def ? 1 : 0;
         num->uval = (unsigned long long)num->val;
         num->is_unsigned = false;
         num->int_size = TK_INT_SIZE_INT;
         num->int_base = 10;
         cur2->next = (token_t *)num;
         cur2 = cur2->next;
      } else {
         cur2->next = copy_token(t);
         cur2 = cur2->next;
         t = t->next;
      }
   }
   cur2->next = calloc(1, sizeof(token_t));
   cur2->next->kind = TK_EOF;

   token_t *expanded = preprocess(head2.next);

   if (expanded->kind == TK_EOF) return false;
   token_t *rest;
  long val = const_expr(&rest, expanded);
  if (rest->kind != TK_EOF) {
    pp_error("定数式に余分なトークンがあります", NULL);
  }
   return val != 0;
}

static token_t *stringify_tokens(token_t *tok, token_t *macro_tok) {
  size_t cap = 64;
  char *buf = calloc(1, cap);
  size_t len = 0;
  
  for (token_t *t = tok; t; t = t->next) {
    if (len > 0 && t->has_space) {
        if (len + 1 >= cap) {
          if (cap > SIZE_MAX / 2) {
            pp_error("文字列化中にサイズが大きすぎます", NULL);
          }
          cap *= 2;
          buf = xrealloc(buf, cap);
        }
        buf[len++] = ' ';
      }
    int tlen = 0;
    const char *ts = token_text(t, &tlen);
    if (!ts) ts = "";
    if (tlen < 0 || (size_t)tlen > SIZE_MAX - len - 1) {
      pp_error("文字列化中にサイズが大きすぎます", NULL);
    }
    size_t need = len + (size_t)tlen + 1;
    while (need > cap) {
      if (cap > SIZE_MAX / 2) {
        pp_error("文字列化中にサイズが大きすぎます", NULL);
      }
      cap *= 2;
    }
    if (need > len + (size_t)tlen + 1) {
      pp_error("文字列化中にサイズが不正です", NULL);
    }
    buf = xrealloc(buf, cap);
    memcpy(buf + len, ts, (size_t)tlen);
    len += (size_t)tlen;
  }
  buf[len] = '\0';
  
  char *str_buf = my_strndup(buf, len);
  free(buf);
  
  token_string_t *res = calloc(1, sizeof(token_string_t));
  res->pp.base.kind = TK_STRING;
  res->str = str_buf;
  res->len = len;
  res->pp.base.file_name = macro_tok->file_name;
  res->pp.base.line_no = macro_tok->line_no;
  return (token_t *)res;
}

static token_t *paste_tokens(token_t *tok) {
  if (!tok) return NULL;
  token_t head; head.next = tok;
  token_t *prev = &head;
  token_t *cur = tok;
  while (cur && cur->next) {
    if (cur->next->kind == TK_HASHHASH) {
      token_t *hashhash = cur->next;
      token_t *rhs = hashhash->next;
      if (!rhs) break; // invalid ## at end of macro

      int len_l = 0;
      int len_r = 0;
      const char *s_l = token_text(cur, &len_l);
      const char *s_r = token_text(rhs, &len_r);
      if (len_l < 0 || len_r < 0 || (size_t)len_l > SIZE_MAX - (size_t)len_r - 1) {
        pp_error("トークン結合中にサイズが大きすぎます", NULL);
      }
      size_t len = (size_t)len_l + (size_t)len_r;
      char *buf = calloc(1, len + 1);
      memcpy(buf, s_l, (size_t)len_l);
      memcpy(buf + len_l, s_r, (size_t)len_r);
      
      char *saved_input = tk_get_user_input();
      char *saved_filename = tk_get_filename();
      token_t *saved_token = token;

      tk_set_filename("<paste>");
      token_t *merged = tk_tokenize(buf);

      tk_set_filename(saved_filename);
      tk_set_user_input(saved_input);
      token = saved_token;

      if (merged->kind == TK_EOF) {
        merged = cur;
      } else {
        merged->next = rhs->next;
        merged->file_name = cur->file_name;
        merged->line_no = cur->line_no;
        cur = merged;
        prev->next = cur;
      }
    } else {
      prev = cur;
      cur = cur->next;
    }
  }
  return head.next;
}

// プリプロセッサのメイン処理
token_t *preprocess(token_t *tok) {
  token_t head;
  head.next = NULL;
  token_t *cur = &head;

  while (tok->kind != TK_EOF) {
      // 行頭かつ '#' 記号の場合はディレクティブ行として処理
    if (tok->at_bol && tok->kind == TK_HASH) {
      tok = tok->next; // '#' をスキップ
      
      if (is_dir(tok, "include")) {
        tok = tok->next;
        size_t filename_cap = 64;
        size_t filename_len = 0;
        char *filename = calloc(filename_cap, 1);
        if (!filename) {
          diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
        }
        
        if (tok->kind == TK_STRING) {
          token_string_t *st = as_string(tok);
          size_t need = (size_t)st->len + 1;
          if (st->len < 0 || need == 0) {
            pp_error("不正な include ファイル名です", NULL);
          }
          if (need > filename_cap) {
            filename_cap = need;
            filename = xrealloc(filename, filename_cap);
          }
          memcpy(filename, st->str, (size_t)st->len);
          filename[st->len] = '\0';
          filename_len = (size_t)st->len;
          tok = tok->next;
        } else if (tok->kind == TK_LT) {
          tok = tok->next;
          while (tok->kind != TK_EOF && tok->kind != TK_GT) {
            int tlen = 0;
            const char *ts = token_text(tok, &tlen);
            if (!ts) ts = "";
            if (tlen < 0 || (size_t)tlen > SIZE_MAX - filename_len - 1) {
              pp_error("include ファイル名が大きすぎます", NULL);
            }
            size_t need = filename_len + (size_t)tlen + 1;
            if (need > filename_cap) {
              while (filename_cap < need) {
                if (filename_cap > SIZE_MAX / 2) {
                  pp_error("include ファイル名が大きすぎます", NULL);
                }
                filename_cap *= 2;
              }
              filename = xrealloc(filename, filename_cap);
            }
            memcpy(filename + filename_len, ts, (size_t)tlen);
            filename_len += (size_t)tlen;
            filename[filename_len] = '\0';
            tok = tok->next;
          }
          if (tok->kind == TK_EOF) {
            pp_error("期待される '>' がありません", NULL);
          }
          tok = tok->next; // '>' をスキップ
        }
        validate_include_path_or_die(filename);

        char *buf = read_file(filename);
        if (!buf) {
          size_t alt_len = strlen("include/") + strlen(filename) + 1;
          char *alt = calloc(alt_len, 1);
          if (!alt) {
            diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
          }
          snprintf(alt, alt_len, "include/%s", filename);
          buf = read_file(alt);
          free(alt);
        }
        if (!buf) {
          diag_emit_internalf(DIAG_ERR_PREPROCESS_GENERIC, "ファイルが見つかりません: %s", filename);
          free(filename);
        }

        char *saved_input = tk_get_user_input();
        char *saved_filename = tk_get_filename();
        token_t *saved_token = token;

        tk_set_filename(my_strndup(filename, strlen(filename)));
        token_t *tok2 = tk_tokenize(buf);
        push_include_or_die(filename);
        tok2 = preprocess(tok2);
        pop_include();

        tk_set_filename(saved_filename);
        tk_set_user_input(saved_input);
        token = saved_token;

        if (tok2->kind != TK_EOF) {
          while (tok2->kind != TK_EOF) {
            cur->next = copy_token(tok2);
            cur = cur->next;
            tok2 = tok2->next;
          }
        }
        free(filename);
        continue;
      }

      if (is_dir(tok, "ifdef")) {
        tok = tok->next;
        if (tok->kind != TK_IDENT) pp_error("マクロ名がありません", NULL);
        token_ident_t *id = as_ident(tok);
        char *name = my_strndup(id->str, id->len);
        bool is_true = find_macro(name) != NULL;
        free(name);
        
        cond_incl_t *ci = calloc(1, sizeof(cond_incl_t));
        ci->ctx = IN_THEN;
        ci->included = is_true;
        ci->next = cond_incl;
        cond_incl = ci;
        
        tok = tok->next; // skip macro name
        while (tok->kind != TK_EOF && !tok->at_bol) tok = tok->next; // skip to eol
        if (!is_true) tok = skip_cond_incl(tok);
        continue;
      }
      
      if (is_dir(tok, "ifndef")) {
        tok = tok->next;
        if (tok->kind != TK_IDENT) pp_error("マクロ名がありません", NULL);
        token_ident_t *id = as_ident(tok);
        char *name = my_strndup(id->str, id->len);
        bool is_true = find_macro(name) == NULL;
        free(name);
        
        cond_incl_t *ci = calloc(1, sizeof(cond_incl_t));
        ci->ctx = IN_THEN;
        ci->included = is_true;
        ci->next = cond_incl;
        cond_incl = ci;
        
        tok = tok->next; // skip macro name
        while (tok->kind != TK_EOF && !tok->at_bol) tok = tok->next; // skip to eol
        if (!is_true) tok = skip_cond_incl(tok);
        continue;
      }
      
      if (is_dir(tok, "else")) {
        if (!cond_incl) pp_error("孤立した #else", NULL);
        if (cond_incl->ctx == IN_ELSE) pp_error("#else の重複", NULL);
        cond_incl->ctx = IN_ELSE;
        tok = tok->next;
        while (tok->kind != TK_EOF && !tok->at_bol) tok = tok->next; // skip to eol
        
        if (cond_incl->included) {
           tok = skip_cond_incl(tok);
        } else {
           cond_incl->included = true;
        }
        continue;
      }
      
      if (is_dir(tok, "elif")) {
        if (!cond_incl) pp_error("孤立した #elif", NULL);
        if (cond_incl->ctx == IN_ELSE) pp_error("#else の後の #elif", NULL);
        cond_incl->ctx = IN_ELIF;
        tok = tok->next;
        
        if (cond_incl->included) {
           while (tok->kind != TK_EOF && !tok->at_bol) tok = tok->next;
           tok = skip_cond_incl(tok);
        } else {
           bool is_true = evaluate_constexpr(&tok, tok);
           if (is_true) cond_incl->included = true;
           if (!is_true) tok = skip_cond_incl(tok);
        }
        continue;
      }
      
      if (is_dir(tok, "if")) {
        tok = tok->next;
        bool is_true = evaluate_constexpr(&tok, tok);
        cond_incl_t *ci = calloc(1, sizeof(cond_incl_t));
        ci->ctx = IN_THEN;
        ci->included = is_true;
        ci->next = cond_incl;
        cond_incl = ci;
        if (!is_true) tok = skip_cond_incl(tok);
        continue;
      }
      
      if (is_dir(tok, "endif")) {
        if (!cond_incl) pp_error("孤立した #endif", NULL);
        cond_incl_t *ci = cond_incl;
        cond_incl = cond_incl->next;
        free(ci);
        tok = tok->next;
        while (tok->kind != TK_EOF && !tok->at_bol) tok = tok->next; // skip to eol
        continue;
      }
      
      if (is_dir(tok, "define")) {
        tok = tok->next;
        if (tok->kind != TK_IDENT) {
          pp_error("マクロ名がありません", NULL);
        }
        token_ident_t *id = as_ident(tok);
        char *name = my_strndup(id->str, id->len);
        tok = tok->next;
        
        bool is_funclike = false;
        char **params = NULL;
        int num_params = 0;

        if (tok->kind == TK_LPAREN && !tok->has_space) {
          is_funclike = true;
          tok = tok->next;
          int cap = 8;
          params = calloc(cap, sizeof(char*));
          while (tok->kind != TK_EOF && tok->kind != TK_RPAREN) {
            if (tok->kind != TK_IDENT) pp_error("マクロの引数が不正です", NULL);
            if (num_params >= cap) {
              cap *= 2;
              params = xreallocarray(params, (size_t)cap, sizeof(char *));
            }
            token_ident_t *pid = as_ident(tok);
            params[num_params++] = my_strndup(pid->str, pid->len);
            tok = tok->next;
            if (tok->kind == TK_COMMA) tok = tok->next;
          }
          if (tok->kind == TK_RPAREN) tok = tok->next;
        }

        token_t head;
        head.next = NULL;
        token_t *cur_body = &head;
        while (tok->kind != TK_EOF && !tok->at_bol) {
          cur_body->next = copy_token(tok);
          cur_body = cur_body->next;
          tok = tok->next;
        }
        cur_body->next = NULL;

        add_macro(name, is_funclike, params, num_params, head.next);
        continue;
      }

      if (is_dir(tok, "undef")) {
        tok = tok->next;
        if (tok->kind != TK_IDENT) {
          pp_error("マクロ名がありません", NULL);
        }
        token_ident_t *id = as_ident(tok);
        char *name = my_strndup(id->str, id->len);
        tok = tok->next;

        macro_t *prev = NULL;
        for (macro_t *m = macros; m; prev = m, m = m->next) {
           if (!strcmp(m->name, name)) {
              if (prev) prev->next = m->next;
              else macros = m->next;
              break;
           }
        }
        free(name);

        while (tok->kind != TK_EOF && !tok->at_bol) {
          tok = tok->next;
        }
        continue;
      }

      if (is_dir(tok, "error")) {
        tok = tok->next;
        size_t cap = 64;
        size_t len = 0;
        char *msg = calloc(cap, 1);
        if (!msg) {
          diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
        }
        const char *prefix = "error: ";
        size_t pfx_len = strlen(prefix);
        if (cap <= pfx_len) {
          cap = pfx_len + 1;
          msg = xrealloc(msg, cap);
        }
        memcpy(msg, prefix, pfx_len);
        len = pfx_len;
        while (tok->kind != TK_EOF && !tok->at_bol) {
          int tlen = 0;
          const char *ts = token_text(tok, &tlen);
          char tmp[64];
          tmp[0] = '\0';
          if (tok->kind == TK_NUM) {
            if (tk_as_num(tok)->num_kind == TK_NUM_KIND_INT) {
              snprintf(tmp, sizeof(tmp), "%lld", tk_as_num_int(tok)->val);
            } else {
              snprintf(tmp, sizeof(tmp), "%g", tk_as_num_float(tok)->fval);
            }
            ts = tmp;
            tlen = (int)strlen(tmp);
          }
          if (ts && tlen > 0) {
            size_t need = len + (size_t)tlen + 2;
            while (need > cap) {
              if (cap > SIZE_MAX / 2) pp_error("error メッセージが大きすぎます", NULL);
              cap *= 2;
            }
            msg = xrealloc(msg, cap);
            memcpy(msg + len, ts, (size_t)tlen);
            len += (size_t)tlen;
            msg[len] = '\0';
          }
          if (tok->has_space) {
            if (len + 2 > cap) {
              if (cap > SIZE_MAX / 2) pp_error("error メッセージが大きすぎます", NULL);
              cap *= 2;
              msg = xrealloc(msg, cap);
            }
            msg[len++] = ' ';
            msg[len] = '\0';
          }
          tok = tok->next;
        }
        diag_emit_internalf(DIAG_ERR_PREPROCESS_GENERIC, "%s", msg);
      }
      
      // ひとまず改行（次の行頭）またはEOFまでトークンを読み飛ばす
      while (tok->kind != TK_EOF && !tok->at_bol) {
        tok = tok->next;
      }
      continue;
    }

    if (tok->kind == TK_IDENT) {
      token_ident_t *id = as_ident(tok);
      char *name = my_strndup(id->str, id->len);
      macro_t *m = find_macro(name);
      
      if (m && !hideset_contains(as_pp(tok)->hideset, name)) {
        if (m->is_funclike) {
           if (tok->next && tok->next->kind == TK_LPAREN) {
             token_t *macro_tok = tok;
             tok = tok->next->next; // skip macro name and '('
             
             token_t **args = calloc(m->num_params > 0 ? m->num_params : 1, sizeof(token_t*));
             int arg_cnt = 0;
             if (!(tok->kind == TK_RPAREN)) {
               while (tok->kind != TK_EOF) {
                 token_t head_arg; head_arg.next = NULL;
                 token_t *cur_arg = &head_arg;
                 int nest = 0;
                 while (tok->kind != TK_EOF) {
                   if (nest == 0 && (tok->kind == TK_COMMA || tok->kind == TK_RPAREN)) break;
                   if (tok->kind == TK_LPAREN) nest++;
                   if (tok->kind == TK_RPAREN) nest--;
                   cur_arg->next = copy_token(tok);
                   cur_arg = cur_arg->next;
                   tok = tok->next;
                 }
                 if (arg_cnt < m->num_params) args[arg_cnt++] = head_arg.next;
                 if (tok->kind == TK_COMMA) tok = tok->next;
                 else break;
               }
             }
             if (tok->kind != TK_RPAREN) {
               pp_error("関数マクロ呼び出しの引数が閉じられていません", NULL);
             }
             tok = tok->next; // skip ')'
             
             token_t body_head; body_head.next = NULL;
             token_t *cur_body = &body_head;
             for (token_t *t = m->body; t; t = t->next) {
               if (t->kind == TK_HASH && t->next && t->next->kind == TK_IDENT) {
                 int p_idx = -1;
                 for (int i=0; i<m->num_params; i++) {
                   token_ident_t *pid = as_ident(t->next);
                   if (strlen(m->params[i]) == (size_t)pid->len && !strncmp(m->params[i], pid->str, pid->len)) {
                     p_idx = i; break;
                   }
                 }
                 if (p_idx != -1) {
                   token_t *str_tok = stringify_tokens(args[p_idx], macro_tok);
                   cur_body->next = str_tok;
                   cur_body = cur_body->next;
                   t = t->next; // skip IDENT
                   continue;
                 }
               }
               
               if (t->kind == TK_IDENT) {
                 int p_idx = -1;
                 for (int i=0; i<m->num_params; i++) {
                   token_ident_t *pid = as_ident(t);
                   if (strlen(m->params[i]) == (size_t)pid->len && !strncmp(m->params[i], pid->str, pid->len)) {
                     p_idx = i; break;
                   }
                 }
                 if (p_idx != -1) {
                   for (token_t *a = args[p_idx]; a; a = a->next) {
                     cur_body->next = copy_token(a);
                     cur_body = cur_body->next;
                   }
                   continue;
                 }
               }
               cur_body->next = copy_token(t);
               cur_body = cur_body->next;
             }
             cur_body->next = NULL;
             
             token_t *body_copy = paste_tokens(body_head.next);
             hideset_t *hs = hideset_union(as_pp(macro_tok)->hideset, new_hideset(name));
             for (token_t *t = body_copy; t; t = t->next) {
               as_pp(t)->hideset = hideset_union(as_pp(t)->hideset, hs);
             }
             if (body_copy) {
               body_copy->at_bol = macro_tok->at_bol;
               body_copy->has_space = macro_tok->has_space;
               token_t *tail = body_copy;
               while (tail->next) tail = tail->next;
               tail->next = tok;
               tok = body_copy;
               free(name);
               continue;
             } else {
               free(name);
               continue;
             }
           }
        } else {
           token_t *body_copy = copy_token_list(m->body);
           body_copy = paste_tokens(body_copy);
           
           hideset_t *hs = hideset_union(as_pp(tok)->hideset, new_hideset(name));
           for (token_t *t = body_copy; t; t = t->next) {
             as_pp(t)->hideset = hideset_union(as_pp(t)->hideset, hs);
           }

           if (body_copy) {
              body_copy->at_bol = tok->at_bol;
              body_copy->has_space = tok->has_space;

              token_t *tail = body_copy;
              while (tail->next) tail = tail->next;
              tail->next = tok->next;
              tok = body_copy;
              free(name);
              continue;
           } else {
              tok = tok->next;
              free(name);
              continue;
           }
        }
      }
      free(name);
    }

    // 通常のコード行のトークンはそのまま出力へ繋ぐ
    cur->next = copy_token(tok);
    cur = cur->next;
    tok = tok->next;
  }

  cur->next = tok; // TK_EOF を繋ぐ
  return head.next;
}

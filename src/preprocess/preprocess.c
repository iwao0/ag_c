#include "preprocess.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdio.h>
#include <ctype.h>

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
  token_t *t = calloc(1, sizeof(token_t));
  *t = *tok;
  t->next = NULL;
  return t;
}

static char *read_file(char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp)
    return NULL;

  if (fseek(fp, 0, SEEK_END) == -1)
    return NULL;
  size_t size = ftell(fp);
  if (fseek(fp, 0, SEEK_SET) == -1)
    return NULL;

  char *buf = calloc(1, size + 2);
  fread(buf, size, 1, fp);

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
  if (!tok || !tok->str) return false;
  int len = strlen(name);
  return tok->len == len && !strncmp(tok->str, name, len) && isalpha(tok->str[0]);
}

static token_t *skip_cond_incl(token_t *tok) {
  int nest = 0;
  while (tok->kind != TK_EOF) {
    if (tok->at_bol && tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '#') {
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
  if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '(') {
    long val = const_expr(&tok, tok->next);
    if (!(tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == ')')) {
        fprintf(stderr, "期待される )\n"); exit(1);
    }
    *rest = tok->next;
    return val;
  }
  if (tok->kind == TK_NUM) {
    long val = tok->val;
    *rest = tok->next;
    return val;
  }
  if (tok->kind == TK_IDENT) {
    *rest = tok->next;
    return 0; // undefined macro to 0
  }
  fprintf(stderr, "定数式のエラー: 予期しないトークンです\n"); exit(1);
}

static long unary(token_t **rest, token_t *tok) {
  if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '+') {
    return unary(rest, tok->next);
  }
  if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '-') {
    return -unary(rest, tok->next);
  }
  if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '!') {
    return !unary(rest, tok->next);
  }
  if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '~') {
    return ~unary(rest, tok->next);
  }
  return primary(rest, tok);
}

static long mul(token_t **rest, token_t *tok) {
  long val = unary(&tok, tok);
  for (;;) {
    if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '*') {
      val *= unary(&tok, tok->next);
    } else if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '/') {
      long rhs = unary(&tok, tok->next);
      if (rhs == 0) { fprintf(stderr, "ゼロ除算\n"); exit(1); }
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
    if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '+') {
      val += mul(&tok, tok->next);
    } else if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '-') {
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
    if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '<') {
      val = val < add(&tok, tok->next);
    } else if (tok->kind == TK_RESERVED && tok->len == 2 && !strncmp(tok->str, "<=", 2)) {
      val = val <= add(&tok, tok->next);
    } else if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '>') {
      val = val > add(&tok, tok->next);
    } else if (tok->kind == TK_RESERVED && tok->len == 2 && !strncmp(tok->str, ">=", 2)) {
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
    if (tok->kind == TK_RESERVED && tok->len == 2 && !strncmp(tok->str, "==", 2)) {
      val = val == relational(&tok, tok->next);
    } else if (tok->kind == TK_RESERVED && tok->len == 2 && !strncmp(tok->str, "!=", 2)) {
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
    if (tok->kind == TK_RESERVED && tok->len == 2 && !strncmp(tok->str, "&&", 2)) {
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
    if (tok->kind == TK_RESERVED && tok->len == 2 && !strncmp(tok->str, "||", 2)) {
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
      if (t->kind == TK_IDENT && t->len == 7 && !strncmp(t->str, "defined", 7)) {
         t = t->next;
         bool has_paren = false;
         if (t->kind == TK_RESERVED && t->len == 1 && t->str[0] == '(') {
            has_paren = true;
            t = t->next;
         }
         if (t->kind != TK_IDENT) { fprintf(stderr, "defined の後にマクロ名が必要です\n"); exit(1); }
         char *name = my_strndup(t->str, t->len);
         bool is_def = find_macro(name) != NULL;
         free(name);
         t = t->next;
         if (has_paren) {
            if (!(t->kind == TK_RESERVED && t->len == 1 && t->str[0] == ')')) {
               fprintf(stderr, "defined の閉じ括弧がありません\n"); exit(1);
            }
            t = t->next;
         }
         
         token_t *num = calloc(1, sizeof(token_t));
         num->kind = TK_NUM;
         num->val = is_def ? 1 : 0;
         num->str = "0"; // just dummy
         num->len = 1;
         cur2->next = num;
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
      fprintf(stderr, "定数式に余分なトークンがあります\n"); exit(1);
   }
   return val != 0;
}

static token_t *stringify_tokens(token_t *tok, token_t *macro_tok) {
  int cap = 64;
  char *buf = calloc(1, cap);
  int len = 0;
  
  for (token_t *t = tok; t; t = t->next) {
    if (len > 0 && t->has_space) {
      if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
      buf[len++] = ' ';
    }
    if (len + t->len >= cap) { cap = (cap + t->len) * 2; buf = realloc(buf, cap); }
    memcpy(buf + len, t->str, t->len);
    len += t->len;
  }
  buf[len] = '\0';
  
  char *str_buf = my_strndup(buf, len);
  free(buf);
  
  token_t *res = calloc(1, sizeof(token_t));
  res->kind = TK_STRING;
  res->str = str_buf;
  res->len = len;
  res->file_name = macro_tok->file_name;
  res->line_no = macro_tok->line_no;
  return res;
}

static token_t *paste_tokens(token_t *tok) {
  if (!tok) return NULL;
  token_t head; head.next = tok;
  token_t *prev = &head;
  token_t *cur = tok;
  while (cur && cur->next) {
    if (cur->next->kind == TK_RESERVED && cur->next->len == 2 && !strncmp(cur->next->str, "##", 2)) {
      token_t *hashhash = cur->next;
      token_t *rhs = hashhash->next;
      if (!rhs) break; // invalid ## at end of macro
      
      int len = cur->len + rhs->len;
      char *buf = calloc(1, len + 1);
      memcpy(buf, cur->str, cur->len);
      memcpy(buf + cur->len, rhs->str, rhs->len);
      
      char *saved_input = get_user_input();
      char *saved_filename = get_filename();
      token_t *saved_token = token;

      set_filename("<paste>");
      token_t *merged = tokenize(buf);

      set_filename(saved_filename);
      set_user_input(saved_input);
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
    if (tok->at_bol && tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '#') {
      tok = tok->next; // '#' をスキップ
      
      if (is_dir(tok, "include")) {
        tok = tok->next;
        char filename[1024] = {0};
        
        if (tok->kind == TK_STRING) {
          strncpy(filename, tok->str, tok->len);
          tok = tok->next;
        } else if (tok->kind == TK_RESERVED && tok->str[0] == '<') {
          tok = tok->next;
          int pos = 0;
          while (tok->kind != TK_EOF && !(tok->kind == TK_RESERVED && tok->str[0] == '>')) {
            strncpy(filename + pos, tok->str, tok->len);
            pos += tok->len;
            tok = tok->next;
          }
          if (tok->kind == TK_EOF) {
            fprintf(stderr, "期待される '>' がありません\n");
            exit(1);
          }
          tok = tok->next; // '>' をスキップ
        }

        char *buf = read_file(filename);
        if (!buf) {
          fprintf(stderr, "ファイルが見つかりません: %s\n", filename);
          exit(1);
        }

        char *saved_input = get_user_input();
        char *saved_filename = get_filename();
        token_t *saved_token = token;

        set_filename(my_strndup(filename, strlen(filename)));
        token_t *tok2 = tokenize(buf);
        tok2 = preprocess(tok2);

        set_filename(saved_filename);
        set_user_input(saved_input);
        token = saved_token;

        if (tok2->kind != TK_EOF) {
          while (tok2->kind != TK_EOF) {
            cur->next = copy_token(tok2);
            cur = cur->next;
            tok2 = tok2->next;
          }
        }
        continue;
      }

      if (is_dir(tok, "ifdef")) {
        tok = tok->next;
        if (!isalpha(tok->str[0])) { fprintf(stderr, "マクロ名がありません\n"); exit(1); }
        char *name = my_strndup(tok->str, tok->len);
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
        if (!isalpha(tok->str[0])) { fprintf(stderr, "マクロ名がありません\n"); exit(1); }
        char *name = my_strndup(tok->str, tok->len);
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
        if (!cond_incl) { fprintf(stderr, "孤立した #else\n"); exit(1); }
        if (cond_incl->ctx == IN_ELSE) { fprintf(stderr, "#else の重複\n"); exit(1); }
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
        if (!cond_incl) { fprintf(stderr, "孤立した #elif\n"); exit(1); }
        if (cond_incl->ctx == IN_ELSE) { fprintf(stderr, "#else の後の #elif\n"); exit(1); }
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
        if (!cond_incl) { fprintf(stderr, "孤立した #endif\n"); exit(1); }
        cond_incl_t *ci = cond_incl;
        cond_incl = cond_incl->next;
        free(ci);
        tok = tok->next;
        while (tok->kind != TK_EOF && !tok->at_bol) tok = tok->next; // skip to eol
        continue;
      }
      
      if (is_dir(tok, "define")) {
        tok = tok->next;
        if (!isalpha(tok->str[0])) {
          fprintf(stderr, "マクロ名がありません\n");
          exit(1);
        }
        char *name = my_strndup(tok->str, tok->len);
        tok = tok->next;
        
        bool is_funclike = false;
        char **params = NULL;
        int num_params = 0;

        if (tok->kind == TK_RESERVED && tok->len == 1 && tok->str[0] == '(' && !tok->has_space) {
          is_funclike = true;
          tok = tok->next;
          int cap = 8;
          params = calloc(cap, sizeof(char*));
          while (tok->kind != TK_EOF && !(tok->kind == TK_RESERVED && tok->str[0] == ')')) {
            if (tok->kind != TK_IDENT) { fprintf(stderr, "マクロの引数が不正です\n"); exit(1); }
            if (num_params >= cap) { cap *= 2; params = realloc(params, cap * sizeof(char*)); }
            params[num_params++] = my_strndup(tok->str, tok->len);
            tok = tok->next;
            if (tok->kind == TK_RESERVED && tok->str[0] == ',') tok = tok->next;
          }
          if (tok->kind == TK_RESERVED && tok->str[0] == ')') tok = tok->next;
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
        if (!isalpha(tok->str[0])) {
          fprintf(stderr, "マクロ名がありません\n");
          exit(1);
        }
        char *name = my_strndup(tok->str, tok->len);
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
        fprintf(stderr, "error: ");
        while (tok->kind != TK_EOF && !tok->at_bol) {
          if (tok->kind == TK_NUM) {
            fprintf(stderr, "%d", tok->val);
          } else {
            fprintf(stderr, "%.*s", tok->len, tok->str);
          }
          if (tok->has_space) fprintf(stderr, " ");
          tok = tok->next;
        }
        fprintf(stderr, "\n");
        exit(1);
      }
      
      // ひとまず改行（次の行頭）またはEOFまでトークンを読み飛ばす
      while (tok->kind != TK_EOF && !tok->at_bol) {
        tok = tok->next;
      }
      continue;
    }

    if (tok->kind == TK_IDENT) {
      char *name = my_strndup(tok->str, tok->len);
      macro_t *m = find_macro(name);
      
      if (m && !hideset_contains(tok->hideset, name)) {
        if (m->is_funclike) {
           if (tok->next && tok->next->kind == TK_RESERVED && tok->next->str[0] == '(') {
             token_t *macro_tok = tok;
             tok = tok->next->next; // skip macro name and '('
             
             token_t **args = calloc(m->num_params > 0 ? m->num_params : 1, sizeof(token_t*));
             int arg_cnt = 0;
             if (!(tok->kind == TK_RESERVED && tok->str[0] == ')')) {
               while (tok->kind != TK_EOF) {
                 token_t head_arg; head_arg.next = NULL;
                 token_t *cur_arg = &head_arg;
                 int nest = 0;
                 while (tok->kind != TK_EOF) {
                   if (nest == 0 && tok->kind == TK_RESERVED && (tok->str[0] == ',' || tok->str[0] == ')')) break;
                   if (tok->kind == TK_RESERVED && tok->str[0] == '(') nest++;
                   if (tok->kind == TK_RESERVED && tok->str[0] == ')') nest--;
                   cur_arg->next = copy_token(tok);
                   cur_arg = cur_arg->next;
                   tok = tok->next;
                 }
                 if (arg_cnt < m->num_params) args[arg_cnt++] = head_arg.next;
                 if (tok->kind == TK_RESERVED && tok->str[0] == ',') tok = tok->next;
                 else break;
               }
             }
             if (tok->kind != TK_RESERVED || tok->str[0] != ')') {
               fprintf(stderr, "関数マクロ呼び出しの引数が閉じられていません\n"); exit(1);
             }
             tok = tok->next; // skip ')'
             
             token_t body_head; body_head.next = NULL;
             token_t *cur_body = &body_head;
             for (token_t *t = m->body; t; t = t->next) {
               if (t->kind == TK_RESERVED && t->len == 1 && t->str[0] == '#' && t->next && t->next->kind == TK_IDENT) {
                 int p_idx = -1;
                 for (int i=0; i<m->num_params; i++) {
                   if (strlen(m->params[i]) == (size_t)t->next->len && !strncmp(m->params[i], t->next->str, t->next->len)) {
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
                   if (strlen(m->params[i]) == (size_t)t->len && !strncmp(m->params[i], t->str, t->len)) {
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
             hideset_t *hs = hideset_union(macro_tok->hideset, new_hideset(name));
             for (token_t *t = body_copy; t; t = t->next) {
               t->hideset = hideset_union(t->hideset, hs);
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
           
           hideset_t *hs = hideset_union(tok->hideset, new_hideset(name));
           for (token_t *t = body_copy; t; t = t->next) {
             t->hideset = hideset_union(t->hideset, hs);
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

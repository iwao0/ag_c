#include "preprocess.h"
#include "../diag/diag.h"
#include "../tokenizer/allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef struct macro macro_t;
#define MACRO_INLINE_PARAMS 8
struct macro {
  macro_t *next;
  char *name;
  token_t *body;
  bool is_funclike;
  bool is_variadic;  // 末尾が `...` (合成パラメータ "__VA_ARGS__") の可変長マクロ
  char **params;
  int num_params;    // 可変長時は合成 __VA_ARGS__ スロットを含む
  char *inline_params[MACRO_INLINE_PARAMS];
};

static macro_t *macros;

static token_pp_t *as_pp(token_t *tok) { return (token_pp_t *)tok; }
static token_ident_t *as_ident(token_t *tok) { return (token_ident_t *)tok; }
static token_string_t *as_string(token_t *tok) { return (token_string_t *)tok; }
static token_num_t *as_num(token_t *tok) { return (token_num_t *)tok; }

#define PP_MAX_INCLUDE_DEPTH 64
#define PP_MAX_MACRO_EXPANSIONS 20000
#define PP_MAX_LINE_FILENAME_LEN 1024
#define PP_MAX_INCLUDE_FILE_BYTES (16 * 1024 * 1024)
#define PP_MAX_IF_EXPR_TOKENS 4096
#define PP_MAX_IF_EXPR_EVAL_STEPS 2048
static const char *k_include_search_roots[] = {
    "",
    "include/",
};

typedef struct include_frame include_frame_t;
struct include_frame {
  include_frame_t *next;
  const char *path;
};

static include_frame_t *include_stack = NULL;
static int include_depth = 0;
static size_t macro_expand_steps = 0;
static int include_last_errno = 0;
static size_t if_expr_eval_steps = 0;
static void pp_error(diag_error_id_t id, const char *arg) __attribute__((noreturn));

static void if_expr_step_or_die(void) {
  if_expr_eval_steps++;
  if (if_expr_eval_steps > PP_MAX_IF_EXPR_EVAL_STEPS) {
    pp_error(DIAG_ERR_PREPROCESS_IF_EXPR_EVAL_LIMIT_EXCEEDED, NULL);
  }
}

static void record_include_errno(int err) {
  if (!err) return;
  if (include_last_errno == 0) {
    include_last_errno = err;
    return;
  }
  if (err == ELOOP) {
    include_last_errno = err;
    return;
  }
  if ((include_last_errno == ENOENT || include_last_errno == ENOTDIR) &&
      err != ENOENT && err != ENOTDIR) {
    include_last_errno = err;
  }
}

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

static void pp_error(diag_error_id_t id, const char *arg) __attribute__((noreturn));
static void pp_error(diag_error_id_t id, const char *arg) {
  const char *msg = diag_message_for(id);
  if (arg) diag_emit_internalf(id, msg, arg);
  diag_emit_internalf(id, "%s", msg);
}

static void validate_include_path_or_die(const char *path) {
  if (!path || !*path) {
    pp_error(DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, NULL);
  }
  if (isalpha((unsigned char)path[0]) && path[1] == ':') {
    pp_error(DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH, path);
  }
  if (path[0] == '/' || path[0] == '\\') {
    pp_error(DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH, path);
  }
  for (const char *p = path; *p; p++) {
    if (*p == '\\') {
      pp_error(DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH, path);
    }
    if ((p == path || p[-1] == '/') && p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
      pp_error(DIAG_ERR_PREPROCESS_PARENT_DIR_INCLUDE_FORBIDDEN, path);
    }
  }
}

static void validate_line_filename_or_die(const char *name, int len) {
  if (!name || len <= 0 || len > PP_MAX_LINE_FILENAME_LEN) {
    pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
  }
  for (int i = 0; i < len; ) {
    unsigned char c = (unsigned char)name[i];
    if (c < 0x20 || c == 0x7F) {
      pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
    }
    if (c < 0x80) {
      i++;
      continue;
    }
    if ((c & 0xE0) == 0xC0) {
      if (i + 1 >= len) pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
      unsigned char c1 = (unsigned char)name[i + 1];
      if ((c1 & 0xC0) != 0x80 || (c & 0xFE) == 0xC0) {
        pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
      }
      i += 2;
      continue;
    }
    if ((c & 0xF0) == 0xE0) {
      if (i + 2 >= len) pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
      unsigned char c1 = (unsigned char)name[i + 1];
      unsigned char c2 = (unsigned char)name[i + 2];
      if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
        pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
      }
      if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0)) {
        pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
      }
      i += 3;
      continue;
    }
    if ((c & 0xF8) == 0xF0) {
      if (i + 3 >= len) pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
      unsigned char c1 = (unsigned char)name[i + 1];
      unsigned char c2 = (unsigned char)name[i + 2];
      unsigned char c3 = (unsigned char)name[i + 3];
      if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
        pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
      }
      if ((c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 >= 0x90) || c > 0xF4) {
        pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
      }
      i += 4;
      continue;
    }
    pp_error(DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID, NULL);
  }
}

static bool path_is_within(const char *path, const char *base) {
  size_t n = strlen(base);
  if (n == 0) return false;
  return strncmp(path, base, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

static bool include_path_is_allowed(const char *resolved) {
  static bool roots_initialized = false;
  static char project_root[PATH_MAX];
  static char include_root[PATH_MAX];
  static bool have_project_root = false;
  static bool have_include_root = false;

  if (!roots_initialized) {
    roots_initialized = true;
    have_project_root = realpath(".", project_root) != NULL;
    have_include_root = realpath("include", include_root) != NULL;
  }

  return (have_project_root && path_is_within(resolved, project_root)) ||
         (have_include_root && path_is_within(resolved, include_root));
}

static void validate_include_realpath_or_die(const char *candidate, const char *display_path) {
  char resolved[PATH_MAX];
  if (!realpath(candidate, resolved)) return;
  if (!include_path_is_allowed(resolved)) {
    pp_error(DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH, display_path);
  }
}

static char *read_include_file_secure(const char *candidate, const char *display_path) {
  int oflags = O_RDONLY | O_CLOEXEC;
#ifdef O_NOFOLLOW
  oflags |= O_NOFOLLOW;
#endif
  int fd = open(candidate, oflags);
  if (fd < 0) {
    record_include_errno(errno);
    return NULL;
  }

  struct stat st;
  if (fstat(fd, &st) != 0) {
    record_include_errno(errno);
    close(fd);
    return NULL;
  }
  if (!S_ISREG(st.st_mode)) {
    record_include_errno(EINVAL);
    close(fd);
    return NULL;
  }
  if (st.st_size < 0 || (uintmax_t)st.st_size > PP_MAX_INCLUDE_FILE_BYTES) {
    record_include_errno(EFBIG);
    close(fd);
    return NULL;
  }

  FILE *fp = fdopen(fd, "r");
  if (!fp) {
    record_include_errno(errno);
    close(fd);
    return NULL;
  }

  char opened_path[PATH_MAX];
  bool have_opened_path = false;

#ifdef F_GETPATH
  if (fd >= 0 && fcntl(fd, F_GETPATH, opened_path) == 0) {
    have_opened_path = true;
  }
#endif
  if (!have_opened_path) {
    if (!realpath(candidate, opened_path)) {
      record_include_errno(errno);
      fclose(fp);
      return NULL;
    }
    have_opened_path = true;
  }
  if (have_opened_path && !include_path_is_allowed(opened_path)) {
    fclose(fp);
    pp_error(DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH, display_path);
  }

  if (fseek(fp, 0, SEEK_END) == -1) {
    record_include_errno(errno);
    fclose(fp);
    return NULL;
  }
  long file_size = ftell(fp);
  if (file_size < 0) {
    record_include_errno(errno);
    fclose(fp);
    return NULL;
  }
  size_t size = (size_t)file_size;
  if (size > SIZE_MAX - 2) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) == -1) {
    record_include_errno(errno);
    fclose(fp);
    return NULL;
  }

  char *buf = calloc(1, size + 2);
  if (!buf) {
    record_include_errno(ENOMEM);
    fclose(fp);
    return NULL;
  }
  if (fread(buf, 1, size, fp) != size) {
    record_include_errno(errno ? errno : EIO);
    fclose(fp);
    free(buf);
    return NULL;
  }
  if (size == 0 || buf[size - 1] != '\n') buf[size++] = '\n';
  buf[size] = '\0';
  fclose(fp);
  return buf;
}

static char *load_include_with_allowlist_or_die(const char *filename) {
  include_last_errno = 0;
  for (size_t i = 0; i < sizeof(k_include_search_roots) / sizeof(k_include_search_roots[0]); i++) {
    const char *root = k_include_search_roots[i];
    size_t cand_len = strlen(root) + strlen(filename) + 1;
    char *candidate = calloc(cand_len, 1);
    if (!candidate) {
      diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
    }
    snprintf(candidate, cand_len, "%s%s", root, filename);
    validate_include_realpath_or_die(candidate, filename);
    char *buf = read_include_file_secure(candidate, filename);
    free(candidate);
    if (buf) return buf;
  }
  return NULL;
}

static char *normalize_include_path_or_die(const char *path) {
  size_t n = strlen(path);
  char *out = calloc(n + 1, 1);
  if (!out) {
    pp_error(DIAG_ERR_INTERNAL_OOM, NULL);
  }
  size_t j = 0;
  size_t i = 0;

  while (path[i] == '.' && path[i + 1] == '/') i += 2;

  while (path[i]) {
    if (path[i] == '/') {
      out[j++] = '/';
      while (path[i] == '/') i++;
      if (path[i] == '.' && path[i + 1] == '/') {
        i += 2;
        continue;
      }
      continue;
    }
    out[j++] = path[i++];
  }
  if (j == 0) {
    out[j++] = '.';
  }
  out[j] = '\0';
  return out;
}

static void push_include_or_die(const char *path) {
  if (include_depth >= PP_MAX_INCLUDE_DEPTH) {
    pp_error(DIAG_ERR_PREPROCESS_INCLUDE_NEST_TOO_DEEP, NULL);
  }
  include_frame_t *f = calloc(1, sizeof(include_frame_t));
  if (!f) {
    pp_error(DIAG_ERR_INTERNAL_OOM, NULL);
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

static void count_macro_expansion_or_die(void) {
  macro_expand_steps++;
  if (macro_expand_steps > PP_MAX_MACRO_EXPANSIONS) {
    pp_error(DIAG_ERR_PREPROCESS_MACRO_EXPANSION_LIMIT_EXCEEDED, NULL);
  }
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

static void add_macro(char *name, bool is_funclike, bool is_variadic, char **params, int num_params, token_t *body) {
  macro_t *m = calloc(1, sizeof(macro_t));
  m->name = name;
  m->body = body;
  m->is_funclike = is_funclike;
  m->is_variadic = is_variadic;
  m->num_params = num_params;
  if (num_params <= MACRO_INLINE_PARAMS) {
    for (int i = 0; i < num_params; i++) m->inline_params[i] = params[i];
    m->params = m->inline_params;
  } else {
    m->params = params;
  }
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

// === pragma once ===
typedef struct pragma_once_entry pragma_once_entry_t;
struct pragma_once_entry {
  pragma_once_entry_t *next;
  char *path;
};
static pragma_once_entry_t *pragma_once_list = NULL;

static bool pragma_once_seen(const char *path) {
  for (pragma_once_entry_t *e = pragma_once_list; e; e = e->next) {
    if (!strcmp(e->path, path)) return true;
  }
  return false;
}

static void pragma_once_add(const char *path) {
  if (pragma_once_seen(path)) return;
  pragma_once_entry_t *e = calloc(1, sizeof(pragma_once_entry_t));
  e->path = my_strndup(path, strlen(path));
  e->next = pragma_once_list;
  pragma_once_list = e;
}

// === 定義済みマクロ初期化 ===
static token_t *make_int_token(long long val, token_t *ref) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", val);
  int slen = (int)strlen(buf);
  token_num_int_t *t = tk_allocator_calloc(1, sizeof(token_num_int_t));
  t->base.pp.base.kind = TK_NUM;
  if (ref) {
    t->base.pp.base.line_no  = ref->line_no;
    t->base.pp.base.file_name_id = ref->file_name_id;
    t->base.pp.base.at_bol   = ref->at_bol;
    t->base.pp.base.has_space = ref->has_space;
  }
  t->base.str      = my_strndup(buf, slen);
  t->base.len      = slen;
  t->base.num_kind = TK_NUM_KIND_INT;
  t->val           = val;
  t->uval          = (unsigned long long)val;
  t->int_size      = TK_INT_SIZE_INT;
  t->int_base      = 10;
  return (token_t *)t;
}

static token_t *make_string_token(const char *s, token_t *ref) {
  int slen = (int)strlen(s);
  token_string_t *t = tk_allocator_calloc(1, sizeof(token_string_t));
  t->pp.base.kind = TK_STRING;
  if (ref) {
    t->pp.base.line_no   = ref->line_no;
    t->pp.base.file_name_id = ref->file_name_id;
    t->pp.base.at_bol    = ref->at_bol;
    t->pp.base.has_space = ref->has_space;
  }
  t->str             = my_strndup(s, slen);
  t->len             = slen;
  t->char_width      = TK_CHAR_WIDTH_CHAR;
  t->str_prefix_kind = TK_STR_PREFIX_NONE;
  return (token_t *)t;
}

static void add_int_macro(const char *name, long long val) {
  token_t *tok = make_int_token(val, NULL);
  add_macro(my_strndup(name, strlen(name)), false, false, NULL, 0, tok);
}

static void add_string_macro(const char *name, const char *s) {
  token_t *tok = make_string_token(s, NULL);
  add_macro(my_strndup(name, strlen(name)), false, false, NULL, 0, tok);
}

static void pp_init_predefined_macros(void) {
  add_int_macro("__STDC__", 1);
  add_int_macro("__STDC_VERSION__", 201112LL);

  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  static const char *months[] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
  };
  char date_buf[16];
  char time_buf[10];
  snprintf(date_buf, sizeof(date_buf), "%s %2d %4d",
           months[tm_info->tm_mon], tm_info->tm_mday, tm_info->tm_year + 1900);
  snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
           tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
  add_string_macro("__DATE__", date_buf);
  add_string_macro("__TIME__", time_buf);
}

static hideset_t *new_hideset(char *name) {
  hideset_t *hs = tk_allocator_calloc(1, sizeof(hideset_t));
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
      token_ident_t *dst = tk_allocator_calloc(1, sizeof(token_ident_t));
      dst->pp.base = src->pp.base;
      dst->pp.hideset = src->pp.hideset;
      dst->str = src->str;
      dst->len = src->len;
      t = (token_t *)dst;
      break;
    }
    case TK_STRING: {
      token_string_t *src = as_string(tok);
      token_string_t *dst = tk_allocator_calloc(1, sizeof(token_string_t));
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
        token_num_int_t *dst = tk_allocator_calloc(1, sizeof(token_num_int_t));
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
        token_num_float_t *dst = tk_allocator_calloc(1, sizeof(token_num_float_t));
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
      token_pp_t *dst = tk_allocator_calloc(1, sizeof(token_pp_t));
      dst->base = src->base;
      dst->hideset = src->hideset;
      t = (token_t *)dst;
      break;
    }
  }

  t->next = NULL;
  return t;
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
static long add(token_t **rest, token_t *tok);

static long primary(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  if (tok->kind == TK_LPAREN) {
    long val = const_expr(&tok, tok->next);
    if (!(tok->kind == TK_RPAREN)) {
      pp_error(DIAG_ERR_PREPROCESS_RPAREN_REQUIRED, NULL);
    }
    *rest = tok->next;
    return val;
  }
  if (tok->kind == TK_NUM) {
    if (tk_as_num(tok)->num_kind != TK_NUM_KIND_INT) {
      pp_error(DIAG_ERR_PREPROCESS_IF_INT_LITERAL_REQUIRED, NULL);
    }
    long val = tk_as_num_int(tok)->val;
    *rest = tok->next;
    return val;
  }
  if (tok->kind == TK_IDENT) {
    *rest = tok->next;
    return 0; // undefined macro to 0
  }
  pp_error(DIAG_ERR_PREPROCESS_CONST_EXPR_UNEXPECTED_TOKEN, NULL);
}

static long unary(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
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
  if_expr_step_or_die();
  long val = unary(&tok, tok);
  for (;;) {
    if (tok->kind == TK_MUL) {
      if_expr_step_or_die();
      val *= unary(&tok, tok->next);
    } else if (tok->kind == TK_DIV) {
      if_expr_step_or_die();
      long rhs = unary(&tok, tok->next);
      if (rhs == 0) pp_error(DIAG_ERR_PREPROCESS_DIVISION_BY_ZERO, NULL);
      val /= rhs;
    } else if (tok->kind == TK_MOD) {
      if_expr_step_or_die();
      long rhs = unary(&tok, tok->next);
      if (rhs == 0) pp_error(DIAG_ERR_PREPROCESS_DIVISION_BY_ZERO, NULL);
      val %= rhs;
    } else {
      *rest = tok;
      return val;
    }
  }
}

/* シフト `<<` `>>` (加減算より低く、関係演算より高い)。 */
static long shift(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = add(&tok, tok);
  for (;;) {
    if (tok->kind == TK_SHL) {
      if_expr_step_or_die();
      val = val << add(&tok, tok->next);
    } else if (tok->kind == TK_SHR) {
      if_expr_step_or_die();
      val = val >> add(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long add(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = mul(&tok, tok);
  for (;;) {
    if (tok->kind == TK_PLUS) {
      if_expr_step_or_die();
      val += mul(&tok, tok->next);
    } else if (tok->kind == TK_MINUS) {
      if_expr_step_or_die();
      val -= mul(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long relational(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = shift(&tok, tok);
  for (;;) {
    if (tok->kind == TK_LT) {
      if_expr_step_or_die();
      val = val < shift(&tok, tok->next);
    } else if (tok->kind == TK_LE) {
      if_expr_step_or_die();
      val = val <= shift(&tok, tok->next);
    } else if (tok->kind == TK_GT) {
      if_expr_step_or_die();
      val = val > shift(&tok, tok->next);
    } else if (tok->kind == TK_GE) {
      if_expr_step_or_die();
      val = val >= shift(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long equality(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = relational(&tok, tok);
  for (;;) {
    if (tok->kind == TK_EQEQ) {
      if_expr_step_or_die();
      val = val == relational(&tok, tok->next);
    } else if (tok->kind == TK_NEQ) {
      if_expr_step_or_die();
      val = val != relational(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

/* ビット AND `&` (等価演算より低く、ビット XOR より高い)。 */
static long bitand(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = equality(&tok, tok);
  for (;;) {
    if (tok->kind == TK_AMP) {
      if_expr_step_or_die();
      val = val & equality(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

/* ビット XOR `^`。 */
static long bitxor(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = bitand(&tok, tok);
  for (;;) {
    if (tok->kind == TK_CARET) {
      if_expr_step_or_die();
      val = val ^ bitand(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

/* ビット OR `|`。 */
static long bitor(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = bitxor(&tok, tok);
  for (;;) {
    if (tok->kind == TK_PIPE) {
      if_expr_step_or_die();
      val = val | bitxor(&tok, tok->next);
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long logand(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = bitor(&tok, tok);
  for (;;) {
    if (tok->kind == TK_ANDAND) {
      if_expr_step_or_die();
      long rhs = equality(&tok, tok->next);
      val = val && rhs;
    } else {
      *rest = tok;
      return val;
    }
  }
}

static long logor(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long val = logand(&tok, tok);
  for (;;) {
    if (tok->kind == TK_OROR) {
      if_expr_step_or_die();
      long rhs = logand(&tok, tok->next);
      val = val || rhs;
    } else {
      *rest = tok;
      return val;
    }
  }
}

/* 条件演算子 `?:` (C11 6.10.1 が #if 定数式で要求)。最も低い優先順位。
 * 選択されない側も #if では評価されうる (定数式なので副作用なし) が、
 * ゼロ除算等を避けるため C のように選択側のみ評価する。 */
static long conditional(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long cond = logor(&tok, tok);
  if (tok->kind != TK_QUESTION) {
    *rest = tok;
    return cond;
  }
  if_expr_step_or_die();
  long then_val = const_expr(&tok, tok->next);
  if (tok->kind != TK_COLON) {
    pp_error(DIAG_ERR_PREPROCESS_RPAREN_REQUIRED, NULL);
  }
  long else_val = conditional(&tok, tok->next);
  *rest = tok;
  return cond ? then_val : else_val;
}

static long const_expr(token_t **rest, token_t *tok) {
  return conditional(rest, tok);
}

token_t *preprocess(token_t *tok); // front declaration
token_t *preprocess_ctx(tokenizer_context_t *tk_ctx, token_t *tok); // front declaration
static tokenizer_context_t *g_preprocess_tk_ctx;

/* 関数マクロの実引数を、代入前に独立して完全マクロ展開する (C11 6.10.3.1)。
 * 戻り値は TK_EOF 終端の展開済みリスト。include_depth を一時的に上げて
 * macro_expand_steps のリセットや定義済みマクロ再初期化を回避する
 * (展開ステップの暴走ガードは維持される)。 */
static token_t *pp_expand_arg(token_t *arg) {
  token_t *copy = copy_token_list(arg); // NULL 終端の深いコピー
  token_t *eof = tk_allocator_calloc(1, sizeof(token_t));
  eof->kind = TK_EOF;
  token_t *list;
  if (copy) {
    token_t *tail = copy;
    while (tail->next) tail = tail->next;
    tail->next = eof;
    list = copy;
  } else {
    list = eof;
  }
  int saved_depth = include_depth;
  include_depth++;
  token_t *expanded = preprocess_ctx(g_preprocess_tk_ctx, list);
  include_depth = saved_depth;
  return expanded;
}

static bool evaluate_constexpr(token_t **rest_tok, token_t *tok) {
   token_t head;
   head.next = NULL;
   token_t *cur = &head;
   size_t if_expr_token_count = 0;
   
   while (tok->kind != TK_EOF && !tok->at_bol) {
      if_expr_token_count++;
      if (if_expr_token_count > PP_MAX_IF_EXPR_TOKENS) {
        pp_error(DIAG_ERR_PREPROCESS_IF_EXPR_TOKEN_LIMIT_EXCEEDED, NULL);
      }
      cur->next = copy_token(tok);
      cur->next->at_bol = false; 
      cur = cur->next;
      tok = tok->next;
   }
   cur->next = tk_allocator_calloc(1, sizeof(token_t));
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
         if (t->kind != TK_IDENT) pp_error(DIAG_ERR_PREPROCESS_DEFINED_MACRO_NAME_REQUIRED, NULL);
         token_ident_t *id = as_ident(t);
         char *name = my_strndup(id->str, id->len);
         bool is_def = find_macro(name) != NULL;
         free(name);
         t = t->next;
         if (has_paren) {
            if (!(t->kind == TK_RPAREN)) {
              pp_error(DIAG_ERR_PREPROCESS_DEFINED_RPAREN_MISSING, NULL);
            }
            t = t->next;
         }
         
         token_num_int_t *num = tk_allocator_calloc(1, sizeof(token_num_int_t));
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
   cur2->next = tk_allocator_calloc(1, sizeof(token_t));
   cur2->next->kind = TK_EOF;

   token_t *expanded = preprocess_ctx(g_preprocess_tk_ctx, head2.next);

   if (expanded->kind == TK_EOF) return false;
   if_expr_eval_steps = 0;
   token_t *rest;
  long val = const_expr(&rest, expanded);
  if (rest->kind != TK_EOF) {
    pp_error(DIAG_ERR_PREPROCESS_CONST_EXPR_EXTRA_TOKEN, NULL);
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
            pp_error(DIAG_ERR_PREPROCESS_STRINGIZE_SIZE_TOO_LARGE, NULL);
          }
          cap *= 2;
          buf = xrealloc(buf, cap);
        }
        buf[len++] = ' ';
      }
    int tlen = 0;
    char *tmp_quoted = NULL;
    const char *ts;
    if (t->kind == TK_STRING) {
      /* C11 6.10.3.2: 文字列リテラルを stringize するときは囲みの `"` を保持し、
       * 内部の `"` と `\` の前に `\` を挿入する。token_text は引用符なしの内容
       * だけを返すため、ここで再構築する (これがないと STR("hi") が hi になる)。 */
      token_string_t *st = (token_string_t *)t;
      int slen = st->len < 0 ? 0 : st->len;
      tmp_quoted = malloc((size_t)slen * 2 + 3);
      size_t q = 0;
      tmp_quoted[q++] = '"';
      for (int i = 0; i < slen; i++) {
        char c = st->str[i];
        if (c == '"' || c == '\\') tmp_quoted[q++] = '\\';
        tmp_quoted[q++] = c;
      }
      tmp_quoted[q++] = '"';
      ts = tmp_quoted;
      tlen = (int)q;
    } else {
      ts = token_text(t, &tlen);
    }
    if (!ts) ts = "";
    if (tlen < 0 || (size_t)tlen > SIZE_MAX - len - 1) {
      pp_error(DIAG_ERR_PREPROCESS_STRINGIZE_SIZE_TOO_LARGE, NULL);
    }
    size_t need = len + (size_t)tlen + 1;
    while (need > cap) {
      if (cap > SIZE_MAX / 2) {
        pp_error(DIAG_ERR_PREPROCESS_STRINGIZE_SIZE_TOO_LARGE, NULL);
      }
      cap *= 2;
    }
    if (need > len + (size_t)tlen + 1) {
      pp_error(DIAG_ERR_PREPROCESS_STRINGIZE_INVALID_SIZE, NULL);
    }
    buf = xrealloc(buf, cap);
    memcpy(buf + len, ts, (size_t)tlen);
    len += (size_t)tlen;
    free(tmp_quoted);
  }
  buf[len] = '\0';
  
  char *str_buf = my_strndup(buf, len);
  free(buf);
  
  token_string_t *res = tk_allocator_calloc(1, sizeof(token_string_t));
  res->pp.base.kind = TK_STRING;
  res->str = str_buf;
  res->len = len;
  res->pp.base.file_name_id = macro_tok->file_name_id;
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
        pp_error(DIAG_ERR_PREPROCESS_TOKEN_PASTE_SIZE_TOO_LARGE, NULL);
      }
      size_t len = (size_t)len_l + (size_t)len_r;
      char *buf = calloc(1, len + 1);
      memcpy(buf, s_l, (size_t)len_l);
      memcpy(buf + len_l, s_r, (size_t)len_r);
      
      const char *saved_input = tk_get_user_input_ctx(g_preprocess_tk_ctx);
      const char *saved_filename = tk_get_filename_ctx(g_preprocess_tk_ctx);
      token_t *saved_token = tk_get_current_token_ctx(g_preprocess_tk_ctx);

      tk_set_filename_ctx(g_preprocess_tk_ctx, "<paste>");
      token_t *merged = tk_tokenize_ctx(g_preprocess_tk_ctx, buf);
      // Token-pasting must produce exactly one preprocessing token.
      if (merged->kind == TK_EOF || !merged->next || merged->next->kind != TK_EOF) {
        pp_error(DIAG_ERR_PREPROCESS_TOKEN_PASTE_INVALID_RESULT, NULL);
      }

      tk_set_filename_ctx(g_preprocess_tk_ctx, saved_filename);
      tk_set_user_input_ctx(g_preprocess_tk_ctx, saved_input);
      tk_set_current_token_ctx(g_preprocess_tk_ctx, saved_token);

      merged->next = rhs->next;
      merged->file_name_id = cur->file_name_id;
      merged->line_no = cur->line_no;
      cur = merged;
      prev->next = cur;
    } else {
      prev = cur;
      cur = cur->next;
    }
  }
  return head.next;
}

// プリプロセッサのメイン処理
token_t *preprocess(token_t *tok) {
  return preprocess_ctx(tk_get_default_context(), tok);
}

// 行頭（at_bol）まで進める。ディレクティブの末尾余剰トークンの読み飛ばし用。
static token_t *skip_to_next_line(token_t *tok) {
  while (tok->kind != TK_EOF && !tok->at_bol) tok = tok->next;
  return tok;
}

// 識別子トークンを必須として取り出し、my_strndup したコピーを返す。
// 失敗時は pp_error で中断。
static char *consume_required_macro_name(token_t **ptok) {
  if ((*ptok)->kind != TK_IDENT) pp_error(DIAG_ERR_PREPROCESS_MACRO_NAME_REQUIRED, NULL);
  token_ident_t *id = as_ident(*ptok);
  char *name = my_strndup(id->str, id->len);
  *ptok = (*ptok)->next;
  return name;
}

// 条件分岐スタックに新しいエントリを push。
static void push_cond_incl(bool included) {
  cond_incl_t *ci = calloc(1, sizeof(cond_incl_t));
  ci->ctx = IN_THEN;
  ci->included = included;
  ci->next = cond_incl;
  cond_incl = ci;
}

// #ifdef / #ifndef: マクロ定義の有無で then/else を選択。negated=true で #ifndef。
static token_t *handle_ifdef_or_ifndef(token_t *tok, bool negated) {
  tok = tok->next; // skip directive name
  char *name = consume_required_macro_name(&tok);
  bool defined = find_macro(name) != NULL;
  free(name);
  bool is_true = negated ? !defined : defined;
  push_cond_incl(is_true);
  tok = skip_to_next_line(tok);
  if (!is_true) tok = skip_cond_incl(tok);
  return tok;
}

static token_t *handle_else(token_t *tok) {
  if (!cond_incl) pp_error(DIAG_ERR_PREPROCESS_ELSE_WITHOUT_IF, NULL);
  if (cond_incl->ctx == IN_ELSE) pp_error(DIAG_ERR_PREPROCESS_DUPLICATE_ELSE, NULL);
  cond_incl->ctx = IN_ELSE;
  tok = skip_to_next_line(tok->next);
  if (cond_incl->included) {
    tok = skip_cond_incl(tok);
  } else {
    cond_incl->included = true;
  }
  return tok;
}

static token_t *handle_elif(token_t *tok) {
  if (!cond_incl) pp_error(DIAG_ERR_PREPROCESS_ELIF_WITHOUT_IF, NULL);
  if (cond_incl->ctx == IN_ELSE) pp_error(DIAG_ERR_PREPROCESS_ELIF_AFTER_ELSE, NULL);
  cond_incl->ctx = IN_ELIF;
  tok = tok->next;
  if (cond_incl->included) {
    tok = skip_to_next_line(tok);
    return skip_cond_incl(tok);
  }
  bool is_true = evaluate_constexpr(&tok, tok);
  if (is_true) cond_incl->included = true;
  if (!is_true) tok = skip_cond_incl(tok);
  return tok;
}

static token_t *handle_if(token_t *tok) {
  tok = tok->next;
  bool is_true = evaluate_constexpr(&tok, tok);
  push_cond_incl(is_true);
  if (!is_true) tok = skip_cond_incl(tok);
  return tok;
}

static token_t *handle_endif(token_t *tok) {
  if (!cond_incl) pp_error(DIAG_ERR_PREPROCESS_ENDIF_WITHOUT_IF, NULL);
  cond_incl_t *ci = cond_incl;
  cond_incl = cond_incl->next;
  free(ci);
  return skip_to_next_line(tok->next);
}

// "name.h" や <name.h> 形式のファイル名トークンを読み取り、calloc 済みの
// バッファに正規化前の文字列を組み立てて返す。tok は閉じ '>' or 文字列の次へ進む。
static char *consume_include_filename(token_t **ptok) {
  token_t *tok = *ptok;
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
      pp_error(DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, NULL);
    }
    if (need > filename_cap) {
      filename_cap = need;
      filename = xrealloc(filename, filename_cap);
    }
    memcpy(filename, st->str, (size_t)st->len);
    filename[st->len] = '\0';
    filename_len = (size_t)st->len;
    (void)filename_len;
    tok = tok->next;
  } else if (tok->kind == TK_LT) {
    tok = tok->next;
    while (tok->kind != TK_EOF && tok->kind != TK_GT) {
      int tlen = 0;
      const char *ts = token_text(tok, &tlen);
      if (!ts) ts = "";
      if (tlen < 0 || (size_t)tlen > SIZE_MAX - filename_len - 1) {
        pp_error(DIAG_ERR_PREPROCESS_INCLUDE_FILENAME_TOO_LARGE, NULL);
      }
      size_t need = filename_len + (size_t)tlen + 1;
      if (need > filename_cap) {
        while (filename_cap < need) {
          if (filename_cap > SIZE_MAX / 2) {
            pp_error(DIAG_ERR_PREPROCESS_INCLUDE_FILENAME_TOO_LARGE, NULL);
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
      pp_error(DIAG_ERR_PREPROCESS_GT_REQUIRED, NULL);
    }
    tok = tok->next; // '>' をスキップ
  }
  *ptok = tok;
  return filename;
}

// include_last_errno に応じて適切な診断 ID を選ぶ。
static diag_error_id_t include_read_failure_diag_id(void) {
  if (include_last_errno == ENOENT) return DIAG_ERR_PREPROCESS_INCLUDE_NOT_FOUND;
  if (include_last_errno == EACCES || include_last_errno == EPERM) return DIAG_ERR_PREPROCESS_INCLUDE_PERMISSION_DENIED;
  if (include_last_errno == ELOOP) return DIAG_ERR_PREPROCESS_INCLUDE_SYMLINK_LOOP;
  return DIAG_ERR_PREPROCESS_INCLUDE_READ_FAILED;
}

// インクルードしたファイルをトークナイズ・前処理し、結果を **pcur のチェーン末尾へ
// 連結する。tk_ctx の filename/input/current_token は呼び出し前後で保存・復元される。
static void include_and_splice(char *filename, token_t **pcur) {
  char *buf = load_include_with_allowlist_or_die(filename);
  if (!buf) {
    diag_error_id_t id = include_read_failure_diag_id();
    diag_emit_internalf(id, diag_message_for(id), filename);
    free(filename);
  }
  const char *saved_input = tk_get_user_input_ctx(g_preprocess_tk_ctx);
  const char *saved_filename = tk_get_filename_ctx(g_preprocess_tk_ctx);
  token_t *saved_token = tk_get_current_token_ctx(g_preprocess_tk_ctx);
  tk_set_filename_ctx(g_preprocess_tk_ctx, my_strndup(filename, strlen(filename)));
  token_t *tok2 = tk_tokenize_ctx(g_preprocess_tk_ctx, buf);
  push_include_or_die(filename);
  tok2 = preprocess_ctx(g_preprocess_tk_ctx, tok2);
  pop_include();
  tk_set_filename_ctx(g_preprocess_tk_ctx, saved_filename);
  tk_set_user_input_ctx(g_preprocess_tk_ctx, saved_input);
  tk_set_current_token_ctx(g_preprocess_tk_ctx, saved_token);
  while (tok2->kind != TK_EOF) {
    (*pcur)->next = copy_token(tok2);
    *pcur = (*pcur)->next;
    tok2 = tok2->next;
  }
}

// #include "name.h" または #include <name.h>
static token_t *handle_include(token_t *tok, token_t **pcur) {
  tok = tok->next; // skip "include"
  char *filename = consume_include_filename(&tok);
  validate_include_path_or_die(filename);
  char *normalized = normalize_include_path_or_die(filename);
  free(filename);
  filename = normalized;
  if (pragma_once_seen(filename)) {
    free(filename);
    return tok;
  }
  include_and_splice(filename, pcur);
  free(filename);
  return tok;
}

// #define MACRO_NAME [( params )] body...
static token_t *handle_define(token_t *tok) {
  tok = tok->next;
  if (tok->kind != TK_IDENT) {
    pp_error(DIAG_ERR_PREPROCESS_MACRO_NAME_REQUIRED, NULL);
  }
  token_ident_t *id = as_ident(tok);
  char *name = my_strndup(id->str, id->len);
  tok = tok->next;

  bool is_funclike = false;
  bool is_variadic = false;
  char **params = NULL;
  int num_params = 0;
  char *inline_params_buf[MACRO_INLINE_PARAMS];

  if (tok->kind == TK_LPAREN && !tok->has_space) {
    is_funclike = true;
    tok = tok->next;
    int cap = MACRO_INLINE_PARAMS;
    params = inline_params_buf;
    while (tok->kind != TK_EOF && tok->kind != TK_RPAREN) {
      // 容量拡張は ident / `...` のどちらの追記より前に行う。
      if (num_params >= cap) {
        if (params == inline_params_buf) {
          params = calloc((size_t)cap * 2, sizeof(char *));
          for (int j = 0; j < num_params; j++) params[j] = inline_params_buf[j];
        } else {
          params = xreallocarray(params, (size_t)cap * 2, sizeof(char *));
        }
        cap *= 2;
      }
      if (tok->kind == TK_ELLIPSIS) {
        // C99 6.10.3: `...` は最後のパラメータでなければならない。
        if (tok->next->kind != TK_RPAREN) {
          pp_error(DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT, NULL);
        }
        params[num_params++] = my_strndup("__VA_ARGS__", 11);
        is_variadic = true;
        tok = tok->next; // `)` へ
        break;
      }
      if (tok->kind != TK_IDENT) pp_error(DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT, NULL);
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

  add_macro(name, is_funclike, is_variadic, params, num_params, head.next);
  return tok;
}

// マクロを名前で連結リストから外す（freeはしない: name 文字列は他で保持される可能性）。
static void remove_macro_by_name(const char *name) {
  macro_t *prev = NULL;
  for (macro_t *m = macros; m; prev = m, m = m->next) {
    if (!strcmp(m->name, name)) {
      if (prev) prev->next = m->next;
      else macros = m->next;
      break;
    }
  }
}

static token_t *handle_undef(token_t *tok) {
  tok = tok->next;
  char *name = consume_required_macro_name(&tok);
  remove_macro_by_name(name);
  free(name);
  return skip_to_next_line(tok);
}

// #error directive: 残りトークンを文字列化して診断を出す。
static token_t *handle_error(token_t *tok) {
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
        if (cap > SIZE_MAX / 2) pp_error(DIAG_ERR_PREPROCESS_ERROR_MESSAGE_TOO_LARGE, NULL);
        cap *= 2;
      }
      msg = xrealloc(msg, cap);
      memcpy(msg + len, ts, (size_t)tlen);
      len += (size_t)tlen;
      msg[len] = '\0';
    }
    if (tok->has_space) {
      if (len + 2 > cap) {
        if (cap > SIZE_MAX / 2) pp_error(DIAG_ERR_PREPROCESS_ERROR_MESSAGE_TOO_LARGE, NULL);
        cap *= 2;
        msg = xrealloc(msg, cap);
      }
      msg[len++] = ' ';
      msg[len] = '\0';
    }
    tok = tok->next;
  }
  const char *detail = msg;
  if (strncmp(detail, prefix, pfx_len) == 0) {
    detail += pfx_len;
  }
  diag_emit_internalf(DIAG_ERR_PREPROCESS_ERROR_DIRECTIVE,
                      diag_message_for(DIAG_ERR_PREPROCESS_ERROR_DIRECTIVE), detail);
  return tok;
}

// #line N ["filename"]: 以降のトークンの line_no / file_name_id を書き換える。
static token_t *handle_line(token_t *tok) {
  tok = tok->next;
  if (!(tok && tok->kind == TK_NUM && tk_as_num(tok)->num_kind == TK_NUM_KIND_INT)) {
    return skip_to_next_line(tok);
  }
  long long new_line = tk_as_num_int(tok)->val;
  if (new_line <= 0 || new_line > INT_MAX) {
    pp_error(DIAG_ERR_PREPROCESS_LINE_NUMBER_INVALID, NULL);
  }
  tok = tok->next;
  char *new_file = NULL;
  if (tok && tok->kind == TK_STRING) {
    token_string_t *st = as_string(tok);
    validate_line_filename_or_die(st->str, st->len);
    new_file = my_strndup(st->str, st->len);
    tok = tok->next;
  }
  tok = skip_to_next_line(tok);
  if (tok->kind != TK_EOF) {
    long long offset = new_line - (long long)tok->line_no;
    for (token_t *t = tok; t && t->kind != TK_EOF; t = t->next) {
      t->line_no = (int)((long long)t->line_no + offset);
      if (new_file) t->file_name_id = tk_filename_intern(new_file);
    }
  }
  return tok;
}

// `#pragma pack(...)` の中身を解釈し、出力チェーンに pragma_pack マーカーを追加する。
// tok は '(' の次を指す状態で呼ばれ、終了時には ')' を消費した後 (もしくは行末) を指す。
static token_t *handle_pragma_pack_body(token_t *tok, token_t **pcur) {
  if (ident_is(tok, "push")) {
    tok = tok->next;
    if (tok->kind == TK_COMMA) {
      tok = tok->next;
      if (tok->kind == TK_NUM) {
        token_t *marker = make_int_token(((token_num_int_t *)tok)->val, tok);
        marker->kind = TK_PRAGMA_PACK_PUSH;
        (*pcur)->next = marker;
        *pcur = (*pcur)->next;
        tok = tok->next;
      }
    }
  } else if (ident_is(tok, "pop")) {
    tok = tok->next;
    token_t *marker = tk_allocator_calloc(1, sizeof(token_t));
    marker->kind = TK_PRAGMA_PACK_POP;
    (*pcur)->next = marker;
    *pcur = (*pcur)->next;
  } else if (tok->kind == TK_NUM) {
    token_t *marker = make_int_token(((token_num_int_t *)tok)->val, tok);
    marker->kind = TK_PRAGMA_PACK_SET;
    (*pcur)->next = marker;
    *pcur = (*pcur)->next;
    tok = tok->next;
  } else if (tok->kind == TK_RPAREN) {
    token_t *marker = tk_allocator_calloc(1, sizeof(token_t));
    marker->kind = TK_PRAGMA_PACK_RESET;
    (*pcur)->next = marker;
    *pcur = (*pcur)->next;
  }
  while (tok->kind != TK_RPAREN && tok->kind != TK_EOF && !tok->at_bol) tok = tok->next;
  if (tok->kind == TK_RPAREN) tok = tok->next;
  return tok;
}

static token_t *handle_pragma(token_t *tok, token_t **pcur) {
  tok = tok->next;
  if (ident_is(tok, "once")) {
    tok = tok->next;
    if (include_stack) {
      pragma_once_add(include_stack->path);
    }
  } else if (ident_is(tok, "pack")) {
    tok = tok->next;
    if (tok->kind == TK_LPAREN) {
      tok = tok->next;
      tok = handle_pragma_pack_body(tok, pcur);
    }
  }
  return skip_to_next_line(tok);
}

// プリプロセッサのメイン処理（Tokenizerコンテキスト明示版）
token_t *preprocess_ctx(tokenizer_context_t *tk_ctx, token_t *tok) {
  tokenizer_context_t *prev_tk_ctx = g_preprocess_tk_ctx;
  g_preprocess_tk_ctx = tk_ctx ? tk_ctx : tk_get_default_context();
  if (include_depth == 0) {
    macro_expand_steps = 0;
    pp_init_predefined_macros();
  }

  token_t head;
  head.next = NULL;
  token_t *cur = &head;

  while (tok->kind != TK_EOF) {
      // 行頭かつ '#' 記号の場合はディレクティブ行として処理
    if (tok->at_bol && tok->kind == TK_HASH) {
      tok = tok->next; // '#' をスキップ
      
      if (is_dir(tok, "include")) { tok = handle_include(tok, &cur); continue; }

      if (is_dir(tok, "ifdef"))  { tok = handle_ifdef_or_ifndef(tok, false); continue; }
      if (is_dir(tok, "ifndef")) { tok = handle_ifdef_or_ifndef(tok, true);  continue; }
      if (is_dir(tok, "else"))   { tok = handle_else(tok);  continue; }
      if (is_dir(tok, "elif"))   { tok = handle_elif(tok);  continue; }
      if (is_dir(tok, "if"))     { tok = handle_if(tok);    continue; }
      if (is_dir(tok, "endif"))  { tok = handle_endif(tok); continue; }
      
      if (is_dir(tok, "define")) { tok = handle_define(tok); continue; }
      if (is_dir(tok, "undef"))  { tok = handle_undef(tok);  continue; }
      if (is_dir(tok, "error"))  { tok = handle_error(tok); }
      if (is_dir(tok, "line"))   { tok = handle_line(tok);   continue; }
      if (is_dir(tok, "pragma")) { tok = handle_pragma(tok, &cur); continue; }

      // ひとまず改行（次の行頭）またはEOFまでトークンを読み飛ばす
      while (tok->kind != TK_EOF && !tok->at_bol) {
        tok = tok->next;
      }
      continue;
    }

    if (tok->kind == TK_IDENT) {
      token_ident_t *id = as_ident(tok);
      char *name = my_strndup(id->str, id->len);

      if (!strcmp(name, "__LINE__")) {
        free(name);
        token_t *lt = make_int_token(tok->line_no, tok);
        cur->next = lt;
        cur = cur->next;
        tok = tok->next;
        continue;
      }
      if (!strcmp(name, "__FILE__")) {
        free(name);
        const char *fn = tk_filename_lookup(tok->file_name_id);
        const char *fname = fn ? fn : "";
        token_t *ft = make_string_token(fname, tok);
        cur->next = ft;
        cur = cur->next;
        tok = tok->next;
        continue;
      }

      macro_t *m = find_macro(name);
      
      if (m && !hideset_contains(as_pp(tok)->hideset, name)) {
        count_macro_expansion_or_die();
        if (m->is_funclike) {
           if (tok->next && tok->next->kind == TK_LPAREN) {
             token_t *macro_tok = tok;
             tok = tok->next->next; // skip macro name and '('
             
             token_t **args = calloc(m->num_params > 0 ? m->num_params : 1, sizeof(token_t*));
             int arg_cnt = 0;
             int parsed_args = 0;
             bool has_empty_arg = false;
             // 可変長マクロでは末尾の合成 __VA_ARGS__ スロットを除いた数が名前付き引数。
             int num_named = m->is_variadic ? m->num_params - 1 : m->num_params;
             if (!(tok->kind == TK_RPAREN)) {
               while (tok->kind != TK_EOF) {
                 token_t head_arg; head_arg.next = NULL;
                 token_t *cur_arg = &head_arg;
                 int nest = 0;
                 // 名前付き引数を埋め終えた可変長マクロでは、このグループが
                 // __VA_ARGS__ の本体。トップレベルのカンマも取り込み `)` まで集約する。
                 bool collecting_va = m->is_variadic && (arg_cnt >= num_named);
                 while (tok->kind != TK_EOF) {
                   if (nest == 0 && tok->kind == TK_RPAREN) break;
                   if (nest == 0 && tok->kind == TK_COMMA && !collecting_va) break;
                   if (tok->kind == TK_LPAREN) nest++;
                   if (tok->kind == TK_RPAREN) nest--;
                   cur_arg->next = copy_token(tok);
                   cur_arg = cur_arg->next;
                   tok = tok->next;
                 }
                 parsed_args++;
                 if (!head_arg.next && !collecting_va) has_empty_arg = true;
                 if (arg_cnt < m->num_params) args[arg_cnt++] = head_arg.next;
                 if (collecting_va) break; // 可変長部を `)` まで集約済み
                 if (tok->kind == TK_COMMA) tok = tok->next;
                 else break;
               }
             }
             if (tok->kind != TK_RPAREN) {
               pp_error(DIAG_ERR_PREPROCESS_FUNC_MACRO_ARG_NOT_CLOSED, NULL);
             }
             /* C99 6.10.3p4: マクロ実引数は空でもよい (空引数は placemarker として
              * 何も展開しない)。`F(7,)` / `F(,x)` / `F(a,,c)` はすべて合法なので
              * has_empty_arg でエラーにしない。引数の個数のみ検査する。 */
             (void)has_empty_arg;
             if (m->is_variadic) {
               // 可変長部 (__VA_ARGS__) は空でもよい。clang/gcc は `F(a,...)` を `F(42)`
               // で呼べる (空 __VA_ARGS__、C23 で標準化)。名前付き引数より少ない場合だけエラー。
               if (parsed_args < num_named) {
                 pp_error(DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT, NULL);
               }
             } else {
               if (parsed_args != m->num_params) {
                 pp_error(DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT, NULL);
               }
             }
             tok = tok->next; // skip ')'

             token_t *prev_body = NULL;
             for (token_t *bt = m->body; bt; bt = bt->next) {
               if (bt->kind != TK_HASHHASH) {
                 prev_body = bt;
                 continue;
               }
               if (!prev_body || !bt->next) {
                 pp_error(DIAG_ERR_PREPROCESS_MACRO_TOKEN_PASTE_INVALID_POSITION, NULL);
               }
               if (prev_body->kind == TK_HASHHASH || bt->next->kind == TK_HASHHASH || bt->next->kind == TK_HASH) {
                 pp_error(DIAG_ERR_PREPROCESS_MACRO_TOKEN_PASTE_INVALID_POSITION, NULL);
               }
               prev_body = bt;
             }
             
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
                   /* C11 6.10.3.1: 実引数は # / ## の対象でない限り、代入前に
                    * 完全展開する。## に隣接する場合は未展開のまま代入する。 */
                   token_t *pv = NULL;
                   for (token_t *b = m->body; b && b != t; b = b->next) pv = b;
                   bool paste_operand = (t->next && t->next->kind == TK_HASHHASH) ||
                                        (pv && pv->kind == TK_HASHHASH);
                   token_t *sub = paste_operand ? args[p_idx] : pp_expand_arg(args[p_idx]);
                   for (token_t *a = sub; a && a->kind != TK_EOF; a = a->next) {
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
               /* 展開結果はマクロ呼び出し位置にあるものとして再配置する (C11 6.10.3)。
                * これがないとマクロ本体内の __LINE__/__FILE__ が #define の位置を指していた
                * (本体トークンの line_no が定義行のままだったため)。 */
               t->line_no = macro_tok->line_no;
               t->file_name_id = macro_tok->file_name_id;
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
             /* 展開結果はマクロ呼び出し位置にあるものとして再配置 (C11 6.10.3)。
              * オブジェクト形式マクロ本体内の __LINE__/__FILE__ も呼び出し行を指すようにする。 */
             t->line_no = tok->line_no;
             t->file_name_id = tok->file_name_id;
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
  g_preprocess_tk_ctx = prev_tk_ctx;
  return head.next;
}

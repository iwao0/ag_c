#include "preprocess.h"
#include "../parser/config_runtime.h"
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
/* アライメント降順 (ポインタ/配列 → int → bool) に並べてパディングを除き sizeof=104B
 * (並べ替え前は 112B)。 */
struct macro {
  macro_t *next;
  char *name;
  token_t *body;
  char **params;
  char *inline_params[MACRO_INLINE_PARAMS];
  int num_params;    // 可変長時は合成 __VA_ARGS__ スロットを含む
  bool is_funclike;
  bool is_variadic;  // 末尾が `...` (合成パラメータ "__VA_ARGS__") の可変長マクロ
};

static macro_t *macros;

static token_pp_t *as_pp(token_t *tok) { return (token_pp_t *)tok; }
static token_ident_t *as_ident(token_t *tok) { return (token_ident_t *)tok; }
static token_string_t *as_string(token_t *tok) { return (token_string_t *)tok; }
static token_num_t *as_num(token_t *tok) { return (token_num_t *)tok; }

#define PP_MAX_INCLUDE_DEPTH 64
#define PP_MAX_MACRO_EXPANSIONS 262144
#define PP_MAX_LINE_FILENAME_LEN 1024
#define PP_MAX_INCLUDE_FILE_BYTES (16 * 1024 * 1024)
#define PP_MAX_IF_EXPR_TOKENS 4096
#define PP_MAX_IF_EXPR_EVAL_STEPS 8192
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

typedef struct {
  const char *path;
  const char *source;
  size_t source_len;
} pp_virtual_header_t;

static pp_virtual_header_t *g_virtual_headers;
static int g_virtual_header_count;
static int g_virtual_headers_enabled;
static int g_virtual_include_depth_limit = PP_MAX_INCLUDE_DEPTH;

static char *normalize_include_path_or_die(const char *path);
static char *dirname_dup_or_null(const char *path);
static char *my_strndup(const char *s, size_t n);
static size_t if_expr_eval_steps = 0;
/* false のとき #if 定数式をトークン消費のみ (短絡評価の未選択側)。 */
static bool g_if_expr_eval = true;
static void pp_error(diag_error_id_t id, const char *arg) __attribute__((noreturn));
static const char *k_pp_month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};
static char pp_date_buf[16];
static char pp_time_buf[10];

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

void pp_virtual_headers_clear(void) {
  free(g_virtual_headers);
  g_virtual_headers = NULL;
  g_virtual_header_count = 0;
  g_virtual_headers_enabled = 0;
  g_virtual_include_depth_limit = PP_MAX_INCLUDE_DEPTH;
}

static uint32_t virtual_bundle_u32(const unsigned char *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int virtual_path_is_canonical(const char *path) {
  if (!path || !path[0] || strlen(path) > PP_MAX_LINE_FILENAME_LEN ||
      path[0] == '/' || path[0] == '\\' || strchr(path, '\\') || strchr(path, ':')) {
    return 0;
  }
  const char *segment = path;
  for (const char *p = path; ; p++) {
    if (*p != '/' && *p != '\0') continue;
    size_t len = (size_t)(p - segment);
    if (len == 0 || (len == 1 && segment[0] == '.') ||
        (len == 2 && segment[0] == '.' && segment[1] == '.')) {
      return 0;
    }
    if (*p == '\0') return 1;
    segment = p + 1;
  }
}

static diag_error_id_t virtual_path_error(const char *path) {
  if (!path || !path[0]) return DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME;
  if (strlen(path) > PP_MAX_LINE_FILENAME_LEN) {
    return DIAG_ERR_PREPROCESS_INCLUDE_FILENAME_TOO_LARGE;
  }
  if (path[0] == '/' || path[0] == '\\' || strchr(path, '\\') || strchr(path, ':')) {
    return DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH;
  }
  const char *segment = path;
  for (const char *p = path; ; p++) {
    if (*p != '/' && *p != '\0') continue;
    size_t len = (size_t)(p - segment);
    if (len == 0 || (len == 1 && segment[0] == '.')) {
      return DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH;
    }
    if (len == 2 && segment[0] == '.' && segment[1] == '.') {
      return DIAG_ERR_PREPROCESS_PARENT_DIR_INCLUDE_FORBIDDEN;
    }
    if (*p == '\0') return 0;
    segment = p + 1;
  }
}

static void validate_virtual_path_or_die(const char *path) {
  diag_error_id_t id = virtual_path_error(path);
  if (!id) return;
  if (id == DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH ||
      id == DIAG_ERR_PREPROCESS_PARENT_DIR_INCLUDE_FORBIDDEN) {
    pp_error(id, path);
  }
  pp_error(id, NULL);
}

void pp_virtual_headers_configure(const unsigned char *bundle, size_t bundle_len,
                                  int max_files, int max_file_bytes,
                                  int max_total_bytes, int max_include_depth) {
  pp_virtual_headers_clear();
  if (!bundle || bundle_len < 4 || max_files < 0 || max_file_bytes < 0 ||
      max_total_bytes < 0 || max_include_depth <= 0 ||
      max_include_depth > PP_MAX_INCLUDE_DEPTH) {
    pp_error(DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, NULL);
  }
  uint32_t count = virtual_bundle_u32(bundle);
  if (count > (uint32_t)max_files || count > (uint32_t)INT_MAX) {
    pp_error(DIAG_ERR_PREPROCESS_VIRTUAL_HEADER_COUNT_LIMIT_EXCEEDED, NULL);
  }
  pp_virtual_header_t *headers = calloc(count ? count : 1, sizeof(*headers));
  if (!headers) pp_error(DIAG_ERR_INTERNAL_OOM, NULL);
  size_t offset = 4;
  size_t total = 0;
  for (uint32_t i = 0; i < count; i++) {
    if (offset > bundle_len || bundle_len - offset < 8) {
      free(headers);
      pp_error(DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, NULL);
    }
    uint32_t path_len = virtual_bundle_u32(bundle + offset);
    uint32_t source_len = virtual_bundle_u32(bundle + offset + 4);
    offset += 8;
    size_t need = (size_t)path_len + 1 + (size_t)source_len + 1;
    if (need < (size_t)path_len || offset > bundle_len || need > bundle_len - offset) {
      free(headers);
      pp_error(DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, NULL);
    }
    const char *path = (const char *)(bundle + offset);
    const char *source = path + path_len + 1;
    if (path[path_len] != '\0' || source[source_len] != '\0' ||
        memchr(path, '\0', path_len) || memchr(source, '\0', source_len)) {
      free(headers);
      pp_error(DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, NULL);
    }
    diag_error_id_t path_error = virtual_path_error(path);
    if (path_error) {
      free(headers);
      if (path_error == DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH ||
          path_error == DIAG_ERR_PREPROCESS_PARENT_DIR_INCLUDE_FORBIDDEN) {
        pp_error(path_error, path);
      }
      pp_error(path_error, NULL);
    }
    if (source_len > (uint32_t)max_file_bytes) {
      free(headers);
      pp_error(DIAG_ERR_PREPROCESS_VIRTUAL_HEADER_FILE_SIZE_LIMIT_EXCEEDED, path);
    }
    if (source_len > (uint32_t)max_total_bytes ||
        total > (size_t)max_total_bytes - (size_t)source_len) {
      free(headers);
      pp_error(DIAG_ERR_PREPROCESS_VIRTUAL_HEADER_TOTAL_SIZE_LIMIT_EXCEEDED, NULL);
    }
    total += source_len;
    for (uint32_t j = 0; j < i; j++) {
      if (!strcmp(headers[j].path, path)) {
        free(headers);
        pp_error(DIAG_ERR_PREPROCESS_VIRTUAL_HEADER_DUPLICATE_PATH, path);
      }
    }
    headers[i].path = path;
    headers[i].source = source;
    headers[i].source_len = source_len;
    offset += need;
  }
  if (offset != bundle_len) {
    free(headers);
    pp_error(DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, NULL);
  }
  g_virtual_headers = headers;
  g_virtual_header_count = (int)count;
  g_virtual_headers_enabled = 1;
  g_virtual_include_depth_limit = max_include_depth;
}

static const pp_virtual_header_t *find_virtual_header(const char *path) {
  for (int i = 0; i < g_virtual_header_count; i++) {
    if (!strcmp(g_virtual_headers[i].path, path)) return &g_virtual_headers[i];
  }
  return NULL;
}

static const pp_virtual_header_t *resolve_virtual_header(const char *path,
                                                         const char *current_file,
                                                         char **out_path) {
  validate_virtual_path_or_die(path);
  if (virtual_path_is_canonical(current_file)) {
    char *dir = dirname_dup_or_null(current_file);
    if (dir) {
      size_t len = strlen(dir) + strlen(path) + 1;
      char *candidate = calloc(len, 1);
      if (!candidate) pp_error(DIAG_ERR_INTERNAL_OOM, NULL);
      snprintf(candidate, len, "%s%s", dir, path);
      free(dir);
      validate_virtual_path_or_die(candidate);
      const pp_virtual_header_t *relative = find_virtual_header(candidate);
      if (relative) {
        *out_path = candidate;
        return relative;
      }
      free(candidate);
    }
  }
  const pp_virtual_header_t *direct = find_virtual_header(path);
  if (direct) {
    *out_path = my_strndup(path, strlen(path));
    return direct;
  }
  return NULL;
}

static void validate_include_path_or_die(const char *path, const char *current_file) {
  if (!path || !*path) {
    pp_error(DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, NULL);
  }
  int allow_parent_ref = current_file && strncmp(current_file, "src/", 4) == 0;
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
    if (!allow_parent_ref &&
        (p == path || p[-1] == '/') && p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
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

static char *dirname_dup_or_null(const char *path) {
  if (!path) return NULL;
  const char *slash = strrchr(path, '/');
  if (!slash) return NULL;
  size_t len = (size_t)(slash - path + 1);
  char *dir = calloc(len + 1, 1);
  if (!dir) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  memcpy(dir, path, len);
  dir[len] = '\0';
  return dir;
}

static char *try_load_include_candidate(const char *root, const char *filename, char **out_loaded_path) {
  size_t cand_len = strlen(root) + strlen(filename) + 1;
  char *candidate = calloc(cand_len, 1);
  if (!candidate) {
    diag_emit_internalf(DIAG_ERR_INTERNAL_OOM, "%s", diag_message_for(DIAG_ERR_INTERNAL_OOM));
  }
  snprintf(candidate, cand_len, "%s%s", root, filename);
  validate_include_realpath_or_die(candidate, filename);
  char *buf = read_include_file_secure(candidate, filename);
  if (buf && out_loaded_path) {
    *out_loaded_path = normalize_include_path_or_die(candidate);
  }
  free(candidate);
  return buf;
}

static char *load_include_with_allowlist_or_die(const char *filename, const char *current_file,
                                                char **out_loaded_path) {
  include_last_errno = 0;
  if (out_loaded_path) *out_loaded_path = NULL;
  char *current_dir = dirname_dup_or_null(current_file);
  if (current_dir) {
    char *buf = try_load_include_candidate(current_dir, filename, out_loaded_path);
    free(current_dir);
    if (buf) return buf;
  }
  for (size_t i = 0; i < sizeof(k_include_search_roots) / sizeof(k_include_search_roots[0]); i++) {
    char *buf = try_load_include_candidate(k_include_search_roots[i], filename, out_loaded_path);
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
  if (g_virtual_headers_enabled) {
    for (include_frame_t *f = include_stack; f; f = f->next) {
      if (!strcmp(f->path, path)) {
        pp_error(DIAG_ERR_PREPROCESS_INCLUDE_CYCLE_DETECTED, path);
      }
    }
  }
  int depth_limit = g_virtual_headers_enabled ? g_virtual_include_depth_limit : PP_MAX_INCLUDE_DEPTH;
  if (include_depth >= depth_limit) {
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

static void reset_pragma_once_list(void) {
  while (pragma_once_list) {
    pragma_once_entry_t *entry = pragma_once_list;
    pragma_once_list = entry->next;
    free(entry->path);
    free(entry);
  }
}

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
static void copy_source_location(token_t *dst, const token_t *src) {
  if (!dst || !src) return;
  dst->line_no = src->line_no;
  dst->file_name_id = src->file_name_id;
  dst->source_input = src->source_input;
  dst->byte_offset = src->byte_offset;
  dst->byte_length = src->byte_length;
}

static token_t *make_int_token(long long val, token_t *ref) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", val);
  int slen = (int)strlen(buf);
  token_num_int_t *t = tk_allocator_calloc(1, sizeof(token_num_int_t));
  t->base.pp.base.kind = TK_NUM;
  if (ref) {
    copy_source_location(&t->base.pp.base, ref);
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
    copy_source_location(&t->pp.base, ref);
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
  /* Apple Silicon ARM64 は LP64 (int=4, long/pointer=8)。 */
  add_int_macro("__LP64__", 1);
  if (ps_get_target_pointer_size() == 4) {
    add_int_macro("__wasm32__", 1);
  }

  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  snprintf(pp_date_buf, sizeof(pp_date_buf), "%s %2d %4d",
           k_pp_month_names[tm_info->tm_mon], tm_info->tm_mday, tm_info->tm_year + 1900);
  snprintf(pp_time_buf, sizeof(pp_time_buf), "%02d:%02d:%02d",
           tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
  add_string_macro("__DATE__", pp_date_buf);
  add_string_macro("__TIME__", pp_time_buf);
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
static void skip_const_expr(token_t **rest, token_t *tok);
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
      if (g_if_expr_eval) {
        if (rhs == 0) pp_error(DIAG_ERR_PREPROCESS_DIVISION_BY_ZERO, NULL);
        val /= rhs;
      }
    } else if (tok->kind == TK_MOD) {
      if_expr_step_or_die();
      long rhs = unary(&tok, tok->next);
      if (g_if_expr_eval) {
        if (rhs == 0) pp_error(DIAG_ERR_PREPROCESS_DIVISION_BY_ZERO, NULL);
        val %= rhs;
      }
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
      if (val) {
        long rhs = bitor(&tok, tok->next);
        val = val && rhs;
      } else {
        skip_const_expr(&tok, tok->next);
      }
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
      if (val) {
        skip_const_expr(&tok, tok->next);
        val = 1;
      } else {
        long rhs = logand(&tok, tok->next);
        val = val || rhs;
      }
    } else {
      *rest = tok;
      return val;
    }
  }
}

/* 条件演算子 `?:` (C11 6.10.1 が #if 定数式で要求)。最も低い優先順位。
 * ゼロ除算等を避けるため C と同様に選択側のみ評価する。 */
static long conditional(token_t **rest, token_t *tok) {
  if_expr_step_or_die();
  long cond = logor(&tok, tok);
  if (tok->kind != TK_QUESTION) {
    *rest = tok;
    return cond;
  }
  if_expr_step_or_die();
  if (cond) {
    long then_val = const_expr(&tok, tok->next);
    if (tok->kind != TK_COLON) {
      pp_error(DIAG_ERR_PREPROCESS_RPAREN_REQUIRED, NULL);
    }
    skip_const_expr(&tok, tok->next);
    *rest = tok;
    return then_val;
  }
  skip_const_expr(&tok, tok->next);
  if (tok->kind != TK_COLON) {
    pp_error(DIAG_ERR_PREPROCESS_RPAREN_REQUIRED, NULL);
  }
  long else_val = conditional(&tok, tok->next);
  *rest = tok;
  return else_val;
}

static long const_expr(token_t **rest, token_t *tok) {
  return conditional(rest, tok);
}

static void skip_const_expr(token_t **rest, token_t *tok) {
  bool saved = g_if_expr_eval;
  g_if_expr_eval = false;
  (void)const_expr(rest, tok);
  g_if_expr_eval = saved;
}

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
  copy_source_location(&res->pp.base, macro_tok);
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
      copy_source_location(merged, cur);
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

/* #line 引数列 (指令行の残りトークン) をマクロ展開する (#if 定数式と同様)。 */
static token_t *pp_expand_directive_line(token_t *args) {
  token_t head;
  head.next = NULL;
  token_t *cur = &head;
  token_t *tok = args;
  while (tok->kind != TK_EOF && !tok->at_bol) {
    cur->next = copy_token(tok);
    cur->next->at_bol = false;
    cur = cur->next;
    tok = tok->next;
  }
  cur->next = tk_allocator_calloc(1, sizeof(token_t));
  cur->next->kind = TK_EOF;
  int saved_depth = include_depth;
  include_depth++;
  token_t *expanded = preprocess_ctx(g_preprocess_tk_ctx, head.next);
  include_depth = saved_depth;
  return expanded;
}

/* マクロ展開済み #line 引数を解釈する。行番号が無効なら診断で中断。 */
static bool pp_parse_line_directive_args(token_t *tok, long long *out_line, char **out_file) {
  if (!(tok && tok->kind == TK_NUM && tk_as_num(tok)->num_kind == TK_NUM_KIND_INT)) {
    return false;
  }
  long long new_line = tk_as_num_int(tok)->val;
  if (new_line <= 0 || new_line > INT_MAX) {
    pp_error(DIAG_ERR_PREPROCESS_LINE_NUMBER_INVALID, NULL);
  }
  *out_line = new_line;
  tok = tok->next;
  *out_file = NULL;
  if (tok && tok->kind != TK_EOF && !tok->at_bol) {
    if (tok->kind != TK_STRING) {
      return false;
    }
    token_string_t *st = as_string(tok);
    validate_line_filename_or_die(st->str, st->len);
    *out_file = my_strndup(st->str, st->len);
  }
  return true;
}

// #line N ["filename"]: 以降のトークンの line_no / file_name_id を書き換える。
static token_t *handle_line(token_t *tok) {
  token_t *args = tok->next;
  token_t *expanded = pp_expand_directive_line(args);
  long long new_line;
  char *new_file = NULL;
  if (!pp_parse_line_directive_args(expanded, &new_line, &new_file)) {
    return skip_to_next_line(args);
  }
  tok = skip_to_next_line(args);
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

static void warn_unsupported_gnu_extension_token(const token_t *tok) {
  if (!tok || tok->kind != TK_IDENT) return;
  const token_ident_t *id = (const token_ident_t *)tok;
  diag_warn_tokf(DIAG_WARN_PARSER_UNSUPPORTED_GNU_EXTENSION, tok,
                 "%s: %.*s",
                 diag_warn_message_for(DIAG_WARN_PARSER_UNSUPPORTED_GNU_EXTENSION),
                 id->len, id->str);
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
  } else if (ident_is(tok, "push_macro") || ident_is(tok, "pop_macro")) {
    warn_unsupported_gnu_extension_token(tok);
  }
  return skip_to_next_line(tok);
}

/* オブジェクト形式マクロ本体を展開して返す (本体コピー + ## paste + hideset 付与 +
 * 呼び出し位置への line/file 再配置 + 先頭 at_bol/has_space)。本体が空なら NULL。
 * batch (preprocess_ctx) と streaming (pps_step) の双方から呼び、展開を完全一致させる。 */
static token_t *pp_expand_objlike(macro_t *m, token_t *macro_tok, char *name) {
  token_t *body_copy = copy_token_list(m->body);
  body_copy = paste_tokens(body_copy);
  hideset_t *hs = new_hideset(name);
  for (token_t *t = body_copy; t; t = t->next) {
    count_macro_expansion_or_die();
    as_pp(t)->hideset = hideset_union(as_pp(t)->hideset, hs);
    copy_source_location(t, macro_tok);
  }
  if (body_copy) {
    body_copy->at_bol = macro_tok->at_bol;
    body_copy->has_space = macro_tok->has_space;
  }
  return body_copy;
}

/* func-like マクロの実引数を集める。tok = '(' の次のトークン。args[] (calloc、呼び出し側で
 * free) を返し、*out_rparen に ')' トークンを設定。引数個数を検査する。 */
static token_t **pp_collect_args(macro_t *m, token_t *tok, token_t **out_rparen) {
  token_t **args = calloc(m->num_params > 0 ? (size_t)m->num_params : 1, sizeof(token_t *));
  int arg_cnt = 0;
  int parsed_args = 0;
  bool has_empty_arg = false;
  int num_named = m->is_variadic ? m->num_params - 1 : m->num_params;
  if (!(tok->kind == TK_RPAREN)) {
    while (tok->kind != TK_EOF) {
      token_t head_arg; head_arg.next = NULL;
      token_t *cur_arg = &head_arg;
      int nest = 0;
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
      if (collecting_va) break;
      if (tok->kind == TK_COMMA) tok = tok->next;
      else break;
    }
  }
  if (tok->kind != TK_RPAREN) {
    pp_error(DIAG_ERR_PREPROCESS_FUNC_MACRO_ARG_NOT_CLOSED, NULL);
  }
  (void)has_empty_arg;  // C99 6.10.3p4: 空引数は合法 (placemarker)
  if (m->is_variadic) {
    if (parsed_args < num_named) pp_error(DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT, NULL);
  } else {
    if (parsed_args != m->num_params) pp_error(DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT, NULL);
  }
  *out_rparen = tok;
  return args;
}

static bool pp_arg_is_empty(token_t *arg) {
  return !arg;
}

static int pp_param_idx_for_ident(macro_t *m, token_t *ident_tok) {
  if (ident_tok->kind != TK_IDENT) return -1;
  token_ident_t *pid = as_ident(ident_tok);
  for (int i = 0; i < m->num_params; i++) {
    if (strlen(m->params[i]) == (size_t)pid->len && !strncmp(m->params[i], pid->str, pid->len)) {
      return i;
    }
  }
  return -1;
}

/* func-like マクロ本体に実引数を代入し、## paste・# stringize・hideset 付与・行再配置を行って
 * 展開結果を返す (NULL 終端、空なら NULL)。name は new_hideset 用 (free しない)。 */
static token_t *pp_expand_funclike(macro_t *m, token_t *macro_tok, token_t **args, char *name) {
  /* ## の位置検査 (本体側)。 */
  token_t *prev_body = NULL;
  for (token_t *bt = m->body; bt; bt = bt->next) {
    if (bt->kind != TK_HASHHASH) { prev_body = bt; continue; }
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
      for (int i = 0; i < m->num_params; i++) {
        token_ident_t *pid = as_ident(t->next);
        if (strlen(m->params[i]) == (size_t)pid->len && !strncmp(m->params[i], pid->str, pid->len)) {
          p_idx = i; break;
        }
      }
      if (p_idx != -1) {
        cur_body->next = stringify_tokens(args[p_idx], macro_tok);
        cur_body = cur_body->next;
        t = t->next;  // skip IDENT
        continue;
      }
    }
    if (t->kind == TK_IDENT) {
      int p_idx = -1;
      for (int i = 0; i < m->num_params; i++) {
        token_ident_t *pid = as_ident(t);
        if (strlen(m->params[i]) == (size_t)pid->len && !strncmp(m->params[i], pid->str, pid->len)) {
          p_idx = i; break;
        }
      }
      if (p_idx != -1) {
        /* C11 6.10.3.1: # / ## に隣接しない実引数は代入前に完全展開する。 */
        token_t *pv = NULL;
        for (token_t *b = m->body; b && b != t; b = b->next) pv = b;
        bool paste_operand = (t->next && t->next->kind == TK_HASHHASH) ||
                             (pv && pv->kind == TK_HASHHASH);
        /* C11 6.10.3.2p3: placemarker (空引数) と ## の組は paste せず placemarker を削除。 */
        if (paste_operand && pp_arg_is_empty(args[p_idx])) {
          if (t->next && t->next->kind == TK_HASHHASH) {
            t = t->next;  /* 空 LHS: パラメータと ## を落とす */
            continue;
          }
          if (pv && pv->kind == TK_HASHHASH) {
            continue;  /* 空 RHS: パラメータ名だけ落とす (## は LHS 側で除去済み) */
          }
        }
        if (paste_operand && t->next && t->next->kind == TK_HASHHASH) {
          int rhs_idx = pp_param_idx_for_ident(m, t->next->next);
          if (rhs_idx >= 0 && pp_arg_is_empty(args[rhs_idx])) {
            token_t *sub = args[p_idx];
            for (token_t *a = sub; a && a->kind != TK_EOF; a = a->next) {
              cur_body->next = copy_token(a);
              cur_body = cur_body->next;
            }
            t = t->next->next;  /* ## と空 RHS パラメータ名をスキップ */
            continue;
          }
        }
        token_t *sub = paste_operand ? args[p_idx] : pp_expand_arg(args[p_idx]);
        for (token_t *a = sub; a && a->kind != TK_EOF; a = a->next) {
          cur_body->next = copy_token(a);
          cur_body = cur_body->next;
        }
        continue;
      }
    }
    if (t->kind == TK_HASHHASH && t->next && t->next->kind == TK_IDENT) {
      int rhs_idx = pp_param_idx_for_ident(m, t->next);
      bool rhs_is_va_args = rhs_idx >= 0 && m->is_variadic &&
                            strcmp(m->params[rhs_idx], "__VA_ARGS__") == 0;
      if (rhs_is_va_args && cur_body != &body_head && cur_body->kind == TK_COMMA) {
        if (pp_arg_is_empty(args[rhs_idx])) {
          token_t *prev = &body_head;
          while (prev->next && prev->next != cur_body) prev = prev->next;
          prev->next = NULL;
          cur_body = prev;
          t = t->next;
        }
        continue;
      }
      if (rhs_idx >= 0 && pp_arg_is_empty(args[rhs_idx])) {
        t = t->next;
        continue;
      }
    }
    cur_body->next = copy_token(t);
    cur_body = cur_body->next;
  }
  cur_body->next = NULL;

  token_t *body_copy = paste_tokens(body_head.next);
  hideset_t *hs = new_hideset(name);
  for (token_t *t = body_copy; t; t = t->next) {
    count_macro_expansion_or_die();
    as_pp(t)->hideset = hideset_union(as_pp(t)->hideset, hs);
    copy_source_location(t, macro_tok);
  }
  if (body_copy) {
    body_copy->at_bol = macro_tok->at_bol;
    body_copy->has_space = macro_tok->has_space;
  }
  return body_copy;
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

      /* #include はストリーム経路 (pps_handle_include) で処理する。batch preprocess_ctx は
       * マクロ引数展開・#if 式評価のサブ展開でのみ使われ、それらのトークン列に #include 指令は
       * 現れないため、ここで #include を扱う必要はない。 */
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
             token_t *rparen = NULL;
             token_t **args = pp_collect_args(m, tok->next->next, &rparen);
             tok = rparen->next;  // skip ')'
             token_t *body_copy = pp_expand_funclike(m, macro_tok, args, name);
             free(args);
             if (body_copy) {
               token_t *tail = body_copy;
               while (tail->next) tail = tail->next;
               tail->next = tok;
               tok = body_copy;   // rescan
             }
             free(name);
             continue;
           }
        } else {
           token_t *body_copy = pp_expand_objlike(m, tok, name);
           if (body_copy) {
              token_t *tail = body_copy;
              while (tail->next) tail = tail->next;
              tail->next = tok->next;
              tok = body_copy;   // rescan (展開結果をメインループで再走査)
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

    /* 通常のコード行のトークンはコピーせず「再利用」して出力へ繋ぐ。copy_token は
     * str/hideset 含む shallow clone なのでパーサから見て等価で、入力 raw トークンは
     * この後参照されない (前方一回走査) ため所有権の重複もない。マクロ非展開部を二重に
     * 確保しないぶんトークンのピークメモリが減る (展開なしコードでは約半分)。
     * tok->next は次の emit (または末尾の EOF) で上書き設定されるので保存しておく。 */
    token_t *nx = tok->next;
    cur->next = tok;
    cur = tok;
    tok = nx;
  }

  cur->next = tok; // TK_EOF を繋ぐ
  g_preprocess_tk_ctx = prev_tk_ctx;
  return head.next;
}

/* ============================================================================
 * トークンストリーム経路: `#` 指令の無いファイル専用の遅延プリプロセス生成器。
 * 指令・ユーザマクロ・include・条件・#line が存在しない入力では preprocess_ctx は
 * 「通過 / __LINE__ / __FILE__ / object-like predefined マクロ展開」だけを行うので、
 * その部分集合を 1 トークンずつ pull で再現する。出力はバッチ版とバイト一致する。
 * パーサがカーソルを前進させるたび (tk_set_cursor_hook) に必要分だけ materialize し、
 * 通り過ぎた recyclable チャンクを解放する。
 * ========================================================================== */
#define PP_STREAM_LOOKAHEAD 256

/* #include ストリーム化 (Stage 5): 被 include を別ソースとして開いた遅延字句に切り替え、
 * フレームをスタックに積む。被 include の lexer が TK_EOF を返したら pop して親 lexer へ戻る
 * (被 include の EOF は出力に出さない)。各フレームは pop 時に復元する親の状態を保持する。 */
typedef struct pp_include_frame pp_include_frame_t;
struct pp_include_frame {
  pp_include_frame_t *parent;       // 外側方向 (NULL = 最外ファイルの 1 つ内側)
  tk_token_stream_t  *parent_lex;   // pop 時に s->lex を戻す親 lexer
  char               *buf;          // 被 include のソースバッファ (pop で free)
  char               *path_owned;   // normalize 済みパス (pop_include() の後に free)
  token_t            *saved_pb_head; // 親の pushback 列 (指令行の次行頭トークン等)。pop で復元
  const char *saved_input;          // 親 ctx->user_input
  const char *saved_filename;       // 親 ctx->current_filename
  int         saved_line_delta;     // 親の #line 状態 (Stage 4)
  int         saved_file_override_set;
  uint16_t    saved_file_override;
};

struct pp_stream {
  tk_token_stream_t *lex;
  token_t *out_head;   // 解放されていない先頭 (デバッグ用)
  token_t *out_tail;   // 末尾 (ここに append)
  token_t *cursor;     // パーサの現在トークン
  token_t *refill_at;  // カーソルがここへ来たら次の補充 (LOOKAHEAD 先のトークン)
  token_t *pb_head;    // pushback: pull 済みだが未消費の raw トークン列 (マクロ rescan /
                       // skip_cond_incl が止まった指令の差し戻し)。pps_pull_raw が先に返す。
  token_t *reclaim_hold; // 非 NULL の間チャンク解放を保留。out-of-order 区間をカーソルが
                         // ここ (区間末尾の out_tail) まで通過したら解放を再開する。
  int ooo_active;        // 入れ子展開 (pushback 非空時の更なる pushback) で out 鎖が
                         // 確保順≠消費順になった区間を処理中。drain まで reclaim_hold を更新。
  int eof_done;
  /* #line 状態 (Stage 4)。バッチ handle_line は「指令以降の全 raw トークンの line_no に
   * offset を加算 / file_name_id を上書き」する。ストリームでは後続を一括変更できないので、
   * 遅延デルタとして保持し、lex 由来 (物理) トークンを pull した時点で 1 回だけ適用する。
   * file_override は #line にファイル指定がある度に更新され、以降 sticky (指定無し #line は
   * 既存のファイル名を保つ = バッチと同じ)。id 0 も有効なので別フラグで有無を持つ。 */
  int line_delta;          // 物理 line_no に加算するデルタ (既定 0)
  int file_override_set;   // file_override が有効か
  uint16_t file_override;  // 有効時、lex 由来トークンの file_name_id をこれで上書き
  pp_include_frame_t *frames;  // 被 include フレームスタック (NULL = 最外ファイル)
};

static token_t *pps_pull_raw(pp_stream_t *s);
static void pps_pushback_one(pp_stream_t *s, token_t *t);
static void pps_on_advance(token_t *cursor);
static void pps_ensure_lookahead(void);

typedef void (*pps_cursor_hook_t)(token_t *);

static pp_stream_t *g_active_pps = NULL;

static pps_cursor_hook_t pps_suspend_cursor_hook(void) {
  pps_cursor_hook_t saved_hook = tk_get_cursor_hook();
  tk_set_cursor_hook(NULL);
  return saved_hook;
}

static void pps_restore_cursor_hook(pps_cursor_hook_t hook) {
  tk_set_cursor_hook(hook);
}

static void pps_install_tokenizer_hooks(void) {
  tk_set_cursor_hook(pps_on_advance);
  tk_set_ensure_lookahead_hook(pps_ensure_lookahead);
}

static void pps_clear_tokenizer_hooks(void) {
  tk_set_cursor_hook(NULL);
  tk_set_ensure_lookahead_hook(NULL);
}

static void pps_activate(pp_stream_t *s) {
  g_active_pps = s;
  pps_install_tokenizer_hooks();
}

static void pps_deactivate(pp_stream_t *s) {
  pps_clear_tokenizer_hooks();
  if (!s || g_active_pps == s) g_active_pps = NULL;
}

static void pps_update_stream_pin(pp_stream_t *s) {
  if (!s) {
    tk_allocator_recyc_stream_unpin();
    return;
  }
  if (s->pb_head) {
    tk_allocator_recyc_stream_pin(s->pb_head);
  } else if (s->out_head) {
    tk_allocator_recyc_stream_pin(s->out_head);
  } else {
    tk_allocator_recyc_stream_unpin();
  }
}

static void pps_append(pp_stream_t *s, token_t *t) {
  t->next = NULL;
  if (s->out_tail) s->out_tail->next = t;
  else s->out_head = t;
  s->out_tail = t;
}

/* 被 include フレームを 1 つ pop する。pps_pull_raw が被 include の TK_EOF を見たときに呼ぶ。
 * tk_stream_delete → end_tokenize_session は tk_set_current_token(非NULL) でカーソルフックを
 * 発火し s->cursor を破壊し得る (refill 経由の pop ではフック=pps_on_advance)。よって delete の
 * 間だけフックを退避する。dispatch 経由の pop ではフックは既に NULL なので、現在値を保存して
 * 復元する (無条件で pps_on_advance に戻すと dispatch 中にフックを誤って再有効化してしまう)。 */
static void pps_pop_frame(pp_stream_t *s) {
  pp_include_frame_t *f = s->frames;
  pps_cursor_hook_t saved_hook = pps_suspend_cursor_hook();
  tk_stream_delete(s->lex);          // EOF に達した被 include の lexer
  pps_restore_cursor_hook(saved_hook);
  tk_set_filename_ctx(g_preprocess_tk_ctx, f->saved_filename);
  tk_set_user_input_ctx(g_preprocess_tk_ctx, f->saved_input);
  s->pb_head           = f->saved_pb_head;  // 親の pushback 列 (被 include 後に続く)
  pps_update_stream_pin(s);
  s->line_delta        = f->saved_line_delta;
  s->file_override_set = f->saved_file_override_set;
  s->file_override     = f->saved_file_override;
  s->lex    = f->parent_lex;
  s->frames = f->parent;
  pop_include();
  free(f->path_owned);  // pop_include() の後 (include_stack が path を参照するため)
  /* f->buf は解放しない: 被 include のトークン (識別子・文字列リテラル) の str は buf 内を
   * 指しており (replace_trigraphs はトリグラフ無しなら入力をそのまま返す)、パーサが typedef 名
   * やタグ名などをポインタで永続保持するため、コンパイル終了まで生存させる必要がある。
   * バッチ include_and_splice も buf を解放せずプロセス終了まで保持する。 */
  free(f);
}

/* 次の raw トークンを 1 つ取り出す。pushback があればそれを先に返し、無ければ遅延字句から。
 * 被 include の lexer が返す TK_EOF は **そのまま返す**。フレームの pop は論理ステップの境界
 * (pps_step) で行う。ここで pop すると、指令行 materialize の先読みが被 include の EOF を踏んだ
 * 際に指令処理前にフレームが pop され、親トークンを巻き込んでしまう (self-include 無限ループの
 * 原因だった)。lex 由来 (物理) トークンには #line のデルタ/ファイル上書きをここで 1 回だけ適用
 * する (pushback 由来 = マクロ rescan 本体や合成 EOF・差し戻し済みトークンには適用しない。
 * 展開本体は呼び出し位置の line/file を pp_expand_* が既にコピー済み、差し戻し済みトークンは
 * pull 時に適用済み)。バッチが「raw を先に書き換えてから preprocess」するのと等価になる。 */
static token_t *pps_pull_raw(pp_stream_t *s) {
  if (s->pb_head) {  // pop で復元した親の pushback もここで拾う (lexer より優先)
    token_t *t = s->pb_head;
    s->pb_head = t->next;
    pps_update_stream_pin(s);
    t->next = NULL;
    return t;
  }
  token_t *t = tk_stream_next(s->lex);
  if (t && t->kind != TK_EOF) {  // バッチ handle_line も EOF は変更しない
    if (s->line_delta) t->line_no = (int)((long long)t->line_no + s->line_delta);
    if (s->file_override_set) t->file_name_id = s->file_override;
  }
  return t;
}

static int pp_body_is_single_call_replacement(token_t *body) {
  if (!body || body->kind != TK_IDENT || !body->next || body->next->kind != TK_LPAREN) return 0;
  int nest = 0;
  for (token_t *t = body->next; t; t = t->next) {
    if (t->kind == TK_LPAREN) nest++;
    else if (t->kind == TK_RPAREN) {
      nest--;
      if (nest < 0) return 0;
      if (nest == 0) return t->next == NULL;
    }
  }
  return 0;
}

/* `MACRO(args)(more)` 形: 置換列末尾の `)` の直後の `(more)` だけを繋ぎ、
 * その閉じ `)` までを preprocess_ctx で縮約してから pushback する。 */
static token_t *pp_stream_splice_paren_suffix_and_rescan(pp_stream_t *s, token_t *body) {
  if (!body) return NULL;
  token_t *copy = copy_token_list(body);
  token_t *tail = copy;
  while (tail->next) tail = tail->next;
  if (tail->kind != TK_RPAREN || !pp_body_is_single_call_replacement(copy)) return copy;

  token_t *next_raw = pps_pull_raw(s);
  if (!next_raw || next_raw->kind != TK_LPAREN) {
    if (next_raw) pps_pushback_one(s, next_raw);
    return copy;
  }

  token_t *suffix = copy_token(next_raw);
  token_t *suffix_tail = suffix;
  int nest = 1;
  while (nest > 0) {
    token_t *t = pps_pull_raw(s);
    if (!t) break;
    suffix_tail->next = copy_token(t);
    suffix_tail = suffix_tail->next;
    if (t->kind == TK_LPAREN) nest++;
    if (t->kind == TK_RPAREN) nest--;
  }
  tail->next = suffix;

  token_t *eof = tk_allocator_calloc(1, sizeof(token_t));
  eof->kind = TK_EOF;
  suffix_tail->next = eof;

  int saved_depth = include_depth;
  include_depth++;
  token_t *expanded = preprocess_ctx(g_preprocess_tk_ctx, copy);
  include_depth = saved_depth;
  if (expanded) {
    token_t *prev = NULL;
    for (token_t *t = expanded; t; prev = t, t = t->next) {
      if (t->kind == TK_EOF) {
        if (prev) prev->next = NULL;
        else expanded = NULL;
        break;
      }
    }
  }
  return expanded;
}

/* トークン 1 つを pushback の先頭へ差し戻す。 */
static void pps_pushback_one(pp_stream_t *s, token_t *t) {
  t->next = s->pb_head;
  s->pb_head = t;
  pps_update_stream_pin(s);
}

/* NULL 終端リストを pushback の先頭へ差し戻す (順序は保つ)。 */
static void pps_pushback_list(pp_stream_t *s, token_t *head) {
  if (!head) return;
  token_t *tail = head;
  while (tail->next) tail = tail->next;
  tail->next = s->pb_head;
  s->pb_head = head;
  pps_update_stream_pin(s);
}

/* 指令行/マクロ引数バッファの終端用に合成 EOF (at_bol) を作る。recyclable 側に確保。 */
static token_t *pps_make_eof(token_t *ref) {
  token_pp_t *t = tk_allocator_calloc(1, sizeof(token_pp_t));
  t->base.kind = TK_EOF;
  t->base.at_bol = true;
  if (ref) copy_source_location(&t->base, ref);
  return (token_t *)t;
}

/* first を起点に 1 論理行 (次の at_bol / EOF の手前まで) を materialize し、末尾に合成 EOF を
 * 付けて返す。次行先頭 (または EOF) トークンは pushback に戻す。バッチ指令ハンドラは
 * `while (kind!=TK_EOF && !at_bol)` で走査するので、この合成 EOF 終端で停止できる。 */
static token_t *pps_materialize_line(pp_stream_t *s, token_t *first) {
  token_t *cur = first;
  while (cur->next) cur = cur->next;   // first が既に鎖を持つ場合 (# -> name) に対応
  /* 行末検出のため次行の先頭トークンを 1 つ先読みする。その先読み先が `#if 0` 偽分岐の
   * 先頭でトークナイズ不能文字 (` @ $) だと、スキップ開始前にここで tokenize されて E2028 に
   * なる。先読み区間はトークナイズ不能文字を許容 (TK_UNKNOWN 化) し、偽分岐なら後段の skip が
   * 捨て、active なら pps_step が出力時に E2028 を出す。 */
  tk_set_tolerate_untokenizable(true);
  for (;;) {
    token_t *nx = pps_pull_raw(s);
    if (!nx) break;                    // 入力末尾
    if (nx->kind == TK_EOF || nx->at_bol) { pps_pushback_one(s, nx); break; }
    cur->next = nx; cur = nx;
  }
  tk_set_tolerate_untokenizable(false);
  cur->next = pps_make_eof(first);
  return first;
}

/* '(' (lparen、pull 済み) から対応する ')' まで balanced に materialize する。'(' の次の
 * トークン (引数列) を返す。末尾は ')' (+ 合成 EOF)。pp_collect_args に渡す。引数は行を
 * またげる (nest==0 の ')' まで pull、at_bol は無視)。 */
static token_t *pps_materialize_balanced(pp_stream_t *s, token_t *lparen) {
  token_t head; head.next = NULL; token_t *cur = &head;
  int nest = 1;
  for (;;) {
    token_t *t = pps_pull_raw(s);
    if (!t) break;
    if (t->kind == TK_EOF) { cur->next = t; cur = t; break; }  // 未閉じ: collect_args がエラー
    if (t->kind == TK_LPAREN) nest++;
    if (t->kind == TK_RPAREN) nest--;
    cur->next = t; cur = t;
    if (t->kind == TK_RPAREN && nest == 0) break;
  }
  cur->next = pps_make_eof(lparen);
  return head.next;
}

/* 偽 #if 分岐の読み飛ばし (pull-and-discard)。BOL の #if 入れ子を数え、対応する
 * #else/#elif/#endif でその指令行を materialize して pushback し、次の pps_step に再 dispatch
 * させる。読み飛ばしたトークンは出力に載らないので window 解放で回収される。 */
static void pps_skip_cond_incl_impl(pp_stream_t *s) {
  int nest = 0;
  for (;;) {
    token_t *tok = pps_pull_raw(s);
    if (!tok) { s->eof_done = 1; return; }
    if (tok->kind == TK_EOF) { pps_pushback_one(s, tok); return; }
    if (tok->at_bol && tok->kind == TK_HASH) {
      token_t *name = pps_pull_raw(s);
      bool stop = false;
      if (is_dir(name, "if") || is_dir(name, "ifdef") || is_dir(name, "ifndef")) nest++;
      else if (is_dir(name, "endif")) { if (nest == 0) stop = true; else nest--; }
      else if (is_dir(name, "else") || is_dir(name, "elif")) { if (nest == 0) stop = true; }
      if (stop) {
        /* 一致指令の # と名前を raw のまま戻す (行の materialize は次の pps_step に任せる。
         * ここで materialize すると合成 EOF が pushback に紛れて stream を誤終端する)。 */
        pps_pushback_one(s, name);
        pps_pushback_one(s, tok);
        return;
      }
      /* 一致しない指令: その行の残りを捨てる (次の BOL まで)。 */
      if (name && name->kind != TK_EOF && !name->at_bol) {
        for (;;) {
          token_t *t = pps_pull_raw(s);
          if (!t) { s->eof_done = 1; return; }
          if (t->kind == TK_EOF || t->at_bol) { pps_pushback_one(s, t); break; }
        }
      } else if (name) {
        pps_pushback_one(s, name);
      }
    }
    /* それ以外のトークンは捨てる (偽分岐内)。 */
  }
}

/* 偽分岐の読み飛ばし中はトークナイズ不能文字 (` @ $ 等) を許容する (C 翻訳フェーズ 3: 偽分岐の
 * 中身は単一文字 pp-token に分解されるだけでよい)。読み飛ばしは生トークンを pull して捨てる
 * ので、ここで tk_set_tolerate_untokenizable を立てておけば偽分岐内の非C 文字でエラーにならない。 */
static void pps_skip_cond_incl(pp_stream_t *s) {
  tk_set_tolerate_untokenizable(true);
  pps_skip_cond_incl_impl(s);
  tk_set_tolerate_untokenizable(false);
}

/* ストリーミング版の条件指令ハンドラ。式評価/スタック操作はバッチの補助関数を再利用し、
 * 偽分岐の読み飛ばしだけ pps_skip_cond_incl に差し替える。after_hash は指令名トークン。 */
static void pps_handle_if(pp_stream_t *s, token_t *after_hash) {
  token_t *tok = after_hash->next;
  bool is_true = evaluate_constexpr(&tok, tok);
  push_cond_incl(is_true);
  if (!is_true) pps_skip_cond_incl(s);
}
static void pps_handle_ifdef(pp_stream_t *s, token_t *after_hash, bool negated) {
  token_t *tok = after_hash->next;
  char *name = consume_required_macro_name(&tok);
  bool defined = find_macro(name) != NULL;
  free(name);
  bool is_true = negated ? !defined : defined;
  push_cond_incl(is_true);
  if (!is_true) pps_skip_cond_incl(s);
}
static void pps_handle_elif(pp_stream_t *s, token_t *after_hash) {
  if (!cond_incl) pp_error(DIAG_ERR_PREPROCESS_ELIF_WITHOUT_IF, NULL);
  if (cond_incl->ctx == IN_ELSE) pp_error(DIAG_ERR_PREPROCESS_ELIF_AFTER_ELSE, NULL);
  cond_incl->ctx = IN_ELIF;
  token_t *tok = after_hash->next;
  if (cond_incl->included) { pps_skip_cond_incl(s); return; }
  bool is_true = evaluate_constexpr(&tok, tok);
  if (is_true) cond_incl->included = true;
  else pps_skip_cond_incl(s);
}
static void pps_handle_else(pp_stream_t *s, token_t *after_hash) {
  (void)after_hash;
  if (!cond_incl) pp_error(DIAG_ERR_PREPROCESS_ELSE_WITHOUT_IF, NULL);
  if (cond_incl->ctx == IN_ELSE) pp_error(DIAG_ERR_PREPROCESS_DUPLICATE_ELSE, NULL);
  cond_incl->ctx = IN_ELSE;
  if (cond_incl->included) pps_skip_cond_incl(s);
  else cond_incl->included = true;
}

/* ストリーミング版 #line。バッチ handle_line は「次の物理行以降の全トークン」を書き換えるが、
 * ストリームでは遅延デルタ (line_delta / file_override) に変換し pps_pull_raw で適用する。
 * デルタは「次の物理トークンの素 (デルタ適用前) の line_no」基準で絶対計算する:
 *   line_delta = N - raw_next_line。バッチは既調整 line に offset を足すが、累積分は相殺され
 *   結果は常に N - raw_next_line に一致する (HANDOFF Stage 4 参照)。
 * after_hash は指令名 "line" トークン。バッチ同様、N が無効なら何もしない (デルタ不変)。 */
static void pps_handle_line(pp_stream_t *s, token_t *after_hash) {
  token_t *expanded = pp_expand_directive_line(after_hash->next);
  long long new_line;
  char *new_file = NULL;
  if (!pp_parse_line_directive_args(expanded, &new_line, &new_file)) {
    return;  // バッチ: skip_to_next_line で無視 (デルタ変更なし)
  }
  /* 次の物理トークンを 1 つ覗いて素の line_no を得る (pull で旧デルタ適用済みなので差し引く)。
   * バッチは次トークンが EOF なら何もしない (offset 計算も適用も行わない) ので同じく分岐する。 */
  token_t *nx = pps_pull_raw(s);
  if (nx && nx->kind != TK_EOF) {
    long long raw = (long long)nx->line_no - s->line_delta;  // 旧デルタを除いた素値
    s->line_delta = (int)(new_line - raw);
    if (new_file) {
      s->file_override = tk_filename_intern(new_file);
      s->file_override_set = 1;
    }
    /* 覗いたトークンは pushback 側に戻る = 以後デルタ非適用なので、新デルタ/上書きを今ここで
     * 反映しておく (line_no は raw + 新デルタ = N)。 */
    nx->line_no = (int)((long long)raw + s->line_delta);
    if (s->file_override_set) nx->file_name_id = s->file_override;
  }
  if (nx) pps_pushback_one(s, nx);
  /* new_file は free しない: tk_filename_intern がポインタをそのまま filename_table に
   * 永続保持するため (バッチ handle_line も free せず table に所有させる)。 */
}

/* #include をストリーム経路で処理する。バッチ handle_include と同じ順で被 include を解決し、
 * splice する代わりに被 include の遅延字句フレームを push する。pps_pull_raw が被 include を
 * O(ウィンドウ) で pull し、EOF で pps_pop_frame が親へ戻す。dispatch 中 (フック NULL) に
 * 呼ばれる前提。 */
static void pps_handle_include(pp_stream_t *s, token_t *after_hash) {
  token_t *tok = after_hash->next;  // skip "include"
  char *filename = consume_include_filename(&tok);
  const char *current_file = tk_get_filename_ctx(g_preprocess_tk_ctx);
  char *loaded_path = NULL;
  char *buf = NULL;
  if (g_virtual_headers_enabled) {
    const pp_virtual_header_t *header = resolve_virtual_header(filename, current_file, &loaded_path);
    if (!header) {
      diag_emit_internalf(DIAG_ERR_PREPROCESS_INCLUDE_NOT_FOUND,
                          diag_message_for(DIAG_ERR_PREPROCESS_INCLUDE_NOT_FOUND), filename);
    }
    buf = (char *)header->source;
  } else {
    validate_include_path_or_die(filename, current_file);
    char *normalized = normalize_include_path_or_die(filename);
    free(filename);
    filename = normalized;
    buf = load_include_with_allowlist_or_die(filename, current_file, &loaded_path);
    if (!buf) {  // not found / 権限 / symlink loop: 診断して終了
      diag_error_id_t id = include_read_failure_diag_id();
      diag_emit_internalf(id, diag_message_for(id), filename);
    }
  }
  if (!loaded_path) loaded_path = my_strndup(filename, strlen(filename));
  if (pragma_once_seen(loaded_path)) {  // 既に #pragma once 済み: フレームを積まず無視 (バッチ同様)
    free(filename);
    free(loaded_path);
    return;
  }

  /* ctx 切替前に親状態を保存する。 */
  pp_include_frame_t *f = calloc(1, sizeof(*f));
  if (!f) pp_error(DIAG_ERR_INTERNAL_OOM, NULL);
  f->parent                  = s->frames;
  f->parent_lex              = s->lex;
  f->buf                     = buf;
  f->path_owned              = loaded_path;
  f->saved_input             = tk_get_user_input_ctx(g_preprocess_tk_ctx);
  f->saved_filename          = tk_get_filename_ctx(g_preprocess_tk_ctx);
  f->saved_line_delta        = s->line_delta;
  f->saved_file_override_set = s->file_override_set;
  f->saved_file_override     = s->file_override;

  /* 被 include 内の #pragma once が include_stack->path に被 include 名を記録できるよう、
   * tokenize 前に push する。深さ/循環制限もここで発火しうる (バッチ同様)。 */
  push_include_or_die(loaded_path);

  /* ctx を被 include に切替。以後 init_token_base が被 include 名で file_name_id を付与し、
   * 新しい lexer は line_no=1 起算なので line_delta=0 で正しい行番号になる。my_strndup は
   * tk_filename_intern がポインタ保持するため free しない (バッチ同様)。 */
  tk_set_filename_ctx(g_preprocess_tk_ctx, my_strndup(loaded_path, strlen(loaded_path)));
  s->line_delta        = 0;
  s->file_override_set = 0;
  s->file_override     = 0;

  /* 指令行 materialize で pushback された次行頭トークン (親) を退避し、被 include を空 pushback で
   * 読む。被 include 終端 (pop) で復元するので、出力順は「被 include → 親の続き」になる。 */
  f->saved_pb_head = s->pb_head;
  s->pb_head = NULL;
  pps_update_stream_pin(s);

  /* 被 include の遅延字句を開く。begin_tokenize_session が current_token=NULL にし、
   * tokenize_prepare_input が user_input を被 include バッファに上書きする。current_token は
   * パーサのカーソルなので保存・復元する (バッチ include_and_splice 同様)。 */
  token_t *saved_token = tk_get_current_token_ctx(g_preprocess_tk_ctx);
  s->lex    = tk_stream_new(g_preprocess_tk_ctx, buf);
  s->frames = f;
  tk_set_current_token_ctx(g_preprocess_tk_ctx, saved_token);
  free(filename);
}

/* materialize 済みの指令行を処理する。bounded 指令 (define/undef/error/pragma/endif/line) は
 * バッチハンドラ相当を行バッファ上で実行。条件指令・include は streaming 版を呼ぶ。 */
static void pps_dispatch_directive(pp_stream_t *s, token_t *line) {
  token_t *tok = line->next;  // '#' を読み飛ばす
  if (!tok || tok->kind == TK_EOF) return;  // 空指令 '#'
  if (is_dir(tok, "ifdef"))  { pps_handle_ifdef(s, tok, false); return; }
  if (is_dir(tok, "ifndef")) { pps_handle_ifdef(s, tok, true);  return; }
  if (is_dir(tok, "if"))     { pps_handle_if(s, tok); return; }
  if (is_dir(tok, "elif"))   { pps_handle_elif(s, tok); return; }
  if (is_dir(tok, "else"))   { pps_handle_else(s, tok); return; }
  if (is_dir(tok, "endif"))  { handle_endif(tok); return; }
  if (is_dir(tok, "include")) { pps_handle_include(s, tok); return; }
  if (is_dir(tok, "define")) {
    tk_allocator_set_recyclable(0);  // マクロ本体は永続アリーナへ (window 解放で消さない)
    handle_define(tok);
    tk_allocator_set_recyclable(1);
    return;
  }
  if (is_dir(tok, "undef"))  { handle_undef(tok); return; }
  if (is_dir(tok, "line"))   { pps_handle_line(s, tok); return; }
  if (is_dir(tok, "error"))  { handle_error(tok); return; }
  if (is_dir(tok, "pragma")) {
    token_t lc; lc.next = NULL; token_t *cur = &lc;
    handle_pragma(tok, &cur);                 // pragma pack マーカーを lc に append
    for (token_t *t = lc.next; t; ) { token_t *nx = t->next; pps_append(s, t); t = nx; }
    return;
  }
  /* 未知指令はバッチ同様に無視 (行は破棄)。 */
}

/* 入力 1 論理ステップを処理し、出力へ append したトークン数を返す (指令やマクロ展開の
 * pushback では 0)。EOF は append (1) して s->eof_done を立てる。 */
static int pps_step(pp_stream_t *s) {
  token_t *tok = pps_pull_raw(s);
  if (!tok) { s->eof_done = 1; return 0; }
  if (tok->kind == TK_EOF) {
    if (s->frames) { pps_pop_frame(s); return 0; }  // 被 include 終端: pop して親から続ける (出力しない)
    pps_append(s, tok); s->eof_done = 1; return 1;
  }

  /* BOL の '#': 指令行。materialize して dispatch (出力は 0 個 or pragma マーカー)。
   * dispatch 中はネスト字句/プリプロセス (evaluate_constexpr→preprocess_ctx 等) が
   * カーソルフックを発火させるので一時的に外す。 */
  if (tok->at_bol && tok->kind == TK_HASH) {
    token_t *line = pps_materialize_line(s, tok);
    pps_cursor_hook_t saved_hook = pps_suspend_cursor_hook();
    pps_dispatch_directive(s, line);
    pps_restore_cursor_hook(saved_hook);
    return 0;
  }

  if (tok->kind == TK_IDENT) {
    token_ident_t *id = as_ident(tok);
    /* __LINE__ / __FILE__ は preprocess_ctx と同じくマクロ表より先に inline 処理。 */
    if (id->len == 8 && memcmp(id->str, "__LINE__", 8) == 0) {
      pps_append(s, make_int_token(tok->line_no, tok));
      return 1;
    }
    if (id->len == 8 && memcmp(id->str, "__FILE__", 8) == 0) {
      const char *fn = tk_filename_lookup(tok->file_name_id);
      pps_append(s, make_string_token(fn ? fn : "", tok));
      return 1;
    }
    char *name = my_strndup(id->str, id->len);
    macro_t *m = find_macro(name);
    if (m && !hideset_contains(as_pp(tok)->hideset, name)) {
      count_macro_expansion_or_die();  // batch と同じ展開ステップ上限 (E1029)。無いと深い再帰展開でクラッシュ
      /* マクロ展開は batch と同じ pp_expand_objlike / pp_expand_funclike を使い、結果を
       * pushback して rescan する (= batch の splice + continue 相当)。展開中は paste_tokens
       * (tk_tokenize_ctx) / pp_expand_arg (preprocess_ctx) がネスト session でカーソルフックを
       * 発火させるので一時的に外す。 */
      if (m->is_funclike) {
        token_t *nx = pps_pull_raw(s);   // 2-token 先読み: 呼び出しの '(' か
        if (nx && nx->kind == TK_LPAREN) {
          token_t *grp = pps_materialize_balanced(s, nx);
          token_t *rparen = NULL;
          token_t **args = pp_collect_args(m, grp, &rparen);
          pps_cursor_hook_t saved_hook = pps_suspend_cursor_hook();
          token_t *body = pp_expand_funclike(m, tok, args, name);
          free(args);
          if (body) {
            /* The suffix rescan calls preprocess_ctx() on a synthetic token list.
             * Keep the outer streaming cursor hook disabled so that nested cursor
             * movement cannot refill the outer stream before the expansion result
             * is pushed back. */
            body = pp_stream_splice_paren_suffix_and_rescan(s, body);
            if (s->pb_head) s->ooo_active = 1;
            pps_pushback_list(s, body);
          }
          pps_restore_cursor_hook(saved_hook);
          free(name);
          return 0;
        }
        /* '(' が続かない: マクロ呼び出しでない。名前を通過させ nx を戻す。 */
        if (nx) pps_pushback_one(s, nx);
        pps_append(s, tok);
        free(name);
        return 1;
      } else {
        pps_cursor_hook_t saved_hook = pps_suspend_cursor_hook();
        token_t *body = pp_expand_objlike(m, tok, name);  // ## 展開で tk_tokenize_ctx 経由あり
        pps_restore_cursor_hook(saved_hook);
        if (body) {
          if (s->pb_head) s->ooo_active = 1;  // 入れ子展開 = out-of-order
          pps_pushback_list(s, body);
        }
        free(name);
        return 0;
      }
    }
    free(name);
  }
  if (tok->kind == TK_UNKNOWN) {
    /* TK_UNKNOWN は `#if 0` 偽分岐の先読み等で許容生成された「トークナイズ不能文字」。
     * ここまで来た＝active コードに現れたということなので、翻訳フェーズ 7 相当で E2028。
     * (偽分岐内のものは skip が捨てるのでここには到達しない。) */
    diag_emit_tokf(DIAG_ERR_TOKENIZER_TOKENIZE_FAILED, tok, "%s",
                   diag_message_for(DIAG_ERR_TOKENIZER_TOKENIZE_FAILED));
  }
  pps_append(s, tok);  // 通過 (raw トークンを再利用)
  return 1;
}

/* カーソルの先を補充する。常に LOOKAHEAD 以上のトークンを先に保つよう、2*LOOKAHEAD まで
 * materialize し、次回補充の起点 refill_at = カーソル+LOOKAHEAD を記録する。これにより
 * 補充は LOOKAHEAD 前進ごとに 1 回 (走査 O(LOOKAHEAD)) で済み、前進あたり償却 O(1)。 */
static void pps_refill(pp_stream_t *s) {
  if (!s->cursor) return;
  /* 既存の先読み出力数を数える。指令ステップは出力 0 なので、pps_step の戻り (append 数) を
   * 積算しないと正しく数えられない (1 ステップ=1 出力とは限らない)。 */
  int have = 0;
  for (token_t *t = s->cursor->next; t && have < 2 * PP_STREAM_LOOKAHEAD; t = t->next) have++;
  while (have < 2 * PP_STREAM_LOOKAHEAD && !s->eof_done) {
    have += pps_step(s);
    /* 入れ子展開で out 鎖が確保順≠消費順になった区間 (ooo_active) の間は、その末尾
     * (out_tail) を reclaim_hold に記録し、カーソルが通過するまで解放を止める (hook 参照)。
     * pushback が空になったら区間終了 (reclaim_hold はカーソル通過まで残す)。浅い展開
     * (SQ/ADD 等、本体に未展開マクロを含まない) は ooo_active が立たず O(ウィンドウ)。 */
    if (s->ooo_active) s->reclaim_hold = s->out_tail;
    if (!s->pb_head) s->ooo_active = 0;
  }
  /* refill_at = カーソル + LOOKAHEAD のトークン (補充の起点)。出力鎖を 1 度だけ歩く。 */
  token_t *mark = s->cursor;
  for (int i = 0; i < PP_STREAM_LOOKAHEAD && mark->next; i++) mark = mark->next;
  s->refill_at = mark;
}

/* カーソル前進フック: 必要時のみ補充 (refill_at に到達 or 先が尽きた) + 通過チャンク解放。
 * 大半の前進では補充判定が O(1) ですぐ抜ける。 */
static void pps_on_advance(token_t *cursor) {
  pp_stream_t *s = g_active_pps;
  if (!s || !cursor) return;  // ネスト字句/プリプロセスが current_token=NULL で発火する場合あり
  s->cursor = cursor;
  if (cursor == s->reclaim_hold) s->reclaim_hold = NULL;  // out-of-order 区間を通過 → 解放再開
  if (cursor == s->refill_at || !cursor->next) pps_refill(s);  // pps_refill が reclaim_hold を更新
  /* reclaim_hold が立っている間 (マクロ展開で out 鎖が確保順≠消費順になった区間をカーソルが
   * まだ通過中) は chunk 解放を止める。通常コードでは展開がすぐ終わり reclaim_hold が解け、
   * ウィンドウは O(1) のまま。病的な深い再帰展開では展開上限 (E1029) に達してエラーになる。 */
  if (!s->reclaim_hold && !s->pb_head) tk_allocator_recyc_on_cursor(cursor);
}

/* カーソルを進めずに前方を先読みするパーサ経路 (_Generic の型照合等) 用に、現在カーソルの
 * 前方 lookahead を明示的に満たす。set_curtok のジャンプが refill_at を飛び越えると補充が
 * チェーン末尾到達まで起きず、t->next の純粋な先読みが未生成境界 (NULL) を踏むため、
 * 深い型先読みの直前に呼んで窓を確保する。非ストリーム時は no-op。 */
static void pps_ensure_lookahead(void) {
  if (g_active_pps && g_active_pps->cursor) pps_refill(g_active_pps);
}

/* ストリーム生成器を開く。predefined マクロは永続側へ作り、以後の生成は recyclable 側。
 * 先頭トークン (パーサのカーソル開始位置) を返す。 */
token_t *pp_stream_open(pp_stream_t **out_s, tokenizer_context_t *tk_ctx, const char *src) {
  pp_stream_t *s = calloc(1, sizeof(pp_stream_t));
  g_preprocess_tk_ctx = tk_ctx ? tk_ctx : tk_get_default_context();
  reset_pragma_once_list();
  while (cond_incl) {
    cond_incl_t *entry = cond_incl;
    cond_incl = entry->next;
    free(entry);
  }
  while (include_stack) pop_include();
  macro_expand_steps = 0;
  if_expr_eval_steps = 0;
  include_last_errno = 0;
  /* predefined マクロは永続アリーナへ (recyclable reset で消えないように)。 */
  tk_allocator_set_recyclable(0);
  macros = NULL;
  pp_init_predefined_macros();
  /* 以後の生成は recyclable アリーナ。 */
  tk_allocator_set_recyclable(1);
  s->lex = tk_stream_new(tk_ctx, src);
  s->cursor = NULL;
  /* 先頭を 1 つ生成し、そこから lookahead 分を満たす。 */
  while (!s->out_head && !s->eof_done) pps_step(s);
  s->cursor = s->out_head;
  pps_refill(s);
  pps_activate(s);
  *out_s = s;
  return s->out_head;
}

void pp_stream_close(pp_stream_t *s) {
  pps_deactivate(s);
  tk_allocator_set_recyclable(0);
  tk_allocator_recyc_stream_unpin();
  tk_allocator_recyc_reset();
  if (s) {
    /* 早期終了 (パーサが EOF まで来なかった) で残った被 include フレームを後始末する。
     * フックは既に NULL なので tk_stream_delete はフックを発火しない。 */
    while (s->frames) {
      pp_include_frame_t *f = s->frames;
      tk_stream_delete(s->lex);
      s->lex = f->parent_lex;
      s->frames = f->parent;
      pop_include();
      free(f->path_owned);
      /* f->buf は解放しない (pps_pop_frame 参照: トークン str が buf 内を指す)。 */
      free(f);
    }
    tk_stream_delete(s->lex);
    free(s);
  }
}

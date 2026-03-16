#include "scanner.h"
#include "charclass.h"
#include "literals.h"
#include "tokenizer.h"

static inline bool tk_is_space_fast(char c) {
  // Hot path for typical ASCII whitespace.
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static char *tk_skip_ignored_fallback(char *p, bool *at_bol, bool *has_space, int *line_no) {
  // 行継続（バックスラッシュ + 改行）を除去
  if (*p == '\\' && p[1] == '\n') {
    p += 2;
    (*line_no)++;
    return p;
  }

  // 行コメント // ... \n
  if (*p == '/' && p[1] == '/') {
    *has_space = true;
    p += 2;
    while (*p && *p != '\n') p++;
    if (*p == '\n') {
      *at_bol = true;
      (*line_no)++;
      p++;
    }
    return p;
  }

  // ブロックコメント /* ... */
  if (*p == '/' && p[1] == '*') {
    *has_space = true;
    p += 2;
    bool closed = false;
    while (*p) {
      if (*p == '\n') {
        *at_bol = true;
        (*line_no)++;
      }
      if (*p == '*' && p[1] == '/') {
        p += 2;
        closed = true;
        break;
      }
      p++;
    }
    if (!closed) {
      error_at(p, "コメントが閉じられていません");
    }
    return p;
  }
  return p;
}

char *tk_skip_ignored(char *p, bool *at_bol, bool *has_space, int *line_no) {
  for (;;) {
    // Hot path: ASCII空白だけを最短で処理
    while (tk_is_space_fast(*p)) {
      *has_space = true;
      if (*p == '\n') {
        *at_bol = true;
        (*line_no)++;
      }
      p++;
    }

    char c = *p;
    if (c == '/' || c == '\\') {
      char *next = tk_skip_ignored_fallback(p, at_bol, has_space, line_no);
      if (next == p) return p;
      p = next;
      continue;
    }

    // 非ASCII空白などは低頻度フォールバックへ。
    if ((unsigned char)c >= 0x80 && tk_is_space(c)) {
      *has_space = true;
      p++;
      continue;
    }

    return p;
  }
}

bool tk_scan_ident_start(const char *p, int *adv) {
  int ucn_len = 0;
  if (tk_is_ident_start_byte(*p)) {
    *adv = 1;
    return true;
  }
  if (tk_starts_with_ucn(p, &ucn_len)) {
    *adv = ucn_len;
    return true;
  }
  return false;
}

bool tk_scan_ident_continue(const char *p, int *adv) {
  int ucn_len = 0;
  if (tk_is_ident_continue_byte(*p)) {
    *adv = 1;
    return true;
  }
  if (tk_starts_with_ucn(p, &ucn_len)) {
    *adv = ucn_len;
    return true;
  }
  return false;
}

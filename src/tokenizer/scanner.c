#include "scanner.h"
#include "charclass.h"
#include "literals.h"
#include "tokenizer.h"

char *tk_skip_ignored(char *p, bool *at_bol, bool *has_space, int *line_no) {
  for (;;) {
    // 行継続（バックスラッシュ + 改行）を除去
    if (*p == '\\' && p[1] == '\n') {
      p += 2;
      (*line_no)++;
      continue;
    }

    // 空白文字をスキップ
    if (tk_is_space(*p)) {
      *has_space = true;
      if (*p == '\n') {
        *at_bol = true;
        (*line_no)++;
      }
      p++;
      continue;
    }

    // 行コメント // ... \n
    if (*p == '/' && p[1] == '/') {
      *has_space = true;
      p += 2;
      while (*p && *p != '\n')
        p++;
      if (*p == '\n') {
        *at_bol = true;
        (*line_no)++;
        p++;
      }
      continue;
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

#include "internal/scanner.h"
#include "internal/branch_hint.h"
#include "internal/charclass.h"
#include "internal/diag_helper.h"
#include "internal/literals.h"
#include "tokenizer.h"

static inline bool tk_is_space_fast(char c) {
  // Hot path for typical ASCII whitespace.
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/** @brief 低頻度ケース（コメント/行継続/エラー）を処理する。 */
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
      TK_DIAG_AT(DIAG_ERR_TOKENIZER_UNTERMINATED_COMMENT, p);
    }
    return p;
  }
  return p;
}

/** @brief 空白・コメント・行継続をスキップして次の有効文字位置を返す。 */
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
    if (TK_UNLIKELY(c == '/' || c == '\\')) {
      char *next = tk_skip_ignored_fallback(p, at_bol, has_space, line_no);
      if (next == p) return p;
      p = next;
      continue;
    }

    // 非ASCII空白などは低頻度フォールバックへ。
    if (TK_UNLIKELY((unsigned char)c >= 0x80 && tk_is_space(c))) {
      *has_space = true;
      p++;
      continue;
    }

    return p;
  }
}

/** @brief 識別子開始判定（UCN含む）を行う。 */
bool tk_scan_ident_start(const char *p, int *adv) {
  int ucn_len = 0;
  if (tk_is_ident_start_byte(*p)) {
    *adv = 1;
    return true;
  }
  // Avoid UCN helper call unless the first byte can actually start UCN.
  if (*p == '\\' && tk_starts_with_ucn(p, &ucn_len)) {
    *adv = ucn_len;
    return true;
  }
  return false;
}

/** @brief 識別子継続判定（UCN含む）を行う。 */
bool tk_scan_ident_continue(const char *p, int *adv) {
  int ucn_len = 0;
  if (tk_is_ident_continue_byte(*p)) {
    *adv = 1;
    return true;
  }
  // Avoid UCN helper call unless the first byte can actually start UCN.
  if (*p == '\\' && tk_starts_with_ucn(p, &ucn_len)) {
    *adv = ucn_len;
    return true;
  }
  return false;
}

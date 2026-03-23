#include "diag.h"
#include "messages.h"
#include "ui_texts.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_diag_locale = "ja";

/**
 * @brief 診断メッセージのロケールを設定する。
 * @param locale ロケール名（例: "ja", "en"）。
 */
void diag_set_locale(const char *locale) {
  if (!locale || locale[0] == '\0') return;
  g_diag_locale = locale;
}

/**
 * @brief 現在の診断ロケールを取得する。
 * @return 現在有効なロケール名。
 */
const char *diag_get_locale(void) {
  return g_diag_locale;
}

/**
 * @brief エラーIDに対応するメッセージを現在ロケールに従って取得する。
 * @param id エラーID。
 * @return ローカライズ済みメッセージ。未定義時はエラーキー。
 */
const char *diag_message_for(diag_error_id_t id) {
  const char *msg = NULL;
#if defined(DIAG_LANG_ALL)
  if (strcmp(g_diag_locale, "en") == 0) {
    msg = diag_message_en(id);
    if (msg) return msg;
    msg = diag_message_ja(id);
    if (msg) return msg;
  } else {
    msg = diag_message_ja(id);
    if (msg) return msg;
    msg = diag_message_en(id);
    if (msg) return msg;
  }
#elif defined(DIAG_LANG_EN)
  (void)g_diag_locale;
  msg = diag_message_en(id);
  if (msg) return msg;
#else
  (void)g_diag_locale;
  msg = diag_message_ja(id);
  if (msg) return msg;
#endif
  return diag_error_key(id);
}

const char *diag_warn_message_for(diag_warn_id_t id) {
  const char *msg = NULL;
#if defined(DIAG_LANG_ALL)
  if (strcmp(g_diag_locale, "en") == 0) {
    msg = diag_warn_message_en(id);
    if (msg) return msg;
    msg = diag_warn_message_ja(id);
    if (msg) return msg;
  } else {
    msg = diag_warn_message_ja(id);
    if (msg) return msg;
    msg = diag_warn_message_en(id);
    if (msg) return msg;
  }
#elif defined(DIAG_LANG_EN)
  (void)g_diag_locale;
  msg = diag_warn_message_en(id);
  if (msg) return msg;
#else
  (void)g_diag_locale;
  msg = diag_warn_message_ja(id);
  if (msg) return msg;
#endif
  return diag_warn_key(id);
}

/**
 * @brief テキストIDに対応するテキストを現在ロケールに従って取得する。
 * @param id テキストID。
 * @return ローカライズ済みテキスト。未定義時は "unknown.text"。
 */
const char *diag_text_for(diag_text_id_t id) {
  const char *msg = NULL;
#if defined(DIAG_LANG_ALL)
  if (strcmp(g_diag_locale, "en") == 0) {
    msg = diag_text_en(id);
    if (msg) return msg;
    msg = diag_text_ja(id);
    if (msg) return msg;
  } else {
    msg = diag_text_ja(id);
    if (msg) return msg;
    msg = diag_text_en(id);
    if (msg) return msg;
  }
#elif defined(DIAG_LANG_EN)
  (void)g_diag_locale;
  msg = diag_text_en(id);
  if (msg) return msg;
#else
  (void)g_diag_locale;
  msg = diag_text_ja(id);
  if (msg) return msg;
#endif
  return diag_ui_text_for(DIAG_UI_TEXT_UNKNOWN_TEXT, g_diag_locale);
}

/**
 * @brief トークンの実際の値を補助表示する。
 * @param tok 表示対象トークン。
 * @return なし。
 */
static void print_token_actual(const token_t *tok) {
  if (!tok) return;
  const char *label = diag_ui_text_for(DIAG_UI_TEXT_ACTUAL_TOKEN_LABEL, g_diag_locale);
  if (tok->kind == TK_IDENT) {
    const token_ident_t *id = (const token_ident_t *)tok;
    int n = id->len < 0 ? 0 : id->len;
    fprintf(stderr, " (%s: '%.*s')", label, n, id->str);
    return;
  }
  if (tok->kind == TK_STRING) {
    const token_string_t *st = (const token_string_t *)tok;
    int n = st->len < 0 ? 0 : st->len;
    fprintf(stderr, " (%s: '%.*s')", label, n, st->str);
    return;
  }
  if (tok->kind == TK_NUM) {
    const token_num_t *num = (const token_num_t *)tok;
    int n = num->len < 0 ? 0 : num->len;
    fprintf(stderr, " (%s: '%.*s')", label, n, num->str);
    return;
  }
  const char *name = NULL;
  switch (tok->kind) {
  case TK_EOF: name = "EOF"; break;
  case TK_IF: name = "if"; break;
  case TK_ELSE: name = "else"; break;
  case TK_WHILE: name = "while"; break;
  case TK_FOR: name = "for"; break;
  case TK_RETURN: name = "return"; break;
  case TK_AUTO: name = "auto"; break;
  case TK_BREAK: name = "break"; break;
  case TK_CASE: name = "case"; break;
  case TK_CONST: name = "const"; break;
  case TK_CONTINUE: name = "continue"; break;
  case TK_DEFAULT: name = "default"; break;
  case TK_DO: name = "do"; break;
  case TK_ENUM: name = "enum"; break;
  case TK_EXTERN: name = "extern"; break;
  case TK_GOTO: name = "goto"; break;
  case TK_INLINE: name = "inline"; break;
  case TK_INT: name = "int"; break;
  case TK_REGISTER: name = "register"; break;
  case TK_RESTRICT: name = "restrict"; break;
  case TK_SIGNED: name = "signed"; break;
  case TK_SIZEOF: name = "sizeof"; break;
  case TK_STATIC: name = "static"; break;
  case TK_STRUCT: name = "struct"; break;
  case TK_SWITCH: name = "switch"; break;
  case TK_TYPEDEF: name = "typedef"; break;
  case TK_UNION: name = "union"; break;
  case TK_UNSIGNED: name = "unsigned"; break;
  case TK_VOLATILE: name = "volatile"; break;
  case TK_CHAR: name = "char"; break;
  case TK_VOID: name = "void"; break;
  case TK_SHORT: name = "short"; break;
  case TK_LONG: name = "long"; break;
  case TK_FLOAT: name = "float"; break;
  case TK_DOUBLE: name = "double"; break;
  case TK_ALIGNAS: name = "_Alignas"; break;
  case TK_ALIGNOF: name = "_Alignof"; break;
  case TK_ATOMIC: name = "_Atomic"; break;
  case TK_BOOL: name = "_Bool"; break;
  case TK_COMPLEX: name = "_Complex"; break;
  case TK_GENERIC: name = "_Generic"; break;
  case TK_IMAGINARY: name = "_Imaginary"; break;
  case TK_NORETURN: name = "_Noreturn"; break;
  case TK_STATIC_ASSERT: name = "_Static_assert"; break;
  case TK_THREAD_LOCAL: name = "_Thread_local"; break;
  case TK_LPAREN: name = "("; break;
  case TK_RPAREN: name = ")"; break;
  case TK_LBRACE: name = "{"; break;
  case TK_RBRACE: name = "}"; break;
  case TK_LBRACKET: name = "["; break;
  case TK_RBRACKET: name = "]"; break;
  case TK_COMMA: name = ","; break;
  case TK_SEMI: name = ";"; break;
  case TK_ASSIGN: name = "="; break;
  case TK_PLUS: name = "+"; break;
  case TK_MINUS: name = "-"; break;
  case TK_MUL: name = "*"; break;
  case TK_DIV: name = "/"; break;
  case TK_MOD: name = "%"; break;
  case TK_BANG: name = "!"; break;
  case TK_TILDE: name = "~"; break;
  case TK_LT: name = "<"; break;
  case TK_LE: name = "<="; break;
  case TK_GT: name = ">"; break;
  case TK_GE: name = ">="; break;
  case TK_EQEQ: name = "=="; break;
  case TK_NEQ: name = "!="; break;
  case TK_ANDAND: name = "&&"; break;
  case TK_OROR: name = "||"; break;
  case TK_AMP: name = "&"; break;
  case TK_PIPE: name = "|"; break;
  case TK_CARET: name = "^"; break;
  case TK_QUESTION: name = "?"; break;
  case TK_COLON: name = ":"; break;
  case TK_INC: name = "++"; break;
  case TK_DEC: name = "--"; break;
  case TK_SHL: name = "<<"; break;
  case TK_SHR: name = ">>"; break;
  case TK_ARROW: name = "->"; break;
  case TK_PLUSEQ: name = "+="; break;
  case TK_MINUSEQ: name = "-="; break;
  case TK_MULEQ: name = "*="; break;
  case TK_DIVEQ: name = "/="; break;
  case TK_MODEQ: name = "%="; break;
  case TK_SHLEQ: name = "<<="; break;
  case TK_SHREQ: name = ">>="; break;
  case TK_ANDEQ: name = "&="; break;
  case TK_XOREQ: name = "^="; break;
  case TK_OREQ: name = "|="; break;
  case TK_ELLIPSIS: name = "..."; break;
  case TK_HASH: name = "#"; break;
  case TK_HASHHASH: name = "##"; break;
  case TK_DOT: name = "."; break;
  default: break;
  }
  if (name)
    fprintf(stderr, " (%s: '%s')", label, name);
  else
    fprintf(stderr, diag_ui_text_for(DIAG_UI_TEXT_ACTUAL_TOKEN_KIND_FMT, g_diag_locale), (int)tok->kind);
}

/**
 * @brief 入力位置ベースの診断を出力して終了する。
 * @param id エラーID。
 * @param input 入力全体文字列。
 * @param loc エラー位置。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
void diag_emit_atf(diag_error_id_t id, const char *input, const char *loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int pos = 0;
  if (input && loc && loc >= input) pos = (int)(loc - input);
  if (input) {
    fprintf(stderr, "%s\n", input);
    fprintf(stderr, "%*s", pos, "");
  }
  fprintf(stderr, "^ %s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

/**
 * @brief トークンベースの診断を出力して終了する。
 * @param id エラーID。
 * @param tok エラー位置を示すトークン。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
void diag_emit_tokf(diag_error_id_t id, const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  { char *fn = tk_filename_lookup(tok ? tok->file_name_id : 0);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  print_token_actual(tok);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

void diag_warn_tokf(diag_warn_id_t id, const token_t *tok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  { char *fn = tk_filename_lookup(tok ? tok->file_name_id : 0);
    if (tok && fn) fprintf(stderr, "%s:%d: ", fn, tok->line_no); }
  fprintf(stderr, "%s: %s: ", diag_warn_code(id), diag_text_for(DIAG_TEXT_WARNING));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

/**
 * @brief 内部診断を出力して終了する。
 * @param id エラーID。
 * @param fmt 追加メッセージのフォーマット文字列。
 * @return 戻らない。
 */
void diag_emit_internalf(diag_error_id_t id, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "%s: ", diag_error_code(id));
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

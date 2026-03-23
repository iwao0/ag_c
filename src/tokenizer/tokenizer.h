#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "token.h"
#include <stddef.h>

typedef struct tokenizer_context_t tokenizer_context_t;

/** @brief Tokenizerの実行時設定コンテキスト。 */
struct tokenizer_context_t {
  bool strict_c11_mode;
  bool enable_trigraphs;
  bool enable_binary_literals;
  bool enable_c11_audit_extensions;
  token_t *current_token;
  const char *user_input;
  const char *current_filename;
};

/** @brief 現在トークンカーソルの参照を取得する。 */
token_t *tk_get_current_token(void);
/** @brief 明示コンテキストの現在トークンカーソルの参照を取得する。 */
token_t *tk_get_current_token_ctx(tokenizer_context_t *ctx);
/** @brief 現在トークンカーソルを更新する。 */
void tk_set_current_token(token_t *tok);
/** @brief 明示コンテキストの現在トークンカーソルを更新する。 */
void tk_set_current_token_ctx(tokenizer_context_t *ctx, token_t *tok);

/** @brief token kind を可読文字列へ変換する。 */
const char *tk_token_kind_str(token_kind_t kind, int *len);

/** @brief 次トークンが1文字記号 op なら消費して true を返す。 */
bool tk_consume(char op);
/** @brief 明示コンテキストで次トークンが1文字記号 op なら消費して true を返す。 */
bool tk_consume_ctx(tokenizer_context_t *ctx, char op);
/** @brief 次トークンが複数文字演算子 op なら消費して true を返す。 */
bool tk_consume_str(const char *op);
/** @brief 明示コンテキストで次トークンが複数文字演算子 op なら消費して true を返す。 */
bool tk_consume_str_ctx(tokenizer_context_t *ctx, const char *op);
/** @brief 次トークンが識別子なら消費して返す。 */
token_ident_t *tk_consume_ident(void);
/** @brief 明示コンテキストで次トークンが識別子なら消費して返す。 */
token_ident_t *tk_consume_ident_ctx(tokenizer_context_t *ctx);

/** @brief 次トークンが1文字記号 op であることを期待して消費する。 */
void tk_expect(char op);
/** @brief 明示コンテキストで次トークンが1文字記号 op であることを期待して消費する。 */
void tk_expect_ctx(tokenizer_context_t *ctx, char op);

/**
 * @brief 次トークンが整数リテラルであることを期待して int 値を返す。
 * @return 消費した整数値。
 * @warning 浮動小数点トークンや int 範囲外はエラー終了する。
 */
int tk_expect_number(void);
/** @brief 明示コンテキストで次トークンが整数リテラルであることを期待して int 値を返す。 */
int tk_expect_number_ctx(tokenizer_context_t *ctx);

/** @brief 現在トークンが EOF かを返す。 */
bool tk_at_eof(void);
/** @brief 明示コンテキストで現在トークンが EOF かを返す。 */
bool tk_at_eof_ctx(tokenizer_context_t *ctx);

/**
 * @brief 入力文字列をトークナイズして先頭トークンを返す。
 * @warning 不正な字句を検出した場合は診断API（`diag_emit_*`）で終了する。
 */
token_t *tk_tokenize(char *p);
/** @brief 明示コンテキストでトークナイズして先頭トークンを返す。 */
token_t *tk_tokenize_ctx(tokenizer_context_t *ctx, char *p);

/** @brief 明示コンテキストの入力文字列（エラー表示用）を取得する。 */
const char *tk_get_user_input_ctx(tokenizer_context_t *ctx);
/** @brief 明示コンテキストの入力文字列（エラー表示用）を設定する。 */
void tk_set_user_input_ctx(tokenizer_context_t *ctx, const char *p);

/** @brief 明示コンテキストのファイル名（エラー表示用）を取得する。 */
const char *tk_get_filename_ctx(tokenizer_context_t *ctx);
/** @brief 明示コンテキストのファイル名（エラー表示用）を設定する。 */
void tk_set_filename_ctx(tokenizer_context_t *ctx, const char *name);

/**
 * @brief strict C11 モードの有効/無効を取得する。
 * @note strict C11 が有効な場合、2進整数リテラル（`0b...`）は拒否される。
 */
bool tk_get_strict_c11_mode(void);
/** @brief strict C11 モードの有効/無効を設定する。 */
void tk_set_strict_c11_mode(bool strict);
/** @brief トライグラフ置換の有効/無効を取得する。 */
bool tk_get_enable_trigraphs(void);
/** @brief トライグラフ置換の有効/無効を設定する。 */
void tk_set_enable_trigraphs(bool enable);
/**
 * @brief 2進整数リテラル拡張の有効/無効を取得する。
 * @note strict C11 が有効な場合、この設定が true でも `0b...` は拒否される。
 */
bool tk_get_enable_binary_literals(void);
/** @brief 2進整数リテラル拡張の有効/無効を設定する。 */
void tk_set_enable_binary_literals(bool enable);
/** @brief C11監査ログ（拡張使用検出）の有効/無効を取得する。 */
bool tk_get_enable_c11_audit_extensions(void);
/** @brief C11監査ログ（拡張使用検出）の有効/無効を設定する。 */
void tk_set_enable_c11_audit_extensions(bool enable);

/** @brief 既定のTokenizerコンテキストを返す。 */
tokenizer_context_t *tk_get_default_context(void);
/** @brief コンテキストを既定値で初期化する。 */
void tk_context_init(tokenizer_context_t *ctx);
/** @brief コンテキストのstrict C11有効/無効を取得する。 */
bool tk_ctx_get_strict_c11_mode(const tokenizer_context_t *ctx);
/** @brief コンテキストのstrict C11有効/無効を設定する。 */
void tk_ctx_set_strict_c11_mode(tokenizer_context_t *ctx, bool strict);
/** @brief コンテキストのトライグラフ置換有効/無効を取得する。 */
bool tk_ctx_get_enable_trigraphs(const tokenizer_context_t *ctx);
/** @brief コンテキストのトライグラフ置換有効/無効を設定する。 */
void tk_ctx_set_enable_trigraphs(tokenizer_context_t *ctx, bool enable);
/** @brief コンテキストの2進整数リテラル拡張有効/無効を取得する。 */
bool tk_ctx_get_enable_binary_literals(const tokenizer_context_t *ctx);
/** @brief コンテキストの2進整数リテラル拡張有効/無効を設定する。 */
void tk_ctx_set_enable_binary_literals(tokenizer_context_t *ctx, bool enable);
/** @brief コンテキストのC11監査ログ有効/無効を取得する。 */
bool tk_ctx_get_enable_c11_audit_extensions(const tokenizer_context_t *ctx);
/** @brief コンテキストのC11監査ログ有効/無効を設定する。 */
void tk_ctx_set_enable_c11_audit_extensions(tokenizer_context_t *ctx, bool enable);

/** @brief Tokenizerメモリアロケーション統計。 */
typedef struct {
  size_t alloc_count;
  size_t alloc_bytes;
  size_t peak_alloc_bytes;
} tokenizer_stats_t;

/** @brief Tokenizer統計カウンタをリセットする。 */
void tk_reset_tokenizer_stats(void);
/** @brief 現在のTokenizer統計を取得する。 */
tokenizer_stats_t tk_get_tokenizer_stats(void);

#endif

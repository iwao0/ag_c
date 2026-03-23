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

/**
 * @brief 既定コンテキストの現在トークンカーソルを取得する。
 * @return 現在トークン。未設定時は `NULL`。
 */
token_t *tk_get_current_token(void);
/**
 * @brief 指定コンテキストの現在トークンカーソルを取得する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @return 現在トークン。未設定時は `NULL`。
 */
token_t *tk_get_current_token_ctx(tokenizer_context_t *ctx);
/**
 * @brief 既定コンテキストの現在トークンカーソルを更新する。
 * @param tok 新しい現在トークン。`NULL` 可。
 */
void tk_set_current_token(token_t *tok);
/**
 * @brief 指定コンテキストの現在トークンカーソルを更新する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @param tok 新しい現在トークン。`NULL` 可。
 */
void tk_set_current_token_ctx(tokenizer_context_t *ctx, token_t *tok);

/**
 * @brief token kind を可読文字列へ変換する。
 * @param kind 変換対象の token kind。
 * @param len 文字列長の出力先。不要なら `NULL` 可。
 * @return 種別に対応する文字列表現。
 */
const char *tk_token_kind_str(token_kind_t kind, int *len);

/**
 * @brief 次トークンが1文字記号 `op` なら消費する。
 * @param op 期待する1文字記号。
 * @return 一致して消費した場合 `true`。不一致/未設定なら `false`（非破壊）。
 */
bool tk_consume(char op);
/**
 * @brief 指定コンテキストで次トークンが1文字記号 `op` なら消費する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @param op 期待する1文字記号。
 * @return 一致して消費した場合 `true`。不一致/未設定なら `false`（非破壊）。
 */
bool tk_consume_ctx(tokenizer_context_t *ctx, char op);
/**
 * @brief 次トークンが記号文字列 `op` なら消費する。
 * @param op 期待する記号文字列（例: `"=="`, `"->"`）。
 * @return 一致して消費した場合 `true`。不一致/未設定なら `false`（非破壊）。
 */
bool tk_consume_str(const char *op);
/**
 * @brief 指定コンテキストで次トークンが記号文字列 `op` なら消費する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @param op 期待する記号文字列。
 * @return 一致して消費した場合 `true`。不一致/未設定なら `false`（非破壊）。
 */
bool tk_consume_str_ctx(tokenizer_context_t *ctx, const char *op);
/**
 * @brief 次トークンが識別子なら消費して返す。
 * @return 消費した識別子トークン。一致しない場合は `NULL`（非破壊）。
 */
token_ident_t *tk_consume_ident(void);
/**
 * @brief 指定コンテキストで次トークンが識別子なら消費して返す。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @return 消費した識別子トークン。一致しない場合は `NULL`（非破壊）。
 */
token_ident_t *tk_consume_ident_ctx(tokenizer_context_t *ctx);

/**
 * @brief 次トークンが1文字記号 `op` であることを期待して消費する。
 * @param op 期待する1文字記号。
 * @warning 不一致または現在トークン未設定時は診断終了する。
 */
void tk_expect(char op);
/**
 * @brief 指定コンテキストで次トークンが1文字記号 `op` であることを期待して消費する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @param op 期待する1文字記号。
 * @warning 不一致または現在トークン未設定時は診断終了する。
 */
void tk_expect_ctx(tokenizer_context_t *ctx, char op);

/**
 * @brief 次トークンが整数リテラルであることを期待して int 値を返す。
 * @return 消費した整数値。
 * @warning 浮動小数点トークンや int 範囲外はエラー終了する。
 */
int tk_expect_number(void);
/**
 * @brief 指定コンテキストで次トークンが整数リテラルであることを期待して `int` 値を返す。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @return 消費した整数値。
 * @warning 不一致・範囲外・現在トークン未設定時は診断終了する。
 */
int tk_expect_number_ctx(tokenizer_context_t *ctx);

/**
 * @brief 現在トークンが EOF かを返す。
 * @return 現在トークンが EOF のとき `true`。未設定を含むそれ以外は `false`。
 */
bool tk_at_eof(void);
/**
 * @brief 指定コンテキストで現在トークンが EOF かを返す。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @return 現在トークンが EOF のとき `true`。未設定を含むそれ以外は `false`。
 */
bool tk_at_eof_ctx(tokenizer_context_t *ctx);

/**
 * @brief 入力文字列をトークナイズして先頭トークンを返す。
 * @warning 不正な字句を検出した場合は診断API（`diag_emit_*`）で終了する。
 */
token_t *tk_tokenize(const char *p);
/**
 * @brief 指定コンテキストで入力文字列をトークナイズする。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @param p 入力文字列。
 * @return 先頭トークン（末尾は `TK_EOF`）。
 * @warning 不正な字句を検出した場合は診断APIで終了する。
 */
token_t *tk_tokenize_ctx(tokenizer_context_t *ctx, const char *p);

/**
 * @brief 指定コンテキストの入力文字列（診断表示用）を取得する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @return 設定済み入力文字列。未設定時は `NULL`。
 */
const char *tk_get_user_input_ctx(tokenizer_context_t *ctx);
/**
 * @brief 指定コンテキストの入力文字列（診断表示用）を設定する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @param p 設定する入力文字列。
 */
void tk_set_user_input_ctx(tokenizer_context_t *ctx, const char *p);

/**
 * @brief 指定コンテキストのファイル名（診断表示用）を取得する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @return 設定済みファイル名。未設定時は `NULL`。
 */
const char *tk_get_filename_ctx(tokenizer_context_t *ctx);
/**
 * @brief 指定コンテキストのファイル名（診断表示用）を設定する。
 * @param ctx 対象コンテキスト。`NULL` の場合は既定コンテキスト。
 * @param name 設定するファイル名。
 */
void tk_set_filename_ctx(tokenizer_context_t *ctx, const char *name);

/**
 * @brief strict C11 モードの有効/無効を取得する。
 * @note strict C11 が有効な場合、2進整数リテラル（`0b...`）は拒否される。
 */
bool tk_get_strict_c11_mode(void);
/**
 * @brief strict C11 モードの有効/無効を設定する。
 * @param strict `true` で strict C11 を有効化。
 */
void tk_set_strict_c11_mode(bool strict);
/** @brief トライグラフ置換の有効/無効を取得する。 */
bool tk_get_enable_trigraphs(void);
/**
 * @brief トライグラフ置換の有効/無効を設定する。
 * @param enable `true` で有効化。
 */
void tk_set_enable_trigraphs(bool enable);
/**
 * @brief 2進整数リテラル拡張の有効/無効を取得する。
 * @note strict C11 が有効な場合、この設定が true でも `0b...` は拒否される。
 */
bool tk_get_enable_binary_literals(void);
/**
 * @brief 2進整数リテラル拡張の有効/無効を設定する。
 * @param enable `true` で有効化。
 */
void tk_set_enable_binary_literals(bool enable);
/** @brief C11監査ログ（拡張使用検出）の有効/無効を取得する。 */
bool tk_get_enable_c11_audit_extensions(void);
/**
 * @brief C11監査ログ（拡張使用検出）の有効/無効を設定する。
 * @param enable `true` で有効化。
 */
void tk_set_enable_c11_audit_extensions(bool enable);

/**
 * @brief 既定のTokenizerコンテキストを返す。
 * @return 既定コンテキストへのポインタ。
 */
tokenizer_context_t *tk_get_default_context(void);
/**
 * @brief コンテキストを既定値で初期化する。
 * @param ctx 初期化対象コンテキスト。
 */
void tk_context_init(tokenizer_context_t *ctx);
/** @brief コンテキストのstrict C11有効/無効を取得する。 */
bool tk_ctx_get_strict_c11_mode(const tokenizer_context_t *ctx);
/**
 * @brief コンテキストのstrict C11有効/無効を設定する。
 * @param ctx 対象コンテキスト。
 * @param strict `true` で有効化。
 */
void tk_ctx_set_strict_c11_mode(tokenizer_context_t *ctx, bool strict);
/** @brief コンテキストのトライグラフ置換有効/無効を取得する。 */
bool tk_ctx_get_enable_trigraphs(const tokenizer_context_t *ctx);
/**
 * @brief コンテキストのトライグラフ置換有効/無効を設定する。
 * @param ctx 対象コンテキスト。
 * @param enable `true` で有効化。
 */
void tk_ctx_set_enable_trigraphs(tokenizer_context_t *ctx, bool enable);
/** @brief コンテキストの2進整数リテラル拡張有効/無効を取得する。 */
bool tk_ctx_get_enable_binary_literals(const tokenizer_context_t *ctx);
/**
 * @brief コンテキストの2進整数リテラル拡張有効/無効を設定する。
 * @param ctx 対象コンテキスト。
 * @param enable `true` で有効化。
 */
void tk_ctx_set_enable_binary_literals(tokenizer_context_t *ctx, bool enable);
/** @brief コンテキストのC11監査ログ有効/無効を取得する。 */
bool tk_ctx_get_enable_c11_audit_extensions(const tokenizer_context_t *ctx);
/**
 * @brief コンテキストのC11監査ログ有効/無効を設定する。
 * @param ctx 対象コンテキスト。
 * @param enable `true` で有効化。
 */
void tk_ctx_set_enable_c11_audit_extensions(tokenizer_context_t *ctx, bool enable);

/** @brief Tokenizerメモリアロケーション統計。 */
typedef struct {
  size_t alloc_count;
  size_t alloc_bytes;
  size_t peak_alloc_bytes;
} tokenizer_stats_t;

/** @brief Tokenizer統計カウンタをリセットする。 */
void tk_reset_tokenizer_stats(void);
/**
 * @brief 現在のTokenizer統計を取得する。
 * @return `alloc_count` / `alloc_bytes` / `peak_alloc_bytes` を格納した統計値。
 */
tokenizer_stats_t tk_get_tokenizer_stats(void);

#endif

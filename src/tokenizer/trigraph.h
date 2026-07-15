#ifndef TOKENIZER_TRIGRAPH_H
#define TOKENIZER_TRIGRAPH_H

typedef struct tokenizer_context_t tokenizer_context_t;

/**
 * @brief 翻訳フェーズ1: トライグラフ (`??=` 等) を対応文字へ置換する。
 * @param in 入力ソース文字列。
 * @return トライグラフを含む場合は置換した新規バッファ、含まない/無効化時は `in` をそのまま返す。
 *         有効化判定は現在の実行コンテキスト (tk_ctx_get_enable_trigraphs) に従う。
 */
char *tk_replace_trigraphs(tokenizer_context_t *context, const char *in);

#endif

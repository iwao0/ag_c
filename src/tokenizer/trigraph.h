#ifndef TOKENIZER_TRIGRAPH_H
#define TOKENIZER_TRIGRAPH_H

typedef struct tokenizer_context_t tokenizer_context_t;

/**
 * @brief Translation phase 1: replace trigraphs (such as `??=`) with their corresponding characters.
 * @param in Input source string.
 * @return A newly allocated replacement buffer when trigraphs are present;
 *         otherwise `in` unchanged when none are present or replacement is disabled.
 *         Enablement follows the current execution context (tk_ctx_get_enable_trigraphs).
 */
char *tk_replace_trigraphs(tokenizer_context_t *context, const char *in);

#endif

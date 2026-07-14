#include "preprocess_compat.h"

#include "../compilation_session_compat.h"

token_t *preprocess(token_t *tok) {
  return preprocess_ctx(tk_get_default_context(), tok);
}

token_t *preprocess_ctx(tokenizer_context_t *tk_ctx, token_t *tok) {
  ag_target_info_t target =
      ag_compilation_session_effective_target_compat();
  return preprocess_for_target_ctx(tk_ctx, &target, tok);
}

token_t *pp_stream_open(
    pp_stream_t **out_s, tokenizer_context_t *tk_ctx, const char *src) {
  ag_target_info_t target =
      ag_compilation_session_effective_target_compat();
  return pp_stream_open_for_target(out_s, tk_ctx, &target, src);
}

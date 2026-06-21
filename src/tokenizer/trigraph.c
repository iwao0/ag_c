/*
 * 翻訳フェーズ1: トライグラフ置換 (C11 5.2.1.1)。トークナイズ前の入力正規化で、
 * tokenize_prepare_input から呼ばれる。トークン生成本体からは独立した前処理。
 */
#include "trigraph.h"
#include "allocator.h"
#include "context.h"
#include <string.h>

static char trigraph_to_char(char c) {
  switch (c) {
    case '=': return '#';
    case '(': return '[';
    case '/': return '\\';
    case ')': return ']';
    case '\'': return '^';
    case '<': return '{';
    case '>': return '}';
    case '!': return '|';
    case '-': return '~';
    default: return '\0';
  }
}

char *tk_replace_trigraphs(const char *in) {
  if (!tk_ctx_get_enable_trigraphs(tk_runtime_ctx())) return (char *)in;
  size_t n = strlen(in);
  bool has_trigraph = false;
  for (size_t i = 0; i + 2 < n; i++) {
    if (in[i] == '?' && in[i + 1] == '?' && trigraph_to_char(in[i + 2])) {
      has_trigraph = true;
      break;
    }
  }
  if (!has_trigraph) return (char *)in;

  char *out = tk_allocator_calloc(n + 1, 1);
  size_t i = 0;
  size_t j = 0;

  while (i < n) {
    if (i + 2 < n && in[i] == '?' && in[i + 1] == '?') {
      char mapped = trigraph_to_char(in[i + 2]);
      if (mapped) {
        out[j++] = mapped;
        i += 3;
        continue;
      }
    }
    out[j++] = in[i++];
  }
  out[j] = '\0';
  return out;
}

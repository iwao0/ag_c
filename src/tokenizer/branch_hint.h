#ifndef TOKENIZER_BRANCH_HINT_H
#define TOKENIZER_BRANCH_HINT_H

#if defined(__GNUC__) || defined(__clang__)
#define TK_LIKELY(x) __builtin_expect(!!(x), 1)
#define TK_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define TK_LIKELY(x) (x)
#define TK_UNLIKELY(x) (x)
#endif

#endif

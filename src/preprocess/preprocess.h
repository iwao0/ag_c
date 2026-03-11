#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "../tokenizer/tokenizer.h"

// プリプロセッサのエントリポイント
// トークン列を受け取り、マクロ展開とディレクティブ処理を
// 行った後の新しいトークン列を返す
token_t *preprocess(token_t *tok);

#endif

#include "messages.h"
#include <stddef.h>

const char *diag_message_ja(diag_error_id_t id) {
  switch (id) {
    case DIAG_ERR_INTERNAL_OOM: return "メモリ確保に失敗しました";
    case DIAG_ERR_PREPROCESS_GENERIC: return "プリプロセッサエラーです";
    case DIAG_ERR_TOKENIZER_GENERIC: return "トークナイズエラーです";
    case DIAG_ERR_TOKENIZER_UNEXPECTED_CHAR: return "不正な文字です";
    case DIAG_ERR_TOKENIZER_TOKEN_TOO_LONG: return "トークン長が上限を超えています";
    case DIAG_ERR_PARSER_GENERIC: return "構文解析エラーです";
    case DIAG_ERR_CODEGEN_GENERIC: return "コード生成エラーです";
  }
  return NULL;
}

const char *diag_message_en(diag_error_id_t id) {
  switch (id) {
    case DIAG_ERR_INTERNAL_OOM: return "Out of memory";
    case DIAG_ERR_PREPROCESS_GENERIC: return "Preprocessor error";
    case DIAG_ERR_TOKENIZER_GENERIC: return "Tokenizer error";
    case DIAG_ERR_TOKENIZER_UNEXPECTED_CHAR: return "Unexpected character";
    case DIAG_ERR_TOKENIZER_TOKEN_TOO_LONG: return "Token is too long";
    case DIAG_ERR_PARSER_GENERIC: return "Parser error";
    case DIAG_ERR_CODEGEN_GENERIC: return "Codegen error";
  }
  return NULL;
}

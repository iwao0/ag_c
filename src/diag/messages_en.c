#include "messages.h"
#include <stddef.h>

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

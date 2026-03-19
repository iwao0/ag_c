#include "messages.h"
#include <stddef.h>

const char *diag_message_en(diag_error_id_t id) {
  switch (id) {
    case DIAG_ERR_INTERNAL_OOM: return "Out of memory";
    case DIAG_ERR_PREPROCESS_GENERIC: return "Preprocessor error";
    case DIAG_ERR_TOKENIZER_GENERIC: return "Tokenizer error";
    case DIAG_ERR_TOKENIZER_UNEXPECTED_CHAR: return "Unexpected character";
    case DIAG_ERR_TOKENIZER_TOKEN_TOO_LONG: return "Token is too long";
    case DIAG_ERR_TOKENIZER_UNTERMINATED: return "Unterminated literal or comment";
    case DIAG_ERR_TOKENIZER_INVALID_NUMBER: return "Invalid numeric literal";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE: return "Invalid escape sequence";
    case DIAG_ERR_TOKENIZER_EXPECTED_TOKEN: return "Expected token is missing";
    case DIAG_ERR_TOKENIZER_EXPECTED_INTEGER: return "Expected integer is missing";
    case DIAG_ERR_PARSER_GENERIC: return "Parser error";
    case DIAG_ERR_CODEGEN_GENERIC: return "Codegen error";
    case DIAG_ERR_CODEGEN_OUTPUT_FAILED: return "Failed to emit code";
    case DIAG_ERR_CODEGEN_INVALID_LVALUE: return "Invalid lvalue in assignment";
    case DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW: return "Invalid control flow usage";
  }
  return NULL;
}

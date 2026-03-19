#include "messages.h"
#include <stddef.h>

/**
 * @brief エラーIDに対応する英語メッセージを返す。
 * @param id エラーID。
 * @return 英語メッセージ。未定義時は NULL。
 */
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
    case DIAG_ERR_TOKENIZER_INVALID_CHAR_LITERAL: return "Invalid character literal";
    case DIAG_ERR_TOKENIZER_UNTERMINATED_COMMENT: return "Unterminated comment";
    case DIAG_ERR_TOKENIZER_UNTERMINATED_LITERAL: return "Unterminated string or character literal";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_HEX: return "Invalid hexadecimal escape";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_UCN: return "Invalid universal character name escape";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_GENERAL: return "Invalid escape";
    case DIAG_ERR_PARSER_GENERIC: return "Parser error";
    case DIAG_ERR_PARSER_EXPECTED_TOKEN: return "Expected token is missing";
    case DIAG_ERR_PARSER_UNEXPECTED_TOKEN: return "Unexpected token";
    case DIAG_ERR_PARSER_UNDEFINED_SYMBOL: return "Undefined symbol";
    case DIAG_ERR_PARSER_DUPLICATE_SYMBOL: return "Duplicate symbol";
    case DIAG_ERR_PARSER_INVALID_CONTEXT: return "Invalid context";
    case DIAG_ERR_PARSER_INVALID_TYPE_SPEC: return "Invalid type specifier combination";
    case DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE: return "String literal is too large";
    case DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID: return "Invalid size while concatenating string literals";
    case DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED: return "Expected _Static_assert";
    case DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST: return "_Static_assert condition must be an integer constant expression";
    case DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING: return "_Static_assert second argument must be a string literal";
    case DIAG_ERR_PARSER_STATIC_ASSERT_FAILED: return "_Static_assert failed";
    case DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN: return "Missing ')' in function declarator";
    case DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED: return "Typedef name is required";
    case DIAG_ERR_PARSER_TYPE_NAME_REQUIRED: return "Type name is required";
    case DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED: return "Variable name is required";
    case DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED: return "'(' is required after _Alignas";
    case DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED: return "Type name is required in _Atomic(...)";
    case DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED: return "Array size must be a positive integer";
    case DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN: return "Incomplete type member is not allowed";
    case DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED: return "Member type is required";
    case DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN: return "Function type member is not allowed";
    case DIAG_ERR_PARSER_TYPEDEF_KEYWORD_REQUIRED: return "'typedef' is required";
    case DIAG_ERR_PARSER_ARRAY_SIZE_CONSTEXPR_REQUIRED: return "Array size requires an integer constant expression";
    case DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY: return "Scalar brace initializer supports only one element";
    case DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED: return "Failed to resolve string initializer";
    case DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS: return "Array initializer exceeds the number of elements";
    case DIAG_ERR_PARSER_ARRAY_INIT_DUPLICATE_ELEMENT: return "Array initializer has a duplicate element designation";
    case DIAG_ERR_PARSER_ARRAY_INIT_UNSUPPORTED_FORM: return "Array initialization currently supports only '{...}' or string literal";
    case DIAG_ERR_PARSER_AGGREGATE_INIT_SCALAR_SIZE_UNSUPPORTED: return "Struct/union initialization currently supports only 1/2/4/8-byte scalars";
    case DIAG_ERR_PARSER_AGGREGATE_INIT_BRACE_REQUIRED: return "Struct/union initialization currently requires '{...}' form";
    case DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS: return "Struct initializer exceeds the number of members";
    case DIAG_ERR_PARSER_STRUCT_INIT_DUPLICATE_MEMBER: return "Struct initializer has a duplicate member designation";
    case DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED: return "Single-expression struct initialization requires a compatible object";
    case DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND: return "Target member for union initialization was not found";
    case DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY: return "Union initializer currently supports only one element";
    case DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN: return "Object of incomplete type cannot be declared";
    case DIAG_ERR_CODEGEN_GENERIC: return "Codegen error";
    case DIAG_ERR_CODEGEN_OUTPUT_FAILED: return "Failed to emit code";
    case DIAG_ERR_CODEGEN_INVALID_LVALUE: return "Invalid lvalue in assignment";
    case DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW: return "Invalid control flow usage";
  }
  return NULL;
}

#include "messages.h"
#include "ui_texts.h"
#include <stddef.h>

/**
 * @brief エラーIDに対応する英語メッセージを返す。
 * @param id エラーID。
 * @return 英語メッセージ。未定義時は NULL。
 */
const char *diag_message_en(diag_error_id_t id) {
  switch (id) {
    case DIAG_ERR_INTERNAL_OOM: return "Out of memory";
    case DIAG_ERR_INTERNAL_USAGE: return "Usage: %s <input.c>";
    case DIAG_ERR_INTERNAL_INPUT_READ_FAILED: return "Failed to read input file: %s";
    case DIAG_ERR_INTERNAL_CONFIG_TOML_PARSE_FAILED: return "Failed to parse config.toml: %s";
    case DIAG_ERR_INTERNAL_CONFIG_TOML_FALLBACK_DEFAULTS: return "config.toml load aborted; applying defaults";
    case DIAG_ERR_PREPROCESS_GENERIC: return "Preprocessor error";
    case DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME: return "Invalid include filename";
    case DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH: return "Disallowed include path: %s";
    case DIAG_ERR_PREPROCESS_PARENT_DIR_INCLUDE_FORBIDDEN: return "Include path containing parent directory reference is forbidden: %s";
    case DIAG_ERR_PREPROCESS_INCLUDE_NEST_TOO_DEEP: return "Include nesting is too deep";
    case DIAG_ERR_PREPROCESS_INCLUDE_CYCLE_DETECTED: return "Include cycle detected: %s";
    case DIAG_ERR_PREPROCESS_RPAREN_REQUIRED: return "')' is required";
    case DIAG_ERR_PREPROCESS_IF_INT_LITERAL_REQUIRED: return "Integer literal is required in #if constant expression";
    case DIAG_ERR_PREPROCESS_CONST_EXPR_UNEXPECTED_TOKEN: return "Constant expression error: unexpected token";
    case DIAG_ERR_PREPROCESS_DIVISION_BY_ZERO: return "Division by zero";
    case DIAG_ERR_PREPROCESS_DEFINED_MACRO_NAME_REQUIRED: return "Macro name is required after defined";
    case DIAG_ERR_PREPROCESS_DEFINED_RPAREN_MISSING: return "Missing closing parenthesis for defined";
    case DIAG_ERR_PREPROCESS_CONST_EXPR_EXTRA_TOKEN: return "Extra token in constant expression";
    case DIAG_ERR_PREPROCESS_STRINGIZE_SIZE_TOO_LARGE: return "Stringize size is too large";
    case DIAG_ERR_PREPROCESS_STRINGIZE_INVALID_SIZE: return "Invalid size while stringizing";
    case DIAG_ERR_PREPROCESS_TOKEN_PASTE_SIZE_TOO_LARGE: return "Token pasting size is too large";
    case DIAG_ERR_PREPROCESS_INCLUDE_FILENAME_TOO_LARGE: return "Include filename is too large";
    case DIAG_ERR_PREPROCESS_GT_REQUIRED: return "'>' is required";
    case DIAG_ERR_PREPROCESS_MACRO_NAME_REQUIRED: return "Macro name is missing";
    case DIAG_ERR_PREPROCESS_ELSE_WITHOUT_IF: return "Stray #else";
    case DIAG_ERR_PREPROCESS_DUPLICATE_ELSE: return "Duplicate #else";
    case DIAG_ERR_PREPROCESS_ELIF_WITHOUT_IF: return "Stray #elif";
    case DIAG_ERR_PREPROCESS_ELIF_AFTER_ELSE: return "#elif after #else";
    case DIAG_ERR_PREPROCESS_ENDIF_WITHOUT_IF: return "Stray #endif";
    case DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT: return "Invalid macro argument";
    case DIAG_ERR_PREPROCESS_ERROR_MESSAGE_TOO_LARGE: return "Error message is too large";
    case DIAG_ERR_PREPROCESS_FUNC_MACRO_ARG_NOT_CLOSED: return "Function-like macro invocation arguments are not closed";
    case DIAG_ERR_PREPROCESS_LINE_NUMBER_INVALID: return "Invalid #line line number";
    case DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID: return "Invalid #line filename";
    case DIAG_ERR_PREPROCESS_MACRO_EXPANSION_LIMIT_EXCEEDED: return "Macro expansion limit exceeded";
    case DIAG_ERR_PREPROCESS_TOKEN_PASTE_INVALID_RESULT: return "Invalid token-paste result";
    case DIAG_ERR_PREPROCESS_MACRO_TOKEN_PASTE_INVALID_POSITION: return "Invalid ## position in macro definition";
    case DIAG_ERR_PREPROCESS_INCLUDE_READ_FAILED: return "Failed to read include file: %s";
    case DIAG_ERR_PREPROCESS_ERROR_DIRECTIVE: return "error: %s";
    case DIAG_ERR_PREPROCESS_INCLUDE_NOT_FOUND: return "Include file not found: %s";
    case DIAG_ERR_PREPROCESS_INCLUDE_PERMISSION_DENIED: return "Permission denied for include file: %s";
    case DIAG_ERR_PREPROCESS_INCLUDE_SYMLINK_LOOP: return "Symlink loop detected in include file: %s";
    case DIAG_ERR_PREPROCESS_IF_EXPR_TOKEN_LIMIT_EXCEEDED: return "#if expression token count limit exceeded";
    case DIAG_ERR_PREPROCESS_IF_EXPR_EVAL_LIMIT_EXCEEDED: return "#if expression evaluation step limit exceeded";
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
    case DIAG_ERR_TOKENIZER_TOKEN_SIZE_WITH_NAME: return "%s is too large";
    case DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE: return "Integer literal is too large";
    case DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID: return "Invalid integer suffix";
    case DIAG_ERR_TOKENIZER_INT_LITERAL_INVALID: return "Invalid integer literal";
    case DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID: return "Invalid numeric literal";
    case DIAG_ERR_TOKENIZER_HEX_FLOAT_LITERAL_INVALID: return "Invalid hexadecimal floating literal";
    case DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID: return "Invalid floating suffix";
    case DIAG_ERR_TOKENIZER_BIN_LITERAL_STRICT_UNSUPPORTED: return "Binary literal is not supported in strict C11";
    case DIAG_ERR_TOKENIZER_BIN_LITERAL_INVALID: return "Invalid binary literal";
    case DIAG_ERR_TOKENIZER_OCT_LITERAL_INVALID: return "Invalid octal literal";
    case DIAG_ERR_TOKENIZER_CHAR_LITERAL_EMPTY: return "Empty character literal is not allowed";
    case DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID: return "Invalid character literal";
    case DIAG_ERR_TOKENIZER_STRING_LITERAL_UNTERMINATED: return "Unterminated string literal";
    case DIAG_ERR_TOKENIZER_IDENT_UCN_INVALID: return "Invalid UCN in identifier";
    case DIAG_ERR_TOKENIZER_TOKENIZE_FAILED: return "Failed to tokenize";
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
    case DIAG_ERR_PARSER_MEMBER_NOT_FOUND: return "Member '%.*s' was not found";
    case DIAG_ERR_PARSER_CAST_STORAGE_CLASS_FORBIDDEN: return "Storage-class specifier is not allowed in cast type";
    case DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED: return "Cast to %s value is not supported (non-scalar)";
    case DIAG_ERR_PARSER_CAST_TYPE_RESOLVE_FAILED: return "Failed to resolve cast type specifier";
    case DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED: return "_Alignof requires a type name";
    case DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID: return "Invalid _Generic association type";
    case DIAG_ERR_PARSER_GENERIC_NO_MATCH: return "No matching association in _Generic";
    case DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED: return "Number is required";
    case DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED: return "Undefined enumerator '%.*s'";
    case DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED: return "goto to undefined label '%.*s'";
    case DIAG_ERR_PARSER_MISSING_CLOSING_PAREN: return "Missing corresponding closing parenthesis";
    case DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED: return "Function definition is required";
    case DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED: return "%s requires an integer constant expression";
    case DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED: return "%s must be non-negative";
    case DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT: return "_Complex/_Imaginary cast supports only floating types";
    case DIAG_ERR_PARSER_COMPLEX_IMAGINARY_TYPE_REQUIRES_FLOAT: return "_Complex/_Imaginary can be specified only with floating types";
    case DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID: return "Non-void function requires an expression in return";
    case DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID: return "Void function cannot return an expression";
    case DIAG_ERR_PARSER_ARROW_LHS_REQUIRES_STRUCT_PTR: return "Left-hand side of '->' must be a struct/union pointer";
    case DIAG_ERR_PARSER_DOT_LHS_REQUIRES_STRUCT: return "Left-hand side of '.' must be a struct/union";
    case DIAG_ERR_PARSER_STRING_PREFIX_MISMATCH: return "String literals with different prefixes cannot be concatenated";
    case DIAG_ERR_PARSER_VARIADIC_NOT_LAST: return "'...' can be specified only at the end of a variadic argument list";
    case DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE: return "Duplicate symbol (switch case): %lld";
    case DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT: return "Duplicate symbol (switch default)";
    case DIAG_ERR_PARSER_LVALUE_REQUIRED: return "%s target must be an lvalue";
    case DIAG_ERR_PARSER_INTEGER_SCALAR_REQUIRED: return "%s target must be an integer scalar";
    case DIAG_ERR_PARSER_RULE_DETAIL: return "[%s] %s";
    case DIAG_ERR_PARSER_MISSING_ITEM: return "Expected token is missing: %s";
    case DIAG_ERR_PARSER_UNDEFINED_WITH_KIND: return "Undefined symbol (%s): '%.*s'";
    case DIAG_ERR_PARSER_DUPLICATE_WITH_KIND: return "Duplicate symbol (%s): '%.*s'";
    case DIAG_ERR_PARSER_ONLY_IN_SCOPE: return "Invalid context: %s / %s";
    case DIAG_ERR_PARSER_DIAG_FORMAT_FAILED: return "Failed to format diagnostic message";
    case DIAG_ERR_PARSER_DYNARRAY_INVALID_SIZE: return "Invalid array size";
    case DIAG_ERR_PARSER_DYNARRAY_TOO_LARGE: return "Array size is too large";
    case DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED: return "Scalar/pointer to struct cast is disabled by parser config";
    case DIAG_ERR_PARSER_CAST_UNION_SCALAR_POINTER_DISABLED: return "Scalar/pointer to union cast is disabled by parser config";
    case DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH: return "Cast to %s value is not supported (type mismatch)";
    case DIAG_ERR_PARSER_UNION_ARRAY_MEMBER_NONBRACE_UNSUPPORTED: return "Non-braced initialization for union array member is disabled by parser config";
    case DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY: return "Nested designator target is not an array member";
    case DIAG_ERR_PARSER_CONST_ASSIGNMENT: return "Cannot assign to const-qualified variable";
    case DIAG_ERR_PARSER_CONST_QUAL_DISCARD: return "Implicit conversion discards 'const' qualifier from pointer";
    case DIAG_ERR_CODEGEN_GENERIC: return "Codegen error";
    case DIAG_ERR_CODEGEN_OUTPUT_FAILED: return "Failed to emit code";
    case DIAG_ERR_CODEGEN_INVALID_LVALUE: return "Invalid lvalue in assignment";
    case DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW: return "Invalid control flow usage";
    case DIAG_ERR_CODEGEN_BREAK_OUTSIDE_LOOP_OR_SWITCH: return "break can only be used inside loop or switch";
    case DIAG_ERR_CODEGEN_CONTINUE_OUTSIDE_LOOP: return "continue can only be used inside loop";
    case DIAG_ERR_CODEGEN_GOTO_LABEL_UNDEFINED: return "goto to undefined label '%.*s'";
  }
  return NULL;
}

const char *diag_warn_message_en(diag_warn_id_t id) {
  switch (id) {
    case DIAG_WARN_PARSER_IMPLICIT_INT_RETURN: return "return type omitted (implicit int)";
    case DIAG_WARN_PARSER_UNREACHABLE_CODE: return "unreachable code";
    case DIAG_WARN_PARSER_UNUSED_VARIABLE: return "unused variable '%.*s'";
    case DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE: return "variable '%.*s' is used uninitialized";
  }
  return NULL;
}

/**
 * @brief テキストIDに対応する英語テキストを返す。
 * @param id テキストID。
 * @return 英語テキスト。未定義時は NULL。
 */
const char *diag_text_en(diag_text_id_t id) {
  switch (id) {
    case DIAG_TEXT_WARNING: return "warning";
    case DIAG_TEXT_C11_AUDIT_PREFIX: return "c11-audit";
    case DIAG_TEXT_C11_AUDIT_BINARY_LITERAL_EXTENSION: return "binary literal extension";
    case DIAG_TEXT_TAG_NAME: return "tag name";
    case DIAG_TEXT_MEMBER_NAME: return "member name";
    case DIAG_TEXT_ENUMERATOR_NAME: return "enumerator name";
    case DIAG_TEXT_PARAMETER: return "parameter";
    case DIAG_TEXT_TAG_TYPE_SUFFIX: return "tag type";
    case DIAG_TEXT_TAG_TYPE: return "tag type";
    case DIAG_TEXT_BREAK: return "break";
    case DIAG_TEXT_CONTINUE: return "continue";
    case DIAG_TEXT_LOOP_OR_SWITCH_SCOPE: return "inside a loop or switch";
    case DIAG_TEXT_LOOP_SCOPE: return "inside a loop";
    case DIAG_TEXT_GOTO_LABEL_AFTER: return "label name after goto";
    case DIAG_TEXT_SWITCH_SCOPE: return "inside switch";
    case DIAG_TEXT_CASE: return "case";
    case DIAG_TEXT_DEFAULT: return "default";
    case DIAG_TEXT_LABEL: return "label";
    case DIAG_TEXT_ARRAY_DESIGNATOR_INDEX: return "array designator index";
    case DIAG_TEXT_ARRAY_SIZE: return "array size";
    case DIAG_TEXT_WHILE: return "'while'";
  }
  return NULL;
}

const char *diag_ui_text_en(diag_ui_text_id_t id) {
  switch (id) {
    case DIAG_UI_TEXT_UNKNOWN_TEXT: return "unknown.text";
    case DIAG_UI_TEXT_ACTUAL_TOKEN_LABEL: return "actual token";
    case DIAG_UI_TEXT_ACTUAL_TOKEN_KIND_FMT: return " (actual token kind: %d)";
  }
  return NULL;
}

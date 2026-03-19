#include "messages.h"
#include <stddef.h>

/**
 * @brief エラーIDに対応する日本語メッセージを返す。
 * @param id エラーID。
 * @return 日本語メッセージ。未定義時は NULL。
 */
const char *diag_message_ja(diag_error_id_t id) {
  switch (id) {
    case DIAG_ERR_INTERNAL_OOM: return "メモリ確保に失敗しました";
    case DIAG_ERR_PREPROCESS_GENERIC: return "プリプロセッサエラーです";
    case DIAG_ERR_TOKENIZER_GENERIC: return "トークナイズエラーです";
    case DIAG_ERR_TOKENIZER_UNEXPECTED_CHAR: return "不正な文字です";
    case DIAG_ERR_TOKENIZER_TOKEN_TOO_LONG: return "トークン長が上限を超えています";
    case DIAG_ERR_TOKENIZER_UNTERMINATED: return "リテラルまたはコメントが閉じられていません";
    case DIAG_ERR_TOKENIZER_INVALID_NUMBER: return "数値リテラルが不正です";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE: return "エスケープシーケンスが不正です";
    case DIAG_ERR_TOKENIZER_EXPECTED_TOKEN: return "必要なトークンがありません";
    case DIAG_ERR_TOKENIZER_EXPECTED_INTEGER: return "必要な整数がありません";
    case DIAG_ERR_TOKENIZER_INVALID_CHAR_LITERAL: return "文字リテラルが不正です";
    case DIAG_ERR_TOKENIZER_UNTERMINATED_COMMENT: return "コメントが閉じられていません";
    case DIAG_ERR_TOKENIZER_UNTERMINATED_LITERAL: return "文字列または文字リテラルが閉じられていません";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_HEX: return "16進エスケープが不正です";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_UCN: return "UCNエスケープが不正です";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_GENERAL: return "不正なエスケープです";
    case DIAG_ERR_PARSER_GENERIC: return "構文解析エラーです";
    case DIAG_ERR_PARSER_EXPECTED_TOKEN: return "必要なトークンがありません";
    case DIAG_ERR_PARSER_UNEXPECTED_TOKEN: return "予期しないトークンです";
    case DIAG_ERR_PARSER_UNDEFINED_SYMBOL: return "未定義の識別子です";
    case DIAG_ERR_PARSER_DUPLICATE_SYMBOL: return "識別子が重複しています";
    case DIAG_ERR_PARSER_INVALID_CONTEXT: return "この文脈では使用できません";
    case DIAG_ERR_PARSER_INVALID_TYPE_SPEC: return "不正な型指定子の組み合わせです";
    case DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE: return "文字列リテラルが大きすぎます";
    case DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID: return "文字列連結中にサイズが不正です";
    case DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED: return "'_Static_assert' が必要です";
    case DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST: return "_Static_assert の条件は整数定数式が必要です";
    case DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING: return "_Static_assert の第2引数は文字列リテラルが必要です";
    case DIAG_ERR_PARSER_STATIC_ASSERT_FAILED: return "_Static_assert が失敗しました";
    case DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN: return "関数宣言子の ')' が不足しています";
    case DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED: return "typedef名が必要です";
    case DIAG_ERR_PARSER_TYPE_NAME_REQUIRED: return "型名が必要です";
    case DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED: return "変数名が期待されます";
    case DIAG_ERR_CODEGEN_GENERIC: return "コード生成エラーです";
    case DIAG_ERR_CODEGEN_OUTPUT_FAILED: return "コード生成出力に失敗しました";
    case DIAG_ERR_CODEGEN_INVALID_LVALUE: return "代入の左辺値が不正です";
    case DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW: return "制御フローの使用位置が不正です";
  }
  return NULL;
}

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
    case DIAG_ERR_CODEGEN_GENERIC: return "Codegen error";
    case DIAG_ERR_CODEGEN_OUTPUT_FAILED: return "Failed to emit code";
    case DIAG_ERR_CODEGEN_INVALID_LVALUE: return "Invalid lvalue in assignment";
    case DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW: return "Invalid control flow usage";
  }
  return NULL;
}

#include "error_catalog.h"
#include <stddef.h>

typedef struct {
  diag_error_id_t id;
  const char *code;
  const char *key;
} diag_entry_t;

static const diag_entry_t k_diag_entries[] = {
    {DIAG_ERR_INTERNAL_OOM, "E0001", "internal.out_of_memory"},
    {DIAG_ERR_PREPROCESS_GENERIC, "E1000", "preprocess.generic"},
    {DIAG_ERR_TOKENIZER_GENERIC, "E2000", "tokenizer.generic"},
    {DIAG_ERR_TOKENIZER_UNEXPECTED_CHAR, "E2001", "tokenizer.unexpected_char"},
    {DIAG_ERR_TOKENIZER_TOKEN_TOO_LONG, "E2002", "tokenizer.token_too_long"},
    {DIAG_ERR_TOKENIZER_UNTERMINATED, "E2003", "tokenizer.unterminated"},
    {DIAG_ERR_TOKENIZER_INVALID_NUMBER, "E2004", "tokenizer.invalid_number"},
    {DIAG_ERR_TOKENIZER_INVALID_ESCAPE, "E2005", "tokenizer.invalid_escape"},
    {DIAG_ERR_TOKENIZER_EXPECTED_TOKEN, "E2006", "tokenizer.expected_token"},
    {DIAG_ERR_TOKENIZER_EXPECTED_INTEGER, "E2007", "tokenizer.expected_integer"},
    {DIAG_ERR_TOKENIZER_INVALID_CHAR_LITERAL, "E2008", "tokenizer.invalid_char_literal"},
    {DIAG_ERR_TOKENIZER_UNTERMINATED_COMMENT, "E2009", "tokenizer.unterminated_comment"},
    {DIAG_ERR_TOKENIZER_UNTERMINATED_LITERAL, "E2010", "tokenizer.unterminated_literal"},
    {DIAG_ERR_TOKENIZER_INVALID_ESCAPE_HEX, "E2011", "tokenizer.invalid_escape_hex"},
    {DIAG_ERR_TOKENIZER_INVALID_ESCAPE_UCN, "E2012", "tokenizer.invalid_escape_ucn"},
    {DIAG_ERR_TOKENIZER_INVALID_ESCAPE_GENERAL, "E2013", "tokenizer.invalid_escape_general"},
    {DIAG_ERR_PARSER_GENERIC, "E3000", "parser.generic"},
    {DIAG_ERR_PARSER_EXPECTED_TOKEN, "E3001", "parser.expected_token"},
    {DIAG_ERR_PARSER_UNEXPECTED_TOKEN, "E3002", "parser.unexpected_token"},
    {DIAG_ERR_PARSER_UNDEFINED_SYMBOL, "E3003", "parser.undefined_symbol"},
    {DIAG_ERR_PARSER_DUPLICATE_SYMBOL, "E3004", "parser.duplicate_symbol"},
    {DIAG_ERR_PARSER_INVALID_CONTEXT, "E3005", "parser.invalid_context"},
    {DIAG_ERR_PARSER_INVALID_TYPE_SPEC, "E3006", "parser.invalid_type_spec"},
    {DIAG_ERR_PARSER_STRING_LITERAL_TOO_LARGE, "E3007", "parser.string_literal_too_large"},
    {DIAG_ERR_PARSER_STRING_CONCAT_SIZE_INVALID, "E3008", "parser.string_concat_size_invalid"},
    {DIAG_ERR_PARSER_STATIC_ASSERT_EXPECTED, "E3009", "parser.static_assert_expected"},
    {DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST, "E3010", "parser.static_assert_cond_not_const"},
    {DIAG_ERR_PARSER_STATIC_ASSERT_MSG_NOT_STRING, "E3011", "parser.static_assert_msg_not_string"},
    {DIAG_ERR_PARSER_STATIC_ASSERT_FAILED, "E3012", "parser.static_assert_failed"},
    {DIAG_ERR_PARSER_MISSING_FUNC_DECL_RPAREN, "E3013", "parser.missing_func_decl_rparen"},
    {DIAG_ERR_PARSER_TYPEDEF_NAME_REQUIRED, "E3014", "parser.typedef_name_required"},
    {DIAG_ERR_PARSER_TYPE_NAME_REQUIRED, "E3015", "parser.type_name_required"},
    {DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED, "E3016", "parser.variable_name_required"},
    {DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED, "E3017", "parser.alignas_lparen_required"},
    {DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED, "E3018", "parser.atomic_type_name_required"},
    {DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED, "E3019", "parser.array_size_positive_required"},
    {DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN, "E3020", "parser.incomplete_member_forbidden"},
    {DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED, "E3021", "parser.member_type_required"},
    {DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN, "E3022", "parser.function_member_forbidden"},
    {DIAG_ERR_PARSER_TYPEDEF_KEYWORD_REQUIRED, "E3023", "parser.typedef_keyword_required"},
    {DIAG_ERR_PARSER_ARRAY_SIZE_CONSTEXPR_REQUIRED, "E3024", "parser.array_size_constexpr_required"},
    {DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY, "E3025", "parser.scalar_brace_single_element_only"},
    {DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED, "E3026", "parser.string_init_resolve_failed"},
    {DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS, "E3027", "parser.array_init_too_many_elements"},
    {DIAG_ERR_PARSER_ARRAY_INIT_DUPLICATE_ELEMENT, "E3028", "parser.array_init_duplicate_element"},
    {DIAG_ERR_PARSER_ARRAY_INIT_UNSUPPORTED_FORM, "E3029", "parser.array_init_unsupported_form"},
    {DIAG_ERR_PARSER_AGGREGATE_INIT_SCALAR_SIZE_UNSUPPORTED, "E3030", "parser.aggregate_init_scalar_size_unsupported"},
    {DIAG_ERR_PARSER_AGGREGATE_INIT_BRACE_REQUIRED, "E3031", "parser.aggregate_init_brace_required"},
    {DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS, "E3032", "parser.struct_init_too_many_members"},
    {DIAG_ERR_PARSER_STRUCT_INIT_DUPLICATE_MEMBER, "E3033", "parser.struct_init_duplicate_member"},
    {DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED, "E3034", "parser.struct_copy_compat_required"},
    {DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND, "E3035", "parser.union_init_target_member_not_found"},
    {DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY, "E3036", "parser.union_init_single_element_only"},
    {DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN, "E3037", "parser.incomplete_object_forbidden"},
    {DIAG_ERR_PARSER_MEMBER_NOT_FOUND, "E3038", "parser.member_not_found"},
    {DIAG_ERR_PARSER_CAST_STORAGE_CLASS_FORBIDDEN, "E3039", "parser.cast_storage_class_forbidden"},
    {DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED, "E3040", "parser.cast_nonscalar_unsupported"},
    {DIAG_ERR_PARSER_CAST_TYPE_RESOLVE_FAILED, "E3041", "parser.cast_type_resolve_failed"},
    {DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED, "E3042", "parser.alignof_type_name_required"},
    {DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID, "E3043", "parser.generic_assoc_type_invalid"},
    {DIAG_ERR_PARSER_GENERIC_NO_MATCH, "E3044", "parser.generic_no_match"},
    {DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED, "E3045", "parser.primary_number_expected"},
    {DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED, "E3046", "parser.enum_const_undefined"},
    {DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED, "E3047", "parser.goto_label_undefined"},
    {DIAG_ERR_PARSER_MISSING_CLOSING_PAREN, "E3048", "parser.missing_closing_paren"},
    {DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED, "E3049", "parser.function_def_expected"},
    {DIAG_ERR_CODEGEN_GENERIC, "E4000", "codegen.generic"},
    {DIAG_ERR_CODEGEN_OUTPUT_FAILED, "E4001", "codegen.output_failed"},
    {DIAG_ERR_CODEGEN_INVALID_LVALUE, "E4002", "codegen.invalid_lvalue"},
    {DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW, "E4003", "codegen.invalid_control_flow"},
};

/**
 * @brief エラーIDに対応するカタログ要素を探索する。
 * @param id エラーID。
 * @return 対応要素へのポインタ。見つからない場合は NULL。
 */
static const diag_entry_t *find_entry(diag_error_id_t id) {
  for (size_t i = 0; i < sizeof(k_diag_entries) / sizeof(k_diag_entries[0]); i++) {
    if (k_diag_entries[i].id == id) return &k_diag_entries[i];
  }
  return NULL;
}

/**
 * @brief エラーIDを表示コードに変換する。
 * @param id エラーID。
 * @return 表示コード文字列。未登録時は "E9999"。
 */
const char *diag_error_code(diag_error_id_t id) {
  const diag_entry_t *entry = find_entry(id);
  return entry ? entry->code : "E9999";
}

/**
 * @brief エラーIDを論理キーに変換する。
 * @param id エラーID。
 * @return 論理キー文字列。未登録時は "unknown.error"。
 */
const char *diag_error_key(diag_error_id_t id) {
  const diag_entry_t *entry = find_entry(id);
  return entry ? entry->key : "unknown.error";
}

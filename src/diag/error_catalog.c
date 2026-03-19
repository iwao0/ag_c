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
    {DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME, "E1001", "preprocess.invalid_include_filename"},
    {DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH, "E1002", "preprocess.disallowed_include_path"},
    {DIAG_ERR_PREPROCESS_PARENT_DIR_INCLUDE_FORBIDDEN, "E1003", "preprocess.parent_dir_include_forbidden"},
    {DIAG_ERR_PREPROCESS_INCLUDE_NEST_TOO_DEEP, "E1004", "preprocess.include_nest_too_deep"},
    {DIAG_ERR_PREPROCESS_INCLUDE_CYCLE_DETECTED, "E1005", "preprocess.include_cycle_detected"},
    {DIAG_ERR_PREPROCESS_RPAREN_REQUIRED, "E1006", "preprocess.rparen_required"},
    {DIAG_ERR_PREPROCESS_IF_INT_LITERAL_REQUIRED, "E1007", "preprocess.if_int_literal_required"},
    {DIAG_ERR_PREPROCESS_CONST_EXPR_UNEXPECTED_TOKEN, "E1008", "preprocess.const_expr_unexpected_token"},
    {DIAG_ERR_PREPROCESS_DIVISION_BY_ZERO, "E1009", "preprocess.division_by_zero"},
    {DIAG_ERR_PREPROCESS_DEFINED_MACRO_NAME_REQUIRED, "E1010", "preprocess.defined_macro_name_required"},
    {DIAG_ERR_PREPROCESS_DEFINED_RPAREN_MISSING, "E1011", "preprocess.defined_rparen_missing"},
    {DIAG_ERR_PREPROCESS_CONST_EXPR_EXTRA_TOKEN, "E1012", "preprocess.const_expr_extra_token"},
    {DIAG_ERR_PREPROCESS_STRINGIZE_SIZE_TOO_LARGE, "E1013", "preprocess.stringize_size_too_large"},
    {DIAG_ERR_PREPROCESS_STRINGIZE_INVALID_SIZE, "E1014", "preprocess.stringize_invalid_size"},
    {DIAG_ERR_PREPROCESS_TOKEN_PASTE_SIZE_TOO_LARGE, "E1015", "preprocess.token_paste_size_too_large"},
    {DIAG_ERR_PREPROCESS_INCLUDE_FILENAME_TOO_LARGE, "E1016", "preprocess.include_filename_too_large"},
    {DIAG_ERR_PREPROCESS_GT_REQUIRED, "E1017", "preprocess.gt_required"},
    {DIAG_ERR_PREPROCESS_MACRO_NAME_REQUIRED, "E1018", "preprocess.macro_name_required"},
    {DIAG_ERR_PREPROCESS_ELSE_WITHOUT_IF, "E1019", "preprocess.else_without_if"},
    {DIAG_ERR_PREPROCESS_DUPLICATE_ELSE, "E1020", "preprocess.duplicate_else"},
    {DIAG_ERR_PREPROCESS_ELIF_WITHOUT_IF, "E1021", "preprocess.elif_without_if"},
    {DIAG_ERR_PREPROCESS_ELIF_AFTER_ELSE, "E1022", "preprocess.elif_after_else"},
    {DIAG_ERR_PREPROCESS_ENDIF_WITHOUT_IF, "E1023", "preprocess.endif_without_if"},
    {DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT, "E1024", "preprocess.invalid_macro_argument"},
    {DIAG_ERR_PREPROCESS_ERROR_MESSAGE_TOO_LARGE, "E1025", "preprocess.error_message_too_large"},
    {DIAG_ERR_PREPROCESS_FUNC_MACRO_ARG_NOT_CLOSED, "E1026", "preprocess.func_macro_arg_not_closed"},
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
    {DIAG_ERR_TOKENIZER_TOKEN_SIZE_WITH_NAME, "E2014", "tokenizer.token_size_with_name"},
    {DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE, "E2015", "tokenizer.int_literal_too_large"},
    {DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID, "E2016", "tokenizer.int_suffix_invalid"},
    {DIAG_ERR_TOKENIZER_INT_LITERAL_INVALID, "E2017", "tokenizer.int_literal_invalid"},
    {DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID, "E2018", "tokenizer.num_literal_invalid"},
    {DIAG_ERR_TOKENIZER_HEX_FLOAT_LITERAL_INVALID, "E2019", "tokenizer.hex_float_literal_invalid"},
    {DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID, "E2020", "tokenizer.float_suffix_invalid"},
    {DIAG_ERR_TOKENIZER_BIN_LITERAL_STRICT_UNSUPPORTED, "E2021", "tokenizer.bin_literal_strict_unsupported"},
    {DIAG_ERR_TOKENIZER_BIN_LITERAL_INVALID, "E2022", "tokenizer.bin_literal_invalid"},
    {DIAG_ERR_TOKENIZER_OCT_LITERAL_INVALID, "E2023", "tokenizer.oct_literal_invalid"},
    {DIAG_ERR_TOKENIZER_CHAR_LITERAL_EMPTY, "E2024", "tokenizer.char_literal_empty"},
    {DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID, "E2025", "tokenizer.char_literal_invalid"},
    {DIAG_ERR_TOKENIZER_STRING_LITERAL_UNTERMINATED, "E2026", "tokenizer.string_literal_unterminated"},
    {DIAG_ERR_TOKENIZER_IDENT_UCN_INVALID, "E2027", "tokenizer.ident_ucn_invalid"},
    {DIAG_ERR_TOKENIZER_TOKENIZE_FAILED, "E2028", "tokenizer.tokenize_failed"},
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
    {DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED, "E3050", "parser.nonneg_constexpr_required"},
    {DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED, "E3051", "parser.nonneg_value_required"},
    {DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT, "E3052", "parser.complex_imaginary_cast_requires_float"},
    {DIAG_ERR_PARSER_COMPLEX_IMAGINARY_TYPE_REQUIRES_FLOAT, "E3053", "parser.complex_imaginary_type_requires_float"},
    {DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID, "E3054", "parser.return_value_required_nonvoid"},
    {DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID, "E3055", "parser.return_value_forbidden_void"},
    {DIAG_ERR_PARSER_ARROW_LHS_REQUIRES_STRUCT_PTR, "E3056", "parser.arrow_lhs_requires_struct_ptr"},
    {DIAG_ERR_PARSER_DOT_LHS_REQUIRES_STRUCT, "E3057", "parser.dot_lhs_requires_struct"},
    {DIAG_ERR_PARSER_STRING_PREFIX_MISMATCH, "E3058", "parser.string_prefix_mismatch"},
    {DIAG_ERR_PARSER_VARIADIC_NOT_LAST, "E3059", "parser.variadic_not_last"},
    {DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE, "E3060", "parser.switch_duplicate_case"},
    {DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT, "E3061", "parser.switch_duplicate_default"},
    {DIAG_ERR_PARSER_LVALUE_REQUIRED, "E3062", "parser.lvalue_required"},
    {DIAG_ERR_PARSER_INTEGER_SCALAR_REQUIRED, "E3063", "parser.integer_scalar_required"},
    {DIAG_ERR_PARSER_RULE_DETAIL, "E3064", "parser.rule_detail"},
    {DIAG_ERR_PARSER_MISSING_ITEM, "E3065", "parser.missing_item"},
    {DIAG_ERR_PARSER_UNDEFINED_WITH_KIND, "E3066", "parser.undefined_with_kind"},
    {DIAG_ERR_PARSER_DUPLICATE_WITH_KIND, "E3067", "parser.duplicate_with_kind"},
    {DIAG_ERR_PARSER_ONLY_IN_SCOPE, "E3068", "parser.only_in_scope"},
    {DIAG_ERR_PARSER_DIAG_FORMAT_FAILED, "E3069", "parser.diag_format_failed"},
    {DIAG_ERR_PARSER_DYNARRAY_INVALID_SIZE, "E3070", "parser.dynarray_invalid_size"},
    {DIAG_ERR_PARSER_DYNARRAY_TOO_LARGE, "E3071", "parser.dynarray_too_large"},
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

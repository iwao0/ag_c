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
    case DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED: return "_Alignas の後には '(' が必要です";
    case DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED: return "_Atomic(...) の中には型名が必要です";
    case DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED: return "配列サイズは正の整数である必要があります";
    case DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN: return "不完全型のメンバは定義できません";
    case DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED: return "メンバ型が期待されます";
    case DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN: return "関数型のメンバは定義できません";
    case DIAG_ERR_PARSER_TYPEDEF_KEYWORD_REQUIRED: return "'typedef' が必要です";
    case DIAG_ERR_PARSER_ARRAY_SIZE_CONSTEXPR_REQUIRED: return "配列サイズには整数定数式が必要です";
    case DIAG_ERR_PARSER_SCALAR_BRACE_SINGLE_ELEMENT_ONLY: return "スカラ初期化子の波括弧内は1要素のみ対応です";
    case DIAG_ERR_PARSER_STRING_INIT_RESOLVE_FAILED: return "文字列初期化子の解決に失敗しました";
    case DIAG_ERR_PARSER_ARRAY_INIT_TOO_MANY_ELEMENTS: return "配列初期化子が要素数を超えています";
    case DIAG_ERR_PARSER_ARRAY_INIT_DUPLICATE_ELEMENT: return "配列初期化子で同一要素が重複指定されています";
    case DIAG_ERR_PARSER_ARRAY_INIT_UNSUPPORTED_FORM: return "配列初期化は現在 '{...}' または文字列リテラルのみ対応です";
    case DIAG_ERR_PARSER_AGGREGATE_INIT_SCALAR_SIZE_UNSUPPORTED: return "構造体/共用体初期化は現在 1/2/4/8 byte スカラのみ対応です";
    case DIAG_ERR_PARSER_AGGREGATE_INIT_BRACE_REQUIRED: return "構造体/共用体初期化は現在 '{...}' 形式のみ対応です";
    case DIAG_ERR_PARSER_STRUCT_INIT_TOO_MANY_MEMBERS: return "構造体初期化子がメンバ数を超えています";
    case DIAG_ERR_PARSER_STRUCT_INIT_DUPLICATE_MEMBER: return "構造体初期化子で同一メンバが重複指定されています";
    case DIAG_ERR_PARSER_STRUCT_COPY_COMPAT_REQUIRED: return "構造体の単一式初期化は同型オブジェクトのみ対応です";
    case DIAG_ERR_PARSER_UNION_INIT_TARGET_MEMBER_NOT_FOUND: return "共用体の初期化対象メンバが見つかりません";
    case DIAG_ERR_PARSER_UNION_INIT_SINGLE_ELEMENT_ONLY: return "共用体初期化子は現状1要素のみ対応です";
    case DIAG_ERR_PARSER_INCOMPLETE_OBJECT_FORBIDDEN: return "不完全型のオブジェクトは宣言できません";
    case DIAG_ERR_PARSER_MEMBER_NOT_FOUND: return "メンバ '%.*s' は存在しません";
    case DIAG_ERR_PARSER_CAST_STORAGE_CLASS_FORBIDDEN: return "cast 型名にストレージ指定子は使えません";
    case DIAG_ERR_PARSER_CAST_NONSCALAR_UNSUPPORTED: return "%s 値へのキャストは未対応です（非スカラ型）";
    case DIAG_ERR_PARSER_CAST_TYPE_RESOLVE_FAILED: return "cast 型指定子の解釈に失敗しました";
    case DIAG_ERR_PARSER_ALIGNOF_TYPE_NAME_REQUIRED: return "_Alignof には型名が必要です";
    case DIAG_ERR_PARSER_GENERIC_ASSOC_TYPE_INVALID: return "_Generic の関連型が不正です";
    case DIAG_ERR_PARSER_GENERIC_NO_MATCH: return "_Generic に一致する関連がありません";
    case DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED: return "数値を期待しています";
    case DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED: return "未定義の列挙子 '%.*s' です";
    case DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED: return "未定義ラベル '%.*s' への goto です";
    case DIAG_ERR_PARSER_MISSING_CLOSING_PAREN: return "対応する閉じ括弧がありません";
    case DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED: return "関数定義が必要です";
    case DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED: return "%sには整数定数式が必要です";
    case DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED: return "%sは0以上である必要があります";
    case DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT: return "_Complex/_Imaginary cast は浮動小数型のみ対応です";
    case DIAG_ERR_PARSER_COMPLEX_IMAGINARY_TYPE_REQUIRES_FLOAT: return "_Complex/_Imaginary は浮動小数型にのみ指定できます";
    case DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID: return "[stmt] void 以外の関数では return に式が必要です";
    case DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID: return "[stmt] void 関数では return に式を指定できません";
    case DIAG_ERR_CODEGEN_GENERIC: return "コード生成エラーです";
    case DIAG_ERR_CODEGEN_OUTPUT_FAILED: return "コード生成出力に失敗しました";
    case DIAG_ERR_CODEGEN_INVALID_LVALUE: return "代入の左辺値が不正です";
    case DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW: return "制御フローの使用位置が不正です";
  }
  return NULL;
}

#include "messages.h"
#include "ui_texts.h"
#include <stddef.h>

/**
 * @brief エラーIDに対応する日本語メッセージを返す。
 * @param id エラーID。
 * @return 日本語メッセージ。未定義時は NULL。
 */
const char *diag_message_ja(diag_error_id_t id) {
  switch (id) {
    case DIAG_ERR_INTERNAL_OOM: return "メモリ確保に失敗しました";
    case DIAG_ERR_INTERNAL_USAGE: return "使い方: %s <input.c>";
    case DIAG_ERR_INTERNAL_INPUT_READ_FAILED: return "入力ファイルを読み込めませんでした: %s";
    case DIAG_ERR_INTERNAL_CONFIG_TOML_PARSE_FAILED: return "config.toml の解析に失敗しました: %s";
    case DIAG_ERR_INTERNAL_CONFIG_TOML_FALLBACK_DEFAULTS: return "config.toml の読み込みを中止し、既定値を適用します";
    case DIAG_ERR_INTERNAL_INVARIANT_FAILED: return "コンパイラ内部の不変条件に違反しました";
    case DIAG_ERR_PREPROCESS_GENERIC: return "プリプロセッサエラーです";
    case DIAG_ERR_PREPROCESS_INVALID_INCLUDE_FILENAME: return "不正な include ファイル名です";
    case DIAG_ERR_PREPROCESS_DISALLOWED_INCLUDE_PATH: return "許可されない include パスです: %s";
    case DIAG_ERR_PREPROCESS_PARENT_DIR_INCLUDE_FORBIDDEN: return "親ディレクトリ参照を含む include は禁止です: %s";
    case DIAG_ERR_PREPROCESS_INCLUDE_NEST_TOO_DEEP: return "include のネストが深すぎます";
    case DIAG_ERR_PREPROCESS_INCLUDE_CYCLE_DETECTED: return "循環 include を検出しました: %s";
    case DIAG_ERR_PREPROCESS_RPAREN_REQUIRED: return ") が必要です";
    case DIAG_ERR_PREPROCESS_IF_INT_LITERAL_REQUIRED: return "#if の定数式では整数リテラルが必要です";
    case DIAG_ERR_PREPROCESS_CONST_EXPR_UNEXPECTED_TOKEN: return "定数式のエラー: 予期しないトークンです";
    case DIAG_ERR_PREPROCESS_DIVISION_BY_ZERO: return "ゼロ除算";
    case DIAG_ERR_PREPROCESS_DEFINED_MACRO_NAME_REQUIRED: return "defined の後にマクロ名が必要です";
    case DIAG_ERR_PREPROCESS_DEFINED_RPAREN_MISSING: return "defined の閉じ括弧がありません";
    case DIAG_ERR_PREPROCESS_CONST_EXPR_EXTRA_TOKEN: return "定数式に余分なトークンがあります";
    case DIAG_ERR_PREPROCESS_STRINGIZE_SIZE_TOO_LARGE: return "文字列化中にサイズが大きすぎます";
    case DIAG_ERR_PREPROCESS_STRINGIZE_INVALID_SIZE: return "文字列化中にサイズが不正です";
    case DIAG_ERR_PREPROCESS_TOKEN_PASTE_SIZE_TOO_LARGE: return "トークン結合中にサイズが大きすぎます";
    case DIAG_ERR_PREPROCESS_INCLUDE_FILENAME_TOO_LARGE: return "include ファイル名が大きすぎます";
    case DIAG_ERR_PREPROCESS_GT_REQUIRED: return "'>' が必要です";
    case DIAG_ERR_PREPROCESS_MACRO_NAME_REQUIRED: return "マクロ名がありません";
    case DIAG_ERR_PREPROCESS_ELSE_WITHOUT_IF: return "孤立した #else";
    case DIAG_ERR_PREPROCESS_DUPLICATE_ELSE: return "#else の重複";
    case DIAG_ERR_PREPROCESS_ELIF_WITHOUT_IF: return "孤立した #elif";
    case DIAG_ERR_PREPROCESS_ELIF_AFTER_ELSE: return "#else の後の #elif";
    case DIAG_ERR_PREPROCESS_ENDIF_WITHOUT_IF: return "孤立した #endif";
    case DIAG_ERR_PREPROCESS_INVALID_MACRO_ARGUMENT: return "マクロの引数が不正です";
    case DIAG_ERR_PREPROCESS_ERROR_MESSAGE_TOO_LARGE: return "error メッセージが大きすぎます";
    case DIAG_ERR_PREPROCESS_FUNC_MACRO_ARG_NOT_CLOSED: return "関数マクロ呼び出しの引数が閉じられていません";
    case DIAG_ERR_PREPROCESS_LINE_NUMBER_INVALID: return "#line の行番号が不正です";
    case DIAG_ERR_PREPROCESS_LINE_FILENAME_INVALID: return "#line のファイル名が不正です";
    case DIAG_ERR_PREPROCESS_MACRO_EXPANSION_LIMIT_EXCEEDED: return "マクロ展開回数の上限を超えました";
    case DIAG_ERR_PREPROCESS_TOKEN_PASTE_INVALID_RESULT: return "トークン連結結果が不正です";
    case DIAG_ERR_PREPROCESS_MACRO_TOKEN_PASTE_INVALID_POSITION: return "マクロ定義内の ## の位置が不正です";
    case DIAG_ERR_PREPROCESS_INCLUDE_READ_FAILED: return "include ファイルを読み込めませんでした: %s";
    case DIAG_ERR_PREPROCESS_ERROR_DIRECTIVE: return "error: %s";
    case DIAG_ERR_PREPROCESS_INCLUDE_NOT_FOUND: return "include ファイルが見つかりません: %s";
    case DIAG_ERR_PREPROCESS_INCLUDE_PERMISSION_DENIED: return "include ファイルへのアクセス権がありません: %s";
    case DIAG_ERR_PREPROCESS_INCLUDE_SYMLINK_LOOP: return "include でシンボリックリンクの循環を検出しました: %s";
    case DIAG_ERR_PREPROCESS_IF_EXPR_TOKEN_LIMIT_EXCEEDED: return "#if 式のトークン数が上限を超えました";
    case DIAG_ERR_PREPROCESS_IF_EXPR_EVAL_LIMIT_EXCEEDED: return "#if 式の評価ステップ数が上限を超えました";
    case DIAG_ERR_PREPROCESS_VIRTUAL_HEADER_COUNT_LIMIT_EXCEEDED: return "virtual header件数が上限を超えました";
    case DIAG_ERR_PREPROCESS_VIRTUAL_HEADER_FILE_SIZE_LIMIT_EXCEEDED: return "virtual headerのファイルサイズが上限を超えました: %s";
    case DIAG_ERR_PREPROCESS_VIRTUAL_HEADER_TOTAL_SIZE_LIMIT_EXCEEDED: return "virtual headerの合計サイズが上限を超えました";
    case DIAG_ERR_PREPROCESS_VIRTUAL_HEADER_DUPLICATE_PATH: return "virtual header pathが重複しています: %s";
    case DIAG_ERR_TOKENIZER_GENERIC: return "トークナイズエラーです";
    case DIAG_ERR_TOKENIZER_UNEXPECTED_CHAR: return "不正な文字です";
    case DIAG_ERR_TOKENIZER_TOKEN_TOO_LONG: return "トークン長が上限を超えています";
    case DIAG_ERR_TOKENIZER_UNTERMINATED: return "リテラルまたはコメントが閉じられていません";
    case DIAG_ERR_TOKENIZER_INVALID_NUMBER: return "数値リテラルが不正です";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE: return "エスケープシーケンスが不正です";
    case DIAG_ERR_TOKENIZER_EXPECTED_TOKEN: return "トークンが必要です";
    case DIAG_ERR_TOKENIZER_EXPECTED_INTEGER: return "必要な整数がありません";
    case DIAG_ERR_TOKENIZER_INVALID_CHAR_LITERAL: return "文字リテラルが不正です";
    case DIAG_ERR_TOKENIZER_UNTERMINATED_COMMENT: return "コメントが閉じられていません";
    case DIAG_ERR_TOKENIZER_UNTERMINATED_LITERAL: return "文字列または文字リテラルが閉じられていません";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_HEX: return "16進エスケープが不正です";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_UCN: return "UCNエスケープが不正です";
    case DIAG_ERR_TOKENIZER_INVALID_ESCAPE_GENERAL: return "不正なエスケープです";
    case DIAG_ERR_TOKENIZER_TOKEN_SIZE_WITH_NAME: return "%s が大きすぎます";
    case DIAG_ERR_TOKENIZER_INT_LITERAL_TOO_LARGE: return "整数リテラルが大きすぎます";
    case DIAG_ERR_TOKENIZER_INT_SUFFIX_INVALID: return "整数サフィックスが不正です";
    case DIAG_ERR_TOKENIZER_INT_LITERAL_INVALID: return "整数リテラルが不正です";
    case DIAG_ERR_TOKENIZER_NUM_LITERAL_INVALID: return "数値リテラルが不正です";
    case DIAG_ERR_TOKENIZER_HEX_FLOAT_LITERAL_INVALID: return "16進浮動小数点リテラルが不正です";
    case DIAG_ERR_TOKENIZER_FLOAT_SUFFIX_INVALID: return "浮動小数点サフィックスが不正です";
    case DIAG_ERR_TOKENIZER_BIN_LITERAL_STRICT_UNSUPPORTED: return "2進数リテラルは strict C11 では未対応です";
    case DIAG_ERR_TOKENIZER_BIN_LITERAL_INVALID: return "2進数リテラルが不正です";
    case DIAG_ERR_TOKENIZER_OCT_LITERAL_INVALID: return "8進数リテラルが不正です";
    case DIAG_ERR_TOKENIZER_CHAR_LITERAL_EMPTY: return "空の文字リテラルは使えません";
    case DIAG_ERR_TOKENIZER_CHAR_LITERAL_INVALID: return "文字リテラルが不正です";
    case DIAG_ERR_TOKENIZER_STRING_LITERAL_UNTERMINATED: return "文字列リテラルが閉じられていません";
    case DIAG_ERR_TOKENIZER_IDENT_UCN_INVALID: return "識別子内のUCNが不正です";
    case DIAG_ERR_TOKENIZER_TOKENIZE_FAILED: return "トークナイズできません";
    case DIAG_ERR_TOKENIZER_CHAR_LITERAL_UNTERMINATED: return "文字リテラルが閉じられていません";
    case DIAG_ERR_PARSER_GENERIC: return "構文解析エラーです";
    case DIAG_ERR_PARSER_EXPECTED_TOKEN: return "トークンが必要です";
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
    case DIAG_ERR_PARSER_VARIABLE_NAME_REQUIRED: return "変数名が必要です";
    case DIAG_ERR_PARSER_ALIGNAS_LPAREN_REQUIRED: return "_Alignas の後には '(' が必要です";
    case DIAG_ERR_PARSER_ATOMIC_TYPE_NAME_REQUIRED: return "_Atomic(...) の中には型名が必要です";
    case DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED: return "配列サイズは正の整数である必要があります";
    case DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN: return "不完全型のメンバは定義できません";
    case DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED: return "メンバ型が必要です";
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
    case DIAG_ERR_PARSER_PRIMARY_NUMBER_EXPECTED: return "数値が必要です";
    case DIAG_ERR_PARSER_ENUM_CONST_UNDEFINED: return "未定義の列挙子 '%.*s' です";
    case DIAG_ERR_PARSER_GOTO_LABEL_UNDEFINED: return "未定義ラベル '%.*s' への goto です";
    case DIAG_ERR_PARSER_MISSING_CLOSING_PAREN: return "対応する閉じ括弧がありません";
    case DIAG_ERR_PARSER_FUNCTION_DEF_EXPECTED: return "関数定義が必要です";
    case DIAG_ERR_PARSER_NONNEG_CONSTEXPR_REQUIRED: return "%sには整数定数式が必要です";
    case DIAG_ERR_PARSER_NONNEG_VALUE_REQUIRED: return "%sは0以上である必要があります";
    case DIAG_ERR_PARSER_COMPLEX_IMAGINARY_CAST_REQUIRES_FLOAT: return "_Complex/_Imaginary cast は浮動小数型のみ対応です";
    case DIAG_ERR_PARSER_COMPLEX_IMAGINARY_TYPE_REQUIRES_FLOAT: return "_Complex/_Imaginary は浮動小数型にのみ指定できます";
    case DIAG_ERR_PARSER_RETURN_VALUE_REQUIRED_NONVOID: return "void 以外の関数では return に式が必要です";
    case DIAG_ERR_PARSER_RETURN_VALUE_FORBIDDEN_VOID: return "void 関数では return に式を指定できません";
    case DIAG_ERR_PARSER_ARROW_LHS_REQUIRES_STRUCT_PTR: return "'->' の左辺は構造体/共用体ポインタである必要があります";
    case DIAG_ERR_PARSER_DOT_LHS_REQUIRES_STRUCT: return "'.' の左辺は構造体/共用体である必要があります";
    case DIAG_ERR_PARSER_STRING_PREFIX_MISMATCH: return "異なる接頭辞の文字列リテラルは連結できません";
    case DIAG_ERR_PARSER_VARIADIC_NOT_LAST: return "'...' は可変長引数リストの末尾にのみ指定できます";
    case DIAG_ERR_PARSER_SWITCH_DUPLICATE_CASE: return "識別子が重複しています (switch case): %lld";
    case DIAG_ERR_PARSER_SWITCH_DUPLICATE_DEFAULT: return "識別子が重複しています (switch default)";
    case DIAG_ERR_PARSER_LVALUE_REQUIRED: return "%s の対象は左辺値である必要があります";
    case DIAG_ERR_PARSER_INTEGER_SCALAR_REQUIRED: return "%s の対象は整数スカラーである必要があります";
    case DIAG_ERR_PARSER_RULE_DETAIL: return "[%s] %s";
    case DIAG_ERR_PARSER_MISSING_ITEM: return "必要な項目がありません: %s";
    case DIAG_ERR_PARSER_UNDEFINED_WITH_KIND: return "未定義の識別子です (%s): '%.*s'";
    case DIAG_ERR_PARSER_DUPLICATE_WITH_KIND: return "識別子が重複しています (%s): '%.*s'";
    case DIAG_ERR_PARSER_ONLY_IN_SCOPE: return "この文脈では使用できません: %s / %s";
    case DIAG_ERR_PARSER_DIAG_FORMAT_FAILED: return "診断メッセージの生成に失敗しました";
    case DIAG_ERR_PARSER_DYNARRAY_INVALID_SIZE: return "配列サイズが不正です";
    case DIAG_ERR_PARSER_DYNARRAY_TOO_LARGE: return "配列サイズが大きすぎます";
    case DIAG_ERR_PARSER_CAST_STRUCT_SCALAR_POINTER_DISABLED: return "struct への scalar/pointer cast は設定で無効です";
    case DIAG_ERR_PARSER_CAST_UNION_SCALAR_POINTER_DISABLED: return "union への scalar/pointer cast は設定で無効です";
    case DIAG_ERR_PARSER_CAST_NONSCALAR_TYPE_MISMATCH: return "%s 値へのキャストは未対応です（型不整合）";
    case DIAG_ERR_PARSER_UNION_ARRAY_MEMBER_NONBRACE_UNSUPPORTED: return "共用体の配列メンバ非波括弧初期化は設定で無効です";
    case DIAG_ERR_PARSER_NESTED_DESIG_NOT_ARRAY: return "入れ子designatorの対象が配列メンバではありません";
    case DIAG_ERR_PARSER_CONST_ASSIGNMENT: return "const修飾された変数への代入はできません";
    case DIAG_ERR_PARSER_CONST_QUAL_DISCARD: return "const修飾されたポインタからconst無しポインタへの暗黙変換はできません";
    case DIAG_ERR_PARSER_VLA_PARAM_DIM_NOT_PRECEDING_PARAM: return "VLA パラメータの dim '%.*s' は同関数の先行パラメータでなければなりません";
    case DIAG_ERR_PARSER_EXPR_NEST_TOO_DEEP: return "式ネストが深すぎます（上限 %d）";
    case DIAG_ERR_PARSER_PAREN_NEST_TOO_DEEP: return "括弧ネストが深すぎます（上限 %d）";
    case DIAG_ERR_PARSER_DECLARATOR_LIST_TOO_LONG: return "宣言子列が多すぎます（上限 %d）";
    case DIAG_ERR_PARSER_MEMBER_DESIGNATOR_INVALID: return "メンバ指定初期化子が不正です";
    case DIAG_ERR_PARSER_MEMBER_DESIGNATOR_NOT_FOUND: return "メンバ指定初期化子のメンバが見つかりません";
    case DIAG_ERR_PARSER_ARRAY_DESIGNATOR_INDEX_INVALID: return "配列指定初期化子の添字が不正です";
    case DIAG_ERR_PARSER_INITIALIZER_ELEMENT_LIMIT_EXCEEDED: return "初期化子要素数が多すぎます（上限 %d）";
    case DIAG_ERR_PARSER_VOID_OBJECT_FORBIDDEN: return "void 型のオブジェクトは宣言できません: '%.*s'";
    case DIAG_ERR_PARSER_IMPLICIT_INT_FORBIDDEN: return "型指定子のない宣言はC11では使用できません";
    case DIAG_ERR_PARSER_CONTINUATION_ENTRY_TYPE: return "continuation entry は int(void) 型である必要があります";
    case DIAG_ERR_PARSER_CONTINUATION_FRAME_CONDITION_TYPE: return "continuation のフレーム条件は int(void) 型である必要があります";
    case DIAG_ERR_PARSER_CONTINUATION_GOTO_LABEL_ACROSS_FRAMES: return "continuation entry ではフレームをまたぐ goto またはラベルを使用できません";
    case DIAG_ERR_PARSER_CONTINUATION_VLA_ACROSS_FRAMES: return "continuation entry ではフレームをまたぐ VLA を使用できません";
    case DIAG_ERR_PARSER_CONTINUATION_ALLOCA_ACROSS_FRAMES: return "continuation entry ではフレームをまたぐ alloca を使用できません";
    case DIAG_ERR_PARSER_CONTINUATION_FRAME_LOOP_REQUIRED: return "continuation entry には while(frame_condition()) の直接ループが1つ必要です";
    case DIAG_ERR_PARSER_CONTINUATION_FRAME_CONDITION_CALL_COUNT: return "フレーム条件はcontinuation entry内の直接while条件として1回だけ使用し、ほかの場所からの呼び出しは削除してください";
    case DIAG_ERR_PARSER_UNSUPPORTED_GNU_EXTENSION: return "GNU 拡張は使用できません: %s";
    case DIAG_ERR_PARSER_ASSIGN_FUNCTION_TARGET: return "関数識別子に代入することはできません (C11 6.5.16p2)";
    case DIAG_ERR_PARSER_ASSIGN_TARGET_NOT_MODIFIABLE: return "代入先は変更可能な左辺値である必要があります (C11 6.5.16p2)";
    case DIAG_ERR_PARSER_ASSIGN_TYPES_INCOMPATIBLE: return "代入する型に互換性がありません (C11 6.5.16.1)";
    case DIAG_ERR_PARSER_CONDITIONAL_CONDITION_NOT_SCALAR: return "条件演算子の第1オペランドはスカラ型である必要があります (C11 6.5.15p2)";
    case DIAG_ERR_PARSER_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE: return "条件演算子の第2・第3オペランドの型に互換性がありません (C11 6.5.15)";
    case DIAG_ERR_PARSER_CALL_NOT_CALLABLE: return "呼び出す式は関数型または関数へのポインタ型である必要があります (C11 6.5.2.2p1)";
    case DIAG_ERR_PARSER_CALL_ARGUMENT_COUNT_MISMATCH: return "関数呼び出しの引数数が関数プロトタイプと一致しません (C11 6.5.2.2)";
    case DIAG_ERR_CODEGEN_GENERIC: return "コード生成エラーです";
    case DIAG_ERR_CODEGEN_OUTPUT_FAILED: return "コード生成出力に失敗しました";
    case DIAG_ERR_CODEGEN_INVALID_LVALUE: return "代入の左辺値が不正です";
    case DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW: return "制御フローの使用位置が不正です";
    case DIAG_ERR_CODEGEN_BREAK_OUTSIDE_LOOP_OR_SWITCH: return "break はループまたはswitch内でのみ使用できます";
    case DIAG_ERR_CODEGEN_CONTINUE_OUTSIDE_LOOP: return "continue はループ内でのみ使用できます";
    case DIAG_ERR_CODEGEN_GOTO_LABEL_UNDEFINED: return "未定義ラベル '%.*s' への goto です";
    case DIAG_ERR_CODEGEN_IR_BUILD_EMIT_FAILED: return "IR の構築または出力に失敗しました";
    case DIAG_ERR_CODEGEN_UNSUPPORTED_IR_OP: return "未対応の IR 命令です: %s";
    case DIAG_ERR_CODEGEN_WASM_OBJECT_OPEN_FAILED: return "Wasm オブジェクト出力を開けませんでした: %s";
    case DIAG_ERR_CODEGEN_WASM_OBJECT_ADDRESSABLE_SIZE_EXCEEDED: return "Wasm オブジェクト出力がアドレス可能なサイズを超えました";
    case DIAG_ERR_CODEGEN_WASM_OBJECT_WRITE_FAILED: return "Wasm オブジェクト出力の書き込みに失敗しました";
    case DIAG_ERR_CODEGEN_WASM_OBJECT_OUTPUT_SINK_MISSING: return "Wasm オブジェクト出力先が設定されていません";
  }
  return NULL;
}

const char *diag_warn_message_ja(diag_warn_id_t id) {
  switch (id) {
    case DIAG_WARN_PARSER_IMPLICIT_INT_RETURN: return "戻り値型が省略されています（暗黙の int）";
    case DIAG_WARN_PARSER_UNREACHABLE_CODE: return "到達不能なコードです";
    case DIAG_WARN_PARSER_UNUSED_VARIABLE: return "未使用の変数 '%.*s'";
    case DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE: return "初期化されていない変数 '%.*s' が使用されています";
    case DIAG_WARN_PARSER_MISSING_RETURN: return "関数 '%.*s' は値を返さずに終端します (C11 6.9.1p12)";
    case DIAG_WARN_PARSER_RETURN_STACK_ADDRESS: return "ローカル変数 '%.*s' のアドレスを返しています (dangling pointer になります)";
    case DIAG_WARN_PARSER_ASSIGN_IN_CONDITION: return "'%s' 文の条件に代入式を使っています ('==' のタイプミスの可能性)";
    case DIAG_WARN_PARSER_COMMA_IN_CONDITION: return "'%s' 文の条件にカンマ演算子を使っています ('&&' のタイプミスの可能性)";
    case DIAG_WARN_PARSER_EMPTY_BODY: return "'if' 文の本体が空です (タイプミスの可能性)";
    case DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING: return "浮動小数点値%sを整数へ変換しています (小数部が切り捨てられます)";
    case DIAG_WARN_PARSER_CONSTANT_OVERFLOW: return "整数リテラル %lld は %d バイト型に収まりません (値が切り詰められます)";
    case DIAG_WARN_PARSER_SELF_ASSIGN: return "変数を自身に代入しています (タイプミスの可能性)";
    case DIAG_WARN_PARSER_SELF_COMPARE: return "自己比較 (両辺が同じ値): '%s'";
    case DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE: return "シフト量 %lld が型の幅 (%d ビット) を超えています (C11 6.5.7p3 未定義動作): %s";
    case DIAG_WARN_PARSER_DIVIDE_BY_ZERO: return "'%s' の右辺が 0 です (C11 6.5.5p5 未定義動作)";
    case DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL: return "関数 '%.*s' は宣言されていません (C99/C11 では暗黙の関数宣言は使用できません)";
    case DIAG_WARN_PARSER_SWITCH_FALLTHROUGH: return "switch の case が break / return 等で終端せず、次の case に到達します";
    case DIAG_WARN_PARSER_SIGN_COMPARE: return "'%s' で符号付きと符号なしの整数を比較しています (負値が大きな正の値として扱われる可能性)";
    case DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO: return "符号なし整数を使う '%s' の比較結果は常に %d です (符号なし整数は 0 以上です)";
    case DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS: return "'%s' の両辺が同じ式です (常に同じ結果、タイプミスの可能性)";
    case DIAG_WARN_PARSER_LOGICAL_NOT_PARENTHESES: return "'%s' の左辺が単項 '!' で、'!' の優先順位が '%s' より高いため '(!x) %s y' と解釈されます ('!(x %s y)' を意図していませんか)";
    case DIAG_WARN_PARSER_POINTER_INTEGER_COMPARE: return "ポインタを非ゼロ整数定数 %lld と '%s' で比較しています (C11 6.5.16.1)";
    case DIAG_WARN_PARSER_INTEGER_OVERFLOW: return "整数定数式 %lld %s %lld = %lld は int の範囲を超えています (C11 6.5p5 未定義動作)";
  }
  return NULL;
}

/**
 * @brief テキストIDに対応する日本語テキストを返す。
 * @param id テキストID。
 * @return 日本語テキスト。未定義時は NULL。
 */
const char *diag_text_ja(diag_text_id_t id) {
  switch (id) {
    case DIAG_TEXT_WARNING: return "警告";
    case DIAG_TEXT_C11_AUDIT_PREFIX: return "c11監査";
    case DIAG_TEXT_C11_AUDIT_BINARY_LITERAL_EXTENSION: return "2進数リテラル拡張";
    case DIAG_TEXT_TAG_NAME: return "タグ名";
    case DIAG_TEXT_MEMBER_NAME: return "メンバ名";
    case DIAG_TEXT_ENUMERATOR_NAME: return "列挙子名";
    case DIAG_TEXT_PARAMETER: return "仮引数";
    case DIAG_TEXT_TAG_TYPE_SUFFIX: return "のタグ型";
    case DIAG_TEXT_TAG_TYPE: return "タグ型";
    case DIAG_TEXT_BREAK: return "break";
    case DIAG_TEXT_CONTINUE: return "continue";
    case DIAG_TEXT_LOOP_OR_SWITCH_SCOPE: return "ループまたはswitch内";
    case DIAG_TEXT_LOOP_SCOPE: return "ループ内";
    case DIAG_TEXT_GOTO_LABEL_AFTER: return "goto の後のラベル名";
    case DIAG_TEXT_SWITCH_SCOPE: return "switch 内";
    case DIAG_TEXT_CASE: return "case";
    case DIAG_TEXT_DEFAULT: return "default";
    case DIAG_TEXT_LABEL: return "ラベル";
    case DIAG_TEXT_ARRAY_DESIGNATOR_INDEX: return "配列designator添字";
    case DIAG_TEXT_ARRAY_SIZE: return "配列サイズ";
    case DIAG_TEXT_WHILE: return "'while'";
  }
  return NULL;
}

const char *diag_ui_text_ja(diag_ui_text_id_t id) {
  switch (id) {
    case DIAG_UI_TEXT_UNKNOWN_TEXT: return "unknown.text";
    case DIAG_UI_TEXT_ACTUAL_TOKEN_LABEL: return "実際のトークン";
    case DIAG_UI_TEXT_ACTUAL_TOKEN_KIND_FMT: return " (実際のトークン種別: %d)";
  }
  return NULL;
}

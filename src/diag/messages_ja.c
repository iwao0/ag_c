#include "messages.h"
#include <stddef.h>

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
    case DIAG_ERR_CODEGEN_GENERIC: return "コード生成エラーです";
    case DIAG_ERR_CODEGEN_OUTPUT_FAILED: return "コード生成出力に失敗しました";
    case DIAG_ERR_CODEGEN_INVALID_LVALUE: return "代入の左辺値が不正です";
    case DIAG_ERR_CODEGEN_INVALID_CONTROL_FLOW: return "制御フローの使用位置が不正です";
  }
  return NULL;
}

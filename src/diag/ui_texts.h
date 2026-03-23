#ifndef DIAG_UI_TEXTS_H
#define DIAG_UI_TEXTS_H

typedef enum {
  DIAG_UI_TEXT_UNKNOWN_TEXT = 1,
  DIAG_UI_TEXT_ACTUAL_TOKEN_LABEL = 2,
  DIAG_UI_TEXT_ACTUAL_TOKEN_KIND_FMT = 3,
} diag_ui_text_id_t;

/**
 * @brief UI向け内部テキストをロケールに応じて取得する。
 * @param id テキストID。
 * @param locale ロケール（"ja" / "en"）。
 * @return 対応文字列。未定義時は英語既定文言。
 */
const char *diag_ui_text_for(diag_ui_text_id_t id, const char *locale);
const char *diag_ui_text_ja(diag_ui_text_id_t id);
const char *diag_ui_text_en(diag_ui_text_id_t id);

#endif

#ifndef DIAG_UI_TEXTS_H
#define DIAG_UI_TEXTS_H

typedef enum {
  DIAG_UI_TEXT_UNKNOWN_TEXT = 1,
  DIAG_UI_TEXT_ACTUAL_TOKEN_LABEL = 2,
  DIAG_UI_TEXT_ACTUAL_TOKEN_KIND_FMT = 3,
} diag_ui_text_id_t;

/**
 * @brief Return internal UI text for the requested locale.
 * @param id Text ID.
 * @param locale Locale ("ja" / "en").
 * @return Matching text, or the default English text when undefined.
 */
const char *diag_ui_text_for(diag_ui_text_id_t id, const char *locale);
const char *diag_ui_text_ja(diag_ui_text_id_t id);
const char *diag_ui_text_en(diag_ui_text_id_t id);

#endif

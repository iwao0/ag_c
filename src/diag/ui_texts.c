#include "ui_texts.h"
#include <string.h>

const char *diag_ui_text_for(diag_ui_text_id_t id, const char *locale) {
  const char *msg = NULL;
#if defined(DIAG_LANG_ALL)
  if (locale && strcmp(locale, "en") == 0) {
    msg = diag_ui_text_en(id);
    if (msg) return msg;
    msg = diag_ui_text_ja(id);
    if (msg) return msg;
  } else {
    msg = diag_ui_text_ja(id);
    if (msg) return msg;
    msg = diag_ui_text_en(id);
    if (msg) return msg;
  }
#elif defined(DIAG_LANG_EN)
  (void)locale;
  msg = diag_ui_text_en(id);
  if (msg) return msg;
#else
  (void)locale;
  msg = diag_ui_text_ja(id);
  if (msg) return msg;
#endif
  return "unknown.text";
}

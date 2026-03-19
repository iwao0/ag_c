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
    {DIAG_ERR_PARSER_GENERIC, "E3000", "parser.generic"},
    {DIAG_ERR_CODEGEN_GENERIC, "E4000", "codegen.generic"},
};

static const diag_entry_t *find_entry(diag_error_id_t id) {
  for (size_t i = 0; i < sizeof(k_diag_entries) / sizeof(k_diag_entries[0]); i++) {
    if (k_diag_entries[i].id == id) return &k_diag_entries[i];
  }
  return NULL;
}

const char *diag_error_code(diag_error_id_t id) {
  const diag_entry_t *entry = find_entry(id);
  return entry ? entry->code : "E9999";
}

const char *diag_error_key(diag_error_id_t id) {
  const diag_entry_t *entry = find_entry(id);
  return entry ? entry->key : "unknown.error";
}

#include "warning_catalog.h"
#include <stddef.h>

typedef struct {
  diag_warn_id_t id;
  const char *code;
  const char *key;
} diag_warn_entry_t;

static const diag_warn_entry_t k_warn_entries[] = {
    {DIAG_WARN_PARSER_IMPLICIT_INT_RETURN, "W3001", "parser.implicit_int_return"},
    {DIAG_WARN_PARSER_UNREACHABLE_CODE, "W3002", "parser.unreachable_code"},
    {DIAG_WARN_PARSER_UNUSED_VARIABLE, "W3003", "parser.unused_variable"},
};

static const diag_warn_entry_t *find_warn_entry(diag_warn_id_t id) {
  for (size_t i = 0; i < sizeof(k_warn_entries) / sizeof(k_warn_entries[0]); i++) {
    if (k_warn_entries[i].id == id) return &k_warn_entries[i];
  }
  return NULL;
}

const char *diag_warn_code(diag_warn_id_t id) {
  const diag_warn_entry_t *entry = find_warn_entry(id);
  return entry ? entry->code : "W9999";
}

const char *diag_warn_key(diag_warn_id_t id) {
  const diag_warn_entry_t *entry = find_warn_entry(id);
  return entry ? entry->key : "unknown.warning";
}

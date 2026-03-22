#ifndef DIAG_WARNING_CATALOG_H
#define DIAG_WARNING_CATALOG_H

typedef enum {
  DIAG_WARN_PARSER_IMPLICIT_INT_RETURN = 3001,
  DIAG_WARN_PARSER_UNREACHABLE_CODE = 3002,
  DIAG_WARN_PARSER_UNUSED_VARIABLE = 3003,
} diag_warn_id_t;

const char *diag_warn_code(diag_warn_id_t id);
const char *diag_warn_key(diag_warn_id_t id);

#endif

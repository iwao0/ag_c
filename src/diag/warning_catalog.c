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
    {DIAG_WARN_PARSER_UNINITIALIZED_VARIABLE, "W3004", "parser.uninitialized_variable"},
    {DIAG_WARN_PARSER_MISSING_RETURN, "W3005", "parser.missing_return"},
    {DIAG_WARN_PARSER_RETURN_STACK_ADDRESS, "W3006", "parser.return_stack_address"},
    {DIAG_WARN_PARSER_ASSIGN_IN_CONDITION, "W3007", "parser.assign_in_condition"},
    {DIAG_WARN_PARSER_COMMA_IN_CONDITION, "W3008", "parser.comma_in_condition"},
    {DIAG_WARN_PARSER_EMPTY_BODY, "W3009", "parser.empty_body"},
    {DIAG_WARN_PARSER_FLOAT_TO_INT_NARROWING, "W3010", "parser.float_to_int_narrowing"},
    {DIAG_WARN_PARSER_CONSTANT_OVERFLOW, "W3011", "parser.constant_overflow"},
    {DIAG_WARN_PARSER_SELF_ASSIGN, "W3012", "parser.self_assign"},
    {DIAG_WARN_PARSER_SELF_COMPARE, "W3013", "parser.self_compare"},
    {DIAG_WARN_PARSER_SHIFT_OUT_OF_RANGE, "W3014", "parser.shift_out_of_range"},
    {DIAG_WARN_PARSER_DIVIDE_BY_ZERO, "W3015", "parser.divide_by_zero"},
    {DIAG_WARN_PARSER_IMPLICIT_FUNCTION_DECL, "W3016", "parser.implicit_function_declaration"},
    {DIAG_WARN_PARSER_SWITCH_FALLTHROUGH, "W3017", "parser.switch_fallthrough"},
    {DIAG_WARN_PARSER_SIGN_COMPARE, "W3018", "parser.sign_compare"},
    {DIAG_WARN_PARSER_TAUTOLOGICAL_UNSIGNED_ZERO, "W3019", "parser.tautological_unsigned_zero_compare"},
    {DIAG_WARN_PARSER_IDENTICAL_LOGICAL_OPERANDS, "W3020", "parser.identical_logical_operands"},
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

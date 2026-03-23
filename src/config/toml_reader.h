#ifndef CONFIG_TOML_READER_H
#define CONFIG_TOML_READER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct config_values_t {
  char locale[16];
  bool strict_c11;
  bool enable_trigraphs;
  bool enable_binary_literals;
  bool enable_c11_audit_extensions;
  bool enable_size_compatible_nonscalar_cast;
  bool enable_struct_scalar_pointer_cast;
  bool enable_union_scalar_pointer_cast;
  bool enable_union_array_member_nonbrace_init;
} config_values_t;

typedef enum config_toml_error_kind_t {
  CONFIG_TOML_ERR_NONE = 0,
  CONFIG_TOML_ERR_UNTERMINATED_STRING,
  CONFIG_TOML_ERR_STRING_MUST_BE_QUOTED,
  CONFIG_TOML_ERR_STRING_TOO_LONG,
  CONFIG_TOML_ERR_INVALID_ESCAPE,
  CONFIG_TOML_ERR_INVALID_U_ESCAPE,
  CONFIG_TOML_ERR_INVALID_U8_ESCAPE,
  CONFIG_TOML_ERR_UNSUPPORTED_ESCAPE,
  CONFIG_TOML_ERR_DUPLICATE_KEY,
  CONFIG_TOML_ERR_MALFORMED_SECTION_HEADER,
  CONFIG_TOML_ERR_UNKNOWN_SECTION,
  CONFIG_TOML_ERR_KEY_VALUE_BEFORE_SECTION,
  CONFIG_TOML_ERR_UNKNOWN_KEY,
  CONFIG_TOML_ERR_INVALID_VALUE_FOR_KEY,
  CONFIG_TOML_ERR_UNSUPPORTED_LOCALE,
  CONFIG_TOML_ERR_BOOL_REQUIRED,
  CONFIG_TOML_ERR_INTERNAL_PARSER_STATE,
  CONFIG_TOML_ERR_EXPECTED_KEY_VALUE,
  CONFIG_TOML_ERR_EMPTY_KEY,
  CONFIG_TOML_ERR_EMPTY_VALUE_FOR_KEY,
} config_toml_error_kind_t;

typedef struct config_toml_error_t {
  config_toml_error_kind_t kind;
  int line_no;
  char arg1[96];
  char arg2[96];
} config_toml_error_t;

void config_values_init_defaults(config_values_t *cfg);
bool config_toml_read(const char *source_path, config_values_t *cfg, config_toml_error_t *err);

#endif

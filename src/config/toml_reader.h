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

void config_values_init_defaults(config_values_t *cfg);
bool config_toml_read(const char *source_path, config_values_t *cfg, char *err, size_t err_cap);

#endif

#include "config.h"
#include "toml_reader.h"
#include "../diag/diag.h"
#include "../parser/config_runtime.h"
#include "../tokenizer/tokenizer.h"
#include <stdio.h>

static void apply_config_values(const config_values_t *cfg) {
  diag_set_locale(cfg->locale);
  tk_set_strict_c11_mode(cfg->strict_c11);
  tk_set_enable_trigraphs(cfg->enable_trigraphs);
  tk_set_enable_binary_literals(cfg->enable_binary_literals);
  tk_set_enable_c11_audit_extensions(cfg->enable_c11_audit_extensions);
  ps_set_enable_size_compatible_nonscalar_cast(cfg->enable_size_compatible_nonscalar_cast);
  ps_set_enable_struct_scalar_pointer_cast(cfg->enable_struct_scalar_pointer_cast);
  ps_set_enable_union_scalar_pointer_cast(cfg->enable_union_scalar_pointer_cast);
  ps_set_enable_union_array_member_nonbrace_init(cfg->enable_union_array_member_nonbrace_init);
}

void load_config_toml(const char *source_path) {
  config_values_t cfg;
  config_values_init_defaults(&cfg);
  apply_config_values(&cfg);

  char err[256] = {0};
  if (config_toml_read(source_path, &cfg, err, sizeof(err))) {
    apply_config_values(&cfg);
    return;
  }

  fprintf(stderr, "config.toml parse error: %s\n", err);
  fprintf(stderr, "config.toml load aborted. fallback to defaults.\n");
  config_values_init_defaults(&cfg);
  apply_config_values(&cfg);
}

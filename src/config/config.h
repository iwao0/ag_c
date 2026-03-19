#ifndef CONFIG_H
#define CONFIG_H

// Load settings from config.toml.
// Prefer "<source_path directory>/config.toml"; fallback to current working directory.
// Missing file is treated as "use defaults".
void load_config_toml(const char *source_path);

#endif

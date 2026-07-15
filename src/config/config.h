#ifndef CONFIG_H
#define CONFIG_H

typedef struct ag_compilation_session_t ag_compilation_session_t;

// Load settings from config.toml.
// Prefer "<source_path directory>/config.toml"; fallback to current working directory.
// Missing file is treated as "use defaults".
void load_config_toml_in_session(
    ag_compilation_session_t *session, const char *source_path);

#endif

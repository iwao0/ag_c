#ifndef CONFIG_H
#define CONFIG_H

// Load settings from config.toml in the current working directory.
// Missing file is treated as "use defaults".
void load_config_toml(void);

#endif


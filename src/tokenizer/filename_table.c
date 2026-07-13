#include "token.h"
#include <stdlib.h>
#include <string.h>

#define FILENAME_TABLE_CAP 256
static const char *filename_table[FILENAME_TABLE_CAP];
static uint16_t filename_table_count = 0;

uint16_t tk_filename_intern(const char *name) {
  if (!name) return 0;
  for (uint16_t i = 0; i < filename_table_count; i++) {
    if (filename_table[i] == name || !strcmp(filename_table[i], name)) return i;
  }
  if (filename_table_count >= FILENAME_TABLE_CAP) return 0;
  size_t len = strlen(name);
  char *copy = malloc(len + 1);
  if (!copy) return 0;
  memcpy(copy, name, len + 1);
  filename_table[filename_table_count] = copy;
  return filename_table_count++;
}

const char *tk_filename_lookup(uint16_t id) {
  if (id >= filename_table_count) return NULL;
  return filename_table[id];
}

void tk_filename_reset_translation_unit(void) {
  for (uint16_t i = 0; i < filename_table_count; i++) {
    free((void *)filename_table[i]);
    filename_table[i] = NULL;
  }
  filename_table_count = 0;
}

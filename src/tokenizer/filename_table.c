#include "token.h"
#include <string.h>

#define FILENAME_TABLE_CAP 256
static char *filename_table[FILENAME_TABLE_CAP];
static uint16_t filename_table_count = 0;

uint16_t tk_filename_intern(char *name) {
  if (!name) return 0;
  for (uint16_t i = 0; i < filename_table_count; i++) {
    if (filename_table[i] == name || !strcmp(filename_table[i], name)) return i;
  }
  if (filename_table_count >= FILENAME_TABLE_CAP) return 0;
  filename_table[filename_table_count] = name;
  return filename_table_count++;
}

char *tk_filename_lookup(uint16_t id) {
  if (id >= filename_table_count) return NULL;
  return filename_table[id];
}

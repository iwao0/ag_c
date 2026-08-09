#ifndef AG_LANGUAGE_DOCUMENTATION_H
#define AG_LANGUAGE_DOCUMENTATION_H

#include <stddef.h>

typedef enum {
  AG_LANGUAGE_DOCUMENTATION_OK = 0,
  AG_LANGUAGE_DOCUMENTATION_OUT_OF_MEMORY,
  AG_LANGUAGE_DOCUMENTATION_LIMIT,
} ag_language_documentation_status_t;

typedef enum {
  AG_LANGUAGE_DOCUMENTATION_BLOCK = 0,
  AG_LANGUAGE_DOCUMENTATION_LINE,
} ag_language_documentation_style_t;

typedef struct {
  const char *source_name;
  const char *source;
  size_t source_length;
  size_t comment_start;
  size_t comment_end;
  size_t declaration_start;
  size_t declaration_end;
  ag_language_documentation_style_t style;
} ag_language_documentation_entry_t;

typedef struct {
  ag_language_documentation_entry_t *entries;
  size_t count;
  size_t capacity;
} ag_language_documentation_index_t;

ag_language_documentation_status_t ag_language_documentation_index_add_source(
    ag_language_documentation_index_t *index,
    const char *source_name, const char *source, size_t source_length,
    size_t max_entries);
const ag_language_documentation_entry_t *ag_language_documentation_find(
    const ag_language_documentation_index_t *index,
    const char *source_name, size_t declaration_offset);
size_t ag_language_documentation_normalized_length(
    const ag_language_documentation_entry_t *entry);
int ag_language_documentation_normalize(
    const ag_language_documentation_entry_t *entry,
    char *output, size_t output_size);
void ag_language_documentation_index_dispose(
    ag_language_documentation_index_t *index);

#endif

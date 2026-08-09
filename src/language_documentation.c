#include "language_documentation.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  size_t start;
  size_t end;
} documentation_line_t;

static int is_horizontal_space(char c) {
  return c == ' ' || c == '\t';
}

static int newline_is_escaped(
    const char *source, size_t newline_offset) {
  if (!source || newline_offset == 0) return 0;
  size_t previous = newline_offset - 1;
  if (source[previous] == '\r' && previous > 0) previous--;
  return source[previous] == '\\';
}

static size_t declaration_end_after(
    const char *source, size_t length, size_t start) {
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 0;
  int preprocessor_line = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  int top_level_paren_closed = 0;
  int top_level_assignment = 0;
  for (size_t i = start; i < length; i++) {
    char c = source[i];
    char next = i + 1 < length ? source[i + 1] : 0;
    if (line_comment) {
      if (c == '\n') {
        line_comment = 0;
        at_line_start = 1;
      }
      continue;
    }
    if (block_comment) {
      if (c == '*' && next == '/') {
        block_comment = 0;
        i++;
      }
      continue;
    }
    if (quote) {
      if (escaped) escaped = 0;
      else if (c == '\\') escaped = 1;
      else if (c == quote) quote = 0;
      continue;
    }
    if (preprocessor_line) {
      if (c == '\n' && !newline_is_escaped(source, i)) {
        preprocessor_line = 0;
        at_line_start = 1;
      }
      continue;
    }
    if (c == '/' && next == '/') {
      line_comment = 1;
      i++;
      continue;
    }
    if (c == '/' && next == '*') {
      block_comment = 1;
      i++;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      at_line_start = 0;
      continue;
    }
    if (c == '\n') {
      at_line_start = 1;
      continue;
    }
    if (at_line_start &&
        (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') {
      preprocessor_line = 1;
      at_line_start = 0;
      continue;
    }
    at_line_start = 0;
    if (c == '(') {
      paren_depth++;
    } else if (c == ')' && paren_depth > 0) {
      paren_depth--;
      if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
        top_level_paren_closed = 1;
    } else if (c == '[') {
      bracket_depth++;
    } else if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
    } else if (c == '=' && paren_depth == 0 && bracket_depth == 0 &&
               brace_depth == 0) {
      top_level_assignment = 1;
    } else if (c == '{' && paren_depth == 0 && bracket_depth == 0 &&
               brace_depth == 0 && top_level_paren_closed &&
               !top_level_assignment) {
      return i + 1;
    } else if (c == '{') {
      brace_depth++;
    } else if (c == '}' && brace_depth > 0) {
      brace_depth--;
    } else if (c == ';' && paren_depth == 0 && bracket_depth == 0 &&
               brace_depth == 0) {
      return i + 1;
    }
  }
  return length;
}

static size_t declaration_start_after_comment(
    const char *source, size_t length, size_t comment_end) {
  size_t cursor = comment_end;
  int newline_count = 0;
  while (cursor < length && isspace((unsigned char)source[cursor])) {
    if (source[cursor] == '\n') newline_count++;
    cursor++;
  }
  if (newline_count > 1 || cursor >= length || source[cursor] == '#')
    return (size_t)-1;
  if (cursor + 1 < length && source[cursor] == '/' &&
      (source[cursor + 1] == '/' || source[cursor + 1] == '*'))
    return (size_t)-1;
  return cursor;
}

static ag_language_documentation_status_t append_entry(
    ag_language_documentation_index_t *index,
    const ag_language_documentation_entry_t *entry,
    size_t max_entries) {
  if (index->count >= max_entries)
    return AG_LANGUAGE_DOCUMENTATION_LIMIT;
  if (index->count == index->capacity) {
    size_t next_capacity = index->capacity ? index->capacity * 2 : 16;
    if (next_capacity < index->capacity || next_capacity > max_entries)
      next_capacity = max_entries;
    ag_language_documentation_entry_t *next = realloc(
        index->entries, next_capacity * sizeof(*next));
    if (!next) return AG_LANGUAGE_DOCUMENTATION_OUT_OF_MEMORY;
    index->entries = next;
    index->capacity = next_capacity;
  }
  index->entries[index->count++] = *entry;
  return AG_LANGUAGE_DOCUMENTATION_OK;
}

static ag_language_documentation_status_t record_comment(
    ag_language_documentation_index_t *index,
    const char *source_name, const char *source, size_t source_length,
    size_t comment_start, size_t comment_end,
    ag_language_documentation_style_t style, size_t max_entries) {
  size_t declaration_start = declaration_start_after_comment(
      source, source_length, comment_end);
  if (declaration_start == (size_t)-1)
    return AG_LANGUAGE_DOCUMENTATION_OK;
  size_t declaration_end = declaration_end_after(
      source, source_length, declaration_start);
  if (declaration_end <= declaration_start)
    return AG_LANGUAGE_DOCUMENTATION_OK;
  return append_entry(
      index,
      &(ag_language_documentation_entry_t){
          .source_name = source_name,
          .source = source,
          .source_length = source_length,
          .comment_start = comment_start,
          .comment_end = comment_end,
          .declaration_start = declaration_start,
          .declaration_end = declaration_end,
          .style = style,
      },
      max_entries);
}

static size_t block_comment_end(
    const char *source, size_t length, size_t start) {
  for (size_t i = start + 2; i + 1 < length; i++)
    if (source[i] == '*' && source[i + 1] == '/') return i + 2;
  return length;
}

static size_t line_documentation_end(
    const char *source, size_t length, size_t start) {
  size_t line_start = start;
  size_t last_end = start;
  for (;;) {
    size_t line_end = line_start;
    while (line_end < length && source[line_end] != '\n') line_end++;
    last_end = line_end;
    if (last_end > line_start && source[last_end - 1] == '\r') last_end--;
    if (line_end >= length) return last_end;
    size_t next = line_end + 1;
    while (next < length &&
           (source[next] == ' ' || source[next] == '\t' ||
            source[next] == '\r'))
      next++;
    if (next + 2 >= length || source[next] != '/' ||
        source[next + 1] != '/' || source[next + 2] != '/')
      return last_end;
    line_start = next;
  }
}

ag_language_documentation_status_t ag_language_documentation_index_add_source(
    ag_language_documentation_index_t *index,
    const char *source_name, const char *source, size_t source_length,
    size_t max_entries) {
  if (!index || !source_name || !source || max_entries == 0)
    return AG_LANGUAGE_DOCUMENTATION_LIMIT;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 1;
  int preprocessor_line = 0;
  for (size_t i = 0; i < source_length; i++) {
    char c = source[i];
    char next = i + 1 < source_length ? source[i + 1] : 0;
    if (line_comment) {
      if (c == '\n') {
        line_comment = 0;
        at_line_start = 1;
        preprocessor_line = 0;
      }
      continue;
    }
    if (block_comment) {
      if (c == '*' && next == '/') {
        block_comment = 0;
        i++;
      }
      continue;
    }
    if (quote) {
      if (escaped) escaped = 0;
      else if (c == '\\') escaped = 1;
      else if (c == quote) quote = 0;
      continue;
    }
    if (preprocessor_line) {
      if (c == '\n' && !newline_is_escaped(source, i)) {
        preprocessor_line = 0;
        at_line_start = 1;
      }
      continue;
    }
    if (c == '\n') {
      at_line_start = 1;
      continue;
    }
    if (at_line_start &&
        (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') {
      preprocessor_line = 1;
      at_line_start = 0;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      at_line_start = 0;
      continue;
    }
    if (c == '/' && next == '/' && i + 2 < source_length &&
        source[i + 2] == '/' && at_line_start) {
      size_t end = line_documentation_end(source, source_length, i);
      ag_language_documentation_status_t status = record_comment(
          index, source_name, source, source_length, i, end,
          AG_LANGUAGE_DOCUMENTATION_LINE, max_entries);
      if (status != AG_LANGUAGE_DOCUMENTATION_OK) return status;
      i = end > 0 ? end - 1 : end;
      at_line_start = 0;
      continue;
    }
    if (c == '/' && next == '/') {
      line_comment = 1;
      i++;
      at_line_start = 0;
      continue;
    }
    if (c == '/' && next == '*') {
      size_t end = block_comment_end(source, source_length, i);
      if (i + 2 < source_length && source[i + 2] == '*') {
        ag_language_documentation_status_t status = record_comment(
            index, source_name, source, source_length, i, end,
            AG_LANGUAGE_DOCUMENTATION_BLOCK, max_entries);
        if (status != AG_LANGUAGE_DOCUMENTATION_OK) return status;
      }
      i = end > 0 ? end - 1 : end;
      at_line_start = 0;
      continue;
    }
    at_line_start = 0;
  }
  return AG_LANGUAGE_DOCUMENTATION_OK;
}

const ag_language_documentation_entry_t *ag_language_documentation_find(
    const ag_language_documentation_index_t *index,
    const char *source_name, size_t declaration_offset) {
  if (!index || !source_name) return NULL;
  const ag_language_documentation_entry_t *best = NULL;
  for (size_t i = 0; i < index->count; i++) {
    const ag_language_documentation_entry_t *entry = &index->entries[i];
    if (strcmp(entry->source_name, source_name) != 0 ||
        declaration_offset < entry->declaration_start ||
        declaration_offset >= entry->declaration_end)
      continue;
    if (!best || entry->declaration_start > best->declaration_start)
      best = entry;
  }
  return best;
}

static documentation_line_t line_content(
    const ag_language_documentation_entry_t *entry,
    size_t line_start, size_t line_end) {
  const char *source = entry->source;
  if (line_end > line_start && source[line_end - 1] == '\r') line_end--;
  size_t content = line_start;
  if (entry->style == AG_LANGUAGE_DOCUMENTATION_LINE) {
    while (content < line_end && is_horizontal_space(source[content]))
      content++;
    if (content + 2 < line_end && source[content] == '/' &&
        source[content + 1] == '/' && source[content + 2] == '/')
      content += 3;
    if (content < line_end && is_horizontal_space(source[content])) content++;
  } else {
    size_t probe = content;
    while (probe < line_end && is_horizontal_space(source[probe])) probe++;
    if (probe < line_end && source[probe] == '*') {
      content = probe + 1;
      if (content < line_end && is_horizontal_space(source[content]))
        content++;
    }
  }
  while (line_end > content && is_horizontal_space(source[line_end - 1]))
    line_end--;
  return (documentation_line_t){content, line_end};
}

static void documentation_content_bounds(
    const ag_language_documentation_entry_t *entry,
    size_t *start, size_t *end) {
  *start = entry->comment_start;
  *end = entry->comment_end;
  if (entry->style == AG_LANGUAGE_DOCUMENTATION_BLOCK) {
    if (*end >= *start + 5) {
      *start += 3;
      *end -= 2;
    } else {
      *start = *end;
    }
  }
}

static size_t normalized_documentation(
    const ag_language_documentation_entry_t *entry,
    char *output, size_t output_size) {
  if (!entry || !entry->source) return 0;
  size_t content_start = 0;
  size_t content_end = 0;
  documentation_content_bounds(entry, &content_start, &content_end);
  size_t first_nonblank = (size_t)-1;
  size_t last_nonblank = 0;
  size_t common_indent = (size_t)-1;
  size_t line_number = 0;
  for (size_t cursor = content_start; cursor <= content_end; line_number++) {
    size_t line_end = cursor;
    while (line_end < content_end && entry->source[line_end] != '\n')
      line_end++;
    documentation_line_t line = line_content(entry, cursor, line_end);
    size_t indent = 0;
    while (line.start + indent < line.end &&
           is_horizontal_space(entry->source[line.start + indent]))
      indent++;
    if (line.start + indent < line.end) {
      if (first_nonblank == (size_t)-1) first_nonblank = line_number;
      last_nonblank = line_number;
      if (indent < common_indent) common_indent = indent;
    }
    if (line_end >= content_end) break;
    cursor = line_end + 1;
  }
  if (first_nonblank == (size_t)-1) {
    if (output && output_size > 0) output[0] = '\0';
    return 0;
  }
  size_t needed = 0;
  size_t written = 0;
  line_number = 0;
  for (size_t cursor = content_start; cursor <= content_end; line_number++) {
    size_t line_end = cursor;
    while (line_end < content_end && entry->source[line_end] != '\n')
      line_end++;
    if (line_number >= first_nonblank && line_number <= last_nonblank) {
      documentation_line_t line = line_content(entry, cursor, line_end);
      size_t indent = 0;
      while (indent < common_indent && line.start + indent < line.end &&
             is_horizontal_space(entry->source[line.start + indent]))
        indent++;
      line.start += indent;
      size_t line_length = line.end - line.start;
      if (line_number > first_nonblank) {
        if (output && written + 1 < output_size) output[written] = '\n';
        written++;
        needed++;
      }
      if (output && written < output_size) {
        size_t available = output_size - written - 1;
        size_t copy_length = line_length < available ? line_length : available;
        memcpy(output + written, entry->source + line.start, copy_length);
      }
      written += line_length;
      needed += line_length;
    }
    if (line_end >= content_end || line_number >= last_nonblank) break;
    cursor = line_end + 1;
  }
  if (output && output_size > 0) {
    size_t terminator = written < output_size ? written : output_size - 1;
    output[terminator] = '\0';
  }
  return needed;
}

size_t ag_language_documentation_normalized_length(
    const ag_language_documentation_entry_t *entry) {
  return normalized_documentation(entry, NULL, 0);
}

int ag_language_documentation_normalize(
    const ag_language_documentation_entry_t *entry,
    char *output, size_t output_size) {
  size_t needed = normalized_documentation(entry, output, output_size);
  return output && output_size > needed;
}

void ag_language_documentation_index_dispose(
    ag_language_documentation_index_t *index) {
  if (!index) return;
  free(index->entries);
  *index = (ag_language_documentation_index_t){0};
}

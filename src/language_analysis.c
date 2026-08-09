#include "language_analysis.h"

#include <ctype.h>
#include <limits.h>
#if !defined(AGC_TARGET_WASM32) && !defined(__wasm32__)
#include <setjmp.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag/diag.h"
#include "frontend/translation_unit.h"
#include "frontend/translation_unit_resolver.h"
#include "language_documentation.h"
#include "parser/function_public.h"
#include "parser/gvar_public.h"
#include "parser/local_registry.h"
#include "parser/lvar_public.h"
#include "parser/semantic_ctx.h"
#include "preprocess/preprocess.h"
#include "semantic/prototype_parameter.h"
#include "semantic/record_decl_table.h"
#include "semantic/scope_graph.h"
#include "semantic/syntax_typed_hir_resolution.h"
#include "semantic/type_identity.h"
#include "source_manager.h"
#include "tokenizer/tokenizer.h"
#include "type_display.h"

#define AG_LANGUAGE_CURSOR_MARKER "__agc_language_cursor_marker_7f31"

typedef struct {
  ag_language_analysis_snapshot_t *snapshot;
  ag_language_analysis_error_t *error;
  ag_language_analysis_limits_t limits;
  const ag_language_documentation_index_t *documentation_index;
  int failed;
} snapshot_builder_t;

typedef struct {
  const char *name;
  const char *source;
  size_t length;
} analysis_source_view_t;

typedef struct {
  char *name;
  ag_language_source_range_t declaration;
  char *documentation;
  int has_documentation_range;
  ag_language_source_range_t documentation_range;
  int documentation_priority;
  ag_language_source_range_t *definitions;
  int definition_count;
  int definition_capacity;
} project_function_entry_t;

struct ag_language_project_index_t {
  unsigned int revision;
  project_function_entry_t *functions;
  int function_count;
  int function_capacity;
  int definition_count;
  int valid;
  ag_language_project_index_t *pending;
};

static void set_error(ag_language_analysis_error_t *error,
                      ag_language_analysis_status_t status,
                      const char *code, const char *limit,
                      size_t max, size_t actual) {
  if (!error) return;
  memset(error, 0, sizeof(*error));
  error->status = status;
  if (code) snprintf(error->code, sizeof(error->code), "%s", code);
  if (limit) snprintf(error->limit, sizeof(error->limit), "%s", limit);
  error->max = max;
  error->actual = actual;
}

static void builder_limit(snapshot_builder_t *builder,
                          const char *limit, const char *code,
                          size_t max, size_t actual) {
  if (!builder || builder->failed) return;
  builder->failed = 1;
  set_error(builder->error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
            code, limit, max, actual);
}

static void *snapshot_alloc(snapshot_builder_t *builder, size_t size) {
  if (!builder || builder->failed || size == 0) return NULL;
  size_t used = builder->snapshot->allocated_bytes;
  if (size > (size_t)builder->limits.max_snapshot_bytes ||
      used > (size_t)builder->limits.max_snapshot_bytes - size) {
    builder_limit(builder, "maxAnalysisSnapshotBytes",
                  "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES",
                  (size_t)builder->limits.max_snapshot_bytes, used + size);
    return NULL;
  }
  void *memory = calloc(1, size);
  if (!memory) {
    builder->failed = 1;
    set_error(builder->error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
              "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    return NULL;
  }
  builder->snapshot->allocated_bytes += size;
  return memory;
}

static char *snapshot_copy_n(snapshot_builder_t *builder,
                             const char *text, size_t length) {
  if (!text) text = "";
  if (length > (size_t)builder->limits.max_string_bytes) {
    builder_limit(builder, "maxAnalysisStringBytes",
                  "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES",
                  (size_t)builder->limits.max_string_bytes, length);
    return NULL;
  }
  char *copy = snapshot_alloc(builder, length + 1);
  if (!copy) return NULL;
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

static char *snapshot_copy(snapshot_builder_t *builder, const char *text) {
  return snapshot_copy_n(builder, text ? text : "", text ? strlen(text) : 0);
}

ag_language_analysis_limits_t ag_language_analysis_default_limits(void) {
  return (ag_language_analysis_limits_t){
      .max_sources = 4096,
      .max_source_bytes = 0x7ffffffeu,
      .max_total_source_bytes = 0x7fffffffu,
      .max_symbols = 4096,
      .max_completion_items = 4096,
      .max_string_bytes = 64 * 1024,
      .max_snapshot_bytes = 4 * 1024 * 1024,
  };
}

static int limits_are_valid(ag_language_analysis_limits_t *limits) {
  ag_language_analysis_limits_t defaults =
      ag_language_analysis_default_limits();
  if (limits->max_sources <= 0) limits->max_sources = defaults.max_sources;
  if (limits->max_source_bytes == 0)
    limits->max_source_bytes = defaults.max_source_bytes;
  if (limits->max_total_source_bytes == 0)
    limits->max_total_source_bytes = defaults.max_total_source_bytes;
  if (limits->max_symbols <= 0) limits->max_symbols = defaults.max_symbols;
  if (limits->max_completion_items <= 0)
    limits->max_completion_items = defaults.max_completion_items;
  if (limits->max_string_bytes <= 0)
    limits->max_string_bytes = defaults.max_string_bytes;
  if (limits->max_snapshot_bytes <= 0)
    limits->max_snapshot_bytes = defaults.max_snapshot_bytes;
  return limits->max_sources > 0 && limits->max_source_bytes > 0 &&
         limits->max_total_source_bytes > 0 && limits->max_symbols > 0 &&
         limits->max_completion_items > 0 &&
         limits->max_string_bytes > 0 && limits->max_snapshot_bytes > 0;
}

static int is_identifier_byte(unsigned char byte) {
  return byte == '_' || byte >= 0x80 || isalnum(byte);
}

static void identifier_at(const char *source, size_t length, size_t cursor,
                          const char **name, size_t *name_length) {
  *name = NULL;
  *name_length = 0;
  if (!source || cursor > length) return;
  size_t selected = cursor;
  if (selected == length || !is_identifier_byte((unsigned char)source[selected])) {
    if (selected == 0 ||
        !is_identifier_byte((unsigned char)source[selected - 1])) return;
    selected--;
  }
  size_t start = selected;
  size_t end = selected + 1;
  while (start > 0 && is_identifier_byte((unsigned char)source[start - 1]))
    start--;
  while (end < length && is_identifier_byte((unsigned char)source[end])) end++;
  *name = source + start;
  *name_length = end - start;
}

static size_t skip_analysis_space_and_comments(
    const char *source, size_t length, size_t cursor) {
  while (cursor < length) {
    if (isspace((unsigned char)source[cursor])) {
      cursor++;
      continue;
    }
    if (cursor + 1 < length && source[cursor] == '/' &&
        source[cursor + 1] == '*') {
      cursor += 2;
      while (cursor + 1 < length &&
             !(source[cursor] == '*' && source[cursor + 1] == '/'))
        cursor++;
      if (cursor + 1 < length) cursor += 2;
      continue;
    }
    if (cursor + 1 < length && source[cursor] == '/' &&
        source[cursor + 1] == '/') {
      cursor += 2;
      while (cursor < length && source[cursor] != '\n') cursor++;
      continue;
    }
    break;
  }
  return cursor;
}

static int word_before(
    const char *source, size_t cursor, const char *word) {
  while (cursor > 0 && isspace((unsigned char)source[cursor - 1])) cursor--;
  size_t end = cursor;
  while (cursor > 0 &&
         is_identifier_byte((unsigned char)source[cursor - 1]))
    cursor--;
  size_t length = end - cursor;
  return length == strlen(word) && memcmp(source + cursor, word, length) == 0;
}

static int enum_body_open_at(
    const char *source, size_t limit, size_t *enum_open,
    size_t *outer_brace_count) {
  size_t *braces = NULL;
  size_t brace_count = 0;
  size_t brace_capacity = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 1;
  int preprocessor_line = 0;
  for (size_t i = 0; i < limit; i++) {
    char c = source[i];
    char next = i + 1 < limit ? source[i + 1] : 0;
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
      preprocessor_line = 0;
      continue;
    }
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r')) continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    if (c == '{') {
      if (brace_count == brace_capacity) {
        size_t next_capacity = brace_capacity ? brace_capacity * 2 : 8;
        if (next_capacity < brace_capacity ||
            next_capacity > SIZE_MAX / sizeof(*braces)) {
          free(braces);
          return 0;
        }
        size_t *next_braces = realloc(
            braces, next_capacity * sizeof(*braces));
        if (!next_braces) {
          free(braces);
          return 0;
        }
        braces = next_braces;
        brace_capacity = next_capacity;
      }
      braces[brace_count++] = i;
    } else if (c == '}' && brace_count > 0) {
      brace_count--;
    }
  }
  if (brace_count == 0) {
    free(braces);
    return 0;
  }
  size_t open = braces[brace_count - 1];
  size_t outer_count = brace_count - 1;
  free(braces);
  size_t cursor = open;
  while (cursor > 0 && isspace((unsigned char)source[cursor - 1])) cursor--;
  size_t word_end = cursor;
  while (cursor > 0 &&
         is_identifier_byte((unsigned char)source[cursor - 1]))
    cursor--;
  if (word_end - cursor == strlen("enum") &&
      memcmp(source + cursor, "enum", strlen("enum")) == 0) {
    *enum_open = open;
    *outer_brace_count = outer_count;
    return 1;
  }
  if (cursor == word_end) return 0;
  while (cursor > 0 && isspace((unsigned char)source[cursor - 1])) cursor--;
  if (!word_before(source, cursor, "enum")) return 0;
  *enum_open = open;
  *outer_brace_count = outer_count;
  return 1;
}

static int enum_enumerator_bounds(
    const char *source, size_t length, size_t enum_open,
    size_t name_start, size_t name_end, size_t *item_end) {
  size_t segment_start = enum_open + 1;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  for (size_t i = segment_start; i < name_start; i++) {
    char c = source[i];
    char next = i + 1 < name_start ? source[i + 1] : 0;
    if (line_comment) {
      if (c == '\n') line_comment = 0;
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
      continue;
    }
    if (c == '(') paren_depth++;
    else if (c == ')' && paren_depth > 0) paren_depth--;
    else if (c == '[') bracket_depth++;
    else if (c == ']' && bracket_depth > 0) bracket_depth--;
    else if (c == '{') brace_depth++;
    else if (c == '}' && brace_depth > 0) brace_depth--;
    else if (c == ',' && paren_depth == 0 && bracket_depth == 0 &&
             brace_depth == 0)
      segment_start = i + 1;
  }
  if (skip_analysis_space_and_comments(
          source, name_start, segment_start) != name_start)
    return 0;

  paren_depth = 0;
  bracket_depth = 0;
  brace_depth = 0;
  line_comment = 0;
  block_comment = 0;
  quote = 0;
  escaped = 0;
  for (size_t i = name_end; i < length; i++) {
    char c = source[i];
    char next = i + 1 < length ? source[i + 1] : 0;
    if (line_comment) {
      if (c == '\n') line_comment = 0;
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
      continue;
    }
    if (c == '(') paren_depth++;
    else if (c == ')' && paren_depth > 0) paren_depth--;
    else if (c == '[') bracket_depth++;
    else if (c == ']' && bracket_depth > 0) bracket_depth--;
    else if (c == '{') brace_depth++;
    else if (c == '}' && brace_depth > 0) brace_depth--;
    else if ((c == ',' || c == '}') && paren_depth == 0 &&
             bracket_depth == 0 && brace_depth == 0) {
      *item_end = i;
      return 1;
    }
  }
  return 0;
}

static char *build_enum_declaration_recovery_source(
    const char *source, size_t length, size_t cursor, int *changed,
    size_t *source_consumed) {
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(source, length, cursor, &name, &name_length);
  if (!name || name_length == 0) return NULL;
  size_t name_start = (size_t)(name - source);
  size_t name_end = name_start + name_length;
  size_t enum_open = 0;
  size_t outer_brace_count = 0;
  size_t item_end = 0;
  if (!enum_body_open_at(
          source, name_start, &enum_open, &outer_brace_count) ||
      !enum_enumerator_bounds(
          source, length, enum_open, name_start, name_end, &item_end))
    return NULL;
  static const char suffix[] =
      "} __agc_language_enum_holder;\n"
      "int " AG_LANGUAGE_CURSOR_MARKER ";\n";
  size_t item_length = item_end - name_start;
  if (name_start > SIZE_MAX - item_length ||
      outer_brace_count > SIZE_MAX / 2 ||
      name_start + item_length > SIZE_MAX - sizeof(suffix) ||
      name_start + item_length + sizeof(suffix) >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length =
      name_start + item_length + sizeof(suffix) - 1 +
      outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, name_start);
  memcpy(result + name_start, source + name_start, item_length);
  size_t output = name_start + item_length;
  memcpy(result + output, suffix, sizeof(suffix) - 1);
  output += sizeof(suffix) - 1;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = 1;
  if (source_consumed) *source_consumed = item_end;
  return result;
}

static int analysis_word_is(const char *source, size_t start, size_t length,
                            const char *word) {
  size_t word_length = strlen(word);
  return length == word_length &&
         memcmp(source + start, word, word_length) == 0;
}

static int analysis_declaration_type_word(const char *source, size_t start,
                                          size_t length) {
  static const char *const words[] = {
      "void", "char", "short", "int", "long", "float", "double",
      "signed", "unsigned", "_Bool", "struct", "union", "enum",
      "_Atomic", "_Complex", "_Imaginary",
  };
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    if (analysis_word_is(source, start, length, words[i])) return 1;
  return 0;
}

static int analysis_non_declaration_word(const char *source, size_t start,
                                         size_t length) {
  static const char *const words[] = {
      "return", "if", "else", "while", "do", "for", "switch",
      "case", "default", "goto", "break", "continue",
  };
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    if (analysis_word_is(source, start, length, words[i])) return 1;
  return 0;
}

static int analysis_declaration_modifier_word(
    const char *source, size_t start, size_t length) {
  static const char *const words[] = {
      "const", "volatile", "restrict", "static", "extern", "register",
      "auto", "_Thread_local",
  };
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    if (analysis_word_is(source, start, length, words[i])) return 1;
  return 0;
}

static int object_declaration_prefix(
    const char *source, size_t name_start, size_t *outer_brace_count,
    int *paren_depth, int *bracket_depth, int *brace_depth) {
  size_t start = 0;
  size_t open_braces = 0;
  int parens = 0;
  int brackets = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 1;
  int preprocessor_line = 0;
  for (size_t i = 0; i < name_start; i++) {
    char c = source[i];
    char next = i + 1 < name_start ? source[i + 1] : 0;
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
      if (preprocessor_line) start = i + 1;
      at_line_start = 1;
      preprocessor_line = 0;
      continue;
    }
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r')) continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    if (c == '(') {
      parens++;
    } else if (c == ')' && parens > 0) {
      parens--;
    } else if (c == '[') {
      brackets++;
    } else if (c == ']' && brackets > 0) {
      brackets--;
    } else if (c == '{' && parens == 0 && brackets == 0) {
      open_braces++;
      start = i + 1;
    } else if (c == '}' && parens == 0 && brackets == 0) {
      if (open_braces > 0) open_braces--;
      start = i + 1;
    } else if (c == ';' && parens == 0 && brackets == 0) {
      start = i + 1;
    }
  }

  int has_type = 0;
  int typedef_candidate_count = 0;
  int has_non_declaration = 0;
  int assignment_after_comma = 0;
  int local_parens = 0;
  int local_brackets = 0;
  int local_braces = 0;
  line_comment = 0;
  block_comment = 0;
  quote = 0;
  escaped = 0;
  for (size_t i = start; i < name_start; i++) {
    char c = source[i];
    char next = i + 1 < name_start ? source[i + 1] : 0;
    if (line_comment) {
      if (c == '\n') line_comment = 0;
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
      continue;
    }
    if (is_identifier_byte((unsigned char)c)) {
      size_t word_start = i;
      while (i + 1 < name_start &&
             is_identifier_byte((unsigned char)source[i + 1]))
        i++;
      size_t word_length = i + 1 - word_start;
      int is_type = analysis_declaration_type_word(
          source, word_start, word_length);
      if (is_type)
        has_type = 1;
      else if (!analysis_declaration_modifier_word(
                   source, word_start, word_length) &&
               !analysis_non_declaration_word(
                   source, word_start, word_length))
        typedef_candidate_count++;
      if (analysis_non_declaration_word(
              source, word_start, word_length))
        has_non_declaration = 1;
      continue;
    }
    if (c == '(') local_parens++;
    else if (c == ')' && local_parens > 0) local_parens--;
    else if (c == '[') local_brackets++;
    else if (c == ']' && local_brackets > 0) local_brackets--;
    else if (c == '{') local_braces++;
    else if (c == '}' && local_braces > 0) local_braces--;
    else if (c == ',' && local_parens == 0 && local_brackets == 0 &&
             local_braces == 0)
      assignment_after_comma = 0;
    else if (c == '=' && local_parens == 0 && local_brackets == 0 &&
             local_braces == 0)
      assignment_after_comma = 1;
  }
  if ((!has_type && typedef_candidate_count != 1) ||
      has_non_declaration || assignment_after_comma)
    return 0;
  *outer_brace_count = open_braces;
  *paren_depth = local_parens;
  *bracket_depth = local_brackets;
  *brace_depth = local_braces;
  return 1;
}

static int object_declarator_end(
    const char *source, size_t length, size_t name_end, int paren_depth,
    int bracket_depth, int brace_depth, size_t *declarator_end) {
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  for (size_t i = name_end; i < length; i++) {
    char c = source[i];
    char next = i + 1 < length ? source[i + 1] : 0;
    if (line_comment) {
      if (c == '\n') line_comment = 0;
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
      continue;
    }
    if ((c == ',' || c == ';') && paren_depth == 0 &&
        bracket_depth == 0 && brace_depth == 0) {
      *declarator_end = i;
      return 1;
    }
    if (c == '(') paren_depth++;
    else if (c == ')' && paren_depth > 0) paren_depth--;
    else if (c == '[') bracket_depth++;
    else if (c == ']' && bracket_depth > 0) bracket_depth--;
    else if (c == '{') brace_depth++;
    else if (c == '}' && brace_depth > 0) brace_depth--;
  }
  return 0;
}

static int function_declaration_recovery_end(
    const char *source, size_t length, size_t name_end,
    size_t *declaration_end, int *is_definition) {
  size_t cursor = skip_analysis_space_and_comments(
      source, length, name_end);
  if (cursor >= length || source[cursor] != '(') return 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int body_depth = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 0;
  int preprocessor_line = 0;
  for (size_t i = cursor; i < length; i++) {
    char c = source[i];
    char next = i + 1 < length ? source[i + 1] : 0;
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
      if (!preprocessor_line ||
          i == 0 || source[i - 1] != '\\')
        preprocessor_line = 0;
      at_line_start = 1;
      continue;
    }
    if (at_line_start &&
        (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    if (body_depth > 0) {
      if (c == '{') body_depth++;
      else if (c == '}' && --body_depth == 0) {
        *declaration_end = i + 1;
        *is_definition = 1;
        return 1;
      }
      continue;
    }
    if (c == '(') paren_depth++;
    else if (c == ')' && paren_depth > 0) paren_depth--;
    else if (c == '[') bracket_depth++;
    else if (c == ']' && bracket_depth > 0) bracket_depth--;
    else if (paren_depth == 0 && bracket_depth == 0 && c == ';') {
      *declaration_end = i + 1;
      *is_definition = 0;
      return 1;
    } else if (paren_depth == 0 && bracket_depth == 0 && c == '{') {
      body_depth = 1;
    } else if (paren_depth == 0 && bracket_depth == 0 && c == ',') {
      return 0;
    }
  }
  return 0;
}

typedef enum {
  AG_LANGUAGE_RECOVERY_CHANGED = 1 << 0,
  AG_LANGUAGE_RECOVERY_PARTIAL_IDENTIFIER = 1 << 1,
  AG_LANGUAGE_RECOVERY_COMPLETE_IDENTIFIER_ELIDED = 1 << 2,
} ag_language_recovery_flags_t;

static char *build_function_declaration_recovery_source(
    const char *source, size_t length, size_t cursor, int *changed,
    size_t *source_consumed) {
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(source, length, cursor, &name, &name_length);
  if (!name || name_length == 0) return NULL;
  size_t name_start = (size_t)(name - source);
  size_t name_end = name_start + name_length;
  size_t outer_brace_count = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  if (!object_declaration_prefix(
          source, name_start, &outer_brace_count, &paren_depth,
          &bracket_depth, &brace_depth) ||
      paren_depth != 0 || bracket_depth != 0 || brace_depth != 0)
    return NULL;
  size_t declaration_end = 0;
  int is_definition = 0;
  if (!function_declaration_recovery_end(
          source, length, name_end,
          &declaration_end, &is_definition) ||
      (is_definition && outer_brace_count != 0))
    return NULL;
  static const char suffix[] =
      "\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  if (outer_brace_count > SIZE_MAX / 2 ||
      declaration_end > SIZE_MAX - sizeof(suffix) ||
      declaration_end + sizeof(suffix) >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length =
      declaration_end + sizeof(suffix) - 1 +
      outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, declaration_end);
  size_t output = declaration_end;
  memcpy(result + output, suffix, sizeof(suffix) - 1);
  output += sizeof(suffix) - 1;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = declaration_end;
  return result;
}

static char *build_object_declaration_recovery_source(
    const char *source, size_t length, size_t cursor, int *changed,
    size_t *source_consumed) {
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(source, length, cursor, &name, &name_length);
  if (!name || name_length == 0) return NULL;
  size_t name_start = (size_t)(name - source);
  size_t name_end = name_start + name_length;
  size_t outer_brace_count = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  if (!object_declaration_prefix(
          source, name_start, &outer_brace_count, &paren_depth,
          &bracket_depth, &brace_depth))
    return NULL;
  size_t after_name = skip_analysis_space_and_comments(
      source, length, name_end);
  if (after_name < length && source[after_name] == '(' &&
      paren_depth == 0 && bracket_depth == 0 && brace_depth == 0)
    return NULL;
  size_t declarator_end = 0;
  if (!object_declarator_end(
          source, length, name_end, paren_depth, bracket_depth,
          brace_depth, &declarator_end))
    return NULL;
  static const char suffix[] =
      ";\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  size_t declarator_length = declarator_end - name_start;
  if (name_start > SIZE_MAX - declarator_length ||
      outer_brace_count > SIZE_MAX / 2 ||
      name_start + declarator_length > SIZE_MAX - sizeof(suffix) ||
      name_start + declarator_length + sizeof(suffix) >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length =
      name_start + declarator_length + sizeof(suffix) - 1 +
      outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, name_start);
  memcpy(
      result + name_start, source + name_start, declarator_length);
  size_t output = name_start + declarator_length;
  memcpy(result + output, suffix, sizeof(suffix) - 1);
  output += sizeof(suffix) - 1;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = declarator_end;
  return result;
}

static int is_conditional_directive_line(
    const char *source, size_t start, size_t end) {
  while (start < end &&
         (source[start] == ' ' || source[start] == '\t' ||
          source[start] == '\r'))
    start++;
  if (start >= end || source[start++] != '#') return 0;
  while (start < end &&
         (source[start] == ' ' || source[start] == '\t' ||
          source[start] == '\r'))
    start++;
  size_t word_start = start;
  while (start < end &&
         is_identifier_byte((unsigned char)source[start]))
    start++;
  size_t word_length = start - word_start;
  static const char *const directives[] = {
      "if", "ifdef", "ifndef", "elif", "else", "endif",
  };
  for (size_t i = 0; i < sizeof(directives) / sizeof(directives[0]); i++)
    if (analysis_word_is(
            source, word_start, word_length, directives[i]))
      return 1;
  return 0;
}

static size_t analysis_logical_line_end(
    const char *source, size_t length, size_t start) {
  size_t cursor = start;
  for (;;) {
    while (cursor < length && source[cursor] != '\n') cursor++;
    size_t content_end = cursor;
    if (content_end > start && source[content_end - 1] == '\r')
      content_end--;
    int continued =
        content_end > start && source[content_end - 1] == '\\';
    if (cursor < length) cursor++;
    if (!continued || cursor >= length) return cursor;
    start = cursor;
  }
}

static char *append_conditional_validation_tail(
    char *recovery, const char *source, size_t source_length,
    size_t source_consumed) {
  if (!recovery || source_consumed >= source_length) return recovery;
  size_t scan = source_consumed;
  if (scan > 0 && source[scan - 1] != '\n')
    scan = analysis_logical_line_end(source, source_length, scan);
  int has_conditional = 0;
  for (size_t line = scan; line < source_length;) {
    size_t physical_end = line;
    while (physical_end < source_length && source[physical_end] != '\n')
      physical_end++;
    size_t logical_end = analysis_logical_line_end(
        source, source_length, line);
    if (is_conditional_directive_line(
            source, line, physical_end)) {
      has_conditional = 1;
      break;
    }
    line = logical_end;
  }
  if (!has_conditional) return recovery;

  size_t recovery_length = strlen(recovery);
  size_t tail_length = source_length - source_consumed;
  int needs_newline =
      recovery_length > 0 && recovery[recovery_length - 1] != '\n';
  if (recovery_length > SIZE_MAX - tail_length -
                            (size_t)needs_newline - 1) {
    free(recovery);
    return NULL;
  }
  char *result = malloc(
      recovery_length + (size_t)needs_newline + tail_length + 1);
  if (!result) {
    free(recovery);
    return NULL;
  }
  memcpy(result, recovery, recovery_length);
  size_t output = recovery_length;
  if (needs_newline) result[output++] = '\n';
  memset(result + output, ' ', tail_length);
  for (size_t i = source_consumed; i < source_length; i++)
    if (source[i] == '\n')
      result[output + i - source_consumed] = '\n';
  for (size_t line = scan; line < source_length;) {
    size_t physical_end = line;
    while (physical_end < source_length && source[physical_end] != '\n')
      physical_end++;
    size_t logical_end = analysis_logical_line_end(
        source, source_length, line);
    if (is_conditional_directive_line(
            source, line, physical_end))
      memcpy(result + output + line - source_consumed,
             source + line, logical_end - line);
    line = logical_end;
  }
  output += tail_length;
  result[output] = '\0';
  free(recovery);
  return result;
}

typedef struct {
  char open;
  int is_for_control;
  size_t for_separator_count;
  int is_generic_selection;
  size_t generic_separator_count;
  int generic_association_has_colon;
  size_t pending_conditional_count;
} recovery_delimiter_t;

static char *build_recovery_source(const char *source, size_t source_length,
                                   size_t cursor,
                                   int *changed) {
  size_t source_consumed = cursor;
  char *enum_recovery = build_enum_declaration_recovery_source(
      source, source_length, cursor, changed, &source_consumed);
  if (enum_recovery)
    return append_conditional_validation_tail(
        enum_recovery, source, source_length, source_consumed);
  char *function_recovery =
      build_function_declaration_recovery_source(
          source, source_length, cursor, changed, &source_consumed);
  if (function_recovery)
    return append_conditional_validation_tail(
        function_recovery, source, source_length, source_consumed);
  char *object_recovery = build_object_declaration_recovery_source(
      source, source_length, cursor, changed, &source_consumed);
  if (object_recovery)
    return append_conditional_validation_tail(
        object_recovery, source, source_length, source_consumed);
  int has_complete_identifier = 0;
  const char *cursor_name = NULL;
  size_t cursor_name_length = 0;
  identifier_at(
      source, source_length, cursor,
      &cursor_name, &cursor_name_length);
  if (cursor_name && cursor_name_length > 0) {
    size_t name_start = (size_t)(cursor_name - source);
    size_t name_end = name_start + cursor_name_length;
    /* A delimiter after the name proves that this is a complete source token,
     * rather than an identifier prefix still being typed at EOF. */
    if (name_end < source_length &&
        cursor >= name_start && cursor <= name_end)
      has_complete_identifier = 1;
  }
  int cursor_identifier_starts_conditional = 0;
  if (has_complete_identifier) {
    size_t name_end = (size_t)(cursor_name - source) + cursor_name_length;
    size_t after_name = skip_analysis_space_and_comments(
        source, source_length, name_end);
    cursor_identifier_starts_conditional =
        after_name < source_length && source[after_name] == '?';
  }
  size_t capacity = cursor * 2 + 8192;
  if (capacity < cursor || capacity > (size_t)INT_MAX) return NULL;
  char *result = calloc(capacity, 1);
  recovery_delimiter_t *stack = calloc(cursor + 1, sizeof(*stack));
  if (!result || !stack) {
    free(result);
    free(stack);
    return NULL;
  }
  memcpy(result, source, cursor);
  int identifier_elided = cursor_name && cursor_name_length > 0;
  if (cursor > 0 &&
      is_identifier_byte((unsigned char)source[cursor - 1])) {
    size_t identifier_start = cursor - 1;
    while (identifier_start > 0 &&
           is_identifier_byte((unsigned char)source[identifier_start - 1]))
      identifier_start--;
    for (size_t i = identifier_start; i < cursor; i++) result[i] = ' ';
    size_t operator_end = identifier_start;
    while (operator_end > 0 &&
           isspace((unsigned char)source[operator_end - 1]))
      operator_end--;
    if (operator_end > 0 && source[operator_end - 1] == '.') {
      result[operator_end - 1] = ' ';
    } else if (operator_end > 1 && source[operator_end - 2] == '-' &&
               source[operator_end - 1] == '>') {
      result[operator_end - 2] = ' ';
      result[operator_end - 1] = ' ';
    }
  }
  size_t stack_count = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 1;
  int preprocessor_line = 0;
  int previous_token_is_for = 0;
  int previous_token_is_generic = 0;
  size_t root_pending_conditional_count = 0;
  char last_significant = 0;
  for (size_t i = 0; i < cursor; i++) {
    char c = result[i];
    char next = i + 1 < cursor ? result[i + 1] : 0;
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
      previous_token_is_for = 0;
      previous_token_is_generic = 0;
      continue;
    }
    if (c == '\n') {
      at_line_start = 1;
      preprocessor_line = 0;
      continue;
    }
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r')) continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    int opens_for_control = 0;
    int opens_generic_selection = 0;
    if (is_identifier_byte((unsigned char)c)) {
      if (i == 0 ||
          !is_identifier_byte((unsigned char)result[i - 1])) {
        size_t identifier_end = i + 1;
        while (identifier_end < cursor &&
               is_identifier_byte((unsigned char)result[identifier_end]))
          identifier_end++;
        previous_token_is_for = identifier_end - i == strlen("for") &&
                                memcmp(result + i, "for", strlen("for")) == 0;
        previous_token_is_generic =
            identifier_end - i == strlen("_Generic") &&
            memcmp(result + i, "_Generic", strlen("_Generic")) == 0;
      }
    } else if (!isspace((unsigned char)c)) {
      opens_for_control = c == '(' && previous_token_is_for;
      opens_generic_selection = c == '(' && previous_token_is_generic;
      previous_token_is_for = 0;
      previous_token_is_generic = 0;
    }
    if (c == '(' || c == '[' || c == '{') {
      stack[stack_count++] = (recovery_delimiter_t){
          .open = c,
          .is_for_control = opens_for_control,
          .for_separator_count = 0,
          .is_generic_selection = opens_generic_selection,
          .generic_separator_count = 0,
          .generic_association_has_colon = 0,
          .pending_conditional_count = 0,
      };
    } else if ((c == ')' || c == ']' || c == '}') && stack_count > 0) {
      char open = stack[stack_count - 1].open;
      if ((open == '(' && c == ')') || (open == '[' && c == ']') ||
          (open == '{' && c == '}')) stack_count--;
    } else if (c == ';' && stack_count > 0 &&
               stack[stack_count - 1].open == '(' &&
               stack[stack_count - 1].is_for_control) {
      stack[stack_count - 1].for_separator_count++;
    }
    size_t *pending_conditional_count =
        stack_count > 0
            ? &stack[stack_count - 1].pending_conditional_count
            : &root_pending_conditional_count;
    if (stack_count > 0 && stack[stack_count - 1].is_generic_selection) {
      recovery_delimiter_t *generic = &stack[stack_count - 1];
      if (c == ',') {
        generic->generic_separator_count++;
        generic->generic_association_has_colon = 0;
      } else if (c == ':' && *pending_conditional_count == 0 &&
                 generic->generic_separator_count > 0) {
        generic->generic_association_has_colon = 1;
      }
    }
    if (c == '?') {
      (*pending_conditional_count)++;
    } else if (c == ':' && *pending_conditional_count > 0) {
      (*pending_conditional_count)--;
    } else if (c == ';') {
      *pending_conditional_count = 0;
    }
    if (!isspace((unsigned char)c)) last_significant = c;
  }
  size_t length = cursor;
#define APPEND_LITERAL(text)                                                     \
  do {                                                                           \
    const char *append_text = (text);                                             \
    size_t append_len = strlen(append_text);                                      \
    if (length + append_len + 1 >= capacity) { free(stack); free(result); return NULL; } \
    memcpy(result + length, append_text, append_len);                             \
    length += append_len;                                                         \
  } while (0)
#define APPEND_BYTES(bytes, byte_count)                                          \
  do {                                                                           \
    const char *append_bytes = (bytes);                                           \
    size_t append_len = (byte_count);                                             \
    if (length + append_len + 1 >= capacity) { free(stack); free(result); return NULL; } \
    memcpy(result + length, append_bytes, append_len);                            \
    length += append_len;                                                         \
  } while (0)
  if (line_comment) APPEND_LITERAL("\n");
  if (block_comment) APPEND_LITERAL("*/\n");
  if (quote) APPEND_LITERAL(quote == '\'' ? "'\n" : "\"\n");
  if (preprocessor_line) APPEND_LITERAL("\n");
  int cursor_in_generic_association_type = 0;
  for (size_t i = stack_count; i > 0; i--) {
    if (!stack[i - 1].is_generic_selection) continue;
    cursor_in_generic_association_type =
        stack[i - 1].generic_separator_count > 0 &&
        !stack[i - 1].generic_association_has_colon;
    break;
  }
  if (!cursor_in_generic_association_type &&
      (cursor_identifier_starts_conditional ||
      last_significant == '=' || last_significant == ',' ||
      last_significant == '(' || last_significant == '[' ||
      last_significant == '+' || last_significant == '-' ||
      last_significant == '*' || last_significant == '/' ||
      last_significant == '%' || last_significant == '&' ||
      last_significant == '|' || last_significant == '^' ||
      last_significant == '!' || last_significant == '~' ||
      last_significant == '<' || last_significant == '>' ||
      last_significant == '?' || last_significant == ':'))
    APPEND_LITERAL(" 0");
#define APPEND_PENDING_CONDITIONALS(count)                                      \
  do {                                                                           \
    size_t pending_count = (count);                                               \
    while (pending_count-- > 0) APPEND_LITERAL(" : 0");                         \
  } while (0)
  int cursor_marker_appended = 0;
  for (size_t i = stack_count; i > 0; i--) {
    APPEND_PENDING_CONDITIONALS(
        stack[i - 1].pending_conditional_count);
    if (stack[i - 1].open == '(') {
      if (stack[i - 1].is_generic_selection) {
        if (stack[i - 1].generic_separator_count == 0) {
          APPEND_LITERAL(", default: 0");
        } else if (!stack[i - 1].generic_association_has_colon) {
          if (has_complete_identifier && cursor_name &&
              cursor_name_length > 0) {
            APPEND_BYTES(cursor_name, cursor_name_length);
          } else {
            APPEND_LITERAL("int");
          }
          APPEND_LITERAL(": 0");
        }
      }
      if (stack[i - 1].is_for_control) {
        size_t separator_count = stack[i - 1].for_separator_count;
        while (separator_count < 2) {
          APPEND_LITERAL(";");
          separator_count++;
        }
      }
      APPEND_LITERAL(")");
      if (stack[i - 1].is_for_control) {
        APPEND_LITERAL(" {\nint " AG_LANGUAGE_CURSOR_MARKER ";\n}\n");
        cursor_marker_appended = 1;
      }
    } else if (stack[i - 1].open == '[') APPEND_LITERAL("]");
  }
  APPEND_PENDING_CONDITIONALS(root_pending_conditional_count);
  if (!cursor_marker_appended) {
    if (last_significant != 0 && last_significant != ';' &&
        last_significant != '}')
      APPEND_LITERAL(";");
    APPEND_LITERAL("\nint " AG_LANGUAGE_CURSOR_MARKER ";\n");
  }
  for (size_t i = stack_count; i > 0; i--)
    if (stack[i - 1].open == '{') APPEND_LITERAL("}\n");
  result[length] = '\0';
#undef APPEND_PENDING_CONDITIONALS
#undef APPEND_BYTES
#undef APPEND_LITERAL
  free(stack);
  if (changed) {
    *changed = AG_LANGUAGE_RECOVERY_CHANGED;
    if (identifier_elided) {
      *changed |= has_complete_identifier
                      ? AG_LANGUAGE_RECOVERY_COMPLETE_IDENTIFIER_ELIDED
                      : AG_LANGUAGE_RECOVERY_PARTIAL_IDENTIFIER;
    }
  }
  return append_conditional_validation_tail(
      result, source, source_length, cursor);
}

static uint32_t read_u32_le(const unsigned char *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int bundle_source_at(const ag_language_analysis_request_t *request,
                            int wanted, analysis_source_view_t *view) {
  if (!request || !view || wanted < 0 ||
      !request->virtual_header_bundle ||
      request->virtual_header_bundle_length < 4) return 0;
  const unsigned char *bundle = request->virtual_header_bundle;
  size_t length = request->virtual_header_bundle_length;
  uint32_t count = read_u32_le(bundle);
  if ((uint32_t)wanted >= count) return 0;
  size_t offset = 4;
  for (uint32_t index = 0; index < count; index++) {
    if (offset > length || length - offset < 8) return 0;
    uint32_t path_len = read_u32_le(bundle + offset);
    uint32_t source_len = read_u32_le(bundle + offset + 4);
    offset += 8;
    size_t need = (size_t)path_len + 1 + (size_t)source_len + 1;
    if (offset > length || need > length - offset) return 0;
    if ((int)index == wanted) {
      view->name = (const char *)(bundle + offset);
      view->source = (const char *)(bundle + offset + path_len + 1);
      view->length = source_len;
      return 1;
    }
    offset += need;
  }
  return 0;
}

static int source_count(const ag_language_analysis_request_t *request) {
  if (!request || !request->virtual_header_bundle ||
      request->virtual_header_bundle_length < 4) return 1;
  uint32_t count = read_u32_le(request->virtual_header_bundle);
  return count < (uint32_t)INT_MAX ? (int)count + 1 : 1;
}

static int source_at(const ag_language_analysis_request_t *request,
                     int index, analysis_source_view_t *view) {
  if (index == 0) {
    *view = (analysis_source_view_t){
        request->source_name, request->source, request->source_length};
    return 1;
  }
  return bundle_source_at(request, index - 1, view);
}

static int build_documentation_index(
    const ag_language_analysis_request_t *request,
    const ag_language_analysis_limits_t *limits,
    ag_language_documentation_index_t *index,
    ag_language_analysis_error_t *error) {
  for (int source_index = 0; source_index < source_count(request);
       source_index++) {
    analysis_source_view_t source = {0};
    if (!source_at(request, source_index, &source)) continue;
    ag_language_documentation_status_t status =
        ag_language_documentation_index_add_source(
            index, source.name, source.source, source.length,
            (size_t)limits->max_symbols);
    if (status == AG_LANGUAGE_DOCUMENTATION_OK) continue;
    ag_language_documentation_index_dispose(index);
    if (status == AG_LANGUAGE_DOCUMENTATION_LIMIT) {
      set_error(error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
                "AGC_LIMIT_MAX_ANALYSIS_SYMBOLS", "maxAnalysisSymbols",
                (size_t)limits->max_symbols,
                (size_t)limits->max_symbols + 1);
    } else {
      set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
                "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    }
    return 0;
  }
  return 1;
}

static int find_identifier(const char *source, size_t length,
                           const char *name, size_t name_len,
                           size_t *offset) {
  if (!source || !name || name_len == 0 || name_len > length) return 0;
  for (size_t i = 0; i + name_len <= length; i++) {
    if (memcmp(source + i, name, name_len) != 0) continue;
    if (i > 0 && is_identifier_byte((unsigned char)source[i - 1])) continue;
    if (i + name_len < length &&
        is_identifier_byte((unsigned char)source[i + name_len])) continue;
    *offset = i;
    return 1;
  }
  return 0;
}

static ag_language_position_t position_at(const char *source, size_t length,
                                          size_t offset) {
  ag_language_position_t position = {1, 1, (int)offset};
  if (offset > length) offset = length;
  for (size_t i = 0; i < offset; i++) {
    if (source[i] == '\n') {
      position.line++;
      position.column = 1;
    } else {
      position.column++;
    }
  }
  return position;
}

static void locate_declaration(snapshot_builder_t *builder,
                               const ag_language_analysis_request_t *request,
                               const psx_scope_declaration_t *declaration,
                               const char *name, size_t name_len,
                               ag_language_source_range_t *range) {
  if (declaration && declaration->source_name &&
      declaration->source_input && declaration->source_byte_offset >= 0 &&
      declaration->source_byte_length >= 0) {
    size_t source_length = strlen(declaration->source_input);
    size_t start = (size_t)declaration->source_byte_offset;
    size_t end = start + (size_t)declaration->source_byte_length;
    if (start <= source_length && end <= source_length) {
      range->source_name = snapshot_copy(builder, declaration->source_name);
      range->start = position_at(
          declaration->source_input, source_length, start);
      range->end = position_at(
          declaration->source_input, source_length, end);
      return;
    }
  }
  range->source_name = snapshot_copy(builder, "");
  range->start = (ag_language_position_t){0, 0, -1};
  range->end = (ag_language_position_t){0, 0, -1};
  for (int index = 0; index < source_count(request); index++) {
    analysis_source_view_t source = {0};
    if (!source_at(request, index, &source)) continue;
    size_t search_len = source.length;
    if (index == 0 && search_len > request->cursor_byte_offset)
      search_len = request->cursor_byte_offset;
    size_t offset = 0;
    if (!find_identifier(source.source, search_len, name, name_len, &offset))
      continue;
    free(range->source_name);
    range->source_name = snapshot_copy(builder, source.name);
    range->start = position_at(source.source, source.length, offset);
    range->end = position_at(source.source, source.length, offset + name_len);
    return;
  }
}

static int copy_function_source_location(
    snapshot_builder_t *builder,
    const psx_function_source_location_t *location,
    ag_language_source_range_t *range) {
  if (!builder || !location || !range || !location->source_name ||
      !location->source_input || location->byte_offset < 0 ||
      location->byte_length < 0)
    return 0;
  size_t source_length = strlen(location->source_input);
  size_t start = (size_t)location->byte_offset;
  size_t end = start + (size_t)location->byte_length;
  if (start > source_length || end > source_length) return 0;
  range->source_name = snapshot_copy(builder, location->source_name);
  range->start = position_at(location->source_input, source_length, start);
  range->end = position_at(location->source_input, source_length, end);
  return !builder->failed;
}

static char *format_type(snapshot_builder_t *builder,
                         const psx_semantic_type_table_t *types,
                         psx_qual_type_t type) {
  if (type.type_id == PSX_TYPE_ID_INVALID) return snapshot_copy(builder, "");
  int needed = ag_format_c_type(types, type, NULL, 0);
  if (needed < 0) return snapshot_copy(builder, "");
  if (needed > builder->limits.max_string_bytes) {
    builder_limit(builder, "maxAnalysisStringBytes",
                  "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES",
                  (size_t)builder->limits.max_string_bytes, (size_t)needed);
    return NULL;
  }
  char *result = snapshot_alloc(builder, (size_t)needed + 1);
  if (!result) return NULL;
  if (ag_format_c_type(types, type, result, (size_t)needed + 1) < 0)
    result[0] = '\0';
  return result;
}

static const char *object_signature_storage_prefix(
    const psx_scope_declaration_t *declaration) {
  if (!declaration || !declaration->payload) return "";
  if (declaration->kind == PSX_DECL_LOCAL_OBJECT) {
    const lvar_t *local = declaration->payload;
    if (ps_lvar_is_static_local(local)) {
      const global_var_t *global = ps_lvar_static_storage_global(local);
      return global && ps_gvar_is_thread_local(global)
                 ? "static _Thread_local "
                 : "static ";
    }
    if (ps_lvar_is_register(local)) return "register ";
    return "";
  }
  if (declaration->kind == PSX_DECL_GLOBAL_OBJECT) {
    const global_var_t *global = declaration->payload;
    int is_thread_local = ps_gvar_is_thread_local(global);
    if (ps_gvar_is_extern_decl(global))
      return is_thread_local ? "extern _Thread_local " : "extern ";
    if (ps_gvar_is_static_storage(global))
      return is_thread_local ? "static _Thread_local " : "static ";
    if (is_thread_local) return "_Thread_local ";
  }
  return "";
}

static char *format_object_signature(
    snapshot_builder_t *builder,
    const psx_semantic_type_table_t *types, psx_qual_type_t type,
    const psx_scope_declaration_t *declaration) {
  if (!builder || !types || !declaration ||
      type.type_id == PSX_TYPE_ID_INVALID ||
      !declaration->name || declaration->name_len <= 0)
    return snapshot_copy(builder, "");
  int declaration_length = ag_format_c_declaration(
      types, type, declaration->name, (size_t)declaration->name_len,
      NULL, 0);
  if (declaration_length < 0) return snapshot_copy(builder, "");
  const char *prefix = object_signature_storage_prefix(declaration);
  size_t prefix_length = strlen(prefix);
  size_t formatted_length = (size_t)declaration_length;
  if (prefix_length > SIZE_MAX - formatted_length) {
    builder_limit(builder, "maxAnalysisStringBytes",
                  "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES",
                  (size_t)builder->limits.max_string_bytes, SIZE_MAX);
    return NULL;
  }
  size_t total_length = prefix_length + formatted_length;
  if (total_length > (size_t)builder->limits.max_string_bytes) {
    builder_limit(builder, "maxAnalysisStringBytes",
                  "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES",
                  (size_t)builder->limits.max_string_bytes, total_length);
    return NULL;
  }
  char *result = snapshot_alloc(builder, total_length + 1);
  if (!result) return NULL;
  memcpy(result, prefix, prefix_length);
  if (ag_format_c_declaration(
          types, type, declaration->name,
          (size_t)declaration->name_len,
          result + prefix_length, formatted_length + 1) < 0)
    result[0] = '\0';
  return result;
}

static psx_qual_type_t declaration_type(
    psx_semantic_context_t *semantic_context,
    const psx_scope_declaration_t *declaration,
    psx_scope_lookup_point_t point) {
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  if (!declaration || !declaration->payload)
    return (psx_qual_type_t){PSX_TYPE_ID_INVALID, 0};
  switch (declaration->kind) {
    case PSX_DECL_LOCAL_OBJECT:
      return ps_lvar_decl_qual_type((const lvar_t *)declaration->payload);
    case PSX_DECL_PARAMETER:
      return psx_prototype_parameter_qual_type(declaration->payload);
    case PSX_DECL_GLOBAL_OBJECT:
      return ps_gvar_decl_qual_type((const global_var_t *)declaration->payload);
    case PSX_DECL_FUNCTION:
      return ps_function_symbol_qual_type(declaration->payload);
    case PSX_DECL_TYPEDEF: {
      psx_typedef_info_t info;
      return ps_ctx_find_typedef_name_at_in(
                 semantic_context, (char *)declaration->name,
                 declaration->name_len, point, &info)
                 ? info.decl_qual_type
                 : (psx_qual_type_t){PSX_TYPE_ID_INVALID, 0};
    }
    case PSX_DECL_ENUM_CONSTANT:
      return psx_semantic_type_table_fundamental_integer(
          types, PSX_INTEGER_KIND_INT, 0, 0);
    case PSX_DECL_TAG: {
      const token_kind_t kinds[] = {TK_STRUCT, TK_UNION, TK_ENUM};
      for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        psx_qual_type_t type = ps_ctx_tag_qual_type_at_in(
            semantic_context, kinds[i], (char *)declaration->name,
            declaration->name_len, point);
        if (type.type_id != PSX_TYPE_ID_INVALID) return type;
      }
      break;
    }
    default:
      break;
  }
  return (psx_qual_type_t){PSX_TYPE_ID_INVALID, 0};
}

static const char *symbol_kind_name(ag_language_symbol_kind_t kind) {
  switch (kind) {
    case AG_LANGUAGE_SYMBOL_OBJECT: return "object";
    case AG_LANGUAGE_SYMBOL_PARAMETER: return "parameter";
    case AG_LANGUAGE_SYMBOL_FUNCTION: return "function";
    case AG_LANGUAGE_SYMBOL_TYPEDEF: return "typedef";
    case AG_LANGUAGE_SYMBOL_ENUM_CONSTANT: return "enumConstant";
    case AG_LANGUAGE_SYMBOL_TAG: return "tag";
    case AG_LANGUAGE_SYMBOL_MEMBER: return "member";
    case AG_LANGUAGE_SYMBOL_MACRO: return "macro";
  }
  return "unknown";
}

static const char *namespace_name(ag_language_namespace_t name_space) {
  switch (name_space) {
    case AG_LANGUAGE_NAMESPACE_ORDINARY: return "ordinary";
    case AG_LANGUAGE_NAMESPACE_TAG: return "tag";
    case AG_LANGUAGE_NAMESPACE_LABEL: return "label";
    case AG_LANGUAGE_NAMESPACE_MEMBER: return "member";
    case AG_LANGUAGE_NAMESPACE_MACRO: return "macro";
  }
  return "ordinary";
}

static const char *initializer_name(ag_language_initializer_state_t state) {
  switch (state) {
    case AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT: return "explicitConstant";
    case AG_LANGUAGE_INITIALIZER_ZERO: return "zero";
    case AG_LANGUAGE_INITIALIZER_INDETERMINATE: return "indeterminate";
    case AG_LANGUAGE_INITIALIZER_RUNTIME: return "runtime";
    default: return "none";
  }
}

static int ensure_symbol_capacity(snapshot_builder_t *builder, int needed,
                                  int *capacity) {
  if (needed > builder->limits.max_symbols) {
    builder_limit(builder, "maxAnalysisSymbols",
                  "AGC_LIMIT_MAX_ANALYSIS_SYMBOLS",
                  (size_t)builder->limits.max_symbols, (size_t)needed);
    return 0;
  }
  if (needed > builder->limits.max_completion_items) {
    builder_limit(builder, "maxCompletionItems",
                  "AGC_LIMIT_MAX_COMPLETION_ITEMS",
                  (size_t)builder->limits.max_completion_items,
                  (size_t)needed);
    return 0;
  }
  if (needed <= *capacity) return 1;
  int next_capacity = *capacity ? *capacity * 2 : 32;
  if (next_capacity < needed) next_capacity = needed;
  if (next_capacity > builder->limits.max_completion_items)
    next_capacity = builder->limits.max_completion_items;
  size_t new_bytes = (size_t)next_capacity * sizeof(ag_language_symbol_t);
  size_t old_bytes = (size_t)(*capacity) * sizeof(ag_language_symbol_t);
  if (new_bytes > (size_t)builder->limits.max_snapshot_bytes ||
      builder->snapshot->allocated_bytes - old_bytes >
          (size_t)builder->limits.max_snapshot_bytes - new_bytes) {
    builder_limit(builder, "maxAnalysisSnapshotBytes",
                  "AGC_LIMIT_MAX_ANALYSIS_SNAPSHOT_BYTES",
                  (size_t)builder->limits.max_snapshot_bytes,
                  builder->snapshot->allocated_bytes - old_bytes + new_bytes);
    return 0;
  }
  ag_language_symbol_t *next = realloc(
      builder->snapshot->completion_items, new_bytes);
  if (!next) {
    builder->failed = 1;
    set_error(builder->error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
              "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    return 0;
  }
  memset(next + *capacity, 0,
         (size_t)(next_capacity - *capacity) * sizeof(*next));
  builder->snapshot->completion_items = next;
  builder->snapshot->allocated_bytes =
      builder->snapshot->allocated_bytes - old_bytes + new_bytes;
  *capacity = next_capacity;
  return 1;
}

static void fill_function(snapshot_builder_t *builder,
                          const psx_semantic_type_table_t *types,
                          psx_qual_type_t function_type,
                          ag_language_symbol_t *symbol) {
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          types, function_type.type_id, &shape) ||
      shape.kind != PSX_TYPE_FUNCTION) return;
  symbol->has_function_prototype = shape.has_function_prototype ? 1 : 0;
  symbol->is_variadic = shape.is_variadic_function ? 1 : 0;
  symbol->return_type = format_type(
      builder, types,
      psx_semantic_type_table_base(types, function_type.type_id));
  symbol->parameter_count = shape.parameter_count;
  if (shape.parameter_count <= 0) return;
  symbol->parameters = snapshot_alloc(
      builder, (size_t)shape.parameter_count * sizeof(*symbol->parameters));
  if (!symbol->parameters) return;
  for (int i = 0; i < shape.parameter_count; i++) {
    symbol->parameters[i].name = snapshot_copy(builder, "");
    symbol->parameters[i].type = format_type(
        builder, types,
        psx_semantic_type_table_parameter(types, function_type.type_id, i));
  }
}

static void fill_function_storage_class(
    snapshot_builder_t *builder,
    const psx_scope_declaration_t *declaration,
    ag_language_symbol_t *symbol) {
  if (!declaration || declaration->kind != PSX_DECL_FUNCTION ||
      !declaration->payload || !symbol)
    return;
  const psx_function_symbol_t *function = declaration->payload;
  const char *storage_class = "";
  if (ps_function_symbol_has_internal_linkage(function))
    storage_class = "static";
  else if (ps_function_symbol_has_explicit_extern(function))
    storage_class = "extern";
  free(symbol->storage_class);
  symbol->storage_class = snapshot_copy(builder, storage_class);
}

static int is_parameter_type_word(const char *word, size_t length) {
  static const char *const words[] = {
      "void", "char", "short", "int", "long", "float", "double",
      "signed", "unsigned", "const", "volatile", "restrict", "_Atomic",
      "struct", "union", "enum", "register", "static", "extern"};
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    if (strlen(words[i]) == length && memcmp(words[i], word, length) == 0)
      return 1;
  return 0;
}

static void fill_function_parameter_names(
    snapshot_builder_t *builder,
    const ag_language_analysis_request_t *request,
    ag_language_symbol_t *symbol) {
  if (!symbol || symbol->parameter_count <= 0) return;
  for (int source_index = 0; source_index < source_count(request); source_index++) {
    analysis_source_view_t source = {0};
    if (!source_at(request, source_index, &source)) continue;
    size_t function_offset = 0;
    if (!find_identifier(source.source, source.length, symbol->name,
                         strlen(symbol->name), &function_offset)) continue;
    size_t cursor = function_offset + strlen(symbol->name);
    while (cursor < source.length && isspace((unsigned char)source.source[cursor]))
      cursor++;
    if (cursor >= source.length || source.source[cursor] != '(') continue;
    cursor++;
    int parameter_index = 0;
    int nesting = 0;
    size_t segment_start = cursor;
    for (; cursor <= source.length && parameter_index < symbol->parameter_count;
         cursor++) {
      char c = cursor < source.length ? source.source[cursor] : ')';
      if (c == '(' || c == '[') nesting++;
      else if ((c == ')' || c == ']') && nesting > 0) nesting--;
      if (!((c == ',' && nesting == 0) || (c == ')' && nesting == 0)))
        continue;
      const char *best = NULL;
      size_t best_length = 0;
      for (size_t i = segment_start; i < cursor;) {
        if (!is_identifier_byte((unsigned char)source.source[i])) {
          i++;
          continue;
        }
        size_t start = i++;
        while (i < cursor &&
               is_identifier_byte((unsigned char)source.source[i])) i++;
        size_t length = i - start;
        if (!is_parameter_type_word(source.source + start, length)) {
          best = source.source + start;
          best_length = length;
        }
      }
      if (best && best_length > 0) {
        free(symbol->parameters[parameter_index].name);
        symbol->parameters[parameter_index].name = snapshot_copy_n(
            builder, best, best_length);
      }
      parameter_index++;
      segment_start = cursor + 1;
      if (c == ')') break;
    }
    return;
  }
}

static ag_language_symbol_kind_t declaration_kind(
    const psx_scope_declaration_t *declaration) {
  switch (declaration->kind) {
    case PSX_DECL_FUNCTION: return AG_LANGUAGE_SYMBOL_FUNCTION;
    case PSX_DECL_TYPEDEF: return AG_LANGUAGE_SYMBOL_TYPEDEF;
    case PSX_DECL_ENUM_CONSTANT: return AG_LANGUAGE_SYMBOL_ENUM_CONSTANT;
    case PSX_DECL_TAG: return AG_LANGUAGE_SYMBOL_TAG;
    case PSX_DECL_MEMBER: return AG_LANGUAGE_SYMBOL_MEMBER;
    case PSX_DECL_PARAMETER: return AG_LANGUAGE_SYMBOL_PARAMETER;
    case PSX_DECL_LOCAL_OBJECT:
      if (declaration->payload &&
          ps_lvar_is_param((const lvar_t *)declaration->payload))
        return AG_LANGUAGE_SYMBOL_PARAMETER;
      return AG_LANGUAGE_SYMBOL_OBJECT;
    default: return AG_LANGUAGE_SYMBOL_OBJECT;
  }
}

static ag_language_namespace_t declaration_namespace(
    psx_c_namespace_t name_space) {
  switch (name_space) {
    case PSX_NAMESPACE_TAG: return AG_LANGUAGE_NAMESPACE_TAG;
    case PSX_NAMESPACE_LABEL: return AG_LANGUAGE_NAMESPACE_LABEL;
    case PSX_NAMESPACE_MEMBER: return AG_LANGUAGE_NAMESPACE_MEMBER;
    default: return AG_LANGUAGE_NAMESPACE_ORDINARY;
  }
}

static void fill_initializer(snapshot_builder_t *builder,
                             psx_semantic_context_t *semantic_context,
                             const psx_scope_declaration_t *declaration,
                             ag_language_symbol_t *symbol) {
  symbol->initializer_state = AG_LANGUAGE_INITIALIZER_NONE;
  symbol->constant_value = snapshot_copy(builder, "");
  if (declaration->kind == PSX_DECL_LOCAL_OBJECT) {
    const lvar_t *local = declaration->payload;
    psx_lvar_registry_view_t view = ps_lvar_registry_view(local);
    if (view.is_param) return;
    if (ps_lvar_is_static_local(local)) {
      global_var_t *global = ps_lvar_static_storage_global(local);
      symbol->initializer_state =
          global && ps_gvar_has_explicit_initializer(global)
              ? AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT
              : AG_LANGUAGE_INITIALIZER_ZERO;
      symbol->storage_class = snapshot_copy(builder, "static");
    } else {
      symbol->initializer_state = view.is_initialized
                                      ? AG_LANGUAGE_INITIALIZER_RUNTIME
                                      : AG_LANGUAGE_INITIALIZER_INDETERMINATE;
      symbol->storage_class = snapshot_copy(builder, "automatic");
    }
  } else if (declaration->kind == PSX_DECL_GLOBAL_OBJECT) {
    const global_var_t *global = declaration->payload;
    if (ps_gvar_is_extern_decl(global)) {
      symbol->storage_class = snapshot_copy(builder, "extern");
    } else {
      symbol->initializer_state = ps_gvar_has_explicit_initializer(global)
                                      ? AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT
                                      : AG_LANGUAGE_INITIALIZER_ZERO;
      symbol->storage_class = snapshot_copy(builder, "staticStorage");
    }
    if (ps_gvar_has_explicit_initializer(global)) {
      psx_gvar_init_scalar_value_t value = ps_gvar_init_scalar_value(global, 8);
      if (value.kind == PSX_GVAR_INIT_VALUE_INTEGER) {
        char number[64];
        snprintf(number, sizeof(number), "%lld", value.value);
        free(symbol->constant_value);
        symbol->constant_value = snapshot_copy(builder, number);
      }
    }
  } else if (declaration->kind == PSX_DECL_ENUM_CONSTANT) {
    long long value = 0;
    if (ps_ctx_enum_const_value_by_declaration_id_in(
            semantic_context, declaration->id, &value)) {
      char number[64];
      snprintf(number, sizeof(number), "%lld", value);
      symbol->initializer_state = AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT;
      free(symbol->constant_value);
      symbol->constant_value = snapshot_copy(builder, number);
    }
  }
}

static int initializer_source_range(
    snapshot_builder_t *builder,
    const ag_language_analysis_request_t *request,
    ag_language_symbol_t *symbol) {
  if (!builder || !request || !symbol ||
      symbol->initializer_state == AG_LANGUAGE_INITIALIZER_NONE ||
      !symbol->declaration.source_name ||
      symbol->declaration.end.offset < 0)
    return 1;
  analysis_source_view_t source = {0};
  int found_source = 0;
  for (int index = 0; index < source_count(request); index++) {
    if (source_at(request, index, &source) && source.name &&
        strcmp(source.name, symbol->declaration.source_name) == 0) {
      found_source = 1;
      break;
    }
  }
  if (!found_source) return 1;
  size_t cursor = (size_t)symbol->declaration.end.offset;
  if (cursor > source.length) return 1;
  while (cursor < source.length &&
         isspace((unsigned char)source.source[cursor]))
    cursor++;
  if (cursor >= source.length || source.source[cursor] != '=') return 1;
  cursor++;
  while (cursor < source.length &&
         isspace((unsigned char)source.source[cursor]))
    cursor++;
  size_t start = cursor;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  int quote = 0;
  int escaped = 0;
  for (; cursor < source.length; cursor++) {
    char c = source.source[cursor];
    if (quote) {
      if (escaped) escaped = 0;
      else if (c == '\\') escaped = 1;
      else if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c == '(') paren_depth++;
    else if (c == ')' && paren_depth > 0) paren_depth--;
    else if (c == '[') bracket_depth++;
    else if (c == ']' && bracket_depth > 0) bracket_depth--;
    else if (c == '{') brace_depth++;
    else if (c == '}' && brace_depth > 0) brace_depth--;
    else if ((c == ',' || c == ';') && paren_depth == 0 &&
             bracket_depth == 0 && brace_depth == 0)
      break;
  }
  size_t end = cursor;
  while (end > start && isspace((unsigned char)source.source[end - 1])) end--;
  if (end <= start) return 1;
  symbol->has_initializer_range = 1;
  symbol->initializer_range.source_name = snapshot_copy(builder, source.name);
  symbol->initializer_range.start = position_at(
      source.source, source.length, start);
  symbol->initializer_range.end = position_at(
      source.source, source.length, end);
  return !builder->failed;
}

static int source_ranges_identical(
    const ag_language_source_range_t *left,
    const ag_language_source_range_t *right) {
  return left && right && left->source_name && right->source_name &&
         strcmp(left->source_name, right->source_name) == 0 &&
         left->start.offset == right->start.offset &&
         left->end.offset == right->end.offset;
}

static const ag_language_documentation_entry_t *documentation_for_range(
    const snapshot_builder_t *builder,
    const ag_language_source_range_t *range) {
  if (!builder || !builder->documentation_index || !range ||
      !range->source_name || range->start.offset < 0)
    return NULL;
  return ag_language_documentation_find(
      builder->documentation_index, range->source_name,
      (size_t)range->start.offset);
}

static int set_symbol_documentation(
    snapshot_builder_t *builder, ag_language_symbol_t *symbol,
    const ag_language_documentation_entry_t *entry) {
  if (!builder || !symbol || !entry) return 0;
  size_t length = ag_language_documentation_normalized_length(entry);
  if (length == 0) return 0;
  if (length > (size_t)builder->limits.max_string_bytes) {
    builder_limit(builder, "maxAnalysisStringBytes",
                  "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES",
                  (size_t)builder->limits.max_string_bytes, length);
    return 0;
  }
  char *documentation = snapshot_alloc(builder, length + 1);
  if (!documentation ||
      !ag_language_documentation_normalize(
          entry, documentation, length + 1))
    return 0;
  free(symbol->documentation);
  symbol->documentation = documentation;
  symbol->has_documentation_range = 1;
  symbol->documentation_range.source_name = snapshot_copy(
      builder, entry->source_name);
  symbol->documentation_range.start = position_at(
      entry->source, entry->source_length, entry->comment_start);
  symbol->documentation_range.end = position_at(
      entry->source, entry->source_length, entry->comment_end);
  return !builder->failed;
}

static void fill_documentation(
    snapshot_builder_t *builder, ag_language_symbol_t *symbol) {
  if (!builder || !symbol ||
      (symbol->kind != AG_LANGUAGE_SYMBOL_OBJECT &&
       symbol->kind != AG_LANGUAGE_SYMBOL_FUNCTION))
    return;
  const ag_language_documentation_entry_t *declaration_entry =
      documentation_for_range(builder, &symbol->declaration);
  int declaration_is_definition =
      symbol->kind == AG_LANGUAGE_SYMBOL_FUNCTION &&
      symbol->has_definition &&
      source_ranges_identical(&symbol->declaration, &symbol->definition);
  if (set_symbol_documentation(builder, symbol, declaration_entry))
    return;
  if (symbol->kind != AG_LANGUAGE_SYMBOL_FUNCTION ||
      !symbol->has_definition || declaration_is_definition)
    return;
  set_symbol_documentation(
      builder, symbol,
      documentation_for_range(builder, &symbol->definition));
}

static int add_declaration_symbol(
    snapshot_builder_t *builder,
    const ag_language_analysis_request_t *request,
    psx_semantic_context_t *semantic_context,
    const psx_scope_declaration_t *declaration,
    psx_scope_lookup_point_t point, int *capacity) {
  int count = builder->snapshot->completion_item_count;
  if (!ensure_symbol_capacity(builder, count + 1, capacity)) return 0;
  ag_language_symbol_t *symbol =
      &builder->snapshot->completion_items[count];
  builder->snapshot->completion_item_count++;
  symbol->name = snapshot_copy_n(
      builder, declaration->name, (size_t)declaration->name_len);
  symbol->documentation = snapshot_copy(builder, "");
  symbol->kind = declaration_kind(declaration);
  symbol->name_space = declaration_namespace(declaration->name_space);
  symbol->scope_depth = psx_scope_graph_scope_depth(
      ps_ctx_scope_graph(semantic_context), declaration->scope_id);
  symbol->declaration_order = declaration->declaration_order;
  const psx_function_symbol_t *function_symbol =
      declaration->kind == PSX_DECL_FUNCTION
          ? declaration->payload : NULL;
  psx_function_source_location_t function_declaration = {0};
  if (!function_symbol ||
      !ps_function_symbol_declaration_location(
          function_symbol, &function_declaration) ||
      !copy_function_source_location(
          builder, &function_declaration, &symbol->declaration))
    locate_declaration(builder, request, declaration, declaration->name,
                       (size_t)declaration->name_len,
                       &symbol->declaration);
  psx_function_source_location_t function_definition = {0};
  if (function_symbol &&
      ps_function_symbol_definition_location(
          function_symbol, &function_definition) &&
      copy_function_source_location(
          builder, &function_definition, &symbol->definition)) {
    symbol->has_definition = 1;
    symbol->definition_candidates = snapshot_alloc(
        builder, sizeof(*symbol->definition_candidates));
    if (symbol->definition_candidates) {
      symbol->definition_candidates[0].source_name = snapshot_copy(
          builder, symbol->definition.source_name);
      symbol->definition_candidates[0].start = symbol->definition.start;
      symbol->definition_candidates[0].end = symbol->definition.end;
      symbol->definition_candidate_count = 1;
    }
  }
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t qual_type = declaration_type(
      semantic_context, declaration, point);
  symbol->type = format_type(builder, types, qual_type);
  symbol->signature = snapshot_copy(builder, "");
  symbol->return_type = snapshot_copy(builder, "");
  symbol->storage_class = snapshot_copy(builder, "");
  symbol->constant_value = snapshot_copy(builder, "");
  symbol->macro_replacement = snapshot_copy(builder, "");
  if (symbol->kind == AG_LANGUAGE_SYMBOL_OBJECT) {
    free(symbol->signature);
    symbol->signature = format_object_signature(
        builder, types, qual_type, declaration);
  }
  if (symbol->kind == AG_LANGUAGE_SYMBOL_FUNCTION) {
    free(symbol->signature);
    symbol->signature = snapshot_copy(builder, symbol->type);
    fill_function(builder, types, qual_type, symbol);
    fill_function_storage_class(builder, declaration, symbol);
    fill_function_parameter_names(builder, request, symbol);
  }
  free(symbol->constant_value);
  symbol->constant_value = NULL;
  fill_initializer(builder, semantic_context, declaration, symbol);
  initializer_source_range(builder, request, symbol);
  if (declaration->kind == PSX_DECL_LOCAL_OBJECT &&
      symbol->has_initializer_range && declaration->payload &&
      !ps_lvar_is_param((const lvar_t *)declaration->payload) &&
      !ps_lvar_is_static_local((const lvar_t *)declaration->payload))
    symbol->initializer_state = AG_LANGUAGE_INITIALIZER_RUNTIME;
  fill_documentation(builder, symbol);
  if (builder->failed) return 0;
  return 1;
}

static int add_macro_symbols(snapshot_builder_t *builder,
                             const ag_language_analysis_request_t *request,
                             ag_preprocessor_context_t *preprocessor,
                             int *capacity) {
  int macro_count = pp_macro_count_in(preprocessor);
  for (int macro_index = 0; macro_index < macro_count; macro_index++) {
    ag_pp_macro_view_t view;
    if (!pp_macro_view_at_in(preprocessor, macro_index, &view) ||
        !view.name || view.name_len <= 0 ||
        (view.name_len >= 2 && view.name[0] == '_' && view.name[1] == '_'))
      continue;
    int count = builder->snapshot->completion_item_count;
    if (!ensure_symbol_capacity(builder, count + 1, capacity)) return 0;
    ag_language_symbol_t *symbol =
        &builder->snapshot->completion_items[count];
    builder->snapshot->completion_item_count++;
    symbol->name = snapshot_copy_n(builder, view.name, (size_t)view.name_len);
    symbol->documentation = snapshot_copy(builder, "");
    symbol->kind = AG_LANGUAGE_SYMBOL_MACRO;
    symbol->name_space = AG_LANGUAGE_NAMESPACE_MACRO;
    symbol->type = snapshot_copy(builder, "macro");
    symbol->signature = snapshot_copy(builder, "");
    symbol->return_type = snapshot_copy(builder, "");
    symbol->storage_class = snapshot_copy(builder, "preprocessor");
    symbol->constant_value = snapshot_copy(builder, "");
    symbol->scope_depth = 0;
    symbol->declaration_order = 0;
    symbol->macro_is_function_like = view.is_function_like;
    symbol->macro_is_variadic = view.is_variadic;
    symbol->macro_parameter_count = view.parameter_count;
    if (view.source_name && view.source_input &&
        view.source_byte_offset >= 0 && view.source_byte_length >= 0) {
      size_t source_length = strlen(view.source_input);
      size_t start = (size_t)view.source_byte_offset;
      size_t end = start + (size_t)view.source_byte_length;
      if (start <= source_length && end <= source_length) {
        symbol->declaration.source_name = snapshot_copy(
            builder, view.source_name);
        symbol->declaration.start = position_at(
            view.source_input, source_length, start);
        symbol->declaration.end = position_at(
            view.source_input, source_length, end);
      }
    }
    if (!symbol->declaration.source_name)
      locate_declaration(builder, request, NULL, view.name,
                         (size_t)view.name_len, &symbol->declaration);
    int replacement_len = pp_macro_format_replacement_in(
        preprocessor, macro_index, NULL, 0);
    if (replacement_len < 0) replacement_len = 0;
    if (replacement_len > builder->limits.max_string_bytes) {
      builder_limit(builder, "maxAnalysisStringBytes",
                    "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES",
                    (size_t)builder->limits.max_string_bytes,
                    (size_t)replacement_len);
      return 0;
    }
    symbol->macro_replacement = snapshot_alloc(
        builder, (size_t)replacement_len + 1);
    if (symbol->macro_replacement)
      pp_macro_format_replacement_in(
          preprocessor, macro_index, symbol->macro_replacement,
          (size_t)replacement_len + 1);
    if (view.parameter_count > 0) {
      symbol->macro_parameters = snapshot_alloc(
          builder, (size_t)view.parameter_count * sizeof(char *));
      for (int i = 0; i < view.parameter_count && !builder->failed; i++)
        symbol->macro_parameters[i] = snapshot_copy(
            builder, pp_macro_parameter_at_in(preprocessor, macro_index, i));
    }
    if (builder->failed) return 0;
  }
  return 1;
}

static int member_base_at_cursor(
    const ag_language_analysis_request_t *request,
    const char **object_name, size_t *object_name_len, int *uses_arrow) {
  *object_name = NULL;
  *object_name_len = 0;
  *uses_arrow = 0;
  size_t cursor = request->cursor_byte_offset;
  while (cursor > 0 &&
         is_identifier_byte((unsigned char)request->source[cursor - 1]))
    cursor--;
  while (cursor > 0 && isspace((unsigned char)request->source[cursor - 1]))
    cursor--;
  if (cursor > 0 && request->source[cursor - 1] == '.') {
    cursor--;
  } else if (cursor > 1 && request->source[cursor - 2] == '-' &&
             request->source[cursor - 1] == '>') {
    cursor -= 2;
    *uses_arrow = 1;
  } else {
    return 0;
  }
  while (cursor > 0 && isspace((unsigned char)request->source[cursor - 1]))
    cursor--;
  size_t end = cursor;
  while (cursor > 0 &&
         is_identifier_byte((unsigned char)request->source[cursor - 1]))
    cursor--;
  if (cursor == end) return 0;
  *object_name = request->source + cursor;
  *object_name_len = end - cursor;
  return 1;
}

static int add_member_symbols(
    snapshot_builder_t *builder,
    const ag_language_analysis_request_t *request,
    psx_semantic_context_t *semantic_context,
    psx_scope_lookup_point_t point, int *capacity) {
  const char *object_name = NULL;
  size_t object_name_len = 0;
  int uses_arrow = 0;
  if (!member_base_at_cursor(
          request, &object_name, &object_name_len, &uses_arrow))
    return 1;
  psx_scope_graph_t *graph = ps_ctx_scope_graph(semantic_context);
  psx_decl_id_t object_id = psx_scope_graph_lookup(
      graph, PSX_NAMESPACE_ORDINARY, object_name, (int)object_name_len,
      point);
  const psx_scope_declaration_t *object =
      psx_scope_graph_declaration(graph, object_id);
  if (!object || (object->kind != PSX_DECL_LOCAL_OBJECT &&
                  object->kind != PSX_DECL_GLOBAL_OBJECT &&
                  object->kind != PSX_DECL_PARAMETER))
    return 1;
  const psx_semantic_type_table_t *types =
      ps_ctx_semantic_type_table_in(semantic_context);
  psx_qual_type_t object_type = declaration_type(
      semantic_context, object, point);
  psx_type_shape_t shape = {0};
  if (!psx_semantic_type_table_describe(
          types, object_type.type_id, &shape)) return 1;
  if (uses_arrow) {
    if (shape.kind != PSX_TYPE_POINTER) return 1;
    object_type = psx_semantic_type_table_base(types, object_type.type_id);
    if (!psx_semantic_type_table_describe(
            types, object_type.type_id, &shape)) return 1;
  }
  if (shape.kind != PSX_TYPE_STRUCT && shape.kind != PSX_TYPE_UNION) return 1;
  const psx_record_decl_t *record = psx_record_decl_table_lookup(
      ps_ctx_record_decl_table_in(semantic_context), shape.record_id);
  if (!record || !record->members) return 1;
  for (int member_index = 0;
       member_index < record->member_count && !builder->failed;
       member_index++) {
    const psx_record_member_decl_t *member = &record->members[member_index];
    if (!member->name || member->len <= 0) continue;
    int count = builder->snapshot->completion_item_count;
    if (!ensure_symbol_capacity(builder, count + 1, capacity)) return 0;
    ag_language_symbol_t *symbol =
        &builder->snapshot->completion_items[count];
    builder->snapshot->completion_item_count++;
    symbol->name = snapshot_copy_n(
        builder, member->name, (size_t)member->len);
    symbol->documentation = snapshot_copy(builder, "");
    symbol->kind = AG_LANGUAGE_SYMBOL_MEMBER;
    symbol->name_space = AG_LANGUAGE_NAMESPACE_MEMBER;
    symbol->type = format_type(builder, types, member->decl_qual_type);
    symbol->signature = snapshot_copy(builder, "");
    symbol->return_type = snapshot_copy(builder, "");
    symbol->storage_class = snapshot_copy(builder, "member");
    symbol->constant_value = snapshot_copy(builder, "");
    symbol->macro_replacement = snapshot_copy(builder, "");
    symbol->scope_depth = psx_scope_graph_scope_depth(
        graph, object->scope_id) + 1;
    symbol->declaration_order = (unsigned int)member_index;
    locate_declaration(builder, request, NULL,
                       member->name, (size_t)member->len,
                       &symbol->declaration);
  }
  return !builder->failed;
}

static int symbol_compare(const void *left_pointer, const void *right_pointer) {
  const ag_language_symbol_t *left = left_pointer;
  const ag_language_symbol_t *right = right_pointer;
  if (left->scope_depth != right->scope_depth)
    return right->scope_depth - left->scope_depth;
  int name_order = strcmp(left->name ? left->name : "",
                          right->name ? right->name : "");
  if (name_order) return name_order;
  return (int)left->kind - (int)right->kind;
}

static void select_hover(ag_language_analysis_snapshot_t *snapshot,
                         const char *name, size_t name_len) {
  snapshot->hover_index = -1;
  if (!name || name_len == 0) return;
  for (int i = 0; i < snapshot->completion_item_count; i++) {
    ag_language_symbol_t *symbol = &snapshot->completion_items[i];
    if (strlen(symbol->name) != name_len ||
        memcmp(symbol->name, name, name_len) != 0) continue;
    if (snapshot->hover_index < 0 ||
        symbol->kind == AG_LANGUAGE_SYMBOL_MACRO)
      snapshot->hover_index = i;
    if (symbol->kind == AG_LANGUAGE_SYMBOL_MACRO) return;
  }
}

static int copy_dependencies(
    snapshot_builder_t *builder, const ag_compilation_session_t *session) {
  int count =
      ag_compilation_session_virtual_header_dependency_count(session);
  if (count <= 0) return 1;
  builder->snapshot->dependencies = snapshot_alloc(
      builder, (size_t)count * sizeof(*builder->snapshot->dependencies));
  if (!builder->snapshot->dependencies) return 0;
  builder->snapshot->dependency_count = count;
  for (int i = 0; i < count; i++) {
    builder->snapshot->dependencies[i] = snapshot_copy(
        builder,
        ag_compilation_session_virtual_header_dependency_name_at(session, i));
    if (builder->failed) return 0;
  }
  return 1;
}

static int copy_diagnostics(snapshot_builder_t *builder,
                            const ag_diagnostic_context_t *diagnostics,
                            size_t cursor_offset) {
  int available = diag_context_record_count(diagnostics);
  int count = 0;
  for (int i = 0; i < available; i++) {
    int start = diag_context_record_start_offset(diagnostics, i);
    if (start < 0 || (size_t)start <= cursor_offset) count++;
  }
  if (count <= 0) return 1;
  builder->snapshot->diagnostics = snapshot_alloc(
      builder, (size_t)count * sizeof(*builder->snapshot->diagnostics));
  if (!builder->snapshot->diagnostics) return 0;
  builder->snapshot->diagnostic_count = count;
  int output = 0;
  for (int i = 0; i < available; i++) {
    int start = diag_context_record_start_offset(diagnostics, i);
    if (start >= 0 && (size_t)start > cursor_offset) continue;
    ag_language_diagnostic_t *diagnostic =
        &builder->snapshot->diagnostics[output++];
    diagnostic->severity = diag_context_record_severity(diagnostics, i);
    diagnostic->code = snapshot_copy(
        builder, diag_context_record_code(diagnostics, i));
    diagnostic->message = snapshot_copy(
        builder, diag_context_record_message(diagnostics, i));
    diagnostic->range.source_name = snapshot_copy(
        builder, diag_context_record_source_name(diagnostics, i));
    diagnostic->range.start = (ag_language_position_t){
        diag_context_record_start_line(diagnostics, i),
        diag_context_record_start_column(diagnostics, i), start};
    diagnostic->range.end = (ag_language_position_t){
        diag_context_record_end_line(diagnostics, i),
        diag_context_record_end_column(diagnostics, i),
        diag_context_record_end_offset(diagnostics, i)};
  }
  builder->snapshot->diagnostic_count = output;
  return !builder->failed;
}

static int append_partial_identifier_diagnostic(
    snapshot_builder_t *builder,
    const ag_language_analysis_request_t *request) {
  const char *name = NULL;
  size_t name_len = 0;
  identifier_at(request->source, request->source_length,
                request->cursor_byte_offset, &name, &name_len);
  if (!name || name_len == 0) return 1;
  int old_count = builder->snapshot->diagnostic_count;
  ag_language_diagnostic_t *next = snapshot_alloc(
      builder, (size_t)(old_count + 1) * sizeof(*next));
  if (!next) return 0;
  if (old_count > 0)
    memcpy(next, builder->snapshot->diagnostics,
           (size_t)old_count * sizeof(*next));
  free(builder->snapshot->diagnostics);
  builder->snapshot->diagnostics = next;
  builder->snapshot->diagnostic_count = old_count + 1;
  ag_language_diagnostic_t *diagnostic = &next[old_count];
  diagnostic->severity = 3;
  diagnostic->code = snapshot_copy(builder, "AGC_PARTIAL_IDENTIFIER");
  diagnostic->message = snapshot_copy(
      builder, "source ends with an incomplete identifier at the analysis cursor");
  diagnostic->range.source_name = snapshot_copy(builder, request->source_name);
  size_t start = (size_t)(name - request->source);
  diagnostic->range.start = position_at(
      request->source, request->source_length, start);
  diagnostic->range.end = position_at(
      request->source, request->source_length, start + name_len);
  return !builder->failed;
}

typedef struct {
  int severity;
  char *code;
  char *message;
  char *source_name;
  ag_language_position_t start;
  ag_language_position_t end;
} saved_analysis_diagnostic_t;

static char *analysis_strdup(const char *text) {
  if (!text) text = "";
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (!copy) return NULL;
  memcpy(copy, text, length + 1);
  return copy;
}

static void dispose_saved_diagnostic(saved_analysis_diagnostic_t *saved) {
  if (!saved) return;
  free(saved->code);
  free(saved->message);
  free(saved->source_name);
  memset(saved, 0, sizeof(*saved));
}

static int save_last_diagnostic(
    const ag_diagnostic_context_t *diagnostics,
    saved_analysis_diagnostic_t *saved) {
  if (!diagnostics || !saved) return 0;
  int index = diag_context_record_count(diagnostics) - 1;
  if (index < 0) return 0;
  *saved = (saved_analysis_diagnostic_t){
      .severity = diag_context_record_severity(diagnostics, index),
      .code = analysis_strdup(diag_context_record_code(diagnostics, index)),
      .message = analysis_strdup(diag_context_record_message(diagnostics, index)),
      .source_name = analysis_strdup(
          diag_context_record_source_name(diagnostics, index)),
      .start = {
          diag_context_record_start_line(diagnostics, index),
          diag_context_record_start_column(diagnostics, index),
          diag_context_record_start_offset(diagnostics, index),
      },
      .end = {
          diag_context_record_end_line(diagnostics, index),
          diag_context_record_end_column(diagnostics, index),
          diag_context_record_end_offset(diagnostics, index),
      },
  };
  if (saved->code && saved->message && saved->source_name) return 1;
  dispose_saved_diagnostic(saved);
  return 0;
}

static int append_saved_diagnostic(
    snapshot_builder_t *builder,
    const saved_analysis_diagnostic_t *saved) {
  if (!builder || !saved || !saved->code) return 1;
  int old_count = builder->snapshot->diagnostic_count;
  ag_language_diagnostic_t *next = snapshot_alloc(
      builder, (size_t)(old_count + 1) * sizeof(*next));
  if (!next) return 0;
  if (old_count > 0)
    memcpy(next, builder->snapshot->diagnostics,
           (size_t)old_count * sizeof(*next));
  free(builder->snapshot->diagnostics);
  builder->snapshot->diagnostics = next;
  builder->snapshot->diagnostic_count = old_count + 1;
  ag_language_diagnostic_t *diagnostic = &next[old_count];
  diagnostic->severity = saved->severity;
  diagnostic->code = snapshot_copy(builder, saved->code);
  diagnostic->message = snapshot_copy(builder, saved->message);
  diagnostic->range.source_name = snapshot_copy(builder, saved->source_name);
  diagnostic->range.start = saved->start;
  diagnostic->range.end = saved->end;
  return !builder->failed;
}

static int elide_failed_statement(
    char *source, size_t cursor,
    const saved_analysis_diagnostic_t *diagnostic,
    const char *source_name) {
  if (!source || !diagnostic || !diagnostic->source_name || !source_name ||
      strcmp(diagnostic->source_name, source_name) != 0 ||
      diagnostic->start.offset < 0 ||
      (size_t)diagnostic->start.offset >= cursor)
    return 0;
  size_t failed = (size_t)diagnostic->start.offset;
  size_t start = failed;
  while (start > 0 && source[start - 1] != ';' &&
         source[start - 1] != '{' && source[start - 1] != '}')
    start--;
  size_t end = failed;
  while (end < cursor && source[end] != ';' &&
         source[end] != '{' && source[end] != '}')
    end++;
  if (end >= cursor || source[end] != ';' || end <= start) return 0;
  for (size_t i = start; i < end; i++)
    if (source[i] != '\n' && source[i] != '\r') source[i] = ' ';
  return 1;
}

typedef struct {
  ag_compilation_session_t *session;
  tokenizer_context_t *tokenizer;
  const char *recovery_source;
  char *recovery_source_owned;
  ag_language_documentation_index_t *documentation_index_owned;
  pp_stream_t *preprocessor_stream;
  psx_frontend_stream_t frontend;
#if !defined(AGC_TARGET_WASM32) && !defined(__wasm32__)
  jmp_buf fatal_jump;
#endif
  int started;
  int fatal_recovered;
  int has_semantic_lookup_point;
  psx_scope_lookup_point_t semantic_lookup_point;
  saved_analysis_diagnostic_t semantic_diagnostic;
} analysis_parse_state_t;

static void cleanup_analysis_parse_state(void *context) {
  analysis_parse_state_t *state = context;
  if (!state) return;
  if (state->session) {
    ag_diagnostic_context_t *diagnostics =
        ag_compilation_session_diagnostic_context(state->session);
    ag_preprocessor_context_t *preprocessor =
        ag_compilation_session_preprocessor_context(state->session);
    if (diagnostics) {
      diag_context_clear_fatal_recovery(diagnostics);
      diag_context_set_capture_only(diagnostics, 0);
    }
    if (preprocessor)
      pp_context_set_language_analysis_mode(preprocessor, 0);
  }
  if (state->frontend.is_started)
    psx_frontend_stream_abort(&state->frontend);
  if (state->preprocessor_stream)
    pp_stream_close(state->preprocessor_stream);
  dispose_saved_diagnostic(&state->semantic_diagnostic);
  free(state->recovery_source_owned);
  if (state->documentation_index_owned) {
    ag_language_documentation_index_dispose(
        state->documentation_index_owned);
    free(state->documentation_index_owned);
  }
  free(state);
}

static analysis_parse_state_t *create_analysis_parse_state(
    ag_compilation_session_t *session, tokenizer_context_t *tokenizer,
    char *recovery_source,
    ag_language_documentation_index_t *documentation_index) {
  analysis_parse_state_t *state = calloc(1, sizeof(*state));
  if (!state) {
    free(recovery_source);
    if (documentation_index) {
      ag_language_documentation_index_dispose(documentation_index);
      free(documentation_index);
    }
    return NULL;
  }
  state->session = session;
  state->tokenizer = tokenizer;
  state->recovery_source = recovery_source;
  state->recovery_source_owned = recovery_source;
  state->documentation_index_owned = documentation_index;
  if (!ag_compilation_session_register_translation_unit_cleanup(
          session, cleanup_analysis_parse_state, state)) {
    cleanup_analysis_parse_state(state);
    return NULL;
  }
  return state;
}

static void finish_analysis_parse_state(
    analysis_parse_state_t *state) {
  if (!state) return;
  ag_compilation_session_t *session = state->session;
  if (session)
    (void)ag_compilation_session_unregister_translation_unit_cleanup(
        session, cleanup_analysis_parse_state, state);
  cleanup_analysis_parse_state(state);
}

static void save_semantic_rejection(
    analysis_parse_state_t *state,
    ag_compilation_session_t *session,
    const psx_resolved_hir_build_failure_t *failure,
    const token_t *fallback_token) {
  if (!state || state->semantic_diagnostic.code) return;
  const token_t *token = failure && failure->source_token
                             ? failure->source_token : fallback_token;
  ag_source_manager_t *sources = diag_context_source_manager(
      ag_compilation_session_diagnostic_context(session));
  char message[160];
  snprintf(message, sizeof(message),
           "semantic analysis stopped at an invalid incomplete expression "
           "(rejection %d)",
           failure ? (int)failure->rejection : 0);
  state->semantic_diagnostic = (saved_analysis_diagnostic_t){
      .severity = 1,
      .code = analysis_strdup("AGC_PARTIAL_SEMANTIC"),
      .message = analysis_strdup(message),
      .source_name = analysis_strdup(
          ag_source_manager_name(sources, token ? token->file_name_id : 0)),
  };
  if (!token || !token->source_input || token->byte_offset < 0) return;
  size_t source_length = strlen(token->source_input);
  size_t start = (size_t)token->byte_offset;
  size_t end = start + (size_t)(token->byte_length > 0
                                    ? token->byte_length : 0);
  state->semantic_diagnostic.start = position_at(
      token->source_input, source_length, start);
  state->semantic_diagnostic.end = position_at(
      token->source_input, source_length,
      end <= source_length ? end : source_length);
}

static int collect_analysis_function_declarations(
    void *context, ag_compilation_session_t *session,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  analysis_parse_state_t *state = context;
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  psx_resolved_hir_build_failure_t failure;
  psx_scope_lookup_point_t lookup_point;
  psx_syntax_typed_hir_resolution_status_t status =
      psx_collect_syntax_function_declarations_for_analysis_in_contexts(
          ag_compilation_session_semantic_context(session),
          ag_compilation_session_global_registry(session),
          ag_compilation_session_local_registry(session),
          ag_compilation_session_lowering_context(session),
          ag_compilation_session_options_view(session),
          syntax_function, &lookup_point, &failure);
  if (status == PSX_SYNTAX_TYPED_HIR_FAILED) return 0;
  state->semantic_lookup_point = lookup_point;
  state->has_semantic_lookup_point = 1;
  if (status == PSX_SYNTAX_TYPED_HIR_REJECTED)
    save_semantic_rejection(
        state, session, &failure, fallback_diag_tok);
  return 1;
}

#if !defined(AGC_TARGET_WASM32) && !defined(__wasm32__)
static void recover_analysis_fatal_diagnostic(void *context) {
  analysis_parse_state_t *state = context;
  state->fatal_recovered = 1;
  longjmp(state->fatal_jump, 1);
}
#endif

static void parse_analysis_source_body(analysis_parse_state_t *state) {
  token_t *tokens = pp_stream_open_in(
      ag_compilation_session_preprocessor_context(state->session),
      &state->preprocessor_stream, state->recovery_source);
  if (!tokens || !psx_frontend_stream_begin(
                     &state->frontend, state->session,
                     state->tokenizer, tokens))
    return;
  state->started = 1;
  psx_frontend_function_t function;
  while (psx_frontend_next_function_with_resolver(
             &state->frontend, &function,
             collect_analysis_function_declarations, state)) {
    /* Local payload stays alive until the public snapshot is copied. */
  }
  (void)psx_frontend_stream_end(&state->frontend);
}

static int parse_analysis_source(analysis_parse_state_t *state) {
  if (!state || !state->session || !state->tokenizer ||
      !state->recovery_source)
    return 0;
  ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(state->session);
  ag_preprocessor_context_t *preprocessor =
      ag_compilation_session_preprocessor_context(state->session);
  diag_context_set_capture_only(diagnostics, 1);
  pp_context_set_language_analysis_mode(preprocessor, 1);
#if !defined(AGC_TARGET_WASM32) && !defined(__wasm32__)
  diag_context_set_fatal_recovery(
      diagnostics, recover_analysis_fatal_diagnostic, state);
  if (setjmp(state->fatal_jump) == 0) {
    parse_analysis_source_body(state);
  }
  diag_context_clear_fatal_recovery(diagnostics);
#else
  parse_analysis_source_body(state);
#endif
  pp_context_set_language_analysis_mode(preprocessor, 0);
  diag_context_set_capture_only(diagnostics, 0);
  if (state->frontend.is_started)
    psx_frontend_stream_abort(&state->frontend);
  return state->started || state->fatal_recovered;
}

int ag_language_analyze_source(
    ag_compilation_session_t *session,
    const ag_language_analysis_request_t *request,
    ag_language_analysis_snapshot_t *snapshot,
    ag_language_analysis_error_t *error) {
  if (snapshot) memset(snapshot, 0, sizeof(*snapshot));
  if (error) memset(error, 0, sizeof(*error));
  if (!session) {
    set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
              "AGC_LANGUAGE_ANALYSIS_INVALID_REQUEST", NULL, 0, 0);
    return 0;
  }
  if (!ag_compilation_session_reset_translation_unit(session)) {
    set_error(error, AG_LANGUAGE_ANALYSIS_FAILED,
              "AGC_LANGUAGE_ANALYSIS_SESSION_RESET_FAILED", NULL, 0, 0);
    return 0;
  }
  diag_reset_records_in(
      ag_compilation_session_diagnostic_context(session));
  if (!snapshot || !request || !request->source_name ||
      !request->source_name[0] || !request->source ||
      !request->cursor_source_name ||
      strcmp(request->source_name, request->cursor_source_name) != 0 ||
      request->cursor_byte_offset > request->source_length ||
      memchr(request->source, '\0', request->source_length)) {
    set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
              "AGC_LANGUAGE_ANALYSIS_INVALID_REQUEST", NULL, 0, 0);
    return 0;
  }
  ag_language_analysis_limits_t limits = request->limits;
  if (!limits_are_valid(&limits)) {
    set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
              "AGC_LANGUAGE_ANALYSIS_INVALID_LIMITS", NULL, 0, 0);
    return 0;
  }
  int analysis_source_count = source_count(request);
  if (analysis_source_count > limits.max_sources) {
    set_error(error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
              "AGC_LIMIT_MAX_SOURCES", "maxSources",
              (size_t)limits.max_sources, (size_t)analysis_source_count);
    return 0;
  }
  if (request->source_length > limits.max_source_bytes) {
    set_error(error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
              "AGC_LIMIT_MAX_SOURCE_BYTES", "maxSourceBytes",
              limits.max_source_bytes, request->source_length);
    return 0;
  }
  size_t total_source_bytes = request->source_length;
  for (int index = 1; index < analysis_source_count; index++) {
    analysis_source_view_t source = {0};
    if (!source_at(request, index, &source)) continue;
    if (source.length > SIZE_MAX - total_source_bytes) {
      total_source_bytes = SIZE_MAX;
      break;
    }
    total_source_bytes += source.length;
  }
  if (total_source_bytes > limits.max_total_source_bytes) {
    set_error(error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
              "AGC_LIMIT_MAX_TOTAL_SOURCE_BYTES", "maxTotalSourceBytes",
              limits.max_total_source_bytes, total_source_bytes);
    return 0;
  }
  ag_language_documentation_index_t *documentation_index =
      calloc(1, sizeof(*documentation_index));
  if (!documentation_index) {
    set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
              "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    return 0;
  }
  if (!build_documentation_index(
          request, &limits, documentation_index, error)) {
    ag_language_documentation_index_dispose(documentation_index);
    free(documentation_index);
    return 0;
  }
  snapshot_builder_t builder = {
      .snapshot = snapshot,
      .error = error,
      .limits = limits,
      .documentation_index = documentation_index,
  };
  static const unsigned char empty_virtual_headers[4] = {0, 0, 0, 0};
  const unsigned char *header_bundle = request->virtual_header_bundle
                                           ? request->virtual_header_bundle
                                           : empty_virtual_headers;
  size_t header_bundle_length = request->virtual_header_bundle
                                    ? request->virtual_header_bundle_length
                                    : sizeof(empty_virtual_headers);
  pp_virtual_headers_configure_in(
      ag_compilation_session_preprocessor_context(session),
      header_bundle, header_bundle_length,
      request->max_header_files > 0 ? request->max_header_files : 128,
      request->max_header_file_bytes > 0
          ? request->max_header_file_bytes : 1024 * 1024,
      request->max_header_total_bytes > 0
          ? request->max_header_total_bytes : 4 * 1024 * 1024,
      request->max_include_depth > 0 ? request->max_include_depth : 32);
  int recovery_changed = 0;
  char *recovery_source = build_recovery_source(
      request->source, request->source_length,
      request->cursor_byte_offset, &recovery_changed);
  if (!recovery_source) {
    ag_language_documentation_index_dispose(documentation_index);
    free(documentation_index);
    set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
              "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    return 0;
  }
  tokenizer_context_t *tokenizer = ag_compilation_session_tokenizer(session);
  tk_set_filename_ctx(tokenizer, request->source_name);
  analysis_parse_state_t *parse_state = create_analysis_parse_state(
      session, tokenizer, recovery_source, documentation_index);
  if (!parse_state) {
    set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
              "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    return 0;
  }
  if (!parse_analysis_source(parse_state)) {
    finish_analysis_parse_state(parse_state);
    (void)ag_compilation_session_reset_translation_unit(session);
    set_error(error, AG_LANGUAGE_ANALYSIS_FAILED,
              "AGC_LANGUAGE_ANALYSIS_PARSE_START_FAILED", NULL, 0, 0);
    return 0;
  }
  saved_analysis_diagnostic_t saved_fatal = {0};
  analysis_parse_state_t *retry_state = NULL;
  analysis_parse_state_t *final_parse = parse_state;
  int recovered_before_retry = parse_state->fatal_recovered;
  int retry_attempted = 0;
  ag_diagnostic_context_t *diagnostic_context =
      ag_compilation_session_diagnostic_context(session);
  if (parse_state->fatal_recovered &&
      save_last_diagnostic(diagnostic_context, &saved_fatal) &&
      elide_failed_statement(
          recovery_source, request->cursor_byte_offset,
          &saved_fatal, request->source_name)) {
    parse_state->recovery_source_owned = NULL;
    parse_state->documentation_index_owned = NULL;
    finish_analysis_parse_state(parse_state);
    parse_state = NULL;
    if (!ag_compilation_session_reset_translation_unit(session)) {
      dispose_saved_diagnostic(&saved_fatal);
      free(recovery_source);
      ag_language_documentation_index_dispose(documentation_index);
      free(documentation_index);
      set_error(error, AG_LANGUAGE_ANALYSIS_FAILED,
                "AGC_LANGUAGE_ANALYSIS_SESSION_RESET_FAILED", NULL, 0, 0);
      return 0;
    }
    pp_virtual_headers_configure_in(
        ag_compilation_session_preprocessor_context(session),
        header_bundle, header_bundle_length,
        request->max_header_files > 0 ? request->max_header_files : 128,
        request->max_header_file_bytes > 0
            ? request->max_header_file_bytes : 1024 * 1024,
        request->max_header_total_bytes > 0
            ? request->max_header_total_bytes : 4 * 1024 * 1024,
        request->max_include_depth > 0 ? request->max_include_depth : 32);
    tk_set_filename_ctx(tokenizer, request->source_name);
    retry_state = create_analysis_parse_state(
        session, tokenizer, recovery_source, documentation_index);
    if (!retry_state) {
      dispose_saved_diagnostic(&saved_fatal);
      set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
                "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
      return 0;
    }
    retry_attempted = 1;
    final_parse = retry_state;
    if (!parse_analysis_source(retry_state)) {
      finish_analysis_parse_state(retry_state);
      dispose_saved_diagnostic(&saved_fatal);
      (void)ag_compilation_session_reset_translation_unit(session);
      set_error(error, AG_LANGUAGE_ANALYSIS_FAILED,
                "AGC_LANGUAGE_ANALYSIS_PARSE_START_FAILED", NULL, 0, 0);
      return 0;
    }
  }
  if (!retry_attempted) dispose_saved_diagnostic(&saved_fatal);

  psx_scope_graph_t *scope_graph = ag_compilation_session_scope_graph(session);
  const psx_scope_declaration_t *marker = NULL;
  size_t declaration_count = psx_scope_graph_declaration_count(scope_graph);
  for (size_t i = declaration_count; i > 0; i--) {
    const psx_scope_declaration_t *candidate =
        psx_scope_graph_declaration_at(scope_graph, i - 1);
    if (candidate && candidate->name &&
        strcmp(candidate->name, AG_LANGUAGE_CURSOR_MARKER) == 0) {
      marker = candidate;
      break;
    }
  }
  psx_scope_lookup_point_t point = marker
      ? (psx_scope_lookup_point_t){marker->scope_id,
                                   marker->declaration_order}
      : final_parse->has_semantic_lookup_point
            ? final_parse->semantic_lookup_point
            : psx_scope_graph_capture_lookup_point(scope_graph);
  psx_semantic_context_t *semantic_context =
      ag_compilation_session_semantic_context(session);
  int symbol_capacity = 0;
  for (size_t i = 0; i < declaration_count && !builder.failed; i++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i);
    if (!declaration || !declaration->name ||
        strcmp(declaration->name, AG_LANGUAGE_CURSOR_MARKER) == 0 ||
        (declaration->name_len >= 6 &&
         memcmp(declaration->name, "__agc_", 6) == 0) ||
        declaration->kind == PSX_DECL_LABEL ||
        declaration->kind == PSX_DECL_MEMBER)
      continue;
    psx_decl_id_t resolved = psx_scope_graph_lookup(
        scope_graph, declaration->name_space, declaration->name,
        declaration->name_len, point);
    if (resolved != declaration->id) continue;
    add_declaration_symbol(&builder, request, semantic_context,
                           declaration, point, &symbol_capacity);
  }
  ag_language_documentation_index_dispose(documentation_index);
  free(documentation_index);
  final_parse->documentation_index_owned = NULL;
  builder.documentation_index = NULL;
  if (!builder.failed)
    add_member_symbols(&builder, request, semantic_context,
                       point, &symbol_capacity);
  if (!builder.failed)
    add_macro_symbols(&builder, request,
                      ag_compilation_session_preprocessor_context(session),
                      &symbol_capacity);
  if (!builder.failed)
    copy_dependencies(&builder, session);
  const ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(session);
  if (!builder.failed)
    copy_diagnostics(&builder, diagnostics, request->cursor_byte_offset);
  if (!builder.failed)
    append_saved_diagnostic(&builder, &saved_fatal);
  if (!builder.failed)
    append_saved_diagnostic(
        &builder, &final_parse->semantic_diagnostic);
  if (!builder.failed &&
      (recovery_changed & AG_LANGUAGE_RECOVERY_PARTIAL_IDENTIFIER))
    append_partial_identifier_diagnostic(&builder, request);
  if (builder.failed) {
    dispose_saved_diagnostic(&saved_fatal);
    finish_analysis_parse_state(parse_state);
    finish_analysis_parse_state(retry_state);
    (void)ag_compilation_session_reset_translation_unit(session);
    ag_language_analysis_snapshot_dispose(snapshot);
    return 0;
  }
  qsort(snapshot->completion_items,
        (size_t)snapshot->completion_item_count,
        sizeof(*snapshot->completion_items), symbol_compare);
  const char *hover_name = NULL;
  size_t hover_name_len = 0;
  identifier_at(request->source, request->source_length,
                request->cursor_byte_offset, &hover_name, &hover_name_len);
  select_hover(snapshot, hover_name, hover_name_len);
  int unresolved_elided_identifier =
      (recovery_changed &
       AG_LANGUAGE_RECOVERY_COMPLETE_IDENTIFIER_ELIDED) &&
      snapshot->hover_index < 0;
  int has_error = 0;
  for (int i = 0; i < snapshot->diagnostic_count; i++)
    if (snapshot->diagnostics[i].severity == 1) has_error = 1;
  snapshot->partial =
                      has_error || !marker ||
                      (recovery_changed &
                       AG_LANGUAGE_RECOVERY_PARTIAL_IDENTIFIER) ||
                      unresolved_elided_identifier ||
                      final_parse->fatal_recovered || recovered_before_retry ||
                      final_parse->semantic_diagnostic.code != NULL;
  dispose_saved_diagnostic(&saved_fatal);
  finish_analysis_parse_state(parse_state);
  finish_analysis_parse_state(retry_state);
  if (error) error->status = AG_LANGUAGE_ANALYSIS_OK;
  return 1;
}

static void project_range_dispose(
    ag_language_source_range_t *range) {
  if (!range) return;
  free(range->source_name);
  *range = (ag_language_source_range_t){0};
}

static int project_range_copy(
    ag_language_source_range_t *destination,
    const ag_language_source_range_t *source) {
  if (!destination || !source || !source->source_name) return 0;
  size_t name_length = strlen(source->source_name);
  char *name = malloc(name_length + 1);
  if (!name) return 0;
  memcpy(name, source->source_name, name_length + 1);
  *destination = *source;
  destination->source_name = name;
  return 1;
}

static int project_ranges_equal(
    const ag_language_source_range_t *left,
    const ag_language_source_range_t *right) {
  return left && right && left->source_name && right->source_name &&
         strcmp(left->source_name, right->source_name) == 0 &&
         left->start.offset == right->start.offset &&
         left->end.offset == right->end.offset;
}

static void project_function_dispose(
    project_function_entry_t *function) {
  if (!function) return;
  free(function->name);
  free(function->documentation);
  project_range_dispose(&function->declaration);
  project_range_dispose(&function->documentation_range);
  for (int i = 0; i < function->definition_count; i++)
    project_range_dispose(&function->definitions[i]);
  free(function->definitions);
  *function = (project_function_entry_t){0};
}

static void project_index_clear(
    ag_language_project_index_t *index) {
  if (!index) return;
  for (int i = 0; i < index->function_count; i++)
    project_function_dispose(&index->functions[i]);
  free(index->functions);
  index->functions = NULL;
  index->function_count = 0;
  index->function_capacity = 0;
  index->definition_count = 0;
  index->valid = 0;
}

static void project_index_abort_pending(
    ag_language_project_index_t *index) {
  if (!index || !index->pending) return;
  ag_language_project_index_t *pending = index->pending;
  index->pending = NULL;
  project_index_clear(pending);
  free(pending);
}

ag_language_project_index_t *ag_language_project_index_create(void) {
  return calloc(1, sizeof(ag_language_project_index_t));
}

void ag_language_project_index_destroy(
    ag_language_project_index_t *index) {
  if (!index) return;
  project_index_abort_pending(index);
  project_index_clear(index);
  free(index);
}

unsigned int ag_language_project_index_revision(
    const ag_language_project_index_t *index) {
  return index && index->valid ? index->revision : 0;
}

static project_function_entry_t *project_find_function(
    ag_language_project_index_t *index, const char *name) {
  if (!index || !name) return NULL;
  for (int i = 0; i < index->function_count; i++)
    if (strcmp(index->functions[i].name, name) == 0)
      return &index->functions[i];
  return NULL;
}

static const project_function_entry_t *project_find_function_const(
    const ag_language_project_index_t *index, const char *name) {
  return project_find_function(
      (ag_language_project_index_t *)index, name);
}

static int symbol_documentation_is_from_definition(
    const ag_language_symbol_t *symbol) {
  if (!symbol || symbol->kind != AG_LANGUAGE_SYMBOL_FUNCTION ||
      !symbol->documentation || !symbol->documentation[0] ||
      !symbol->has_documentation_range || !symbol->has_definition)
    return 0;
  if (source_ranges_identical(&symbol->declaration, &symbol->definition))
    return 1;
  if (!symbol->documentation_range.source_name ||
      !symbol->definition.source_name ||
      strcmp(symbol->documentation_range.source_name,
             symbol->definition.source_name) != 0)
    return 0;
  if (!symbol->declaration.source_name ||
      strcmp(symbol->declaration.source_name,
             symbol->definition.source_name) != 0)
    return 1;
  return symbol->documentation_range.start.offset >=
         symbol->declaration.end.offset;
}

static int project_store_documentation(
    project_function_entry_t *entry,
    const ag_language_symbol_t *symbol) {
  if (!entry || !symbol || !symbol->documentation ||
      !symbol->documentation[0])
    return 1;
  int priority = symbol_documentation_is_from_definition(symbol) ? 1 : 2;
  if (entry->documentation_priority >= priority) return 1;
  size_t length = strlen(symbol->documentation);
  char *documentation = malloc(length + 1);
  ag_language_source_range_t range = {0};
  if (!documentation ||
      (symbol->has_documentation_range &&
       !project_range_copy(&range, &symbol->documentation_range))) {
    free(documentation);
    project_range_dispose(&range);
    return 0;
  }
  memcpy(documentation, symbol->documentation, length + 1);
  free(entry->documentation);
  project_range_dispose(&entry->documentation_range);
  entry->documentation = documentation;
  entry->has_documentation_range = symbol->has_documentation_range;
  entry->documentation_range = range;
  entry->documentation_priority = priority;
  return 1;
}

static project_function_entry_t *project_add_function(
    ag_language_project_index_t *index,
    const ag_language_symbol_t *symbol,
    int max_symbols) {
  if (!index || !symbol || !symbol->name ||
      index->function_count >= max_symbols)
    return NULL;
  if (index->function_count == index->function_capacity) {
    int next_capacity = index->function_capacity
                            ? index->function_capacity * 2 : 32;
    if (next_capacity < index->function_capacity ||
        next_capacity > max_symbols)
      next_capacity = max_symbols;
    project_function_entry_t *next = realloc(
        index->functions,
        (size_t)next_capacity * sizeof(*next));
    if (!next) return NULL;
    memset(next + index->function_capacity, 0,
           (size_t)(next_capacity - index->function_capacity) *
               sizeof(*next));
    index->functions = next;
    index->function_capacity = next_capacity;
  }
  project_function_entry_t *entry =
      &index->functions[index->function_count++];
  size_t name_length = strlen(symbol->name);
  entry->name = malloc(name_length + 1);
  if (!entry->name ||
      !project_range_copy(&entry->declaration,
                          &symbol->declaration) ||
      !project_store_documentation(entry, symbol)) {
    project_function_dispose(entry);
    index->function_count--;
    return NULL;
  }
  memcpy(entry->name, symbol->name, name_length + 1);
  return entry;
}

static int project_add_definition(
    ag_language_project_index_t *index,
    project_function_entry_t *entry,
    const ag_language_source_range_t *definition,
    int max_symbols, int *limit_exceeded) {
  if (!index || !entry || !definition || !definition->source_name)
    return 0;
  for (int i = 0; i < entry->definition_count; i++)
    if (project_ranges_equal(&entry->definitions[i], definition))
      return 1;
  if (index->definition_count >= max_symbols) {
    if (limit_exceeded) *limit_exceeded = 1;
    return 0;
  }
  if (entry->definition_count == entry->definition_capacity) {
    int next_capacity = entry->definition_capacity
                            ? entry->definition_capacity * 2 : 2;
    if (next_capacity > max_symbols) next_capacity = max_symbols;
    ag_language_source_range_t *next = realloc(
        entry->definitions,
        (size_t)next_capacity * sizeof(*next));
    if (!next) return 0;
    memset(next + entry->definition_capacity, 0,
           (size_t)(next_capacity - entry->definition_capacity) *
               sizeof(*next));
    entry->definitions = next;
    entry->definition_capacity = next_capacity;
  }
  if (!project_range_copy(
          &entry->definitions[entry->definition_count], definition))
    return 0;
  entry->definition_count++;
  index->definition_count++;
  return 1;
}

static int project_merge_snapshot(
    ag_language_project_index_t *index,
    const ag_language_analysis_snapshot_t *snapshot,
    int max_symbols, int *limit_exceeded) {
  if (!index || !snapshot) return 0;
  for (int i = 0; i < snapshot->completion_item_count; i++) {
    const ag_language_symbol_t *symbol =
        &snapshot->completion_items[i];
    if (symbol->kind != AG_LANGUAGE_SYMBOL_FUNCTION ||
        symbol->scope_depth != 0 ||
        (symbol->storage_class &&
         strcmp(symbol->storage_class, "static") == 0))
      continue;
    project_function_entry_t *entry =
        project_find_function(index, symbol->name);
    if (!entry && index->function_count >= max_symbols) {
      if (limit_exceeded) *limit_exceeded = 1;
      return 0;
    }
    if (!entry)
      entry = project_add_function(index, symbol, max_symbols);
    if (!entry) return 0;
    if (!project_store_documentation(entry, symbol)) return 0;
    for (int candidate = 0;
         candidate < symbol->definition_candidate_count; candidate++)
      if (!project_add_definition(
              index, entry,
              &symbol->definition_candidates[candidate],
              max_symbols, limit_exceeded))
        return 0;
    if (symbol->has_definition &&
        symbol->definition_candidate_count == 0 &&
        !project_add_definition(
            index, entry, &symbol->definition, max_symbols,
            limit_exceeded))
      return 0;
  }
  return 1;
}

int ag_language_project_index_update(
    ag_compilation_session_t *session,
    ag_language_project_index_t *index,
    const ag_language_project_update_request_t *request,
    ag_language_analysis_error_t *error) {
  if (error) memset(error, 0, sizeof(*error));
  project_index_abort_pending(index);
  if (!session || !index || !request || request->revision == 0 ||
      !request->sources || request->source_count <= 0) {
    set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
              "AGC_LANGUAGE_ANALYSIS_INVALID_PROJECT", NULL, 0, 0);
    return 0;
  }
  if (index->valid && index->revision == request->revision) {
    if (error) error->status = AG_LANGUAGE_ANALYSIS_OK;
    return 1;
  }
  ag_language_analysis_limits_t limits = request->limits;
  if (!limits_are_valid(&limits)) {
    set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
              "AGC_LANGUAGE_ANALYSIS_INVALID_LIMITS", NULL, 0, 0);
    return 0;
  }
  if (request->source_count > limits.max_sources) {
    set_error(error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
              "AGC_LIMIT_MAX_SOURCES", "maxSources",
              (size_t)limits.max_sources,
              (size_t)request->source_count);
    return 0;
  }
  size_t total_source_bytes = 0;
  for (int i = 0; i < request->source_count; i++) {
    const ag_language_project_source_t *source = &request->sources[i];
    if (!source->source_name || !source->source_name[0] ||
        !source->source ||
        memchr(source->source, '\0', source->source_length)) {
      set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
                "AGC_LANGUAGE_ANALYSIS_INVALID_PROJECT_SOURCE",
                NULL, 0, 0);
      return 0;
    }
    for (int previous = 0; previous < i; previous++) {
      const char *previous_name =
          request->sources[previous].source_name;
      if (previous_name &&
          strcmp(previous_name, source->source_name) == 0) {
        set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
                  "AGC_LANGUAGE_ANALYSIS_DUPLICATE_PROJECT_SOURCE",
                  NULL, 0, 0);
        return 0;
      }
    }
    if (source->source_length > limits.max_source_bytes) {
      set_error(error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
                "AGC_LIMIT_MAX_SOURCE_BYTES", "maxSourceBytes",
                limits.max_source_bytes, source->source_length);
      return 0;
    }
    if (source->source_length > SIZE_MAX - total_source_bytes) {
      total_source_bytes = SIZE_MAX;
      break;
    }
    total_source_bytes += source->source_length;
  }
  if (total_source_bytes > limits.max_total_source_bytes) {
    set_error(error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
              "AGC_LIMIT_MAX_TOTAL_SOURCE_BYTES",
              "maxTotalSourceBytes", limits.max_total_source_bytes,
              total_source_bytes);
    return 0;
  }

  ag_language_project_index_t *next = calloc(1, sizeof(*next));
  if (!next) {
    set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
              "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    return 0;
  }
  next->revision = request->revision;
  index->pending = next;
  for (int i = 0; i < request->source_count; i++) {
    const ag_language_project_source_t *source = &request->sources[i];
    ag_language_analysis_snapshot_t snapshot = {0};
    ag_language_analysis_error_t analysis_error = {0};
    int project_limit_exceeded = 0;
    int ok = ag_language_analyze_source(
        session,
        &(ag_language_analysis_request_t){
            .source_name = source->source_name,
            .source = source->source,
            .source_length = source->source_length,
            .cursor_source_name = source->source_name,
            .cursor_byte_offset = source->source_length,
            .virtual_header_bundle = request->virtual_header_bundle,
            .virtual_header_bundle_length =
                request->virtual_header_bundle_length,
            .max_header_files = request->max_header_files,
            .max_header_file_bytes = request->max_header_file_bytes,
            .max_header_total_bytes = request->max_header_total_bytes,
            .max_include_depth = request->max_include_depth,
            .limits = limits,
        },
        &snapshot, &analysis_error);
    const char *diagnostic_code = NULL;
    if (ok) {
      for (int diagnostic_index = 0;
           diagnostic_index < snapshot.diagnostic_count;
           diagnostic_index++) {
        if (snapshot.diagnostics[diagnostic_index].severity == 1) {
          diagnostic_code = snapshot.diagnostics[diagnostic_index].code;
          break;
        }
      }
    }
    if (!ok || diagnostic_code || !project_merge_snapshot(
                   next, &snapshot, limits.max_symbols,
                   &project_limit_exceeded)) {
      if (diagnostic_code)
        set_error(error, AG_LANGUAGE_ANALYSIS_FAILED,
                  diagnostic_code, NULL, 0, 0);
      ag_language_analysis_snapshot_dispose(&snapshot);
      project_index_abort_pending(index);
      if (error && !diagnostic_code) {
        if (!ok) {
          *error = analysis_error;
        } else if (project_limit_exceeded) {
          set_error(error, AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
                    "AGC_LIMIT_MAX_ANALYSIS_SYMBOLS",
                    "maxAnalysisSymbols",
                    (size_t)limits.max_symbols,
                    (size_t)limits.max_symbols + 1);
        } else {
          set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
                    "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL,
                    0, 0);
        }
      }
      return 0;
    }
    ag_language_analysis_snapshot_dispose(&snapshot);
  }
  index->pending = NULL;
  project_index_clear(index);
  *index = *next;
  index->pending = NULL;
  free(next);
  index->valid = 1;
  if (error) error->status = AG_LANGUAGE_ANALYSIS_OK;
  return 1;
}

static int snapshot_copy_project_range(
    snapshot_builder_t *builder,
    ag_language_source_range_t *destination,
    const ag_language_source_range_t *source) {
  if (!builder || !destination || !source || !source->source_name)
    return 0;
  destination->source_name = snapshot_copy(builder, source->source_name);
  destination->start = source->start;
  destination->end = source->end;
  return !builder->failed;
}

int ag_language_analyze_project_source(
    ag_compilation_session_t *session,
    const ag_language_project_index_t *index,
    const ag_language_analysis_request_t *request,
    ag_language_analysis_snapshot_t *snapshot,
    ag_language_analysis_error_t *error) {
  if (!index || !index->valid) {
    if (snapshot) memset(snapshot, 0, sizeof(*snapshot));
    set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
              "AGC_LANGUAGE_ANALYSIS_PROJECT_NOT_INDEXED", NULL,
              0, 0);
    return 0;
  }
  if (!ag_language_analyze_source(
          session, request, snapshot, error))
    return 0;
  ag_language_analysis_limits_t limits = request->limits;
  if (!limits_are_valid(&limits)) {
    ag_language_analysis_snapshot_dispose(snapshot);
    set_error(error, AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
              "AGC_LANGUAGE_ANALYSIS_INVALID_LIMITS", NULL, 0, 0);
    return 0;
  }
  snapshot_builder_t builder = {
      .snapshot = snapshot,
      .error = error,
      .limits = limits,
  };
  for (int i = 0; i < snapshot->completion_item_count; i++) {
    ag_language_symbol_t *symbol = &snapshot->completion_items[i];
    if (symbol->kind != AG_LANGUAGE_SYMBOL_FUNCTION ||
        (symbol->storage_class &&
         strcmp(symbol->storage_class, "static") == 0))
      continue;
    const project_function_entry_t *entry =
        project_find_function_const(index, symbol->name);
    if (!entry) continue;
    int has_visible_prototype_documentation =
        symbol->documentation && symbol->documentation[0] &&
        !symbol_documentation_is_from_definition(symbol);
    if (!has_visible_prototype_documentation &&
        entry->documentation && entry->documentation[0]) {
      free(symbol->documentation);
      project_range_dispose(&symbol->documentation_range);
      symbol->documentation = snapshot_copy(
          &builder, entry->documentation);
      symbol->has_documentation_range = entry->has_documentation_range;
      if (entry->has_documentation_range)
        snapshot_copy_project_range(
            &builder, &symbol->documentation_range,
            &entry->documentation_range);
    }
    if (entry->definition_count == 0) continue;
    project_range_dispose(&symbol->definition);
    for (int candidate = 0;
         candidate < symbol->definition_candidate_count; candidate++)
      project_range_dispose(&symbol->definition_candidates[candidate]);
    free(symbol->definition_candidates);
    symbol->definition = (ag_language_source_range_t){0};
    symbol->has_definition = 0;
    symbol->definition_conflict = entry->definition_count > 1;
    symbol->definition_candidate_count = entry->definition_count;
    symbol->definition_candidates = snapshot_alloc(
        &builder, (size_t)entry->definition_count *
                      sizeof(*symbol->definition_candidates));
    for (int candidate = 0;
         candidate < entry->definition_count && !builder.failed;
         candidate++)
      snapshot_copy_project_range(
          &builder, &symbol->definition_candidates[candidate],
          &entry->definitions[candidate]);
    if (entry->definition_count == 1 && !builder.failed) {
      symbol->has_definition = snapshot_copy_project_range(
          &builder, &symbol->definition, &entry->definitions[0]);
    }
  }
  if (builder.failed) {
    ag_language_analysis_snapshot_dispose(snapshot);
    return 0;
  }
  if (error) error->status = AG_LANGUAGE_ANALYSIS_OK;
  return 1;
}

static void dispose_range(ag_language_source_range_t *range) {
  if (!range) return;
  free(range->source_name);
}

void ag_language_analysis_snapshot_dispose(
    ag_language_analysis_snapshot_t *snapshot) {
  if (!snapshot) return;
  for (int i = 0; i < snapshot->completion_item_count; i++) {
    ag_language_symbol_t *symbol = &snapshot->completion_items[i];
    free(symbol->name);
    free(symbol->type);
    free(symbol->signature);
    free(symbol->return_type);
    free(symbol->storage_class);
    free(symbol->documentation);
    free(symbol->constant_value);
    free(symbol->macro_replacement);
    dispose_range(&symbol->declaration);
    dispose_range(&symbol->documentation_range);
    dispose_range(&symbol->definition);
    for (int candidate = 0;
         candidate < symbol->definition_candidate_count; candidate++)
      dispose_range(&symbol->definition_candidates[candidate]);
    free(symbol->definition_candidates);
    dispose_range(&symbol->initializer_range);
    for (int p = 0; p < symbol->parameter_count; p++) {
      free(symbol->parameters[p].name);
      free(symbol->parameters[p].type);
    }
    free(symbol->parameters);
    for (int p = 0; p < symbol->macro_parameter_count; p++)
      free(symbol->macro_parameters[p]);
    free(symbol->macro_parameters);
  }
  free(snapshot->completion_items);
  for (int i = 0; i < snapshot->dependency_count; i++)
    free(snapshot->dependencies[i]);
  free(snapshot->dependencies);
  for (int i = 0; i < snapshot->diagnostic_count; i++) {
    free(snapshot->diagnostics[i].code);
    free(snapshot->diagnostics[i].message);
    dispose_range(&snapshot->diagnostics[i].range);
  }
  free(snapshot->diagnostics);
  memset(snapshot, 0, sizeof(*snapshot));
}

typedef struct {
  char *out;
  size_t capacity;
  size_t length;
  int failed;
} json_writer_t;

static void json_bytes(json_writer_t *writer, const char *bytes, size_t length) {
  if (!writer || writer->failed || !bytes ||
      writer->length > (size_t)INT_MAX - length) {
    if (writer) writer->failed = 1;
    return;
  }
  if (writer->out && writer->length < writer->capacity) {
    size_t writable = writer->capacity - writer->length;
    if (writable > length) writable = length;
    memcpy(writer->out + writer->length, bytes, writable);
  }
  writer->length += length;
}

static void json_literal(json_writer_t *writer, const char *literal) {
  json_bytes(writer, literal, strlen(literal));
}

static void json_int(json_writer_t *writer, long long value) {
  char number[64];
  snprintf(number, sizeof(number), "%lld", value);
  json_literal(writer, number);
}

static void json_string(json_writer_t *writer, const char *text) {
  static const char hex[] = "0123456789abcdef";
  json_literal(writer, "\"");
  if (!text) text = "";
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    unsigned char c = *p;
    if (c == '"') json_literal(writer, "\\\"");
    else if (c == '\\') json_literal(writer, "\\\\");
    else if (c == '\n') json_literal(writer, "\\n");
    else if (c == '\r') json_literal(writer, "\\r");
    else if (c == '\t') json_literal(writer, "\\t");
    else if (c < 0x20) {
      char escape[6] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
      json_bytes(writer, escape, sizeof(escape));
    } else {
      json_bytes(writer, (const char *)p, 1);
    }
  }
  json_literal(writer, "\"");
}

static void json_position(json_writer_t *writer,
                          ag_language_position_t position) {
  json_literal(writer, "{\"line\":"); json_int(writer, position.line);
  json_literal(writer, ",\"column\":"); json_int(writer, position.column);
  json_literal(writer, ",\"offset\":"); json_int(writer, position.offset);
  json_literal(writer, "}");
}

static void json_range(json_writer_t *writer,
                       const ag_language_source_range_t *range) {
  json_literal(writer, "{\"sourceName\":");
  json_string(writer, range->source_name);
  json_literal(writer, ",\"start\":"); json_position(writer, range->start);
  json_literal(writer, ",\"end\":"); json_position(writer, range->end);
  json_literal(writer, "}");
}

static void json_symbol(json_writer_t *writer,
                        const ag_language_symbol_t *symbol) {
  json_literal(writer, "{\"name\":"); json_string(writer, symbol->name);
  json_literal(writer, ",\"kind\":");
  json_string(writer, symbol_kind_name(symbol->kind));
  json_literal(writer, ",\"nameSpace\":");
  json_string(writer, namespace_name(symbol->name_space));
  json_literal(writer, ",\"type\":"); json_string(writer, symbol->type);
  json_literal(writer, ",\"signature\":");
  json_string(writer, symbol->signature);
  json_literal(writer, ",\"storageClass\":");
  json_string(writer, symbol->storage_class);
  json_literal(writer, ",\"scopeDepth\":");
  json_int(writer, symbol->scope_depth);
  json_literal(writer, ",\"declarationOrder\":");
  json_int(writer, symbol->declaration_order);
  json_literal(writer, ",\"declaration\":");
  json_range(writer, &symbol->declaration);
  json_literal(writer, ",\"documentation\":");
  json_string(writer, symbol->documentation);
  json_literal(writer, ",\"documentationRange\":");
  if (symbol->has_documentation_range)
    json_range(writer, &symbol->documentation_range);
  else
    json_literal(writer, "null");
  json_literal(writer, ",\"definition\":");
  if (symbol->has_definition)
    json_range(writer, &symbol->definition);
  else
    json_literal(writer, "null");
  json_literal(writer, ",\"definitionConflict\":");
  json_literal(writer, symbol->definition_conflict ? "true" : "false");
  json_literal(writer, ",\"definitionCandidates\":[");
  for (int i = 0; i < symbol->definition_candidate_count; i++) {
    if (i) json_literal(writer, ",");
    json_range(writer, &symbol->definition_candidates[i]);
  }
  json_literal(writer, "]");
  json_literal(writer, ",\"initializer\":{\"state\":");
  json_string(writer, initializer_name(symbol->initializer_state));
  json_literal(writer, ",\"constantValue\":");
  if (symbol->constant_value && symbol->constant_value[0])
    json_string(writer, symbol->constant_value);
  else json_literal(writer, "null");
  json_literal(writer, ",\"range\":");
  if (symbol->has_initializer_range)
    json_range(writer, &symbol->initializer_range);
  else
    json_literal(writer, "null");
  json_literal(writer, "}");
  json_literal(writer, ",\"function\":");
  if (symbol->kind == AG_LANGUAGE_SYMBOL_FUNCTION) {
    json_literal(writer, "{\"returnType\":");
    json_string(writer, symbol->return_type);
    json_literal(writer, ",\"hasPrototype\":");
    json_literal(writer, symbol->has_function_prototype ? "true" : "false");
    json_literal(writer, ",\"variadic\":");
    json_literal(writer, symbol->is_variadic ? "true" : "false");
    json_literal(writer, ",\"parameters\":[");
    for (int i = 0; i < symbol->parameter_count; i++) {
      if (i) json_literal(writer, ",");
      json_literal(writer, "{\"name\":");
      json_string(writer, symbol->parameters[i].name);
      json_literal(writer, ",\"type\":");
      json_string(writer, symbol->parameters[i].type);
      json_literal(writer, "}");
    }
    json_literal(writer, "]}");
  } else json_literal(writer, "null");
  json_literal(writer, ",\"macro\":");
  if (symbol->kind == AG_LANGUAGE_SYMBOL_MACRO) {
    json_literal(writer, "{\"functionLike\":");
    json_literal(writer, symbol->macro_is_function_like ? "true" : "false");
    json_literal(writer, ",\"variadic\":");
    json_literal(writer, symbol->macro_is_variadic ? "true" : "false");
    json_literal(writer, ",\"parameters\":[");
    for (int i = 0; i < symbol->macro_parameter_count; i++) {
      if (i) json_literal(writer, ",");
      json_string(writer, symbol->macro_parameters[i]);
    }
    json_literal(writer, "],\"replacement\":");
    json_string(writer, symbol->macro_replacement);
    json_literal(writer, "}");
  } else json_literal(writer, "null");
  json_literal(writer, "}");
}

int ag_language_analysis_snapshot_write_json(
    const ag_language_analysis_snapshot_t *snapshot,
    char *out, size_t out_size) {
  if (!snapshot) return -1;
  json_writer_t writer = {out, out_size > 0 ? out_size - 1 : 0, 0, 0};
  json_literal(&writer, "{\"diagnostics\":[");
  for (int i = 0; i < snapshot->diagnostic_count; i++) {
    const ag_language_diagnostic_t *diagnostic = &snapshot->diagnostics[i];
    if (i) json_literal(&writer, ",");
    json_literal(&writer, "{\"severity\":");
    json_string(&writer, diagnostic->severity == 1 ? "error" :
                         diagnostic->severity == 2 ? "warning" : "note");
    json_literal(&writer, ",\"code\":"); json_string(&writer, diagnostic->code);
    json_literal(&writer, ",\"message\":");
    json_string(&writer, diagnostic->message);
    json_literal(&writer, ",\"sourceId\":0,\"sourceName\":");
    json_string(&writer, diagnostic->range.source_name);
    json_literal(&writer, ",\"start\":");
    json_position(&writer, diagnostic->range.start);
    json_literal(&writer, ",\"end\":");
    json_position(&writer, diagnostic->range.end);
    json_literal(&writer, ",\"notes\":[]}");
  }
  json_literal(&writer, "],\"completionItems\":[");
  for (int i = 0; i < snapshot->completion_item_count; i++) {
    if (i) json_literal(&writer, ",");
    json_symbol(&writer, &snapshot->completion_items[i]);
  }
  json_literal(&writer, "],\"hover\":");
  if (snapshot->hover_index >= 0 &&
      snapshot->hover_index < snapshot->completion_item_count)
    json_symbol(&writer, &snapshot->completion_items[snapshot->hover_index]);
  else json_literal(&writer, "null");
  json_literal(&writer, ",\"dependencies\":[");
  for (int i = 0; i < snapshot->dependency_count; i++) {
    if (i) json_literal(&writer, ",");
    json_string(&writer, snapshot->dependencies[i]);
  }
  json_literal(&writer, "]");
  json_literal(&writer, ",\"partial\":");
  json_literal(&writer, snapshot->partial ? "true" : "false");
  json_literal(&writer, "}");
  if (out && out_size > 0) {
    size_t end = writer.length < out_size ? writer.length : out_size - 1;
    out[end] = '\0';
  }
  if (writer.failed || writer.length > (size_t)INT_MAX) return -1;
  if (out && writer.length + 1 > out_size) return -2;
  return (int)writer.length;
}

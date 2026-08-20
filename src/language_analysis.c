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
#include "parser/ast.h"
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
  const ag_language_analysis_request_t *request;
  const ag_language_documentation_index_t *documentation_index;
  int enable_trigraphs;
  int primary_needs_offset_mapping;
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

static int is_identifier_start_byte(unsigned char byte) {
  return byte == '_' || byte >= 0x80 || isalpha(byte);
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

static size_t analysis_line_splice_size_mode(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs) {
  if (!source || cursor >= length) return 0;
  if (source[cursor] == '\\') {
    if (cursor + 1 < length && source[cursor + 1] == '\n') return 2;
    if (cursor + 2 < length && source[cursor + 1] == '\r' &&
        source[cursor + 2] == '\n')
      return 3;
  }
  if (enable_trigraphs && cursor + 3 < length && source[cursor] == '?' &&
      source[cursor + 1] == '?' && source[cursor + 2] == '/') {
    if (source[cursor + 3] == '\n') return 4;
    if (cursor + 4 < length && source[cursor + 3] == '\r' &&
        source[cursor + 4] == '\n')
      return 5;
  }
  return 0;
}

typedef struct {
  size_t start;
  size_t end;
  size_t logical_length;
} analysis_identifier_span_t;

static int analysis_line_splice_before(
    const char *source, size_t length, size_t end,
    int enable_trigraphs, size_t *start) {
  size_t max_width = enable_trigraphs ? 5 : 3;
  for (size_t width = 2; width <= max_width && width <= end; width++) {
    size_t candidate = end - width;
    if (analysis_line_splice_size_mode(
            source, length, candidate, enable_trigraphs) == width) {
      if (start) *start = candidate;
      return 1;
    }
  }
  return 0;
}

static int analysis_identifier_span_at_mode(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, analysis_identifier_span_t *span) {
  if (span) *span = (analysis_identifier_span_t){0};
  if (!source || !span || cursor > length) return 0;
  size_t selected = SIZE_MAX;
  if (cursor < length &&
      is_identifier_byte((unsigned char)source[cursor])) {
    selected = cursor;
  } else if (cursor > 0 &&
             is_identifier_byte((unsigned char)source[cursor - 1])) {
    selected = cursor - 1;
  } else {
    size_t search_start = cursor > 5 ? cursor - 5 : 0;
    for (size_t candidate = search_start;
         candidate <= cursor && candidate < length; candidate++) {
      size_t splice_size = analysis_line_splice_size_mode(
          source, length, candidate, enable_trigraphs);
      if (splice_size && candidate > 0 &&
          candidate + splice_size < length &&
          candidate <= cursor && cursor <= candidate + splice_size &&
          is_identifier_byte((unsigned char)source[candidate - 1]) &&
          is_identifier_byte(
              (unsigned char)source[candidate + splice_size])) {
        selected = candidate - 1;
        break;
      }
    }
  }
  if (selected == SIZE_MAX) return 0;

  size_t start = selected;
  for (;;) {
    while (start > 0 &&
           is_identifier_byte((unsigned char)source[start - 1]))
      start--;
    size_t splice_start = 0;
    if (!analysis_line_splice_before(
            source, length, start, enable_trigraphs, &splice_start) ||
        splice_start == 0 ||
        !is_identifier_byte((unsigned char)source[splice_start - 1]))
      break;
    start = splice_start;
  }

  size_t end = selected + 1;
  for (;;) {
    while (end < length &&
           is_identifier_byte((unsigned char)source[end]))
      end++;
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, end, enable_trigraphs);
    if (!splice_size || end + splice_size >= length ||
        !is_identifier_byte((unsigned char)source[end + splice_size]))
      break;
    end += splice_size;
  }

  size_t logical_length = 0;
  for (size_t current = start; current < end;) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, end, current, enable_trigraphs);
    if (splice_size) {
      current += splice_size;
      continue;
    }
    if (!is_identifier_byte((unsigned char)source[current])) return 0;
    logical_length++;
    current++;
  }
  if (logical_length == 0) return 0;
  *span = (analysis_identifier_span_t){
      .start = start,
      .end = end,
      .logical_length = logical_length,
  };
  return 1;
}

static int analysis_identifier_span_matches(
    const char *source, size_t source_length,
    const analysis_identifier_span_t *span, int enable_trigraphs,
    const char *name, size_t name_length) {
  if (!source || !span || !name ||
      span->logical_length != name_length || span->end > source_length)
    return 0;
  size_t name_offset = 0;
  for (size_t current = span->start; current < span->end;) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, span->end, current, enable_trigraphs);
    if (splice_size) {
      current += splice_size;
      continue;
    }
    if (name_offset >= name_length ||
        source[current] != name[name_offset])
      return 0;
    current++;
    name_offset++;
  }
  return name_offset == name_length;
}

static size_t analysis_complete_line_splice_at_cursor(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs) {
  if (!source || cursor > length) return cursor;
  size_t search_start = cursor > 5 ? cursor - 5 : 0;
  for (size_t candidate = search_start;
       candidate < cursor && candidate < length; candidate++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, candidate, enable_trigraphs);
    if (splice_size && cursor < candidate + splice_size)
      return candidate + splice_size;
  }
  return cursor;
}

static size_t analysis_logical_line_start_mode(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs) {
  size_t start = cursor;
  while (start > 0 && source[start - 1] != '\n') start--;
  for (;;) {
    size_t splice_start = 0;
    if (!analysis_line_splice_before(
            source, length, start, enable_trigraphs, &splice_start))
      return start;
    start = splice_start;
    while (start > 0 && source[start - 1] != '\n') start--;
  }
}

static size_t analysis_skip_directive_spacing_mode(
    const char *source, size_t length, size_t cursor, size_t end,
    int enable_trigraphs, int *saw_horizontal_space) {
  while (cursor < end) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, cursor, enable_trigraphs);
    if (splice_size) {
      cursor += splice_size;
      continue;
    }
    if (source[cursor] != ' ' && source[cursor] != '\t' &&
        source[cursor] != '\r')
      break;
    if (saw_horizontal_space) *saw_horizontal_space = 1;
    cursor++;
  }
  return cursor;
}

static int analysis_match_directive_word_mode(
    const char *source, size_t length, size_t cursor, size_t end,
    int enable_trigraphs, const char *word, size_t word_length,
    size_t *after_word) {
  size_t word_offset = 0;
  while (word_offset < word_length) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, cursor, enable_trigraphs);
    if (splice_size) {
      cursor += splice_size;
      continue;
    }
    if (cursor >= end || source[cursor] != word[word_offset]) return 0;
    cursor++;
    word_offset++;
  }
  if (after_word) *after_word = cursor;
  return 1;
}

static size_t analysis_complete_macro_definition_at_cursor(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs) {
  analysis_identifier_span_t identifier = {0};
  if (!analysis_identifier_span_at_mode(
          source, length, cursor, enable_trigraphs, &identifier))
    return 0;

  size_t line_start = analysis_logical_line_start_mode(
      source, length, identifier.start, enable_trigraphs);
  size_t probe = line_start;
  probe = analysis_skip_directive_spacing_mode(
      source, length, probe, identifier.start, enable_trigraphs, NULL);
  if (probe < identifier.start && source[probe] == '#') {
    probe++;
  } else if (enable_trigraphs && probe + 2 < identifier.start &&
             source[probe] == '?' && source[probe + 1] == '?' &&
             source[probe + 2] == '=') {
    probe += 3;
  } else {
    return 0;
  }
  probe = analysis_skip_directive_spacing_mode(
      source, length, probe, identifier.start, enable_trigraphs, NULL);
  static const char define_keyword[] = "define";
  size_t define_length = sizeof(define_keyword) - 1;
  if (!analysis_match_directive_word_mode(
          source, length, probe, identifier.start, enable_trigraphs,
          define_keyword, define_length, &probe))
    return 0;
  int saw_horizontal_space = 0;
  probe = analysis_skip_directive_spacing_mode(
      source, length, probe, identifier.start, enable_trigraphs,
      &saw_horizontal_space);
  if (!saw_horizontal_space || probe != identifier.start) return 0;

  for (size_t end = identifier.end; end < length;) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, end, enable_trigraphs);
    if (splice_size) {
      end += splice_size;
      continue;
    }
    if (source[end] == '\n') return end + 1;
    end++;
  }
  return length;
}

static size_t analysis_preprocessor_directive_line_end_mode(
    const char *source, size_t length, size_t start,
    int enable_trigraphs) {
  size_t cursor = start;
  while (cursor < length) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, cursor, enable_trigraphs);
    if (splice_size) {
      cursor += splice_size;
      continue;
    }
    if (source[cursor] == '\n') return cursor + 1;
    cursor++;
  }
  return length;
}

static int analysis_offset_is_directive_code_mode(
    const char *source, size_t length, size_t start, size_t offset,
    int enable_trigraphs) {
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  for (size_t cursor = start; cursor < offset && cursor < length;
       cursor++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, cursor, enable_trigraphs);
    if (splice_size) {
      cursor += splice_size - 1;
      continue;
    }
    char current = source[cursor];
    char next = cursor + 1 < offset ? source[cursor + 1] : 0;
    if (line_comment) continue;
    if (block_comment) {
      if (current == '*' && next == '/') {
        block_comment = 0;
        cursor++;
      }
      continue;
    }
    if (quote) {
      if (escaped) {
        escaped = 0;
      } else if (current == '\\') {
        escaped = 1;
      } else if (current == quote) {
        quote = 0;
      }
      continue;
    }
    if (current == '/' && next == '/') {
      line_comment = 1;
      cursor++;
    } else if (current == '/' && next == '*') {
      block_comment = 1;
      cursor++;
    } else if (current == '\'' || current == '"') {
      quote = current;
    }
  }
  return !line_comment && !block_comment && !quote;
}

static int analysis_directive_operand_starts_at_mode(
    const char *source, size_t length, size_t start, size_t operand_start,
    int enable_trigraphs) {
  for (size_t cursor = start; cursor < operand_start;) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, cursor, enable_trigraphs);
    if (splice_size) {
      cursor += splice_size;
      continue;
    }
    if (source[cursor] == ' ' || source[cursor] == '\t' ||
        source[cursor] == '\r') {
      cursor++;
      continue;
    }
    if (cursor + 1 < operand_start && source[cursor] == '/' &&
        source[cursor + 1] == '*') {
      cursor += 2;
      int closed = 0;
      while (cursor < operand_start) {
        splice_size = analysis_line_splice_size_mode(
            source, length, cursor, enable_trigraphs);
        if (splice_size) {
          cursor += splice_size;
          continue;
        }
        if (cursor + 1 < operand_start && source[cursor] == '*' &&
            source[cursor + 1] == '/') {
          cursor += 2;
          closed = 1;
          break;
        }
        cursor++;
      }
      if (!closed) return 0;
      continue;
    }
    return 0;
  }
  return 1;
}

typedef enum {
  ANALYSIS_CONDITIONAL_LINE_OTHER = 0,
  ANALYSIS_CONDITIONAL_LINE_OPEN,
  ANALYSIS_CONDITIONAL_LINE_BRANCH,
  ANALYSIS_CONDITIONAL_LINE_ENDIF,
} analysis_conditional_line_kind_t;

static size_t analysis_skip_conditional_directive_spacing_mode(
    const char *source, size_t length, size_t cursor, size_t end,
    int enable_trigraphs) {
  while (cursor < end) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, cursor, enable_trigraphs);
    if (splice_size) {
      cursor += splice_size;
      continue;
    }
    if (source[cursor] == ' ' || source[cursor] == '\t' ||
        source[cursor] == '\r') {
      cursor++;
      continue;
    }
    if (cursor + 1 < end && source[cursor] == '/' &&
        source[cursor + 1] == '*') {
      cursor += 2;
      while (cursor + 1 < end &&
             !(source[cursor] == '*' && source[cursor + 1] == '/'))
        cursor++;
      if (cursor + 1 >= end) return end;
      cursor += 2;
      continue;
    }
    break;
  }
  return cursor;
}

static int analysis_conditional_directive_word_is_mode(
    const char *source, size_t length, size_t start, size_t end,
    int enable_trigraphs, const char *word, size_t word_length) {
  size_t after_word = 0;
  if (!analysis_match_directive_word_mode(
          source, length, start, end, enable_trigraphs,
          word, word_length, &after_word))
    return 0;
  while (after_word < end) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, after_word, enable_trigraphs);
    if (!splice_size) break;
    after_word += splice_size;
  }
  return after_word >= end ||
         !is_identifier_byte((unsigned char)source[after_word]);
}

static analysis_conditional_line_kind_t
analysis_conditional_logical_line_kind_mode(
    const char *source, size_t length, size_t start, size_t end,
    int enable_trigraphs) {
  start = analysis_skip_conditional_directive_spacing_mode(
      source, length, start, end, enable_trigraphs);
  if (start >= end || source[start++] != '#')
    return ANALYSIS_CONDITIONAL_LINE_OTHER;
  start = analysis_skip_conditional_directive_spacing_mode(
      source, length, start, end, enable_trigraphs);
  static const struct {
    const char *word;
    size_t length;
    analysis_conditional_line_kind_t kind;
  } directives[] = {
      {"if", sizeof("if") - 1, ANALYSIS_CONDITIONAL_LINE_OPEN},
      {"ifdef", sizeof("ifdef") - 1, ANALYSIS_CONDITIONAL_LINE_OPEN},
      {"ifndef", sizeof("ifndef") - 1, ANALYSIS_CONDITIONAL_LINE_OPEN},
      {"elif", sizeof("elif") - 1, ANALYSIS_CONDITIONAL_LINE_BRANCH},
      {"else", sizeof("else") - 1, ANALYSIS_CONDITIONAL_LINE_BRANCH},
      {"endif", sizeof("endif") - 1, ANALYSIS_CONDITIONAL_LINE_ENDIF},
  };
  for (size_t i = 0; i < sizeof(directives) / sizeof(directives[0]); i++)
    if (analysis_conditional_directive_word_is_mode(
            source, length, start, end, enable_trigraphs,
            directives[i].word, directives[i].length))
      return directives[i].kind;
  return ANALYSIS_CONDITIONAL_LINE_OTHER;
}

static int analysis_matching_conditional_opener_line_start(
    const char *source, size_t length, size_t current_line_start,
    int enable_trigraphs,
    size_t *out_line_start) {
  if (out_line_start) *out_line_start = 0;
  size_t scan = current_line_start;
  size_t nested_count = 0;
  while (scan > 0) {
    size_t physical_end = scan;
    if (source[physical_end - 1] == '\n') physical_end--;
    size_t physical_start = physical_end;
    while (physical_start > 0 && source[physical_start - 1] != '\n')
      physical_start--;
    size_t line_start = analysis_logical_line_start_mode(
        source, length, physical_start, enable_trigraphs);
    size_t line_end = analysis_preprocessor_directive_line_end_mode(
        source, length, line_start, enable_trigraphs);
    analysis_conditional_line_kind_t kind =
        analysis_conditional_logical_line_kind_mode(
            source, length, line_start, line_end, enable_trigraphs);
    if (kind == ANALYSIS_CONDITIONAL_LINE_ENDIF) {
      nested_count++;
    } else if (kind == ANALYSIS_CONDITIONAL_LINE_OPEN) {
      if (nested_count == 0) {
        if (out_line_start) *out_line_start = line_start;
        return 1;
      }
      nested_count--;
    }
    scan = line_start;
  }
  return 0;
}

static int analysis_preprocessor_operand_line_start_at_cursor(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, size_t *out_line_start) {
  if (out_line_start) *out_line_start = 0;
  analysis_identifier_span_t identifier = {0};
  if (!analysis_identifier_span_at_mode(
          source, length, cursor, enable_trigraphs, &identifier))
    return 0;
  size_t line_start = analysis_logical_line_start_mode(
      source, length, identifier.start, enable_trigraphs);
  size_t line_end = analysis_preprocessor_directive_line_end_mode(
      source, length, line_start, enable_trigraphs);
  size_t probe = analysis_skip_directive_spacing_mode(
      source, length, line_start, identifier.start,
      enable_trigraphs, NULL);
  if (probe < identifier.start && source[probe] == '#') {
    probe++;
  } else {
    return 0;
  }
  probe = analysis_skip_directive_spacing_mode(
      source, length, probe, identifier.start,
      enable_trigraphs, NULL);
  static const struct {
    const char *name;
    size_t length;
    int direct_operand;
    int use_conditional_group_start;
  } directives[] = {
      {"if", sizeof("if") - 1, 0, 0},
      {"ifdef", sizeof("ifdef") - 1, 1, 0},
      {"ifndef", sizeof("ifndef") - 1, 1, 0},
      {"elif", sizeof("elif") - 1, 0, 1},
      {"undef", sizeof("undef") - 1, 1, 0},
  };
  int matched = 0;
  size_t recovery_line_start = line_start;
  for (size_t i = 0; i < sizeof(directives) / sizeof(directives[0]);
       i++) {
    size_t after_directive = 0;
    if (!analysis_match_directive_word_mode(
            source, length, probe, identifier.start,
            enable_trigraphs, directives[i].name,
            directives[i].length, &after_directive))
      continue;
    size_t boundary = after_directive;
    while (boundary < identifier.start) {
      size_t splice_size = analysis_line_splice_size_mode(
          source, length, boundary, enable_trigraphs);
      if (!splice_size) break;
      boundary += splice_size;
    }
    if ((boundary < identifier.start &&
         is_identifier_byte((unsigned char)source[boundary])) ||
        identifier.start < after_directive || identifier.end > line_end)
      continue;
    if (directives[i].direct_operand
            ? !analysis_directive_operand_starts_at_mode(
                  source, length, after_directive, identifier.start,
                  enable_trigraphs)
            : !analysis_offset_is_directive_code_mode(
                  source, length, after_directive, identifier.start,
                  enable_trigraphs))
      continue;
    if (directives[i].use_conditional_group_start &&
        !analysis_matching_conditional_opener_line_start(
            source, length, line_start, enable_trigraphs,
            &recovery_line_start))
      continue;
    matched = 1;
    break;
  }
  if (!matched) return 0;
  if (out_line_start) *out_line_start = recovery_line_start;
  return 1;
}

static size_t skip_analysis_space_and_comments_mode(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs) {
  while (cursor < length) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, cursor, enable_trigraphs);
    if (splice_size > 0) {
      cursor += splice_size;
      continue;
    }
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
      while (cursor < length && source[cursor] != '\n') {
        splice_size = analysis_line_splice_size_mode(
            source, length, cursor, enable_trigraphs);
        if (splice_size > 0) {
          cursor += splice_size;
          continue;
        }
        cursor++;
      }
      continue;
    }
    break;
  }
  return cursor;
}

static size_t skip_analysis_space_and_comments(
    const char *source, size_t length, size_t cursor) {
  return skip_analysis_space_and_comments_mode(
      source, length, cursor, 0);
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

static int tag_body_open_at(
    const char *source, size_t limit, const char *tag_keyword,
    size_t *tag_open, size_t *outer_brace_count) {
  if (!source || !tag_keyword || !tag_open || !outer_brace_count)
    return 0;
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
  size_t keyword_length = strlen(tag_keyword);
  if (word_end - cursor == keyword_length &&
      memcmp(source + cursor, tag_keyword, keyword_length) == 0) {
    *tag_open = open;
    *outer_brace_count = outer_count;
    return 1;
  }
  if (cursor == word_end) return 0;
  while (cursor > 0 && isspace((unsigned char)source[cursor - 1])) cursor--;
  if (!word_before(source, cursor, tag_keyword)) return 0;
  *tag_open = open;
  *outer_brace_count = outer_count;
  return 1;
}

static int record_body_open_at(
    const char *source, size_t limit, size_t *record_open) {
  if (!source || !record_open) return 0;
  size_t search_limit = limit;
  size_t selected = SIZE_MAX;
  for (;;) {
    size_t open = 0;
    size_t outer_brace_count = 0;
    if (!tag_body_open_at(
            source, search_limit, "struct", &open,
            &outer_brace_count) &&
        !tag_body_open_at(
            source, search_limit, "union", &open,
            &outer_brace_count))
      break;
    (void)outer_brace_count;
    selected = open;
    if (open == 0) break;
    search_limit = open;
  }
  if (selected == SIZE_MAX) return 0;
  *record_open = selected;
  return 1;
}

typedef struct {
  int paren_depth;
  int bracket_depth;
  int brace_depth;
  int pending_conditional_count;
  int case_expression_active;
  int line_comment;
  int block_comment;
  int quote;
  int preprocessor_line;
  size_t last_word_start;
  size_t last_word_length;
} analysis_source_prefix_state_t;

static int analysis_source_prefix_state(
    const char *source, size_t length,
    analysis_source_prefix_state_t *state) {
  if (!source || !state) return 0;
  *state = (analysis_source_prefix_state_t){0};
  int escaped = 0;
  int at_line_start = 1;
  for (size_t i = 0; i < length; i++) {
    char c = source[i];
    char next = i + 1 < length ? source[i + 1] : 0;
    if (state->line_comment) {
      if (c == '\n') {
        state->line_comment = 0;
        at_line_start = 1;
        state->preprocessor_line = 0;
      }
      continue;
    }
    if (state->block_comment) {
      if (c == '*' && next == '/') {
        state->block_comment = 0;
        i++;
      }
      continue;
    }
    if (state->quote) {
      if (escaped) escaped = 0;
      else if (c == '\\') escaped = 1;
      else if (c == state->quote) state->quote = 0;
      continue;
    }
    if (c == '/' && next == '/') {
      state->line_comment = 1;
      i++;
      continue;
    }
    if (c == '/' && next == '*') {
      state->block_comment = 1;
      i++;
      continue;
    }
    if (c == '\'' || c == '"') {
      state->quote = c;
      at_line_start = 0;
      continue;
    }
    if (c == '\n') {
      if (!state->preprocessor_line || i == 0 || source[i - 1] != '\\')
        state->preprocessor_line = 0;
      at_line_start = 1;
      continue;
    }
    if (at_line_start &&
        (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') state->preprocessor_line = 1;
    at_line_start = 0;
    if (state->preprocessor_line) continue;
    if (isspace((unsigned char)c)) continue;
    if (is_identifier_byte((unsigned char)c)) {
      size_t word_start = i;
      while (i + 1 < length &&
             is_identifier_byte((unsigned char)source[i + 1]))
        i++;
      state->last_word_start = word_start;
      state->last_word_length = i + 1 - word_start;
      if (state->last_word_length == strlen("case") &&
          memcmp(source + word_start, "case", strlen("case")) == 0)
        state->case_expression_active = 1;
      continue;
    }
    if (c == '(') {
      state->paren_depth++;
    } else if (c == ')') {
      if (state->paren_depth == 0) return 0;
      state->paren_depth--;
    } else if (c == '[') {
      state->bracket_depth++;
    } else if (c == ']') {
      if (state->bracket_depth == 0) return 0;
      state->bracket_depth--;
    } else if (c == '{') {
      state->brace_depth++;
    } else if (c == '}') {
      if (state->brace_depth == 0) return 0;
      state->brace_depth--;
    } else if (c == '?') {
      state->pending_conditional_count++;
    } else if (c == ':' && state->pending_conditional_count > 0) {
      state->pending_conditional_count--;
    } else if (c == ':') {
      state->case_expression_active = 0;
    } else if (c == ';') {
      state->pending_conditional_count = 0;
      state->case_expression_active = 0;
    }
  }
  return 1;
}

static int analysis_prefix_last_word_is(
    const char *source, const analysis_source_prefix_state_t *state,
    const char *word) {
  if (!source || !state || !word) return 0;
  size_t word_length = strlen(word);
  return state->last_word_length == word_length &&
         memcmp(source + state->last_word_start, word, word_length) == 0;
}

static int analysis_source_delimiters_are_balanced(
    const char *source, size_t length) {
  if (!source || (length > 0 && source[length - 1] == '\\')) return 0;
  analysis_source_prefix_state_t state;
  return analysis_source_prefix_state(source, length, &state) &&
         !state.block_comment && !state.quote &&
         state.paren_depth == 0 && state.bracket_depth == 0 &&
         state.brace_depth == 0;
}

static char *build_jump_label_recovery_source(
    const char *source, size_t length, size_t cursor, int *changed) {
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(source, length, cursor, &name, &name_length);
  if (!name || name_length == 0) return NULL;
  size_t name_start = (size_t)(name - source);
  size_t name_end = name_start + name_length;
  size_t after_name = skip_analysis_space_and_comments(
      source, length, name_end);
  analysis_source_prefix_state_t prefix;
  if (!analysis_source_prefix_state(source, name_start, &prefix) ||
      prefix.line_comment || prefix.block_comment || prefix.quote ||
      prefix.preprocessor_line || prefix.paren_depth != 0 ||
      prefix.bracket_depth != 0 || prefix.brace_depth <= 0 ||
      !analysis_source_delimiters_are_balanced(source, length))
    return NULL;

  int is_goto_name =
      analysis_prefix_last_word_is(source, &prefix, "goto") &&
      after_name < length && source[after_name] == ';';
  int is_label_name =
      after_name < length && source[after_name] == ':' &&
      prefix.pending_conditional_count == 0 &&
      !prefix.case_expression_active &&
      !analysis_prefix_last_word_is(source, &prefix, "case") &&
      !(name_length == strlen("default") &&
        memcmp(name, "default", strlen("default")) == 0);
  if (is_label_name) {
    size_t record_open = 0;
    if (record_body_open_at(source, name_start, &record_open))
      is_label_name = 0;
  }
  if (!is_goto_name && !is_label_name) return NULL;

  char *result = malloc(length + 1);
  if (!result) return NULL;
  memcpy(result, source, length);
  result[length] = '\0';
  if (changed) *changed = 0;
  return result;
}

static int analysis_identifier_is_complete_member_access(
    const char *source, size_t length,
    const analysis_identifier_span_t *identifier) {
  if (!source || !identifier || identifier->logical_length == 0 ||
      identifier->end > length)
    return 0;
  size_t operator_end = identifier->start;
  while (operator_end > 0 &&
         isspace((unsigned char)source[operator_end - 1]))
    operator_end--;
  int has_member_operator =
      (operator_end > 0 && source[operator_end - 1] == '.') ||
      (operator_end > 1 && source[operator_end - 2] == '-' &&
       source[operator_end - 1] == '>');
  analysis_source_prefix_state_t prefix;
  if (!analysis_source_prefix_state(
          source, identifier->start, &prefix) ||
      prefix.line_comment || prefix.block_comment || prefix.quote ||
      prefix.preprocessor_line ||
      !has_member_operator)
    return 0;
  return 1;
}

typedef enum {
  AG_LANGUAGE_RECOVERY_CHANGED = 1 << 0,
  AG_LANGUAGE_RECOVERY_PARTIAL_IDENTIFIER = 1 << 1,
  AG_LANGUAGE_RECOVERY_COMPLETE_IDENTIFIER_ELIDED = 1 << 2,
  AG_LANGUAGE_RECOVERY_INCOMPLETE_SOURCE = 1 << 3,
  AG_LANGUAGE_RECOVERY_AMBIGUOUS_EOF_IDENTIFIER = 1 << 4,
  AG_LANGUAGE_RECOVERY_ENUM_CALL_ELIDED = 1 << 5,
  AG_LANGUAGE_RECOVERY_BLOCK_EXTERN_DECLARATION_RETAINED = 1 << 6,
} ag_language_recovery_flags_t;

static char analysis_last_significant_between(
    const char *source, size_t start, size_t end, int enable_trigraphs,
    size_t *last_offset) {
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 0;
  int preprocessor_line = 0;
  char last = 0;
  if (last_offset) *last_offset = SIZE_MAX;
  for (size_t i = start; i < end; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, end, i, enable_trigraphs);
    if (splice_size) {
      i += splice_size - 1;
      continue;
    }
    char c = source[i];
    char next = i + 1 < end ? source[i + 1] : 0;
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
      last = 'x';
      if (last_offset) *last_offset = i;
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
    if (preprocessor_line || isspace((unsigned char)c)) continue;
    last = c;
    if (last_offset) *last_offset = i;
  }
  return last;
}

static int analysis_identifier_before_offset_matches(
    const char *source, size_t length, size_t start, size_t end,
    int enable_trigraphs, const char *name) {
  size_t identifier_offset = SIZE_MAX;
  analysis_last_significant_between(
      source, start, end, enable_trigraphs, &identifier_offset);
  analysis_identifier_span_t identifier = {0};
  size_t name_length = strlen(name);
  return identifier_offset != SIZE_MAX &&
         analysis_identifier_span_at_mode(
             source, length, identifier_offset, enable_trigraphs,
             &identifier) &&
         analysis_identifier_span_matches(
             source, length, &identifier, enable_trigraphs,
             name, name_length);
}

static int analysis_delimited_tail_end_mode(
    const char *source, size_t source_length, size_t start,
    char terminator, int allow_top_level_comma, int enable_trigraphs,
    size_t *tail_end);

static int analysis_direct_parameter_array_bound_marker_offset(
    const char *source, size_t length, size_t parameter_open,
    size_t parameter_close, size_t selected_start,
    size_t *marker_offset, size_t *selected_parameter_start);

static int analysis_direct_callback_parameter_bound_marker_offset(
    const char *source, size_t length, size_t scan_start,
    size_t selected_start, int enable_trigraphs, size_t *marker_offset,
    size_t *parameter_close, size_t *selected_parameter_start) {
  if (!source || !marker_offset || !parameter_close ||
      scan_start >= selected_start || selected_start >= length)
    return 0;
  size_t parameter_open = SIZE_MAX;
  size_t previous_group_close = SIZE_MAX;
  size_t paren_depth = 0;
  size_t bracket_depth = 0;
  size_t array_bound_paren_depth = SIZE_MAX;
  int current_group_nested = 0;
  int current_group_has_pointer = 0;
  int previous_group_nested = 0;
  int previous_group_has_pointer = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = scan_start == 0;
  int preprocessor_line = 0;
  for (size_t i = scan_start; i < selected_start; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, i, enable_trigraphs);
    if (splice_size > 0) {
      i += splice_size - 1;
      continue;
    }
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
      at_line_start = 1;
      preprocessor_line = 0;
      continue;
    }
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r')) continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    if (c == '(') {
      if (paren_depth == 0) {
        parameter_open = i;
        current_group_nested = 0;
        current_group_has_pointer = 0;
      } else {
        current_group_nested = 1;
      }
      paren_depth++;
    } else if (c == ')' && paren_depth > 0) {
      paren_depth--;
      if (paren_depth == 0) {
        previous_group_close = i;
        previous_group_nested = current_group_nested;
        previous_group_has_pointer = current_group_has_pointer;
        parameter_open = SIZE_MAX;
      }
    } else if (c == '*' && paren_depth == 1) {
      current_group_has_pointer = 1;
    } else if (c == '[') {
      if (bracket_depth == 0) array_bound_paren_depth = paren_depth;
      bracket_depth++;
    } else if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      if (bracket_depth == 0) array_bound_paren_depth = SIZE_MAX;
    }
  }
  if (parameter_open == SIZE_MAX || paren_depth != 1 ||
      bracket_depth == 0 || array_bound_paren_depth != 1 ||
      previous_group_nested || !previous_group_has_pointer)
    return 0;
  size_t prefix_last_offset = SIZE_MAX;
  if (analysis_last_significant_between(
          source, scan_start, parameter_open,
          enable_trigraphs, &prefix_last_offset) != ')' ||
      prefix_last_offset == SIZE_MAX ||
      prefix_last_offset != previous_group_close)
    return 0;
  size_t close = 0;
  if (!analysis_delimited_tail_end_mode(
          source, length, parameter_open + 1, ')', 1,
          enable_trigraphs, &close))
    return 0;
  if (!analysis_direct_parameter_array_bound_marker_offset(
          source, length, parameter_open, close, selected_start,
          marker_offset, selected_parameter_start))
    return 0;
  *parameter_close = close;
  return 1;
}

static char *build_record_member_recovery_source(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, int *changed, size_t *source_consumed) {
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(source, length, cursor, &name, &name_length);
  if (!name || name_length == 0) return NULL;
  size_t name_start = (size_t)(name - source);
  size_t after_name = skip_analysis_space_and_comments(
      source, length, name_start + name_length);
  size_t before_name = name_start;
  while (before_name > 0 &&
         isspace((unsigned char)source[before_name - 1]))
    before_name--;
  int is_parenthesized_pointer_name =
      after_name < length && source[after_name] == ')' &&
      before_name > 0 && source[before_name - 1] == '*';
  if (after_name >= length ||
      (source[after_name] != ';' && source[after_name] != ',' &&
       source[after_name] != '[' && source[after_name] != ':' &&
       source[after_name] != ']' &&
       !is_parenthesized_pointer_name))
    return NULL;
  size_t record_open = 0;
  if (!record_body_open_at(source, name_start, &record_open) ||
      record_open >= name_start ||
      !analysis_source_delimiters_are_balanced(source, length))
    return NULL;
  size_t prefix_last_offset = SIZE_MAX;
  char prefix_last = analysis_last_significant_between(
      source, record_open + 1, name_start, enable_trigraphs,
      &prefix_last_offset);
  if (source[after_name] == ',' && prefix_last == '(' &&
      prefix_last_offset != SIZE_MAX &&
      analysis_identifier_before_offset_matches(
          source, length, record_open + 1, prefix_last_offset,
          enable_trigraphs, "_Static_assert"))
    return NULL;
  size_t direct_open = 0;
  size_t outer_brace_count = 0;
  int direct_record =
      (tag_body_open_at(
           source, name_start, "struct", &direct_open,
           &outer_brace_count) ||
       tag_body_open_at(
           source, name_start, "union", &direct_open,
           &outer_brace_count)) &&
      direct_open == record_open;
  size_t parameter_marker_offset = 0;
  size_t parameter_close = 0;
  if (direct_record &&
      analysis_direct_callback_parameter_bound_marker_offset(
          source, length, record_open, name_start, enable_trigraphs,
          &parameter_marker_offset, &parameter_close, NULL)) {
    size_t parameter_member_end = skip_analysis_space_and_comments_mode(
        source, length, parameter_close + 1, enable_trigraphs);
    if (parameter_member_end >= length ||
        source[parameter_member_end] != ';')
      return NULL;
    static const char parameter_marker[] =
        ", int " AG_LANGUAGE_CURSOR_MARKER;
    static const char record_tail[] =
        " } __agc_record_operand;\n";
    size_t prefix_length = parameter_member_end + 1;
    if (prefix_length > SIZE_MAX - sizeof(parameter_marker) ||
        prefix_length + sizeof(parameter_marker) >
            SIZE_MAX - sizeof(record_tail) ||
        outer_brace_count >
            (SIZE_MAX - prefix_length - sizeof(parameter_marker) -
             sizeof(record_tail)) /
                2)
      return NULL;
    size_t result_length =
        prefix_length + sizeof(parameter_marker) - 1 +
        sizeof(record_tail) - 1 + outer_brace_count * 2;
    char *result = malloc(result_length + 1);
    if (!result) return NULL;
    memcpy(result, source, parameter_marker_offset);
    memcpy(result + parameter_marker_offset, parameter_marker,
           sizeof(parameter_marker) - 1);
    memcpy(result + parameter_marker_offset + sizeof(parameter_marker) - 1,
           source + parameter_marker_offset,
           prefix_length - parameter_marker_offset);
    size_t output = prefix_length + sizeof(parameter_marker) - 1;
    memcpy(result + output, record_tail, sizeof(record_tail) - 1);
    output += sizeof(record_tail) - 1;
    for (size_t i = 0; i < outer_brace_count; i++) {
      result[output++] = '}';
      result[output++] = '\n';
    }
    result[output] = '\0';
    if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
    if (source_consumed) *source_consumed = prefix_length;
    return result;
  }
  const char *record_operand_tail = NULL;
  size_t record_operand_tail_length = 0;
  size_t operand_source_consumed = 0;
  static const char bitfield_tail[] =
      "; } __agc_record_operand;\n"
      "int " AG_LANGUAGE_CURSOR_MARKER ";\n";
  static const char array_bound_tail[] =
      "]; } __agc_record_operand;\n"
      "int " AG_LANGUAGE_CURSOR_MARKER ";\n";
  if (source[after_name] == ';' && prefix_last == ':') {
    record_operand_tail = bitfield_tail;
    record_operand_tail_length = sizeof(bitfield_tail) - 1;
    operand_source_consumed = after_name + 1;
  } else if (source[after_name] == ']' && prefix_last == '[') {
    size_t member_end = skip_analysis_space_and_comments_mode(
        source, length, after_name + 1, enable_trigraphs);
    if (member_end < length && source[member_end] == ';') {
      record_operand_tail = array_bound_tail;
      record_operand_tail_length = sizeof(array_bound_tail) - 1;
      operand_source_consumed = member_end + 1;
    }
  }
  if (record_operand_tail) {
    size_t prefix_length = name_start + name_length;
    if (direct_record &&
        prefix_length <= SIZE_MAX - record_operand_tail_length &&
        outer_brace_count <=
            (SIZE_MAX - prefix_length - record_operand_tail_length) / 2) {
      size_t result_length = prefix_length + record_operand_tail_length +
                             outer_brace_count * 2;
      char *result = malloc(result_length + 1);
      if (!result) return NULL;
      memcpy(result, source, prefix_length);
      size_t output = prefix_length;
      memcpy(result + output, record_operand_tail,
             record_operand_tail_length);
      output += record_operand_tail_length;
      for (size_t i = 0; i < outer_brace_count; i++) {
        result[output++] = '}';
        result[output++] = '\n';
      }
      result[output] = '\0';
      if (changed) *changed = 1;
      if (source_consumed) *source_consumed = operand_source_consumed;
      return result;
    }
  }
  static const char marker[] =
      "\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  if (length > SIZE_MAX - sizeof(marker)) return NULL;
  size_t result_length = length + sizeof(marker) - 1;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, length);
  memcpy(result + length, marker, sizeof(marker));
  if (changed) *changed = 1;
  if (source_consumed) *source_consumed = length;
  return result;
}

static char *build_direct_callback_parameter_bound_recovery_source(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, int *changed, size_t *source_consumed) {
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(source, length, cursor, &name, &name_length);
  if (!name || name_length == 0) return NULL;
  size_t name_start = (size_t)(name - source);
  size_t record_open = 0;
  if (record_body_open_at(source, name_start, &record_open)) return NULL;
  size_t marker_offset = 0;
  size_t parameter_close = 0;
  size_t selected_parameter_start = 0;
  if (!analysis_direct_callback_parameter_bound_marker_offset(
          source, length, 0, name_start, enable_trigraphs,
          &marker_offset, &parameter_close,
          &selected_parameter_start))
    return NULL;
  size_t declaration_end = skip_analysis_space_and_comments_mode(
      source, length, parameter_close + 1, enable_trigraphs);
  if (declaration_end >= length || source[declaration_end] != ';' ||
      !analysis_source_delimiters_are_balanced(source, length))
    return NULL;
  analysis_source_prefix_state_t prefix;
  if (!analysis_source_prefix_state(source, name_start, &prefix) ||
      prefix.line_comment || prefix.block_comment || prefix.quote ||
      prefix.preprocessor_line)
    return NULL;
  size_t outer_brace_count = (size_t)prefix.brace_depth;
  static const char parameter_marker[] =
      "int " AG_LANGUAGE_CURSOR_MARKER;
  size_t prefix_length = declaration_end + 1;
  if (selected_parameter_start >= marker_offset ||
      marker_offset > parameter_close ||
      selected_parameter_start > SIZE_MAX - sizeof(parameter_marker) ||
      selected_parameter_start + sizeof(parameter_marker) >
          SIZE_MAX - (prefix_length - parameter_close) ||
      outer_brace_count >
          (SIZE_MAX - selected_parameter_start -
           sizeof(parameter_marker) -
           (prefix_length - parameter_close)) / 2)
    return NULL;
  size_t result_length = selected_parameter_start +
                         sizeof(parameter_marker) - 1 +
                         (prefix_length - parameter_close) +
                         outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, selected_parameter_start);
  size_t output = selected_parameter_start;
  memcpy(result + output, parameter_marker,
         sizeof(parameter_marker) - 1);
  output += sizeof(parameter_marker) - 1;
  memcpy(result + output, source + parameter_close,
         prefix_length - parameter_close);
  output += prefix_length - parameter_close;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = prefix_length;
  return result;
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

static int enum_initializer_operand_end(
    const char *source, size_t length, size_t enum_open,
    size_t operand_start, size_t *item_start, size_t *initializer_start,
    size_t *item_end, int *unterminated) {
  size_t segment_start = enum_open + 1;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int saw_initializer = 0;
  for (size_t i = segment_start; i < length; i++) {
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
    if ((c == ',' || c == '}') && paren_depth == 0 &&
        bracket_depth == 0 && brace_depth == 0) {
      if (operand_start >= segment_start && operand_start < i) {
        if (!saw_initializer) return 0;
        if (item_start) *item_start = segment_start;
        *item_end = i;
        return 1;
      }
      if (c == '}') return 0;
      segment_start = i + 1;
      saw_initializer = 0;
      if (initializer_start) *initializer_start = 0;
      continue;
    }
    if (!saw_initializer && i < operand_start && c == '=' &&
        paren_depth == 0 &&
        bracket_depth == 0 && brace_depth == 0) {
      saw_initializer = 1;
      if (initializer_start) *initializer_start = i + 1;
    }
    if (c == '(') paren_depth++;
    else if (c == ')') {
      if (paren_depth == 0) return 0;
      paren_depth--;
    } else if (c == '[') bracket_depth++;
    else if (c == ']') {
      if (bracket_depth == 0) return 0;
      bracket_depth--;
    } else if (c == '{') brace_depth++;
    else if (c == '}') {
      if (brace_depth == 0) return 0;
      brace_depth--;
    }
  }
  if (operand_start >= segment_start && saw_initializer &&
      paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 &&
      !block_comment && !quote) {
    if (item_start) *item_start = segment_start;
    *item_end = length;
    if (unterminated) *unterminated = 1;
    return 1;
  }
  return 0;
}

static int enum_direct_call_identifier_at_cursor(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, analysis_identifier_span_t *identifier) {
  if (!source || !identifier || cursor > length) return 0;
  size_t enum_open = 0;
  size_t outer_brace_count = 0;
  if (!tag_body_open_at(
          source, cursor, "enum", &enum_open, &outer_brace_count))
    return 0;
  (void)outer_brace_count;
  size_t item_start = 0;
  size_t initializer_start = 0;
  size_t item_end = 0;
  int unterminated = 0;
  if (!enum_initializer_operand_end(
          source, length, enum_open, cursor, &item_start,
          &initializer_start, &item_end, &unterminated))
    return 0;
  (void)item_start;
  (void)unterminated;
  size_t operand_start = skip_analysis_space_and_comments_mode(
      source, item_end, initializer_start, enable_trigraphs);
  analysis_identifier_span_t callee = {0};
  if (operand_start >= item_end || cursor < operand_start ||
      !analysis_identifier_span_at_mode(
          source, item_end, operand_start, enable_trigraphs, &callee) ||
      callee.start != operand_start)
    return 0;
  size_t call_open = skip_analysis_space_and_comments_mode(
      source, item_end, callee.end, enable_trigraphs);
  size_t call_end = 0;
  if (call_open >= item_end || source[call_open] != '(' ||
      !analysis_delimited_tail_end_mode(
          source, item_end, call_open + 1, ')', 1,
          enable_trigraphs, &call_end) ||
      skip_analysis_space_and_comments_mode(
          source, item_end, call_end + 1, enable_trigraphs) != item_end ||
      cursor > item_end)
    return 0;
  *identifier = callee;
  return 1;
}

static char *build_enum_declaration_recovery_source(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, int preserve_recoverable_operand, int *changed,
    size_t *source_consumed) {
  const char *name = NULL;
  size_t name_length = 0;
  analysis_identifier_span_t direct_call_identifier = {0};
  int cursor_in_direct_call = enum_direct_call_identifier_at_cursor(
      source, length, cursor, enable_trigraphs,
      &direct_call_identifier);
  identifier_at(source, length, cursor, &name, &name_length);
  int cursor_before_operand = 0;
  if (!name || name_length == 0) {
    size_t next_identifier = skip_analysis_space_and_comments_mode(
        source, length, cursor, enable_trigraphs);
    if (cursor_in_direct_call) {
      name = source + direct_call_identifier.start;
      name_length = direct_call_identifier.end -
                    direct_call_identifier.start;
    } else if (next_identifier > cursor && next_identifier < length &&
        is_identifier_byte((unsigned char)source[next_identifier])) {
      size_t next_identifier_end = next_identifier + 1;
      while (next_identifier_end < length &&
             is_identifier_byte((unsigned char)source[next_identifier_end]))
        next_identifier_end++;
      name = source + next_identifier;
      name_length = next_identifier_end - next_identifier;
      cursor_before_operand = 1;
    } else {
      return NULL;
    }
  }
  size_t name_start = (size_t)(name - source);
  size_t name_end = name_start + name_length;
  size_t enum_open = 0;
  size_t outer_brace_count = 0;
  size_t item_start = 0;
  size_t initializer_start = 0;
  size_t item_end = 0;
  int unterminated_initializer = 0;
  int initializer_operand = 0;
  if (!tag_body_open_at(
          source, name_start, "enum", &enum_open,
          &outer_brace_count))
    return NULL;
  if (!enum_enumerator_bounds(
          source, length, enum_open, name_start, name_end, &item_end))
    initializer_operand = enum_initializer_operand_end(
        source, length, enum_open, name_start, &item_start,
        &initializer_start, &item_end, &unterminated_initializer);
  (void)initializer_start;
  if (!item_end || (cursor_before_operand && !initializer_operand))
    return NULL;
  static const char complete_suffix[] =
      "} __agc_language_enum_holder;\n"
      "int " AG_LANGUAGE_CURSOR_MARKER ";\n";
  static const char incomplete_suffix[] =
      "\n} __agc_language_enum_holder;\n"
      "int " AG_LANGUAGE_CURSOR_MARKER ";\n";
  const char *suffix = unterminated_initializer
                           ? incomplete_suffix
                           : complete_suffix;
  size_t suffix_length = unterminated_initializer
                             ? sizeof(incomplete_suffix) - 1
                             : sizeof(complete_suffix) - 1;
  int ambiguous_eof_identifier =
      !cursor_before_operand && unterminated_initializer &&
      name_end == length;
  int elide_eof_identifier =
      ambiguous_eof_identifier && !preserve_recoverable_operand;
  int elide_complete_call =
      unterminated_initializer && !preserve_recoverable_operand &&
      cursor_in_direct_call;
  static const char cursor_gap_replacement[] =
      "__agc_language_cursor_enum_gap = 0";
  static const char invalid_call_replacement[] =
      "__agc_language_invalid_enum_call = 0";
  size_t source_prefix_length =
      cursor_before_operand ? item_start
      : elide_complete_call ? item_start
      : elide_eof_identifier ? name_start
                             : item_end;
  size_t replacement_length =
      cursor_before_operand ? sizeof(cursor_gap_replacement) - 1
      : elide_complete_call ? sizeof(invalid_call_replacement) - 1
      : elide_eof_identifier ? 1
                             : 0;
  if (outer_brace_count > (SIZE_MAX - 1) / 2) return NULL;
  size_t outer_brace_bytes = outer_brace_count * 2;
  if (replacement_length > SIZE_MAX - 1 - outer_brace_bytes ||
      suffix_length > SIZE_MAX - 1 - outer_brace_bytes - replacement_length ||
      source_prefix_length >
          SIZE_MAX - 1 - outer_brace_bytes - replacement_length -
              suffix_length)
    return NULL;
  size_t result_length =
      source_prefix_length + replacement_length + suffix_length +
      outer_brace_bytes;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, source_prefix_length);
  size_t output = source_prefix_length;
  if (cursor_before_operand) {
    memcpy(result + output, cursor_gap_replacement,
           sizeof(cursor_gap_replacement) - 1);
    output += sizeof(cursor_gap_replacement) - 1;
  } else if (elide_complete_call) {
    memcpy(result + output, invalid_call_replacement,
           sizeof(invalid_call_replacement) - 1);
    output += sizeof(invalid_call_replacement) - 1;
  } else if (elide_eof_identifier) {
    result[output++] = '0';
  }
  memcpy(result + output, suffix, suffix_length);
  output += suffix_length;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed)
    *changed = AG_LANGUAGE_RECOVERY_CHANGED |
               (unterminated_initializer
                    ? AG_LANGUAGE_RECOVERY_INCOMPLETE_SOURCE
                    : 0) |
               (ambiguous_eof_identifier
                    ? AG_LANGUAGE_RECOVERY_AMBIGUOUS_EOF_IDENTIFIER
                    : 0) |
               (elide_complete_call
                    ? AG_LANGUAGE_RECOVERY_ENUM_CALL_ELIDED
                    : 0);
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

static int analysis_type_name_qualifier_word(
    const char *source, size_t start, size_t length);

static int analysis_delimited_tail_end_mode(
    const char *source, size_t source_length, size_t start,
    char terminator, int allow_top_level_comma, int enable_trigraphs,
    size_t *tail_end) {
  size_t paren_depth = 0;
  size_t bracket_depth = 0;
  size_t brace_depth = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  for (size_t cursor = start; cursor < source_length; cursor++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, source_length, cursor, enable_trigraphs);
    if (splice_size > 0) {
      cursor += splice_size - 1;
      continue;
    }
    char c = source[cursor];
    char next = cursor + 1 < source_length ? source[cursor + 1] : 0;
    if (line_comment) {
      if (c == '\n') line_comment = 0;
      continue;
    }
    if (block_comment) {
      if (c == '*' && next == '/') {
        block_comment = 0;
        cursor++;
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
      cursor++;
      continue;
    }
    if (c == '/' && next == '*') {
      block_comment = 1;
      cursor++;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (terminator == ':' && c == ':' && paren_depth == 0 &&
        bracket_depth == 0 && brace_depth == 0) {
      if (tail_end) *tail_end = cursor;
      return 1;
    }
    if (c == '(') {
      paren_depth++;
    } else if (c == ')') {
      if (paren_depth == 0) {
        if (terminator == ')' && bracket_depth == 0 && brace_depth == 0) {
          if (tail_end) *tail_end = cursor;
          return 1;
        }
        return 0;
      }
      paren_depth--;
    } else if (c == '[') {
      bracket_depth++;
    } else if (c == ']') {
      if (bracket_depth == 0) {
        if (terminator == ']' && paren_depth == 0 && brace_depth == 0) {
          if (tail_end) *tail_end = cursor;
          return 1;
        }
        return 0;
      }
      bracket_depth--;
    } else if (c == '{') {
      brace_depth++;
    } else if (c == '}') {
      if (brace_depth == 0) {
        if (terminator == '}' && paren_depth == 0 && bracket_depth == 0) {
          if (tail_end) *tail_end = cursor;
          return 1;
        }
        return 0;
      }
      brace_depth--;
    } else if ((c == ';' || (c == ',' && !allow_top_level_comma)) &&
               paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
      return 0;
    }
  }
  return 0;
}

static int analysis_case_label_end_mode(
    const char *source, size_t source_length, size_t start,
    int enable_trigraphs, size_t *label_end) {
  size_t paren_depth = 0;
  size_t bracket_depth = 0;
  size_t brace_depth = 0;
  size_t pending_conditional_count = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  for (size_t cursor = start; cursor < source_length; cursor++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, source_length, cursor, enable_trigraphs);
    if (splice_size > 0) {
      cursor += splice_size - 1;
      continue;
    }
    char c = source[cursor];
    char next = cursor + 1 < source_length ? source[cursor + 1] : 0;
    if (line_comment) {
      if (c == '\n') line_comment = 0;
      continue;
    }
    if (block_comment) {
      if (c == '*' && next == '/') {
        block_comment = 0;
        cursor++;
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
      cursor++;
      continue;
    }
    if (c == '/' && next == '*') {
      block_comment = 1;
      cursor++;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c == '?') {
      pending_conditional_count++;
      continue;
    }
    if (c == ':') {
      if (pending_conditional_count > 0) {
        pending_conditional_count--;
        continue;
      }
      if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
        if (label_end) *label_end = cursor;
        return 1;
      }
      continue;
    }
    if (c == '(') {
      paren_depth++;
    } else if (c == ')') {
      if (paren_depth == 0) return 0;
      paren_depth--;
    } else if (c == '[') {
      bracket_depth++;
    } else if (c == ']') {
      if (bracket_depth == 0) return 0;
      bracket_depth--;
    } else if (c == '{') {
      brace_depth++;
    } else if (c == '}') {
      if (brace_depth == 0) return 0;
      brace_depth--;
    } else if ((c == ';' || c == ',') && paren_depth == 0 &&
               bracket_depth == 0 && brace_depth == 0) {
      return 0;
    }
  }
  return 0;
}

static int object_declaration_prefix(
    const char *source, size_t name_start, size_t *outer_brace_count,
    int *paren_depth, int *bracket_depth, int *brace_depth,
    int *definite_declaration, int *for_init_declaration,
    size_t *declaration_start) {
  if (definite_declaration) *definite_declaration = 0;
  if (for_init_declaration) *for_init_declaration = 0;
  if (declaration_start) *declaration_start = 0;
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
  int tag_specifier_state = 0;
  size_t tag_body_depth = 0;
  int previous_token_is_for = 0;
  int for_paren_depth = 0;
  size_t for_separator_count = 0;
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
      previous_token_is_for = 0;
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
    if (tag_body_depth > 0) {
      if (c == '{') {
        tag_body_depth++;
      } else if (c == '}') {
        tag_body_depth--;
      }
      continue;
    }
    if (is_identifier_byte((unsigned char)c)) {
      size_t word_start = i;
      while (i + 1 < name_start &&
             is_identifier_byte((unsigned char)source[i + 1]))
        i++;
      size_t word_length = i + 1 - word_start;
      previous_token_is_for = analysis_word_is(
          source, word_start, word_length, "for");
      if (analysis_word_is(source, word_start, word_length, "struct") ||
          analysis_word_is(source, word_start, word_length, "union") ||
          analysis_word_is(source, word_start, word_length, "enum")) {
        tag_specifier_state = 1;
      } else if (tag_specifier_state == 1) {
        tag_specifier_state = 2;
      } else {
        tag_specifier_state = 0;
      }
      continue;
    }
    if (c == '{' && tag_specifier_state != 0) {
      tag_body_depth = 1;
      tag_specifier_state = 0;
      previous_token_is_for = 0;
      continue;
    }
    int opens_for_control = c == '(' && previous_token_is_for &&
                            parens == 0 && brackets == 0;
    if (!isspace((unsigned char)c)) {
      tag_specifier_state = 0;
      previous_token_is_for = 0;
    }
    if (c == '(') {
      parens++;
      if (opens_for_control) {
        start = i + 1;
        for_paren_depth = parens;
        for_separator_count = 0;
      }
    } else if (c == ')' && parens > 0) {
      if (for_paren_depth == parens) for_paren_depth = 0;
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
    } else if (c == ';') {
      if (for_paren_depth > 0 && parens == for_paren_depth &&
          brackets == 0)
        for_separator_count++;
      if (parens == 0 && brackets == 0) start = i + 1;
    }
  }

  int has_type = 0;
  int typedef_candidate_count = 0;
  int typedef_candidates_before_first_comma = 0;
  int saw_top_level_comma = 0;
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
      else if (is_identifier_start_byte(
                   (unsigned char)source[word_start]) &&
               !analysis_declaration_modifier_word(
                   source, word_start, word_length) &&
               !analysis_non_declaration_word(
                   source, word_start, word_length)) {
        typedef_candidate_count++;
        if (!saw_top_level_comma && !assignment_after_comma)
          typedef_candidates_before_first_comma++;
      }
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
             local_braces == 0) {
      saw_top_level_comma = 1;
      assignment_after_comma = 0;
    }
    else if (c == '=' && local_parens == 0 && local_brackets == 0 &&
             local_braces == 0)
      assignment_after_comma = 1;
  }
  int has_typedef_declarator_prefix =
      definite_declaration &&
      (typedef_candidate_count == 2 ||
       (saw_top_level_comma &&
        typedef_candidates_before_first_comma == 2));
  if ((!has_type && typedef_candidate_count != 1 &&
       !has_typedef_declarator_prefix) ||
      has_non_declaration || assignment_after_comma)
    return 0;
  *outer_brace_count = open_braces;
  *paren_depth = local_parens;
  *bracket_depth = local_brackets;
  *brace_depth = local_braces;
  if (definite_declaration)
    *definite_declaration = has_type || has_typedef_declarator_prefix;
  if (for_init_declaration)
    *for_init_declaration = for_paren_depth > 0 &&
                            for_separator_count == 0;
  if (declaration_start) *declaration_start = start;
  return 1;
}

static int analysis_typedef_specifier_before_first_declarator(
    const char *source, size_t length, size_t name_start, size_t name_end,
    size_t declaration_start, size_t outer_brace_count,
    int enable_trigraphs) {
  enum {
    ANALYSIS_TYPEDEF_SPECIFIER_ALIAS,
    ANALYSIS_TYPEDEF_SPECIFIER_ATOMIC,
  } selected_specifier_mode = ANALYSIS_TYPEDEF_SPECIFIER_ALIAS;
  size_t scan = declaration_start;
  int has_typedef_keyword = 0;
  int atomic_qualifier_before_name = 0;
  while (scan < name_start) {
    scan = skip_analysis_space_and_comments_mode(
        source, name_start, scan, enable_trigraphs);
    if (scan >= name_start) break;
    if (!is_identifier_byte((unsigned char)source[scan])) return 0;
    size_t word_start = scan;
    while (scan < name_start &&
           is_identifier_byte((unsigned char)source[scan]))
      scan++;
    size_t word_length = scan - word_start;
    if (analysis_word_is(
            source, word_start, word_length, "typedef")) {
      if (has_typedef_keyword) return 0;
      has_typedef_keyword = 1;
    } else if (analysis_declaration_modifier_word(
                   source, word_start, word_length)) {
      continue;
    } else if (analysis_word_is(
                   source, word_start, word_length, "_Atomic")) {
      scan = skip_analysis_space_and_comments_mode(
          source, name_start, scan, enable_trigraphs);
      if (scan >= name_start || source[scan] != '(') return 0;
      scan = skip_analysis_space_and_comments_mode(
          source, name_start, scan + 1, enable_trigraphs);
      while (scan < name_start) {
        if (!is_identifier_byte((unsigned char)source[scan])) return 0;
        size_t qualifier_start = scan;
        while (scan < name_start &&
               is_identifier_byte((unsigned char)source[scan]))
          scan++;
        if (!analysis_type_name_qualifier_word(
                source, qualifier_start, scan - qualifier_start))
          return 0;
        atomic_qualifier_before_name = 1;
        scan = skip_analysis_space_and_comments_mode(
            source, name_start, scan, enable_trigraphs);
      }
      if (scan != name_start) return 0;
      selected_specifier_mode = ANALYSIS_TYPEDEF_SPECIFIER_ATOMIC;
      break;
    } else {
      return 0;
    }
  }
  if (!has_typedef_keyword) return 0;

  scan = name_end;
  if (selected_specifier_mode == ANALYSIS_TYPEDEF_SPECIFIER_ATOMIC) {
    size_t pointer_count = 0;
    int last_pointer_has_qualifier = 0;
    while (1) {
      scan = skip_analysis_space_and_comments_mode(
          source, length, scan, enable_trigraphs);
      if (scan >= length || source[scan] != '*') break;
      pointer_count++;
      scan++;
      last_pointer_has_qualifier = 0;
      while (1) {
        scan = skip_analysis_space_and_comments_mode(
            source, length, scan, enable_trigraphs);
        if (scan >= length ||
            !is_identifier_byte((unsigned char)source[scan]))
          break;
        size_t qualifier_start = scan;
        while (scan < length &&
               is_identifier_byte((unsigned char)source[scan]))
          scan++;
        if (!analysis_type_name_qualifier_word(
                source, qualifier_start, scan - qualifier_start))
          return 0;
        last_pointer_has_qualifier = 1;
      }
    }
    if (atomic_qualifier_before_name && pointer_count == 0) return 0;
    if (last_pointer_has_qualifier) return 0;
    if (scan >= length || source[scan] != ')') return 0;
    scan++;
  }
  while (scan < length) {
    scan = skip_analysis_space_and_comments_mode(
        source, length, scan, enable_trigraphs);
    if (scan >= length) return 0;
    char c = source[scan];
    if (c == ';' || c == ',' || c == '=' || c == '[' || c == '{' ||
        c == '}')
      return 0;
    if (c == '*' || c == '(') {
      scan++;
      continue;
    }
    if (!is_identifier_byte((unsigned char)c)) return 0;
    size_t candidate_start = scan;
    while (scan < length &&
           is_identifier_byte((unsigned char)source[scan]))
      scan++;
    size_t candidate_length = scan - candidate_start;
    if (analysis_declaration_modifier_word(
            source, candidate_start, candidate_length))
      continue;
    size_t candidate_outer_brace_count = 0;
    size_t candidate_declaration_start = 0;
    int candidate_paren_depth = 0;
    int candidate_bracket_depth = 0;
    int candidate_brace_depth = 0;
    int candidate_definite_declaration = 0;
    int candidate_for_init_declaration = 0;
    return object_declaration_prefix(
               source, candidate_start, &candidate_outer_brace_count,
               &candidate_paren_depth, &candidate_bracket_depth,
               &candidate_brace_depth, &candidate_definite_declaration,
               &candidate_for_init_declaration,
               &candidate_declaration_start) &&
           candidate_definite_declaration &&
           !candidate_for_init_declaration &&
           candidate_declaration_start == declaration_start &&
           candidate_outer_brace_count == outer_brace_count;
  }
  return 0;
}

static char *build_declaration_start_lookup_recovery_source(
    const char *source, size_t declaration_start,
    size_t outer_brace_count, int for_init_declaration, int *changed,
    size_t *source_consumed) {
  static const char regular_marker[] =
      "int " AG_LANGUAGE_CURSOR_MARKER ";\n";
  static const char for_init_marker[] =
      "int " AG_LANGUAGE_CURSOR_MARKER "; ; ) {\n}\n";
  const char *marker = for_init_declaration
                           ? for_init_marker
                           : regular_marker;
  size_t marker_length = strlen(marker);
  if (outer_brace_count > SIZE_MAX / 2 ||
      declaration_start > SIZE_MAX - marker_length ||
      declaration_start + marker_length >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length = declaration_start + marker_length +
                         outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, declaration_start);
  size_t output = declaration_start;
  memcpy(result + output, marker, marker_length);
  output += marker_length;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = declaration_start;
  return result;
}

static char *build_retained_declaration_lookup_recovery_source(
    const char *source, size_t declaration_end,
    size_t outer_brace_count, int *changed,
    size_t *source_consumed) {
  static const char marker[] =
      "\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  size_t marker_length = strlen(marker);
  if (!source || outer_brace_count > SIZE_MAX / 2 ||
      declaration_end > SIZE_MAX - marker_length ||
      declaration_end + marker_length >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length = declaration_end + marker_length +
                         outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, declaration_end);
  size_t output = declaration_end;
  memcpy(result + output, marker, marker_length);
  output += marker_length;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed)
    *changed = AG_LANGUAGE_RECOVERY_CHANGED |
               AG_LANGUAGE_RECOVERY_BLOCK_EXTERN_DECLARATION_RETAINED;
  if (source_consumed) *source_consumed = declaration_end;
  return result;
}

static char *build_prior_declarator_lookup_recovery_source(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, int *changed, size_t *source_consumed) {
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(source, length, cursor, &name, &name_length);
  if (!name || name_length == 0) return NULL;
  size_t name_start = (size_t)(name - source);
  size_t outer_brace_count = 0;
  size_t declaration_start = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  int definite_declaration = 0;
  int for_init_declaration = 0;
  if (!object_declaration_prefix(
          source, name_start, &outer_brace_count, &paren_depth,
          &bracket_depth, &brace_depth, &definite_declaration,
          &for_init_declaration, &declaration_start))
    return NULL;

  if (!for_init_declaration &&
      analysis_typedef_specifier_before_first_declarator(
          source, length, name_start, name_start + name_length,
          declaration_start, outer_brace_count, enable_trigraphs))
    return build_declaration_start_lookup_recovery_source(
        source, declaration_start, outer_brace_count, 0, changed,
        source_consumed);
  if (!definite_declaration) return NULL;

  size_t last_top_level_comma = SIZE_MAX;
  int has_typedef_keyword = 0;
  int has_current_declarator_identifier = 0;
  int has_first_declarator_identifier = 0;
  int current_declarator_has_assignment = 0;
  char first_declarator_delimiter = 0;
  int local_parens = 0;
  int local_brackets = 0;
  int local_braces = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = declaration_start == 0 ||
                      source[declaration_start - 1] == '\n';
  int preprocessor_line = 0;
  for (size_t i = declaration_start; i < name_start; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, name_start, i, enable_trigraphs);
    if (splice_size > 0) {
      i += splice_size - 1;
      continue;
    }
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
      at_line_start = 1;
      preprocessor_line = 0;
      continue;
    }
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    else if (at_line_start && enable_trigraphs && c == '?' &&
             next == '?' && i + 2 < name_start &&
             source[i + 2] == '=') {
      preprocessor_line = 1;
      i += 2;
    }
    at_line_start = 0;
    if (preprocessor_line) continue;
    if (is_identifier_byte((unsigned char)c)) {
      size_t word_start = i;
      while (i + 1 < name_start &&
             is_identifier_byte((unsigned char)source[i + 1]))
        i++;
      size_t word_length = i + 1 - word_start;
      if (local_parens == 0 && local_brackets == 0 &&
          local_braces == 0 &&
          analysis_word_is(
              source, word_start, word_length, "typedef"))
        has_typedef_keyword = 1;
      if (last_top_level_comma == SIZE_MAX &&
          !has_first_declarator_identifier) {
        size_t candidate_outer_brace_count = 0;
        size_t candidate_declaration_start = 0;
        int candidate_paren_depth = 0;
        int candidate_bracket_depth = 0;
        int candidate_brace_depth = 0;
        int candidate_definite_declaration = 0;
        int candidate_for_init_declaration = 0;
        if (object_declaration_prefix(
                source, word_start, &candidate_outer_brace_count,
                &candidate_paren_depth, &candidate_bracket_depth,
                &candidate_brace_depth, &candidate_definite_declaration,
                &candidate_for_init_declaration,
                &candidate_declaration_start) &&
            candidate_definite_declaration &&
            candidate_for_init_declaration == for_init_declaration &&
            candidate_declaration_start == declaration_start &&
            candidate_outer_brace_count == outer_brace_count)
          has_first_declarator_identifier = 1;
      }
      if (last_top_level_comma != SIZE_MAX &&
          word_start > last_top_level_comma)
        has_current_declarator_identifier = 1;
      continue;
    }
    if (has_first_declarator_identifier &&
        first_declarator_delimiter == 0 && (c == '(' || c == '['))
      first_declarator_delimiter = c;
    if (c == ';' && local_parens == 0 && local_brackets == 0 &&
        local_braces == 0)
      return NULL;
    if (c == '(') local_parens++;
    else if (c == ')' && local_parens > 0) local_parens--;
    else if (c == '[') local_brackets++;
    else if (c == ']' && local_brackets > 0) local_brackets--;
    else if (c == '{') local_braces++;
    else if (c == '}' && local_braces > 0) local_braces--;
    else if (c == ',' && local_parens == 0 && local_brackets == 0 &&
             local_braces == 0) {
      last_top_level_comma = i;
      has_current_declarator_identifier = 0;
      current_declarator_has_assignment = 0;
    } else if (c == '=' && local_parens == 0 && local_brackets == 0 &&
             local_braces == 0)
      current_declarator_has_assignment = 1;
  }
  int cursor_in_first_declarator =
      last_top_level_comma == SIZE_MAX &&
      has_first_declarator_identifier &&
      !current_declarator_has_assignment &&
      ((has_typedef_keyword &&
        ((first_declarator_delimiter == '(' && local_parens > 0 &&
          local_brackets == 0) ||
         (first_declarator_delimiter == '[' && local_brackets > 0))) ||
       (!has_typedef_keyword && brace_depth == 0 &&
        paren_depth == 0 &&
        first_declarator_delimiter == '[' && local_brackets > 0));
  if (cursor_in_first_declarator)
    return build_declaration_start_lookup_recovery_source(
        source, declaration_start, outer_brace_count,
        for_init_declaration, changed, source_consumed);
  int cursor_in_later_declarator =
      last_top_level_comma != SIZE_MAX &&
      has_current_declarator_identifier &&
      !current_declarator_has_assignment &&
      (has_typedef_keyword ||
       (paren_depth == 0 && brace_depth == 0 &&
        first_declarator_delimiter != '(' &&
        local_brackets > 0));
  if (!cursor_in_later_declarator)
    return NULL;

  static const char regular_suffix[] =
      ";\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  static const char for_init_suffix[] =
      ", " AG_LANGUAGE_CURSOR_MARKER "; ; ) {\n}\n";
  const char *suffix = for_init_declaration
                           ? for_init_suffix
                           : regular_suffix;
  size_t suffix_length = strlen(suffix);
  if (outer_brace_count > SIZE_MAX / 2 ||
      last_top_level_comma > SIZE_MAX - suffix_length ||
      last_top_level_comma + suffix_length >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length = last_top_level_comma + suffix_length +
                         outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, last_top_level_comma);
  size_t output = last_top_level_comma;
  memcpy(result + output, suffix, suffix_length);
  output += suffix_length;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = last_top_level_comma;
  return result;
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

static int analysis_active_brace_path_at_mode(
    const char *source, size_t length, size_t limit,
    int enable_trigraphs, size_t **path, size_t *path_count) {
  if (path) *path = NULL;
  if (path_count) *path_count = 0;
  if (!source || limit > length || !path || !path_count) return 0;
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
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, i, enable_trigraphs);
    if (splice_size > 0) {
      i += splice_size - 1;
      continue;
    }
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
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    else if (at_line_start && enable_trigraphs && c == '?' &&
             next == '?' && i + 2 < limit && source[i + 2] == '=') {
      preprocessor_line = 1;
      i += 2;
    }
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
    } else if (c == '}') {
      if (brace_count == 0) {
        free(braces);
        return 0;
      }
      brace_count--;
    }
  }
  if (block_comment || quote || preprocessor_line) {
    free(braces);
    return 0;
  }
  *path = braces;
  *path_count = brace_count;
  return 1;
}

static int analysis_brace_path_is_prefix(
    const size_t *prefix, size_t prefix_count,
    const size_t *path, size_t path_count) {
  if (prefix_count > path_count) return 0;
  for (size_t i = 0; i < prefix_count; i++)
    if (prefix[i] != path[i]) return 0;
  return 1;
}

static int analysis_declaration_prefix_contains_word_mode(
    const char *source, size_t length, size_t start, size_t end,
    const char *word, int enable_trigraphs) {
  if (!source || !word || start > end || end > length) return 0;
  size_t scan = start;
  while (scan < end) {
    scan = skip_analysis_space_and_comments_mode(
        source, end, scan, enable_trigraphs);
    if (scan >= end) break;
    if (!is_identifier_byte((unsigned char)source[scan])) {
      scan++;
      continue;
    }
    size_t word_start = scan;
    while (scan < end &&
           is_identifier_byte((unsigned char)source[scan]))
      scan++;
    if (analysis_word_is(
            source, word_start, scan - word_start, word))
      return 1;
  }
  return 0;
}

typedef enum {
  ANALYSIS_VISIBLE_DIRECT_DECLARATION_NONE = 0,
  ANALYSIS_VISIBLE_DIRECT_DECLARATION_FILE_TYPEDEF,
  ANALYSIS_VISIBLE_DIRECT_DECLARATION_OTHER,
} analysis_visible_direct_declaration_kind_t;

static analysis_visible_direct_declaration_kind_t
analysis_visible_direct_declaration_before(
    const char *source, size_t length, size_t limit,
    const char *name, size_t name_length,
    const size_t *current_path, size_t current_path_count,
    int enable_trigraphs) {
  analysis_visible_direct_declaration_kind_t visible =
      ANALYSIS_VISIBLE_DIRECT_DECLARATION_NONE;
  size_t visible_scope_depth = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 1;
  int preprocessor_line = 0;
  for (size_t i = 0; i < limit; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, i, enable_trigraphs);
    if (splice_size > 0) {
      i += splice_size - 1;
      continue;
    }
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
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    else if (at_line_start && enable_trigraphs && c == '?' &&
             next == '?' && i + 2 < limit && source[i + 2] == '=') {
      preprocessor_line = 1;
      i += 2;
    }
    at_line_start = 0;
    if (preprocessor_line ||
        !is_identifier_byte((unsigned char)c))
      continue;
    size_t candidate_start = i;
    while (i + 1 < limit &&
           is_identifier_byte((unsigned char)source[i + 1]))
      i++;
    size_t candidate_length = i + 1 - candidate_start;
    if (candidate_length != name_length ||
        memcmp(source + candidate_start, name, name_length) != 0)
      continue;

    size_t enum_open = 0;
    size_t enum_outer_brace_count = 0;
    size_t enum_item_end = 0;
    if (tag_body_open_at(
            source, candidate_start, "enum", &enum_open,
            &enum_outer_brace_count) &&
        enum_enumerator_bounds(
            source, length, enum_open, candidate_start,
            candidate_start + candidate_length, &enum_item_end) &&
        enum_item_end < limit) {
      size_t *enum_path = NULL;
      size_t enum_path_count = 0;
      if (analysis_active_brace_path_at_mode(
              source, length, enum_open, enable_trigraphs,
              &enum_path, &enum_path_count) &&
          enum_path_count == enum_outer_brace_count &&
          enum_path_count >= visible_scope_depth &&
          analysis_brace_path_is_prefix(
              enum_path, enum_path_count,
              current_path, current_path_count)) {
        visible = ANALYSIS_VISIBLE_DIRECT_DECLARATION_OTHER;
        visible_scope_depth = enum_path_count;
      }
      free(enum_path);
      continue;
    }

    size_t candidate_outer_brace_count = 0;
    size_t candidate_declaration_start = 0;
    int candidate_paren_depth = 0;
    int candidate_bracket_depth = 0;
    int candidate_brace_depth = 0;
    int candidate_definite_declaration = 0;
    int candidate_for_init_declaration = 0;
    if (!object_declaration_prefix(
            source, candidate_start, &candidate_outer_brace_count,
            &candidate_paren_depth, &candidate_bracket_depth,
            &candidate_brace_depth, &candidate_definite_declaration,
            &candidate_for_init_declaration,
            &candidate_declaration_start) ||
        !candidate_definite_declaration)
      continue;
    size_t *candidate_path = NULL;
    size_t candidate_path_count = 0;
    if (!analysis_active_brace_path_at_mode(
            source, length, candidate_start, enable_trigraphs,
            &candidate_path, &candidate_path_count)) {
      free(candidate_path);
      continue;
    }
    int visible_candidate =
        candidate_path_count == candidate_outer_brace_count &&
        candidate_path_count >= visible_scope_depth &&
        analysis_brace_path_is_prefix(
            candidate_path, candidate_path_count,
            current_path, current_path_count);
    free(candidate_path);
    if (!visible_candidate) continue;
    int direct_declaration =
        !candidate_for_init_declaration && candidate_paren_depth == 0 &&
        candidate_bracket_depth == 0 && candidate_brace_depth == 0;
    if (!direct_declaration) {
      visible = ANALYSIS_VISIBLE_DIRECT_DECLARATION_OTHER;
      visible_scope_depth = candidate_path_count;
      continue;
    }
    size_t candidate_declarator_end = 0;
    if (!object_declarator_end(
            source, length, candidate_start + candidate_length,
            candidate_paren_depth, candidate_bracket_depth,
            candidate_brace_depth, &candidate_declarator_end) ||
        candidate_declarator_end >= limit)
      continue;
    int is_typedef = analysis_declaration_prefix_contains_word_mode(
        source, length, candidate_declaration_start, candidate_start,
        "typedef", enable_trigraphs);
    visible = is_typedef && candidate_path_count == 0
                  ? ANALYSIS_VISIBLE_DIRECT_DECLARATION_FILE_TYPEDEF
                  : ANALYSIS_VISIBLE_DIRECT_DECLARATION_OTHER;
    visible_scope_depth = candidate_path_count;
  }
  return visible;
}

static char *build_file_typedef_block_extern_type_recovery_source(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, int *changed, size_t *source_consumed) {
  const char *selected_name = NULL;
  size_t selected_length = 0;
  identifier_at(
      source, length, cursor, &selected_name, &selected_length);
  if (!selected_name || selected_length == 0) return NULL;
  size_t selected_start = (size_t)(selected_name - source);
  size_t scan = selected_start + selected_length;
  size_t candidate_start = SIZE_MAX;
  int has_parenthesized_declarator = 0;
  int has_parenthesized_pointer = 0;
  int has_parenthesized_pointer_qualifier = 0;
  int has_parenthesized_restrict_pointer_qualifier = 0;
  int has_parenthesized_atomic_pointer_qualifier = 0;
  size_t declarator_pointer_count = 0;
  while (scan < length) {
    scan = skip_analysis_space_and_comments_mode(
        source, length, scan, enable_trigraphs);
    if (scan >= length) return NULL;
    if (!has_parenthesized_declarator && source[scan] == '(') {
      if (declarator_pointer_count > 1) return NULL;
      has_parenthesized_declarator = 1;
      scan++;
      continue;
    }
    if (source[scan] == '*') {
      declarator_pointer_count++;
      if (has_parenthesized_declarator &&
          declarator_pointer_count > 2)
        return NULL;
      if (has_parenthesized_declarator) {
        if (declarator_pointer_count == 2 &&
            (has_parenthesized_pointer_qualifier ||
             has_parenthesized_atomic_pointer_qualifier))
          return NULL;
        has_parenthesized_pointer = 1;
      }
      scan++;
      continue;
    }
    if (!is_identifier_byte((unsigned char)source[scan])) return NULL;
    size_t word_start = scan;
    while (scan < length &&
           is_identifier_byte((unsigned char)source[scan]))
      scan++;
    size_t word_length = scan - word_start;
    if (analysis_word_is(
            source, word_start, word_length, "_Atomic")) {
      if (!has_parenthesized_declarator ||
          !has_parenthesized_pointer ||
          declarator_pointer_count > 1 ||
          has_parenthesized_restrict_pointer_qualifier ||
          has_parenthesized_atomic_pointer_qualifier)
        return NULL;
      has_parenthesized_atomic_pointer_qualifier = 1;
      continue;
    }
    if (analysis_type_name_qualifier_word(
            source, word_start, word_length)) {
      if (has_parenthesized_declarator) {
        if (!has_parenthesized_pointer ||
            declarator_pointer_count > 1)
          return NULL;
        if (analysis_word_is(
                source, word_start, word_length, "restrict")) {
          if (has_parenthesized_atomic_pointer_qualifier)
            return NULL;
          has_parenthesized_restrict_pointer_qualifier = 1;
        }
        has_parenthesized_pointer_qualifier = 1;
      }
      continue;
    }
    if (word_length != selected_length ||
        memcmp(source + word_start, selected_name,
               selected_length) != 0)
      return NULL;
    candidate_start = word_start;
    break;
  }
  if (candidate_start == SIZE_MAX) return NULL;
  size_t candidate_end = candidate_start + selected_length;
  size_t declaration_end = skip_analysis_space_and_comments_mode(
      source, length, candidate_end, enable_trigraphs);
  if (has_parenthesized_declarator) {
    if (declaration_end >= length || source[declaration_end] != ')')
      return NULL;
    declaration_end = skip_analysis_space_and_comments_mode(
        source, length, declaration_end + 1, enable_trigraphs);
  }
  int has_array_suffix = 0;
  if (!has_parenthesized_declarator &&
      declaration_end < length && source[declaration_end] == '[') {
    has_array_suffix = 1;
    declaration_end = skip_analysis_space_and_comments_mode(
        source, length, declaration_end + 1, enable_trigraphs);
    if (declaration_end >= length) return NULL;
    if (source[declaration_end] != ']') {
      if (source[declaration_end] < '1' ||
          source[declaration_end] > '9')
        return NULL;
      do {
        declaration_end++;
      } while (declaration_end < length &&
               source[declaration_end] >= '0' &&
               source[declaration_end] <= '9');
      declaration_end = skip_analysis_space_and_comments_mode(
          source, length, declaration_end, enable_trigraphs);
      if (declaration_end >= length || source[declaration_end] != ']')
        return NULL;
    }
    declaration_end = skip_analysis_space_and_comments_mode(
        source, length, declaration_end + 1, enable_trigraphs);
  }
  if (declaration_end >= length || source[declaration_end] != ';')
    return NULL;

  size_t outer_brace_count = 0;
  size_t declaration_start = 0;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  int definite_declaration = 0;
  int for_init_declaration = 0;
  if (!object_declaration_prefix(
          source, candidate_start, &outer_brace_count,
          &paren_depth, &bracket_depth, &brace_depth,
          &definite_declaration, &for_init_declaration,
          &declaration_start) ||
      outer_brace_count == 0 || for_init_declaration ||
      paren_depth != (has_parenthesized_declarator ? 1 : 0) ||
      bracket_depth != 0 || brace_depth != 0)
    return NULL;
  (void)definite_declaration;

  int has_extern = 0;
  scan = declaration_start;
  while (scan < selected_start) {
    scan = skip_analysis_space_and_comments_mode(
        source, selected_start, scan, enable_trigraphs);
    if (scan >= selected_start) break;
    if (!is_identifier_byte((unsigned char)source[scan])) return NULL;
    size_t word_start = scan;
    while (scan < selected_start &&
           is_identifier_byte((unsigned char)source[scan]))
      scan++;
    size_t word_length = scan - word_start;
    if (analysis_word_is(
            source, word_start, word_length, "extern")) {
      if (has_extern) return NULL;
      has_extern = 1;
      continue;
    }
    if (analysis_type_name_qualifier_word(
            source, word_start, word_length) ||
        analysis_word_is(
            source, word_start, word_length, "_Thread_local"))
      continue;
    return NULL;
  }
  if (!has_extern) return NULL;

  size_t *current_path = NULL;
  size_t current_path_count = 0;
  if (!analysis_active_brace_path_at_mode(
          source, length, selected_start, enable_trigraphs,
          &current_path, &current_path_count) ||
      current_path_count != outer_brace_count) {
    free(current_path);
    return NULL;
  }
  analysis_visible_direct_declaration_kind_t visible =
      analysis_visible_direct_declaration_before(
          source, length, declaration_start,
          selected_name, selected_length,
          current_path, current_path_count, enable_trigraphs);
  free(current_path);
  if (visible != ANALYSIS_VISIBLE_DIRECT_DECLARATION_FILE_TYPEDEF)
    return NULL;
  if (has_array_suffix ||
      has_parenthesized_pointer_qualifier ||
      has_parenthesized_atomic_pointer_qualifier)
    return build_retained_declaration_lookup_recovery_source(
        source, declaration_end + 1, outer_brace_count,
        changed, source_consumed);
  return build_declaration_start_lookup_recovery_source(
      source, declaration_start, outer_brace_count, 0,
      changed, source_consumed);
}

static int analysis_complete_direct_object_initializer_operand(
    const char *source, size_t length,
    size_t candidate_start, size_t candidate_end,
    const analysis_identifier_span_t *identifier, int enable_trigraphs,
    size_t *initializer_end) {
  if (!source || !identifier || identifier->logical_length == 0 ||
      candidate_start == SIZE_MAX || candidate_end <= candidate_start ||
      candidate_end >= identifier->start || identifier->start >= length)
    return 0;
  size_t outer_brace_count = 0;
  int declarator_paren_depth = 0;
  int declarator_bracket_depth = 0;
  int declarator_brace_depth = 0;
  int definite_declaration = 0;
  if (!object_declaration_prefix(
          source, candidate_start, &outer_brace_count,
          &declarator_paren_depth, &declarator_bracket_depth,
          &declarator_brace_depth, &definite_declaration, NULL, NULL))
    return 0;
  size_t declarator_end = 0;
  if (!object_declarator_end(
          source, length, candidate_end, declarator_paren_depth,
          declarator_bracket_depth, declarator_brace_depth,
          &declarator_end) ||
      declarator_end < identifier->end ||
      (source[declarator_end] != ';' && source[declarator_end] != ',') ||
      skip_analysis_space_and_comments_mode(
          source, length, identifier->end, enable_trigraphs) != declarator_end)
    return 0;
  if (initializer_end) *initializer_end = declarator_end;
  return 1;
}

static int analysis_old_style_identifier_list_at(
    const char *source, size_t length, size_t open) {
  if (!source || open >= length || source[open] != '(') return 0;
  size_t cursor = skip_analysis_space_and_comments(
      source, length, open + 1);
  if (cursor < length && source[cursor] == ')') return 1;
  for (;;) {
    if (cursor >= length ||
        !(source[cursor] == '_' ||
          (unsigned char)source[cursor] >= 0x80 ||
          isalpha((unsigned char)source[cursor])))
      return 0;
    cursor++;
    while (cursor < length &&
           is_identifier_byte((unsigned char)source[cursor]))
      cursor++;
    cursor = skip_analysis_space_and_comments(
        source, length, cursor);
    if (cursor < length && source[cursor] == ')') return 1;
    if (cursor >= length || source[cursor] != ',') return 0;
    cursor = skip_analysis_space_and_comments(
        source, length, cursor + 1);
  }
}

static int function_declaration_recovery_end(
    const char *source, size_t length, size_t name_end,
    int initial_paren_depth, int initial_bracket_depth,
    size_t *declaration_end, int *is_definition,
    size_t *body_open, size_t *parameter_list_close) {
  if (body_open) *body_open = SIZE_MAX;
  if (parameter_list_close) *parameter_list_close = SIZE_MAX;
  size_t cursor = name_end;
  int direct_function_name =
      initial_paren_depth == 0 && initial_bracket_depth == 0;
  if (direct_function_name) {
    cursor = skip_analysis_space_and_comments(
        source, length, cursor);
    if (cursor >= length || source[cursor] != '(') return 0;
  }
  int old_style_identifier_list = direct_function_name &&
      analysis_old_style_identifier_list_at(source, length, cursor);
  int paren_depth = initial_paren_depth;
  int bracket_depth = initial_bracket_depth;
  int body_depth = 0;
  int declaration_brace_depth = 0;
  int initial_parameter_list_closed = 0;
  int parameter_declaration_has_tokens = 0;
  int multi_declarator_tail = 0;
  int declarator_has_assignment = 0;
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
    if (declaration_brace_depth > 0) {
      if (c == '{') declaration_brace_depth++;
      else if (c == '}') declaration_brace_depth--;
      continue;
    }
    int closed_initial_parameter_list = 0;
    if (c == '(') paren_depth++;
    else if (c == ')' && paren_depth > 0) {
      paren_depth--;
      if (paren_depth == 0 && !initial_parameter_list_closed) {
        initial_parameter_list_closed = 1;
        closed_initial_parameter_list = 1;
        if (parameter_list_close) *parameter_list_close = i;
      }
    } else if (c == '[') bracket_depth++;
    else if (c == ']' && bracket_depth > 0) bracket_depth--;
    else if (paren_depth == 0 && bracket_depth == 0 && c == ';') {
      if (!multi_declarator_tail && old_style_identifier_list &&
          initial_parameter_list_closed &&
          parameter_declaration_has_tokens) {
        parameter_declaration_has_tokens = 0;
        declarator_has_assignment = 0;
        continue;
      }
      *declaration_end = i + 1;
      *is_definition = 0;
      return 1;
    } else if (paren_depth == 0 && bracket_depth == 0 && c == '{') {
      if (declarator_has_assignment || multi_declarator_tail ||
          (old_style_identifier_list && initial_parameter_list_closed &&
           parameter_declaration_has_tokens)) {
        declaration_brace_depth = 1;
      } else {
        if (body_open) *body_open = i;
        body_depth = 1;
      }
    } else if (paren_depth == 0 && bracket_depth == 0 && c == ',') {
      if (!(old_style_identifier_list && initial_parameter_list_closed &&
            parameter_declaration_has_tokens)) {
        multi_declarator_tail = 1;
        declarator_has_assignment = 0;
      }
    } else if (paren_depth == 0 && bracket_depth == 0 && c == '=') {
      declarator_has_assignment = 1;
    }
    if (!multi_declarator_tail && old_style_identifier_list &&
        initial_parameter_list_closed &&
        !closed_initial_parameter_list &&
        !isspace((unsigned char)c) && c != ';' && c != ',' && c != '{')
      parameter_declaration_has_tokens = 1;
  }
  return 0;
}

static int analysis_direct_parameter_array_bound_marker_offset(
    const char *source, size_t length, size_t parameter_open,
    size_t parameter_close, size_t selected_start,
    size_t *marker_offset, size_t *selected_parameter_start) {
  if (!source || !marker_offset || parameter_open >= selected_start ||
      selected_start >= parameter_close || parameter_close >= length ||
      source[parameter_open] != '(' || source[parameter_close] != ')')
    return 0;
  size_t paren_depth = 1;
  size_t bracket_depth = 0;
  size_t brace_depth = 0;
  size_t array_bound_paren_depth = SIZE_MAX;
  size_t parameter_start = parameter_open + 1;
  size_t cursor_parameter_start = SIZE_MAX;
  int cursor_in_direct_array_bound = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 0;
  int preprocessor_line = 0;
  for (size_t i = parameter_open + 1; i <= parameter_close; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, i, 0);
    if (splice_size > 0) {
      i += splice_size - 1;
      continue;
    }
    char c = source[i];
    char next = i + 1 < length ? source[i + 1] : 0;
    if (i == selected_start) {
      cursor_in_direct_array_bound = bracket_depth > 0 &&
                                     array_bound_paren_depth == 1;
      cursor_parameter_start = parameter_start;
    }
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
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    if (cursor_in_direct_array_bound && i > selected_start &&
        paren_depth == 1 && bracket_depth == 0 && brace_depth == 0 &&
        (c == ',' || c == ')')) {
      *marker_offset = i;
      if (selected_parameter_start)
        *selected_parameter_start = cursor_parameter_start;
      return 1;
    }
    if (c == '(') {
      paren_depth++;
    } else if (c == ')' && paren_depth > 1) {
      paren_depth--;
    } else if (c == '[') {
      if (bracket_depth == 0)
        array_bound_paren_depth = paren_depth;
      bracket_depth++;
    } else if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      if (bracket_depth == 0) array_bound_paren_depth = SIZE_MAX;
    } else if (c == '{') {
      brace_depth++;
    } else if (c == '}' && brace_depth > 0) {
      brace_depth--;
    } else if (c == ',' && paren_depth == 1 && bracket_depth == 0 &&
               brace_depth == 0) {
      parameter_start = i + 1;
    }
  }
  return 0;
}

static int analysis_direct_parameter_type_identifier(
    const char *source, size_t length, size_t parameter_open,
    size_t parameter_close, size_t selected_start,
    int enable_trigraphs) {
  if (!source || parameter_open >= selected_start ||
      selected_start >= parameter_close || parameter_close >= length ||
      source[parameter_open] != '(' || source[parameter_close] != ')')
    return 0;
  size_t parameter_start = parameter_open + 1;
  size_t paren_depth = 1;
  size_t bracket_depth = 0;
  size_t brace_depth = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  for (size_t i = parameter_open + 1; i < selected_start; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, i, enable_trigraphs);
    if (splice_size > 0) {
      i += splice_size - 1;
      continue;
    }
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
    else if (c == ')' && paren_depth > 1) paren_depth--;
    else if (c == '[') bracket_depth++;
    else if (c == ']' && bracket_depth > 0) bracket_depth--;
    else if (c == '{') brace_depth++;
    else if (c == '}' && brace_depth > 0) brace_depth--;
    else if (c == ',' && paren_depth == 1 && bracket_depth == 0 &&
             brace_depth == 0)
      parameter_start = i + 1;
  }
  if (paren_depth != 1 || bracket_depth != 0 || brace_depth != 0)
    return 0;
  size_t scan = parameter_start;
  while (scan < selected_start) {
    scan = skip_analysis_space_and_comments_mode(
        source, selected_start, scan, enable_trigraphs);
    if (scan >= selected_start) break;
    if (!is_identifier_byte((unsigned char)source[scan])) return 0;
    size_t word_start = scan;
    while (scan < selected_start &&
           is_identifier_byte((unsigned char)source[scan]))
      scan++;
    if (!analysis_declaration_modifier_word(
            source, word_start, scan - word_start))
      return 0;
  }
  return 1;
}

static char *build_current_callable_parameter_type_recovery_source(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, int *changed, size_t *source_consumed) {
  const char *selected_name = NULL;
  size_t selected_length = 0;
  identifier_at(
      source, length, cursor, &selected_name, &selected_length);
  if (!selected_name || selected_length == 0) return NULL;
  size_t selected_start = (size_t)(selected_name - source);
  size_t outer_brace_count = 0;
  size_t declaration_start = 0;
  int selected_paren_depth = 0;
  int selected_bracket_depth = 0;
  int selected_brace_depth = 0;
  int definite_declaration = 0;
  int for_init_declaration = 0;
  if (!object_declaration_prefix(
          source, selected_start, &outer_brace_count,
          &selected_paren_depth, &selected_bracket_depth,
          &selected_brace_depth, &definite_declaration,
          &for_init_declaration, &declaration_start) ||
      outer_brace_count == 0 || !definite_declaration ||
      for_init_declaration || selected_brace_depth != 0)
    return NULL;

  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = declaration_start == 0 ||
                      source[declaration_start - 1] == '\n';
  int preprocessor_line = 0;
  int has_typedef_keyword = 0;
  for (size_t i = declaration_start; i < selected_start; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, i, enable_trigraphs);
    if (splice_size > 0) {
      i += splice_size - 1;
      continue;
    }
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
      at_line_start = 1;
      preprocessor_line = 0;
      continue;
    }
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    if (!is_identifier_byte((unsigned char)c)) continue;
    size_t candidate_start = i;
    while (i + 1 < selected_start &&
           is_identifier_byte((unsigned char)source[i + 1]))
      i++;
    size_t candidate_length = i + 1 - candidate_start;
    if (analysis_word_is(
            source, candidate_start, candidate_length, "typedef")) {
      has_typedef_keyword = 1;
      continue;
    }
    if (has_typedef_keyword || candidate_length != selected_length ||
        memcmp(source + candidate_start, selected_name,
               selected_length) != 0)
      continue;

    size_t candidate_outer_brace_count = 0;
    size_t candidate_declaration_start = 0;
    int candidate_paren_depth = 0;
    int candidate_bracket_depth = 0;
    int candidate_brace_depth = 0;
    int candidate_definite_declaration = 0;
    int candidate_for_init_declaration = 0;
    if (!object_declaration_prefix(
            source, candidate_start, &candidate_outer_brace_count,
            &candidate_paren_depth, &candidate_bracket_depth,
            &candidate_brace_depth, &candidate_definite_declaration,
            &candidate_for_init_declaration,
            &candidate_declaration_start) ||
        !candidate_definite_declaration ||
        candidate_for_init_declaration || candidate_bracket_depth != 0 ||
        candidate_brace_depth != 0 ||
        candidate_declaration_start != declaration_start ||
        candidate_outer_brace_count != outer_brace_count)
      continue;

    size_t after_candidate = skip_analysis_space_and_comments_mode(
        source, length, candidate_start + candidate_length,
        enable_trigraphs);
    size_t parameter_open = SIZE_MAX;
    if (candidate_paren_depth == 0 && after_candidate < length &&
        source[after_candidate] == '(') {
      parameter_open = after_candidate;
    } else if (candidate_paren_depth == 1 &&
               after_candidate < length && source[after_candidate] == ')') {
      size_t prefix_last_offset = SIZE_MAX;
      char prefix_last = analysis_last_significant_between(
          source, declaration_start, candidate_start,
          enable_trigraphs, &prefix_last_offset);
      int parenthesized_function_name = prefix_last == '(';
      int direct_callback_object =
          prefix_last == '*' && prefix_last_offset != SIZE_MAX &&
          analysis_last_significant_between(
              source, declaration_start, prefix_last_offset,
              enable_trigraphs, NULL) == '(';
      if (!parenthesized_function_name && !direct_callback_object)
        continue;
      after_candidate = skip_analysis_space_and_comments_mode(
          source, length, after_candidate + 1, enable_trigraphs);
      if (after_candidate < length && source[after_candidate] == '(')
        parameter_open = after_candidate;
    }
    if (parameter_open == SIZE_MAX) continue;
    size_t parameter_close = 0;
    if (!analysis_delimited_tail_end_mode(
            source, length, parameter_open + 1, ')', 1,
            enable_trigraphs, &parameter_close) ||
        !analysis_direct_parameter_type_identifier(
            source, length, parameter_open, parameter_close,
            selected_start, enable_trigraphs))
      continue;
    size_t declaration_end = 0;
    int is_definition = 0;
    if (!function_declaration_recovery_end(
            source, length, candidate_start + candidate_length,
            candidate_paren_depth, candidate_bracket_depth,
            &declaration_end, &is_definition, NULL, NULL) ||
        is_definition || declaration_end <= parameter_close)
      continue;
    return build_declaration_start_lookup_recovery_source(
        source, declaration_start, outer_brace_count, 0,
        changed, source_consumed);
  }
  return NULL;
}

typedef struct {
  char open;
  size_t open_offset;
  int is_initializer_brace;
} analysis_designator_frame_t;

static char *build_object_initializer_recovery_source(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs, int *changed, size_t *source_consumed) {
  analysis_identifier_span_t identifier = {0};
  if (!analysis_identifier_span_at_mode(
          source, length, cursor, enable_trigraphs, &identifier) ||
      identifier.logical_length == 0)
    return NULL;
  analysis_designator_frame_t *frames = calloc(
      identifier.start + 1, sizeof(*frames));
  if (!frames) return NULL;
  size_t frame_count = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 1;
  int preprocessor_line = 0;
  char last_significant = 0;
  for (size_t i = 0; i < identifier.start; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, i, enable_trigraphs);
    if (splice_size > 0) {
      i += splice_size - 1;
      continue;
    }
    char c = source[i];
    char next = i + 1 < identifier.start ? source[i + 1] : 0;
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
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r'))
      continue;
    if (at_line_start && enable_trigraphs &&
        i + 2 < identifier.start && c == '?' && next == '?' &&
        source[i + 2] == '=') {
      preprocessor_line = 1;
      at_line_start = 0;
      i += 2;
      continue;
    }
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    if (c == '(' || c == '[' || c == '{') {
      frames[frame_count++] = (analysis_designator_frame_t){
          .open = c,
          .open_offset = i,
          .is_initializer_brace = c == '{' && last_significant == '=',
      };
    } else if (c == ')' || c == ']' || c == '}') {
      if (frame_count == 0) {
        free(frames);
        return NULL;
      }
      char open = frames[frame_count - 1].open;
      if (!((open == '(' && c == ')') ||
            (open == '[' && c == ']') ||
            (open == '{' && c == '}'))) {
        free(frames);
        return NULL;
      }
      frame_count--;
    }
    if (!isspace((unsigned char)c)) last_significant = c;
  }
  size_t initializer_frame = SIZE_MAX;
  for (size_t i = frame_count; i > 0; i--) {
    if (frames[i - 1].open != '{' ||
        !frames[i - 1].is_initializer_brace)
      continue;
    int valid_outer_initializer = 1;
    for (size_t outer = 0; outer + 1 < i; outer++)
      if (frames[outer].open != '{' ||
          frames[outer].is_initializer_brace) {
        valid_outer_initializer = 0;
        break;
      }
    if (valid_outer_initializer) {
      initializer_frame = i - 1;
      break;
    }
  }
  if (initializer_frame == SIZE_MAX) {
    free(frames);
    return NULL;
  }
  size_t outer_brace_count = 0;
  for (size_t i = 0; i < initializer_frame; i++) {
    if (frames[i].open != '{') {
      free(frames);
      return NULL;
    }
    outer_brace_count++;
  }
  size_t initializer_end = 0;
  if (!analysis_delimited_tail_end_mode(
          source, length, frames[initializer_frame].open_offset + 1,
          '}', 1, enable_trigraphs, &initializer_end) ||
      initializer_end + 1 < identifier.end) {
    free(frames);
    return NULL;
  }
  free(frames);
  static const char marker[] =
      ";\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  size_t prefix_length = initializer_end + 1;
  if (outer_brace_count > SIZE_MAX / 2 ||
      prefix_length > SIZE_MAX - sizeof(marker) ||
      prefix_length + sizeof(marker) >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length = prefix_length + sizeof(marker) - 1 +
                         outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, prefix_length);
  size_t output = prefix_length;
  memcpy(result + output, marker, sizeof(marker) - 1);
  output += sizeof(marker) - 1;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = prefix_length;
  return result;
}

static char *build_function_parameter_recovery_source(
    const char *source, size_t length, size_t cursor, int *changed,
    size_t *source_consumed) {
  const char *selected_name = NULL;
  size_t selected_length = 0;
  identifier_at(
      source, length, cursor, &selected_name, &selected_length);
  if (!selected_name || selected_length == 0) return NULL;
  size_t selected_start = (size_t)(selected_name - source);
  size_t selected_end = selected_start + selected_length;
  size_t selected_record_open = 0;
  if (record_body_open_at(
          source, selected_start, &selected_record_open))
    return NULL;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 1;
  int preprocessor_line = 0;
  int top_level_initializer = 0;
  for (size_t i = 0; i < selected_start; i++) {
    char c = source[i];
    char next = i + 1 < selected_start ? source[i + 1] : 0;
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
      if (!preprocessor_line || i == 0 || source[i - 1] != '\\')
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
    if (is_identifier_byte((unsigned char)c)) {
      size_t candidate_end = i + 1;
      while (candidate_end < selected_start &&
             is_identifier_byte((unsigned char)source[candidate_end]))
        candidate_end++;
      if (!top_level_initializer && paren_depth == 0 &&
          bracket_depth == 0) {
        size_t after_candidate = skip_analysis_space_and_comments(
            source, length, candidate_end);
        if (after_candidate < length && source[after_candidate] == '(') {
          size_t candidate_outer_brace_count = 0;
          int candidate_paren_depth = 0;
          int candidate_bracket_depth = 0;
          int candidate_brace_depth = 0;
          size_t declaration_end = 0;
          size_t function_body_open = SIZE_MAX;
          size_t parameter_list_close = SIZE_MAX;
          int is_definition = 0;
          if (function_declaration_recovery_end(
                  source, length, candidate_end, 0, 0,
                  &declaration_end, &is_definition,
                  &function_body_open, &parameter_list_close) &&
              candidate_end <= selected_start &&
              selected_end <= (is_definition
                                   ? function_body_open
                                   : declaration_end)) {
            int candidate_is_declaration = brace_depth == 0;
            if (!candidate_is_declaration)
              candidate_is_declaration =
                  object_declaration_prefix(
                      source, i, &candidate_outer_brace_count,
                      &candidate_paren_depth, &candidate_bracket_depth,
                      &candidate_brace_depth, NULL, NULL, NULL) &&
                  candidate_outer_brace_count == (size_t)brace_depth &&
                  candidate_paren_depth == 0 &&
                  candidate_bracket_depth == 0 &&
                  candidate_brace_depth == 0;
            if (!candidate_is_declaration ||
                (is_definition && candidate_outer_brace_count != 0)) {
              i = candidate_end - 1;
              continue;
            }
            static const char marker[] =
                "\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
            size_t parameter_open = skip_analysis_space_and_comments(
                source, length, candidate_end);
            size_t parameter_marker_offset = 0;
            size_t selected_parameter_start = 0;
            if (analysis_direct_parameter_array_bound_marker_offset(
                    source, length, parameter_open,
                    parameter_list_close, selected_start,
                    &parameter_marker_offset,
                    &selected_parameter_start)) {
              static const char parameter_marker[] =
                  "int " AG_LANGUAGE_CURSOR_MARKER;
              static const char definition_tail[] = ");\n";
              if (selected_parameter_start >= parameter_marker_offset ||
                  parameter_marker_offset > parameter_list_close ||
                  declaration_end < parameter_list_close)
                return NULL;
              size_t tail_start = parameter_list_close;
              size_t tail_length = declaration_end - tail_start;
              const char *tail = source + tail_start;
              if (is_definition) {
                tail = definition_tail;
                tail_length = sizeof(definition_tail) - 1;
              }
              if (candidate_outer_brace_count > SIZE_MAX / 2 ||
                  selected_parameter_start >
                      SIZE_MAX - sizeof(parameter_marker) ||
                  selected_parameter_start + sizeof(parameter_marker) >
                      SIZE_MAX - tail_length ||
                  selected_parameter_start + sizeof(parameter_marker) +
                          tail_length >
                      SIZE_MAX - candidate_outer_brace_count * 2)
                return NULL;
              size_t result_length =
                  selected_parameter_start +
                  sizeof(parameter_marker) - 1 + tail_length +
                  candidate_outer_brace_count * 2;
              char *result = malloc(result_length + 1);
              if (!result) return NULL;
              memcpy(result, source, selected_parameter_start);
              memcpy(result + selected_parameter_start,
                     parameter_marker, sizeof(parameter_marker) - 1);
              size_t output =
                  selected_parameter_start +
                  sizeof(parameter_marker) - 1;
              memcpy(result + output, tail, tail_length);
              output += tail_length;
              for (size_t close = 0;
                   close < candidate_outer_brace_count; close++) {
                result[output++] = '}';
                result[output++] = '\n';
              }
              result[output] = '\0';
              if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
              if (source_consumed) *source_consumed = declaration_end;
              return result;
            }
            if (!is_definition) {
              if (candidate_outer_brace_count > SIZE_MAX / 2 ||
                  declaration_end > SIZE_MAX - sizeof(marker) ||
                  declaration_end + sizeof(marker) >
                      SIZE_MAX - candidate_outer_brace_count * 2)
                return NULL;
              size_t result_length =
                  declaration_end + sizeof(marker) - 1 +
                  candidate_outer_brace_count * 2;
              char *result = malloc(result_length + 1);
              if (!result) return NULL;
              memcpy(result, source, declaration_end);
              memcpy(result + declaration_end,
                     marker, sizeof(marker) - 1);
              size_t output = declaration_end + sizeof(marker) - 1;
              for (size_t close = 0;
                   close < candidate_outer_brace_count; close++) {
                result[output++] = '}';
                result[output++] = '\n';
              }
              result[output] = '\0';
              if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
              if (source_consumed) *source_consumed = declaration_end;
              return result;
            }
            if (function_body_open == SIZE_MAX) return NULL;
            size_t prefix_length = function_body_open + 1;
            if (declaration_end < prefix_length) return NULL;
            size_t tail_length = declaration_end - prefix_length;
            if (prefix_length > SIZE_MAX - sizeof(marker) ||
                prefix_length + sizeof(marker) >
                    SIZE_MAX - tail_length)
              return NULL;
            size_t result_length =
                prefix_length + sizeof(marker) - 1 + tail_length;
            char *result = malloc(result_length + 1);
            if (!result) return NULL;
            memcpy(result, source, prefix_length);
            memcpy(result + prefix_length, marker, sizeof(marker) - 1);
            memcpy(result + prefix_length + sizeof(marker) - 1,
                   source + prefix_length, tail_length);
            result[result_length] = '\0';
            if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
            if (source_consumed) *source_consumed = declaration_end;
            return result;
          }
        }
      }
      i = candidate_end - 1;
      continue;
    }
    if (c == '(') paren_depth++;
    else if (c == ')' && paren_depth > 0) paren_depth--;
    else if (c == '[') bracket_depth++;
    else if (c == ']' && bracket_depth > 0) bracket_depth--;
    else if (c == '{') brace_depth++;
    else if (c == '}' && brace_depth > 0) brace_depth--;
    if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
      if (c == '=') top_level_initializer = 1;
      else if (c == ',' || c == ';' || c == '}')
        top_level_initializer = 0;
    }
  }
  return NULL;
}

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
  int for_init_declaration = 0;
  if (!object_declaration_prefix(
          source, name_start, &outer_brace_count, &paren_depth,
          &bracket_depth, &brace_depth, NULL,
          &for_init_declaration, NULL) ||
      for_init_declaration ||
      brace_depth != 0 || (paren_depth == 0 && bracket_depth != 0))
    return NULL;
  size_t declaration_end = 0;
  int is_definition = 0;
  if (!function_declaration_recovery_end(
          source, length, name_end, paren_depth, bracket_depth,
          &declaration_end, &is_definition, NULL, NULL) ||
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
  int for_init_declaration = 0;
  if (!object_declaration_prefix(
          source, name_start, &outer_brace_count, &paren_depth,
          &bracket_depth, &brace_depth, NULL,
          &for_init_declaration, NULL) ||
      bracket_depth != 0)
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
  static const char regular_suffix[] =
      ";\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  static const char for_init_suffix[] =
      "; ; ) {\nint " AG_LANGUAGE_CURSOR_MARKER ";\n}\n";
  const char *suffix = for_init_declaration
                           ? for_init_suffix
                           : regular_suffix;
  size_t suffix_length = strlen(suffix);
  size_t declarator_length = declarator_end - name_start;
  if (name_start > SIZE_MAX - declarator_length ||
      outer_brace_count > SIZE_MAX / 2 ||
      name_start + declarator_length > SIZE_MAX - suffix_length - 1 ||
      name_start + declarator_length + suffix_length + 1 >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length =
      name_start + declarator_length + suffix_length +
      outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, name_start);
  memcpy(
      result + name_start, source + name_start, declarator_length);
  size_t output = name_start + declarator_length;
  memcpy(result + output, suffix, suffix_length);
  output += suffix_length;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = declarator_end;
  return result;
}

static char *build_declarator_array_bound_recovery_source(
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
  int definite_declaration = 0;
  int for_init_declaration = 0;
  if (!object_declaration_prefix(
          source, name_start, &outer_brace_count, &paren_depth,
          &bracket_depth, &brace_depth, &definite_declaration,
          &for_init_declaration, NULL) ||
      !definite_declaration || bracket_depth == 0)
    return NULL;
  size_t record_open = 0;
  if (record_body_open_at(source, name_start, &record_open)) return NULL;
  size_t declarator_end = 0;
  if (!object_declarator_end(
          source, length, name_end, paren_depth, bracket_depth,
          brace_depth, &declarator_end))
    return NULL;
  static const char regular_suffix[] =
      ";\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  static const char for_init_suffix[] =
      "; ; ) {\nint " AG_LANGUAGE_CURSOR_MARKER ";\n}\n";
  const char *suffix = for_init_declaration
                           ? for_init_suffix
                           : regular_suffix;
  size_t suffix_length = strlen(suffix);
  if (outer_brace_count > SIZE_MAX / 2 ||
      declarator_end > SIZE_MAX - suffix_length - 1 ||
      declarator_end + suffix_length + 1 >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length = declarator_end + suffix_length +
                         outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, declarator_end);
  size_t output = declarator_end;
  memcpy(result + output, suffix, suffix_length);
  output += suffix_length;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = declarator_end;
  return result;
}

static char *append_conditional_validation_tail(
    char *recovery, const char *source, size_t source_length,
    size_t source_consumed, int enable_trigraphs) {
  if (!recovery || source_consumed >= source_length) return recovery;
  size_t scan = source_consumed;
  if (scan > 0 && source[scan - 1] != '\n')
    scan = analysis_preprocessor_directive_line_end_mode(
        source, source_length, scan, enable_trigraphs);
  int has_conditional = 0;
  for (size_t line = scan; line < source_length;) {
    size_t logical_end = analysis_preprocessor_directive_line_end_mode(
        source, source_length, line, enable_trigraphs);
    if (analysis_conditional_logical_line_kind_mode(
            source, source_length, line, logical_end,
            enable_trigraphs) != ANALYSIS_CONDITIONAL_LINE_OTHER) {
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
    size_t logical_end = analysis_preprocessor_directive_line_end_mode(
        source, source_length, line, enable_trigraphs);
    if (analysis_conditional_logical_line_kind_mode(
            source, source_length, line, logical_end,
            enable_trigraphs) != ANALYSIS_CONDITIONAL_LINE_OTHER)
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
  size_t open_offset;
  int is_for_control;
  size_t for_separator_count;
  int is_do_body;
  int is_static_assert;
  int is_offsetof_call;
  int offsetof_has_comma;
  int is_generic_selection;
  size_t generic_separator_count;
  size_t generic_association_start;
  int generic_association_has_colon;
  int generic_has_default_association;
  int requires_type_name;
  int is_sizeof_context;
  int is_postfix_parenthesized;
  int is_compound_literal;
  size_t pending_conditional_count;
} recovery_delimiter_t;

static int analysis_complete_direct_expression_operand_tail(
    const char *source, size_t source_length,
    const recovery_delimiter_t *stack, size_t stack_count,
    const analysis_identifier_span_t *identifier, int enable_trigraphs,
    size_t *statement_end) {
  if (!source || !stack || !identifier || identifier->logical_length == 0 ||
      identifier->end > source_length)
    return 0;
  size_t cursor = skip_analysis_space_and_comments_mode(
      source, source_length, identifier->end, enable_trigraphs);
  for (size_t i = stack_count; i > 0; i--) {
    char open = stack[i - 1].open;
    if (open == '{') break;
    char close = open == '(' ? ')' : open == '[' ? ']' : 0;
    if (!close || cursor >= source_length || source[cursor] != close)
      return 0;
    cursor = skip_analysis_space_and_comments_mode(
        source, source_length, cursor + 1, enable_trigraphs);
  }
  if (cursor >= source_length || source[cursor] != ';') return 0;
  if (statement_end) *statement_end = cursor;
  return 1;
}

static int analysis_simple_call_primary_token_end(
    const char *source, size_t limit, size_t start, int enable_trigraphs,
    size_t *token_end) {
  if (!source || start >= limit) return 0;
  size_t cursor = start;
  size_t quote_start = cursor;
  if (source[quote_start] == 'u' && quote_start + 2 < limit &&
      source[quote_start + 1] == '8' && source[quote_start + 2] == '"') {
    quote_start += 2;
  } else if ((source[quote_start] == 'u' ||
              source[quote_start] == 'U' ||
              source[quote_start] == 'L') &&
             quote_start + 1 < limit &&
             (source[quote_start + 1] == '\'' ||
              source[quote_start + 1] == '"')) {
    quote_start++;
  }
  if (source[quote_start] == '\'' || source[quote_start] == '"') {
    char quote = source[quote_start];
    int escaped = 0;
    for (cursor = quote_start + 1; cursor < limit; cursor++) {
      size_t splice_size = analysis_line_splice_size_mode(
          source, limit, cursor, enable_trigraphs);
      if (splice_size) {
        cursor += splice_size - 1;
        continue;
      }
      char c = source[cursor];
      if (escaped) {
        escaped = 0;
      } else if (c == '\\') {
        escaped = 1;
      } else if (c == quote) {
        if (token_end) *token_end = cursor + 1;
        return 1;
      }
    }
    return 0;
  }
  if (isdigit((unsigned char)source[cursor]) ||
      (source[cursor] == '.' && cursor + 1 < limit &&
       isdigit((unsigned char)source[cursor + 1]))) {
    char previous = 0;
    cursor++;
    while (cursor < limit) {
      unsigned char c = (unsigned char)source[cursor];
      if (isalnum(c) || c == '_' || c == '.') {
        previous = (char)c;
        cursor++;
        continue;
      }
      if ((c == '+' || c == '-') &&
          (previous == 'e' || previous == 'E' ||
           previous == 'p' || previous == 'P')) {
        previous = (char)c;
        cursor++;
        continue;
      }
      break;
    }
    if (token_end) *token_end = cursor;
    return 1;
  }
  if (!is_identifier_start_byte((unsigned char)source[cursor])) return 0;
  cursor++;
  while (cursor < limit &&
         is_identifier_byte((unsigned char)source[cursor]))
    cursor++;
  if (token_end) *token_end = cursor;
  return 1;
}

static int analysis_simple_call_argument_token_end(
    const char *source, size_t limit, size_t start, int enable_trigraphs,
    size_t *token_end) {
  if (!source || start >= limit) return 0;
  size_t cursor = start;
  if (limit - cursor >= strlen("sizeof") &&
      memcmp(source + cursor, "sizeof", strlen("sizeof")) == 0 &&
      (cursor + strlen("sizeof") == limit ||
       !is_identifier_byte(
           (unsigned char)source[cursor + strlen("sizeof")]))) {
    cursor += strlen("sizeof");
    cursor = skip_analysis_space_and_comments_mode(
        source, limit, cursor, enable_trigraphs);
  } else if (source[cursor] == '&' || source[cursor] == '*' ||
             source[cursor] == '!' || source[cursor] == '~' ||
             source[cursor] == '+' || source[cursor] == '-') {
    cursor = skip_analysis_space_and_comments_mode(
        source, limit, cursor + 1, enable_trigraphs);
  }
  if (!analysis_simple_call_primary_token_end(
          source, limit, cursor, enable_trigraphs, &cursor))
    return 0;
  size_t suffix = skip_analysis_space_and_comments_mode(
      source, limit, cursor, enable_trigraphs);
  size_t operator_length = 0;
  if (suffix < limit && source[suffix] == '.') {
    operator_length = 1;
  } else if (suffix + 1 < limit && source[suffix] == '-' &&
             source[suffix + 1] == '>') {
    operator_length = 2;
  }
  if (operator_length) {
    cursor = skip_analysis_space_and_comments_mode(
        source, limit, suffix + operator_length, enable_trigraphs);
    if (cursor >= limit ||
        !is_identifier_start_byte((unsigned char)source[cursor]))
      return 0;
    cursor++;
    while (cursor < limit &&
           is_identifier_byte((unsigned char)source[cursor]))
      cursor++;
  }
  if (token_end) *token_end = cursor;
  return 1;
}

static int analysis_complete_simple_remaining_call_tail(
    const char *source, size_t source_length,
    const recovery_delimiter_t *stack, size_t stack_count,
    const analysis_identifier_span_t *identifier, int enable_trigraphs,
    size_t *statement_end) {
  if (!source || !stack || stack_count == 0 || !identifier ||
      identifier->logical_length == 0 || identifier->end > source_length)
    return 0;
  const recovery_delimiter_t *call = &stack[stack_count - 1];
  if (call->open != '(' || !call->is_postfix_parenthesized) return 0;
  size_t cursor = skip_analysis_space_and_comments_mode(
      source, source_length, identifier->end, enable_trigraphs);
  if (cursor >= source_length || source[cursor] != ',') return 0;
  size_t call_end = 0;
  if (!analysis_delimited_tail_end_mode(
          source, source_length, call->open_offset + 1, ')', 1,
          enable_trigraphs, &call_end))
    return 0;
  cursor++;
  for (;;) {
    cursor = skip_analysis_space_and_comments_mode(
        source, call_end, cursor, enable_trigraphs);
    if (cursor >= call_end) return 0;
    if (!analysis_simple_call_argument_token_end(
            source, call_end, cursor, enable_trigraphs, &cursor))
      return 0;
    cursor = skip_analysis_space_and_comments_mode(
        source, call_end, cursor, enable_trigraphs);
    if (cursor == call_end) break;
    if (source[cursor] != ',') return 0;
    cursor++;
  }
  cursor = skip_analysis_space_and_comments_mode(
      source, source_length, call_end + 1, enable_trigraphs);
  for (size_t i = stack_count - 1; i > 0; i--) {
    char open = stack[i - 1].open;
    if (open == '{') break;
    char close = open == '(' ? ')' : open == '[' ? ']' : 0;
    if (!close || cursor >= source_length || source[cursor] != close)
      return 0;
    cursor = skip_analysis_space_and_comments_mode(
        source, source_length, cursor + 1, enable_trigraphs);
  }
  if (cursor >= source_length || source[cursor] != ';') return 0;
  if (statement_end) *statement_end = cursor;
  return 1;
}

static int analysis_type_name_qualifier_word(
    const char *source, size_t start, size_t length) {
  static const char *const words[] = {
      "const", "volatile", "restrict",
  };
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    if (analysis_word_is(source, start, length, words[i])) return 1;
  return 0;
}

static int analysis_parenthesized_range_is_type_name_mode(
    const char *source, size_t source_length, size_t start, size_t end,
    int enable_trigraphs, int *definite_type_name) {
  if (definite_type_name) *definite_type_name = 0;
  if (!source || start > end || end > source_length) return 0;
  int saw_type_specifier = 0;
  int saw_definite_type_keyword = 0;
  int expects_tag_name = 0;
  int saw_typedef_candidate = 0;
  size_t paren_depth = 0;
  size_t bracket_depth = 0;
  for (size_t i = start; i < end;) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, source_length, i, enable_trigraphs);
    if (splice_size) {
      i += splice_size;
      continue;
    }
    unsigned char c = (unsigned char)source[i];
    if (isspace(c)) {
      i++;
      continue;
    }
    if (c == '/' && i + 1 < end && source[i + 1] == '/') {
      i += 2;
      while (i < end && source[i] != '\n') i++;
      continue;
    }
    if (c == '/' && i + 1 < end && source[i + 1] == '*') {
      i += 2;
      while (i + 1 < end &&
             !(source[i] == '*' && source[i + 1] == '/'))
        i++;
      if (i + 1 >= end) return 0;
      i += 2;
      continue;
    }
    if (is_identifier_byte(c)) {
      analysis_identifier_span_t identifier = {0};
      if (!analysis_identifier_span_at_mode(
              source, source_length, i, enable_trigraphs,
              &identifier) ||
          identifier.start != i || identifier.end > end)
        return 0;
      int is_tag_keyword =
          analysis_identifier_span_matches(
              source, source_length, &identifier, enable_trigraphs,
              "struct", 6) ||
          analysis_identifier_span_matches(
              source, source_length, &identifier, enable_trigraphs,
              "union", 5) ||
          analysis_identifier_span_matches(
              source, source_length, &identifier, enable_trigraphs,
              "enum", 4);
      int is_type_word = 0;
      static const char *const type_words[] = {
          "void", "char", "short", "int", "long", "float",
          "double", "signed", "unsigned", "_Bool", "_Atomic",
          "_Complex", "_Imaginary",
      };
      for (size_t word = 0;
           word < sizeof(type_words) / sizeof(type_words[0]); word++) {
        size_t word_length = strlen(type_words[word]);
        if (analysis_identifier_span_matches(
                source, source_length, &identifier, enable_trigraphs,
                type_words[word], word_length)) {
          is_type_word = 1;
          break;
        }
      }
      int is_qualifier = 0;
      if (!is_tag_keyword && !is_type_word &&
          identifier.logical_length == identifier.end - identifier.start)
        is_qualifier = analysis_type_name_qualifier_word(
            source, identifier.start, identifier.logical_length);
      if (expects_tag_name) {
        expects_tag_name = 0;
      } else if (is_tag_keyword) {
        saw_type_specifier = 1;
        saw_definite_type_keyword = 1;
        expects_tag_name = 1;
      } else if (is_type_word) {
        saw_type_specifier = 1;
        saw_definite_type_keyword = 1;
      } else if (is_qualifier) {
        saw_definite_type_keyword = 1;
        /* Qualifiers may precede a builtin or typedef type name. */
      } else if (!saw_type_specifier && !saw_typedef_candidate) {
        saw_type_specifier = 1;
        saw_typedef_candidate = 1;
      } else if (paren_depth == 0 && bracket_depth == 0) {
        return 0;
      }
      i = identifier.end;
      continue;
    }
    if (c == '(') {
      if (saw_typedef_candidate && paren_depth == 0) return 0;
      paren_depth++;
    } else if (c == ')') {
      if (paren_depth == 0) return 0;
      paren_depth--;
    } else if (c == '[') {
      bracket_depth++;
    } else if (c == ']') {
      if (bracket_depth == 0) return 0;
      bracket_depth--;
    } else if (c == '*' ||
               (c == ',' && paren_depth > 0) ||
               ((isdigit(c) || c == '+' || c == '-' || c == '/' ||
                 c == '%' || c == '<' || c == '>' || c == '&' ||
                 c == '|' || c == '^' || c == '!' || c == '~' ||
                 c == '?' || c == ':' || c == '.') &&
                bracket_depth > 0)) {
      /* Abstract declarators and array bounds remain within the type name. */
    } else {
      return 0;
    }
    i++;
  }
  int is_type_name = saw_type_specifier && !expects_tag_name &&
                     paren_depth == 0 && bracket_depth == 0;
  if (definite_type_name && is_type_name)
    *definite_type_name = saw_definite_type_keyword;
  return is_type_name;
}

typedef enum {
  ANALYSIS_TYPE_NAME_TAIL_NONE = 0,
  ANALYSIS_TYPE_NAME_TAIL_PARENTHESIZED,
  ANALYSIS_TYPE_NAME_TAIL_GENERIC_ASSOCIATION,
} analysis_type_name_tail_kind_t;

typedef struct {
  analysis_type_name_tail_kind_t kind;
  size_t context_frame;
  size_t tail_end;
  int preserve_statement;
  int append_cast_operand;
  int append_compound_literal;
} analysis_type_name_tail_t;

static int analysis_cast_operand_starts_at_mode(
    const char *source, size_t source_length, size_t start,
    int enable_trigraphs) {
  size_t operand = skip_analysis_space_and_comments_mode(
      source, source_length, start, enable_trigraphs);
  if (operand >= source_length) return 0;
  unsigned char c = (unsigned char)source[operand];
  return is_identifier_byte(c) || isdigit(c) || c == '\'' || c == '"' ||
         c == '(' || c == '+' || c == '-' || c == '!' || c == '~' ||
         c == '*' || c == '&';
}

static int analysis_complete_expression_statement_tail_end_mode(
    const char *source, size_t source_length,
    const recovery_delimiter_t *stack, size_t context_frame,
    size_t start, int allow_compound_brace, int enable_trigraphs,
    int allow_comma_boundary, size_t *statement_end,
    int *ends_with_comma) {
  if (ends_with_comma) *ends_with_comma = 0;
  size_t paren_depth = 0;
  size_t bracket_depth = 0;
  size_t brace_depth = 0;
  for (size_t i = 0; i < context_frame; i++) {
    if (stack[i].open == '(') paren_depth++;
    else if (stack[i].open == '[') bracket_depth++;
  }
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  for (size_t cursor = start; cursor < source_length; cursor++) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, source_length, cursor, enable_trigraphs);
    if (splice_size > 0) {
      cursor += splice_size - 1;
      continue;
    }
    char c = source[cursor];
    char next = cursor + 1 < source_length ? source[cursor + 1] : 0;
    if (line_comment) {
      if (c == '\n') line_comment = 0;
      continue;
    }
    if (block_comment) {
      if (c == '*' && next == '/') {
        block_comment = 0;
        cursor++;
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
      cursor++;
      continue;
    }
    if (c == '/' && next == '*') {
      block_comment = 1;
      cursor++;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c == '(') {
      paren_depth++;
    } else if (c == ')') {
      if (paren_depth == 0) return 0;
      paren_depth--;
    } else if (c == '[') {
      bracket_depth++;
    } else if (c == ']') {
      if (bracket_depth == 0) return 0;
      bracket_depth--;
    } else if (c == '{') {
      if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
        if (!allow_compound_brace) return 0;
        allow_compound_brace = 0;
      }
      brace_depth++;
    } else if (c == '}') {
      if (brace_depth == 0) return 0;
      brace_depth--;
    } else if (c == ',' && paren_depth == 0 && bracket_depth == 0 &&
               brace_depth == 0) {
      if (!allow_comma_boundary) return 0;
      if (statement_end) *statement_end = cursor;
      if (ends_with_comma) *ends_with_comma = 1;
      return 1;
    } else if (c == ';' && paren_depth == 0 && bracket_depth == 0 &&
               brace_depth == 0) {
      if (statement_end) *statement_end = cursor;
      return 1;
    }
  }
  return 0;
}

static char *build_compound_literal_initializer_recovery_source(
    const char *source, size_t source_length,
    const recovery_delimiter_t *stack, size_t stack_count,
    const analysis_identifier_span_t *identifier,
    int enable_trigraphs, int *changed, size_t *source_consumed) {
  if (!source || !stack || !identifier || identifier->logical_length == 0)
    return NULL;
  size_t compound_frame = SIZE_MAX;
  for (size_t i = stack_count; i > 0; i--) {
    if (stack[i - 1].open == '{' &&
        stack[i - 1].is_compound_literal) {
      compound_frame = i - 1;
      break;
    }
  }
  if (compound_frame == SIZE_MAX) return NULL;
  size_t compound_end = 0;
  if (!analysis_delimited_tail_end_mode(
          source, source_length, stack[compound_frame].open_offset + 1,
          '}', 1, enable_trigraphs, &compound_end) ||
      identifier->end > compound_end + 1)
    return NULL;
  size_t statement_end = 0;
  int ends_with_comma = 0;
  if (!analysis_complete_expression_statement_tail_end_mode(
          source, source_length, stack, compound_frame,
          compound_end + 1, 0, enable_trigraphs, 1,
          &statement_end, &ends_with_comma))
    return NULL;
  size_t outer_brace_count = 0;
  for (size_t i = 0; i < compound_frame; i++)
    if (stack[i].open == '{') outer_brace_count++;
  static const char marker[] =
      ";\nint " AG_LANGUAGE_CURSOR_MARKER ";\n";
  size_t prefix_length = statement_end + (ends_with_comma ? 0 : 1);
  size_t marker_offset = ends_with_comma ? 0 : 1;
  if (outer_brace_count > SIZE_MAX / 2 ||
      prefix_length > SIZE_MAX - (sizeof(marker) - marker_offset) ||
      prefix_length + sizeof(marker) - marker_offset >
          SIZE_MAX - outer_brace_count * 2)
    return NULL;
  size_t result_length = prefix_length + sizeof(marker) - 1 - marker_offset +
                         outer_brace_count * 2;
  char *result = malloc(result_length + 1);
  if (!result) return NULL;
  memcpy(result, source, prefix_length);
  size_t output = prefix_length;
  memcpy(result + output, marker + marker_offset,
         sizeof(marker) - 1 - marker_offset);
  output += sizeof(marker) - 1 - marker_offset;
  for (size_t i = 0; i < outer_brace_count; i++) {
    result[output++] = '}';
    result[output++] = '\n';
  }
  result[output] = '\0';
  if (changed) *changed = AG_LANGUAGE_RECOVERY_CHANGED;
  if (source_consumed) *source_consumed = prefix_length;
  return result;
}

static analysis_type_name_tail_t analysis_complete_type_name_tail(
    const char *source, size_t source_length,
    const recovery_delimiter_t *stack, size_t stack_count,
    const analysis_identifier_span_t *identifier, int enable_trigraphs) {
  analysis_type_name_tail_t tail = {0};
  if (!source || !stack || !identifier || identifier->logical_length == 0)
    return tail;
  for (size_t i = stack_count; i > 0; i--) {
    const recovery_delimiter_t *context = &stack[i - 1];
    if (context->open != '(' || context->is_for_control ||
        context->is_postfix_parenthesized || context->requires_type_name ||
        context->is_sizeof_context || context->is_generic_selection)
      continue;
    int cursor_in_nested_array_bound = 0;
    for (size_t nested = i; nested < stack_count; nested++)
      if (stack[nested].open == '[') {
        cursor_in_nested_array_bound = 1;
        break;
      }
    if (cursor_in_nested_array_bound) continue;
    size_t type_name_end = 0;
    if (!analysis_delimited_tail_end_mode(
            source, source_length, context->open_offset + 1, ')', 1,
            enable_trigraphs, &type_name_end) ||
        identifier->end > type_name_end ||
        !analysis_parenthesized_range_is_type_name_mode(
            source, source_length, context->open_offset + 1,
            type_name_end, enable_trigraphs, NULL))
      continue;
    size_t after_type = skip_analysis_space_and_comments_mode(
        source, source_length, type_name_end + 1, enable_trigraphs);
    size_t compound_end = 0;
    if (after_type >= source_length || source[after_type] != '{' ||
        !analysis_delimited_tail_end_mode(
            source, source_length, after_type + 1, '}', 1,
            enable_trigraphs, &compound_end))
      continue;
    tail.kind = ANALYSIS_TYPE_NAME_TAIL_PARENTHESIZED;
    tail.context_frame = i - 1;
    tail.tail_end = type_name_end;
    tail.append_compound_literal = 1;
    return tail;
  }
  size_t array_frame = SIZE_MAX;
  size_t array_end = 0;
  for (size_t i = stack_count; i > 0; i--) {
    if (stack[i - 1].open != '[') continue;
    if (!analysis_delimited_tail_end_mode(
            source, source_length, stack[i - 1].open_offset + 1, ']', 1,
            enable_trigraphs, &array_end) ||
        identifier->end > array_end)
      continue;
    array_frame = i - 1;
    break;
  }
  if (array_frame == SIZE_MAX) return tail;
  for (size_t i = array_frame; i > 0; i--) {
    const recovery_delimiter_t *context = &stack[i - 1];
    if (context->open != '(') continue;
    if (context->is_generic_selection) {
      if (context->generic_separator_count == 0 ||
          context->generic_association_has_colon)
        continue;
      size_t association_end = 0;
      if (!analysis_delimited_tail_end_mode(
              source, source_length, context->generic_association_start,
              ':', 0, enable_trigraphs, &association_end) ||
          identifier->end > association_end ||
          !analysis_parenthesized_range_is_type_name_mode(
              source, source_length, context->generic_association_start,
              association_end, enable_trigraphs, NULL))
        continue;
      tail.kind = ANALYSIS_TYPE_NAME_TAIL_GENERIC_ASSOCIATION;
      tail.context_frame = i - 1;
      tail.tail_end = association_end;
      return tail;
    }
    if (context->is_for_control || context->is_postfix_parenthesized)
      continue;
    size_t type_name_end = 0;
    if (!analysis_delimited_tail_end_mode(
            source, source_length, context->open_offset + 1, ')', 1,
            enable_trigraphs, &type_name_end) ||
        identifier->end > type_name_end ||
        !analysis_parenthesized_range_is_type_name_mode(
            source, source_length, context->open_offset + 1,
            type_name_end, enable_trigraphs, NULL))
      continue;
    int is_explicit_type_name_context =
        context->requires_type_name || context->is_sizeof_context;
    size_t after_type = skip_analysis_space_and_comments_mode(
        source, source_length, type_name_end + 1, enable_trigraphs);
    int append_compound_literal =
        !is_explicit_type_name_context && after_type < source_length &&
        source[after_type] == '{';
    int append_cast_operand =
        !is_explicit_type_name_context && !append_compound_literal &&
        analysis_cast_operand_starts_at_mode(
            source, source_length, type_name_end + 1, enable_trigraphs);
    if (!is_explicit_type_name_context && !append_compound_literal &&
        !append_cast_operand)
      continue;
    size_t statement_end = 0;
    int preserve_statement =
        !is_explicit_type_name_context &&
        analysis_complete_expression_statement_tail_end_mode(
            source, source_length, stack, i - 1, type_name_end + 1,
            append_compound_literal, enable_trigraphs, 0,
            &statement_end, NULL);
    tail.kind = ANALYSIS_TYPE_NAME_TAIL_PARENTHESIZED;
    tail.context_frame = i - 1;
    tail.tail_end = preserve_statement ? statement_end : type_name_end;
    tail.preserve_statement = preserve_statement;
    tail.append_cast_operand = append_cast_operand;
    tail.append_compound_literal = append_compound_literal;
    return tail;
  }
  return tail;
}

static char *build_recovery_source(const char *source, size_t source_length,
                                   size_t cursor,
                                   int enable_trigraphs,
                                   int preserve_ambiguous_eof_identifier,
                                   int *changed) {
  size_t macro_definition_end =
      analysis_complete_macro_definition_at_cursor(
          source, source_length, cursor, enable_trigraphs);
  int cursor_on_complete_macro_definition = macro_definition_end > 0;
  size_t preprocessor_directive_line_start = 0;
  int cursor_on_complete_preprocessor_operand =
      !cursor_on_complete_macro_definition &&
      analysis_preprocessor_operand_line_start_at_cursor(
          source, source_length, cursor, enable_trigraphs,
          &preprocessor_directive_line_start);
  size_t recovery_cursor = cursor_on_complete_macro_definition
                               ? macro_definition_end
                           : cursor_on_complete_preprocessor_operand
                               ? preprocessor_directive_line_start
                               : analysis_complete_line_splice_at_cursor(
                                     source, source_length, cursor,
                                     enable_trigraphs);
  size_t source_consumed = recovery_cursor;
  if (!cursor_on_complete_macro_definition &&
      !cursor_on_complete_preprocessor_operand) {
    char *file_typedef_block_extern_type_recovery =
        build_file_typedef_block_extern_type_recovery_source(
            source, source_length, recovery_cursor, enable_trigraphs,
            changed, &source_consumed);
    if (file_typedef_block_extern_type_recovery)
      return append_conditional_validation_tail(
          file_typedef_block_extern_type_recovery,
          source, source_length, source_consumed,
          enable_trigraphs);
    char *prior_declarator_lookup_recovery =
        build_prior_declarator_lookup_recovery_source(
            source, source_length, recovery_cursor, enable_trigraphs,
            changed, &source_consumed);
    if (prior_declarator_lookup_recovery)
      return append_conditional_validation_tail(
          prior_declarator_lookup_recovery, source, source_length,
          source_consumed, enable_trigraphs);
    char *current_callable_parameter_type_recovery =
        build_current_callable_parameter_type_recovery_source(
            source, source_length, recovery_cursor, enable_trigraphs,
            changed, &source_consumed);
    if (current_callable_parameter_type_recovery)
      return append_conditional_validation_tail(
          current_callable_parameter_type_recovery,
          source, source_length, source_consumed,
          enable_trigraphs);
    char *callback_parameter_bound_recovery =
        build_direct_callback_parameter_bound_recovery_source(
            source, source_length, recovery_cursor, enable_trigraphs,
            changed, &source_consumed);
    if (callback_parameter_bound_recovery)
      return append_conditional_validation_tail(
          callback_parameter_bound_recovery, source, source_length,
          source_consumed, enable_trigraphs);
    char *function_parameter_recovery =
        build_function_parameter_recovery_source(
            source, source_length, recovery_cursor, changed,
            &source_consumed);
    if (function_parameter_recovery)
      return append_conditional_validation_tail(
          function_parameter_recovery, source, source_length,
          source_consumed, enable_trigraphs);
    char *enum_recovery = build_enum_declaration_recovery_source(
        source, source_length, recovery_cursor,
        enable_trigraphs, preserve_ambiguous_eof_identifier, changed,
        &source_consumed);
    if (enum_recovery)
      return append_conditional_validation_tail(
          enum_recovery, source, source_length, source_consumed,
          enable_trigraphs);
    char *record_member_recovery = build_record_member_recovery_source(
        source, source_length, recovery_cursor, enable_trigraphs, changed,
        &source_consumed);
    if (record_member_recovery)
      return append_conditional_validation_tail(
          record_member_recovery, source, source_length,
          source_consumed, enable_trigraphs);
    char *declarator_array_bound_recovery =
        build_declarator_array_bound_recovery_source(
            source, source_length, recovery_cursor, changed,
            &source_consumed);
    if (declarator_array_bound_recovery)
      return append_conditional_validation_tail(
          declarator_array_bound_recovery, source, source_length,
          source_consumed, enable_trigraphs);
    char *jump_label_recovery = build_jump_label_recovery_source(
        source, source_length, recovery_cursor, changed);
    if (jump_label_recovery) return jump_label_recovery;
    char *function_recovery =
        build_function_declaration_recovery_source(
            source, source_length, recovery_cursor, changed,
            &source_consumed);
    if (function_recovery)
      return append_conditional_validation_tail(
          function_recovery, source, source_length, source_consumed,
          enable_trigraphs);
    char *object_recovery = build_object_declaration_recovery_source(
        source, source_length, recovery_cursor, changed,
        &source_consumed);
    if (object_recovery)
      return append_conditional_validation_tail(
          object_recovery, source, source_length, source_consumed,
          enable_trigraphs);
  }
  int has_complete_identifier = 0;
  analysis_identifier_span_t cursor_identifier = {0};
  int has_cursor_identifier = analysis_identifier_span_at_mode(
      source, source_length, cursor, enable_trigraphs,
      &cursor_identifier);
  size_t cursor_name_end = 0;
  if (has_cursor_identifier) {
    cursor_name_end = cursor_identifier.end;
    /* A delimiter after the name proves that this is a complete source token,
     * rather than an identifier prefix still being typed at EOF. */
    if (cursor_identifier.end < source_length &&
        cursor >= cursor_identifier.start &&
        cursor <= cursor_identifier.end)
      has_complete_identifier = 1;
  }
  int cursor_on_complete_member_access =
      has_complete_identifier &&
      analysis_identifier_is_complete_member_access(
          source, source_length, &cursor_identifier);
  int cursor_identifier_starts_conditional = 0;
  int cursor_identifier_starts_parenthesized_suffix = 0;
  int cursor_identifier_has_complete_parenthesized_suffix = 0;
  size_t cursor_parenthesized_suffix_end = 0;
  if (has_complete_identifier) {
    size_t after_name = skip_analysis_space_and_comments_mode(
        source, source_length, cursor_identifier.end,
        enable_trigraphs);
    cursor_identifier_starts_conditional =
        after_name < source_length && source[after_name] == '?';
    cursor_identifier_starts_parenthesized_suffix =
        after_name < source_length && source[after_name] == '(';
    if (cursor_identifier_starts_parenthesized_suffix)
      cursor_identifier_has_complete_parenthesized_suffix =
          analysis_delimited_tail_end_mode(
              source, source_length, after_name + 1, ')', 1,
              enable_trigraphs, &cursor_parenthesized_suffix_end);
  }
  if (recovery_cursor > SIZE_MAX - 8192 ||
      source_length > SIZE_MAX - recovery_cursor - 8192)
    return NULL;
  size_t capacity = source_length + recovery_cursor + 8192;
  if (capacity > (size_t)INT_MAX) return NULL;
  char *result = calloc(capacity, 1);
  recovery_delimiter_t *stack = calloc(
      recovery_cursor + 1, sizeof(*stack));
  if (!result || !stack) {
    free(result);
    free(stack);
    return NULL;
  }
  memcpy(result, source, recovery_cursor);
  int identifier_elided = has_cursor_identifier &&
                          !cursor_on_complete_macro_definition &&
                          !cursor_on_complete_preprocessor_operand &&
                          !cursor_on_complete_member_access;
  if (!cursor_on_complete_macro_definition &&
      !cursor_on_complete_preprocessor_operand &&
      !cursor_on_complete_member_access && has_cursor_identifier) {
    size_t elide_end = cursor < cursor_identifier.end
                           ? cursor : cursor_identifier.end;
    for (size_t i = cursor_identifier.start; i < elide_end; i++)
      if (is_identifier_byte((unsigned char)source[i])) result[i] = ' ';
    size_t operator_end = cursor_identifier.start;
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
  int previous_token_is_do = 0;
  int previous_token_is_static_assert = 0;
  int previous_token_is_offsetof = 0;
  int previous_token_is_generic = 0;
  int previous_token_requires_type_name = 0;
  int previous_token_is_sizeof = 0;
  int previous_token_is_case = 0;
  int previous_token_requires_expression = 0;
  int previous_token_is_tag_keyword = 0;
  int previous_token_ends_expression = 0;
  int simple_do_statement_active = 0;
  size_t root_pending_conditional_count = 0;
  int case_expression_active = 0;
  size_t case_expression_start = 0;
  size_t case_expression_stack_count = 0;
  size_t case_expression_parent_pending_conditional_count = 0;
  char last_significant = 0;
  size_t last_closed_parenthesized_end = 0;
  int last_closed_parenthesized_is_type_name = 0;
  int last_closed_parenthesized_is_definite_type_name = 0;
  size_t last_declarator_identifier_start = SIZE_MAX;
  size_t last_declarator_identifier_end = 0;
  size_t direct_initializer_candidate_start = SIZE_MAX;
  size_t direct_initializer_candidate_end = 0;
  for (size_t i = 0; i < recovery_cursor; i++) {
    size_t splice_size = analysis_line_splice_size_mode(
        result, recovery_cursor, i, enable_trigraphs);
    if (splice_size) {
      i += splice_size - 1;
      continue;
    }
    char c = result[i];
    char next = i + 1 < recovery_cursor ? result[i + 1] : 0;
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
      previous_token_is_do = 0;
      previous_token_is_static_assert = 0;
      previous_token_is_offsetof = 0;
      previous_token_is_generic = 0;
      previous_token_requires_type_name = 0;
      previous_token_is_sizeof = 0;
      previous_token_is_case = 0;
      previous_token_requires_expression = 0;
      previous_token_is_tag_keyword = 0;
      previous_token_ends_expression = 1;
      continue;
    }
    if (c == '\n') {
      at_line_start = 1;
      preprocessor_line = 0;
      continue;
    }
    if (at_line_start && (c == ' ' || c == '\t' || c == '\r')) continue;
    if (at_line_start && enable_trigraphs && i + 2 < recovery_cursor &&
        c == '?' && next == '?' && result[i + 2] == '=') {
      preprocessor_line = 1;
      at_line_start = 0;
      i += 2;
      continue;
    }
    if (at_line_start && c == '#') preprocessor_line = 1;
    at_line_start = 0;
    if (preprocessor_line) continue;
    int opens_for_control = 0;
    int opens_do_body = 0;
    int opens_static_assert = 0;
    int opens_offsetof = 0;
    int opens_generic_selection = 0;
    int opens_type_name_context = 0;
    int opens_sizeof_context = 0;
    int opens_postfix_parenthesized = 0;
    int opens_compound_literal = 0;
    int current_identifier_is_default = 0;
    int at_direct_initializer_level =
        stack_count == 0 || stack[stack_count - 1].open == '{' ||
        (stack[stack_count - 1].open == '(' &&
         stack[stack_count - 1].is_for_control &&
         stack[stack_count - 1].for_separator_count == 0);
    if (is_identifier_byte((unsigned char)c)) {
      if (i == 0 ||
          !is_identifier_byte((unsigned char)result[i - 1])) {
        size_t identifier_end = i + 1;
        while (identifier_end < recovery_cursor &&
               is_identifier_byte((unsigned char)result[identifier_end]))
          identifier_end++;
        if (at_direct_initializer_level &&
            direct_initializer_candidate_start == SIZE_MAX) {
          last_declarator_identifier_start = i;
          last_declarator_identifier_end = identifier_end;
        }
        previous_token_is_for = identifier_end - i == strlen("for") &&
                                memcmp(result + i, "for", strlen("for")) == 0;
        previous_token_is_do = identifier_end - i == strlen("do") &&
                               memcmp(result + i, "do", strlen("do")) == 0;
        if (previous_token_is_do) simple_do_statement_active = 1;
        previous_token_is_static_assert =
            identifier_end - i == strlen("_Static_assert") &&
            memcmp(result + i, "_Static_assert",
                   strlen("_Static_assert")) == 0;
        previous_token_is_offsetof =
            (identifier_end - i == strlen("__builtin_offsetof") &&
             memcmp(result + i, "__builtin_offsetof",
                    strlen("__builtin_offsetof")) == 0) ||
            (identifier_end - i == strlen("offsetof") &&
             memcmp(result + i, "offsetof", strlen("offsetof")) == 0);
        previous_token_is_generic =
            identifier_end - i == strlen("_Generic") &&
            memcmp(result + i, "_Generic", strlen("_Generic")) == 0;
        previous_token_requires_type_name =
            (identifier_end - i == strlen("_Alignof") &&
             memcmp(result + i, "_Alignof", strlen("_Alignof")) == 0) ||
            (identifier_end - i == strlen("_Atomic") &&
             memcmp(result + i, "_Atomic", strlen("_Atomic")) == 0);
        previous_token_is_sizeof =
            identifier_end - i == strlen("sizeof") &&
            memcmp(result + i, "sizeof", strlen("sizeof")) == 0;
        previous_token_is_case =
            identifier_end - i == strlen("case") &&
            memcmp(result + i, "case", strlen("case")) == 0;
        if (previous_token_is_case) {
          case_expression_active = 1;
          case_expression_start = identifier_end;
          case_expression_stack_count = stack_count;
          case_expression_parent_pending_conditional_count =
              stack_count > 0
                  ? stack[stack_count - 1].pending_conditional_count
                  : root_pending_conditional_count;
        }
        previous_token_requires_expression =
            previous_token_is_sizeof ||
            (identifier_end - i == strlen("return") &&
             memcmp(result + i, "return", strlen("return")) == 0) ||
            previous_token_is_case;
        previous_token_is_tag_keyword =
            (identifier_end - i == strlen("struct") &&
             memcmp(result + i, "struct", strlen("struct")) == 0) ||
            (identifier_end - i == strlen("union") &&
             memcmp(result + i, "union", strlen("union")) == 0) ||
            (identifier_end - i == strlen("enum") &&
             memcmp(result + i, "enum", strlen("enum")) == 0);
        current_identifier_is_default =
            identifier_end - i == strlen("default") &&
            memcmp(result + i, "default", strlen("default")) == 0;
        int identifier_is_expression_prefix_keyword =
            previous_token_is_for || previous_token_is_do ||
            previous_token_is_static_assert ||
            previous_token_is_offsetof ||
            previous_token_is_generic ||
            previous_token_requires_type_name ||
            previous_token_requires_expression;
        previous_token_ends_expression =
            !identifier_is_expression_prefix_keyword;
      }
    } else if (!isspace((unsigned char)c)) {
      int adjacent_definite_type_name_cast = 0;
      if (c == '(' && previous_token_ends_expression &&
          last_closed_parenthesized_is_type_name &&
          last_closed_parenthesized_end <= i) {
        size_t after_type_name = skip_analysis_space_and_comments_mode(
            source, source_length, last_closed_parenthesized_end,
            enable_trigraphs);
        int next_parenthesized_is_definite_type_name = 0;
        if (after_type_name == i &&
            !last_closed_parenthesized_is_definite_type_name) {
          size_t next_parenthesized_end = 0;
          if (analysis_delimited_tail_end_mode(
                  source, source_length, i + 1, ')', 1,
                  enable_trigraphs, &next_parenthesized_end))
            analysis_parenthesized_range_is_type_name_mode(
                source, source_length, i + 1,
                next_parenthesized_end, enable_trigraphs,
                &next_parenthesized_is_definite_type_name);
        }
        adjacent_definite_type_name_cast =
            after_type_name == i &&
            (last_closed_parenthesized_is_definite_type_name ||
             next_parenthesized_is_definite_type_name);
      }
      opens_for_control = c == '(' && previous_token_is_for;
      opens_do_body = c == '{' && previous_token_is_do;
      opens_static_assert = c == '(' && previous_token_is_static_assert;
      opens_offsetof = c == '(' && previous_token_is_offsetof;
      opens_generic_selection = c == '(' && previous_token_is_generic;
      opens_type_name_context =
          c == '(' && previous_token_requires_type_name;
      opens_sizeof_context = c == '(' && previous_token_is_sizeof;
      opens_postfix_parenthesized =
          c == '(' && previous_token_ends_expression &&
          !adjacent_definite_type_name_cast;
      if (c == '{' && last_closed_parenthesized_is_type_name &&
          last_closed_parenthesized_end <= i) {
        size_t after_type_name = skip_analysis_space_and_comments_mode(
            source, source_length, last_closed_parenthesized_end,
            enable_trigraphs);
        opens_compound_literal = after_type_name == i;
      }
      previous_token_is_for = 0;
      previous_token_is_do = 0;
      previous_token_is_static_assert = 0;
      previous_token_is_offsetof = 0;
      previous_token_is_generic = 0;
      previous_token_requires_type_name = 0;
      previous_token_is_sizeof = 0;
      previous_token_is_case = 0;
      previous_token_requires_expression = 0;
      previous_token_is_tag_keyword = 0;
      previous_token_ends_expression = 0;
      if (c == '{') simple_do_statement_active = 0;
    }
    if (at_direct_initializer_level && c == '=' &&
        direct_initializer_candidate_start == SIZE_MAX && next != '=' &&
        (i == 0 || (result[i - 1] != '=' && result[i - 1] != '!' &&
                    result[i - 1] != '<' && result[i - 1] != '>' &&
                    result[i - 1] != '+' && result[i - 1] != '-' &&
                    result[i - 1] != '*' && result[i - 1] != '/' &&
                    result[i - 1] != '%' && result[i - 1] != '&' &&
                    result[i - 1] != '|' && result[i - 1] != '^'))) {
      direct_initializer_candidate_start =
          last_declarator_identifier_start;
      direct_initializer_candidate_end = last_declarator_identifier_end;
    }
    if (at_direct_initializer_level &&
        (c == ';' || c == ',' || c == '{' || c == '}')) {
      last_declarator_identifier_start = SIZE_MAX;
      last_declarator_identifier_end = 0;
      direct_initializer_candidate_start = SIZE_MAX;
      direct_initializer_candidate_end = 0;
    }
    if (c == '(' || c == '[' || c == '{') {
      stack[stack_count++] = (recovery_delimiter_t){
          .open = c,
          .open_offset = i,
          .is_for_control = opens_for_control,
          .for_separator_count = 0,
          .is_do_body = opens_do_body,
          .is_static_assert = opens_static_assert,
          .is_offsetof_call = opens_offsetof,
          .offsetof_has_comma = 0,
          .is_generic_selection = opens_generic_selection,
          .generic_separator_count = 0,
          .generic_association_start = i + 1,
          .generic_association_has_colon = 0,
          .generic_has_default_association = 0,
          .requires_type_name = opens_type_name_context,
          .is_sizeof_context = opens_sizeof_context,
          .is_postfix_parenthesized = opens_postfix_parenthesized,
          .is_compound_literal = opens_compound_literal,
          .pending_conditional_count = 0,
      };
    } else if ((c == ')' || c == ']' || c == '}') && stack_count > 0) {
      recovery_delimiter_t closing = stack[stack_count - 1];
      char open = closing.open;
      if ((open == '(' && c == ')') || (open == '[' && c == ']') ||
          (open == '{' && c == '}')) {
        if (open == '(' && c == ')') {
          int definite_type_name = 0;
          last_closed_parenthesized_end = i + 1;
          last_closed_parenthesized_is_type_name =
              !closing.is_for_control &&
              !closing.is_generic_selection &&
              !closing.requires_type_name &&
              !closing.is_sizeof_context &&
              !closing.is_postfix_parenthesized &&
              analysis_parenthesized_range_is_type_name_mode(
                  source, source_length, closing.open_offset + 1, i,
                  enable_trigraphs, &definite_type_name);
          last_closed_parenthesized_is_definite_type_name =
              last_closed_parenthesized_is_type_name &&
              definite_type_name;
        }
        stack_count--;
        previous_token_ends_expression = c == ')' || c == ']';
      }
    } else if (c == ';' && stack_count > 0 &&
               stack[stack_count - 1].open == '(' &&
               stack[stack_count - 1].is_for_control) {
      stack[stack_count - 1].for_separator_count++;
    }
    if (c == ';' &&
        (stack_count == 0 || stack[stack_count - 1].open == '{'))
      simple_do_statement_active = 0;
    if (c == ',' && stack_count > 0 &&
        stack[stack_count - 1].is_offsetof_call)
      stack[stack_count - 1].offsetof_has_comma = 1;
    size_t *pending_conditional_count =
        stack_count > 0
            ? &stack[stack_count - 1].pending_conditional_count
            : &root_pending_conditional_count;
    int closes_case_expression =
        case_expression_active && c == ':' &&
        stack_count == case_expression_stack_count &&
        *pending_conditional_count ==
            case_expression_parent_pending_conditional_count;
    if (stack_count > 0 && stack[stack_count - 1].is_generic_selection) {
      recovery_delimiter_t *generic = &stack[stack_count - 1];
      if (current_identifier_is_default &&
          generic->generic_separator_count > 0 &&
          !generic->generic_association_has_colon)
        generic->generic_has_default_association = 1;
      if (c == ',') {
        generic->generic_separator_count++;
        generic->generic_association_start = i + 1;
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
    if (closes_case_expression) case_expression_active = 0;
    if (!isspace((unsigned char)c)) last_significant = c;
  }
  if (has_complete_identifier) {
    size_t compound_literal_consumed = recovery_cursor;
    char *compound_literal_initializer_recovery =
        build_compound_literal_initializer_recovery_source(
            source, source_length, stack, stack_count,
            &cursor_identifier, enable_trigraphs, changed,
            &compound_literal_consumed);
    if (compound_literal_initializer_recovery) {
      free(stack);
      free(result);
      return append_conditional_validation_tail(
          compound_literal_initializer_recovery, source,
          source_length, compound_literal_consumed,
          enable_trigraphs);
    }
  }
  if (!cursor_on_complete_macro_definition) {
    size_t object_initializer_consumed = recovery_cursor;
    char *object_initializer_recovery =
        build_object_initializer_recovery_source(
            source, source_length, recovery_cursor, enable_trigraphs,
            changed, &object_initializer_consumed);
    if (object_initializer_recovery) {
      free(stack);
      free(result);
      return append_conditional_validation_tail(
          object_initializer_recovery, source, source_length,
          object_initializer_consumed, enable_trigraphs);
    }
  }
  size_t length = recovery_cursor;
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
  int cursor_in_required_type_name =
      stack_count > 0 && stack[stack_count - 1].requires_type_name;
  size_t complete_static_assert_frame = SIZE_MAX;
  size_t complete_static_assert_end = 0;
  if (has_complete_identifier) {
    for (size_t i = stack_count; i > 0; i--) {
      if (!stack[i - 1].is_static_assert) continue;
      size_t close = 0;
      if (!analysis_delimited_tail_end_mode(
              source, source_length, stack[i - 1].open_offset + 1,
              ')', 1, enable_trigraphs, &close) ||
          cursor_identifier.end > close)
        break;
      size_t after_close = skip_analysis_space_and_comments_mode(
          source, source_length, close + 1, enable_trigraphs);
      if (after_close >= source_length || source[after_close] != ';')
        break;
      complete_static_assert_frame = i - 1;
      complete_static_assert_end = after_close;
      break;
    }
  }
  size_t complete_offsetof_frame = SIZE_MAX;
  size_t complete_offsetof_end = 0;
  if (has_complete_identifier) {
    for (size_t i = stack_count; i > 0; i--) {
      if (!stack[i - 1].is_offsetof_call ||
          stack[i - 1].offsetof_has_comma)
        continue;
      size_t close = 0;
      if (!analysis_delimited_tail_end_mode(
              source, source_length, stack[i - 1].open_offset + 1,
              ')', 1, enable_trigraphs, &close) ||
          cursor_identifier.end > close)
        break;
      complete_offsetof_frame = i - 1;
      complete_offsetof_end = close;
      break;
    }
  }
  analysis_type_name_tail_t type_name_tail = {0};
  if (has_complete_identifier)
    type_name_tail = analysis_complete_type_name_tail(
            source, source_length, stack, stack_count,
            &cursor_identifier, enable_trigraphs);
  int cursor_after_complete_type_name_cast = 0;
  if (has_complete_identifier &&
      last_closed_parenthesized_is_type_name &&
      last_closed_parenthesized_end <= cursor_identifier.start) {
    size_t after_cast = skip_analysis_space_and_comments_mode(
        source, source_length, last_closed_parenthesized_end,
        enable_trigraphs);
    cursor_after_complete_type_name_cast =
        after_cast == cursor_identifier.start;
  }
  size_t direct_initializer_end = 0;
  int cursor_on_complete_direct_initializer_operand =
      has_complete_identifier &&
      analysis_complete_direct_object_initializer_operand(
          source, source_length, direct_initializer_candidate_start,
          direct_initializer_candidate_end, &cursor_identifier,
          enable_trigraphs,
          &direct_initializer_end);
  size_t direct_expression_end = 0;
  int cursor_has_complete_direct_expression_tail =
      has_complete_identifier &&
      analysis_complete_direct_expression_operand_tail(
          source, source_length, stack, stack_count,
          &cursor_identifier, enable_trigraphs, &direct_expression_end);
  size_t simple_remaining_call_end = 0;
  int cursor_has_complete_simple_remaining_call_tail =
      has_complete_identifier &&
      analysis_complete_simple_remaining_call_tail(
          source, source_length, stack, stack_count,
          &cursor_identifier, enable_trigraphs,
          &simple_remaining_call_end);
  int cursor_on_complete_tag_name =
      has_complete_identifier && previous_token_is_tag_keyword;
  size_t cursor_tag_tail_end = cursor_name_end;
  int cursor_tag_has_tail = 0;
  int cursor_tag_compound_literal = 0;
  int cursor_tag_cast_operand = 0;
  if (cursor_on_complete_tag_name && stack_count > 0) {
    char tail_terminator = 0;
    if (cursor_in_generic_association_type)
      tail_terminator = ':';
    else if (stack[stack_count - 1].open == '(')
      tail_terminator = ')';
    if (tail_terminator && analysis_delimited_tail_end_mode(
            source, source_length, cursor_name_end, tail_terminator, 0,
            enable_trigraphs, &cursor_tag_tail_end)) {
      cursor_tag_has_tail = 1;
    }
    if (cursor_tag_has_tail && tail_terminator == ')') {
      size_t after_type = skip_analysis_space_and_comments_mode(
          source, source_length, cursor_tag_tail_end + 1,
          enable_trigraphs);
      if (after_type < source_length && source[after_type] == '{') {
        cursor_tag_compound_literal = 1;
      } else if (after_type < source_length &&
                 (is_identifier_byte((unsigned char)source[after_type]) ||
                  source[after_type] == '\'' || source[after_type] == '"' ||
                  source[after_type] == '(')) {
        cursor_tag_cast_operand = 1;
      }
    }
  }
  size_t cursor_case_label_end = 0;
  int cursor_case_has_complete_label =
      !cursor_in_generic_association_type && case_expression_active &&
      has_complete_identifier &&
      cursor_identifier.start >= case_expression_start &&
      analysis_case_label_end_mode(
          source, source_length, case_expression_start,
          enable_trigraphs, &cursor_case_label_end);
  int cursor_needs_expression_placeholder =
      !cursor_in_generic_association_type &&
      (cursor_identifier_starts_conditional ||
       cursor_after_complete_type_name_cast ||
       (previous_token_requires_expression &&
        (!cursor_identifier_starts_parenthesized_suffix ||
         cursor_identifier_has_complete_parenthesized_suffix)) ||
       last_significant == '=' || last_significant == ',' ||
       last_significant == '(' || last_significant == '[' ||
       last_significant == '+' || last_significant == '-' ||
       last_significant == '*' || last_significant == '/' ||
       last_significant == '%' || last_significant == '&' ||
       last_significant == '|' || last_significant == '^' ||
       last_significant == '!' || last_significant == '~' ||
       last_significant == '<' || last_significant == '>' ||
       last_significant == '?' || last_significant == ':');
  size_t recovery_tail_consumed = recovery_cursor;
  if (complete_static_assert_frame != SIZE_MAX) {
    APPEND_BYTES(
        source + cursor_identifier.start,
        complete_static_assert_end + 1 - cursor_identifier.start);
    recovery_tail_consumed = complete_static_assert_end + 1;
    size_t outer_brace_count = 0;
    for (size_t i = 0; i < complete_static_assert_frame; i++)
      if (stack[i].open == '{')
        stack[outer_brace_count++] = stack[i];
    stack_count = outer_brace_count;
    last_significant = ';';
  } else if (complete_offsetof_frame != SIZE_MAX) {
    APPEND_BYTES(
        source + cursor_identifier.start,
        complete_offsetof_end + 1 - cursor_identifier.start);
    recovery_tail_consumed = complete_offsetof_end + 1;
    stack_count = complete_offsetof_frame;
    last_significant = ')';
  } else if (type_name_tail.kind != ANALYSIS_TYPE_NAME_TAIL_NONE) {
    APPEND_BYTES(
        source + cursor_identifier.start,
        type_name_tail.tail_end + 1 - cursor_identifier.start);
    recovery_tail_consumed = type_name_tail.tail_end + 1;
    if (type_name_tail.kind ==
        ANALYSIS_TYPE_NAME_TAIL_GENERIC_ASSOCIATION) {
      stack_count = type_name_tail.context_frame + 1;
      recovery_delimiter_t *generic =
          &stack[type_name_tail.context_frame];
      generic->generic_association_has_colon = 1;
      APPEND_LITERAL(" 0");
      if (!generic->generic_has_default_association) {
        APPEND_LITERAL(", default: 0");
        generic->generic_has_default_association = 1;
      }
    } else {
      if (type_name_tail.preserve_statement) {
        size_t outer_brace_count = 0;
        for (size_t i = 0;
             i < type_name_tail.context_frame; i++)
          if (stack[i].open == '{')
            stack[outer_brace_count++] = stack[i];
        stack_count = outer_brace_count;
        last_significant = ';';
      } else {
        stack_count = type_name_tail.context_frame;
        if (type_name_tail.append_compound_literal) {
          APPEND_LITERAL("{ 0 }");
          cursor_tag_compound_literal = 0;
        } else if (type_name_tail.append_cast_operand) {
          APPEND_LITERAL("0");
          cursor_tag_cast_operand = 0;
        }
      }
    }
  } else if (cursor_on_complete_member_access) {
    if (recovery_cursor < cursor_name_end)
      APPEND_BYTES(source + recovery_cursor,
                   cursor_name_end - recovery_cursor);
    APPEND_LITERAL(", 0");
  } else if (cursor_on_complete_direct_initializer_operand) {
    APPEND_BYTES(source + cursor_identifier.start,
                 cursor_identifier.end - cursor_identifier.start);
    recovery_tail_consumed = direct_initializer_end + 1;
    last_significant = 'x';
  } else if (cursor_on_complete_tag_name) {
    APPEND_BYTES(source + cursor_identifier.start,
                 cursor_identifier.end - cursor_identifier.start);
    if (cursor_tag_has_tail && cursor_tag_tail_end > cursor_name_end)
      APPEND_BYTES(source + cursor_name_end,
                   cursor_tag_tail_end - cursor_name_end);
  } else if (cursor_in_required_type_name) {
    APPEND_LITERAL(" int");
  } else if (cursor_case_has_complete_label) {
    for (size_t i = case_expression_start; i < recovery_cursor; i++)
      if (result[i] != '\n' && result[i] != '\r') result[i] = ' ';
    APPEND_BYTES(source + case_expression_start,
                 cursor_case_label_end + 1 - case_expression_start);
    APPEND_LITERAL(" 0");
    stack_count = case_expression_stack_count;
    if (stack_count > 0)
      stack[stack_count - 1].pending_conditional_count =
          case_expression_parent_pending_conditional_count;
    else
      root_pending_conditional_count =
          case_expression_parent_pending_conditional_count;
  } else if (!cursor_in_generic_association_type &&
             previous_token_is_case &&
             (!cursor_identifier_starts_parenthesized_suffix ||
              cursor_identifier_has_complete_parenthesized_suffix)) {
    if (has_complete_identifier) {
      APPEND_BYTES(source + cursor_identifier.start,
                   cursor_identifier.end - cursor_identifier.start);
      if (cursor_identifier_has_complete_parenthesized_suffix)
        APPEND_BYTES(
            source + cursor_identifier.end,
            cursor_parenthesized_suffix_end + 1 - cursor_identifier.end);
    } else {
      APPEND_LITERAL("0");
    }
    APPEND_LITERAL(": 0");
  } else if (cursor_has_complete_direct_expression_tail &&
             cursor_needs_expression_placeholder) {
    APPEND_BYTES(source + cursor_identifier.start,
                 cursor_identifier.end - cursor_identifier.start);
    recovery_tail_consumed = direct_expression_end + 1;
    last_significant = 'x';
  } else if (cursor_has_complete_simple_remaining_call_tail &&
             cursor_needs_expression_placeholder) {
    APPEND_BYTES(source + cursor_identifier.start,
                 simple_remaining_call_end + 1 -
                     cursor_identifier.start);
    recovery_tail_consumed = simple_remaining_call_end + 1;
    size_t outer_brace_count = 0;
    for (size_t i = 0; i < stack_count; i++)
      if (stack[i].open == '{')
        stack[outer_brace_count++] = stack[i];
    stack_count = outer_brace_count;
    last_significant = ';';
  } else if (cursor_needs_expression_placeholder) {
    APPEND_LITERAL(" 0");
  }
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
          if (!cursor_on_complete_tag_name) {
            if (has_complete_identifier) {
              APPEND_BYTES(source + cursor_identifier.start,
                           cursor_identifier.end -
                               cursor_identifier.start);
            } else {
              APPEND_LITERAL("int");
            }
          }
          APPEND_LITERAL(": 0");
          int cursor_association_is_default =
              has_complete_identifier &&
              analysis_identifier_span_matches(
                  source, source_length, &cursor_identifier,
                  enable_trigraphs, "default", 7);
          if (!stack[i - 1].generic_has_default_association &&
              !cursor_association_is_default)
            APPEND_LITERAL(", default: 0");
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
      if (i == stack_count && cursor_tag_compound_literal)
        APPEND_LITERAL("{ 0 }");
      else if (i == stack_count && cursor_tag_cast_operand)
        APPEND_LITERAL("0");
      if (stack[i - 1].is_for_control) {
        APPEND_LITERAL(" {\nint " AG_LANGUAGE_CURSOR_MARKER ";\n}\n");
        cursor_marker_appended = 1;
      }
    } else if (stack[i - 1].open == '[') APPEND_LITERAL("]");
  }
  APPEND_PENDING_CONDITIONALS(root_pending_conditional_count);
  if (!cursor_marker_appended) {
    if (simple_do_statement_active) {
      if (last_significant != 0 && last_significant != ';' &&
          last_significant != '}')
        APPEND_LITERAL(";");
      APPEND_LITERAL(" while (0);\n");
      last_significant = ';';
    }
    if (last_significant != 0 && last_significant != ';' &&
        last_significant != '}')
      APPEND_LITERAL(";");
    APPEND_LITERAL("\nint " AG_LANGUAGE_CURSOR_MARKER ";\n");
  }
  for (size_t i = stack_count; i > 0; i--)
    if (stack[i - 1].open == '{') {
      APPEND_LITERAL("}\n");
      if (stack[i - 1].is_do_body) APPEND_LITERAL("while (0);\n");
    }
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
      result, source, source_length, recovery_tail_consumed,
      enable_trigraphs);
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

static char analysis_trigraph_replacement(char suffix) {
  switch (suffix) {
    case '=': return '#';
    case '(': return '[';
    case '/': return '\\';
    case ')': return ']';
    case '\'': return '^';
    case '<': return '{';
    case '>': return '}';
    case '!': return '|';
    case '-': return '~';
    default: return 0;
  }
}

static size_t analysis_phase_one_width(
    const char *source, size_t length, size_t cursor,
    int enable_trigraphs) {
  if (enable_trigraphs && cursor + 2 < length &&
      source[cursor] == '?' && source[cursor + 1] == '?' &&
      analysis_trigraph_replacement(source[cursor + 2]))
    return 3;
  return cursor < length ? 1 : 0;
}

static int analysis_source_needs_offset_mapping(
    const char *source, size_t length, int enable_trigraphs) {
  if (!source) return 0;
  for (size_t cursor = 0; cursor < length;) {
    size_t splice_size = analysis_line_splice_size_mode(
        source, length, cursor, enable_trigraphs);
    if (splice_size) return 1;
    size_t width = analysis_phase_one_width(
        source, length, cursor, enable_trigraphs);
    if (width > 1) return 1;
    cursor += width;
  }
  return 0;
}

static int analysis_source_named(
    const ag_language_analysis_request_t *request,
    const char *source_name, analysis_source_view_t *source) {
  if (!request || !source_name || !source) return 0;
  for (int index = 0; index < source_count(request); index++) {
    analysis_source_view_t candidate = {0};
    if (source_at(request, index, &candidate) && candidate.name &&
        strcmp(candidate.name, source_name) == 0) {
      *source = candidate;
      return 1;
    }
  }
  return 0;
}

static int analysis_original_offset(
    const analysis_source_view_t *source, size_t normalized_offset,
    int after_removed_bytes, int enable_trigraphs,
    size_t *original_offset) {
  if (!source || !source->source || !original_offset) return 0;
  size_t original = 0;
  size_t normalized = 0;
  while (original < source->length) {
    size_t splice_size = analysis_line_splice_size_mode(
        source->source, source->length, original, enable_trigraphs);
    if (splice_size) {
      if (normalized == normalized_offset && !after_removed_bytes) {
        *original_offset = original;
        return 1;
      }
      original += splice_size;
      continue;
    }
    if (normalized == normalized_offset) {
      *original_offset = original;
      return 1;
    }
    original += analysis_phase_one_width(
        source->source, source->length, original, enable_trigraphs);
    normalized++;
  }
  if (normalized != normalized_offset) return 0;
  *original_offset = original;
  return 1;
}

static int analysis_original_range(
    const snapshot_builder_t *builder, const char *source_name,
    const char *normalized_source,
    size_t normalized_start, size_t normalized_end,
    analysis_source_view_t *source, size_t *original_start,
    size_t *original_end) {
  if (!builder || !builder->request || normalized_end < normalized_start ||
      !analysis_source_named(builder->request, source_name, source))
    return 0;
  int is_primary = source->source == builder->request->source;
  if (normalized_source == source->source ||
      (is_primary && !builder->primary_needs_offset_mapping)) {
    if (normalized_end > source->length) return 0;
    *original_start = normalized_start;
    *original_end = normalized_end;
    return 1;
  }
  if (!analysis_original_offset(
          source, normalized_start, 1, builder->enable_trigraphs,
          original_start))
    return 0;
  if (normalized_start == normalized_end) {
    *original_end = *original_start;
    return 1;
  }
  return analysis_original_offset(
      source, normalized_end, 0, builder->enable_trigraphs,
      original_end);
}

static void analysis_positions_from_normalized_range(
    const snapshot_builder_t *builder, const char *source_name,
    const char *normalized_source, size_t normalized_length,
    size_t normalized_start, size_t normalized_end,
    ag_language_position_t *start, ag_language_position_t *end) {
  analysis_source_view_t original = {0};
  size_t original_start = 0;
  size_t original_end = 0;
  if (analysis_original_range(
          builder, source_name, normalized_source,
          normalized_start, normalized_end,
          &original, &original_start, &original_end)) {
    *start = position_at(original.source, original.length, original_start);
    *end = position_at(original.source, original.length, original_end);
    return;
  }
  *start = position_at(
      normalized_source, normalized_length, normalized_start);
  *end = position_at(
      normalized_source, normalized_length, normalized_end);
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
      if (declaration->kind == PSX_DECL_TAG) {
        size_t tag_name_start = skip_analysis_space_and_comments(
            declaration->source_input, source_length, end);
        if (tag_name_start <= source_length &&
            name_len <= source_length - tag_name_start &&
            memcmp(declaration->source_input + tag_name_start,
                   name, name_len) == 0 &&
            (tag_name_start + name_len == source_length ||
             !is_identifier_byte((unsigned char)
                 declaration->source_input[tag_name_start + name_len]))) {
          start = tag_name_start;
          end = tag_name_start + name_len;
        }
      }
      range->source_name = snapshot_copy(builder, declaration->source_name);
      analysis_positions_from_normalized_range(
          builder, declaration->source_name,
          declaration->source_input, source_length, start, end,
          &range->start, &range->end);
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
  analysis_positions_from_normalized_range(
      builder, location->source_name, location->source_input,
      source_length, start, end, &range->start, &range->end);
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
    case PSX_DECL_MEMBER: {
      psx_qual_type_t type = {PSX_TYPE_ID_INVALID, 0};
      return ps_ctx_record_member_qual_type_by_declaration_id_in(
                 semantic_context, declaration->id, &type)
                 ? type
                 : (psx_qual_type_t){PSX_TYPE_ID_INVALID, 0};
    }
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
    case AG_LANGUAGE_SYMBOL_LABEL: return "label";
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
    case PSX_DECL_LABEL: return AG_LANGUAGE_SYMBOL_LABEL;
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

static const ag_language_documentation_entry_t *
documentation_for_exact_declaration_start(
    const snapshot_builder_t *builder,
    const ag_language_source_range_t *range) {
  if (!builder || !builder->documentation_index || !range ||
      !range->source_name || range->start.offset < 0)
    return NULL;
  size_t declaration_start = (size_t)range->start.offset;
  for (size_t i = 0; i < builder->documentation_index->count; i++) {
    const ag_language_documentation_entry_t *entry =
        &builder->documentation_index->entries[i];
    if (strcmp(entry->source_name, range->source_name) == 0 &&
        entry->declaration_start == declaration_start)
      return entry;
  }
  return NULL;
}

static const ag_language_documentation_entry_t *
documentation_for_enum_tag(
    const snapshot_builder_t *builder,
    const ag_language_source_range_t *range) {
  if (!builder || !builder->documentation_index || !range ||
      !range->source_name || range->start.offset < 0)
    return NULL;
  size_t tag_start = (size_t)range->start.offset;
  static const char enum_keyword[] = "enum";
  size_t enum_length = sizeof(enum_keyword) - 1;
  for (size_t i = 0; i < builder->documentation_index->count; i++) {
    const ag_language_documentation_entry_t *entry =
        &builder->documentation_index->entries[i];
    if (strcmp(entry->source_name, range->source_name) != 0 ||
        entry->declaration_start > entry->source_length ||
        enum_length > entry->source_length - entry->declaration_start)
      continue;
    size_t cursor = entry->declaration_start;
    if (memcmp(entry->source + cursor, enum_keyword, enum_length) != 0 ||
        (cursor + enum_length < entry->source_length &&
         is_identifier_byte(
             (unsigned char)entry->source[cursor + enum_length])))
      continue;
    cursor = skip_analysis_space_and_comments(
        entry->source, entry->source_length, cursor + enum_length);
    if (cursor == tag_start) return entry;
  }
  return NULL;
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
  if (!builder || !symbol) return;
  const ag_language_documentation_entry_t *declaration_entry = NULL;
  if (symbol->kind == AG_LANGUAGE_SYMBOL_OBJECT ||
      symbol->kind == AG_LANGUAGE_SYMBOL_FUNCTION) {
    declaration_entry = documentation_for_range(
        builder, &symbol->declaration);
  } else if (symbol->kind == AG_LANGUAGE_SYMBOL_ENUM_CONSTANT) {
    declaration_entry = documentation_for_exact_declaration_start(
        builder, &symbol->declaration);
  } else if (symbol->kind == AG_LANGUAGE_SYMBOL_TAG && symbol->type &&
             strncmp(symbol->type, "enum ", 5) == 0) {
    declaration_entry = documentation_for_enum_tag(
        builder, &symbol->declaration);
  } else {
    return;
  }
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
  if (symbol->kind == AG_LANGUAGE_SYMBOL_MEMBER) {
    free(symbol->storage_class);
    symbol->storage_class = snapshot_copy(builder, "member");
  }
  if (symbol->kind == AG_LANGUAGE_SYMBOL_LABEL) {
    size_t name_length = strlen(symbol->name);
    if (name_length > (size_t)builder->limits.max_string_bytes - 1) {
      builder_limit(builder, "maxAnalysisStringBytes",
                    "AGC_LIMIT_MAX_ANALYSIS_STRING_BYTES",
                    (size_t)builder->limits.max_string_bytes,
                    name_length + 1);
    } else {
      free(symbol->signature);
      symbol->signature = snapshot_alloc(builder, name_length + 2);
      if (symbol->signature) {
        memcpy(symbol->signature, symbol->name, name_length);
        symbol->signature[name_length] = ':';
        symbol->signature[name_length + 1] = '\0';
      }
    }
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
        analysis_positions_from_normalized_range(
            builder, view.source_name, view.source_input,
            source_length, start, end,
            &symbol->declaration.start, &symbol->declaration.end);
      }
    }
    if (!symbol->declaration.source_name)
      locate_declaration(builder, request, NULL, view.name,
                         (size_t)view.name_len, &symbol->declaration);
    set_symbol_documentation(
        builder, symbol,
        documentation_for_range(builder, &symbol->declaration));
    if (builder->failed) return 0;
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

static int identifier_span_invokes_function_like_macro(
    const ag_language_analysis_request_t *request,
    const ag_preprocessor_context_t *preprocessor,
    int enable_trigraphs,
    const analysis_identifier_span_t *identifier) {
  if (!request || !preprocessor || !identifier) return 0;
  size_t after_name = skip_analysis_space_and_comments_mode(
      request->source, request->source_length, identifier->end,
      enable_trigraphs);
  if (after_name >= request->source_length ||
      request->source[after_name] != '(')
    return 0;
  if (!analysis_delimited_tail_end_mode(
          request->source, request->source_length, after_name + 1,
          ')', 1, enable_trigraphs, NULL))
    return 0;
  int macro_count = pp_macro_count_in(preprocessor);
  for (int macro_index = 0; macro_index < macro_count; macro_index++) {
    ag_pp_macro_view_t view = {0};
    if (pp_macro_view_at_in(preprocessor, macro_index, &view) &&
        view.is_function_like && view.name && view.name_len > 0 &&
        analysis_identifier_span_matches(
            request->source, request->source_length, identifier,
            enable_trigraphs, view.name, (size_t)view.name_len))
      return 1;
  }
  return 0;
}

static int cursor_invokes_function_like_macro(
    const ag_language_analysis_request_t *request,
    const ag_preprocessor_context_t *preprocessor,
    int enable_trigraphs) {
  if (!request || !preprocessor) return 0;
  analysis_identifier_span_t identifier = {0};
  if (!analysis_identifier_span_at_mode(
          request->source, request->source_length,
          request->cursor_byte_offset, enable_trigraphs,
          &identifier))
    return 0;
  return identifier_span_invokes_function_like_macro(
      request, preprocessor, enable_trigraphs, &identifier);
}

static int identifier_span_resolves_as_enum_constant(
    const ag_language_analysis_request_t *request,
    int enable_trigraphs, const psx_scope_graph_t *scope_graph,
    const analysis_identifier_span_t *identifier) {
  if (!request || !scope_graph || !identifier) return 0;
  const psx_scope_declaration_t *marker = NULL;
  size_t declaration_count =
      psx_scope_graph_declaration_count(scope_graph);
  for (size_t i = declaration_count; i > 0; i--) {
    const psx_scope_declaration_t *candidate =
        psx_scope_graph_declaration_at(scope_graph, i - 1);
    if (candidate && candidate->name &&
        strcmp(candidate->name, AG_LANGUAGE_CURSOR_MARKER) == 0) {
      marker = candidate;
      break;
    }
  }
  if (!marker) return 0;
  psx_scope_lookup_point_t point = {
      marker->scope_id, marker->declaration_order};
  for (size_t i = 0; i < declaration_count; i++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i);
    if (!declaration || declaration->kind != PSX_DECL_ENUM_CONSTANT ||
        declaration->name_space != PSX_NAMESPACE_ORDINARY ||
        !declaration->name || declaration->name_len <= 0 ||
        !analysis_identifier_span_matches(
            request->source, request->source_length, identifier,
            enable_trigraphs, declaration->name,
            (size_t)declaration->name_len))
      continue;
    if (psx_scope_graph_lookup(
            scope_graph, PSX_NAMESPACE_ORDINARY,
            declaration->name, declaration->name_len,
            point) == declaration->id)
      return 1;
  }
  return 0;
}

static int identifier_span_resolves_in_ordinary_namespace(
    const ag_language_analysis_request_t *request,
    int enable_trigraphs, const psx_scope_graph_t *scope_graph,
    const analysis_identifier_span_t *identifier) {
  if (!request || !scope_graph || !identifier) return 0;
  const psx_scope_declaration_t *marker = NULL;
  size_t declaration_count =
      psx_scope_graph_declaration_count(scope_graph);
  for (size_t i = declaration_count; i > 0; i--) {
    const psx_scope_declaration_t *candidate =
        psx_scope_graph_declaration_at(scope_graph, i - 1);
    if (candidate && candidate->name &&
        strcmp(candidate->name, AG_LANGUAGE_CURSOR_MARKER) == 0) {
      marker = candidate;
      break;
    }
  }
  if (!marker) return 0;
  psx_scope_lookup_point_t point = {
      marker->scope_id, marker->declaration_order};
  for (size_t i = 0; i < declaration_count; i++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i);
    if (!declaration ||
        declaration->name_space != PSX_NAMESPACE_ORDINARY ||
        !declaration->name || declaration->name_len <= 0 ||
        !analysis_identifier_span_matches(
            request->source, request->source_length, identifier,
            enable_trigraphs, declaration->name,
            (size_t)declaration->name_len))
      continue;
    if (psx_scope_graph_lookup(
            scope_graph, PSX_NAMESPACE_ORDINARY,
            declaration->name, declaration->name_len,
            point) == declaration->id)
      return 1;
  }
  return 0;
}

static int identifier_span_resolves_as_object_like_macro(
    const ag_language_analysis_request_t *request,
    const ag_preprocessor_context_t *preprocessor,
    int enable_trigraphs,
    const analysis_identifier_span_t *identifier) {
  if (!request || !preprocessor || !identifier) return 0;
  int macro_count = pp_macro_count_in(preprocessor);
  for (int macro_index = 0; macro_index < macro_count; macro_index++) {
    ag_pp_macro_view_t view = {0};
    if (pp_macro_view_at_in(preprocessor, macro_index, &view) &&
        !view.is_function_like && view.name && view.name_len > 0 &&
        analysis_identifier_span_matches(
            request->source, request->source_length, identifier,
            enable_trigraphs, view.name, (size_t)view.name_len))
      return 1;
  }
  return 0;
}

static int enum_direct_call_unresolved_identifier_argument(
    const ag_language_analysis_request_t *request,
    const psx_scope_graph_t *scope_graph,
    const ag_preprocessor_context_t *preprocessor,
    int enable_trigraphs,
    analysis_identifier_span_t *callee,
    analysis_identifier_span_t *unresolved) {
  if (!request || !scope_graph || !preprocessor || !callee ||
      !unresolved ||
      !enum_direct_call_identifier_at_cursor(
          request->source, request->source_length,
          request->cursor_byte_offset, enable_trigraphs, callee))
    return 0;
  size_t call_open = skip_analysis_space_and_comments_mode(
      request->source, request->source_length, callee->end,
      enable_trigraphs);
  size_t call_end = 0;
  if (call_open >= request->source_length ||
      request->source[call_open] != '(' ||
      !analysis_delimited_tail_end_mode(
          request->source, request->source_length, call_open + 1,
          ')', 1, enable_trigraphs, &call_end))
    return 0;
  size_t argument_start = call_open + 1;
  int has_unresolved = 0;
  while (argument_start < call_end) {
    argument_start = skip_analysis_space_and_comments_mode(
        request->source, call_end, argument_start, enable_trigraphs);
    if (argument_start >= call_end ||
        !is_identifier_start_byte(
            (unsigned char)request->source[argument_start]))
      return 0;
    analysis_identifier_span_t argument = {0};
    if (!analysis_identifier_span_at_mode(
            request->source, call_end, argument_start,
            enable_trigraphs, &argument) ||
        argument.start != argument_start)
      return 0;
    size_t after_argument = skip_analysis_space_and_comments_mode(
        request->source, call_end, argument.end, enable_trigraphs);
    if (after_argument < call_end &&
        request->source[after_argument] != ',')
      return 0;
    if (!identifier_span_resolves_as_object_like_macro(
            request, preprocessor, enable_trigraphs, &argument) &&
        !identifier_span_resolves_in_ordinary_namespace(
            request, enable_trigraphs, scope_graph, &argument) &&
        !has_unresolved) {
      *unresolved = argument;
      has_unresolved = 1;
    }
    if (after_argument >= call_end) return has_unresolved;
    argument_start = after_argument + 1;
  }
  return 0;
}

static int cursor_identifier_resolves_as_enum_constant(
    const ag_language_analysis_request_t *request,
    int enable_trigraphs, const psx_scope_graph_t *scope_graph) {
  if (!request || !scope_graph) return 0;
  analysis_identifier_span_t identifier = {0};
  if (!analysis_identifier_span_at_mode(
          request->source, request->source_length,
          request->cursor_byte_offset, enable_trigraphs, &identifier))
    return 0;
  return identifier_span_resolves_as_enum_constant(
      request, enable_trigraphs, scope_graph, &identifier);
}

static int ambiguous_eof_identifier_resolves_as_enum_operand(
    const ag_language_analysis_request_t *request,
    int enable_trigraphs, const psx_scope_graph_t *scope_graph,
    const ag_preprocessor_context_t *preprocessor) {
  if (!request || !scope_graph || !preprocessor) return 0;
  analysis_identifier_span_t identifier = {0};
  if (!analysis_identifier_span_at_mode(
          request->source, request->source_length,
          request->cursor_byte_offset, enable_trigraphs, &identifier) ||
      identifier.end != request->source_length)
    return 0;
  if (cursor_identifier_resolves_as_enum_constant(
          request, enable_trigraphs, scope_graph))
    return 1;
  int macro_count = pp_macro_count_in(preprocessor);
  for (int macro_index = 0; macro_index < macro_count; macro_index++) {
    ag_pp_macro_view_t view = {0};
    if (pp_macro_view_at_in(preprocessor, macro_index, &view) &&
        !view.is_function_like && view.name && view.name_len > 0 &&
        analysis_identifier_span_matches(
            request->source, request->source_length, &identifier,
            enable_trigraphs, view.name, (size_t)view.name_len))
      return 1;
  }
  return 0;
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
    psx_decl_id_t member_declaration_id =
        ps_ctx_record_member_declaration_id_in(
            semantic_context, shape.record_id,
            member->name, member->len);
    const psx_scope_declaration_t *member_declaration =
        psx_scope_graph_declaration(graph, member_declaration_id);
    locate_declaration(builder, request, member_declaration,
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
                         const char *source, size_t source_length,
                         const analysis_identifier_span_t *identifier,
                         int enable_trigraphs,
                         int function_like_macro_invoked) {
  snapshot->hover_index = -1;
  if (!source || !identifier || identifier->logical_length == 0) return;
  for (int i = 0; i < snapshot->completion_item_count; i++) {
    ag_language_symbol_t *symbol = &snapshot->completion_items[i];
    if (!analysis_identifier_span_matches(
            source, source_length, identifier, enable_trigraphs,
            symbol->name, strlen(symbol->name)))
      continue;
    if (snapshot->hover_index < 0)
      snapshot->hover_index = i;
    if (symbol->kind == AG_LANGUAGE_SYMBOL_MACRO &&
        (!symbol->macro_is_function_like || function_like_macro_invoked)) {
      snapshot->hover_index = i;
      return;
    }
  }
}

static int select_hover_kind(ag_language_analysis_snapshot_t *snapshot,
                             const char *source, size_t source_length,
                             const analysis_identifier_span_t *identifier,
                             int enable_trigraphs,
                             ag_language_symbol_kind_t kind) {
  if (!snapshot || !source || !identifier ||
      identifier->logical_length == 0)
    return 0;
  for (int i = 0; i < snapshot->completion_item_count; i++) {
    const ag_language_symbol_t *symbol = &snapshot->completion_items[i];
    if (symbol->kind != kind ||
        !analysis_identifier_span_matches(
            source, source_length, identifier, enable_trigraphs,
            symbol->name, strlen(symbol->name)))
      continue;
    snapshot->hover_index = i;
    return 1;
  }
  return 0;
}

static const psx_scope_declaration_t *
ordinary_scoped_declaration_at_cursor(
    const psx_scope_graph_t *scope_graph,
    const ag_language_analysis_request_t *request) {
  if (!scope_graph || !request || !request->source ||
      !request->source_name)
    return NULL;
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(
      request->source, request->source_length,
      request->cursor_byte_offset, &name, &name_length);
  if (!name || name_length == 0) return NULL;
  size_t name_start = (size_t)(name - request->source);
  if (name_start > INT_MAX || name_length > INT_MAX) return NULL;
  size_t declaration_count =
      psx_scope_graph_declaration_count(scope_graph);
  for (size_t i = declaration_count; i > 0; i--) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i - 1);
    int is_scoped_ordinary = declaration &&
        declaration->name_space == PSX_NAMESPACE_ORDINARY &&
        (declaration->kind == PSX_DECL_ENUM_CONSTANT ||
         declaration->kind == PSX_DECL_PARAMETER ||
         (declaration->kind == PSX_DECL_LOCAL_OBJECT &&
          declaration->payload &&
          ps_lvar_is_param((const lvar_t *)declaration->payload)));
    if (!is_scoped_ordinary || !declaration->name ||
        declaration->name_len != (int)name_length ||
        memcmp(declaration->name, name, name_length) != 0 ||
        !declaration->source_name ||
        strcmp(declaration->source_name, request->source_name) != 0 ||
        declaration->source_byte_offset != (int)name_start ||
        declaration->source_byte_length != (int)name_length)
      continue;
    return declaration;
  }
  return NULL;
}

static int retained_block_extern_lookup_point(
    const psx_scope_graph_t *scope_graph,
    const ag_language_analysis_request_t *request,
    psx_scope_lookup_point_t *point) {
  if (!scope_graph || !request || !request->source ||
      !request->source_name || !point)
    return 0;
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(
      request->source, request->source_length,
      request->cursor_byte_offset, &name, &name_length);
  if (!name || name_length == 0 || name_length > INT_MAX)
    return 0;
  size_t declaration_count =
      psx_scope_graph_declaration_count(scope_graph);
  /* The retained declaration validates its array suffix, but the selected
   * type name is looked up immediately before its linkage alias exists. */
  for (size_t i = declaration_count; i > 0; i--) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i - 1);
    if (!declaration || declaration->kind != PSX_DECL_LINKAGE_ALIAS ||
        declaration->scope_id == PSX_SCOPE_ID_TRANSLATION_UNIT ||
        declaration->name_space != PSX_NAMESPACE_ORDINARY ||
        !declaration->name ||
        declaration->name_len != (int)name_length ||
        memcmp(declaration->name, name, name_length) != 0 ||
        !declaration->source_name ||
        strcmp(declaration->source_name, request->source_name) != 0 ||
        declaration->declaration_order == 0)
      continue;
    *point = (psx_scope_lookup_point_t){
        declaration->scope_id, declaration->declaration_order - 1};
    return 1;
  }
  return 0;
}

static const psx_scope_declaration_t *tag_declaration_at_cursor(
    const psx_scope_graph_t *scope_graph,
    const ag_language_analysis_request_t *request) {
  if (!scope_graph || !request || !request->source ||
      !request->source_name)
    return NULL;
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(
      request->source, request->source_length,
      request->cursor_byte_offset, &name, &name_length);
  if (!name || name_length == 0 || name_length > INT_MAX) return NULL;
  size_t name_start = (size_t)(name - request->source);
  if (name_start > INT_MAX) return NULL;
  size_t declaration_count =
      psx_scope_graph_declaration_count(scope_graph);
  for (size_t i = declaration_count; i > 0; i--) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i - 1);
    if (!declaration || declaration->kind != PSX_DECL_TAG ||
        declaration->name_space != PSX_NAMESPACE_TAG ||
        !declaration->name ||
        declaration->name_len != (int)name_length ||
        memcmp(declaration->name, name, name_length) != 0 ||
        !declaration->source_name || !declaration->source_input ||
        strcmp(declaration->source_name, request->source_name) != 0 ||
        declaration->source_byte_offset < 0 ||
        declaration->source_byte_length < 0)
      continue;
    size_t source_length = strlen(declaration->source_input);
    size_t declaration_start =
        (size_t)declaration->source_byte_offset;
    size_t declaration_length =
        (size_t)declaration->source_byte_length;
    if (declaration_start > source_length ||
        declaration_length > source_length - declaration_start)
      continue;
    size_t declaration_end = declaration_start + declaration_length;
    size_t declaration_name_start = skip_analysis_space_and_comments(
        declaration->source_input, source_length, declaration_end);
    if (declaration_name_start != name_start ||
        name_length > source_length - declaration_name_start ||
        memcmp(declaration->source_input + declaration_name_start,
               name, name_length) != 0)
      continue;
    return declaration;
  }
  return NULL;
}

static const psx_scope_declaration_t *member_declaration_at_cursor(
    const psx_scope_graph_t *scope_graph,
    const ag_language_analysis_request_t *request) {
  if (!scope_graph || !request || !request->source ||
      !request->source_name)
    return NULL;
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(
      request->source, request->source_length,
      request->cursor_byte_offset, &name, &name_length);
  if (!name || name_length == 0 || name_length > INT_MAX) return NULL;
  size_t name_start = (size_t)(name - request->source);
  if (name_start > INT_MAX) return NULL;
  size_t declaration_count =
      psx_scope_graph_declaration_count(scope_graph);
  for (size_t i = declaration_count; i > 0; i--) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(scope_graph, i - 1);
    if (!declaration || declaration->kind != PSX_DECL_MEMBER ||
        declaration->name_space != PSX_NAMESPACE_MEMBER ||
        !declaration->name ||
        declaration->name_len != (int)name_length ||
        memcmp(declaration->name, name, name_length) != 0 ||
        !declaration->source_name ||
        strcmp(declaration->source_name, request->source_name) != 0 ||
        declaration->source_byte_offset != (int)name_start ||
        declaration->source_byte_length != (int)name_length)
      continue;
    return declaration;
  }
  return NULL;
}

static const psx_scope_declaration_t *label_declaration_at_cursor(
    const psx_scope_graph_t *scope_graph,
    const ag_language_analysis_request_t *request,
    psx_scope_lookup_point_t point) {
  if (!scope_graph || !request || !request->source) return NULL;
  const char *name = NULL;
  size_t name_length = 0;
  identifier_at(
      request->source, request->source_length,
      request->cursor_byte_offset, &name, &name_length);
  if (!name || name_length == 0 || name_length > INT_MAX) return NULL;
  psx_decl_id_t declaration_id = psx_scope_graph_lookup(
      scope_graph, PSX_NAMESPACE_LABEL, name, (int)name_length, point);
  const psx_scope_declaration_t *declaration =
      psx_scope_graph_declaration(scope_graph, declaration_id);
  return declaration && declaration->kind == PSX_DECL_LABEL &&
                 declaration->name_space == PSX_NAMESPACE_LABEL
             ? declaration
             : NULL;
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
    const char *source_name =
        diag_context_record_source_name(diagnostics, i);
    diagnostic->range.source_name = snapshot_copy(builder, source_name);
    int end = diag_context_record_end_offset(diagnostics, i);
    analysis_source_view_t original = {0};
    size_t original_start = 0;
    size_t original_end = 0;
    if (start >= 0 && end >= start &&
        analysis_original_range(
            builder, source_name, NULL, (size_t)start, (size_t)end,
            &original, &original_start, &original_end)) {
      diagnostic->range.start = position_at(
          original.source, original.length, original_start);
      diagnostic->range.end = position_at(
          original.source, original.length, original_end);
    } else {
      diagnostic->range.start = (ag_language_position_t){
          diag_context_record_start_line(diagnostics, i),
          diag_context_record_start_column(diagnostics, i), start};
      diagnostic->range.end = (ag_language_position_t){
          diag_context_record_end_line(diagnostics, i),
          diag_context_record_end_column(diagnostics, i), end};
    }
  }
  builder->snapshot->diagnostic_count = output;
  return !builder->failed;
}

static int append_partial_identifier_diagnostic(
    snapshot_builder_t *builder,
    const ag_language_analysis_request_t *request) {
  analysis_identifier_span_t identifier = {0};
  if (!analysis_identifier_span_at_mode(
          request->source, request->source_length,
          request->cursor_byte_offset, builder->enable_trigraphs,
          &identifier))
    return 1;
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
  diagnostic->range.start = position_at(
      request->source, request->source_length, identifier.start);
  diagnostic->range.end = position_at(
      request->source, request->source_length, identifier.end);
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

static int save_invalid_macro_definition_diagnostic(
    const ag_language_analysis_request_t *request,
    int enable_trigraphs, saved_analysis_diagnostic_t *saved) {
  if (!request || !saved) return 0;
  analysis_identifier_span_t identifier = {0};
  if (!analysis_identifier_span_at_mode(
          request->source, request->source_length,
          request->cursor_byte_offset, enable_trigraphs, &identifier))
    return 0;
  *saved = (saved_analysis_diagnostic_t){
      .severity = 1,
      .code = analysis_strdup("AGC_PARTIAL_MACRO_DEFINITION"),
      .message = analysis_strdup(
          "preprocessing stopped at an invalid macro definition"),
      .source_name = analysis_strdup(request->source_name),
      .start = position_at(
          request->source, request->source_length, identifier.start),
      .end = position_at(
          request->source, request->source_length, identifier.end),
  };
  if (saved->code && saved->message && saved->source_name) return 1;
  dispose_saved_diagnostic(saved);
  return 0;
}

static int save_elided_call_not_callable_diagnostic(
    const ag_language_analysis_request_t *request,
    ag_diagnostic_context_t *diagnostics, int enable_trigraphs,
    saved_analysis_diagnostic_t *saved) {
  if (!request || !diagnostics || !saved) return 0;
  analysis_identifier_span_t identifier = {0};
  if (!enum_direct_call_identifier_at_cursor(
          request->source, request->source_length,
          request->cursor_byte_offset, enable_trigraphs, &identifier) &&
      !analysis_identifier_span_at_mode(
          request->source, request->source_length,
          request->cursor_byte_offset, enable_trigraphs, &identifier))
    return 0;
  size_t call_open = skip_analysis_space_and_comments_mode(
      request->source, request->source_length, identifier.end,
      enable_trigraphs);
  if (call_open >= request->source_length ||
      request->source[call_open] != '(' ||
      call_open > request->cursor_byte_offset)
    return 0;
  *saved = (saved_analysis_diagnostic_t){
      .severity = 1,
      .code = analysis_strdup(
          diag_error_code(DIAG_ERR_PARSER_CALL_NOT_CALLABLE)),
      .message = analysis_strdup(diag_message_for_in(
          diagnostics, DIAG_ERR_PARSER_CALL_NOT_CALLABLE)),
      .source_name = analysis_strdup(request->source_name),
      .start = position_at(
          request->source, request->source_length, call_open),
      .end = position_at(
          request->source, request->source_length, call_open + 1),
  };
  if (saved->code && saved->message && saved->source_name) return 1;
  dispose_saved_diagnostic(saved);
  return 0;
}

static int save_elided_call_undefined_identifier_diagnostic(
    const ag_language_analysis_request_t *request,
    ag_diagnostic_context_t *diagnostics,
    const analysis_identifier_span_t *callee,
    const analysis_identifier_span_t *unresolved,
    saved_analysis_diagnostic_t *saved) {
  if (!request || !diagnostics || !callee || !unresolved || !saved ||
      callee->end < callee->start ||
      unresolved->end < unresolved->start ||
      unresolved->end > request->source_length ||
      unresolved->end - unresolved->start > (size_t)INT_MAX)
    return 0;
  const char *format = diag_message_for_in(
      diagnostics, DIAG_ERR_PARSER_UNDEFINED_WITH_KIND);
  int name_length = (int)(unresolved->end - unresolved->start);
  int message_length = snprintf(
      NULL, 0, format, "variable", name_length,
      request->source + unresolved->start);
  if (message_length < 0) return 0;
  char *message = malloc((size_t)message_length + 1);
  if (!message) return 0;
  snprintf(message, (size_t)message_length + 1, format,
           "variable", name_length,
           request->source + unresolved->start);
  *saved = (saved_analysis_diagnostic_t){
      .severity = 1,
      .code = analysis_strdup(
          diag_error_code(DIAG_ERR_PARSER_UNDEFINED_WITH_KIND)),
      .message = message,
      .source_name = analysis_strdup(request->source_name),
      .start = position_at(
          request->source, request->source_length, callee->start),
      .end = position_at(
          request->source, request->source_length, callee->end),
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
  analysis_source_view_t original = {0};
  size_t original_start = 0;
  size_t original_end = 0;
  if (saved->start.offset >= 0 &&
      saved->end.offset >= saved->start.offset &&
      analysis_original_range(
          builder, saved->source_name, NULL,
          (size_t)saved->start.offset, (size_t)saved->end.offset,
          &original, &original_start, &original_end)) {
    diagnostic->range.start = position_at(
        original.source, original.length, original_start);
    diagnostic->range.end = position_at(
        original.source, original.length, original_end);
  } else {
    diagnostic->range.start = saved->start;
    diagnostic->range.end = saved->end;
  }
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

static int elide_cursor_statement(char *source, size_t cursor) {
  if (!source || source[cursor] != ';') return 0;
  size_t start = cursor;
  while (start > 0 && source[start - 1] != ';' &&
         source[start - 1] != '{' && source[start - 1] != '}')
    start--;
  if (start >= cursor) return 0;
  for (size_t i = start; i < cursor; i++)
    if (source[i] != '\n' && source[i] != '\r') source[i] = ' ';
  return 1;
}

typedef struct {
  ag_compilation_session_t *session;
  tokenizer_context_t *tokenizer;
  const ag_language_analysis_request_t *request;
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
  int has_cursor_lookup_point;
  psx_scope_lookup_point_t cursor_lookup_point;
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
    ag_language_documentation_index_t *documentation_index,
    const ag_language_analysis_request_t *request) {
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
  state->request = request;
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

static int push_analysis_statement(
    const node_t ***stack, size_t *count, size_t *capacity,
    const node_t *statement) {
  if (!statement) return 1;
  if (*count == *capacity) {
    size_t next_capacity = *capacity ? *capacity * 2 : 32;
    if (next_capacity < *capacity ||
        next_capacity > SIZE_MAX / sizeof(**stack))
      return 0;
    const node_t **next_stack = realloc(
        (void *)*stack, next_capacity * sizeof(**stack));
    if (!next_stack) return 0;
    *stack = next_stack;
    *capacity = next_capacity;
  }
  (*stack)[(*count)++] = statement;
  return 1;
}

static int jump_name_contains_analysis_cursor(
    const analysis_parse_state_t *state,
    const node_jump_t *jump) {
  if (!state || !state->request || !state->request->source_name ||
      !jump || !jump->name_tok || !jump->name_tok->source_input ||
      jump->name_tok->byte_offset < 0 ||
      jump->name_tok->byte_length < 0)
    return 0;
  ag_source_manager_t *sources = diag_context_source_manager(
      ag_compilation_session_diagnostic_context(state->session));
  const char *source_name = ag_source_manager_name(
      sources, jump->name_tok->file_name_id);
  if (!source_name ||
      strcmp(source_name, state->request->source_name) != 0)
    return 0;
  size_t start = (size_t)jump->name_tok->byte_offset;
  size_t length = (size_t)jump->name_tok->byte_length;
  size_t cursor = state->request->cursor_byte_offset;
  return start <= cursor && cursor - start <= length;
}

static void capture_jump_cursor_lookup_point(
    analysis_parse_state_t *state, const node_t *root) {
  if (!state || !root || state->has_cursor_lookup_point) return;
  const node_t **stack = NULL;
  size_t count = 0;
  size_t capacity = 0;
  if (!push_analysis_statement(&stack, &count, &capacity, root)) return;
  while (count > 0 && !state->has_cursor_lookup_point) {
    const node_t *statement = stack[--count];
    if (!statement) continue;
    switch (statement->kind) {
      case ND_GOTO:
      case ND_LABEL: {
        const node_jump_t *jump = (const node_jump_t *)statement;
        if (jump_name_contains_analysis_cursor(state, jump)) {
          state->cursor_lookup_point = (psx_scope_lookup_point_t){
              jump->scope_seq, jump->declaration_seq};
          state->has_cursor_lookup_point = 1;
          break;
        }
        if (statement->kind == ND_LABEL)
          (void)push_analysis_statement(
              &stack, &count, &capacity, statement->rhs);
        break;
      }
      case ND_BLOCK: {
        const node_block_t *block = (const node_block_t *)statement;
        if (!block->body) break;
        for (size_t i = 0; block->body[i]; i++)
          if (!push_analysis_statement(
                  &stack, &count, &capacity, block->body[i])) {
            count = 0;
            break;
          }
        break;
      }
      case ND_IF: {
        const node_ctrl_t *control = (const node_ctrl_t *)statement;
        (void)push_analysis_statement(
            &stack, &count, &capacity, control->els);
        (void)push_analysis_statement(
            &stack, &count, &capacity, statement->rhs);
        break;
      }
      case ND_WHILE:
      case ND_DO_WHILE:
      case ND_FOR:
      case ND_SWITCH:
      case ND_CASE:
      case ND_DEFAULT:
        (void)push_analysis_statement(
            &stack, &count, &capacity, statement->rhs);
        break;
      default:
        break;
    }
  }
  free((void *)stack);
}

static int collect_analysis_function_declarations(
    void *context, ag_compilation_session_t *session,
    const psx_parsed_function_definition_t *syntax_function,
    const token_t *fallback_diag_tok,
    psx_hir_node_id_t *hir_root) {
  analysis_parse_state_t *state = context;
  if (hir_root) *hir_root = PSX_HIR_NODE_ID_INVALID;
  capture_jump_cursor_lookup_point(
      state, syntax_function ? syntax_function->body : NULL);
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
      .request = request,
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
  tokenizer_context_t *tokenizer = ag_compilation_session_tokenizer(session);
  builder.enable_trigraphs = tk_ctx_get_enable_trigraphs(tokenizer);
  builder.primary_needs_offset_mapping =
      analysis_source_needs_offset_mapping(
          request->source, request->source_length,
          builder.enable_trigraphs);
  int cursor_on_complete_macro_definition =
      analysis_complete_macro_definition_at_cursor(
          request->source, request->source_length,
          request->cursor_byte_offset, builder.enable_trigraphs) > 0;
  int recovery_changed = 0;
  char *recovery_source = build_recovery_source(
      request->source, request->source_length,
      request->cursor_byte_offset, builder.enable_trigraphs, 0,
      &recovery_changed);
  if (!recovery_source) {
    ag_language_documentation_index_dispose(documentation_index);
    free(documentation_index);
    set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
              "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    return 0;
  }
  tk_set_filename_ctx(tokenizer, request->source_name);
  analysis_parse_state_t *parse_state = create_analysis_parse_state(
      session, tokenizer, recovery_source, documentation_index, request);
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
  int ambiguous_eof_identifier_resolved =
      (recovery_changed &
       AG_LANGUAGE_RECOVERY_AMBIGUOUS_EOF_IDENTIFIER) &&
      ambiguous_eof_identifier_resolves_as_enum_operand(
          request, builder.enable_trigraphs,
          ag_compilation_session_scope_graph(session),
          ag_compilation_session_preprocessor_context(session));
  int enum_call_elided =
      (recovery_changed & AG_LANGUAGE_RECOVERY_ENUM_CALL_ELIDED) != 0;
  analysis_identifier_span_t enum_call_identifier = {0};
  int has_enum_call_identifier =
      enum_call_elided && enum_direct_call_identifier_at_cursor(
          request->source, request->source_length,
          request->cursor_byte_offset, builder.enable_trigraphs,
          &enum_call_identifier);
  int enum_call_invokes_macro =
      has_enum_call_identifier &&
      identifier_span_invokes_function_like_macro(
          request, ag_compilation_session_preprocessor_context(session),
          builder.enable_trigraphs, &enum_call_identifier);
  int enum_call_resolves_as_enum =
      has_enum_call_identifier &&
      identifier_span_resolves_as_enum_constant(
          request, builder.enable_trigraphs,
          ag_compilation_session_scope_graph(session),
          &enum_call_identifier);
  int elided_enum_call_not_callable =
      enum_call_resolves_as_enum && !enum_call_invokes_macro;
  saved_analysis_diagnostic_t saved_fatal = {0};
  ag_diagnostic_context_t *diagnostic_context =
      ag_compilation_session_diagnostic_context(session);
  analysis_identifier_span_t unresolved_call_callee = {0};
  analysis_identifier_span_t unresolved_call_argument = {0};
  int elided_enum_call_undefined_identifier =
      enum_call_elided && enum_call_invokes_macro &&
      enum_direct_call_unresolved_identifier_argument(
          request, ag_compilation_session_scope_graph(session),
          ag_compilation_session_preprocessor_context(session),
          builder.enable_trigraphs, &unresolved_call_callee,
          &unresolved_call_argument) &&
      save_elided_call_undefined_identifier_diagnostic(
          request, diagnostic_context, &unresolved_call_callee,
          &unresolved_call_argument, &saved_fatal);
  int reparse_recoverable_enum_operand =
      ambiguous_eof_identifier_resolved ||
      (enum_call_elided && !elided_enum_call_not_callable &&
       !elided_enum_call_undefined_identifier);
  if (reparse_recoverable_enum_operand) {
    parse_state->documentation_index_owned = NULL;
    finish_analysis_parse_state(parse_state);
    parse_state = NULL;
    recovery_source = NULL;
    if (!ag_compilation_session_reset_translation_unit(session)) {
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
    recovery_source = build_recovery_source(
        request->source, request->source_length,
        request->cursor_byte_offset, builder.enable_trigraphs, 1,
        &recovery_changed);
    if (!recovery_source) {
      ag_language_documentation_index_dispose(documentation_index);
      free(documentation_index);
      set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
                "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
      return 0;
    }
    tk_set_filename_ctx(tokenizer, request->source_name);
    parse_state = create_analysis_parse_state(
        session, tokenizer, recovery_source, documentation_index, request);
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
  }
  analysis_parse_state_t *retry_state = NULL;
  analysis_parse_state_t *final_parse = parse_state;
  int recovered_before_retry = parse_state->fatal_recovered;
  int retry_attempted = 0;
  if (elided_enum_call_not_callable)
    (void)save_elided_call_not_callable_diagnostic(
        request, diagnostic_context, builder.enable_trigraphs,
        &saved_fatal);
  int retry_recovery_ready = 0;
  int semantic_macro_invocation =
      parse_state->semantic_diagnostic.code &&
      cursor_invokes_function_like_macro(
          request, ag_compilation_session_preprocessor_context(session),
          builder.enable_trigraphs);
  if (!saved_fatal.code && parse_state->fatal_recovered &&
      save_last_diagnostic(diagnostic_context, &saved_fatal)) {
    retry_recovery_ready = elide_failed_statement(
        recovery_source, request->cursor_byte_offset,
        &saved_fatal, request->source_name);
  } else if (semantic_macro_invocation) {
    retry_recovery_ready = elide_cursor_statement(
        recovery_source, request->cursor_byte_offset);
  }
  if (retry_recovery_ready) {
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
        session, tokenizer, recovery_source, documentation_index, request);
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
  if (!retry_attempted && !elided_enum_call_not_callable &&
      !elided_enum_call_undefined_identifier)
    dispose_saved_diagnostic(&saved_fatal);
  if (recovered_before_retry && !saved_fatal.code &&
      cursor_on_complete_macro_definition)
    (void)save_invalid_macro_definition_diagnostic(
        request, builder.enable_trigraphs, &saved_fatal);

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
  psx_scope_lookup_point_t point;
  if (marker) {
    point = (psx_scope_lookup_point_t){
        marker->scope_id, marker->declaration_order};
  } else if (final_parse->has_cursor_lookup_point) {
    point = final_parse->cursor_lookup_point;
  } else if (final_parse->has_semantic_lookup_point) {
    point = final_parse->semantic_lookup_point;
  } else {
    point = psx_scope_graph_capture_lookup_point(scope_graph);
  }
  if (recovery_changed &
      AG_LANGUAGE_RECOVERY_BLOCK_EXTERN_DECLARATION_RETAINED)
    (void)retained_block_extern_lookup_point(
        scope_graph, request, &point);
  const psx_scope_declaration_t *cursor_ordinary_declaration =
      ordinary_scoped_declaration_at_cursor(scope_graph, request);
  const psx_scope_declaration_t *cursor_scoped_declaration =
      cursor_ordinary_declaration
          ? cursor_ordinary_declaration
          : tag_declaration_at_cursor(scope_graph, request);
  const psx_scope_declaration_t *cursor_member_declaration =
      member_declaration_at_cursor(scope_graph, request);
  psx_semantic_context_t *semantic_context =
      ag_compilation_session_semantic_context(session);
  psx_decl_id_t cursor_member_reference_id =
      ps_ctx_record_member_reference_at_in(
          semantic_context, request->source_name,
          request->cursor_byte_offset);
  const psx_scope_declaration_t *cursor_member_reference =
      psx_scope_graph_declaration(
          scope_graph, cursor_member_reference_id);
  const psx_scope_declaration_t *cursor_member_symbol =
      cursor_member_declaration
          ? cursor_member_declaration
          : cursor_member_reference;
  const psx_scope_declaration_t *cursor_label_declaration =
      final_parse->has_cursor_lookup_point
          ? label_declaration_at_cursor(scope_graph, request, point)
          : NULL;
  if (cursor_scoped_declaration &&
      psx_scope_graph_lookup(
          scope_graph, cursor_scoped_declaration->name_space,
          cursor_scoped_declaration->name,
          cursor_scoped_declaration->name_len,
          point) != cursor_scoped_declaration->id) {
    point = (psx_scope_lookup_point_t){
        cursor_scoped_declaration->scope_id,
        cursor_scoped_declaration->declaration_order,
    };
  }
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
  if (!builder.failed && cursor_member_symbol)
    add_declaration_symbol(
        &builder, request, semantic_context, cursor_member_symbol,
        point, &symbol_capacity);
  if (!builder.failed && cursor_label_declaration)
    add_declaration_symbol(
        &builder, request, semantic_context, cursor_label_declaration,
        point, &symbol_capacity);
  if (!builder.failed)
    add_member_symbols(&builder, request, semantic_context,
                       point, &symbol_capacity);
  if (!builder.failed)
    add_macro_symbols(&builder, request,
                      ag_compilation_session_preprocessor_context(session),
                      &symbol_capacity);
  ag_language_documentation_index_dispose(documentation_index);
  free(documentation_index);
  final_parse->documentation_index_owned = NULL;
  builder.documentation_index = NULL;
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
  analysis_identifier_span_t hover_identifier = {0};
  int has_hover_identifier = analysis_identifier_span_at_mode(
      request->source, request->source_length,
      request->cursor_byte_offset, builder.enable_trigraphs,
      &hover_identifier);
  size_t after_hover_name = skip_analysis_space_and_comments_mode(
      request->source, request->source_length,
      has_hover_identifier ? hover_identifier.end : 0,
      builder.enable_trigraphs);
  int function_like_macro_invoked =
      has_hover_identifier && after_hover_name < request->source_length &&
      request->source[after_hover_name] == '(';
  select_hover(snapshot, request->source, request->source_length,
               has_hover_identifier ? &hover_identifier : NULL,
               builder.enable_trigraphs,
               function_like_macro_invoked);
  const char *member_object_name = NULL;
  size_t member_object_name_len = 0;
  int member_uses_arrow = 0;
  int cursor_is_member_access = member_base_at_cursor(
      request, &member_object_name, &member_object_name_len,
      &member_uses_arrow);
  if (cursor_label_declaration)
    (void)select_hover_kind(
        snapshot, request->source, request->source_length,
        has_hover_identifier ? &hover_identifier : NULL,
        builder.enable_trigraphs, AG_LANGUAGE_SYMBOL_LABEL);
  else if (cursor_member_symbol || cursor_is_member_access)
    (void)select_hover_kind(
        snapshot, request->source, request->source_length,
        has_hover_identifier ? &hover_identifier : NULL,
        builder.enable_trigraphs, AG_LANGUAGE_SYMBOL_MEMBER);
  else if (cursor_scoped_declaration)
    (void)select_hover_kind(
        snapshot, request->source, request->source_length,
        has_hover_identifier ? &hover_identifier : NULL,
        builder.enable_trigraphs,
        declaration_kind(cursor_scoped_declaration));
  int unresolved_ambiguous_eof_identifier =
      (recovery_changed &
       AG_LANGUAGE_RECOVERY_AMBIGUOUS_EOF_IDENTIFIER) &&
      !ambiguous_eof_identifier_resolved;
  if (unresolved_ambiguous_eof_identifier)
    snapshot->hover_index = -1;
  if (unresolved_ambiguous_eof_identifier &&
      !append_partial_identifier_diagnostic(&builder, request)) {
    dispose_saved_diagnostic(&saved_fatal);
    finish_analysis_parse_state(parse_state);
    finish_analysis_parse_state(retry_state);
    (void)ag_compilation_session_reset_translation_unit(session);
    ag_language_analysis_snapshot_dispose(snapshot);
    return 0;
  }
  int unresolved_elided_identifier =
      (recovery_changed &
       AG_LANGUAGE_RECOVERY_COMPLETE_IDENTIFIER_ELIDED) &&
      snapshot->hover_index < 0;
  int has_error = 0;
  for (int i = 0; i < snapshot->diagnostic_count; i++)
    if (snapshot->diagnostics[i].severity == 1) has_error = 1;
  snapshot->partial =
      has_error ||
      (!marker && !final_parse->has_cursor_lookup_point) ||
      (recovery_changed & AG_LANGUAGE_RECOVERY_PARTIAL_IDENTIFIER) ||
      (recovery_changed & AG_LANGUAGE_RECOVERY_INCOMPLETE_SOURCE) ||
      unresolved_elided_identifier || final_parse->fatal_recovered ||
      recovered_before_retry ||
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
      .request = request,
      .enable_trigraphs = tk_ctx_get_enable_trigraphs(
          ag_compilation_session_tokenizer(session)),
  };
  builder.primary_needs_offset_mapping =
      analysis_source_needs_offset_mapping(
          request->source, request->source_length,
          builder.enable_trigraphs);
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

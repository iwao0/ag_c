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
  int failed;
} snapshot_builder_t;

typedef struct {
  const char *name;
  const char *source;
  size_t length;
} analysis_source_view_t;

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

static char *build_recovery_source(const char *source, size_t cursor,
                                   int *changed) {
  size_t capacity = cursor * 2 + 8192;
  if (capacity < cursor || capacity > (size_t)INT_MAX) return NULL;
  char *result = calloc(capacity, 1);
  char *stack = calloc(cursor + 1, 1);
  if (!result || !stack) {
    free(result);
    free(stack);
    return NULL;
  }
  memcpy(result, source, cursor);
  int stripped_identifier = 0;
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
    stripped_identifier = 1;
  }
  size_t stack_count = 0;
  int line_comment = 0;
  int block_comment = 0;
  int quote = 0;
  int escaped = 0;
  int at_line_start = 1;
  int preprocessor_line = 0;
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
    if (c == '(' || c == '[' || c == '{') {
      stack[stack_count++] = c;
    } else if ((c == ')' || c == ']' || c == '}') && stack_count > 0) {
      char open = stack[stack_count - 1];
      if ((open == '(' && c == ')') || (open == '[' && c == ']') ||
          (open == '{' && c == '}')) stack_count--;
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
  if (line_comment) APPEND_LITERAL("\n");
  if (block_comment) APPEND_LITERAL("*/\n");
  if (quote) APPEND_LITERAL(quote == '\'' ? "'\n" : "\"\n");
  if (preprocessor_line) APPEND_LITERAL("\n");
  if (last_significant == '=' || last_significant == ',' ||
      last_significant == '(' || last_significant == '[' ||
      last_significant == '+' || last_significant == '-' ||
      last_significant == '*' || last_significant == '/' ||
      last_significant == '%' || last_significant == '&' ||
      last_significant == '|' || last_significant == '^' ||
      last_significant == '!' || last_significant == '~' ||
      last_significant == '?' || last_significant == ':')
    APPEND_LITERAL(" 0");
  for (size_t i = stack_count; i > 0; i--) {
    if (stack[i - 1] == '(') APPEND_LITERAL(")");
    else if (stack[i - 1] == '[') APPEND_LITERAL("]");
  }
  if (last_significant != ';' && last_significant != '}')
    APPEND_LITERAL(";");
  APPEND_LITERAL("\nint " AG_LANGUAGE_CURSOR_MARKER ";\n");
  for (size_t i = stack_count; i > 0; i--)
    if (stack[i - 1] == '{') APPEND_LITERAL("}\n");
  result[length] = '\0';
#undef APPEND_LITERAL
  free(stack);
  if (changed) *changed = stripped_identifier ? 2 : 1;
  return result;
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
  symbol->kind = declaration_kind(declaration);
  symbol->name_space = declaration_namespace(declaration->name_space);
  symbol->scope_depth = psx_scope_graph_scope_depth(
      ps_ctx_scope_graph(semantic_context), declaration->scope_id);
  symbol->declaration_order = declaration->declaration_order;
  locate_declaration(builder, request, declaration, declaration->name,
                     (size_t)declaration->name_len, &symbol->declaration);
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
  if (symbol->kind == AG_LANGUAGE_SYMBOL_FUNCTION) {
    free(symbol->signature);
    symbol->signature = snapshot_copy(builder, symbol->type);
    fill_function(builder, types, qual_type, symbol);
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
  snapshot_builder_t builder = {snapshot, error, limits, 0};
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
      request->source, request->cursor_byte_offset, &recovery_changed);
  if (!recovery_source) {
    set_error(error, AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
              "AGC_LANGUAGE_ANALYSIS_OUT_OF_MEMORY", NULL, 0, 0);
    return 0;
  }
  tokenizer_context_t *tokenizer = ag_compilation_session_tokenizer(session);
  tk_set_filename_ctx(tokenizer, request->source_name);
  analysis_parse_state_t parse_state = {
      .session = session,
      .tokenizer = tokenizer,
      .recovery_source = recovery_source,
  };
  if (!parse_analysis_source(&parse_state)) {
    if (parse_state.preprocessor_stream)
      pp_stream_close(parse_state.preprocessor_stream);
    free(recovery_source);
    set_error(error, AG_LANGUAGE_ANALYSIS_FAILED,
              "AGC_LANGUAGE_ANALYSIS_PARSE_START_FAILED", NULL, 0, 0);
    return 0;
  }
  saved_analysis_diagnostic_t saved_fatal = {0};
  analysis_parse_state_t retry_state = {0};
  analysis_parse_state_t *final_parse = &parse_state;
  int recovered_before_retry = parse_state.fatal_recovered;
  int retry_attempted = 0;
  ag_diagnostic_context_t *diagnostic_context =
      ag_compilation_session_diagnostic_context(session);
  if (parse_state.fatal_recovered &&
      save_last_diagnostic(diagnostic_context, &saved_fatal) &&
      elide_failed_statement(
          recovery_source, request->cursor_byte_offset,
          &saved_fatal, request->source_name)) {
    if (parse_state.preprocessor_stream) {
      pp_stream_close(parse_state.preprocessor_stream);
      parse_state.preprocessor_stream = NULL;
    }
    if (ag_compilation_session_reset_translation_unit(session)) {
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
      retry_state = (analysis_parse_state_t){
          .session = session,
          .tokenizer = tokenizer,
          .recovery_source = recovery_source,
      };
      retry_attempted = 1;
      final_parse = &retry_state;
      (void)parse_analysis_source(&retry_state);
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
  if (!builder.failed)
    add_member_symbols(&builder, request, semantic_context,
                       point, &symbol_capacity);
  if (!builder.failed)
    add_macro_symbols(&builder, request,
                      ag_compilation_session_preprocessor_context(session),
                      &symbol_capacity);
  const ag_diagnostic_context_t *diagnostics =
      ag_compilation_session_diagnostic_context(session);
  if (!builder.failed)
    copy_diagnostics(&builder, diagnostics, request->cursor_byte_offset);
  if (!builder.failed)
    append_saved_diagnostic(&builder, &saved_fatal);
  if (!builder.failed)
    append_saved_diagnostic(
        &builder, &final_parse->semantic_diagnostic);
  if (!builder.failed && recovery_changed > 1)
    append_partial_identifier_diagnostic(&builder, request);
  if (final_parse->preprocessor_stream)
    pp_stream_close(final_parse->preprocessor_stream);
  free(recovery_source);
  if (builder.failed) {
    dispose_saved_diagnostic(&saved_fatal);
    dispose_saved_diagnostic(&parse_state.semantic_diagnostic);
    dispose_saved_diagnostic(&retry_state.semantic_diagnostic);
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
  int has_error = 0;
  for (int i = 0; i < snapshot->diagnostic_count; i++)
    if (snapshot->diagnostics[i].severity == 1) has_error = 1;
  snapshot->partial = has_error || !marker || recovery_changed > 1 ||
                      final_parse->fatal_recovered || recovered_before_retry ||
                      final_parse->semantic_diagnostic.code != NULL;
  dispose_saved_diagnostic(&saved_fatal);
  dispose_saved_diagnostic(&parse_state.semantic_diagnostic);
  dispose_saved_diagnostic(&retry_state.semantic_diagnostic);
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
    free(symbol->constant_value);
    free(symbol->macro_replacement);
    dispose_range(&symbol->declaration);
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

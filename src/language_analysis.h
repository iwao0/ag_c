#ifndef AG_LANGUAGE_ANALYSIS_H
#define AG_LANGUAGE_ANALYSIS_H

#include <stddef.h>

#include "compilation_session.h"

typedef enum {
  AG_LANGUAGE_SYMBOL_OBJECT = 0,
  AG_LANGUAGE_SYMBOL_PARAMETER,
  AG_LANGUAGE_SYMBOL_FUNCTION,
  AG_LANGUAGE_SYMBOL_TYPEDEF,
  AG_LANGUAGE_SYMBOL_ENUM_CONSTANT,
  AG_LANGUAGE_SYMBOL_TAG,
  AG_LANGUAGE_SYMBOL_MEMBER,
  AG_LANGUAGE_SYMBOL_MACRO,
} ag_language_symbol_kind_t;

typedef enum {
  AG_LANGUAGE_NAMESPACE_ORDINARY = 0,
  AG_LANGUAGE_NAMESPACE_TAG,
  AG_LANGUAGE_NAMESPACE_LABEL,
  AG_LANGUAGE_NAMESPACE_MEMBER,
  AG_LANGUAGE_NAMESPACE_MACRO,
} ag_language_namespace_t;

typedef enum {
  AG_LANGUAGE_INITIALIZER_NONE = 0,
  AG_LANGUAGE_INITIALIZER_EXPLICIT_CONSTANT,
  AG_LANGUAGE_INITIALIZER_ZERO,
  AG_LANGUAGE_INITIALIZER_INDETERMINATE,
  AG_LANGUAGE_INITIALIZER_RUNTIME,
} ag_language_initializer_state_t;

typedef struct {
  int line;
  int column;
  int offset;
} ag_language_position_t;

typedef struct {
  char *source_name;
  ag_language_position_t start;
  ag_language_position_t end;
} ag_language_source_range_t;

typedef struct {
  char *name;
  char *type;
} ag_language_parameter_t;

typedef struct {
  char *name;
  ag_language_symbol_kind_t kind;
  ag_language_namespace_t name_space;
  char *type;
  char *signature;
  char *return_type;
  char *storage_class;
  int scope_depth;
  unsigned int declaration_order;
  ag_language_source_range_t declaration;
  ag_language_initializer_state_t initializer_state;
  char *constant_value;
  int has_initializer_range;
  ag_language_source_range_t initializer_range;
  int has_function_prototype;
  int is_variadic;
  ag_language_parameter_t *parameters;
  int parameter_count;
  int macro_is_function_like;
  int macro_is_variadic;
  char **macro_parameters;
  int macro_parameter_count;
  char *macro_replacement;
} ag_language_symbol_t;

typedef struct {
  int severity;
  char *code;
  char *message;
  ag_language_source_range_t range;
} ag_language_diagnostic_t;

typedef struct {
  ag_language_diagnostic_t *diagnostics;
  int diagnostic_count;
  ag_language_symbol_t *completion_items;
  int completion_item_count;
  int hover_index;
  int partial;
  size_t allocated_bytes;
} ag_language_analysis_snapshot_t;

typedef struct {
  int max_sources;
  size_t max_source_bytes;
  size_t max_total_source_bytes;
  int max_symbols;
  int max_completion_items;
  int max_string_bytes;
  int max_snapshot_bytes;
} ag_language_analysis_limits_t;

typedef struct {
  const char *source_name;
  const char *source;
  size_t source_length;
  const char *cursor_source_name;
  size_t cursor_byte_offset;
  const unsigned char *virtual_header_bundle;
  size_t virtual_header_bundle_length;
  int max_header_files;
  int max_header_file_bytes;
  int max_header_total_bytes;
  int max_include_depth;
  ag_language_analysis_limits_t limits;
} ag_language_analysis_request_t;

typedef enum {
  AG_LANGUAGE_ANALYSIS_OK = 0,
  AG_LANGUAGE_ANALYSIS_INVALID_REQUEST,
  AG_LANGUAGE_ANALYSIS_OUT_OF_MEMORY,
  AG_LANGUAGE_ANALYSIS_RESOURCE_LIMIT,
  AG_LANGUAGE_ANALYSIS_FAILED,
} ag_language_analysis_status_t;

typedef struct {
  ag_language_analysis_status_t status;
  char code[48];
  char limit[40];
  size_t max;
  size_t actual;
} ag_language_analysis_error_t;

ag_language_analysis_limits_t ag_language_analysis_default_limits(void);
int ag_language_analyze_source(
    ag_compilation_session_t *session,
    const ag_language_analysis_request_t *request,
    ag_language_analysis_snapshot_t *snapshot,
    ag_language_analysis_error_t *error);
void ag_language_analysis_snapshot_dispose(
    ag_language_analysis_snapshot_t *snapshot);

/* Serializes the immutable snapshot. Returns the required byte length without
 * the trailing NUL, -1 on invalid input, and -2 when out_size is too small. */
int ag_language_analysis_snapshot_write_json(
    const ag_language_analysis_snapshot_t *snapshot,
    char *out, size_t out_size);

#endif

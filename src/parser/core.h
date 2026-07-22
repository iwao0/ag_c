#ifndef PARSER_INTERNAL_CORE_H
#define PARSER_INTERNAL_CORE_H

/* core.h は AST node 型を使わない (token_kind_t と bool のみ)。
 * Phase C1-2: ast.h ではなく token.h を直接 include する。 */
#include "../tokenizer/token.h"
#include "name_classifier.h"
#include <stdbool.h>

#define PS_MAX_DECLARATOR_COUNT 1024
#define PS_MAX_INITIALIZER_ELEMENTS 4096

typedef struct tokenizer_context_t tokenizer_context_t;
typedef struct ag_diagnostic_context_t ag_diagnostic_context_t;

typedef struct {
  token_kind_t kind;
  int is_unsigned;
  int is_complex;
  int is_long_long;
  int is_plain_char;
  int is_long_double;
  int is_atomic;
  int is_thread_local;
  int is_const_qualified;
  int is_volatile_qualified;
  int is_restrict_qualified;
  int is_inline;
  int is_noreturn;
  int is_typedef;
  int is_auto;
  int is_register;
  int is_extern;
  int is_static;
  int storage_class_count;
  int thread_local_count;
} psx_type_spec_result_t;

typedef struct {
  void *context;
  ag_diagnostic_context_t *diagnostics;
  tokenizer_context_t *tokenizer_context;
  const psx_name_classifier_t *name_classifier;
  void *consume_alignas_context;
  void (*consume_alignas)(void *context, psx_type_spec_result_t *result);
  void (*diagnose_complex_requires_float)(void *context, token_t *token);
} psx_type_spec_syntax_t;

token_kind_t psx_consume_type_kind_with_syntax_ex(
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax);
void psx_consume_decl_modifiers_with_syntax_ex(
    psx_type_spec_result_t *out, const psx_type_spec_syntax_t *syntax);
bool psx_is_decl_prefix_token(token_kind_t k);
bool psx_is_type_specifier_token(token_kind_t kind);
bool psx_is_tag_keyword_token(token_kind_t kind);
bool psx_is_gnu_attribute_token(const token_t *t);
void psx_skip_gnu_attributes_ctx(tokenizer_context_t *tokenizer_context);
void psx_skip_gnu_attributes_at(token_t **t);
void psx_skip_gnu_attributes_at_with_diagnostics(
    token_t **t, ag_diagnostic_context_t *diagnostics);
typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;
bool psx_try_consume_pragma_pack_marker_in(
    psx_parser_runtime_context_t *runtime_context);

#endif

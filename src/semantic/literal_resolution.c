#include "literal_resolution.h"

#include <stdio.h>
#include <stdlib.h>

#include "../parser/ast.h"
#include "../parser/global_registry.h"
#include "../parser/semantic_ctx.h"
#include "../parser/symtab.h"
#include "resolution_state_access.h"
#include "resolution_state.h"
#include "type_identity.h"

static psx_floating_kind_t literal_floating_kind(
    tk_float_kind_t token_kind) {
  switch (token_kind) {
    case TK_FLOAT_KIND_FLOAT:
      return PSX_FLOATING_KIND_FLOAT;
    case TK_FLOAT_KIND_LONG_DOUBLE:
      return PSX_FLOATING_KIND_LONG_DOUBLE;
    case TK_FLOAT_KIND_DOUBLE:
      return PSX_FLOATING_KIND_DOUBLE;
    case TK_FLOAT_KIND_NONE:
      return PSX_FLOATING_KIND_NONE;
  }
  return PSX_FLOATING_KIND_NONE;
}

static int finish_literal_resolution(
    psx_semantic_context_t *semantic_context,
    psx_qual_type_t qual_type,
    psx_literal_semantic_resolution_t *resolution) {
  if (!semantic_context || !resolution ||
      qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  resolution->qual_type = qual_type;
  return psx_semantic_type_table_qual_type_is_valid(
      ps_ctx_semantic_type_table_in(semantic_context),
      resolution->qual_type);
}

int psx_resolve_number_literal_semantics_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    const node_num_t *literal,
    psx_literal_semantic_resolution_t *resolution) {
  if (resolution) *resolution = (psx_literal_semantic_resolution_t){0};
  if (!semantic_context || !literal || !resolution) return 0;
  token_t *tok = literal->base.tok;
  psx_qual_type_t qual_type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (tok && tok->kind == TK_NUM &&
      tk_as_num(tok)->num_kind == TK_NUM_KIND_FLOAT) {
    tk_float_kind_t fp_kind = tk_as_num_float(tok)->fp_kind;
    qual_type = ps_ctx_intern_floating_qual_type_in(
        semantic_context, literal_floating_kind(fp_kind), 0);
    if (global_registry) {
      float_lit_t *registered = calloc(1, sizeof(*registered));
      if (!registered) return 0;
      registered->id = ps_global_registry_next_float_literal_id(
          global_registry);
      registered->fval = literal->fval;
      registered->fp_kind = fp_kind;
      registered->float_suffix_kind = literal->float_suffix_kind;
      psx_register_float_lit_in(global_registry, registered);
    }
  } else {
    tk_int_size_t int_size = TK_INT_SIZE_INT;
    int is_unsigned = 0;
    if (tok && tok->kind == TK_NUM &&
        tk_as_num(tok)->num_kind == TK_NUM_KIND_INT) {
      int_size = tk_as_num_int(tok)->int_size;
      is_unsigned = tk_as_num_int(tok)->is_unsigned ? 1 : 0;
    }
    psx_integer_kind_t integer_kind =
        int_size == TK_INT_SIZE_LONG_LONG
            ? PSX_INTEGER_KIND_LONG_LONG
        : int_size == TK_INT_SIZE_LONG
            ? PSX_INTEGER_KIND_LONG
            : PSX_INTEGER_KIND_INT;
    qual_type = ps_ctx_intern_integer_qual_type_in(
        semantic_context, integer_kind, is_unsigned, 0);
  }
  return finish_literal_resolution(
      semantic_context, qual_type, resolution);
}

int psx_resolve_string_literal_semantics_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    const node_string_t *literal,
    psx_literal_semantic_resolution_t *resolution) {
  if (!literal) {
    if (resolution) *resolution = (psx_literal_semantic_resolution_t){0};
    return 0;
  }
  psx_string_literal_value_t value = {
      .contents = literal->literal_contents,
      .length = literal->literal_length,
      .character_width = literal->char_width,
      .prefix_kind = literal->str_prefix_kind,
  };
  return psx_resolve_string_literal_value_in_contexts(
      semantic_context, global_registry, &value, resolution);
}

int psx_resolve_string_literal_value_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    const psx_string_literal_value_t *literal,
    psx_literal_semantic_resolution_t *resolution) {
  if (resolution) *resolution = (psx_literal_semantic_resolution_t){0};
  if (!semantic_context || !literal || !resolution) return 0;
  int element_width = literal->character_width
                          ? literal->character_width
                          : TK_CHAR_WIDTH_CHAR;
  int element_is_unsigned =
      literal->prefix_kind == TK_STR_PREFIX_u ||
      literal->prefix_kind == TK_STR_PREFIX_U;
  psx_integer_kind_t element_kind =
      element_width == TK_CHAR_WIDTH_CHAR
          ? PSX_INTEGER_KIND_CHAR
      : element_width == TK_CHAR_WIDTH_CHAR16
          ? PSX_INTEGER_KIND_SHORT
          : PSX_INTEGER_KIND_INT;
  psx_qual_type_t element_type = ps_ctx_intern_integer_qual_type_in(
      semantic_context, element_kind, element_is_unsigned,
      element_width == TK_CHAR_WIDTH_CHAR);
  psx_qual_type_t type = ps_ctx_intern_pointer_to_qual_type_in(
      semantic_context, element_type);
  if (global_registry) {
    string_lit_t *registered = calloc(1, sizeof(*registered));
    if (!registered) return 0;
    int id = ps_global_registry_next_string_literal_id(
        global_registry);
    int label_length = snprintf(NULL, 0, ".LC%d", id);
    char *string_label = calloc((size_t)label_length + 1, 1);
    if (!string_label) {
      free(registered);
      return 0;
    }
    snprintf(
        string_label, (size_t)label_length + 1, ".LC%d", id);
    registered->label = string_label;
    registered->str = literal->contents;
    registered->len = literal->length;
    registered->char_width = (tk_char_width_t)literal->character_width;
    registered->str_prefix_kind =
        (tk_string_prefix_kind_t)literal->prefix_kind;
    psx_register_string_lit_in(global_registry, registered);
    resolution->string_label = string_label;
  }
  return finish_literal_resolution(
      semantic_context, type, resolution);
}

void psx_string_literal_bind_label(
    psx_resolution_store_t *store,
    node_string_t *literal, char *label) {
  psx_node_resolution_state_t *state =
      ps_node_resolution_state(store, literal ? &literal->base : NULL);
  if (state) state->literal.string_label = label;
}

char *psx_string_literal_label(
    const psx_resolution_store_t *store,
    const node_string_t *literal) {
  const psx_node_resolution_state_t *state =
      ps_node_resolution_state_const(
          store, literal ? &literal->base : NULL);
  return state ? state->literal.string_label : NULL;
}

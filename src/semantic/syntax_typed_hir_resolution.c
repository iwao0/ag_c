#include "syntax_typed_hir_resolution.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../declaration_pipeline.h"
#include "../lowering/cast_lowering.h"
#include "../lowering/local_object_lowering.h"
#include "../lowering/runtime_context.h"
#include "../parser/arena.h"
#include "../parser/ast.h"
#include "../parser/decl.h"
#include "../parser/function_definition_syntax.h"
#include "../parser/global_registry.h"
#include "../parser/local_declaration_syntax.h"
#include "../parser/lvar_public.h"
#include "../parser/semantic_ctx.h"
#include "../parser/vla_runtime.h"
#include "../target_info.h"
#include "../type_layout.h"
#include "assignment_resolution.h"
#include "call_resolution.h"
#include "character_array_initializer.h"
#include "compound_literal_semantics.h"
#include "declaration_application.h"
#include "declaration_specifier_resolution.h"
#include "typedef_declaration_resolution.h"
#include "expression_operand_resolution.h"
#include "function_definition_resolution.h"
#include "function_call_resolution.h"
#include "generic_selection_resolution.h"
#include "hir_local_resolution.h"
#include "hir_member_resolution.h"
#include "hir_symbol_resolution.h"
#include "identifier_resolution.h"
#include "initializer_resolution.h"
#include "integer_constant_evaluation.h"
#include "literal_resolution.h"
#include "semantic_node_builder.h"
#include "source_cast_type_resolution.h"
#include "static_assert_resolution.h"
#include "type_name_resolution.h"
#include "type_query_semantics.h"
#include "typed_hir_tree_internal.h"

typedef struct direct_identifier_binding_t
    direct_identifier_binding_t;
typedef struct direct_cast_binding_t direct_cast_binding_t;
typedef struct direct_call_binding_t direct_call_binding_t;
typedef struct direct_generic_binding_t direct_generic_binding_t;
typedef struct direct_type_query_binding_t direct_type_query_binding_t;
typedef struct direct_compound_literal_binding_t
    direct_compound_literal_binding_t;
typedef struct direct_case_binding_t direct_case_binding_t;
typedef struct direct_case_value_t direct_case_value_t;
typedef struct direct_switch_scope_t direct_switch_scope_t;
typedef struct direct_goto_binding_t direct_goto_binding_t;
typedef struct direct_local_declaration_binding_t
    direct_local_declaration_binding_t;
typedef struct direct_function_declaration_checkpoint_t
    direct_function_declaration_checkpoint_t;

typedef struct {
  psx_local_initializer_plan_t plan;
  lvar_t **evaluation_temporaries;
} direct_flat_initializer_binding_t;

typedef struct {
  int stride_frame_offset;
  int strides_remaining;
} direct_vla_runtime_view_t;

typedef struct {
  const node_t *value_syntax;
  lvar_t *storage;
  psx_qual_type_t value_qual_type;
} direct_typedef_bound_capture_t;

typedef struct {
  lvar_t *local;
  psx_qual_type_t declaration_qual_type;
  const psx_parsed_initializer_t *initializer;
  direct_flat_initializer_binding_t flat_initializer;
  psx_character_array_initializer_plan_t character_array_initializer;
  psx_vla_runtime_plan_t *vla_runtime_plan;
  direct_typedef_bound_capture_t *typedef_bound_captures;
  int typedef_bound_capture_count;
  int is_semantic_only;
  int is_object_copy_initializer;
} direct_local_declarator_binding_t;

struct direct_function_declaration_checkpoint_t {
  char *name;
  int name_len;
  psx_function_registration_checkpoint_t checkpoint;
  direct_function_declaration_checkpoint_t *next;
};

struct direct_local_declaration_binding_t {
  const node_local_declaration_t *syntax;
  direct_local_declarator_binding_t *declarators;
  int declarator_count;
  int is_semantic_only;
  direct_local_declaration_binding_t *next;
};

enum {
  DIRECT_IDENTIFIER_USAGE_EVALUATED = 1u << 0,
  DIRECT_IDENTIFIER_USAGE_ADDRESS_TAKEN = 1u << 1,
  DIRECT_IDENTIFIER_USAGE_INITIALIZED = 1u << 2,
};

struct direct_identifier_binding_t {
  const node_identifier_t *syntax;
  psx_identifier_expression_resolution_t resolution;
  unsigned usage_flags;
  direct_identifier_binding_t *next;
};

struct direct_cast_binding_t {
  const node_source_cast_t *syntax;
  psx_qual_type_t target_qual_type;
  psx_source_cast_types_resolution_t type_resolution;
  psx_aggregate_source_cast_plan_t aggregate_plan;
  unsigned char types_resolved;
  direct_cast_binding_t *next;
};

struct direct_call_binding_t {
  const node_function_call_t *syntax;
  psx_call_types_resolution_t resolution;
  const node_identifier_t *direct_identifier;
  unsigned char is_implicit;
  direct_call_binding_t *next;
};

struct direct_generic_binding_t {
  const node_generic_selection_t *syntax;
  int selected_index;
  psx_qual_type_t result_qual_type;
  direct_generic_binding_t *next;
};

struct direct_type_query_binding_t {
  const node_t *syntax;
  psx_type_query_plan_t plan;
  psx_semantic_expr_id_t *runtime_factor_ids;
  int runtime_factor_count;
  const node_t **evaluated_prefixes;
  int evaluated_prefix_count;
  direct_type_query_binding_t *next;
};

struct direct_compound_literal_binding_t {
  const node_compound_literal_t *syntax;
  psx_compound_literal_plan_t plan;
  lvar_t *local_object;
  global_var_t *global_object;
  direct_flat_initializer_binding_t flat_initializer;
  psx_character_array_initializer_plan_t character_array_initializer;
  direct_compound_literal_binding_t *next;
};

struct direct_case_binding_t {
  const node_case_t *syntax;
  long long value;
  direct_case_binding_t *next;
};

struct direct_case_value_t {
  long long value;
  direct_case_value_t *next;
};

struct direct_switch_scope_t {
  direct_case_value_t *case_values;
  direct_switch_scope_t *parent;
  unsigned char has_default;
};

struct direct_goto_binding_t {
  const node_jump_t *syntax;
  direct_goto_binding_t *next;
};

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const ag_compilation_options_t *options;
  psx_semantic_node_builder_t builder;
  psx_resolved_hir_build_failure_t *failure;
  const psx_local_lookup_point_t *identifier_lookup_point;
  direct_identifier_binding_t *identifier_bindings;
  direct_cast_binding_t *cast_bindings;
  direct_call_binding_t *call_bindings;
  direct_generic_binding_t *generic_bindings;
  direct_type_query_binding_t *type_query_bindings;
  direct_compound_literal_binding_t *compound_literal_bindings;
  direct_case_binding_t *case_bindings;
  direct_switch_scope_t *switch_scope;
  size_t label_declaration_start;
  direct_goto_binding_t *gotos;
  direct_local_declaration_binding_t *local_declarations;
  direct_function_declaration_checkpoint_t *function_declarations;
  unsigned int loop_depth;
  unsigned int switch_depth;
  unsigned int block_depth;
  unsigned int unevaluated_depth;
  char *function_name;
  int function_name_len;
  psx_qual_type_t function_return_qual_type;
  int enforce_function_return_type;
  int preflight_failed;
  int suppress_value_decay_depth;
} direct_resolution_context_t;

typedef struct {
  psx_semantic_context_t *semantic_context;
  psx_global_registry_t *global_registry;
  psx_local_registry_t *local_registry;
  psx_lowering_context_t *lowering_context;
  const token_ident_t *function_name;
  psx_function_registration_checkpoint_t function_checkpoint;
  psx_global_registry_checkpoint_t global_checkpoint;
  psx_local_registry_checkpoint_t local_checkpoint;
  psx_lowering_context_checkpoint_t lowering_checkpoint;
} direct_function_transaction_t;

static int preflight_direct_expression(
    direct_resolution_context_t *context,
    const node_t *syntax, psx_qual_type_t *qual_type);
static int preflight_direct_expression_impl(
    direct_resolution_context_t *context,
    const node_t *syntax, psx_qual_type_t *qual_type);
static int preflight_direct_statement(
    direct_resolution_context_t *context,
    const node_t *syntax);
static int preflight_direct_statement_impl(
    direct_resolution_context_t *context,
    const node_t *syntax);
static int preflight_direct_lvalue(
    direct_resolution_context_t *context,
    const node_t *syntax, psx_qual_type_t *qual_type);
static int resolve_direct_compound_literal(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound,
    direct_compound_literal_binding_t **out_binding);
static direct_compound_literal_binding_t *
find_direct_compound_literal_binding(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound);
static psx_semantic_node_t *build_direct_compound_literal(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound);
static psx_semantic_node_t *build_direct_addressable_compound_literal(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound);
static void set_failure(
    psx_resolved_hir_build_failure_t *failure,
    psx_resolved_hir_build_status_t status,
    const node_t *source);
static int note_direct_semantic_rejection(
    direct_resolution_context_t *context,
    psx_syntax_typed_hir_rejection_t rejection,
    const node_t *source);
static int note_direct_integer_rejection(
    direct_resolution_context_t *context,
    psx_syntax_typed_hir_rejection_t rejection,
    const node_t *source, long long value);
static int resolve_direct_identifier(
    direct_resolution_context_t *context,
    const node_identifier_t *identifier,
    psx_identifier_expression_resolution_t *resolution);
static int resolve_direct_call_identifier(
    direct_resolution_context_t *context,
    const node_identifier_t *identifier,
    psx_identifier_expression_resolution_t *resolution);
static int direct_syntax_has_vla_binding(
    direct_resolution_context_t *context,
    const node_t *syntax);
static int resolve_direct_declarator_application(
    direct_resolution_context_t *context,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application);

static int resolve_direct_source_cast(
    direct_resolution_context_t *context,
    const node_source_cast_t *cast,
    psx_qual_type_t *target_qual_type) {
  if (!context || !cast || cast->base.kind != ND_SOURCE_CAST ||
      !target_qual_type)
    return 0;
  for (direct_cast_binding_t *binding = context->cast_bindings;
       binding; binding = binding->next) {
    if (binding->syntax == cast) {
      *target_qual_type = binding->target_qual_type;
      return 1;
    }
  }
  psx_qual_type_t resolved;
  if (!psx_resolve_type_name_qual_type_in_contexts(
          context->semantic_context, context->global_registry,
          context->local_registry, &cast->type_name, &resolved))
    return 0;
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      context->semantic_context, resolved.type_id);
  if (!canonical) return 0;
  direct_cast_binding_t *binding = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*binding));
  if (!binding) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &cast->base);
    return 0;
  }
  *binding = (direct_cast_binding_t){
      .syntax = cast,
      .target_qual_type = resolved,
      .next = context->cast_bindings,
  };
  context->cast_bindings = binding;
  *target_qual_type = resolved;
  return 1;
}

static direct_cast_binding_t *find_direct_cast_binding(
    direct_resolution_context_t *context,
    const node_source_cast_t *cast) {
  if (!context || !cast) return NULL;
  for (direct_cast_binding_t *binding = context->cast_bindings;
       binding; binding = binding->next) {
    if (binding->syntax == cast) return binding;
  }
  return NULL;
}

static int note_direct_source_cast_rejection(
    direct_resolution_context_t *context,
    const node_t *syntax,
    const psx_source_cast_types_resolution_t *resolution) {
  if (!resolution) return 0;
  psx_syntax_typed_hir_rejection_t rejection;
  switch (resolution->status) {
    case PSX_SOURCE_CAST_TARGET_NOT_VOID_OR_SCALAR:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_TARGET_NOT_VOID_OR_SCALAR;
      break;
    case PSX_SOURCE_CAST_OPERAND_NOT_SCALAR:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_OPERAND_NOT_SCALAR;
      break;
    case PSX_SOURCE_CAST_AGGREGATE_TYPE_MISMATCH:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_AGGREGATE_TYPE_MISMATCH;
      break;
    case PSX_SOURCE_CAST_STRUCT_EXTENSION_DISABLED:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_STRUCT_EXTENSION_DISABLED;
      break;
    case PSX_SOURCE_CAST_UNION_EXTENSION_DISABLED:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_UNION_EXTENSION_DISABLED;
      break;
    case PSX_SOURCE_CAST_AGGREGATE_UNSUPPORTED:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_AGGREGATE_UNSUPPORTED;
      break;
    case PSX_SOURCE_CAST_AGGREGATE_MEMBER_NOT_FOUND:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_CAST_AGGREGATE_MEMBER_NOT_FOUND;
      break;
    case PSX_SOURCE_CAST_TYPES_OK:
    case PSX_SOURCE_CAST_TYPES_INVALID:
      return 0;
  }
  return note_direct_integer_rejection(
      context, rejection, syntax, resolution->target_type_kind);
}

static int resolve_direct_function_call(
    direct_resolution_context_t *context,
    const node_function_call_t *call,
    direct_call_binding_t **out_binding) {
  if (out_binding) *out_binding = NULL;
  if (!context || !call || !call->callee)
    return 0;
  for (direct_call_binding_t *binding = context->call_bindings;
       binding; binding = binding->next) {
    if (binding->syntax == call) {
      if (out_binding) *out_binding = binding;
      return 1;
    }
  }

  const node_identifier_t *direct_identifier = NULL;
  psx_qual_type_t callee_type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (call->callee->kind == ND_IDENTIFIER) {
    const node_identifier_t *identifier =
        (const node_identifier_t *)call->callee;
    psx_identifier_expression_resolution_t callee_resolution;
    if (!resolve_direct_call_identifier(
            context, identifier, &callee_resolution))
      return 0;
    callee_type = callee_resolution.expression_qual_type;
    if (callee_resolution.symbol.kind == PSX_IDENTIFIER_FUNCTION ||
        callee_resolution.symbol.kind ==
            PSX_IDENTIFIER_UNDECLARED_CALL)
      direct_identifier = identifier;
  } else if (!preflight_direct_expression(
                 context, call->callee, &callee_type)) {
    return 0;
  }
  psx_call_types_resolution_t resolution;
  psx_resolve_call_qual_types_in(
      context->semantic_context,
      callee_type,
      call->argument_count,
      &resolution);
  if (resolution.status == PSX_CALL_TYPES_NOT_CALLABLE)
    return note_direct_semantic_rejection(
        context, PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_NOT_CALLABLE,
        &call->base);
  if (resolution.status == PSX_CALL_TYPES_ARGUMENT_COUNT_MISMATCH)
    return note_direct_semantic_rejection(
        context,
        PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_ARGUMENT_COUNT_MISMATCH,
        &call->base);
  if (resolution.status != PSX_CALL_TYPES_OK) return 0;
  for (int i = 0; i < call->argument_count; i++) {
    psx_qual_type_t argument_type;
    if (!preflight_direct_expression(
            context, call->arguments[i], &argument_type))
      return 0;
  }

  direct_call_binding_t *binding = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*binding));
  if (!binding) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &call->base);
    return 0;
  }
  *binding = (direct_call_binding_t){
      .syntax = call,
      .resolution = resolution,
      .direct_identifier = direct_identifier,
      .is_implicit =
          direct_identifier &&
          callee_type.type_id != PSX_TYPE_ID_INVALID &&
          ps_ctx_find_function_symbol_in(
              context->semantic_context,
              direct_identifier->name,
              direct_identifier->name_len) == NULL,
      .next = context->call_bindings,
  };
  context->call_bindings = binding;
  if (out_binding) *out_binding = binding;
  return 1;
}

static direct_generic_binding_t *find_direct_generic_binding(
    direct_resolution_context_t *context,
    const node_generic_selection_t *selection) {
  if (!context || !selection) return NULL;
  for (direct_generic_binding_t *binding = context->generic_bindings;
       binding; binding = binding->next) {
    if (binding->syntax == selection) return binding;
  }
  return NULL;
}

static int preflight_direct_unevaluated_expression(
    direct_resolution_context_t *context, const node_t *syntax,
    psx_qual_type_t *qual_type) {
  if (!context) return 0;
  context->unevaluated_depth++;
  int resolved = preflight_direct_expression(
      context, syntax, qual_type);
  context->unevaluated_depth--;
  return resolved;
}

static int preflight_direct_sizeof_operand(
    direct_resolution_context_t *context, const node_t *syntax,
    psx_qual_type_t *qual_type) {
  if (!context) return 0;
  context->unevaluated_depth++;
  context->suppress_value_decay_depth++;
  int resolved = preflight_direct_expression(
      context, syntax, qual_type);
  context->suppress_value_decay_depth--;
  context->unevaluated_depth--;
  return resolved;
}

static int resolve_direct_generic_selection(
    direct_resolution_context_t *context,
    const node_generic_selection_t *selection,
    direct_generic_binding_t **out_binding) {
  if (out_binding) *out_binding = NULL;
  if (!context || !selection || !selection->control ||
      !selection->associations || selection->association_count <= 0)
    return 0;
  direct_generic_binding_t *existing =
      find_direct_generic_binding(context, selection);
  if (existing) {
    if (out_binding) *out_binding = existing;
    return 1;
  }

  psx_qual_type_t control_type;
  if (!preflight_direct_unevaluated_expression(
          context, selection->control, &control_type))
    return 0;
  size_t association_count = (size_t)selection->association_count;
  psx_qual_type_t *association_types = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      association_count * sizeof(*association_types));
  unsigned char *is_default = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), association_count);
  if (!association_types || !is_default) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &selection->base);
    return 0;
  }
  memset(association_types, 0,
         association_count * sizeof(*association_types));
  for (int i = 0; i < selection->association_count; i++) {
    const psx_generic_association_t *association =
        &selection->associations[i];
    is_default[i] = association->is_default ? 1 : 0;
    if (!association->is_default &&
        !psx_resolve_type_name_qual_type_in_contexts(
            context->semantic_context, context->global_registry,
            context->local_registry, &association->type_name,
            &association_types[i]))
      return 0;
  }
  psx_generic_selection_resolution_t resolution;
  psx_resolve_generic_selection_qual_types_in(
      control_type, association_types, is_default,
      selection->association_count, &resolution);
  switch (resolution.status) {
    case PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_DEFAULT:
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_DUPLICATE_DEFAULT,
          &selection->base);
    case PSX_GENERIC_SELECTION_RESOLUTION_DUPLICATE_COMPATIBLE_TYPE:
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_DUPLICATE_COMPATIBLE_TYPE,
          &selection->base);
    case PSX_GENERIC_SELECTION_RESOLUTION_NO_MATCH:
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_GENERIC_NO_MATCH,
          &selection->base);
    case PSX_GENERIC_SELECTION_RESOLUTION_TYPE_UNRESOLVED:
      return 0;
    case PSX_GENERIC_SELECTION_RESOLUTION_OK:
      break;
  }

  for (int i = 0; i < selection->association_count; i++) {
    if (i == resolution.selected_index) continue;
    psx_qual_type_t ignored_type;
    if (!preflight_direct_unevaluated_expression(
            context, selection->associations[i].expression,
            &ignored_type))
      return 0;
  }
  psx_qual_type_t result_type;
  if (!preflight_direct_expression(
          context,
          selection->associations[resolution.selected_index].expression,
          &result_type))
    return 0;

  direct_generic_binding_t *binding = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*binding));
  if (!binding) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &selection->base);
    return 0;
  }
  *binding = (direct_generic_binding_t){
      .syntax = selection,
      .selected_index = resolution.selected_index,
      .result_qual_type = result_type,
      .next = context->generic_bindings,
  };
  context->generic_bindings = binding;
  if (out_binding) *out_binding = binding;
  return 1;
}

static const node_t *direct_selected_expression(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  const node_t *selected = syntax;
  while (selected && selected->kind == ND_GENERIC_SELECTION) {
    const node_generic_selection_t *selection =
        (const node_generic_selection_t *)selected;
    direct_generic_binding_t *binding = NULL;
    if (!resolve_direct_generic_selection(
            context, selection, &binding) || !binding ||
        binding->selected_index < 0 ||
        binding->selected_index >= selection->association_count)
      return NULL;
    selected = selection->associations[
        binding->selected_index].expression;
  }
  return selected;
}

static psx_semantic_node_t *build_direct_expression(
    direct_resolution_context_t *context,
    const node_t *syntax);
static psx_semantic_node_t *build_direct_expression_impl(
    direct_resolution_context_t *context,
    const node_t *syntax);
static psx_semantic_node_t *build_direct_binary_expression(
    direct_resolution_context_t *context,
    const node_t *syntax);
static psx_semantic_node_t *apply_direct_expression_decay(
    direct_resolution_context_t *context,
    const node_t *syntax,
    psx_semantic_node_t *expression);
static psx_semantic_node_t *build_direct_statement(
    direct_resolution_context_t *context,
    const node_t *syntax);
static psx_semantic_node_t *build_direct_block_excluding(
    direct_resolution_context_t *context,
    const node_block_t *block, const node_t *excluded);

static void set_failure(
    psx_resolved_hir_build_failure_t *failure,
    psx_resolved_hir_build_status_t status,
    const node_t *source) {
  if (!failure) return;
  failure->status = status;
  failure->source_node_kind = source ? source->kind : -1;
  failure->source_token = source ? source->tok : NULL;
}

static int note_direct_rejection(
    direct_resolution_context_t *context, const node_t *source) {
  if (context && context->failure &&
      context->failure->status == PSX_RESOLVED_HIR_BUILD_OK &&
      context->failure->source_node_kind < 0) {
    context->failure->source_node_kind =
        source ? source->kind : PSX_SYNTAX_NODE_INVALID;
    context->failure->source_token = source ? source->tok : NULL;
  }
  return 0;
}

static int direct_is_predefined_function_name(
    const direct_resolution_context_t *context,
    const node_identifier_t *identifier) {
  static const char name[] = "__func__";
  return context && context->function_name && identifier &&
         identifier->name_len == (int)(sizeof(name) - 1) &&
         memcmp(
             identifier->name, name,
             sizeof(name) - 1) == 0;
}

static int resolve_direct_predefined_function_name_type(
    direct_resolution_context_t *context,
    psx_qual_type_t *qual_type) {
  if (qual_type)
    *qual_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!context || !context->function_name) return 0;
  psx_literal_semantic_resolution_t resolution;
  if (!psx_resolve_string_literal_value_in_contexts(
          context->semantic_context, NULL,
          &(psx_string_literal_value_t){
              .contents = context->function_name,
              .length = context->function_name_len,
              .character_width = TK_CHAR_WIDTH_CHAR,
              .prefix_kind = TK_STR_PREFIX_NONE,
          },
          &resolution))
    return 0;
  if (qual_type) *qual_type = resolution.qual_type;
  return 1;
}

static int note_direct_named_rejection(
    direct_resolution_context_t *context,
    psx_syntax_typed_hir_rejection_t rejection,
    const node_t *source, const char *name, int name_length) {
  note_direct_rejection(context, source);
  if (context && context->failure) {
    context->failure->rejection = rejection;
    context->failure->source_name = name;
    context->failure->source_name_length = name_length;
  }
  return 0;
}

static int note_direct_semantic_rejection(
    direct_resolution_context_t *context,
    psx_syntax_typed_hir_rejection_t rejection,
    const node_t *source) {
  note_direct_rejection(context, source);
  if (context && context->failure)
    context->failure->rejection = rejection;
  return 0;
}

static int note_direct_integer_rejection(
    direct_resolution_context_t *context,
    psx_syntax_typed_hir_rejection_t rejection,
    const node_t *source, long long value) {
  note_direct_semantic_rejection(context, rejection, source);
  if (context && context->failure)
    context->failure->source_integer_value = value;
  return 0;
}

static psx_typed_hir_tree_t *wrap_typed_root(
    psx_semantic_context_t *semantic_context,
    psx_semantic_node_t *root,
    const node_t *source,
    psx_resolved_hir_build_failure_t *failure) {
  if (!semantic_context || !root) return NULL;
  psx_typed_hir_tree_t *tree = arena_alloc_in(
      ps_ctx_arena(semantic_context), sizeof(*tree));
  if (!tree) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY, source);
    return NULL;
  }
  tree->root = root;
  return tree;
}

static int resolve_direct_identifier_with_usage(
    direct_resolution_context_t *context,
    const node_identifier_t *identifier,
    int is_call,
    psx_identifier_expression_resolution_t *resolution) {
  if (!context || !identifier) return 0;
  for (direct_identifier_binding_t *binding =
           context->identifier_bindings;
       binding; binding = binding->next) {
    if (binding->syntax != identifier) continue;
    if (context->unevaluated_depth == 0)
      binding->usage_flags |= DIRECT_IDENTIFIER_USAGE_EVALUATED;
    if (resolution) *resolution = binding->resolution;
    return 1;
  }
  psx_identifier_expression_resolution_t resolved;
  int has_lookup_point = context->identifier_lookup_point ||
                         identifier->base.tok ||
                         identifier->scope_seq != 0 ||
                         identifier->declaration_seq != 0;
  psx_resolve_identifier_expression(
      &(psx_identifier_resolution_request_t){
          .semantic_context = context->semantic_context,
          .global_registry = context->global_registry,
          .local_registry = context->local_registry,
          .name = identifier->name,
          .name_len = identifier->name_len,
          .is_call = is_call,
          .has_local_lookup_point = has_lookup_point,
          .local_lookup_point = {
              .scope_seq = context->identifier_lookup_point
                               ? context->identifier_lookup_point->scope_seq
                               : identifier->scope_seq,
              .declaration_seq = context->identifier_lookup_point
                                     ? context->identifier_lookup_point
                                           ->declaration_seq
                                     : identifier->declaration_seq,
          },
      },
      &resolved);
  switch (resolved.symbol.kind) {
    case PSX_IDENTIFIER_ENUM_CONSTANT:
    case PSX_IDENTIFIER_LOCAL:
    case PSX_IDENTIFIER_GLOBAL_OBJECT:
    case PSX_IDENTIFIER_FUNCTION:
    case PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA:
      break;
    case PSX_IDENTIFIER_UNDECLARED_CALL:
      if (is_call) break;
      return 0;
    case PSX_IDENTIFIER_UNDEFINED:
      return note_direct_named_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_UNDEFINED_IDENTIFIER,
          &identifier->base, identifier->name, identifier->name_len);
  }
  if (resolved.expression_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;
  if (resolved.local_has_static_storage &&
      !resolved.static_storage_global)
    return 0;
  direct_identifier_binding_t *binding = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*binding));
  if (!binding) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &identifier->base);
    return 0;
  }
  *binding = (direct_identifier_binding_t){
      .syntax = identifier,
      .resolution = resolved,
      .usage_flags = context->unevaluated_depth == 0
                         ? DIRECT_IDENTIFIER_USAGE_EVALUATED : 0,
      .next = context->identifier_bindings,
  };
  context->identifier_bindings = binding;
  if (resolution) *resolution = resolved;
  return 1;
}

static int resolve_direct_identifier(
    direct_resolution_context_t *context,
    const node_identifier_t *identifier,
    psx_identifier_expression_resolution_t *resolution) {
  return resolve_direct_identifier_with_usage(
      context, identifier, 0, resolution);
}

static int resolve_direct_call_identifier(
    direct_resolution_context_t *context,
    const node_identifier_t *identifier,
    psx_identifier_expression_resolution_t *resolution) {
  return resolve_direct_identifier_with_usage(
      context, identifier, 1, resolution);
}

static direct_type_query_binding_t *find_direct_type_query_binding(
    direct_resolution_context_t *context, const node_t *syntax) {
  for (direct_type_query_binding_t *binding =
           context ? context->type_query_bindings : NULL;
       binding; binding = binding->next) {
    if (binding->syntax == syntax) return binding;
  }
  return NULL;
}

static const psx_runtime_array_bound_t *direct_bound_for_op(
    const psx_runtime_declarator_application_t *application,
    int op_index) {
  for (int i = 0;
       application && i < application->array_bound_count; i++) {
    if (application->array_bounds[i].declarator_op_index == op_index)
      return &application->array_bounds[i];
  }
  return NULL;
}

static const node_t *direct_bound_syntax_for_op(
    const psx_parsed_declarator_t *declarator, int op_index) {
  for (int i = 0; declarator && i < declarator->array_bound_count; i++) {
    if (declarator->array_bounds[i].declarator_op_index == op_index)
      return declarator->array_bounds[i].expression.node;
  }
  return NULL;
}

static const psx_type_t *direct_type_before_application(
    const psx_type_t *type,
    const psx_runtime_declarator_application_t *application) {
  const psx_type_t *current = type;
  for (int i = 0; current && application &&
                  i < application->shape.count; i++) {
    psx_type_kind_t expected_kind;
    switch (application->shape.ops[i].kind) {
      case PSX_DECL_OP_POINTER:
        expected_kind = PSX_TYPE_POINTER;
        break;
      case PSX_DECL_OP_ARRAY:
        expected_kind = PSX_TYPE_ARRAY;
        break;
      case PSX_DECL_OP_FUNCTION:
        expected_kind = PSX_TYPE_FUNCTION;
        break;
      default:
        return NULL;
    }
    if (current->kind != expected_kind) return NULL;
    current = current->base;
  }
  return current;
}

static int resolve_direct_sizeof_type_name(
    direct_resolution_context_t *context,
    const node_sizeof_query_t *query,
    direct_type_query_binding_t *binding) {
  if (!context || !query || !query->type_name.syntax || !binding)
    return 0;
  psx_type_name_resolution_state_t base_state = {0};
  if (!psx_bind_type_name_ref_in_contexts(
          context->semantic_context, context->global_registry,
          context->local_registry, &query->type_name,
          &base_state))
    return 0;
  const psx_type_t *base_type =
      psx_type_name_bound_base_type(&base_state);
  const psx_runtime_declarator_application_t *runtime_application =
      psx_type_name_bound_runtime_application(&base_state);
  if (!base_type) return 0;

  const psx_parsed_declarator_t *declarator =
      &query->type_name.syntax->declarator;
  psx_runtime_declarator_application_t application;
  if (!resolve_direct_declarator_application(
          context, declarator, &application))
    return 0;
  psx_runtime_declarator_application_t effective_application;
  if (!psx_compose_runtime_declarator_applications_in(
          ps_ctx_arena(context->semantic_context), &application,
          runtime_application,
          &effective_application))
    return 0;
  const psx_type_t *resolved_type =
      psx_apply_runtime_declarator_type_in_context(
          context->semantic_context, base_type,
          &application);
  psx_qual_type_t queried_qual_type = ps_ctx_intern_qual_type_in(
      context->semantic_context, resolved_type);
  if (queried_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;

  int maximum_factors = effective_application.array_bound_count;
  if (maximum_factors > 0) {
    binding->runtime_factor_ids = arena_alloc_in(
        ps_ctx_arena(context->semantic_context),
        (size_t)maximum_factors *
            sizeof(*binding->runtime_factor_ids));
    if (!binding->runtime_factor_ids) {
      context->preflight_failed = 1;
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          &query->base);
      return 0;
    }
  }

  const psx_type_t *factor_base_type = direct_type_before_application(
      base_type, runtime_application);
  if (!factor_base_type) return 0;
  psx_qual_type_t factor_base_qual_type = ps_ctx_intern_qual_type_in(
      context->semantic_context, factor_base_type);
  if (factor_base_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;
  long long factor = ps_type_sizeof_id(
      ps_ctx_semantic_type_table_in(context->semantic_context),
      ps_ctx_record_layout_table_in(context->semantic_context),
      factor_base_qual_type.type_id,
      ps_ctx_data_layout(context->semantic_context));
  if (factor_base_type->kind == PSX_TYPE_VOID) factor = 1;
  for (int i = effective_application.shape.count - 1;
       i >= 0; i--) {
    const psx_declarator_op_t *op =
        &effective_application.shape.ops[i];
    if (op->kind == PSX_DECL_OP_POINTER) {
      factor = ag_data_layout_pointer_size(
          ps_ctx_data_layout(context->semantic_context));
      binding->runtime_factor_count = 0;
      continue;
    }
    if (op->kind != PSX_DECL_OP_ARRAY) continue;
    const psx_runtime_array_bound_t *bound =
        direct_bound_for_op(&effective_application, i);
    if (op->is_vla_array && bound && !bound->is_constant) {
      if (bound->expression_id == PSX_SEMANTIC_EXPR_ID_INVALID ||
          !ps_ctx_semantic_expression_in(
              context->semantic_context, bound->expression_id))
        return 0;
      binding->runtime_factor_ids[
          binding->runtime_factor_count++] =
              bound->expression_id;
      continue;
    }
    if (op->array_len <= 0 || factor <= 0 ||
        factor > LLONG_MAX / op->array_len)
      return 0;
    factor *= op->array_len;
  }
  if (binding->runtime_factor_count > 0) {
    return psx_resolve_sizeof_runtime_product_plan_in(
        context->semantic_context, queried_qual_type, factor,
        binding->runtime_factor_count, &binding->plan);
  }
  return psx_resolve_sizeof_qual_type_plan_in(
      context->semantic_context, queried_qual_type, 0, 0,
      &binding->plan);
}

static int resolve_direct_sizeof_vla_subscript(
    direct_resolution_context_t *context,
    const node_sizeof_query_t *query,
    direct_type_query_binding_t *binding) {
  if (!context || !query || !query->operand || !binding ||
      query->operand->kind != ND_SUBSCRIPT)
    return 0;
  const node_t *base = query->operand;
  int depth = 0;
  while (base && base->kind == ND_SUBSCRIPT) {
    depth++;
    base = base->lhs;
  }
  if (!base || base->kind != ND_IDENTIFIER || depth <= 0)
    return 0;

  psx_identifier_expression_resolution_t identifier;
  context->unevaluated_depth++;
  int identifier_resolved = resolve_direct_identifier(
      context, (const node_identifier_t *)base, &identifier);
  context->unevaluated_depth--;
  if (!identifier_resolved || !identifier.local_is_vla ||
      identifier.symbol.kind != PSX_IDENTIFIER_LOCAL ||
      !identifier.symbol.local)
    return 0;

  binding->evaluated_prefixes = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      (size_t)depth * sizeof(*binding->evaluated_prefixes));
  if (!binding->evaluated_prefixes) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &query->base);
    return 0;
  }
  const node_t *subscript = query->operand;
  for (int i = depth - 1; i >= 0; i--) {
    binding->evaluated_prefixes[i] = subscript->rhs;
    subscript = subscript->lhs;
  }
  binding->evaluated_prefix_count = depth;
  for (int i = 0; i < depth; i++) {
    psx_qual_type_t index_qual_type;
    if (!binding->evaluated_prefixes[i] ||
        !preflight_direct_expression(
            context, binding->evaluated_prefixes[i],
            &index_qual_type))
      return 0;
    const psx_type_t *index_type = ps_ctx_type_by_id_in(
        context->semantic_context, index_qual_type.type_id);
    if (!index_type ||
        (index_type->kind != PSX_TYPE_BOOL &&
         index_type->kind != PSX_TYPE_INTEGER))
      return 0;
  }

  const psx_type_t *queried_type = ps_ctx_type_by_id_in(
      context->semantic_context,
      identifier.declaration_qual_type.type_id);
  for (int i = 0; queried_type && i < depth; i++)
    queried_type = ps_type_subscript_result_in(
        ps_ctx_arena(context->semantic_context), queried_type);
  psx_qual_type_t queried_qual_type = ps_ctx_intern_qual_type_in(
      context->semantic_context, queried_type);
  if (queried_qual_type.type_id == PSX_TYPE_ID_INVALID) return 0;

  lvar_t *local = identifier.symbol.local;
  int row_slot = ps_lvar_vla_row_stride_frame_off(local);
  int remaining = ps_lvar_vla_strides_remaining(local);
  if (row_slot == 0 ||
      (depth != 1 && depth - 1 > remaining))
    return 0;
  return psx_resolve_sizeof_runtime_slot_plan_in(
      context->semantic_context, queried_qual_type,
      row_slot + PSX_VLA_RUNTIME_SLOT_SIZE * (depth - 1),
      &binding->plan);
}

static int resolve_direct_type_query(
    direct_resolution_context_t *context, const node_t *syntax,
    direct_type_query_binding_t **out_binding) {
  if (out_binding) *out_binding = NULL;
  if (!context || !syntax ||
      (syntax->kind != ND_SIZEOF_QUERY &&
       syntax->kind != ND_ALIGNOF_QUERY))
    return 0;
  direct_type_query_binding_t *existing =
      find_direct_type_query_binding(context, syntax);
  if (existing) {
    if (out_binding) *out_binding = existing;
    return 1;
  }
  direct_type_query_binding_t *binding = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*binding));
  if (!binding) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        syntax);
    return 0;
  }
  memset(binding, 0, sizeof(*binding));
  binding->syntax = syntax;

  int resolved = 0;
  if (syntax->kind == ND_ALIGNOF_QUERY) {
    const node_alignof_query_t *query =
        (const node_alignof_query_t *)syntax;
    psx_qual_type_t queried_qual_type;
    resolved = psx_resolve_type_name_qual_type_in_contexts(
                   context->semantic_context,
                   context->global_registry,
                   context->local_registry, &query->type_name,
                   &queried_qual_type) &&
               psx_resolve_alignof_qual_type_plan_in(
                   context->semantic_context, queried_qual_type,
                   &binding->plan);
  } else {
    const node_sizeof_query_t *query =
        (const node_sizeof_query_t *)syntax;
    if (query->is_type_name) {
      resolved = resolve_direct_sizeof_type_name(
          context, query, binding);
    } else if (query->operand) {
      psx_qual_type_t queried_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
      if (query->operand->kind == ND_SUBSCRIPT &&
          direct_syntax_has_vla_binding(context, query->operand)) {
        resolved = resolve_direct_sizeof_vla_subscript(
            context, query, binding);
        if (!resolved &&
            !preflight_direct_sizeof_operand(
                context, query->operand, &queried_qual_type))
          return 0;
      } else if (
          query->operand->kind == ND_IDENTIFIER &&
          direct_is_predefined_function_name(
              context,
              (const node_identifier_t *)query->operand)) {
        if (!resolve_direct_predefined_function_name_type(
                context, &queried_qual_type))
          return 0;
        resolved = psx_resolve_sizeof_qual_type_plan_in(
            context->semantic_context, queried_qual_type, 1,
            (long long)context->function_name_len + 1,
            &binding->plan);
      } else if (query->operand->kind == ND_IDENTIFIER) {
        psx_identifier_expression_resolution_t identifier;
        context->unevaluated_depth++;
        int identifier_resolved = resolve_direct_identifier(
            context, (const node_identifier_t *)query->operand,
            &identifier);
        context->unevaluated_depth--;
        if (!identifier_resolved) return 0;
        queried_qual_type = identifier.declaration_qual_type.type_id !=
                                    PSX_TYPE_ID_INVALID
                                ? identifier.declaration_qual_type
                                : identifier.expression_qual_type;
        if (identifier.local_is_vla_object &&
            identifier.symbol.local) {
          resolved = psx_resolve_sizeof_runtime_slot_plan_in(
              context->semantic_context, queried_qual_type,
              ps_lvar_offset(identifier.symbol.local) +
                  PSX_VLA_RUNTIME_SIZE_RELATIVE_OFFSET,
              &binding->plan);
        }
      } else if (!preflight_direct_sizeof_operand(
                     context, query->operand,
                     &queried_qual_type)) {
        return 0;
      }
      if (!resolved && query->operand->kind == ND_STRING) {
        const node_string_t *string =
            (const node_string_t *)query->operand;
        int width = string->char_width ? (int)string->char_width : 1;
        resolved = psx_resolve_sizeof_qual_type_plan_in(
            context->semantic_context, queried_qual_type, 1,
            (long long)(string->byte_len + 1) * width,
            &binding->plan);
      } else if (!resolved) {
        resolved = psx_resolve_sizeof_qual_type_plan_in(
            context->semantic_context, queried_qual_type, 0, 0,
            &binding->plan);
      }
    }
  }
  if (!resolved) return 0;
  binding->next = context->type_query_bindings;
  context->type_query_bindings = binding;
  if (out_binding) *out_binding = binding;
  return 1;
}

static direct_identifier_binding_t *direct_identifier_binding(
    direct_resolution_context_t *context,
    const node_identifier_t *identifier) {
  if (!context || !identifier) return NULL;
  for (direct_identifier_binding_t *binding =
           context->identifier_bindings;
       binding; binding = binding->next) {
    if (binding->syntax == identifier) return binding;
  }
  return NULL;
}

static int direct_syntax_has_vla_binding(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (!context || !syntax) return 0;
  if (syntax->kind == ND_IDENTIFIER) {
    psx_identifier_expression_resolution_t resolution;
    return resolve_direct_identifier(
               context, (const node_identifier_t *)syntax,
               &resolution) &&
           resolution.local_is_vla;
  }
  return direct_syntax_has_vla_binding(context, syntax->lhs) ||
         direct_syntax_has_vla_binding(context, syntax->rhs);
}

static direct_vla_runtime_view_t direct_vla_runtime_view(
    direct_resolution_context_t *context, const node_t *syntax) {
  direct_vla_runtime_view_t empty = {0};
  if (!context || !syntax) return empty;

  if (syntax->kind == ND_IDENTIFIER) {
    psx_identifier_expression_resolution_t resolution;
    if (!resolve_direct_identifier(
            context, (const node_identifier_t *)syntax,
            &resolution) ||
        !resolution.local_is_vla ||
        resolution.symbol.kind != PSX_IDENTIFIER_LOCAL ||
        !resolution.symbol.local)
      return empty;
    return (direct_vla_runtime_view_t){
        .stride_frame_offset = ps_lvar_vla_row_stride_frame_off(
            resolution.symbol.local),
        .strides_remaining = ps_lvar_vla_strides_remaining(
            resolution.symbol.local),
    };
  }

  direct_vla_runtime_view_t view = {0};
  switch (syntax->kind) {
    case ND_ADD:
      view = direct_vla_runtime_view(context, syntax->lhs);
      return view.stride_frame_offset != 0
                 ? view
                 : direct_vla_runtime_view(context, syntax->rhs);
    case ND_SUB:
    case ND_ASSIGN:
    case ND_COMPOUND_ASSIGN:
    case ND_ADDRESS_OF:
    case ND_SOURCE_CAST:
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC:
      return direct_vla_runtime_view(context, syntax->lhs);
    case ND_COMMA:
    case ND_TERNARY:
      return direct_vla_runtime_view(context, syntax->rhs);
    case ND_SUBSCRIPT:
      view = direct_vla_runtime_view(context, syntax->lhs);
      if (view.stride_frame_offset == 0)
        view = direct_vla_runtime_view(context, syntax->rhs);
      break;
    case ND_UNARY_DEREF:
      view = direct_vla_runtime_view(context, syntax->lhs);
      break;
    default:
      return empty;
  }

  if (view.stride_frame_offset == 0 ||
      view.strides_remaining <= 0)
    return empty;
  view.stride_frame_offset += PSX_VLA_RUNTIME_SLOT_SIZE;
  view.strides_remaining--;
  return view;
}

static void apply_direct_vla_runtime_view(
    direct_resolution_context_t *context, const node_t *syntax,
    psx_qual_type_t result_qual_type,
    psx_hir_node_spec_t *spec) {
  if (!context || !syntax || !spec ||
      result_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return;
  const psx_type_t *result_type = ps_ctx_type_by_id_in(
      context->semantic_context, result_qual_type.type_id);
  if (!result_type || !ps_type_contains_vla_array(result_type))
    return;
  direct_vla_runtime_view_t view =
      direct_vla_runtime_view(context, syntax);
  if (view.stride_frame_offset == 0) return;
  spec->vla_stride_frame_offset = view.stride_frame_offset;
  spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
}

static int resolve_direct_member_access(
    direct_resolution_context_t *context,
    const node_member_access_t *access,
    int require_lvalue_object,
    psx_hir_member_resolution_t *resolution) {
  if (!context || !access || !access->base.lhs || !resolution)
    return 0;
  psx_qual_type_t base_type;
  int base_resolved =
      !access->from_pointer && require_lvalue_object
          ? preflight_direct_lvalue(
                context, access->base.lhs, &base_type)
          : preflight_direct_expression(
                context, access->base.lhs, &base_type);
  if (!base_resolved) return 0;
  if (psx_resolve_member_hir_node_spec_in(
      context->semantic_context, base_type,
      access->member_name, access->member_name_len,
      access->from_pointer, resolution))
    return 1;
  if (resolution->member.status == PSX_MEMBER_ACCESS_INVALID_BASE)
    return note_direct_semantic_rejection(
        context,
        access->from_pointer
            ? PSX_SYNTAX_TYPED_HIR_REJECTION_ARROW_BASE_NOT_AGGREGATE_POINTER
            : PSX_SYNTAX_TYPED_HIR_REJECTION_DOT_BASE_NOT_AGGREGATE,
        &access->base);
  if (resolution->member.status == PSX_MEMBER_ACCESS_NOT_FOUND)
    return note_direct_named_rejection(
        context, PSX_SYNTAX_TYPED_HIR_REJECTION_MEMBER_NOT_FOUND,
        &access->base, access->member_name, access->member_name_len);
  return 0;
}

static int resolve_direct_deref_operand(
    direct_resolution_context_t *context,
    const node_t *syntax, psx_qual_type_t *result_type) {
  if (!context || !syntax || syntax->kind != ND_UNARY_DEREF ||
      !syntax->lhs)
    return 0;
  psx_qual_type_t operand_type;
  if (!preflight_direct_expression(
          context, syntax->lhs, &operand_type))
    return 0;
  psx_deref_operand_status_t status =
      psx_resolve_deref_operand_qual_type_in(
          context->semantic_context, operand_type);
  if (status == PSX_DEREF_OPERAND_NOT_POINTER)
    return note_direct_semantic_rejection(
        context,
        PSX_SYNTAX_TYPED_HIR_REJECTION_DEREF_REQUIRES_POINTER,
        syntax);
  if (status == PSX_DEREF_OPERAND_VOID_POINTER)
    return note_direct_semantic_rejection(
        context,
        PSX_SYNTAX_TYPED_HIR_REJECTION_DEREF_VOID_POINTER,
        syntax);
  psx_qual_type_t result =
      psx_resolve_indirection_result_qual_type_in(
          context->semantic_context, operand_type);
  if (result.type_id == PSX_TYPE_ID_INVALID) return 0;
  if (result_type) *result_type = result;
  return 1;
}

static int preflight_direct_lvalue(
    direct_resolution_context_t *context,
    const node_t *syntax, psx_qual_type_t *qual_type) {
  if (qual_type)
    *qual_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!context || !syntax) return 0;
  if (syntax->kind == ND_GENERIC_SELECTION) {
    const node_t *selected = direct_selected_expression(
        context, syntax);
    return selected && preflight_direct_lvalue(
                           context, selected, qual_type);
  }
  if (syntax->kind == ND_IDENTIFIER) {
    psx_identifier_expression_resolution_t resolution;
    if (!resolve_direct_identifier(
            context, (const node_identifier_t *)syntax,
            &resolution) ||
        resolution.symbol.kind == PSX_IDENTIFIER_ENUM_CONSTANT)
      return 0;
    if (qual_type) *qual_type = resolution.declaration_qual_type;
    return 1;
  }
  if (syntax->kind == ND_UNARY_DEREF) {
    return resolve_direct_deref_operand(
        context, syntax, qual_type);
  }
  if (syntax->kind == ND_SUBSCRIPT) {
    return preflight_direct_expression(
        context, syntax, qual_type);
  }
  if (syntax->kind == ND_MEMBER_ACCESS) {
    psx_hir_member_resolution_t resolution;
    if (!resolve_direct_member_access(
            context, (const node_member_access_t *)syntax,
            1,
            &resolution))
      return 0;
    if (qual_type)
      *qual_type = resolution.member.member_qual_type;
    return 1;
  }
  if (syntax->kind == ND_COMPOUND_LITERAL) {
    direct_compound_literal_binding_t *binding = NULL;
    if (!resolve_direct_compound_literal(
            context, (const node_compound_literal_t *)syntax,
            &binding) || !binding)
      return 0;
    if (qual_type) *qual_type = binding->plan.object_qual_type;
    return 1;
  }
  if (syntax->kind == ND_SOURCE_CAST) {
    psx_qual_type_t cast_type;
    if (!preflight_direct_expression(context, syntax, &cast_type))
      return 0;
    direct_cast_binding_t *binding = find_direct_cast_binding(
        context, (const node_source_cast_t *)syntax);
    if (!binding || !binding->type_resolution.target_is_aggregate)
      return 0;
    if (qual_type) *qual_type = cast_type;
    return 1;
  }
  return 0;
}

static int direct_null_pointer_constant(
    direct_resolution_context_t *context,
    const node_t *syntax,
    psx_qual_type_t type) {
  if (!context || !syntax || syntax->kind != ND_NUM ||
      ((const node_num_t *)syntax)->val != 0)
    return 0;
  const psx_type_t *canonical = ps_ctx_type_by_id_in(
      context->semantic_context, type.type_id);
  return canonical &&
         (canonical->kind == PSX_TYPE_BOOL ||
          canonical->kind == PSX_TYPE_INTEGER);
}

static void mark_direct_assignment_target(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (!context || !syntax || context->unevaluated_depth != 0)
    return;
  if (syntax->kind == ND_GENERIC_SELECTION) {
    const node_t *selected = direct_selected_expression(
        context, syntax);
    if (selected)
      mark_direct_assignment_target(context, selected);
    return;
  }
  if (syntax->kind == ND_IDENTIFIER) {
    direct_identifier_binding_t *binding =
        direct_identifier_binding(
            context, (const node_identifier_t *)syntax);
    if (binding &&
        binding->resolution.symbol.kind == PSX_IDENTIFIER_LOCAL)
      binding->usage_flags |= DIRECT_IDENTIFIER_USAGE_INITIALIZED;
    return;
  }
  if (syntax->kind == ND_UNARY_DEREF && syntax->lhs &&
      syntax->lhs->kind == ND_ADDRESS_OF)
    mark_direct_assignment_target(context, syntax->lhs->lhs);
  if (syntax->kind == ND_MEMBER_ACCESS) {
    const node_member_access_t *access =
        (const node_member_access_t *)syntax;
    if (!access->from_pointer)
      mark_direct_assignment_target(context, syntax->lhs);
  }
}

static int direct_binary_kind(
    psx_syntax_node_kind_t syntax_kind,
    psx_hir_node_kind_t *hir_kind,
    psx_type_binary_op_t *type_operator) {
#define MAP(kind, hir, type_op)       \
  case kind:                          \
    if (hir_kind) *hir_kind = hir;    \
    if (type_operator)                \
      *type_operator = type_op;       \
    return 1
  if (!hir_kind && !type_operator) return 0;
  switch (syntax_kind) {
    MAP(ND_ADD, PSX_HIR_ADD, PSX_TYPE_BINARY_ADD);
    MAP(ND_SUB, PSX_HIR_SUB, PSX_TYPE_BINARY_SUB);
    MAP(ND_MUL, PSX_HIR_MUL, PSX_TYPE_BINARY_MUL);
    MAP(ND_DIV, PSX_HIR_DIV, PSX_TYPE_BINARY_DIV);
    MAP(ND_MOD, PSX_HIR_MOD, PSX_TYPE_BINARY_MOD);
    MAP(ND_BITAND, PSX_HIR_BITAND, PSX_TYPE_BINARY_BITAND);
    MAP(ND_BITXOR, PSX_HIR_BITXOR, PSX_TYPE_BINARY_BITXOR);
    MAP(ND_BITOR, PSX_HIR_BITOR, PSX_TYPE_BINARY_BITOR);
    MAP(ND_SHL, PSX_HIR_SHL, PSX_TYPE_BINARY_SHL);
    MAP(ND_SHR, PSX_HIR_SHR, PSX_TYPE_BINARY_SHR);
    MAP(ND_EQ, PSX_HIR_EQ, PSX_TYPE_BINARY_COMPARE);
    MAP(ND_NE, PSX_HIR_NE, PSX_TYPE_BINARY_COMPARE);
    MAP(ND_LT, PSX_HIR_LT, PSX_TYPE_BINARY_COMPARE);
    MAP(ND_LE, PSX_HIR_LE, PSX_TYPE_BINARY_COMPARE);
    MAP(ND_GT, PSX_HIR_GT, PSX_TYPE_BINARY_COMPARE);
    MAP(ND_GE, PSX_HIR_GE, PSX_TYPE_BINARY_COMPARE);
    MAP(ND_LOGAND, PSX_HIR_LOGAND, PSX_TYPE_BINARY_LOGICAL);
    MAP(ND_LOGOR, PSX_HIR_LOGOR, PSX_TYPE_BINARY_LOGICAL);
    MAP(ND_COMMA, PSX_HIR_COMMA, PSX_TYPE_BINARY_COMMA);
    default:
      return 0;
  }
#undef MAP
}

static int direct_arithmetic_unary_operator(
    psx_syntax_node_kind_t syntax_kind,
    psx_type_arithmetic_unary_op_t *type_operator) {
  if (!type_operator) return 0;
  switch (syntax_kind) {
    case ND_UNARY_PLUS:
      *type_operator = PSX_TYPE_UNARY_PLUS;
      return 1;
    case ND_UNARY_NEGATE:
      *type_operator = PSX_TYPE_UNARY_NEGATE;
      return 1;
    case ND_CREAL:
      *type_operator = PSX_TYPE_UNARY_REAL;
      return 1;
    case ND_CIMAG:
      *type_operator = PSX_TYPE_UNARY_IMAGINARY;
      return 1;
    default:
      return 0;
  }
}

static int direct_incdec_kind(
    psx_syntax_node_kind_t syntax_kind,
    psx_hir_node_kind_t *hir_kind) {
#define MAP(kind, hir) case kind: *hir_kind = hir; return 1
  if (!hir_kind) return 0;
  switch (syntax_kind) {
    MAP(ND_PRE_INC, PSX_HIR_PRE_INC);
    MAP(ND_PRE_DEC, PSX_HIR_PRE_DEC);
    MAP(ND_POST_INC, PSX_HIR_POST_INC);
    MAP(ND_POST_DEC, PSX_HIR_POST_DEC);
    default:
      return 0;
  }
#undef MAP
}

static int direct_compound_assignment_operator(
    token_kind_t source_operator,
    psx_compound_assignment_operator_t *semantic_operator,
    psx_hir_compound_operator_t *hir_operator) {
#define MAP(token, semantic, hir)              \
  case token:                                  \
    if (semantic_operator)                     \
      *semantic_operator = semantic;           \
    if (hir_operator) *hir_operator = hir;     \
    return 1
  switch (source_operator) {
    MAP(TK_PLUSEQ, PSX_COMPOUND_ASSIGN_ADD,
        PSX_HIR_COMPOUND_ADD);
    MAP(TK_MINUSEQ, PSX_COMPOUND_ASSIGN_SUB,
        PSX_HIR_COMPOUND_SUB);
    MAP(TK_MULEQ, PSX_COMPOUND_ASSIGN_MUL,
        PSX_HIR_COMPOUND_MUL);
    MAP(TK_DIVEQ, PSX_COMPOUND_ASSIGN_DIV,
        PSX_HIR_COMPOUND_DIV);
    MAP(TK_MODEQ, PSX_COMPOUND_ASSIGN_MOD,
        PSX_HIR_COMPOUND_MOD);
    MAP(TK_SHLEQ, PSX_COMPOUND_ASSIGN_SHL,
        PSX_HIR_COMPOUND_SHL);
    MAP(TK_SHREQ, PSX_COMPOUND_ASSIGN_SHR,
        PSX_HIR_COMPOUND_SHR);
    MAP(TK_ANDEQ, PSX_COMPOUND_ASSIGN_BITAND,
        PSX_HIR_COMPOUND_BITAND);
    MAP(TK_XOREQ, PSX_COMPOUND_ASSIGN_BITXOR,
        PSX_HIR_COMPOUND_BITXOR);
    MAP(TK_OREQ, PSX_COMPOUND_ASSIGN_BITOR,
        PSX_HIR_COMPOUND_BITOR);
    default:
      return 0;
  }
#undef MAP
}

static int resolve_direct_assignment_types(
    direct_resolution_context_t *context,
    const node_t *syntax,
    psx_qual_type_t target_type,
    psx_qual_type_t value_type,
    psx_assignment_types_resolution_t *resolution,
    psx_hir_compound_operator_t *hir_operator) {
  if (!context || !syntax || !resolution ||
      (syntax->kind != ND_ASSIGN &&
       syntax->kind != ND_COMPOUND_ASSIGN))
    return 0;
  *resolution = (psx_assignment_types_resolution_t){
      .status = PSX_ASSIGNMENT_TYPES_INVALID,
      .result_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  if (syntax->kind == ND_ASSIGN) {
    psx_resolve_assignment_qual_types_in(
        context->semantic_context, target_type, value_type,
        direct_null_pointer_constant(
            context, syntax->rhs, value_type),
        resolution);
    return 1;
  }
  if (syntax->kind != ND_COMPOUND_ASSIGN) return 0;
  psx_compound_assignment_operator_t semantic_operator;
  if (!direct_compound_assignment_operator(
          syntax->source_op, &semantic_operator, hir_operator))
    return 0;
  psx_resolve_compound_assignment_qual_types_in(
      context->semantic_context, semantic_operator,
      target_type, value_type, resolution);
  return 1;
}

static int note_direct_assignment_rejection(
    direct_resolution_context_t *context,
    const node_t *syntax, psx_qual_type_t target_type,
    psx_assignment_types_status_t status) {
  psx_syntax_typed_hir_rejection_t rejection;
  switch (status) {
    case PSX_ASSIGNMENT_TARGET_NOT_MODIFIABLE: {
      if ((target_type.qualifiers &
           PSX_TYPE_QUALIFIER_CONST) != 0) {
        rejection =
            PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_CONST_TARGET;
        break;
      }
      const psx_type_t *target = ps_ctx_type_by_id_in(
          context->semantic_context, target_type.type_id);
      rejection =
          target && target->kind == PSX_TYPE_FUNCTION
              ? PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_FUNCTION_TARGET
              : PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_TARGET_NOT_MODIFIABLE;
      break;
    }
    case PSX_ASSIGNMENT_DISCARDS_QUALIFIERS:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_DISCARDS_QUALIFIERS;
      break;
    case PSX_ASSIGNMENT_TYPES_INCOMPATIBLE:
      rejection =
          PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_INCOMPATIBLE_TYPES;
      break;
    default:
      return 0;
  }
  return note_direct_semantic_rejection(
      context, rejection, syntax);
}

static int preflight_direct_expression(
    direct_resolution_context_t *context,
    const node_t *syntax,
    psx_qual_type_t *qual_type) {
  int resolved = preflight_direct_expression_impl(
      context, syntax, qual_type);
  if (resolved && qual_type && context &&
      context->suppress_value_decay_depth == 0) {
    psx_qual_type_t converted =
        psx_resolve_value_decay_qual_type_in(
            context->semantic_context, *qual_type);
    if (converted.type_id == PSX_TYPE_ID_INVALID)
      resolved = 0;
    else
      *qual_type = converted;
  }
  if (!resolved && context && context->failure &&
      context->failure->status == PSX_RESOLVED_HIR_BUILD_OK &&
      context->failure->source_node_kind < 0) {
    context->failure->source_node_kind =
        syntax ? syntax->kind : PSX_SYNTAX_NODE_INVALID;
    context->failure->source_token = syntax ? syntax->tok : NULL;
  }
  return resolved;
}

static int preflight_direct_expression_impl(
    direct_resolution_context_t *context,
    const node_t *syntax,
    psx_qual_type_t *qual_type) {
  if (qual_type)
    *qual_type = (psx_qual_type_t){
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  if (!syntax) return 0;
  if (syntax->kind == ND_NUM) {
    psx_literal_semantic_resolution_t resolution;
    if (!psx_resolve_number_literal_semantics_in_contexts(
            context->semantic_context, NULL,
            (const node_num_t *)syntax, &resolution))
      return 0;
    if (qual_type) *qual_type = resolution.qual_type;
    return 1;
  }
  if (syntax->kind == ND_STRING) {
    psx_literal_semantic_resolution_t resolution;
    if (!psx_resolve_string_literal_semantics_in_contexts(
            context->semantic_context, NULL,
            (const node_string_t *)syntax, &resolution))
      return 0;
    if (qual_type) *qual_type = resolution.qual_type;
    return 1;
  }
  if (syntax->kind == ND_IDENTIFIER) {
    if (direct_is_predefined_function_name(
            context, (const node_identifier_t *)syntax))
      return resolve_direct_predefined_function_name_type(
          context, qual_type);
    psx_identifier_expression_resolution_t resolution;
    if (!resolve_direct_identifier(
            context, (const node_identifier_t *)syntax,
            &resolution))
      return 0;
    if (qual_type) *qual_type = resolution.expression_qual_type;
    return 1;
  }
  if (syntax->kind == ND_COMPOUND_LITERAL) {
    direct_compound_literal_binding_t *binding = NULL;
    if (!resolve_direct_compound_literal(
            context, (const node_compound_literal_t *)syntax,
            &binding) || !binding)
      return 0;
    if (qual_type) *qual_type = binding->plan.object_qual_type;
    return 1;
  }
  if (syntax->kind == ND_SIZEOF_QUERY ||
      syntax->kind == ND_ALIGNOF_QUERY) {
    direct_type_query_binding_t *binding = NULL;
    if (!resolve_direct_type_query(context, syntax, &binding))
      return 0;
    if (qual_type) *qual_type = binding->plan.result_qual_type;
    return 1;
  }
  if (syntax->kind == ND_GENERIC_SELECTION) {
    direct_generic_binding_t *binding = NULL;
    if (!resolve_direct_generic_selection(
            context, (const node_generic_selection_t *)syntax,
            &binding))
      return 0;
    if (qual_type) *qual_type = binding->result_qual_type;
    return 1;
  }
  if (syntax->kind == ND_SOURCE_CAST) {
    psx_qual_type_t operand_type;
    psx_qual_type_t target_type;
    if (!preflight_direct_expression(
            context, syntax->lhs, &operand_type) ||
        !resolve_direct_source_cast(
            context, (const node_source_cast_t *)syntax,
            &target_type))
      return 0;
    direct_cast_binding_t *binding = find_direct_cast_binding(
        context, (const node_source_cast_t *)syntax);
    if (!binding) return 0;
    if (!binding->types_resolved) {
      psx_resolve_source_cast_qual_types(
          ps_ctx_semantic_type_table_in(context->semantic_context),
          ps_ctx_record_decl_table_in(context->semantic_context),
          ps_ctx_record_layout_table_in(context->semantic_context),
          context->lowering_context
              ? ps_lowering_data_layout(context->lowering_context)
              : NULL,
          target_type, operand_type, context->options,
          &binding->type_resolution);
      binding->types_resolved = 1;
    }
    if (binding->type_resolution.status !=
        PSX_SOURCE_CAST_TYPES_OK)
      return note_direct_source_cast_rejection(
          context, syntax, &binding->type_resolution);
    if (binding->type_resolution.target_is_aggregate) {
      if (!context->lowering_context || !context->options) return 0;
      if (context->unevaluated_depth == 0 &&
          !binding->aggregate_plan.temporary &&
          binding->type_resolution.aggregate.mode ==
              PSX_AGGREGATE_CAST_INITIALIZE_MEMBER &&
          !psx_plan_aggregate_source_cast_resolution(
              context->lowering_context, context->local_registry,
              &binding->type_resolution.aggregate,
              &binding->aggregate_plan))
        return 0;
    }
    if (qual_type) *qual_type = target_type;
    return 1;
  }
  if (syntax->kind == ND_FUNCALL) {
    const node_function_call_t *call =
        (const node_function_call_t *)syntax;
    if (psx_function_call_builtin_kind(call) ==
        PSX_BUILTIN_CALL_EXPECT) {
      const node_t *value = psx_builtin_expect_value_operand(call);
      if (!value)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_CALL_ARGUMENT_COUNT_MISMATCH,
            syntax);
      return preflight_direct_expression(context, value, qual_type);
    }
    direct_call_binding_t *binding = NULL;
    if (!resolve_direct_function_call(
            context, call,
            &binding))
      return 0;
    if (qual_type)
      *qual_type = binding->resolution.return_qual_type;
    return 1;
  }
  psx_hir_node_kind_t incdec_kind;
  if (direct_incdec_kind(syntax->kind, &incdec_kind)) {
    psx_qual_type_t operand_type;
    if (!preflight_direct_lvalue(
            context, syntax->lhs, &operand_type)) {
      if (context->failure &&
          context->failure->rejection !=
              PSX_SYNTAX_TYPED_HIR_REJECTION_NONE)
        return 0;
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_REQUIRES_LVALUE,
          syntax);
    }
    psx_incdec_operand_resolution_t resolution;
    psx_resolve_incdec_operand_qual_type_in(
        context->semantic_context, operand_type, &resolution);
    if (resolution.status == PSX_INCDEC_OPERAND_CONST)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_CONST_OPERAND,
          syntax);
    if (resolution.status != PSX_INCDEC_OPERAND_OK)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_INCDEC_INVALID_OPERAND_TYPE,
          syntax);
    mark_direct_assignment_target(context, syntax->lhs);
    if (qual_type) *qual_type = resolution.result_qual_type;
    return 1;
  }
  if (syntax->kind == ND_ASSIGN ||
      syntax->kind == ND_COMPOUND_ASSIGN) {
    psx_qual_type_t target_type;
    psx_qual_type_t value_type = {
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
    if (!preflight_direct_lvalue(
            context, syntax->lhs, &target_type)) {
      if (context->failure &&
          context->failure->rejection !=
              PSX_SYNTAX_TYPED_HIR_REJECTION_NONE)
        return 0;
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_ASSIGN_REQUIRES_LVALUE,
          syntax);
    }
    if (!preflight_direct_expression(
            context, syntax->rhs, &value_type))
      return 0;
    psx_assignment_types_resolution_t resolution;
    if (!resolve_direct_assignment_types(
            context, syntax, target_type, value_type,
            &resolution, NULL))
      return 0;
    if (resolution.status != PSX_ASSIGNMENT_TYPES_OK)
      return note_direct_assignment_rejection(
          context, syntax, target_type, resolution.status);
    mark_direct_assignment_target(context, syntax->lhs);
    if (qual_type) *qual_type = resolution.result_qual_type;
    return 1;
  }
  if (syntax->kind == ND_SUBSCRIPT) {
    psx_qual_type_t left_type;
    psx_qual_type_t right_type;
    if (!preflight_direct_expression(
            context, syntax->lhs, &left_type) ||
        !preflight_direct_expression(
            context, syntax->rhs, &right_type))
      return 0;
    psx_subscript_qual_types_resolution_t resolution;
    psx_resolve_subscript_qual_types_in(
        context->semantic_context, left_type, right_type,
        &resolution);
    if (resolution.status != PSX_SUBSCRIPT_OPERANDS_OK)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_INVALID_SUBSCRIPT_OPERANDS,
          syntax);
    if (qual_type) *qual_type = resolution.result_qual_type;
    return 1;
  }
  if (syntax->kind == ND_MEMBER_ACCESS) {
    psx_hir_member_resolution_t resolution;
    if (!resolve_direct_member_access(
            context, (const node_member_access_t *)syntax,
            0,
            &resolution))
      return 0;
    if (qual_type)
      *qual_type = resolution.member.member_qual_type;
    return 1;
  }
  if (syntax->kind == ND_UNARY_DEREF) {
    return resolve_direct_deref_operand(
        context, syntax, qual_type);
  }
  if (syntax->kind == ND_ADDRESS_OF) {
    const node_t *operand_syntax = direct_selected_expression(
        context, syntax->lhs);
    if (!operand_syntax) return 0;
    psx_qual_type_t operand_type = {
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
    psx_address_operand_category_t category =
        PSX_ADDRESS_OPERAND_NOT_ADDRESSABLE;
    if (preflight_direct_lvalue(
            context, operand_syntax, &operand_type)) {
      category = PSX_ADDRESS_OPERAND_OBJECT_LVALUE;
      if (operand_syntax->kind == ND_IDENTIFIER) {
        psx_identifier_expression_resolution_t identifier;
        if (resolve_direct_identifier(
                context,
                (const node_identifier_t *)operand_syntax,
                &identifier) &&
            identifier.symbol.kind == PSX_IDENTIFIER_FUNCTION)
          category = PSX_ADDRESS_OPERAND_FUNCTION_DESIGNATOR;
      }
    } else {
      if ((context->failure &&
           context->failure->rejection !=
               PSX_SYNTAX_TYPED_HIR_REJECTION_NONE) ||
          operand_syntax->kind == ND_COMPOUND_LITERAL)
        return 0;
      if (!preflight_direct_expression(
              context, syntax->lhs, &operand_type))
        return 0;
    }
    int operand_is_bitfield = 0;
    if (operand_syntax->kind == ND_MEMBER_ACCESS) {
      psx_hir_member_resolution_t member;
      if (!resolve_direct_member_access(
              context,
              (const node_member_access_t *)operand_syntax,
              1, &member))
        return 0;
      operand_is_bitfield =
          member.member.declaration.bit_width > 0;
    }
    psx_address_operand_resolution_t resolution;
    psx_resolve_address_operand_qual_type_in(
        context->semantic_context, operand_type, category,
        operand_is_bitfield, &resolution);
    if (resolution.status ==
        PSX_ADDRESS_OPERAND_REQUIRES_ADDRESSABLE_VALUE)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_REQUIRES_ADDRESSABLE_VALUE,
          syntax);
    if (resolution.status == PSX_ADDRESS_OPERAND_IS_BITFIELD)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_ADDRESS_OF_BITFIELD,
          syntax);
    if (resolution.status != PSX_ADDRESS_OPERAND_OK) return 0;
    if (context->unevaluated_depth == 0 &&
        syntax->lhs->kind == ND_IDENTIFIER) {
      direct_identifier_binding_t *binding =
          direct_identifier_binding(
              context,
              (const node_identifier_t *)syntax->lhs);
      if (binding &&
          binding->resolution.symbol.kind == PSX_IDENTIFIER_LOCAL) {
        binding->usage_flags |=
            DIRECT_IDENTIFIER_USAGE_ADDRESS_TAKEN;
      }
    }
    if (qual_type) *qual_type = resolution.result_qual_type;
    return 1;
  }
  if (syntax->kind == ND_UNARY_PLUS ||
      syntax->kind == ND_UNARY_NEGATE ||
      syntax->kind == ND_CREAL ||
      syntax->kind == ND_CIMAG) {
    psx_type_arithmetic_unary_op_t type_operator;
    psx_qual_type_t operand_type;
    if (!direct_arithmetic_unary_operator(
            syntax->kind, &type_operator) ||
        !preflight_direct_expression(
            context, syntax->lhs, &operand_type))
      return 0;
    psx_qual_type_t result =
        psx_resolve_arithmetic_unary_result_qual_type_in(
            context->semantic_context, type_operator,
            operand_type);
    if (result.type_id == PSX_TYPE_ID_INVALID)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_ARITHMETIC_UNARY_REQUIRES_ARITHMETIC,
          syntax);
    if (qual_type) *qual_type = result;
    return 1;
  }
  if (syntax->kind == ND_LOGICAL_NOT) {
    psx_qual_type_t operand_type;
    if (!preflight_direct_expression(
            context, syntax->lhs, &operand_type))
      return 0;
    psx_qual_type_t result =
        psx_resolve_logical_not_result_qual_type_in(
            context->semantic_context, operand_type);
    if (result.type_id == PSX_TYPE_ID_INVALID)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_LOGICAL_NOT_REQUIRES_SCALAR,
          syntax);
    if (qual_type) *qual_type = result;
    return 1;
  }
  if (syntax->kind == ND_BITWISE_NOT) {
    psx_qual_type_t operand_type;
    if (!preflight_direct_expression(
            context, syntax->lhs, &operand_type))
      return 0;
    psx_qual_type_t result =
        psx_resolve_bitwise_not_result_qual_type_in(
            context->semantic_context, operand_type);
    if (result.type_id == PSX_TYPE_ID_INVALID)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_BITWISE_NOT_REQUIRES_INTEGER,
          syntax);
    if (qual_type) *qual_type = result;
    return 1;
  }
  if (syntax->kind == ND_TERNARY) {
    const node_ctrl_t *ternary = (const node_ctrl_t *)syntax;
    psx_qual_type_t condition_type;
    psx_qual_type_t then_type;
    psx_qual_type_t else_type;
    if (!preflight_direct_expression(
            context, syntax->lhs, &condition_type) ||
        !preflight_direct_expression(
            context, syntax->rhs, &then_type) ||
        !preflight_direct_expression(
            context, ternary->els, &else_type))
      return 0;
    psx_conditional_types_resolution_t resolution;
    psx_resolve_conditional_qual_types_in(
        context->semantic_context, condition_type,
        then_type, else_type, &resolution);
    if (resolution.status ==
        PSX_CONDITIONAL_CONDITION_NOT_SCALAR)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_CONDITIONAL_CONDITION_NOT_SCALAR,
          syntax);
    if (resolution.status ==
        PSX_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE)
      return note_direct_semantic_rejection(
          context,
          PSX_SYNTAX_TYPED_HIR_REJECTION_CONDITIONAL_BRANCH_TYPES_INCOMPATIBLE,
          syntax);
    if (resolution.status != PSX_CONDITIONAL_TYPES_OK)
      return 0;
    if (qual_type)
      *qual_type = resolution.result_qual_type;
    return 1;
  }
  psx_hir_node_kind_t hir_kind;
  psx_type_binary_op_t type_operator;
  psx_qual_type_t lhs_type;
  psx_qual_type_t rhs_type;
  if (!direct_binary_kind(
          syntax->kind, &hir_kind, &type_operator) ||
      !preflight_direct_expression(
          context, syntax->lhs, &lhs_type) ||
      !preflight_direct_expression(
          context, syntax->rhs, &rhs_type))
    return 0;
  psx_qual_type_t result = psx_resolve_binary_result_qual_type_in(
      context->semantic_context, type_operator,
      lhs_type, rhs_type);
  if (result.type_id == PSX_TYPE_ID_INVALID) return 0;
  if (qual_type) *qual_type = result;
  return 1;
}

static psx_semantic_node_t *build_direct_string_value(
    direct_resolution_context_t *context,
    const node_t *syntax,
    const psx_string_literal_value_t *value,
    int code_unit_count) {
  if (!context || !syntax || !value || code_unit_count < 0)
    return NULL;
  psx_literal_semantic_resolution_t resolution;
  if (!psx_resolve_string_literal_value_in_contexts(
          context->semantic_context, context->global_registry,
          value, &resolution)) {
    set_failure(
        context->failure,
        PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
    return NULL;
  }
  int character_width = value->character_width;
  if (character_width <= 0) character_width = 1;
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_STRING,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = resolution.string_label,
      .name_length = resolution.string_label
                         ? strlen(resolution.string_label) : 0,
      .literal_contents = value->contents,
      .literal_length = value->length > 0
                            ? (size_t)value->length : 0,
      .object_size = (code_unit_count + 1) * character_width,
      .object_align = character_width,
  };
  return psx_semantic_node_builder_leaf_expression(
      &context->builder, &spec, resolution.qual_type, NULL,
      syntax->kind);
}

static psx_semantic_node_t *build_direct_literal(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (!context || !syntax) return NULL;
  const node_t *literal_syntax = syntax;

  if (literal_syntax->kind == ND_STRING) {
    const node_string_t *string =
        (const node_string_t *)literal_syntax;
    return build_direct_string_value(
        context, syntax,
        &(psx_string_literal_value_t){
            .contents = string->literal_contents,
            .length = string->literal_length,
            .character_width = string->char_width,
            .prefix_kind = string->str_prefix_kind,
        },
        string->byte_len);
  }

  psx_literal_semantic_resolution_t resolution;
  int resolved = psx_resolve_number_literal_semantics_in_contexts(
      context->semantic_context, context->global_registry,
      (const node_num_t *)literal_syntax, &resolution);
  if (!resolved) {
    set_failure(
        context->failure,
        PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
    return NULL;
  }

  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_NUMBER,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .integer_value = ((const node_num_t *)literal_syntax)->val,
      .floating_value = ((const node_num_t *)literal_syntax)->fval,
  };
  return psx_semantic_node_builder_leaf_expression(
      &context->builder, &spec, resolution.qual_type, NULL,
      syntax->kind);
}

static psx_semantic_node_t *build_direct_type_query(
    direct_resolution_context_t *context, const node_t *syntax) {
  direct_type_query_binding_t *binding =
      find_direct_type_query_binding(context, syntax);
  if (!binding) return NULL;
  const psx_type_query_plan_t *plan = &binding->plan;
  psx_semantic_node_t *value = NULL;
  if (plan->kind == PSX_TYPE_QUERY_PLAN_RUNTIME_SLOT) {
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_LOCAL,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .storage_offset = plan->runtime_size_slot,
        .object_offset = plan->runtime_size_slot,
        .object_size = PSX_VLA_RUNTIME_SLOT_SIZE,
        .object_align = PSX_VLA_RUNTIME_SLOT_SIZE,
    };
    value = psx_semantic_node_builder_leaf_expression(
        &context->builder, &spec, plan->result_qual_type, NULL,
        syntax->kind);
  } else {
    psx_hir_node_spec_t number_spec = {
        .kind = PSX_HIR_NUMBER,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .integer_value = plan->constant_factor,
    };
    value = psx_semantic_node_builder_leaf_expression(
          &context->builder, &number_spec,
          plan->result_qual_type, NULL, syntax->kind);
    if (plan->kind == PSX_TYPE_QUERY_PLAN_RUNTIME_PRODUCT) {
      if (binding->runtime_factor_count != plan->runtime_factor_count)
        return NULL;
      for (int i = 0; i < binding->runtime_factor_count; i++) {
        const psx_typed_hir_tree_t *factor_tree =
            ps_ctx_semantic_expression_in(
                context->semantic_context,
                binding->runtime_factor_ids[i]);
        psx_semantic_node_t *factor = factor_tree
            ? (psx_semantic_node_t *)factor_tree->root : NULL;
        if (!factor) return NULL;
        psx_qual_type_t factor_type =
            psx_semantic_node_expression_qual_type(factor);
        if (factor_type.type_id != plan->result_qual_type.type_id ||
            factor_type.qualifiers != plan->result_qual_type.qualifiers) {
          psx_semantic_node_t *cast_children[] = {factor};
          psx_hir_edge_kind_t cast_edges[] = {PSX_HIR_EDGE_LHS};
          psx_hir_node_spec_t cast_spec = {
              .kind = PSX_HIR_CAST,
              .attached_qual_type = {
                  PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
          };
          factor = psx_semantic_node_builder_expression(
              &context->builder, &cast_spec, plan->result_qual_type,
              cast_children, cast_edges, 1, NULL, syntax->kind);
          if (!factor) return NULL;
        }
        psx_semantic_node_t *children[] = {value, factor};
        psx_hir_edge_kind_t edges[] = {
            PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
        psx_hir_node_spec_t multiply_spec = {
            .kind = PSX_HIR_MUL,
            .attached_qual_type = {
                PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        };
        value = psx_semantic_node_builder_expression(
            &context->builder, &multiply_spec,
            plan->result_qual_type, children, edges, 2, NULL,
            syntax->kind);
        if (!value) return NULL;
      }
    } else if (plan->kind != PSX_TYPE_QUERY_PLAN_CONSTANT) {
      return NULL;
    }
  }
  if (!value || binding->evaluated_prefix_count <= 0) return value;

  psx_semantic_node_t *prefix = NULL;
  for (int i = 0; i < binding->evaluated_prefix_count; i++) {
    psx_semantic_node_t *item = build_direct_expression(
        context, binding->evaluated_prefixes[i]);
    if (!item) return NULL;
    if (!prefix) {
      prefix = item;
      continue;
    }
    psx_semantic_node_t *children[] = {prefix, item};
    psx_hir_edge_kind_t edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
    psx_hir_node_spec_t comma_spec = {
        .kind = PSX_HIR_COMMA,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    prefix = psx_semantic_node_builder_expression(
        &context->builder, &comma_spec,
        psx_semantic_node_expression_qual_type(item),
        children, edges, 2, NULL, syntax->kind);
    if (!prefix) return NULL;
  }
  psx_semantic_node_t *children[] = {prefix, value};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  psx_hir_node_spec_t comma_spec = {
      .kind = PSX_HIR_COMMA,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_expression(
      &context->builder, &comma_spec, plan->result_qual_type,
      children, edges, 2, NULL, syntax->kind);
}

static psx_semantic_node_t *build_direct_identifier(
    direct_resolution_context_t *context,
    const node_identifier_t *identifier,
    int suppress_array_decay) {
  if (direct_is_predefined_function_name(context, identifier))
    return build_direct_string_value(
        context, &identifier->base,
        &(psx_string_literal_value_t){
            .contents = context->function_name,
            .length = context->function_name_len,
            .character_width = TK_CHAR_WIDTH_CHAR,
            .prefix_kind = TK_STR_PREFIX_NONE,
        },
        context->function_name_len);
  psx_identifier_expression_resolution_t resolution;
  if (!resolve_direct_identifier(context, identifier, &resolution))
    return NULL;
  if (resolution.symbol.kind == PSX_IDENTIFIER_ENUM_CONSTANT) {
    node_num_t literal = {0};
    literal.base.kind = ND_NUM;
    literal.base.tok = identifier->base.tok;
    literal.val = resolution.symbol.enum_value;
    return build_direct_literal(context, &literal.base);
  }

  if (resolution.symbol.kind ==
      PSX_IDENTIFIER_BUILTIN_VA_ARG_AREA) {
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_VARARG_CURSOR,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    return psx_semantic_node_builder_leaf_expression(
        &context->builder, &spec,
        resolution.expression_qual_type, NULL,
        identifier->base.kind);
  }

  psx_hir_node_spec_t spec = {
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = identifier->name,
      .name_length = identifier->name_len > 0
                         ? (size_t)identifier->name_len : 0,
  };
  if (resolution.symbol.kind == PSX_IDENTIFIER_FUNCTION) {
    spec.kind = PSX_HIR_FUNCTION_REF;
    return psx_semantic_node_builder_leaf_expression(
        &context->builder, &spec,
        resolution.expression_qual_type, NULL,
        identifier->base.kind);
  }

  psx_semantic_node_t *object = NULL;
  global_var_t *storage_global =
      resolution.symbol.kind == PSX_IDENTIFIER_GLOBAL_OBJECT
          ? resolution.symbol.global
          : resolution.static_storage_global;
  if (storage_global) {
    psx_hir_symbol_spec_t symbol;
    if (!psx_resolve_global_hir_symbol_spec_in(
            context->semantic_context, storage_global, &symbol)) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL,
          &identifier->base);
      return NULL;
    }
    spec.kind = PSX_HIR_GLOBAL;
    spec.name = symbol.name;
    spec.name_length = symbol.name_length;
    object = psx_semantic_node_builder_leaf_expression(
        &context->builder, &spec,
        resolution.declaration_qual_type, &symbol,
        identifier->base.kind);
  } else if (resolution.symbol.kind == PSX_IDENTIFIER_LOCAL &&
             resolution.symbol.local) {
    if (!psx_resolve_local_hir_node_spec_in(
            context->semantic_context, resolution.symbol.local,
            ps_lvar_offset(resolution.symbol.local), &spec)) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          &identifier->base);
      return NULL;
    }
    psx_qual_type_t object_type = resolution.local_is_vla_object &&
                                      !suppress_array_decay
                                      ? resolution.expression_qual_type
                                      : resolution.declaration_qual_type;
    object = psx_semantic_node_builder_leaf_expression(
        &context->builder, &spec, object_type, NULL,
        identifier->base.kind);
  } else {
    set_failure(
        context->failure,
        PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL,
        &identifier->base);
    return NULL;
  }
  if (!object || suppress_array_decay ||
      !resolution.decays_array_to_address)
    return object;

  psx_semantic_node_t *children[] = {object};
  psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
  psx_hir_node_spec_t address_spec = {
      .kind = PSX_HIR_ADDRESS,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  apply_direct_vla_runtime_view(
      context, &identifier->base,
      resolution.expression_qual_type, &address_spec);
  return psx_semantic_node_builder_expression(
      &context->builder, &address_spec,
      resolution.expression_qual_type,
      children, edges, 1, NULL,
      identifier->base.kind);
}

static void record_direct_identifier_usage(
    direct_resolution_context_t *context) {
  if (!context || !context->local_registry) return;
  for (direct_identifier_binding_t *binding =
           context->identifier_bindings;
       binding; binding = binding->next) {
    lvar_t *local = binding->resolution.symbol.local;
    if (binding->resolution.symbol.kind != PSX_IDENTIFIER_LOCAL ||
        !local)
      continue;
    if (binding->usage_flags & DIRECT_IDENTIFIER_USAGE_EVALUATED) {
      ps_decl_record_lvar_usage_in_region_in(
          context->local_registry, local,
          PSX_LVAR_USAGE_EVALUATED, NULL);
    }
    if (binding->usage_flags &
        DIRECT_IDENTIFIER_USAGE_ADDRESS_TAKEN) {
      ps_decl_record_lvar_usage_in_region_in(
          context->local_registry, local,
          PSX_LVAR_USAGE_ADDRESS_TAKEN, NULL);
    }
    if (binding->usage_flags &
        DIRECT_IDENTIFIER_USAGE_INITIALIZED) {
      ps_decl_record_lvar_usage_in_region_in(
          context->local_registry, local,
          PSX_LVAR_USAGE_INITIALIZED, NULL);
    }
  }
}

static psx_semantic_node_t *build_direct_local_reference(
    direct_resolution_context_t *context, lvar_t *local,
    psx_qual_type_t qual_type, int relative_offset,
    unsigned char bit_width, unsigned char bit_offset,
    unsigned char bit_is_signed, int source_node_kind) {
  if (!context || !local ||
      qual_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  psx_hir_node_spec_t spec = {
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = ps_lvar_name(local),
      .name_length = ps_lvar_name_len(local) > 0
                         ? (size_t)ps_lvar_name_len(local) : 0,
      .bit_width = bit_width,
      .bit_offset = bit_offset,
      .bit_is_signed = bit_is_signed,
  };
  if (!psx_resolve_local_hir_node_spec_in(
          context->semantic_context, local,
          ps_lvar_offset(local) + relative_offset, &spec))
    return NULL;
  return psx_semantic_node_builder_leaf_expression(
      &context->builder, &spec, qual_type, NULL,
      source_node_kind);
}

static psx_semantic_node_t *build_direct_global_reference(
    direct_resolution_context_t *context, global_var_t *global,
    psx_qual_type_t qual_type, int source_node_kind) {
  if (!context || !global ||
      qual_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  psx_hir_symbol_spec_t symbol;
  if (!psx_resolve_global_hir_symbol_spec_in(
          context->semantic_context, global, &symbol))
    return NULL;
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_GLOBAL,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = symbol.name,
      .name_length = symbol.name_length,
  };
  return psx_semantic_node_builder_leaf_expression(
      &context->builder, &spec, qual_type, &symbol,
      source_node_kind);
}

static int build_direct_aggregate_cast_temporary_parts(
    direct_resolution_context_t *context, const node_t *syntax,
    const direct_cast_binding_t *binding,
    psx_semantic_node_t *operand, psx_qual_type_t target_type,
    psx_semantic_node_t **initialization,
    psx_semantic_node_t **object) {
  if (initialization) *initialization = NULL;
  if (object) *object = NULL;
  if (!context || !syntax || !binding || !operand ||
      !binding->type_resolution.target_is_aggregate ||
      !binding->aggregate_plan.temporary ||
      !initialization || !object)
    return 0;
  const psx_aggregate_source_cast_plan_t *plan =
      &binding->aggregate_plan;
  psx_semantic_node_t *target = build_direct_local_reference(
      context, plan->temporary, plan->member_qual_type,
      plan->member_offset, plan->member_bit_width,
      plan->member_bit_offset, plan->member_bit_is_signed,
      syntax->kind);
  *object = build_direct_local_reference(
      context, plan->temporary, target_type, 0, 0, 0, 0,
      syntax->kind);
  if (!target || !*object) return 0;
  psx_semantic_node_t *children[] = {target, operand};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_ASSIGN,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  *initialization = psx_semantic_node_builder_expression(
      &context->builder, &spec, plan->member_qual_type,
      children, edges, 2, NULL, syntax->kind);
  return *initialization != NULL;
}

static psx_semantic_node_t *build_direct_comma(
    direct_resolution_context_t *context, const node_t *syntax,
    psx_semantic_node_t *lhs, psx_semantic_node_t *rhs,
    psx_qual_type_t result_type) {
  if (!context || !syntax || !lhs || !rhs) return NULL;
  psx_semantic_node_t *children[] = {lhs, rhs};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_COMMA,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  apply_direct_vla_runtime_view(
      context, syntax, result_type, &spec);
  return psx_semantic_node_builder_expression(
      &context->builder, &spec, result_type,
      children, edges, 2, NULL, syntax->kind);
}

static psx_semantic_node_t *build_direct_lvalue(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (!context || !syntax) return NULL;
  if (syntax->kind == ND_GENERIC_SELECTION) {
    const node_t *selected = direct_selected_expression(
        context, syntax);
    return selected ? build_direct_lvalue(context, selected) : NULL;
  }
  if (syntax->kind == ND_IDENTIFIER)
    return build_direct_identifier(
        context, (const node_identifier_t *)syntax, 1);
  if (syntax->kind == ND_UNARY_DEREF)
    return build_direct_expression_impl(context, syntax);
  if (syntax->kind == ND_SUBSCRIPT)
    return build_direct_expression_impl(context, syntax);
  if (syntax->kind == ND_MEMBER_ACCESS)
    return build_direct_expression_impl(context, syntax);
  if (syntax->kind == ND_COMPOUND_LITERAL) {
    direct_compound_literal_binding_t *binding = NULL;
    if (resolve_direct_compound_literal(
            context, (const node_compound_literal_t *)syntax,
            &binding) && binding)
      return build_direct_expression_impl(context, syntax);
  }
  if (syntax->kind == ND_SOURCE_CAST) {
    direct_cast_binding_t *binding = find_direct_cast_binding(
        context, (const node_source_cast_t *)syntax);
    if (binding && binding->type_resolution.target_is_aggregate)
      return build_direct_expression_impl(context, syntax);
  }
  return NULL;
}

static psx_semantic_node_t *build_direct_expression(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  psx_hir_node_kind_t binary_kind;
  psx_semantic_node_t *expression =
      syntax && direct_binary_kind(
                    syntax->kind, &binary_kind, NULL)
          ? build_direct_binary_expression(context, syntax)
          : build_direct_expression_impl(context, syntax);
  return apply_direct_expression_decay(context, syntax, expression);
}

typedef struct direct_binary_build_frame_t {
  const node_t *syntax;
  psx_hir_node_kind_t hir_kind;
  psx_type_binary_op_t type_operator;
  psx_semantic_node_t *lhs;
  psx_semantic_node_t *rhs;
  unsigned char state;
} direct_binary_build_frame_t;

static int push_direct_binary_build_frame(
    direct_resolution_context_t *context,
    direct_binary_build_frame_t **frames,
    size_t *count, size_t *capacity,
    const node_t *syntax) {
  psx_hir_node_kind_t hir_kind;
  psx_type_binary_op_t type_operator;
  if (!context || !frames || !count || !capacity || !syntax ||
      !direct_binary_kind(
          syntax->kind, &hir_kind, &type_operator))
    return 0;
  if (*count == *capacity) {
    size_t next_capacity = *capacity ? *capacity * 2 : 32;
    if (next_capacity < *capacity ||
        next_capacity > (size_t)-1 / sizeof(**frames)) {
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          syntax);
      return 0;
    }
    direct_binary_build_frame_t *next = realloc(
        *frames, next_capacity * sizeof(*next));
    if (!next) {
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          syntax);
      return 0;
    }
    *frames = next;
    *capacity = next_capacity;
  }
  (*frames)[(*count)++] = (direct_binary_build_frame_t){
      .syntax = syntax,
      .hir_kind = hir_kind,
      .type_operator = type_operator,
  };
  return 1;
}

static psx_semantic_node_t *apply_direct_expression_decay(
    direct_resolution_context_t *context,
    const node_t *syntax,
    psx_semantic_node_t *expression) {
  if (!context || !syntax || !expression) return NULL;
  psx_qual_type_t source_type =
      psx_semantic_node_expression_qual_type(expression);
  psx_qual_type_t converted_type =
      psx_resolve_value_decay_qual_type_in(
          context->semantic_context, source_type);
  if (converted_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  if (converted_type.type_id == source_type.type_id &&
      converted_type.qualifiers == source_type.qualifiers)
    return expression;
  psx_semantic_node_t *children[] = {expression};
  psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_ADDRESS,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  apply_direct_vla_runtime_view(
      context, syntax, converted_type, &spec);
  return psx_semantic_node_builder_expression(
      &context->builder, &spec, converted_type,
      children, edges, 1, NULL, syntax->kind);
}

static psx_semantic_node_t *build_direct_binary_node(
    direct_resolution_context_t *context,
    const node_t *syntax,
    psx_hir_node_kind_t hir_kind,
    psx_type_binary_op_t type_operator,
    psx_semantic_node_t *lhs,
    psx_semantic_node_t *rhs) {
  if (!context || !syntax || !lhs || !rhs) return NULL;
  psx_qual_type_t result_qual_type =
      psx_resolve_binary_result_qual_type_in(
          context->semantic_context, type_operator,
          psx_semantic_node_expression_qual_type(lhs),
          psx_semantic_node_expression_qual_type(rhs));
  if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    set_failure(
        context->failure,
        PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
    return NULL;
  }
  psx_semantic_node_t *children[] = {lhs, rhs};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  psx_hir_node_spec_t spec = {
      .kind = hir_kind,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  apply_direct_vla_runtime_view(
      context, syntax, result_qual_type, &spec);
  return psx_semantic_node_builder_expression(
      &context->builder, &spec, result_qual_type,
      children, edges, 2, NULL, syntax->kind);
}

static psx_semantic_node_t *build_direct_binary_expression(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  direct_binary_build_frame_t *frames = NULL;
  size_t count = 0;
  size_t capacity = 0;
  if (!push_direct_binary_build_frame(
          context, &frames, &count, &capacity, syntax))
    return NULL;

  while (count > 0) {
    direct_binary_build_frame_t *frame = &frames[count - 1];
    if (frame->state == 0) {
      frame->state = 1;
      psx_hir_node_kind_t child_kind;
      if (direct_binary_kind(
              frame->syntax->lhs->kind, &child_kind, NULL)) {
        if (!push_direct_binary_build_frame(
                context, &frames, &count, &capacity,
                frame->syntax->lhs))
          break;
        continue;
      }
      frame->lhs = build_direct_expression(
          context, frame->syntax->lhs);
      if (!frame->lhs) break;
    }
    if (frame->state == 1) {
      frame->state = 2;
      psx_hir_node_kind_t child_kind;
      if (direct_binary_kind(
              frame->syntax->rhs->kind, &child_kind, NULL)) {
        if (!push_direct_binary_build_frame(
                context, &frames, &count, &capacity,
                frame->syntax->rhs))
          break;
        continue;
      }
      frame->rhs = build_direct_expression(
          context, frame->syntax->rhs);
      if (!frame->rhs) break;
    }

    const node_t *completed_syntax = frame->syntax;
    psx_semantic_node_t *completed = build_direct_binary_node(
        context, completed_syntax, frame->hir_kind,
        frame->type_operator,
        frame->lhs, frame->rhs);
    if (!completed) break;
    count--;
    if (count == 0) {
      free(frames);
      return completed;
    }
    completed = apply_direct_expression_decay(
        context, completed_syntax, completed);
    if (!completed) break;
    direct_binary_build_frame_t *parent = &frames[count - 1];
    if (parent->state == 1)
      parent->lhs = completed;
    else
      parent->rhs = completed;
  }

  free(frames);
  return NULL;
}

static psx_semantic_node_t *build_direct_expression_impl(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (syntax->kind == ND_NUM)
    return build_direct_literal(context, syntax);
  if (syntax->kind == ND_STRING)
    return build_direct_literal(context, syntax);
  if (syntax->kind == ND_IDENTIFIER)
    return build_direct_identifier(
        context, (const node_identifier_t *)syntax, 0);
  if (syntax->kind == ND_COMPOUND_LITERAL)
    return build_direct_compound_literal(
        context, (const node_compound_literal_t *)syntax);
  if (syntax->kind == ND_SIZEOF_QUERY ||
      syntax->kind == ND_ALIGNOF_QUERY)
    return build_direct_type_query(context, syntax);

  if (syntax->kind == ND_GENERIC_SELECTION) {
    const node_generic_selection_t *selection =
        (const node_generic_selection_t *)syntax;
    direct_generic_binding_t *binding =
        find_direct_generic_binding(context, selection);
    if (!binding || binding->selected_index < 0 ||
        binding->selected_index >= selection->association_count)
      return NULL;
    psx_semantic_node_t *selected = build_direct_expression(
        context,
        selection->associations[binding->selected_index].expression);
    if (!selected ||
        psx_semantic_node_expression_qual_type(selected).type_id !=
            binding->result_qual_type.type_id) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    return selected;
  }

  if (syntax->kind == ND_SOURCE_CAST) {
    psx_semantic_node_t *operand =
        build_direct_expression(context, syntax->lhs);
    psx_qual_type_t target_type;
    if (!operand ||
        !resolve_direct_source_cast(
            context, (const node_source_cast_t *)syntax,
            &target_type))
      return NULL;
    direct_cast_binding_t *binding = find_direct_cast_binding(
        context, (const node_source_cast_t *)syntax);
    if (binding && binding->type_resolution.target_is_aggregate &&
        binding->aggregate_plan.temporary) {
      psx_semantic_node_t *initialization = NULL;
      psx_semantic_node_t *object = NULL;
      if (!build_direct_aggregate_cast_temporary_parts(
              context, syntax, binding, operand, target_type,
              &initialization, &object))
        return NULL;
      return build_direct_comma(
          context, syntax, initialization, object, target_type);
    }
    psx_semantic_node_t *children[] = {operand};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_CAST,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    apply_direct_vla_runtime_view(
        context, syntax, target_type, &spec);
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, target_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_FUNCALL) {
    const node_function_call_t *call =
        (const node_function_call_t *)syntax;
    if (psx_function_call_builtin_kind(call) ==
        PSX_BUILTIN_CALL_EXPECT) {
      const node_t *value = psx_builtin_expect_value_operand(call);
      return value ? build_direct_expression(context, value) : NULL;
    }
    direct_call_binding_t *binding = NULL;
    if (!resolve_direct_function_call(
            context, call, &binding))
      return NULL;
    size_t child_count = (size_t)call->argument_count +
                         (binding->direct_identifier ? 0u : 1u);
    psx_semantic_node_t **children = child_count
        ? arena_alloc_in(
              ps_ctx_arena(context->semantic_context),
              child_count * sizeof(*children))
        : NULL;
    psx_hir_edge_kind_t *edges = child_count
        ? arena_alloc_in(
              ps_ctx_arena(context->semantic_context),
              child_count * sizeof(*edges))
        : NULL;
    if (child_count && (!children || !edges)) {
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          syntax);
      return NULL;
    }
    size_t child_index = 0;
    if (!binding->direct_identifier) {
      children[child_index] =
          build_direct_expression(context, call->callee);
      edges[child_index++] = PSX_HIR_EDGE_CALLEE;
      if (!children[child_index - 1]) return NULL;
    }
    for (int i = 0; i < call->argument_count; i++) {
      children[child_index] =
          build_direct_expression(context, call->arguments[i]);
      edges[child_index++] = PSX_HIR_EDGE_ARGUMENT;
      if (!children[child_index - 1]) return NULL;
    }
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_CALL,
        .attached_qual_type =
            binding->resolution.function_qual_type,
        .is_implicit_call = binding->is_implicit,
    };
    if (binding->direct_identifier) {
      spec.name = binding->direct_identifier->name;
      spec.name_length = binding->direct_identifier->name_len > 0
                             ? (size_t)binding->direct_identifier->name_len
                             : 0;
    }
    return psx_semantic_node_builder_expression(
        &context->builder, &spec,
        binding->resolution.return_qual_type,
        children, edges, child_count, NULL, syntax->kind);
  }

  psx_hir_node_kind_t incdec_kind;
  if (direct_incdec_kind(syntax->kind, &incdec_kind)) {
    psx_semantic_node_t *operand =
        build_direct_lvalue(context, syntax->lhs);
    if (!operand) return NULL;
    psx_qual_type_t result_type =
        psx_resolve_incdec_result_qual_type_in(
            context->semantic_context,
            psx_semantic_node_expression_qual_type(operand));
    if (result_type.type_id == PSX_TYPE_ID_INVALID) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {operand};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t spec = {
        .kind = incdec_kind,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    apply_direct_vla_runtime_view(
        context, syntax, result_type, &spec);
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, result_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_ASSIGN ||
      syntax->kind == ND_COMPOUND_ASSIGN) {
    psx_semantic_node_t *target =
        build_direct_lvalue(context, syntax->lhs);
    psx_semantic_node_t *value =
        build_direct_expression(context, syntax->rhs);
    if (!target || !value) return NULL;
    psx_assignment_types_resolution_t resolution;
    psx_hir_compound_operator_t hir_operator =
        PSX_HIR_COMPOUND_ADD;
    if (!resolve_direct_assignment_types(
            context, syntax,
            psx_semantic_node_expression_qual_type(target),
            psx_semantic_node_expression_qual_type(value),
            &resolution, &hir_operator) ||
        resolution.status != PSX_ASSIGNMENT_TYPES_OK) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {target, value};
    psx_hir_edge_kind_t edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
    psx_hir_node_spec_t spec = {
      .kind = syntax->kind == ND_COMPOUND_ASSIGN
                  ? PSX_HIR_COMPOUND_ASSIGN
                  : PSX_HIR_ASSIGN,
        .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .is_source_assignment = syntax->kind == ND_ASSIGN ? 1 : 0,
    };
    if (syntax->kind == ND_COMPOUND_ASSIGN)
      spec.integer_value = hir_operator;
    apply_direct_vla_runtime_view(
        context, syntax, resolution.result_qual_type, &spec);
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, resolution.result_qual_type,
        children, edges, 2, NULL, syntax->kind);
  }

  if (syntax->kind == ND_SUBSCRIPT) {
    psx_semantic_node_t *left =
        build_direct_expression(context, syntax->lhs);
    psx_semantic_node_t *right =
        build_direct_expression(context, syntax->rhs);
    if (!left || !right) return NULL;
    psx_subscript_qual_types_resolution_t resolution;
    psx_resolve_subscript_qual_types_in(
        context->semantic_context,
        psx_semantic_node_expression_qual_type(left),
        psx_semantic_node_expression_qual_type(right),
        &resolution);
    if (resolution.status != PSX_SUBSCRIPT_OPERANDS_OK) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {
        resolution.swapped ? right : left,
        resolution.swapped ? left : right};
    psx_hir_edge_kind_t edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_SUBSCRIPT,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    apply_direct_vla_runtime_view(
        context, syntax, resolution.result_qual_type, &spec);
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, resolution.result_qual_type,
        children, edges, 2, NULL, syntax->kind);
  }

  if (syntax->kind == ND_MEMBER_ACCESS) {
    const node_member_access_t *access =
        (const node_member_access_t *)syntax;
    psx_semantic_node_t *base =
        build_direct_expression(context, syntax->lhs);
    if (!base) return NULL;
    psx_hir_member_resolution_t resolution;
    if (!psx_resolve_member_hir_node_spec_in(
        context->semantic_context,
        psx_semantic_node_expression_qual_type(base),
        access->member_name, access->member_name_len,
        access->from_pointer, &resolution)) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {base};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    return psx_semantic_node_builder_expression(
        &context->builder, &resolution.node_spec,
        resolution.member.member_qual_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_UNARY_DEREF) {
    psx_semantic_node_t *operand =
        build_direct_expression(context, syntax->lhs);
    if (!operand) return NULL;
    psx_qual_type_t result_qual_type =
        psx_resolve_indirection_result_qual_type_in(
            context->semantic_context,
            psx_semantic_node_expression_qual_type(operand));
    if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {operand};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_DEREF,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    apply_direct_vla_runtime_view(
        context, syntax, result_qual_type, &spec);
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, result_qual_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_ADDRESS_OF) {
    const node_t *operand_syntax = direct_selected_expression(
        context, syntax->lhs);
    if (!operand_syntax) return NULL;
    if (operand_syntax->kind == ND_IDENTIFIER) {
      psx_identifier_expression_resolution_t resolution;
      if (resolve_direct_identifier(
              context,
              (const node_identifier_t *)operand_syntax,
              &resolution) &&
          resolution.symbol.kind == PSX_IDENTIFIER_FUNCTION) {
        return build_direct_expression(context, syntax->lhs);
      }
    }
    if (operand_syntax->kind == ND_COMPOUND_LITERAL) {
      direct_compound_literal_binding_t *binding =
          find_direct_compound_literal_binding(
              context,
              (const node_compound_literal_t *)operand_syntax);
      if (binding)
        return build_direct_addressable_compound_literal(
            context,
            (const node_compound_literal_t *)operand_syntax);
    }
    if (operand_syntax->kind == ND_SOURCE_CAST) {
      const node_source_cast_t *cast =
          (const node_source_cast_t *)operand_syntax;
      direct_cast_binding_t *binding = find_direct_cast_binding(
          context, cast);
      if (binding && binding->type_resolution.target_is_aggregate &&
          binding->aggregate_plan.temporary) {
        psx_semantic_node_t *cast_operand = build_direct_expression(
            context, cast->base.lhs);
        psx_semantic_node_t *initialization = NULL;
        psx_semantic_node_t *object = NULL;
        if (!cast_operand ||
            !build_direct_aggregate_cast_temporary_parts(
                context, &cast->base, binding, cast_operand,
                binding->target_qual_type,
                &initialization, &object))
          return NULL;
        psx_qual_type_t address_type =
            psx_resolve_address_result_qual_type_in(
                context->semantic_context,
                binding->target_qual_type);
        if (address_type.type_id == PSX_TYPE_ID_INVALID)
          return NULL;
        psx_semantic_node_t *address_children[] = {object};
        psx_hir_edge_kind_t address_edges[] = {PSX_HIR_EDGE_LHS};
        psx_hir_node_spec_t address_spec = {
            .kind = PSX_HIR_ADDRESS,
            .attached_qual_type = {
                PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        };
        apply_direct_vla_runtime_view(
            context, syntax, address_type, &address_spec);
        psx_semantic_node_t *address =
            psx_semantic_node_builder_expression(
                &context->builder, &address_spec, address_type,
                address_children, address_edges, 1, NULL,
                syntax->kind);
        return address
                   ? build_direct_comma(
                         context, syntax, initialization, address,
                         address_type)
                   : NULL;
      }
    }
    psx_semantic_node_t *operand =
        build_direct_lvalue(context, syntax->lhs);
    if (!operand) return NULL;
    psx_qual_type_t result_qual_type =
        psx_resolve_address_result_qual_type_in(
            context->semantic_context,
            psx_semantic_node_expression_qual_type(operand));
    if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {operand};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_ADDRESS,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    apply_direct_vla_runtime_view(
        context, syntax, result_qual_type, &spec);
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, result_qual_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_UNARY_PLUS ||
      syntax->kind == ND_UNARY_NEGATE ||
      syntax->kind == ND_CREAL ||
      syntax->kind == ND_CIMAG) {
    psx_type_arithmetic_unary_op_t type_operator;
    psx_semantic_node_t *operand =
        build_direct_expression(context, syntax->lhs);
    if (!operand || !direct_arithmetic_unary_operator(
                        syntax->kind, &type_operator))
      return NULL;
    psx_qual_type_t result_qual_type =
        psx_resolve_arithmetic_unary_result_qual_type_in(
            context->semantic_context, type_operator,
            psx_semantic_node_expression_qual_type(operand));
    if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {operand};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t spec = {
        .kind = syntax->kind == ND_UNARY_PLUS
                    ? PSX_HIR_UNARY_PLUS
                    : syntax->kind == ND_CREAL
                          ? PSX_HIR_CREAL
                          : syntax->kind == ND_CIMAG
                                ? PSX_HIR_CIMAG
                                : PSX_HIR_NEGATE,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, result_qual_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_LOGICAL_NOT) {
    psx_semantic_node_t *operand =
        build_direct_expression(context, syntax->lhs);
    if (!operand) return NULL;
    psx_qual_type_t result_qual_type =
        psx_resolve_logical_not_result_qual_type_in(
            context->semantic_context,
            psx_semantic_node_expression_qual_type(operand));
    if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {operand};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_LOGICAL_NOT,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, result_qual_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_BITWISE_NOT) {
    psx_semantic_node_t *operand =
        build_direct_expression(context, syntax->lhs);
    if (!operand) return NULL;
    psx_qual_type_t result_qual_type =
        psx_resolve_bitwise_not_result_qual_type_in(
            context->semantic_context,
            psx_semantic_node_expression_qual_type(operand));
    if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {operand};
    psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_BITWISE_NOT,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, result_qual_type,
        children, edges, 1, NULL, syntax->kind);
  }

  if (syntax->kind == ND_TERNARY) {
    const node_ctrl_t *ternary = (const node_ctrl_t *)syntax;
    psx_semantic_node_t *condition =
        build_direct_expression(context, syntax->lhs);
    psx_semantic_node_t *then_value =
        build_direct_expression(context, syntax->rhs);
    psx_semantic_node_t *else_value =
        build_direct_expression(context, ternary->els);
    if (!condition || !then_value || !else_value) return NULL;
    psx_conditional_types_resolution_t resolution;
    psx_resolve_conditional_qual_types_in(
        context->semantic_context,
        psx_semantic_node_expression_qual_type(condition),
        psx_semantic_node_expression_qual_type(then_value),
        psx_semantic_node_expression_qual_type(else_value),
        &resolution);
    if (resolution.status != PSX_CONDITIONAL_TYPES_OK) {
      set_failure(
          context->failure,
          PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE, syntax);
      return NULL;
    }
    psx_semantic_node_t *children[] = {
        condition, then_value, else_value};
    psx_hir_edge_kind_t edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS, PSX_HIR_EDGE_ELSE};
    psx_hir_node_spec_t spec = {
        .kind = PSX_HIR_TERNARY,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    apply_direct_vla_runtime_view(
        context, syntax, resolution.result_qual_type, &spec);
    return psx_semantic_node_builder_expression(
        &context->builder, &spec, resolution.result_qual_type,
        children, edges, 3, NULL, syntax->kind);
  }

  return NULL;
}

static int preflight_direct_control_expression(
    direct_resolution_context_t *context,
    const node_t *control, const node_t *expression,
    psx_control_expression_requirement_t requirement) {
  psx_qual_type_t condition_type;
  if (!control || !expression ||
      !preflight_direct_expression(
          context, expression, &condition_type))
    return 0;
  psx_control_expression_status_t status;
  psx_resolve_control_expression_qual_type_in(
      context->semantic_context, condition_type, requirement,
      &status);
  if (status == PSX_CONTROL_EXPRESSION_NOT_SCALAR)
    return note_direct_semantic_rejection(
        context,
        PSX_SYNTAX_TYPED_HIR_REJECTION_CONTROL_CONDITION_NOT_SCALAR,
        control);
  if (status == PSX_CONTROL_EXPRESSION_NOT_INTEGER)
    return note_direct_semantic_rejection(
        context,
        PSX_SYNTAX_TYPED_HIR_REJECTION_SWITCH_CONDITION_NOT_INTEGER,
        control);
  return status == PSX_CONTROL_EXPRESSION_OK;
}

static int direct_integer_constant(
    direct_resolution_context_t *context,
    const node_t *syntax, long long *value) {
  if (!context || !syntax || !value) return 0;
  if (syntax->kind == ND_NUM) {
    *value = ((const node_num_t *)syntax)->val;
    return 1;
  }
  if (syntax->kind == ND_IDENTIFIER) {
    psx_identifier_expression_resolution_t resolution;
    if (!resolve_direct_identifier(
            context, (const node_identifier_t *)syntax,
            &resolution) ||
        resolution.symbol.kind != PSX_IDENTIFIER_ENUM_CONSTANT)
      return 0;
    *value = resolution.symbol.enum_value;
    return 1;
  }
  if (syntax->kind == ND_SIZEOF_QUERY ||
      syntax->kind == ND_ALIGNOF_QUERY) {
    direct_type_query_binding_t *binding =
        find_direct_type_query_binding(context, syntax);
    if (!binding ||
        binding->plan.kind != PSX_TYPE_QUERY_PLAN_CONSTANT)
      return 0;
    *value = binding->plan.constant_factor;
    return 1;
  }
  if (syntax->kind == ND_GENERIC_SELECTION) {
    const node_generic_selection_t *selection =
        (const node_generic_selection_t *)syntax;
    direct_generic_binding_t *binding =
        find_direct_generic_binding(context, selection);
    if (!binding || binding->selected_index < 0 ||
        binding->selected_index >= selection->association_count)
      return 0;
    return direct_integer_constant(
        context,
        selection->associations[binding->selected_index].expression,
        value);
  }
  if (syntax->kind == ND_UNARY_PLUS) {
    return direct_integer_constant(
        context, syntax->lhs, value);
  }
  if (syntax->kind == ND_UNARY_NEGATE) {
    long long operand;
    if (!direct_integer_constant(context, syntax->lhs, &operand))
      return 0;
    *value = -operand;
    return 1;
  }
  if (syntax->kind == ND_LOGICAL_NOT) {
    long long operand;
    if (!direct_integer_constant(context, syntax->lhs, &operand))
      return 0;
    *value = !operand;
    return 1;
  }
  if (syntax->kind == ND_BITWISE_NOT) {
    long long operand;
    if (!direct_integer_constant(context, syntax->lhs, &operand))
      return 0;
    *value = ~operand;
    return 1;
  }
  if (syntax->kind == ND_SOURCE_CAST) {
    long long operand;
    psx_qual_type_t target_qual_type;
    if (!direct_integer_constant(context, syntax->lhs, &operand) ||
        !resolve_direct_source_cast(
            context, (const node_source_cast_t *)syntax,
            &target_qual_type))
      return 0;
    const psx_type_t *target = ps_ctx_type_by_id_in(
        context->semantic_context, target_qual_type.type_id);
    if (!target) return 0;
    return psx_normalize_integer_constant_cast(
        target, operand, value);
  }
  if (syntax->kind == ND_TERNARY) {
    const node_ctrl_t *conditional = (const node_ctrl_t *)syntax;
    long long condition;
    if (!direct_integer_constant(
            context, syntax->lhs, &condition))
      return 0;
    return direct_integer_constant(
        context, condition ? syntax->rhs : conditional->els, value);
  }
  if (syntax->kind == ND_COMMA) {
    long long discarded;
    return direct_integer_constant(context, syntax->lhs, &discarded) &&
           direct_integer_constant(context, syntax->rhs, value);
  }
  if (syntax->kind == ND_FUNCALL) {
    const node_function_call_t *call =
        (const node_function_call_t *)syntax;
    const node_t *operand = psx_builtin_expect_value_operand(call);
    return operand &&
           direct_integer_constant(context, operand, value);
  }

  long long lhs;
  if (!direct_integer_constant(context, syntax->lhs, &lhs))
    return 0;
  if (syntax->kind == ND_LOGAND && !lhs) {
    *value = 0;
    return 1;
  }
  if (syntax->kind == ND_LOGOR && lhs) {
    *value = 1;
    return 1;
  }
  long long rhs;
  if (!direct_integer_constant(context, syntax->rhs, &rhs))
    return 0;
  return psx_apply_integer_constant_binary(
      syntax->kind, lhs, rhs, value);
}

static int preflight_direct_initializer(
    direct_resolution_context_t *context, const node_t *syntax) {
  if (!context || !syntax) return 0;
  if (syntax->kind == ND_STRING) return 1;
  if (syntax->kind != ND_INIT_LIST) {
    psx_qual_type_t qual_type;
    return preflight_direct_expression(context, syntax, &qual_type);
  }

  const node_init_list_t *list = (const node_init_list_t *)syntax;
  if (list->entry_count < 0 ||
      (list->entry_count > 0 && !list->entries))
    return 0;
  for (int i = 0; i < list->entry_count; i++) {
    const psx_initializer_entry_t *entry = &list->entries[i];
    if (!entry->value || entry->designator_count > 8) return 0;
    for (int d = 0; d < entry->designator_count; d++) {
      const psx_initializer_designator_t *designator =
          &entry->designators[d];
      if (designator->kind == PSX_INIT_DESIGNATOR_MEMBER) {
        if (!designator->member_name || designator->member_len <= 0)
          return 0;
        continue;
      }
      if (designator->kind != PSX_INIT_DESIGNATOR_INDEX ||
          !designator->index_expr)
        return 0;
      psx_qual_type_t index_type;
      long long index_value = 0;
      if (!preflight_direct_expression(
              context, designator->index_expr, &index_type))
        return 0;
      const psx_type_t *canonical_index_type = ps_ctx_type_by_id_in(
          context->semantic_context, index_type.type_id);
      if (!canonical_index_type ||
          (canonical_index_type->kind != PSX_TYPE_BOOL &&
           canonical_index_type->kind != PSX_TYPE_INTEGER) ||
          !direct_integer_constant(
              context, designator->index_expr, &index_value) ||
          index_value < 0)
        return 0;
      if (designator->is_range) {
        psx_qual_type_t range_end_type;
        long long range_end_value = 0;
        if (!designator->range_end_expr ||
            !preflight_direct_expression(
                context, designator->range_end_expr,
                &range_end_type))
          return 0;
        const psx_type_t *canonical_range_end_type =
            ps_ctx_type_by_id_in(
                context->semantic_context,
                range_end_type.type_id);
        if (!canonical_range_end_type ||
            (canonical_range_end_type->kind != PSX_TYPE_BOOL &&
             canonical_range_end_type->kind != PSX_TYPE_INTEGER) ||
            !direct_integer_constant(
                context, designator->range_end_expr,
                &range_end_value) ||
            range_end_value < index_value)
          return 0;
      } else if (designator->range_end_expr) {
        return 0;
      }
    }
    if (!preflight_direct_initializer(context, entry->value))
      return 0;
  }
  return 1;
}

static psx_semantic_node_t *build_direct_initializer(
    direct_resolution_context_t *context, const node_t *syntax);

static psx_semantic_node_t *build_direct_initializer_designator(
    direct_resolution_context_t *context,
    const psx_initializer_designator_t *designator,
    const node_t *source) {
  if (!context || !designator || !source) return NULL;
  psx_hir_node_spec_t spec = {
      .kind = designator->kind == PSX_INIT_DESIGNATOR_MEMBER
                  ? PSX_HIR_MEMBER_DESIGNATOR
                  : PSX_HIR_INDEX_DESIGNATOR,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .integer_value = designator->is_range ? 1 : 0,
  };
  psx_semantic_node_t *children[2] = {0};
  psx_hir_edge_kind_t edges[2] = {0};
  size_t child_count = 0;
  if (designator->kind == PSX_INIT_DESIGNATOR_MEMBER) {
    spec.name = designator->member_name;
    spec.name_length = designator->member_len > 0
                           ? (size_t)designator->member_len : 0;
  } else {
    children[child_count] = build_direct_expression(
        context, designator->index_expr);
    edges[child_count++] = PSX_HIR_EDGE_DESIGNATOR_INDEX;
    if (designator->range_end_expr) {
      children[child_count] = build_direct_expression(
          context, designator->range_end_expr);
      edges[child_count++] = PSX_HIR_EDGE_DESIGNATOR_RANGE_END;
    }
  }
  return psx_semantic_node_builder_statement(
      &context->builder, &spec,
      child_count ? children : NULL,
      child_count ? edges : NULL, child_count, source->kind);
}

static psx_semantic_node_t *build_direct_initializer_entry(
    direct_resolution_context_t *context,
    const psx_initializer_entry_t *entry,
    const node_t *source) {
  if (!context || !entry || !entry->value || !source ||
      entry->designator_count > 8)
    return NULL;
  psx_semantic_node_t *children[9] = {0};
  psx_hir_edge_kind_t edges[9] = {0};
  size_t child_count = 0;
  for (int i = 0; i < entry->designator_count; i++) {
    children[child_count] = build_direct_initializer_designator(
        context, &entry->designators[i], source);
    edges[child_count++] = PSX_HIR_EDGE_DESIGNATOR;
  }
  children[child_count] = build_direct_initializer(
      context, entry->value);
  edges[child_count++] = PSX_HIR_EDGE_INITIALIZER_VALUE;
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_INITIALIZER_ENTRY,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &spec, children, edges, child_count,
      source->kind);
}

static psx_semantic_node_t *build_direct_initializer(
    direct_resolution_context_t *context, const node_t *syntax) {
  if (!context || !syntax) return NULL;
  if (syntax->kind == ND_STRING)
    return build_direct_literal(context, syntax);
  if (syntax->kind != ND_INIT_LIST)
    return build_direct_expression(context, syntax);

  const node_init_list_t *list = (const node_init_list_t *)syntax;
  size_t child_count = list->entry_count > 0
                           ? (size_t)list->entry_count : 0;
  psx_semantic_node_t **children = child_count
      ? arena_alloc_in(
            ps_ctx_arena(context->semantic_context),
            child_count * sizeof(*children))
      : NULL;
  psx_hir_edge_kind_t *edges = child_count
      ? arena_alloc_in(
            ps_ctx_arena(context->semantic_context),
            child_count * sizeof(*edges))
      : NULL;
  if (child_count && (!children || !edges)) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        syntax);
    return NULL;
  }
  for (size_t i = 0; i < child_count; i++) {
    children[i] = build_direct_initializer_entry(
        context, &list->entries[i], syntax);
    edges[i] = PSX_HIR_EDGE_INITIALIZER_ENTRY;
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_INITIALIZER_LIST,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &spec, children, edges, child_count,
      syntax->kind);
}

static int bind_direct_case_value(
    direct_resolution_context_t *context,
    const node_case_t *case_node, long long value) {
  if (!context || !context->switch_scope || !case_node) return 0;
  for (direct_case_value_t *existing =
           context->switch_scope->case_values;
       existing; existing = existing->next) {
    if (existing->value == value)
      return note_direct_integer_rejection(
          context, PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_CASE,
          &case_node->base, value);
  }
  direct_case_value_t *case_value = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*case_value));
  direct_case_binding_t *binding = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*binding));
  if (!case_value || !binding) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &case_node->base);
    return 0;
  }
  *case_value = (direct_case_value_t){
      .value = value,
      .next = context->switch_scope->case_values,
  };
  context->switch_scope->case_values = case_value;
  *binding = (direct_case_binding_t){
      .syntax = case_node,
      .value = value,
      .next = context->case_bindings,
  };
  context->case_bindings = binding;
  return 1;
}

static int direct_case_value(
    const direct_resolution_context_t *context,
    const node_case_t *case_node, long long *value) {
  if (!context || !case_node || !value) return 0;
  for (const direct_case_binding_t *binding = context->case_bindings;
       binding; binding = binding->next) {
    if (binding->syntax != case_node) continue;
    *value = binding->value;
    return 1;
  }
  return 0;
}

static psx_scope_id_t direct_label_scope(
    const direct_resolution_context_t *context) {
  psx_scope_graph_t *graph = context
      ? ps_ctx_scope_graph(context->semantic_context) : NULL;
  if (!graph) return PSX_SCOPE_ID_INVALID;
  psx_scope_id_t current = psx_scope_graph_current_scope(graph);
  psx_scope_id_t function_scope =
      psx_scope_graph_nearest_scope_of_kind(
          graph, current, PSX_SCOPE_FUNCTION);
  return function_scope != PSX_SCOPE_ID_INVALID
             ? function_scope : current;
}

static void forget_direct_label_declarations(
    direct_resolution_context_t *context) {
  psx_scope_graph_t *graph = context
      ? ps_ctx_scope_graph(context->semantic_context) : NULL;
  if (!graph) return;
  size_t declaration_count = psx_scope_graph_declaration_count(graph);
  if (context->label_declaration_start > declaration_count) return;
  for (size_t index = context->label_declaration_start;
       index < declaration_count; index++) {
    const psx_scope_declaration_t *declaration =
        psx_scope_graph_declaration_at(graph, index);
    if (!declaration ||
        declaration->name_space != PSX_NAMESPACE_LABEL ||
        declaration->kind != PSX_DECL_LABEL)
      continue;
    psx_scope_graph_forget_declaration(graph, declaration->id);
  }
}

static int collect_direct_function_jumps(
    direct_resolution_context_t *context, const node_t *syntax) {
  if (!context || !syntax) return 0;
  switch (syntax->kind) {
    case ND_BLOCK: {
      const node_block_t *block = (const node_block_t *)syntax;
      for (size_t i = 0; block->body && block->body[i]; i++) {
        if (!collect_direct_function_jumps(
                context, block->body[i]))
          return 0;
      }
      return 1;
    }
    case ND_IF: {
      const node_ctrl_t *control = (const node_ctrl_t *)syntax;
      return collect_direct_function_jumps(context, syntax->rhs) &&
             (!control->els ||
              collect_direct_function_jumps(context, control->els));
    }
    case ND_WHILE:
    case ND_DO_WHILE:
    case ND_FOR:
    case ND_SWITCH:
    case ND_CASE:
    case ND_DEFAULT:
      return !syntax->rhs ||
             collect_direct_function_jumps(context, syntax->rhs);
    case ND_LABEL: {
      const node_jump_t *label = (const node_jump_t *)syntax;
      psx_scope_graph_t *graph =
          ps_ctx_scope_graph(context->semantic_context);
      psx_scope_id_t label_scope = direct_label_scope(context);
      if (!graph || label_scope == PSX_SCOPE_ID_INVALID ||
          !label->name || label->name_len <= 0)
        return note_direct_rejection(context, syntax);
      if (psx_scope_graph_lookup_in_scope(
              graph, label_scope, PSX_NAMESPACE_LABEL,
              label->name, label->name_len) != PSX_DECL_ID_INVALID) {
        return note_direct_named_rejection(
            context, PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_LABEL,
            syntax, label->name, label->name_len);
      }
      psx_decl_id_t declaration_id = psx_scope_graph_declare_at(
          graph, label_scope, PSX_NAMESPACE_LABEL, PSX_DECL_LABEL,
          label->name, label->name_len, (void *)label);
      if (declaration_id == PSX_DECL_ID_INVALID) {
        context->preflight_failed = 1;
        set_failure(
            context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
            syntax);
        return 0;
      }
      return !syntax->rhs ||
             collect_direct_function_jumps(context, syntax->rhs);
    }
    case ND_GOTO: {
      direct_goto_binding_t *binding = arena_alloc_in(
          ps_ctx_arena(context->semantic_context), sizeof(*binding));
      if (!binding) {
        context->preflight_failed = 1;
        set_failure(
            context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
            syntax);
        return 0;
      }
      *binding = (direct_goto_binding_t){
          .syntax = (const node_jump_t *)syntax,
          .next = context->gotos,
      };
      context->gotos = binding;
      return 1;
    }
    default:
      return 1;
  }
}

static int validate_direct_function_jumps(
    direct_resolution_context_t *context) {
  if (!context) return 0;
  psx_scope_graph_t *graph =
      ps_ctx_scope_graph(context->semantic_context);
  psx_scope_id_t label_scope = direct_label_scope(context);
  if (!graph || label_scope == PSX_SCOPE_ID_INVALID) return 0;
  for (const direct_goto_binding_t *reference = context->gotos;
       reference; reference = reference->next) {
    const node_jump_t *jump = reference->syntax;
    if (!jump || !jump->name || jump->name_len <= 0)
      return note_direct_rejection(
          context, jump ? &jump->base : NULL);
    if (psx_scope_graph_lookup_in_scope(
            graph, label_scope, PSX_NAMESPACE_LABEL,
            jump->name, jump->name_len) == PSX_DECL_ID_INVALID) {
      return note_direct_named_rejection(
          context, PSX_SYNTAX_TYPED_HIR_REJECTION_UNDEFINED_GOTO,
          &jump->base, jump->name, jump->name_len);
    }
  }
  return 1;
}

static direct_local_declaration_binding_t *
find_direct_local_declaration(
    const direct_resolution_context_t *context,
    const node_local_declaration_t *syntax) {
  for (direct_local_declaration_binding_t *binding =
           context ? context->local_declarations : NULL;
       binding; binding = binding->next) {
    if (binding->syntax == syntax) return binding;
  }
  return NULL;
}

static int resolve_direct_declarator_application(
    direct_resolution_context_t *context,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application) {
  if (!context || !declarator || !application) return 0;
  psx_runtime_array_bound_t *resolved_bounds = NULL;
  if (declarator->array_bound_count > 0) {
    resolved_bounds = arena_alloc_in(
        ps_ctx_arena(context->semantic_context),
        (size_t)declarator->array_bound_count *
        sizeof(*resolved_bounds));
    if (!resolved_bounds) {
      context->preflight_failed = 1;
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          declarator->array_bounds[0].expression.node);
      return 0;
    }
  }
  for (int i = 0; i < declarator->array_bound_count; i++) {
    const psx_parsed_array_bound_t *bound =
        &declarator->array_bounds[i];
    psx_qual_type_t bound_qual_type;
    long long value;
    if (!bound->expression.node ||
        !preflight_direct_expression(
            context, bound->expression.node, &bound_qual_type))
      return 0;
    const psx_type_t *bound_type = ps_ctx_type_by_id_in(
        context->semantic_context, bound_qual_type.type_id);
    if (!bound_type ||
        (bound_type->kind != PSX_TYPE_BOOL &&
         bound_type->kind != PSX_TYPE_INTEGER))
      return 0;
    int is_constant = direct_integer_constant(
        context, bound->expression.node, &value);
    if (is_constant) {
      if (value <= 0 || value > INT_MAX) return 0;
      resolved_bounds[i] = (psx_runtime_array_bound_t){
          .declarator_op_index = bound->declarator_op_index,
          .expression_id = PSX_SEMANTIC_EXPR_ID_INVALID,
          .constant_value = value,
          .is_constant = 1,
      };
      continue;
    }
    const psx_typed_hir_tree_t *dimension = NULL;
    psx_resolved_hir_build_failure_t dimension_failure;
    psx_syntax_typed_hir_resolution_status_t dimension_status =
        psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts(
            context->semantic_context, context->global_registry,
            context->local_registry, bound->expression.node,
            &dimension, &dimension_failure);
    if (dimension_status != PSX_SYNTAX_TYPED_HIR_RESOLVED ||
        !dimension)
      return 0;
    psx_semantic_expr_id_t expression_id =
        ps_ctx_register_semantic_expression_in(
            context->semantic_context, dimension);
    if (expression_id == PSX_SEMANTIC_EXPR_ID_INVALID) return 0;
    resolved_bounds[i] = (psx_runtime_array_bound_t){
        .declarator_op_index = bound->declarator_op_index,
        .expression_id = expression_id,
        .is_constant = 0,
    };
  }
  return psx_apply_resolved_runtime_parsed_declarator_in_contexts(
      context->semantic_context, context->global_registry,
      context->local_registry, declarator, resolved_bounds,
      declarator->array_bound_count, application);
}

static char *new_direct_vla_typedef_bound_name(
    direct_resolution_context_t *context, int *name_len) {
  if (name_len) *name_len = 0;
  if (!context || !context->lowering_context) return NULL;
  int sequence =
      context->lowering_context->vla_typedef_bound_sequence++;
  const char *format = "__vla_typedef_bound_%d";
  int length = snprintf(NULL, 0, format, sequence);
  if (length <= 0) return NULL;
  char *name = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      (size_t)length + 1);
  if (!name) return NULL;
  snprintf(name, (size_t)length + 1, format, sequence);
  if (name_len) *name_len = length;
  return name;
}

static int capture_direct_vla_typedef_bounds(
    direct_resolution_context_t *context,
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    direct_typedef_bound_capture_t **out_captures,
    int *out_capture_count) {
  if (out_captures) *out_captures = NULL;
  if (out_capture_count) *out_capture_count = 0;
  if (!context || !context->lowering_context || !declarator ||
      !application || !out_captures || !out_capture_count)
    return 0;
  int capture_count = 0;
  for (int i = 0; i < application->array_bound_count; i++) {
    if (!application->array_bounds[i].is_constant) capture_count++;
  }
  if (capture_count == 0) return 1;
  direct_typedef_bound_capture_t *captures = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      (size_t)capture_count * sizeof(*captures));
  if (!captures) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        declarator->array_bounds[0].expression.node);
    return 0;
  }
  int capture_index = 0;
  for (int i = 0; i < application->array_bound_count; i++) {
    psx_runtime_array_bound_t *bound = &application->array_bounds[i];
    if (bound->is_constant) continue;
    const node_t *value_syntax = direct_bound_syntax_for_op(
        declarator, bound->declarator_op_index);
    psx_qual_type_t value_qual_type;
    if (!value_syntax ||
        !preflight_direct_expression(
            context, value_syntax, &value_qual_type))
      return 0;
    psx_type_shape_t value_type = {0};
    if (!psx_semantic_type_table_describe(
            ps_ctx_semantic_type_table_in(context->semantic_context),
            value_qual_type.type_id, &value_type) ||
        (value_type.kind != PSX_TYPE_BOOL &&
         value_type.kind != PSX_TYPE_INTEGER))
      return 0;
    int name_len = 0;
    char *name = new_direct_vla_typedef_bound_name(
        context, &name_len);
    lvar_t *storage = name
        ? lower_complete_internal_local_object(
              &(psx_local_object_request_t){
                  .local_registry = context->local_registry,
                  .lowering_context = context->lowering_context,
                  .name = name,
                  .name_len = name_len,
                  .type = value_qual_type,
              })
        : NULL;
    psx_qual_type_t stored_qual_type = storage
        ? ps_lvar_decl_qual_type(storage)
        : (psx_qual_type_t){
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
    psx_semantic_node_t *reference = storage
        ? build_direct_local_reference(
              context, storage,
              (psx_qual_type_t){
                  stored_qual_type.type_id,
                  PSX_TYPE_QUALIFIER_NONE},
              0, 0, 0, 0, ND_LOCAL_DECLARATION)
        : NULL;
    psx_typed_hir_tree_t *reference_tree = reference
        ? wrap_typed_root(
              context->semantic_context, reference,
              value_syntax, context->failure)
        : NULL;
    psx_semantic_expr_id_t reference_id = reference_tree
        ? ps_ctx_register_semantic_expression_in(
              context->semantic_context, reference_tree)
        : PSX_SEMANTIC_EXPR_ID_INVALID;
    if (!storage || stored_qual_type.type_id == PSX_TYPE_ID_INVALID ||
        reference_id == PSX_SEMANTIC_EXPR_ID_INVALID)
      return 0;
    bound->expression_id = reference_id;
    captures[capture_index++] = (direct_typedef_bound_capture_t){
        .value_syntax = value_syntax,
        .storage = storage,
        .value_qual_type = (psx_qual_type_t){
            stored_qual_type.type_id,
            PSX_TYPE_QUALIFIER_NONE},
    };
  }
  *out_captures = captures;
  *out_capture_count = capture_index;
  return capture_index == capture_count;
}

static int resolve_direct_initializer_index(
    void *opaque, const node_t *expression, long long *value) {
  return direct_integer_constant(
      (direct_resolution_context_t *)opaque, expression, value);
}

static int resolve_direct_initializer_value_type(
    void *opaque, const node_t *expression, psx_qual_type_t *type) {
  direct_resolution_context_t *context =
      (direct_resolution_context_t *)opaque;
  psx_qual_type_t object_type;
  if (preflight_direct_lvalue(
          context, expression, &object_type)) {
    const psx_type_t *canonical = ps_ctx_type_by_id_in(
        context->semantic_context, object_type.type_id);
    if (canonical && canonical->kind == PSX_TYPE_ARRAY) {
      if (type) *type = object_type;
      return 1;
    }
  }
  if (expression && expression->kind == ND_COMPOUND_LITERAL) {
    direct_compound_literal_binding_t *binding = NULL;
    if (resolve_direct_compound_literal(
            context, (const node_compound_literal_t *)expression,
            &binding) && binding) {
      const psx_type_t *canonical = ps_ctx_type_by_id_in(
          context->semantic_context,
          binding->plan.object_qual_type.type_id);
      if (canonical && canonical->kind == PSX_TYPE_ARRAY) {
        if (type) *type = binding->plan.object_qual_type;
        return 1;
      }
    }
  }
  return preflight_direct_expression(context, expression, type);
}

static char *new_direct_initializer_value_name(
    direct_resolution_context_t *context, int *name_len) {
  if (name_len) *name_len = 0;
  if (!context || !context->semantic_context ||
      !context->lowering_context)
    return NULL;
  int sequence =
      context->lowering_context->initializer_value_temp_sequence++;
  const char *format = "__initializer_value_%d";
  int length = snprintf(NULL, 0, format, sequence);
  if (length <= 0) return NULL;
  char *name = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      (size_t)length + 1);
  if (!name) return NULL;
  snprintf(name, (size_t)length + 1, format, sequence);
  if (name_len) *name_len = length;
  return name;
}

static int preflight_direct_flat_initializer(
    direct_resolution_context_t *context,
    psx_qual_type_t object_qual_type,
    const psx_parsed_initializer_t *initializer,
    direct_flat_initializer_binding_t *binding) {
  if (!context || !initializer || !binding ||
      initializer->kind != PSX_DECL_INIT_LIST || !initializer->value ||
      initializer->value->kind != ND_INIT_LIST)
    return 0;
  psx_local_initializer_plan_t *plan = &binding->plan;
  const node_init_list_t *list =
      (const node_init_list_t *)initializer->value;
  psx_local_initializer_status_t status =
      psx_resolve_flat_local_initializer_plan(
          ps_ctx_arena(context->semantic_context),
          ps_ctx_semantic_type_table_in(context->semantic_context),
          ps_ctx_record_decl_table_in(context->semantic_context),
          ps_ctx_record_layout_table_in(context->semantic_context),
          ps_lowering_data_layout(context->lowering_context), object_qual_type,
          list, resolve_direct_initializer_index,
          resolve_direct_initializer_value_type, context, plan);
  if (status == PSX_LOCAL_INITIALIZER_OUT_OF_MEMORY) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        initializer->value);
    return 0;
  }
  if (status != PSX_LOCAL_INITIALIZER_OK) return 0;

  psx_qual_type_t *evaluation_types = NULL;
  psx_qual_type_t *temporary_types = NULL;
  if (plan->evaluation_group_count > 0) {
    evaluation_types = arena_alloc_in(
        ps_ctx_arena(context->semantic_context),
        (size_t)plan->evaluation_group_count *
            sizeof(*evaluation_types));
    temporary_types = arena_alloc_in(
        ps_ctx_arena(context->semantic_context),
        (size_t)plan->evaluation_group_count *
            sizeof(*temporary_types));
    if (!evaluation_types || !temporary_types) {
      context->preflight_failed = 1;
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          initializer->value);
      return 0;
    }
    for (int i = 0; i < plan->evaluation_group_count; i++) {
      temporary_types[i] = (psx_qual_type_t){
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
      if (!plan->evaluation_values[i] ||
          !preflight_direct_expression(
              context, plan->evaluation_values[i],
              &evaluation_types[i]))
        return 0;
    }
  }

  for (int i = 0; i < plan->item_count; i++) {
    const psx_local_initializer_item_t *item =
        &plan->items[i];
    if (!item->is_active || !item->value) continue;
    psx_qual_type_t value_type = {
        PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
    if (item->is_object_copy) {
      if (item->evaluation_group >= 0 ||
          !resolve_direct_initializer_value_type(
              context, item->value, &value_type) ||
          value_type.type_id != item->target_qual_type.type_id)
        return 0;
      continue;
    }
    if (item->evaluation_group >= 0) {
      if (item->evaluation_group >= plan->evaluation_group_count)
        return 0;
      value_type = evaluation_types[item->evaluation_group];
      psx_qual_type_t temporary_type = {
          item->target_qual_type.type_id,
          PSX_TYPE_QUALIFIER_NONE};
      if (temporary_types[item->evaluation_group].type_id ==
          PSX_TYPE_ID_INVALID) {
        temporary_types[item->evaluation_group] = temporary_type;
      } else if (temporary_types[item->evaluation_group].type_id !=
                 temporary_type.type_id) {
        return 0;
      }
    } else if (!preflight_direct_expression(
                   context, item->value, &value_type)) {
      return 0;
    }
    long long constant_value = 1;
    int is_null_pointer_constant =
        direct_integer_constant(
            context, item->value, &constant_value) &&
        constant_value == 0;
    psx_assignment_types_resolution_t assignment;
    psx_resolve_assignment_qual_types_in(
        context->semantic_context,
        (psx_qual_type_t){
            item->target_qual_type.type_id,
            PSX_TYPE_QUALIFIER_NONE},
        value_type, is_null_pointer_constant, &assignment);
    if (assignment.status != PSX_ASSIGNMENT_TYPES_OK) return 0;
  }

  if (plan->evaluation_group_count <= 0 ||
      context->unevaluated_depth > 0)
    return 1;
  binding->evaluation_temporaries = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      (size_t)plan->evaluation_group_count *
          sizeof(*binding->evaluation_temporaries));
  if (!binding->evaluation_temporaries) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        initializer->value);
    return 0;
  }
  for (int i = 0; i < plan->evaluation_group_count; i++) {
    int name_len = 0;
    char *name = new_direct_initializer_value_name(
        context, &name_len);
    lvar_t *temporary = name
        ? lower_complete_internal_local_object(
              &(psx_local_object_request_t){
                  .local_registry = context->local_registry,
                  .lowering_context = context->lowering_context,
                  .name = name,
                  .name_len = name_len,
                  .type = temporary_types[i],
              })
        : NULL;
    if (!temporary) return 0;
    binding->evaluation_temporaries[i] = temporary;
  }
  return 1;
}

static const node_string_t *direct_character_array_string_initializer(
    const psx_parsed_initializer_t *initializer) {
  if (!initializer || !initializer->has_initializer ||
      !initializer->value)
    return NULL;
  if (initializer->kind == PSX_DECL_INIT_EXPR &&
      initializer->value->kind == ND_STRING)
    return (const node_string_t *)initializer->value;
  if (initializer->kind == PSX_DECL_INIT_LIST &&
      initializer->value->kind == ND_INIT_LIST) {
    const node_init_list_t *list =
        (const node_init_list_t *)initializer->value;
    if (list->entry_count == 1 &&
        list->entries[0].designator_count == 0 &&
        !list->entries[0].has_index &&
        !list->entries[0].has_member &&
        list->entries[0].value &&
        list->entries[0].value->kind == ND_STRING)
      return (const node_string_t *)list->entries[0].value;
  }
  return NULL;
}

static const psx_type_t *resolve_direct_completed_array_type(
    direct_resolution_context_t *context,
    const psx_type_t *type,
    const psx_parsed_initializer_t *initializer) {
  if (!context || !type || !ps_type_is_incomplete_array(type) ||
      !initializer || !initializer->has_initializer ||
      !initializer->value)
    return NULL;
  const node_string_t *string =
      direct_character_array_string_initializer(initializer);
  if (string) {
    psx_character_array_string_shape_t shape;
    if (psx_resolve_character_array_string_shape(
            type->array_len,
            ps_type_character_code_unit_width(type->base),
            string->literal_contents, string->literal_length,
            (int)string->char_width, &shape) !=
        PSX_CHARACTER_ARRAY_INITIALIZER_OK)
      return NULL;
    return psx_resolve_completed_incomplete_array_type(
        context->semantic_context, type,
        &(psx_incomplete_array_resolution_t){
            .initializer_count = shape.inferred_capacity,
            .entries_initialize_outer_elements = 1,
        });
  }
  psx_incomplete_array_resolution_t resolution;
  if (!psx_resolve_incomplete_array_initializer_shape(
          type, initializer->kind, initializer->value,
          resolve_direct_initializer_index, context, &resolution))
    return NULL;
  return psx_resolve_completed_incomplete_array_type(
      context->semantic_context, type, &resolution);
}

static direct_compound_literal_binding_t *
find_direct_compound_literal_binding(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound) {
  for (direct_compound_literal_binding_t *binding =
           context ? context->compound_literal_bindings : NULL;
       binding; binding = binding->next) {
    if (binding->syntax == compound) return binding;
  }
  return NULL;
}

static char *new_direct_compound_object_name(
    direct_resolution_context_t *context, int file_scope,
    int *name_len) {
  if (name_len) *name_len = 0;
  if (!context || !context->semantic_context ||
      !context->lowering_context)
    return NULL;
  int sequence = file_scope
                     ? context->lowering_context
                           ->file_scope_compound_sequence++
                     : context->lowering_context
                           ->local_compound_sequence++;
  const char *format = file_scope ? "__compound_lit_%d"
                                  : "__compound_object_%d";
  int length = snprintf(NULL, 0, format, sequence);
  if (length <= 0) return NULL;
  char *name = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      (size_t)length + 1);
  if (!name) return NULL;
  snprintf(name, (size_t)length + 1, format, sequence);
  if (name_len) *name_len = length;
  return name;
}

static int resolve_direct_compound_literal(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound,
    direct_compound_literal_binding_t **out_binding) {
  if (out_binding) *out_binding = NULL;
  if (!context || !compound || !compound->base.rhs ||
      compound->base.rhs->kind != ND_INIT_LIST)
    return 0;
  direct_compound_literal_binding_t *existing =
      find_direct_compound_literal_binding(context, compound);
  if (existing) {
    if (out_binding) *out_binding = existing;
    return 1;
  }

  psx_qual_type_t object_qual_type;
  if (!psx_resolve_type_name_qual_type_in_contexts(
          context->semantic_context, context->global_registry,
          context->local_registry, &compound->type_name,
          &object_qual_type))
    return 0;
  const psx_type_t *object_type = ps_ctx_type_by_id_in(
      context->semantic_context, object_qual_type.type_id);
  psx_parsed_initializer_t initializer = {
      .has_initializer = 1,
      .kind = PSX_DECL_INIT_LIST,
      .value = compound->base.rhs,
      .value_tok = compound->base.tok,
  };
  if (object_type && ps_type_is_incomplete_array(object_type)) {
    object_type = resolve_direct_completed_array_type(
        context, object_type, &initializer);
    object_qual_type = ps_ctx_intern_qual_type_in(
        context->semantic_context, object_type);
  }
  if (!object_type ||
      object_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return 0;

  direct_compound_literal_binding_t *binding = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*binding));
  if (!binding) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &compound->base);
    return 0;
  }
  memset(binding, 0, sizeof(*binding));
  binding->syntax = compound;
  if (!psx_resolve_compound_literal_qual_type_plan_in(
          context->semantic_context, object_qual_type,
          compound->type_name.scope_seq,
          context->function_name != NULL,
          &binding->plan))
    return 0;
  const node_string_t *string_initializer =
      direct_character_array_string_initializer(&initializer);
  if (object_type->kind == PSX_TYPE_ARRAY && string_initializer) {
    psx_character_array_initializer_status_t status =
        psx_plan_character_array_string_initializer(
            ps_ctx_arena(context->semantic_context),
            ps_ctx_semantic_type_table_in(context->semantic_context),
            object_qual_type, string_initializer->literal_contents,
            string_initializer->literal_length,
            (int)string_initializer->char_width,
            &binding->character_array_initializer);
    if (status == PSX_CHARACTER_ARRAY_INITIALIZER_OUT_OF_MEMORY) {
      context->preflight_failed = 1;
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          &compound->base);
    }
    if (status != PSX_CHARACTER_ARRAY_INITIALIZER_OK) return 0;
  } else if (!preflight_direct_flat_initializer(
                 context, object_qual_type, &initializer,
                 &binding->flat_initializer))
    return 0;

  if (context->unevaluated_depth == 0) {
    if (!context->lowering_context)
      return 0;
    if (binding->plan.storage_duration ==
        PSX_COMPOUND_LITERAL_STORAGE_AUTOMATIC) {
      if (context->block_depth == 0) return 0;
      int name_len = 0;
      char *name = new_direct_compound_object_name(
          context, 0, &name_len);
      binding->local_object = name
          ? lower_complete_internal_local_object(
                &(psx_local_object_request_t){
                    .local_registry = context->local_registry,
                    .lowering_context = context->lowering_context,
                    .name = name,
                    .name_len = name_len,
                    .type = object_qual_type,
                })
          : NULL;
      if (!binding->local_object) return 0;
      psx_qual_type_t stored_type =
          ps_lvar_decl_qual_type(binding->local_object);
      if (stored_type.type_id != binding->plan.object_qual_type.type_id)
        return 0;
    } else {
      if (!context->options || context->block_depth != 0)
        return 0;
      psx_parsed_initializer_t storage_initializer = initializer;
      const node_init_list_t *list =
          (const node_init_list_t *)compound->base.rhs;
      if (ps_type_is_scalar(object_type)) {
        if (!list || list->entry_count != 1 ||
            list->entries[0].designator_count != 0 ||
            !list->entries[0].value ||
            list->entries[0].value->kind == ND_INIT_LIST)
          return 0;
        storage_initializer.kind = PSX_DECL_INIT_EXPR;
        storage_initializer.value = list->entries[0].value;
      }
      int name_len = 0;
      char *name = new_direct_compound_object_name(
          context, 1, &name_len);
      psx_global_declaration_pipeline_result_t object;
      if (!name ||
          !psx_apply_global_declaration_pipeline(
              &(psx_global_declaration_pipeline_request_t){
                  .semantic_context = context->semantic_context,
                  .global_registry = context->global_registry,
                  .local_registry = context->local_registry,
                  .lowering_context = context->lowering_context,
                  .options = context->options,
                  .name = name,
                  .name_len = name_len,
                  .type = object_qual_type,
                  .is_static = 1,
                  .is_compiler_generated = 1,
                  .initializer = &storage_initializer,
                  .diag_tok = compound->base.tok,
              },
              &object) || !object.global || !object.initialized)
        return 0;
      binding->global_object = object.global;
      psx_qual_type_t stored_type =
          ps_gvar_decl_qual_type(binding->global_object);
      if (stored_type.type_id != binding->plan.object_qual_type.type_id)
        return 0;
    }
  }

  binding->next = context->compound_literal_bindings;
  context->compound_literal_bindings = binding;
  if (out_binding) *out_binding = binding;
  return 1;
}

static int preflight_direct_local_declaration(
    direct_resolution_context_t *context,
    const node_local_declaration_t *syntax) {
  if (!context || !context->lowering_context || !syntax ||
      !syntax->declaration || context->block_depth == 0)
    return 0;
  const psx_parsed_local_declaration_t *declaration =
      syntax->declaration;
  if (declaration->declarator_count < 0)
    return 0;

  psx_decl_specifier_value_resolution_t specifier_resolution;
  psx_resolve_decl_specifier_value_in_contexts(
      &(psx_decl_specifier_value_request_t){
          .semantic_context = context->semantic_context,
          .global_registry = context->global_registry,
          .local_registry = context->local_registry,
          .syntax = &declaration->specifier,
          .is_standalone_tag = declaration->is_standalone_tag,
      },
      &specifier_resolution);
  if (specifier_resolution.status != PSX_DECL_SPECIFIER_VALUE_OK)
    return 0;
  direct_local_declaration_binding_t *binding = arena_alloc_in(
      ps_ctx_arena(context->semantic_context), sizeof(*binding));
  if (!binding) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &syntax->base);
    return 0;
  }
  if (declaration->is_standalone_tag) {
    *binding = (direct_local_declaration_binding_t){
        .syntax = syntax,
        .is_semantic_only = 1,
        .next = context->local_declarations,
    };
    context->local_declarations = binding;
    return 1;
  }
  const psx_type_t *base_type = specifier_resolution.base_type;
  if (!base_type || declaration->declarator_count <= 0)
    return 0;
  direct_local_declarator_binding_t *declarators = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      (size_t)declaration->declarator_count * sizeof(*declarators));
  if (!declarators) {
    context->preflight_failed = 1;
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &syntax->base);
    return 0;
  }
  memset(declarators, 0,
         (size_t)declaration->declarator_count * sizeof(*declarators));

  if (declaration->is_typedef) {
    for (int i = 0; i < declaration->declarator_count; i++) {
      const psx_parsed_declarator_t *declarator =
          &declaration->declarators[i];
      const psx_parsed_initializer_t *initializer =
          &declaration->initializers[i];
      token_ident_t *name = declarator->identifier;
      if (!name || initializer->has_initializer)
        return 0;
      psx_runtime_declarator_application_t application;
      if (!resolve_direct_declarator_application(
              context, declarator, &application))
        return 0;
      direct_typedef_bound_capture_t *captures = NULL;
      int capture_count = 0;
      if (!capture_direct_vla_typedef_bounds(
              context, declarator, &application,
              &captures, &capture_count))
        return 0;
      psx_runtime_declarator_application_t effective_application;
      if (!psx_compose_runtime_declarator_applications_in(
              ps_ctx_arena(context->semantic_context),
              &application,
              specifier_resolution.typedef_runtime_application,
              &effective_application))
        return 0;
      const psx_type_t *type =
          psx_apply_runtime_declarator_type_in_context(
              context->semantic_context, base_type, &application);
      if (!type) return 0;
      psx_typedef_declaration_resolution_t resolution;
      psx_resolve_typedef_declaration(
          &(psx_typedef_declaration_resolution_request_t){
              .semantic_context = context->semantic_context,
              .global_registry = context->global_registry,
              .local_registry = context->local_registry,
              .name = name->str,
              .name_len = name->len,
              .type = type,
              .runtime_application =
                  ps_type_contains_vla_array(type)
                      ? &effective_application : NULL,
          },
          &resolution);
      if (resolution.status != PSX_TYPEDEF_DECLARATION_OK) return 0;
      declarators[i] = (direct_local_declarator_binding_t){
          .typedef_bound_captures = captures,
          .typedef_bound_capture_count = capture_count,
          .is_semantic_only = capture_count == 0,
      };
    }
    *binding = (direct_local_declaration_binding_t){
        .syntax = syntax,
        .declarators = declarators,
        .declarator_count = declaration->declarator_count,
        .next = context->local_declarations,
    };
    context->local_declarations = binding;
    return 1;
  }

  for (int i = 0; i < declaration->declarator_count; i++) {
    const psx_parsed_declarator_t *declarator =
        &declaration->declarators[i];
    const psx_parsed_initializer_t *initializer =
        &declaration->initializers[i];
    token_ident_t *name = declarator->identifier;
    if (!name ||
        (initializer->has_initializer &&
         initializer->kind != PSX_DECL_INIT_EXPR &&
         initializer->kind != PSX_DECL_INIT_LIST))
      return 0;
    psx_runtime_declarator_application_t application;
    if (!resolve_direct_declarator_application(
            context, declarator, &application))
      return 0;
    psx_runtime_declarator_application_t effective_application;
    if (!psx_compose_runtime_declarator_applications_in(
            ps_ctx_arena(context->semantic_context),
            &application,
            specifier_resolution.typedef_runtime_application,
            &effective_application))
      return 0;
    const psx_type_t *type =
        psx_apply_runtime_declarator_type_in_context(
            context->semantic_context, base_type, &application);
    if (type && (declaration->is_extern ||
                 type->kind == PSX_TYPE_FUNCTION)) {
      if (type->kind == PSX_TYPE_FUNCTION) {
        direct_function_declaration_checkpoint_t *checkpoint =
            arena_alloc_in(
                ps_ctx_arena(context->semantic_context),
                sizeof(*checkpoint));
        if (!checkpoint) {
          context->preflight_failed = 1;
          set_failure(
              context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
              &syntax->base);
          return 0;
        }
        *checkpoint = (direct_function_declaration_checkpoint_t){
            .name = name->str,
            .name_len = name->len,
            .next = context->function_declarations,
        };
        ps_ctx_checkpoint_function_registration_in(
            context->semantic_context, name->str, name->len,
            &checkpoint->checkpoint);
        context->function_declarations = checkpoint;
      }
      if (!psx_apply_block_extern_declaration_pipeline(
              &(psx_block_extern_declaration_pipeline_request_t){
                  .semantic_context = context->semantic_context,
                  .global_registry = context->global_registry,
                  .local_registry = context->local_registry,
                  .lowering_context = context->lowering_context,
                  .options = context->options,
                  .name = name->str,
                  .name_len = name->len,
                  .type = type,
                  .has_initializer = initializer->has_initializer,
                  .diag_tok = (token_t *)name,
              }))
        return 0;
      declarators[i] = (direct_local_declarator_binding_t){
          .declaration_qual_type = ps_ctx_intern_qual_type_in(
              context->semantic_context, type),
          .initializer = initializer,
          .is_semantic_only = 1,
      };
      continue;
    }
    if (type && declaration->is_static) {
      psx_static_local_declaration_pipeline_request_t static_request = {
          .semantic_context = context->semantic_context,
          .global_registry = context->global_registry,
          .local_registry = context->local_registry,
          .lowering_context = context->lowering_context,
          .options = context->options,
          .function_name = context->function_name,
          .function_name_len = context->function_name_len,
          .name = name->str,
          .name_len = name->len,
          .type = type,
          .initializer = initializer,
          .diag_tok = (token_t *)name,
      };
      psx_static_local_declaration_pipeline_result_t static_result;
      if (!psx_begin_static_local_declaration_pipeline(
              &static_request,
              &static_result) || !static_result.alias ||
          !static_result.global)
        return 0;
      if (initializer->has_initializer) {
        if (!initializer->value)
          return 0;
        const psx_typed_hir_tree_t *initializer_typed_hir = NULL;
        psx_resolved_hir_build_failure_t initializer_failure;
        psx_syntax_typed_hir_resolution_status_t initializer_status =
            initializer->kind == PSX_DECL_INIT_LIST
                ? psx_resolve_syntax_initializer_direct_to_typed_hir_in_contexts(
                      context->semantic_context,
                      context->global_registry,
                      context->local_registry, initializer->value,
                      &initializer_typed_hir, &initializer_failure)
                : psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts(
                      context->semantic_context,
                      context->global_registry,
                      context->local_registry, initializer->value,
                      &initializer_typed_hir, &initializer_failure);
        if (initializer_status == PSX_SYNTAX_TYPED_HIR_FAILED) {
          context->preflight_failed = 1;
          if (context->failure)
            *context->failure = initializer_failure;
          return 0;
        }
        if (initializer_status != PSX_SYNTAX_TYPED_HIR_RESOLVED ||
            !initializer_typed_hir ||
            !psx_finish_static_local_declaration_typed_hir_pipeline(
                &static_request, &static_result,
                initializer_typed_hir))
          return 0;
      }
      declarators[i] = (direct_local_declarator_binding_t){
          .local = static_result.alias,
          .declaration_qual_type = ps_lvar_decl_qual_type(
              static_result.alias),
          .initializer = initializer,
          .is_semantic_only = 1,
      };
      continue;
    }
    if (type && ps_type_is_incomplete_array(type))
      type = resolve_direct_completed_array_type(
          context, type, initializer);
    int is_complete_fixed_array =
        type && type->kind == PSX_TYPE_ARRAY &&
        !ps_type_is_incomplete_array(type) &&
        !ps_type_contains_vla_array(type);
    int is_complete_aggregate =
        type && ps_type_is_tag_aggregate(type);
    int has_vla_type = type && ps_type_contains_vla_array(type);
    int is_vla_object =
        has_vla_type && type->kind == PSX_TYPE_ARRAY;
    if (!type || (!ps_type_is_scalar(type) &&
                  !is_complete_fixed_array &&
                  !is_complete_aggregate && !has_vla_type))
      return 0;
    if (is_vla_object && initializer->has_initializer)
      return 0;
    psx_automatic_local_declaration_pipeline_request_t request = {
        .semantic_context = context->semantic_context,
        .local_registry = context->local_registry,
        .lowering_context = context->lowering_context,
        .name = name->str,
        .name_len = name->len,
        .type = type,
        .application = &effective_application,
        .requested_alignment =
            specifier_resolution.requested_alignment,
        .initializer = initializer,
        .diag_tok = (token_t *)name,
    };
    psx_automatic_local_declaration_pipeline_result_t result;
    if (!psx_begin_automatic_local_declaration_hir_pipeline(
            &request, &result) || !result.var ||
        (result.initialization && !result.vla_runtime_plan) ||
        (has_vla_type && !result.vla_runtime_plan))
      return 0;
    psx_qual_type_t declaration_qual_type =
        ps_lvar_decl_qual_type(result.var);
    if (declaration_qual_type.type_id == PSX_TYPE_ID_INVALID)
      return 0;

    direct_flat_initializer_binding_t flat_initializer = {0};
    psx_character_array_initializer_plan_t character_initializer = {0};
    int is_object_copy_initializer = 0;
    const node_string_t *string_initializer =
        direct_character_array_string_initializer(initializer);
    if (type->kind == PSX_TYPE_ARRAY && string_initializer) {
      psx_character_array_initializer_status_t status =
          psx_plan_character_array_string_initializer(
              ps_ctx_arena(context->semantic_context),
              ps_ctx_semantic_type_table_in(context->semantic_context),
              declaration_qual_type,
              string_initializer->literal_contents,
              string_initializer->literal_length,
              (int)string_initializer->char_width,
              &character_initializer);
      if (status == PSX_CHARACTER_ARRAY_INITIALIZER_OUT_OF_MEMORY) {
        context->preflight_failed = 1;
        set_failure(
            context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
            initializer->value);
      }
      if (status != PSX_CHARACTER_ARRAY_INITIALIZER_OK)
        return 0;
    } else if (initializer->has_initializer &&
        initializer->kind == PSX_DECL_INIT_LIST) {
      if (!preflight_direct_flat_initializer(
              context, declaration_qual_type, initializer,
              &flat_initializer))
        return 0;
    } else if (initializer->has_initializer &&
        initializer->kind == PSX_DECL_INIT_EXPR) {
      psx_qual_type_t value_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
      if (type->kind == PSX_TYPE_ARRAY) {
        if (!initializer->value ||
            !resolve_direct_initializer_value_type(
                context, initializer->value, &value_type) ||
            value_type.type_id != declaration_qual_type.type_id)
          return note_direct_rejection(
              context, initializer->value);
        is_object_copy_initializer = 1;
      } else {
        if (!initializer->value ||
            !preflight_direct_expression(
                context, initializer->value, &value_type))
          return 0;
        long long constant_value = 1;
        int is_null_pointer_constant =
            direct_integer_constant(
                context, initializer->value, &constant_value) &&
            constant_value == 0;
        psx_assignment_types_resolution_t assignment;
        psx_resolve_assignment_qual_types_in(
            context->semantic_context,
            (psx_qual_type_t){
                declaration_qual_type.type_id,
                PSX_TYPE_QUALIFIER_NONE},
            value_type, is_null_pointer_constant, &assignment);
        if (assignment.status != PSX_ASSIGNMENT_TYPES_OK)
          return note_direct_rejection(
              context, initializer->value);
      }
    }
    declarators[i] = (direct_local_declarator_binding_t){
        .local = result.var,
        .declaration_qual_type = declaration_qual_type,
        .initializer = initializer,
        .flat_initializer = flat_initializer,
        .character_array_initializer = character_initializer,
        .vla_runtime_plan = result.vla_runtime_plan,
        .is_object_copy_initializer = is_object_copy_initializer,
    };
  }
  *binding = (direct_local_declaration_binding_t){
      .syntax = syntax,
      .declarators = declarators,
      .declarator_count = declaration->declarator_count,
      .next = context->local_declarations,
  };
  context->local_declarations = binding;
  return 1;
}

static int preflight_direct_statement(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  int resolved = preflight_direct_statement_impl(context, syntax);
  if (!resolved && context && context->failure &&
      context->failure->status == PSX_RESOLVED_HIR_BUILD_OK &&
      context->failure->source_node_kind < 0) {
    context->failure->source_node_kind =
        syntax ? syntax->kind : PSX_SYNTAX_NODE_INVALID;
    context->failure->source_token = syntax ? syntax->tok : NULL;
  }
  return resolved;
}

static int preflight_direct_statement_impl(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (!context || !syntax) return 0;
  switch (syntax->kind) {
    case ND_BLOCK: {
      const node_block_t *block = (const node_block_t *)syntax;
      int nested_scope = context->block_depth > 0;
      if (nested_scope)
        ps_decl_enter_scope_in(context->local_registry);
      context->block_depth++;
      for (size_t i = 0; block->body && block->body[i]; i++) {
        if (!preflight_direct_statement(context, block->body[i])) {
          context->block_depth--;
          if (nested_scope)
            ps_decl_leave_scope_in(context->local_registry);
          return 0;
        }
      }
      context->block_depth--;
      if (nested_scope)
        ps_decl_leave_scope_in(context->local_registry);
      return 1;
    }
    case ND_LOCAL_DECLARATION:
      return preflight_direct_local_declaration(
          context, (const node_local_declaration_t *)syntax);
    case ND_NULL_STMT:
      return 1;
    case ND_STATIC_ASSERT: {
      const node_static_assert_t *assertion =
          (const node_static_assert_t *)syntax;
      psx_qual_type_t condition_type;
      long long value = 0;
      if (!assertion->condition ||
          !preflight_direct_unevaluated_expression(
              context, assertion->condition, &condition_type))
        return 0;
      const psx_type_t *canonical = ps_ctx_type_by_id_in(
          context->semantic_context, condition_type.type_id);
      int is_constant = canonical &&
          (canonical->kind == PSX_TYPE_BOOL ||
           canonical->kind == PSX_TYPE_INTEGER) &&
          direct_integer_constant(
              context, assertion->condition, &value);
      psx_static_assert_resolution_t resolution;
      psx_resolve_static_assert(
          &(psx_static_assert_request_t){
              .is_constant = is_constant,
              .value = value,
          },
          &resolution);
      if (resolution.status == PSX_STATIC_ASSERT_NOT_CONSTANT)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_STATIC_ASSERT_NOT_CONSTANT,
            syntax);
      if (resolution.status == PSX_STATIC_ASSERT_FAILED)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_STATIC_ASSERT_FAILED,
            syntax);
      return 1;
    }
    case ND_RETURN: {
      if (!context->enforce_function_return_type)
        return !syntax->lhs ||
               preflight_direct_expression(
                   context, syntax->lhs, NULL);
      const psx_type_t *return_type = ps_ctx_type_by_id_in(
          context->semantic_context,
          context->function_return_qual_type.type_id);
      if (!return_type) return 0;
      if (return_type->kind == PSX_TYPE_VOID) {
        if (syntax->lhs)
          return note_direct_semantic_rejection(
              context,
              PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_VALUE_FORBIDDEN,
              syntax);
        return 1;
      }
      if (!syntax->lhs)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_VALUE_REQUIRED,
            syntax);
      psx_qual_type_t value_type;
      if (!preflight_direct_expression(
              context, syntax->lhs, &value_type))
        return 0;
      long long constant_value = 1;
      int is_null_pointer_constant =
          direct_integer_constant(
              context, syntax->lhs, &constant_value) &&
          constant_value == 0;
      psx_return_types_status_t return_status;
      psx_resolve_return_qual_types_in(
          context->semantic_context,
          context->function_return_qual_type, value_type,
          is_null_pointer_constant, &return_status);
      if (return_status == PSX_RETURN_TYPES_INCOMPATIBLE)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_TYPES_INCOMPATIBLE,
            syntax);
      if (return_status == PSX_RETURN_TYPES_DISCARDS_QUALIFIERS)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_RETURN_DISCARDS_QUALIFIERS,
            syntax);
      return return_status == PSX_RETURN_TYPES_OK;
    }
    case ND_IF: {
      const node_ctrl_t *control = (const node_ctrl_t *)syntax;
      return preflight_direct_control_expression(
                 context, syntax, syntax->lhs,
                 PSX_CONTROL_EXPRESSION_REQUIRES_SCALAR) &&
             preflight_direct_statement(context, syntax->rhs) &&
             (!control->els ||
              preflight_direct_statement(context, control->els));
    }
    case ND_WHILE:
    case ND_DO_WHILE: {
      if (!preflight_direct_control_expression(
              context, syntax, syntax->lhs,
              PSX_CONTROL_EXPRESSION_REQUIRES_SCALAR))
        return 0;
      context->loop_depth++;
      int resolved =
          preflight_direct_statement(context, syntax->rhs);
      context->loop_depth--;
      return resolved;
    }
    case ND_FOR: {
      const node_ctrl_t *control = (const node_ctrl_t *)syntax;
      int declaration_scope = control->init &&
          control->init->kind == ND_LOCAL_DECLARATION;
      if (declaration_scope) {
        ps_decl_enter_scope_in(context->local_registry);
      }
      int resolved = !control->init ||
          (declaration_scope
               ? preflight_direct_statement(
                     context, control->init)
               : preflight_direct_expression(
                     context, control->init, NULL));
      if (resolved && syntax->lhs)
        resolved = preflight_direct_control_expression(
            context, syntax, syntax->lhs,
            PSX_CONTROL_EXPRESSION_REQUIRES_SCALAR);
      if (resolved && control->inc)
        resolved = preflight_direct_expression(
            context, control->inc, NULL);
      if (resolved) {
        context->loop_depth++;
        resolved = preflight_direct_statement(
            context, syntax->rhs);
        context->loop_depth--;
      }
      if (declaration_scope) {
        ps_decl_leave_scope_in(context->local_registry);
      }
      return resolved;
    }
    case ND_SWITCH: {
      if (!preflight_direct_control_expression(
              context, syntax, syntax->lhs,
              PSX_CONTROL_EXPRESSION_REQUIRES_INTEGER))
        return 0;
      direct_switch_scope_t scope = {
          .parent = context->switch_scope,
      };
      context->switch_scope = &scope;
      context->switch_depth++;
      int resolved =
          preflight_direct_statement(context, syntax->rhs);
      context->switch_depth--;
      context->switch_scope = scope.parent;
      return resolved;
    }
    case ND_CASE: {
      psx_qual_type_t case_qual_type;
      long long value;
      if (!context->switch_scope)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_CASE_OUTSIDE_SWITCH,
            syntax);
      if (!preflight_direct_expression(
              context, syntax->lhs, &case_qual_type))
        return 0;
      const psx_type_t *case_type = ps_ctx_type_by_id_in(
          context->semantic_context, case_qual_type.type_id);
      if (!case_type ||
          (case_type->kind != PSX_TYPE_BOOL &&
           case_type->kind != PSX_TYPE_INTEGER) ||
          !direct_integer_constant(context, syntax->lhs, &value))
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_CASE_NOT_INTEGER_CONSTANT,
            syntax);
      return bind_direct_case_value(
                 context, (const node_case_t *)syntax, value) &&
             preflight_direct_statement(context, syntax->rhs);
    }
    case ND_DEFAULT:
      if (!context->switch_scope)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_DEFAULT_OUTSIDE_SWITCH,
            syntax);
      if (context->switch_scope->has_default)
        return note_direct_semantic_rejection(
            context, PSX_SYNTAX_TYPED_HIR_REJECTION_DUPLICATE_DEFAULT,
            syntax);
      context->switch_scope->has_default = 1;
      return preflight_direct_statement(context, syntax->rhs);
    case ND_GOTO:
      return 1;
    case ND_LABEL:
      return !syntax->rhs ||
             preflight_direct_statement(context, syntax->rhs);
    case ND_BREAK:
      if (context->loop_depth == 0 && context->switch_depth == 0)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_BREAK_OUTSIDE_LOOP_OR_SWITCH,
            syntax);
      return 1;
    case ND_CONTINUE:
      if (context->loop_depth == 0)
        return note_direct_semantic_rejection(
            context,
            PSX_SYNTAX_TYPED_HIR_REJECTION_CONTINUE_OUTSIDE_LOOP,
            syntax);
      return 1;
    default:
      return preflight_direct_expression(context, syntax, NULL);
  }
}

static psx_semantic_node_t *build_direct_block_excluding(
    direct_resolution_context_t *context,
    const node_block_t *block, const node_t *excluded) {
  size_t count = 0;
  for (size_t i = 0; block->body && block->body[i]; i++) {
    if (block->body[i] != excluded) count++;
  }
  psx_semantic_node_t **children = NULL;
  psx_hir_edge_kind_t *edges = NULL;
  if (count) {
    children = arena_alloc_in(
        ps_ctx_arena(context->semantic_context),
        count * sizeof(*children));
    edges = arena_alloc_in(
        ps_ctx_arena(context->semantic_context),
        count * sizeof(*edges));
    if (!children || !edges) {
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          &block->base);
      return NULL;
    }
    size_t child_index = 0;
    for (size_t i = 0; block->body && block->body[i]; i++) {
      if (block->body[i] == excluded) continue;
      children[child_index] =
          build_direct_statement(context, block->body[i]);
      edges[child_index] = PSX_HIR_EDGE_BLOCK_ITEM;
      if (!children[child_index]) return NULL;
      child_index++;
    }
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_BLOCK,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &spec, children, edges, count,
      block->base.kind);
}

static psx_semantic_node_t *build_direct_block(
    direct_resolution_context_t *context,
    const node_block_t *block) {
  return build_direct_block_excluding(context, block, NULL);
}

static psx_semantic_node_t *build_direct_declared_local(
    direct_resolution_context_t *context,
    const direct_local_declarator_binding_t *declarator,
    int source_node_kind) {
  if (!context || !declarator || !declarator->local) return NULL;
  psx_hir_node_spec_t local_spec = {
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = ps_lvar_name(declarator->local),
      .name_length = ps_lvar_name_len(declarator->local) > 0
                         ? (size_t)ps_lvar_name_len(declarator->local)
                         : 0,
  };
  if (!psx_resolve_local_hir_node_spec_in(
          context->semantic_context, declarator->local,
          ps_lvar_offset(declarator->local), &local_spec))
    return NULL;
  return psx_semantic_node_builder_leaf_expression(
      &context->builder, &local_spec,
      declarator->declaration_qual_type, NULL,
      source_node_kind);
}

static psx_semantic_node_t *build_direct_initializer_target(
    direct_resolution_context_t *context,
    lvar_t *local, const psx_local_initializer_item_t *item,
    int source_node_kind) {
  if (!context || !local || !item ||
      item->target_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  psx_hir_node_spec_t local_spec = {
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = ps_lvar_name(local),
      .name_length = ps_lvar_name_len(local) > 0
                         ? (size_t)ps_lvar_name_len(local)
                         : 0,
      .bit_width = item->bit_width,
      .bit_offset = item->bit_offset,
      .bit_is_signed = item->bit_is_signed,
  };
  if (!psx_resolve_local_hir_node_spec_in(
          context->semantic_context, local,
          ps_lvar_offset(local) + item->relative_offset,
          &local_spec))
    return NULL;
  return psx_semantic_node_builder_leaf_expression(
      &context->builder, &local_spec, item->target_qual_type,
      NULL, source_node_kind);
}

static psx_semantic_node_t *build_direct_flat_initializer(
    direct_resolution_context_t *context,
    lvar_t *local, psx_qual_type_t object_qual_type,
    const direct_flat_initializer_binding_t *binding,
    int source_node_kind) {
  if (!context || !local || !binding)
    return NULL;
  const psx_local_initializer_plan_t *plan = &binding->plan;
  if (!plan->items || plan->item_count <= 0 ||
      plan->object_qual_type.type_id !=
          object_qual_type.type_id ||
      (plan->evaluation_group_count > 0 &&
       !binding->evaluation_temporaries))
    return NULL;

  size_t active_item_count = 0;
  for (int i = 0; i < plan->item_count; i++) {
    if (plan->items[i].is_active) active_item_count++;
  }
  size_t count = (size_t)plan->evaluation_group_count +
                 active_item_count;
  psx_semantic_node_t **children = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      count * sizeof(*children));
  psx_hir_edge_kind_t *edges = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      count * sizeof(*edges));
  if (!children || !edges) return NULL;

  size_t child_index = 0;
  for (int i = 0; i < plan->evaluation_group_count; i++) {
    lvar_t *temporary = binding->evaluation_temporaries[i];
    psx_qual_type_t temporary_type = ps_lvar_decl_qual_type(temporary);
    psx_semantic_node_t *target = build_direct_local_reference(
        context, temporary, temporary_type, 0, 0, 0, 0,
        source_node_kind);
    const node_t *value_syntax = plan->evaluation_values[i];
    psx_semantic_node_t *value = build_direct_expression(
        context, value_syntax);
    if (!target || !value) return NULL;
    long long constant_value = 1;
    int is_null_pointer_constant =
        direct_integer_constant(
            context, value_syntax, &constant_value) &&
        constant_value == 0;
    psx_assignment_types_resolution_t assignment;
    psx_resolve_assignment_qual_types_in(
        context->semantic_context,
        psx_semantic_node_expression_qual_type(target),
        psx_semantic_node_expression_qual_type(value),
        is_null_pointer_constant, &assignment);
    if (assignment.status != PSX_ASSIGNMENT_TYPES_OK)
      return NULL;
    psx_semantic_node_t *assignment_children[] = {target, value};
    psx_hir_edge_kind_t assignment_edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
    psx_hir_node_spec_t assignment_spec = {
        .kind = PSX_HIR_ASSIGN,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .is_declaration_initializer = 1,
    };
    children[child_index] = psx_semantic_node_builder_expression(
        &context->builder, &assignment_spec,
        assignment.result_qual_type, assignment_children,
        assignment_edges, 2, NULL, source_node_kind);
    edges[child_index] = PSX_HIR_EDGE_BLOCK_ITEM;
    if (!children[child_index]) return NULL;
    ps_decl_record_lvar_usage_in_region_in(
        context->local_registry, temporary,
        PSX_LVAR_USAGE_INITIALIZED, NULL);
    child_index++;
  }

  for (int i = 0; i < plan->item_count; i++) {
    const psx_local_initializer_item_t *item =
        &plan->items[i];
    if (!item->is_active) continue;
    psx_semantic_node_t *target = build_direct_initializer_target(
        context, local, item, source_node_kind);
    psx_semantic_node_t *value = NULL;
    int is_null_pointer_constant = 0;
    if (item->evaluation_group >= 0) {
      value = build_direct_local_reference(
          context,
          binding->evaluation_temporaries[item->evaluation_group],
          ps_lvar_decl_qual_type(
              binding->evaluation_temporaries[item->evaluation_group]),
          0, 0, 0, 0, source_node_kind);
    } else if (item->value) {
      value = build_direct_expression(context, item->value);
      long long constant_value = 1;
      is_null_pointer_constant = direct_integer_constant(
          context, item->value, &constant_value) &&
          constant_value == 0;
    } else {
      psx_qual_type_t value_qual_type = item->has_integer_value
          ? (psx_qual_type_t){
                item->target_qual_type.type_id,
                PSX_TYPE_QUALIFIER_NONE}
          : ps_ctx_intern_integer_qual_type_in(
                context->semantic_context, PSX_INTEGER_KIND_INT, 0, 0);
      psx_hir_node_spec_t value_spec = {
          .kind = PSX_HIR_NUMBER,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
          .integer_value = item->has_integer_value
                               ? item->integer_value : 0,
      };
      value = psx_semantic_node_builder_leaf_expression(
          &context->builder, &value_spec, value_qual_type, NULL,
          item->has_integer_value ? ND_STRING : ND_NUM);
      is_null_pointer_constant = value_spec.integer_value == 0;
    }
    if (!target || !value)
      return NULL;
    if (item->is_object_copy) {
      psx_semantic_node_t *copy_children[] = {target, value};
      psx_hir_edge_kind_t copy_edges[] = {
          PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
      psx_hir_node_spec_t copy_spec = {
          .kind = PSX_HIR_OBJECT_COPY,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      };
      children[child_index] = psx_semantic_node_builder_expression(
          &context->builder, &copy_spec,
          item->target_qual_type, copy_children, copy_edges, 2,
          NULL, source_node_kind);
      edges[child_index] = PSX_HIR_EDGE_BLOCK_ITEM;
      if (!children[child_index]) return NULL;
      child_index++;
      continue;
    }
    psx_assignment_types_resolution_t assignment;
    psx_resolve_assignment_qual_types_in(
        context->semantic_context,
        psx_semantic_node_expression_qual_type(target),
        psx_semantic_node_expression_qual_type(value),
        is_null_pointer_constant, &assignment);
    if (assignment.status != PSX_ASSIGNMENT_TYPES_OK)
      return NULL;
    psx_semantic_node_t *assignment_children[] = {target, value};
    psx_hir_edge_kind_t assignment_edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
    psx_hir_node_spec_t assignment_spec = {
        .kind = PSX_HIR_ASSIGN,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    children[child_index] = psx_semantic_node_builder_expression(
        &context->builder, &assignment_spec,
        assignment.result_qual_type, assignment_children,
        assignment_edges, 2, NULL, source_node_kind);
    edges[child_index] = PSX_HIR_EDGE_BLOCK_ITEM;
    if (!children[child_index]) return NULL;
    child_index++;
  }
  psx_hir_node_spec_t block_spec = {
      .kind = PSX_HIR_BLOCK,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &block_spec, children, edges, count,
      source_node_kind);
}

static psx_semantic_node_t *build_direct_character_array_initializer(
    direct_resolution_context_t *context, lvar_t *local,
    const psx_character_array_initializer_plan_t *plan,
    int source_node_kind) {
  if (!context || !local || !plan || !plan->units ||
      plan->unit_count <= 0)
    return NULL;
  int element_size = ps_lowering_type_id_size(
      context->lowering_context, plan->element_qual_type.type_id);
  size_t unit_count = (size_t)plan->unit_count;
  psx_semantic_node_t **assignments = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      unit_count * sizeof(*assignments));
  psx_hir_edge_kind_t *edges = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      unit_count * sizeof(*edges));
  if (element_size <= 0 || !assignments || !edges) return NULL;
  psx_qual_type_t value_qual_type = {
      plan->element_qual_type.type_id, PSX_TYPE_QUALIFIER_NONE};
  for (size_t unit = 0; unit < unit_count; unit++) {
    psx_semantic_node_t *target = build_direct_local_reference(
        context, local, plan->element_qual_type,
        (int)unit * element_size, 0, 0, 0, source_node_kind);
    psx_hir_node_spec_t value_spec = {
        .kind = PSX_HIR_NUMBER,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .integer_value = plan->units[unit],
    };
    psx_semantic_node_t *value =
        psx_semantic_node_builder_leaf_expression(
            &context->builder, &value_spec, value_qual_type,
            NULL, ND_STRING);
    psx_semantic_node_t *assignment_children[] = {target, value};
    psx_hir_edge_kind_t assignment_edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
    psx_hir_node_spec_t assignment_spec = {
        .kind = PSX_HIR_ASSIGN,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    assignments[unit] = target && value
        ? psx_semantic_node_builder_expression(
              &context->builder, &assignment_spec, value_qual_type,
              assignment_children, assignment_edges, 2, NULL,
              source_node_kind)
        : NULL;
    edges[unit] = PSX_HIR_EDGE_BLOCK_ITEM;
    if (!assignments[unit]) return NULL;
  }
  psx_hir_node_spec_t block_spec = {
      .kind = PSX_HIR_BLOCK,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &block_spec, assignments, edges,
      unit_count, source_node_kind);
}

static psx_semantic_node_t *build_direct_compound_literal_value(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound,
    int force_address_result) {
  direct_compound_literal_binding_t *binding =
      find_direct_compound_literal_binding(context, compound);
  if (!binding ||
      (!binding->local_object && !binding->global_object))
    return NULL;

  psx_semantic_node_t *initialization = NULL;
  if (binding->local_object) {
    initialization = binding->character_array_initializer.units
        ? build_direct_character_array_initializer(
              context, binding->local_object,
              &binding->character_array_initializer,
              ND_COMPOUND_LITERAL)
        : build_direct_flat_initializer(
              context, binding->local_object,
              binding->plan.object_qual_type,
              &binding->flat_initializer,
              ND_COMPOUND_LITERAL);
  }
  psx_semantic_node_t *object = binding->local_object
      ? build_direct_local_reference(
            context, binding->local_object,
            binding->plan.object_qual_type, 0, 0, 0, 0,
            ND_COMPOUND_LITERAL)
      : build_direct_global_reference(
            context, binding->global_object,
            binding->plan.object_qual_type,
            ND_COMPOUND_LITERAL);
  if ((binding->local_object && !initialization) || !object)
    return NULL;

  psx_qual_type_t result_qual_type =
      binding->plan.object_qual_type;
  if (force_address_result)
    result_qual_type = psx_resolve_address_result_qual_type_in(
        context->semantic_context,
        binding->plan.object_qual_type);
  if (result_qual_type.type_id == PSX_TYPE_ID_INVALID) return NULL;

  psx_semantic_node_t *value = object;
  if (force_address_result) {
    psx_semantic_node_t *address_children[] = {object};
    psx_hir_edge_kind_t address_edges[] = {PSX_HIR_EDGE_LHS};
    psx_hir_node_spec_t address_spec = {
        .kind = PSX_HIR_ADDRESS,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    value = psx_semantic_node_builder_expression(
        &context->builder, &address_spec,
        result_qual_type,
        address_children, address_edges, 1, NULL,
        ND_COMPOUND_LITERAL);
    if (!value) return NULL;
  }

  if (!initialization) return value;

  psx_semantic_node_t *children[] = {initialization, value};
  psx_hir_edge_kind_t edges[] = {
      PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_STMT_EXPR,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  ps_decl_record_lvar_usage_in_region_in(
      context->local_registry, binding->local_object,
      PSX_LVAR_USAGE_INITIALIZED, NULL);
  return psx_semantic_node_builder_expression(
      &context->builder, &spec, result_qual_type,
      children, edges, 2, NULL, ND_COMPOUND_LITERAL);
}

static psx_semantic_node_t *build_direct_compound_literal(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound) {
  return build_direct_compound_literal_value(
      context, compound, 0);
}

static psx_semantic_node_t *build_direct_addressable_compound_literal(
    direct_resolution_context_t *context,
    const node_compound_literal_t *compound) {
  return build_direct_compound_literal_value(
      context, compound, 1);
}

static psx_semantic_node_t *build_direct_local_declaration(
    direct_resolution_context_t *context,
    const node_local_declaration_t *syntax) {
  direct_local_declaration_binding_t *binding =
      find_direct_local_declaration(context, syntax);
  if (!binding) return NULL;
  if (binding->is_semantic_only) {
    psx_hir_node_spec_t nop_spec = {
        .kind = PSX_HIR_NOP,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
    };
    return psx_semantic_node_builder_statement(
        &context->builder, &nop_spec, NULL, NULL, 0,
        ND_LOCAL_DECLARATION);
  }
  if (binding->declarator_count <= 0) return NULL;
  size_t count = (size_t)binding->declarator_count;
  psx_semantic_node_t **children = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      count * sizeof(*children));
  psx_hir_edge_kind_t *edges = arena_alloc_in(
      ps_ctx_arena(context->semantic_context),
      count * sizeof(*edges));
  if (!children || !edges) {
    set_failure(
        context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        &syntax->base);
    return NULL;
  }
  for (size_t i = 0; i < count; i++) {
    direct_local_declarator_binding_t *declarator =
        &binding->declarators[i];
    const psx_parsed_initializer_t *initializer =
        declarator->initializer;
    if (declarator->typedef_bound_capture_count > 0) {
      size_t capture_count =
          (size_t)declarator->typedef_bound_capture_count;
      psx_semantic_node_t **assignments = arena_alloc_in(
          ps_ctx_arena(context->semantic_context),
          capture_count * sizeof(*assignments));
      psx_hir_edge_kind_t *assignment_edges = arena_alloc_in(
          ps_ctx_arena(context->semantic_context),
          capture_count * sizeof(*assignment_edges));
      if (!assignments || !assignment_edges) return NULL;
      for (size_t capture_index = 0;
           capture_index < capture_count; capture_index++) {
        direct_typedef_bound_capture_t *capture =
            &declarator->typedef_bound_captures[capture_index];
        psx_semantic_node_t *target = build_direct_local_reference(
            context, capture->storage, capture->value_qual_type,
            0, 0, 0, 0, ND_LOCAL_DECLARATION);
        psx_semantic_node_t *value = build_direct_expression(
            context, capture->value_syntax);
        psx_semantic_node_t *assignment_children[] = {target, value};
        psx_hir_edge_kind_t edges_for_assignment[] = {
            PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
        psx_hir_node_spec_t assignment_spec = {
            .kind = PSX_HIR_ASSIGN,
            .attached_qual_type = {
                PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        };
        assignments[capture_index] = target && value
            ? psx_semantic_node_builder_expression(
                  &context->builder, &assignment_spec,
                  capture->value_qual_type,
                  assignment_children, edges_for_assignment, 2,
                  NULL, ND_LOCAL_DECLARATION)
            : NULL;
        assignment_edges[capture_index] = PSX_HIR_EDGE_BLOCK_ITEM;
        if (!assignments[capture_index]) return NULL;
        ps_decl_record_lvar_usage_in_region_in(
            context->local_registry, capture->storage,
            PSX_LVAR_USAGE_INITIALIZED, NULL);
      }
      psx_hir_node_spec_t capture_block_spec = {
          .kind = PSX_HIR_BLOCK,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      };
      children[i] = psx_semantic_node_builder_statement(
          &context->builder, &capture_block_spec,
          assignments, assignment_edges, capture_count,
          ND_LOCAL_DECLARATION);
      edges[i] = PSX_HIR_EDGE_BLOCK_ITEM;
      if (!children[i]) return NULL;
      continue;
    }
    if (declarator->is_semantic_only) {
      psx_hir_node_spec_t nop_spec = {
          .kind = PSX_HIR_NOP,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      };
      children[i] = psx_semantic_node_builder_statement(
          &context->builder, &nop_spec, NULL, NULL, 0,
          ND_LOCAL_DECLARATION);
      edges[i] = PSX_HIR_EDGE_BLOCK_ITEM;
      if (!children[i]) return NULL;
      continue;
    }
    psx_semantic_node_t *vla_runtime = NULL;
    if (declarator->vla_runtime_plan) {
      vla_runtime = psx_semantic_node_builder_vla_runtime(
          &context->builder, declarator->vla_runtime_plan,
          ND_LOCAL_DECLARATION);
      if (!vla_runtime) return NULL;
    }
    if (!initializer || !initializer->has_initializer) {
      if (vla_runtime) {
        children[i] = vla_runtime;
      } else {
        psx_hir_node_spec_t nop_spec = {
            .kind = PSX_HIR_NOP,
            .attached_qual_type = {
                PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        };
        children[i] = psx_semantic_node_builder_statement(
            &context->builder, &nop_spec, NULL, NULL, 0,
            ND_LOCAL_DECLARATION);
      }
      edges[i] = PSX_HIR_EDGE_BLOCK_ITEM;
      if (!children[i]) return NULL;
      continue;
    }

    const psx_character_array_initializer_plan_t *character_plan =
        &declarator->character_array_initializer;
    if (character_plan->units && character_plan->unit_count > 0) {
      children[i] = build_direct_character_array_initializer(
          context, declarator->local, character_plan,
          ND_LOCAL_DECLARATION);
      edges[i] = PSX_HIR_EDGE_BLOCK_ITEM;
      if (!children[i]) return NULL;
      ps_decl_record_lvar_usage_in_region_in(
          context->local_registry, declarator->local,
          PSX_LVAR_USAGE_INITIALIZED, NULL);
      continue;
    }

    if (initializer->kind == PSX_DECL_INIT_LIST) {
      children[i] = build_direct_flat_initializer(
          context, declarator->local,
          declarator->declaration_qual_type,
          &declarator->flat_initializer,
          ND_LOCAL_DECLARATION);
      edges[i] = PSX_HIR_EDGE_BLOCK_ITEM;
      if (!children[i]) return NULL;
      ps_decl_record_lvar_usage_in_region_in(
          context->local_registry, declarator->local,
          PSX_LVAR_USAGE_INITIALIZED, NULL);
      continue;
    }

    psx_semantic_node_t *target = build_direct_declared_local(
        context, declarator, ND_IDENTIFIER);
    if (!target) {
      set_failure(
          context->failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          &syntax->base);
      return NULL;
    }
    psx_semantic_node_t *value = build_direct_expression(
        context, initializer->value);
    if (!target || !value) return NULL;
    psx_semantic_node_t *assignment_children[] = {target, value};
    psx_hir_edge_kind_t assignment_edges[] = {
        PSX_HIR_EDGE_LHS, PSX_HIR_EDGE_RHS};
    psx_hir_node_spec_t assignment_spec = {
        .kind = PSX_HIR_ASSIGN,
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .is_declaration_initializer = 1,
    };
    assignment_spec.kind = declarator->is_object_copy_initializer
                               ? PSX_HIR_OBJECT_COPY
                               : PSX_HIR_ASSIGN;
    psx_semantic_node_t *assignment =
        psx_semantic_node_builder_expression(
        &context->builder, &assignment_spec,
        (psx_qual_type_t){
            declarator->declaration_qual_type.type_id,
            PSX_TYPE_QUALIFIER_NONE},
        assignment_children, assignment_edges, 2, NULL,
        ND_LOCAL_DECLARATION);
    if (vla_runtime && assignment) {
      psx_semantic_node_t *declaration_children[] = {
          vla_runtime, assignment};
      psx_hir_edge_kind_t declaration_edges[] = {
          PSX_HIR_EDGE_BLOCK_ITEM, PSX_HIR_EDGE_BLOCK_ITEM};
      psx_hir_node_spec_t declaration_block_spec = {
          .kind = PSX_HIR_BLOCK,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      };
      children[i] = psx_semantic_node_builder_statement(
          &context->builder, &declaration_block_spec,
          declaration_children, declaration_edges, 2,
          ND_LOCAL_DECLARATION);
    } else {
      children[i] = assignment;
    }
    edges[i] = PSX_HIR_EDGE_BLOCK_ITEM;
    if (!children[i]) return NULL;
    ps_decl_record_lvar_usage_in_region_in(
        context->local_registry, declarator->local,
        PSX_LVAR_USAGE_INITIALIZED, NULL);
  }
  psx_hir_node_spec_t block_spec = {
      .kind = PSX_HIR_BLOCK,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &block_spec, children, edges, count,
      ND_LOCAL_DECLARATION);
}

static psx_semantic_node_t *build_direct_control_statement(
    direct_resolution_context_t *context,
    const node_ctrl_t *control,
    psx_hir_node_kind_t kind) {
  psx_semantic_node_t *children[3];
  psx_hir_edge_kind_t edges[3];
  size_t count = 0;
  children[count] = build_direct_expression(
      context, control->base.lhs);
  edges[count++] = PSX_HIR_EDGE_LHS;
  children[count] = build_direct_statement(
      context, control->base.rhs);
  edges[count++] = PSX_HIR_EDGE_RHS;
  if (!children[0] || !children[1]) return NULL;
  if (kind == PSX_HIR_IF && control->els) {
    children[count] = build_direct_statement(context, control->els);
    edges[count++] = PSX_HIR_EDGE_ELSE;
    if (!children[count - 1]) return NULL;
  }
  psx_hir_node_spec_t spec = {
      .kind = kind,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &spec, children, edges, count,
      control->base.kind);
}

static psx_semantic_node_t *build_direct_for_statement(
    direct_resolution_context_t *context,
    const node_ctrl_t *control) {
  psx_semantic_node_t *children[4];
  psx_hir_edge_kind_t edges[4];
  size_t count = 0;
  if (control->init) {
    children[count] = build_direct_statement(
        context, control->init);
    edges[count++] = PSX_HIR_EDGE_INIT;
  }
  if (control->base.lhs) {
    children[count] = build_direct_expression(
        context, control->base.lhs);
    edges[count++] = PSX_HIR_EDGE_LHS;
  }
  if (control->inc) {
    children[count] = build_direct_expression(
        context, control->inc);
    edges[count++] = PSX_HIR_EDGE_INCREMENT;
  }
  children[count] = build_direct_statement(
      context, control->base.rhs);
  edges[count++] = PSX_HIR_EDGE_RHS;
  for (size_t i = 0; i < count; i++) {
    if (!children[i]) return NULL;
  }
  psx_hir_node_spec_t spec = {
      .kind = PSX_HIR_FOR,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &spec, children, edges, count,
      control->base.kind);
}

static psx_semantic_node_t *build_direct_labeled_statement(
    direct_resolution_context_t *context,
    const node_t *syntax, psx_hir_node_kind_t kind) {
  psx_semantic_node_t *body = syntax->rhs
      ? build_direct_statement(context, syntax->rhs) : NULL;
  psx_semantic_node_t *children[] = {body};
  psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_RHS};
  psx_hir_node_spec_t spec = {
      .kind = kind,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
  };
  if (kind == PSX_HIR_CASE &&
      !direct_case_value(
          context, (const node_case_t *)syntax,
          &spec.integer_value))
    return NULL;
  return psx_semantic_node_builder_statement(
      &context->builder, &spec,
      body ? children : NULL, body ? edges : NULL,
      body ? 1 : 0, syntax->kind);
}

static psx_semantic_node_t *build_direct_jump_statement(
    direct_resolution_context_t *context,
    const node_jump_t *jump, psx_hir_node_kind_t kind) {
  psx_semantic_node_t *body = kind == PSX_HIR_LABEL && jump->base.rhs
      ? build_direct_statement(context, jump->base.rhs) : NULL;
  psx_semantic_node_t *children[] = {body};
  psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_RHS};
  psx_hir_node_spec_t spec = {
      .kind = kind,
      .attached_qual_type = {
          PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      .name = jump->name,
      .name_length = jump->name_len > 0
                         ? (size_t)jump->name_len : 0,
  };
  return psx_semantic_node_builder_statement(
      &context->builder, &spec,
      body ? children : NULL, body ? edges : NULL,
      body ? 1 : 0, jump->base.kind);
}

static psx_semantic_node_t *build_direct_statement(
    direct_resolution_context_t *context,
    const node_t *syntax) {
  if (!context || !syntax) return NULL;
  switch (syntax->kind) {
    case ND_BLOCK:
      return build_direct_block(
          context, (const node_block_t *)syntax);
    case ND_LOCAL_DECLARATION:
      return build_direct_local_declaration(
          context, (const node_local_declaration_t *)syntax);
    case ND_NULL_STMT: {
      psx_hir_node_spec_t spec = {
          .kind = PSX_HIR_NOP,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      };
      return psx_semantic_node_builder_statement(
          &context->builder, &spec, NULL, NULL, 0,
          syntax->kind);
    }
    case ND_STATIC_ASSERT: {
      psx_hir_node_spec_t spec = {
          .kind = PSX_HIR_NOP,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      };
      return psx_semantic_node_builder_statement(
          &context->builder, &spec, NULL, NULL, 0,
          syntax->kind);
    }
    case ND_RETURN: {
      psx_semantic_node_t *value = syntax->lhs
          ? build_direct_expression(context, syntax->lhs) : NULL;
      psx_semantic_node_t *children[] = {value};
      psx_hir_edge_kind_t edges[] = {PSX_HIR_EDGE_LHS};
      psx_hir_node_spec_t spec = {
          .kind = PSX_HIR_RETURN,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      };
      return psx_semantic_node_builder_statement(
          &context->builder, &spec,
          value ? children : NULL, value ? edges : NULL,
          value ? 1 : 0, syntax->kind);
    }
    case ND_IF:
      return build_direct_control_statement(
          context, (const node_ctrl_t *)syntax, PSX_HIR_IF);
    case ND_WHILE:
      return build_direct_control_statement(
          context, (const node_ctrl_t *)syntax, PSX_HIR_WHILE);
    case ND_DO_WHILE:
      return build_direct_control_statement(
          context, (const node_ctrl_t *)syntax, PSX_HIR_DO_WHILE);
    case ND_FOR:
      return build_direct_for_statement(
          context, (const node_ctrl_t *)syntax);
    case ND_SWITCH:
      return build_direct_control_statement(
          context, (const node_ctrl_t *)syntax, PSX_HIR_SWITCH);
    case ND_CASE:
      return build_direct_labeled_statement(
          context, syntax, PSX_HIR_CASE);
    case ND_DEFAULT:
      return build_direct_labeled_statement(
          context, syntax, PSX_HIR_DEFAULT);
    case ND_GOTO:
      return build_direct_jump_statement(
          context, (const node_jump_t *)syntax, PSX_HIR_GOTO);
    case ND_LABEL:
      return build_direct_jump_statement(
          context, (const node_jump_t *)syntax, PSX_HIR_LABEL);
    case ND_BREAK:
    case ND_CONTINUE: {
      psx_hir_node_spec_t spec = {
          .kind = syntax->kind == ND_BREAK
                      ? PSX_HIR_BREAK : PSX_HIR_CONTINUE,
          .attached_qual_type = {
              PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
      };
      return psx_semantic_node_builder_statement(
          &context->builder, &spec, NULL, NULL, 0, syntax->kind);
    }
    default:
      return build_direct_expression(context, syntax);
  }
}

static psx_syntax_typed_hir_resolution_status_t
resolve_syntax_expression_direct_to_typed_hir(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_local_lookup_point_t *lookup_point,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_syntax_integer_constant_result_t *constant_result,
    psx_resolved_hir_build_failure_t *failure) {
  if (typed_hir) *typed_hir = NULL;
  if (constant_result)
    *constant_result = (psx_syntax_integer_constant_result_t){0};
  if (failure) {
    memset(failure, 0, sizeof(*failure));
    failure->source_node_kind = PSX_SYNTAX_NODE_INVALID;
  }
  if (!semantic_context || !global_registry || !local_registry ||
      !syntax_expression || !typed_hir) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_expression);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }

  psx_global_registry_checkpoint_t global_checkpoint = {0};
  psx_local_registry_checkpoint_t local_checkpoint = {0};
  psx_lowering_context_checkpoint_t lowering_checkpoint = {0};
  int global_transaction_active =
      psx_global_registry_checkpoint_is_active(global_registry);
  int local_transaction_active =
      psx_local_registry_checkpoint_is_active(local_registry);
  if (lowering_context && local_transaction_active &&
      !global_transaction_active) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_expression);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  int transactional = lowering_context != NULL &&
      !global_transaction_active && !local_transaction_active;
  if (transactional) {
    if (!psx_global_registry_checkpoint_begin(
            global_registry, &global_checkpoint)) {
      set_failure(
          failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          syntax_expression);
      return PSX_SYNTAX_TYPED_HIR_FAILED;
    }
    if (!psx_local_registry_checkpoint_begin(
            local_registry, &local_checkpoint)) {
      psx_global_registry_checkpoint_rollback(
          global_registry, &global_checkpoint);
      set_failure(
          failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          syntax_expression);
      return PSX_SYNTAX_TYPED_HIR_FAILED;
    }
    psx_lowering_context_checkpoint(
        lowering_context, &lowering_checkpoint);
  }
  if (lowering_context && !options) {
    if (transactional) {
      psx_global_registry_checkpoint_rollback(
          global_registry, &global_checkpoint);
      psx_local_registry_checkpoint_rollback(
          local_registry, &local_checkpoint);
      psx_lowering_context_rollback(
          lowering_context, &lowering_checkpoint);
    }
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_expression);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }

  direct_resolution_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .options = options,
      .failure = failure,
      .identifier_lookup_point = lookup_point,
  };
  psx_semantic_node_builder_init(
      &context.builder, ps_ctx_arena(semantic_context),
      semantic_context, failure);

  psx_semantic_node_t *root = NULL;
  if (syntax_expression->kind == ND_STRING) {
    root = build_direct_literal(&context, syntax_expression);
  } else {
    psx_qual_type_t preflight_type;
    if (!preflight_direct_expression(
            &context, syntax_expression, &preflight_type)) {
      psx_syntax_typed_hir_resolution_status_t status =
          context.preflight_failed
              ? PSX_SYNTAX_TYPED_HIR_FAILED
              : PSX_SYNTAX_TYPED_HIR_REJECTED;
      if (transactional) {
        psx_global_registry_checkpoint_rollback(
            global_registry, &global_checkpoint);
        psx_local_registry_checkpoint_rollback(
            local_registry, &local_checkpoint);
        psx_lowering_context_rollback(
            lowering_context, &lowering_checkpoint);
      }
      return status;
    }
    if (constant_result) {
      const psx_type_t *canonical = ps_ctx_type_by_id_in(
          semantic_context, preflight_type.type_id);
      long long value = 0;
      if (canonical &&
          (canonical->kind == PSX_TYPE_BOOL ||
           canonical->kind == PSX_TYPE_INTEGER) &&
          direct_integer_constant(
              &context, syntax_expression, &value)) {
        constant_result->value = value;
        constant_result->is_constant = 1;
      }
    }
    root = build_direct_expression(&context, syntax_expression);
  }

  psx_typed_hir_tree_t *tree = wrap_typed_root(
      semantic_context, root, syntax_expression, failure);
  if (!tree) {
    if (transactional) {
      psx_global_registry_checkpoint_rollback(
          global_registry, &global_checkpoint);
      psx_local_registry_checkpoint_rollback(
          local_registry, &local_checkpoint);
      psx_lowering_context_rollback(
          lowering_context, &lowering_checkpoint);
    }
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  record_direct_identifier_usage(&context);
  *typed_hir = tree;
  if (transactional) {
    psx_global_registry_checkpoint_commit(
        global_registry, &global_checkpoint);
    psx_local_registry_checkpoint_commit(
        local_registry, &local_checkpoint);
  }
  return PSX_SYNTAX_TYPED_HIR_RESOLVED;
}

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_expression_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure) {
  return resolve_syntax_expression_direct_to_typed_hir(
      semantic_context, global_registry, local_registry,
      NULL, NULL, NULL, syntax_expression, typed_hir, NULL, failure);
}

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_integer_constant_expression_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const psx_local_lookup_point_t *lookup_point,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_syntax_integer_constant_result_t *constant_result,
    psx_resolved_hir_build_failure_t *failure) {
  if (!constant_result) {
    if (typed_hir) *typed_hir = NULL;
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_expression);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  return resolve_syntax_expression_direct_to_typed_hir(
      semantic_context, global_registry, local_registry,
      NULL, NULL, lookup_point, syntax_expression, typed_hir,
      constant_result, failure);
}

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_expression_direct_to_typed_hir_with_lowering_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_expression,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure) {
  return resolve_syntax_expression_direct_to_typed_hir(
      semantic_context, global_registry, local_registry,
      lowering_context, options, NULL, syntax_expression, typed_hir,
      NULL, failure);
}

static psx_syntax_typed_hir_resolution_status_t
resolve_syntax_initializer_direct_to_typed_hir(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_initializer,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure) {
  if (typed_hir) *typed_hir = NULL;
  if (failure) {
    memset(failure, 0, sizeof(*failure));
    failure->source_node_kind = PSX_SYNTAX_NODE_INVALID;
  }
  if (!semantic_context || !global_registry || !local_registry ||
      !syntax_initializer || !typed_hir) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_initializer);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }

  psx_global_registry_checkpoint_t global_checkpoint = {0};
  psx_lowering_context_checkpoint_t lowering_checkpoint = {0};
  int transactional = lowering_context != NULL &&
      !psx_global_registry_checkpoint_is_active(global_registry);
  if (transactional) {
    if (!psx_global_registry_checkpoint_begin(
            global_registry, &global_checkpoint)) {
      set_failure(
          failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          syntax_initializer);
      return PSX_SYNTAX_TYPED_HIR_FAILED;
    }
    psx_lowering_context_checkpoint(
        lowering_context, &lowering_checkpoint);
  }
  if (lowering_context && !options) {
    if (transactional)
      psx_global_registry_checkpoint_rollback(
          global_registry, &global_checkpoint);
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_initializer);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }

  direct_resolution_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .options = options,
      .failure = failure,
  };
  psx_semantic_node_builder_init(
      &context.builder, ps_ctx_arena(semantic_context),
      semantic_context, failure);
  if (!preflight_direct_initializer(&context, syntax_initializer)) {
    psx_syntax_typed_hir_resolution_status_t status =
        context.preflight_failed
            ? PSX_SYNTAX_TYPED_HIR_FAILED
            : PSX_SYNTAX_TYPED_HIR_REJECTED;
    if (transactional) {
      psx_global_registry_checkpoint_rollback(
          global_registry, &global_checkpoint);
      psx_lowering_context_rollback(
          lowering_context, &lowering_checkpoint);
    }
    return status;
  }
  psx_semantic_node_t *root = build_direct_initializer(
      &context, syntax_initializer);
  psx_typed_hir_tree_t *tree = wrap_typed_root(
      semantic_context, root, syntax_initializer, failure);
  if (!tree) {
    if (transactional) {
      psx_global_registry_checkpoint_rollback(
          global_registry, &global_checkpoint);
      psx_lowering_context_rollback(
          lowering_context, &lowering_checkpoint);
    }
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  record_direct_identifier_usage(&context);
  *typed_hir = tree;
  if (transactional)
    psx_global_registry_checkpoint_commit(
        global_registry, &global_checkpoint);
  return PSX_SYNTAX_TYPED_HIR_RESOLVED;
}

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_initializer_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_initializer,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure) {
  return resolve_syntax_initializer_direct_to_typed_hir(
      semantic_context, global_registry, local_registry,
      NULL, NULL, syntax_initializer, typed_hir, failure);
}

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_initializer_direct_to_typed_hir_with_lowering_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const node_t *syntax_initializer,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure) {
  return resolve_syntax_initializer_direct_to_typed_hir(
      semantic_context, global_registry, local_registry,
      lowering_context, options, syntax_initializer, typed_hir,
      failure);
}

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_statement_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    const node_t *syntax_statement,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure) {
  if (typed_hir) *typed_hir = NULL;
  if (failure) {
    memset(failure, 0, sizeof(*failure));
    failure->source_node_kind = PSX_SYNTAX_NODE_INVALID;
  }
  if (!semantic_context || !global_registry || !local_registry ||
      !syntax_statement || !typed_hir) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_statement);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }

  direct_resolution_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .failure = failure,
      .label_declaration_start = psx_scope_graph_declaration_count(
          ps_ctx_scope_graph(semantic_context)),
  };
  psx_semantic_node_builder_init(
      &context.builder, ps_ctx_arena(semantic_context),
      semantic_context, failure);
  if (!collect_direct_function_jumps(&context, syntax_statement) ||
      !validate_direct_function_jumps(&context) ||
      !preflight_direct_statement(&context, syntax_statement)) {
    psx_syntax_typed_hir_resolution_status_t status =
        context.preflight_failed
            ? PSX_SYNTAX_TYPED_HIR_FAILED
            : PSX_SYNTAX_TYPED_HIR_REJECTED;
    forget_direct_label_declarations(&context);
    return status;
  }

  psx_semantic_node_t *root =
      build_direct_statement(&context, syntax_statement);
  psx_typed_hir_tree_t *tree = wrap_typed_root(
      semantic_context, root, syntax_statement, failure);
  if (!tree) {
    forget_direct_label_declarations(&context);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  record_direct_identifier_usage(&context);
  *typed_hir = tree;
  forget_direct_label_declarations(&context);
  return PSX_SYNTAX_TYPED_HIR_RESOLVED;
}

static int begin_direct_function_transaction(
    direct_function_transaction_t *transaction,
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const token_ident_t *function_name) {
  if (!transaction || !semantic_context || !global_registry ||
      !local_registry || !lowering_context || !function_name)
    return 0;
  *transaction = (direct_function_transaction_t){
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .function_name = function_name,
  };
  ps_ctx_checkpoint_function_registration_in(
      semantic_context, function_name->str, function_name->len,
      &transaction->function_checkpoint);
  if (!psx_global_registry_checkpoint_begin(
          global_registry, &transaction->global_checkpoint))
    return 0;
  if (!psx_local_registry_checkpoint_begin(
          local_registry, &transaction->local_checkpoint)) {
    psx_global_registry_checkpoint_rollback(
        global_registry, &transaction->global_checkpoint);
    return 0;
  }
  psx_lowering_context_checkpoint(
      lowering_context, &transaction->lowering_checkpoint);
  return 1;
}

static void rollback_direct_function_resolution(
    direct_function_transaction_t *transaction,
    direct_function_declaration_checkpoint_t *function_declarations,
    int rollback_function_registration) {
  if (!transaction) return;
  psx_global_registry_checkpoint_rollback(
      transaction->global_registry, &transaction->global_checkpoint);
  psx_local_registry_checkpoint_rollback(
      transaction->local_registry, &transaction->local_checkpoint);
  psx_lowering_context_rollback(
      transaction->lowering_context, &transaction->lowering_checkpoint);
  for (direct_function_declaration_checkpoint_t *declaration =
           function_declarations;
       declaration; declaration = declaration->next) {
    ps_ctx_rollback_function_registration_in(
        transaction->semantic_context,
        declaration->name, declaration->name_len,
        &declaration->checkpoint);
  }
  if (rollback_function_registration)
    ps_ctx_rollback_function_registration_in(
        transaction->semantic_context,
        transaction->function_name->str,
        transaction->function_name->len,
        &transaction->function_checkpoint);
}

static void commit_direct_function_resolution(
    direct_function_transaction_t *transaction) {
  if (!transaction) return;
  psx_global_registry_checkpoint_commit(
      transaction->global_registry, &transaction->global_checkpoint);
  psx_local_registry_checkpoint_commit(
      transaction->local_registry, &transaction->local_checkpoint);
}

psx_syntax_typed_hir_resolution_status_t
psx_resolve_syntax_function_direct_to_typed_hir_in_contexts(
    psx_semantic_context_t *semantic_context,
    psx_global_registry_t *global_registry,
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    const ag_compilation_options_t *options,
    const psx_parsed_function_definition_t *syntax_function,
    const psx_typed_hir_tree_t **typed_hir,
    psx_resolved_hir_build_failure_t *failure) {
  if (typed_hir) *typed_hir = NULL;
  if (failure) {
    memset(failure, 0, sizeof(*failure));
    failure->source_node_kind = PSX_SYNTAX_NODE_INVALID;
  }
  if (!semantic_context || !global_registry || !local_registry ||
      !lowering_context || !options || !syntax_function ||
      !syntax_function->body || !syntax_function->declarator.identifier ||
      !typed_hir) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_INVALID_INPUT,
        syntax_function ? syntax_function->body : NULL);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }

  token_ident_t *name = syntax_function->declarator.identifier;
  direct_function_transaction_t transaction;
  if (!begin_direct_function_transaction(
          &transaction, semantic_context, global_registry, local_registry,
          lowering_context, name)) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
        syntax_function->body);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  psx_function_definition_header_resolution_t header;
  if (!psx_resolve_function_definition_header_in_contexts(
          semantic_context, global_registry, local_registry,
          lowering_context, syntax_function, &header)) {
    rollback_direct_function_resolution(&transaction, NULL, 1);
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE,
        syntax_function->body);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }

  psx_qual_type_t return_qual_type =
      psx_semantic_type_table_base(
          ps_ctx_semantic_type_table_in(semantic_context),
          header.signature_qual_type.type_id);
  direct_resolution_context_t context = {
      .semantic_context = semantic_context,
      .global_registry = global_registry,
      .local_registry = local_registry,
      .lowering_context = lowering_context,
      .options = options,
      .failure = failure,
      .function_name = header.name,
      .function_name_len = header.name_len,
      .function_return_qual_type = return_qual_type,
      .enforce_function_return_type = 1,
  };
  psx_semantic_node_builder_init(
      &context.builder, ps_ctx_arena(semantic_context),
      semantic_context, failure);
  if (return_qual_type.type_id == PSX_TYPE_ID_INVALID) {
    set_failure(
        failure, PSX_RESOLVED_HIR_BUILD_MISSING_CANONICAL_TYPE,
        syntax_function->body);
    rollback_direct_function_resolution(
        &transaction, context.function_declarations, 1);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  if (!collect_direct_function_jumps(
          &context, syntax_function->body) ||
      !validate_direct_function_jumps(&context) ||
      !preflight_direct_statement(
          &context, syntax_function->body)) {
    rollback_direct_function_resolution(
        &transaction, context.function_declarations, 1);
    return context.preflight_failed
               ? PSX_SYNTAX_TYPED_HIR_FAILED
               : PSX_SYNTAX_TYPED_HIR_REJECTED;
  }

  psx_semantic_node_t *body = build_direct_statement(
      &context, syntax_function->body);
  size_t child_count = (size_t)header.parameter_count + 1;
  psx_semantic_node_t **children = arena_alloc_in(
      ps_ctx_arena(semantic_context),
      child_count * sizeof(*children));
  psx_hir_edge_kind_t *edges = arena_alloc_in(
      ps_ctx_arena(semantic_context),
      child_count * sizeof(*edges));
  if (!body || !children || !edges) {
    if (failure && failure->status == PSX_RESOLVED_HIR_BUILD_OK)
      set_failure(
          failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          syntax_function->body);
    rollback_direct_function_resolution(
        &transaction, context.function_declarations, 1);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  for (int i = 0; i < header.parameter_count; i++) {
    lvar_t *parameter = header.parameters[i];
    if (!parameter) {
      set_failure(
          failure, PSX_RESOLVED_HIR_BUILD_MISSING_RESOLVED_SYMBOL,
          syntax_function->body);
      rollback_direct_function_resolution(
          &transaction, context.function_declarations, 1);
      return PSX_SYNTAX_TYPED_HIR_FAILED;
    }
    psx_hir_node_spec_t parameter_spec = {
        .attached_qual_type = {
            PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE},
        .name = ps_lvar_name(parameter),
        .name_length = ps_lvar_name_len(parameter) > 0
                           ? (size_t)ps_lvar_name_len(parameter) : 0,
    };
    if (!psx_resolve_local_hir_node_spec_in(
            semantic_context, parameter,
            ps_lvar_offset(parameter), &parameter_spec)) {
      set_failure(
          failure, PSX_RESOLVED_HIR_BUILD_OUT_OF_MEMORY,
          syntax_function->body);
      rollback_direct_function_resolution(
          &transaction, context.function_declarations, 1);
      return PSX_SYNTAX_TYPED_HIR_FAILED;
    }
    children[i] = psx_semantic_node_builder_leaf_expression(
        &context.builder, &parameter_spec,
        ps_lvar_decl_qual_type(parameter), NULL, ND_IDENTIFIER);
    edges[i] = PSX_HIR_EDGE_PARAMETER;
    if (!children[i]) {
      rollback_direct_function_resolution(
          &transaction, context.function_declarations, 1);
      return PSX_SYNTAX_TYPED_HIR_FAILED;
    }
  }
  children[header.parameter_count] = body;
  edges[header.parameter_count] = PSX_HIR_EDGE_FUNCTION_BODY;
  psx_hir_node_spec_t function_spec = {
      .kind = PSX_HIR_FUNCTION,
      .attached_qual_type = header.signature_qual_type,
      .name = header.name,
      .name_length = header.name_len > 0
                         ? (size_t)header.name_len : 0,
      .is_static_function = header.is_static ? 1 : 0,
  };
  psx_semantic_node_t *function =
      psx_semantic_node_builder_statement(
          &context.builder, &function_spec, children, edges,
          child_count, ND_FUNCDEF);
  psx_typed_hir_tree_t *tree = wrap_typed_root(
      semantic_context, function, syntax_function->body, failure);
  if (!tree) {
    rollback_direct_function_resolution(
        &transaction, context.function_declarations, 1);
    return PSX_SYNTAX_TYPED_HIR_FAILED;
  }
  record_direct_identifier_usage(&context);
  commit_direct_function_resolution(&transaction);
  *typed_hir = tree;
  return PSX_SYNTAX_TYPED_HIR_RESOLVED;
}

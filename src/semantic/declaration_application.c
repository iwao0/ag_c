#include "declaration_application.h"

#include "../parser/alignas_value.h"
#include "../parser/diag.h"
#include "../parser/enum_const.h"
#include "../parser/expr.h"
#include "../parser/node_utils.h"
#include "../parser/semantic_ctx.h"
#include "../parser/struct_layout.h"
#include "aggregate_member_resolution.h"
#include "declaration_resolution.h"
#include "constant_expression.h"
#include "enum_constant_resolution.h"
#include "function_parameter_resolution.h"
#include "static_assert_resolution.h"
#include "tag_declaration_resolution.h"
#include "typedef_declaration_resolution.h"
#include "../diag/diag.h"
#include "../diag/error_catalog.h"
#include "../tokenizer/tokenizer.h"

void psx_apply_parsed_typedef_declaration(
    char *name, int name_len, const psx_type_t *type, token_t *diag_tok) {
  psx_typedef_declaration_resolution_t resolution;
  psx_resolve_typedef_declaration(
      &(psx_typedef_declaration_resolution_request_t){
          .name = name,
          .name_len = name_len,
          .type = type,
      },
      &resolution);
  if (resolution.status == PSX_TYPEDEF_DECLARATION_OK) return;
  if (resolution.status == PSX_TYPEDEF_DECLARATION_TYPE_CONFLICT) {
    ps_diag_ctx(diag_tok, "typedef",
                 "typedef名 '%.*s' の型が以前の宣言と異なります (C11 6.7p3)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_OBJECT_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "typedef",
                 "'%.*s' はオブジェクトとして既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_FUNCTION_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "typedef",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_TYPEDEF_DECLARATION_ENUM_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "typedef",
                 "'%.*s' はenum定数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  ps_diag_ctx(diag_tok, "typedef",
               "canonical typedef declaration resolution failed for '%.*s'",
               name_len, name);
}

void psx_apply_parsed_enum_constant(
    char *name, int name_len, long long value, token_t *diag_tok) {
  psx_enum_constant_resolution_t resolution;
  psx_resolve_enum_constant(
      &(psx_enum_constant_resolution_request_t){
          .name = name,
          .name_len = name_len,
          .value = value,
      },
      &resolution);
  if (resolution.status == PSX_ENUM_CONSTANT_OK) return;
  if (resolution.status == PSX_ENUM_CONSTANT_DUPLICATE) {
    psx_diag_duplicate_with_name(
        diag_tok, "enum constant", name, name_len);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_OBJECT_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "enum",
                 "'%.*s' はオブジェクトとして既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_FUNCTION_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "enum",
                 "'%.*s' は関数として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  if (resolution.status == PSX_ENUM_CONSTANT_TYPEDEF_NAME_CONFLICT) {
    ps_diag_ctx(diag_tok, "enum",
                 "'%.*s' はtypedef名として既に宣言されています (C11 6.7p4)",
                 name_len, name);
  }
  ps_diag_ctx(diag_tok, "enum",
               "canonical enum constant resolution failed for '%.*s'",
               name_len, name);
}

void psx_apply_parsed_tag_declaration(
    token_kind_t kind, char *name, int name_len,
    psx_tag_declaration_mode_t mode, int member_count,
    int size, int alignment, token_t *diag_tok) {
  psx_tag_declaration_resolution_t resolution;
  psx_resolve_tag_declaration(
      &(psx_tag_declaration_resolution_request_t){
          .kind = kind,
          .name = name,
          .name_len = name_len,
          .mode = mode,
          .member_count = member_count,
          .size = size,
          .alignment = alignment,
      },
      &resolution);
  if (resolution.status == PSX_TAG_DECLARATION_OK) return;
  if (resolution.status == PSX_TAG_DECLARATION_REDEFINITION) {
    ps_diag_ctx(diag_tok, "tag",
                 "タグ '%.*s' は同一スコープで再定義されています (C11 6.7.2)",
                 name_len, name);
  }
  if (resolution.status == PSX_TAG_DECLARATION_KIND_CONFLICT) {
    ps_diag_ctx(diag_tok, "tag",
                 "タグ '%.*s' は同一スコープで異なる種類として宣言されています (C11 6.7.2.3)",
                 name_len, name);
  }
  ps_diag_ctx(diag_tok, "tag",
               "canonical tag declaration resolution failed for '%.*s'",
               name_len, name);
}

int psx_apply_aggregate_member_declaration(
    psx_aggregate_layout_state_t *layout,
    const psx_aggregate_member_declaration_request_t *request,
    token_t *diag_tok) {
  psx_aggregate_member_declaration_resolution_t resolution;
  psx_resolve_aggregate_member_declaration(layout, request, &resolution);
  if (resolution.status == PSX_AGGREGATE_MEMBER_OK)
    return resolution.registered_member_count;
  if (resolution.status == PSX_AGGREGATE_MEMBER_MISSING_NAME) {
    psx_diag_missing(diag_tok, diag_text_for(DIAG_TEXT_MEMBER_NAME));
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_INCOMPLETE_TYPE) {
    ps_diag_ctx(diag_tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_INCOMPLETE_MEMBER_FORBIDDEN));
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_FUNCTION_TYPE) {
    ps_diag_ctx(diag_tok, "decl", "%s",
                 diag_message_for(
                     DIAG_ERR_PARSER_FUNCTION_MEMBER_FORBIDDEN));
  }
  if (resolution.status ==
      PSX_AGGREGATE_MEMBER_BIT_WIDTH_EXCEEDS_STORAGE) {
    ps_diag_ctx(diag_tok, "member",
                 "bit-field width %d exceeds its %d-bit storage type",
                 request ? request->bit_width : 0,
                 resolution.storage_size * 8);
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_INVALID_BITFIELD_TYPE) {
    ps_diag_ctx(diag_tok, "member",
                 "bit-field has non-integer canonical type");
  }
  if (resolution.status == PSX_AGGREGATE_MEMBER_DUPLICATE) {
    ps_diag_ctx(
        diag_tok, "member",
        "メンバ '%.*s' は同じaggregate内で重複しています (C11 6.7.2.1)",
        resolution.conflicting_name_len,
        resolution.conflicting_name ? resolution.conflicting_name : "");
  }
  ps_diag_ctx(diag_tok, "member",
               "aggregate member declaration resolution failed");
  return 0;
}

void psx_apply_static_assert(node_t *condition, token_t *diag_tok) {
  if (!condition) return;
  int is_constant = 1;
  long long value = psx_eval_const_int(condition, &is_constant);
  psx_static_assert_resolution_t resolution;
  psx_resolve_static_assert(
      &(psx_static_assert_request_t){
          .is_constant = is_constant,
          .value = value,
      },
      &resolution);
  if (resolution.status == PSX_STATIC_ASSERT_NOT_CONSTANT) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST,
                   diag_tok, "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_STATIC_ASSERT_COND_NOT_CONST));
  }
  if (resolution.status == PSX_STATIC_ASSERT_FAILED) {
    diag_emit_tokf(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED,
                   diag_tok, "%s",
                   diag_message_for(DIAG_ERR_PARSER_STATIC_ASSERT_FAILED));
  }
}

static void apply_decl_tag_action(
    const psx_parsed_tag_action_t *action, void *context) {
  (void)context;
  if (!action || action->action == PSX_PARSED_TAG_NONE) return;
  psx_apply_parsed_tag_declaration(
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_REFERENCE, 0, 0, 0,
      action->diagnostic_token);
  if (action->action != PSX_PARSED_TAG_DEFINITION) return;

  int member_count = 0;
  int size = 0;
  int alignment = 0;
  if (action->kind == TK_ENUM) {
    member_count = ps_apply_parsed_enum_body(action->enum_body);
    size = 4;
    alignment = 4;
  } else {
    member_count = ps_apply_parsed_aggregate_body_layout(
        action->aggregate_body, action->kind,
        action->name, action->name_len, &size, &alignment);
  }
  psx_apply_parsed_tag_declaration(
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_DEFINITION, member_count, size, alignment,
      action->diagnostic_token);
}

static const psx_decl_syntax_resolution_context_t
    parser_decl_resolution_context = {
        .apply_tag_action = apply_decl_tag_action,
        .context = NULL,
};

psx_type_t *psx_apply_parsed_type_name(
    const psx_parsed_type_name_t *type_name) {
  if (!type_name) return NULL;
  psx_type_t *base_type = NULL;
  if (type_name->atomic_inner) {
    base_type = psx_apply_parsed_type_name(type_name->atomic_inner);
    if (!base_type) return NULL;
    base_type->is_atomic = 1;
  } else {
    base_type = psx_apply_parsed_decl_specifier(&type_name->specifier);
    if (!base_type) return NULL;
  }

  psx_declarator_shape_t shape;
  ps_declarator_shape_init(&shape);
  psx_apply_parsed_declarator(&type_name->declarator, &shape, NULL);
  return psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_decl_type = base_type,
          .declarator_shape = &shape,
      });
}

psx_type_t *psx_apply_parsed_declarator_type(
    const psx_type_t *base_type,
    const psx_parsed_declarator_t *declarator) {
  if (!base_type || !declarator) return NULL;
  psx_declarator_shape_t shape;
  ps_declarator_shape_init(&shape);
  psx_apply_parsed_declarator(declarator, &shape, NULL);
  return psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_decl_type = base_type,
          .declarator_shape = &shape,
      });
}

psx_type_t *psx_apply_runtime_declarator_type(
    const psx_type_t *base_type,
    const psx_runtime_declarator_application_t *application) {
  if (!base_type || !application) return NULL;
  return psx_resolve_decl_type(
      &(psx_decl_type_request_t){
          .base_decl_type = base_type,
          .declarator_shape = &application->shape,
      });
}

void psx_parse_declaration_phase_syntax(
    psx_declaration_phase_t *phase,
    const psx_decl_specifier_syntax_options_t *options) {
  if (!phase) return;
  *phase = (psx_declaration_phase_t){0};
  ps_parse_decl_specifier_syntax_ex(&phase->syntax, options);
  phase->state = PSX_DECLARATION_PHASE_SYNTAX;
}

int psx_apply_declaration_phase(
    psx_declaration_phase_t *phase, int standalone_tag) {
  if (!phase || phase->state != PSX_DECLARATION_PHASE_SYNTAX) return 0;
  phase->requested_alignment =
      psx_apply_parsed_decl_alignment(&phase->syntax);
  if (standalone_tag &&
      phase->syntax.source == PSX_PARSED_DECL_TYPE_TAG) {
    psx_apply_parsed_standalone_tag(&phase->syntax);
    phase->state = PSX_DECLARATION_PHASE_STANDALONE_TAG;
    return 1;
  }
  phase->base_type = psx_apply_parsed_decl_specifier(&phase->syntax);
  if (!phase->base_type) return 0;
  phase->state = PSX_DECLARATION_PHASE_RESOLVED_TYPE;
  return 1;
}

void psx_dispose_declaration_phase(psx_declaration_phase_t *phase) {
  if (!phase) return;
  ps_dispose_decl_specifier_syntax(&phase->syntax);
  *phase = (psx_declaration_phase_t){0};
}

psx_type_t *psx_apply_parsed_decl_specifier(
    const psx_parsed_decl_specifier_t *specifier) {
  return psx_resolve_decl_specifier_syntax(
      specifier, &parser_decl_resolution_context);
}

int psx_apply_parsed_decl_alignment(
    const psx_parsed_decl_specifier_t *specifier) {
  if (!specifier) return 0;
  int alignment = 0;
  for (int i = 0; i < specifier->alignas_expression_count; i++) {
    const psx_parsed_const_expr_t *expression =
        &specifier->alignas_expressions[i];
    int value = ps_eval_parsed_alignas_value(
        expression->start, expression->end);
    if (value > alignment) alignment = value;
  }
  return alignment;
}

void psx_apply_parsed_standalone_tag(
    const psx_parsed_decl_specifier_t *specifier) {
  if (!specifier || specifier->source != PSX_PARSED_DECL_TYPE_TAG) return;
  const psx_parsed_tag_action_t *action = &specifier->tag_action;
  if (action->action == PSX_PARSED_TAG_DEFINITION) {
    apply_decl_tag_action(action, NULL);
    return;
  }
  psx_apply_parsed_tag_declaration(
      action->kind, action->name, action->name_len,
      PSX_TAG_DECLARATION_FORWARD, 0, 0, 0,
      action->diagnostic_token);
}

void psx_apply_parsed_declarator(
    const psx_parsed_declarator_t *declarator,
    psx_declarator_shape_t *shape, int *bit_width) {
  psx_resolve_declarator_syntax(
      declarator, shape, bit_width, &parser_decl_resolution_context);
}

static node_t *parse_runtime_array_bound_expression(
    const psx_parsed_const_expr_t *expression) {
  token_t *saved = tk_get_current_token();
  tk_set_current_token(expression->start);
  node_t *node = ps_expr_assign();
  if (tk_get_current_token() != expression->end) {
    ps_diag_ctx(tk_get_current_token(), "declarator-resolution",
                 "runtime array bound was not fully consumed");
  }
  tk_set_current_token(saved);
  return node;
}

void psx_apply_runtime_parsed_declarator(
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application) {
  psx_apply_runtime_parsed_declarator_ex(declarator, application, -1);
}

void psx_apply_runtime_parsed_declarator_ex(
    const psx_parsed_declarator_t *declarator,
    psx_runtime_declarator_application_t *application,
    int skipped_function_op_index) {
  if (!declarator || !application) return;
  *application = (psx_runtime_declarator_application_t){
      .shape = declarator->declarator_shape,
  };
  for (int i = 0; i < declarator->array_bound_count; i++) {
    const psx_parsed_array_bound_t *parsed = &declarator->array_bounds[i];
    if (parsed->declarator_op_index < 0 ||
        parsed->declarator_op_index >= application->shape.count ||
        application->shape.ops[parsed->declarator_op_index].kind !=
            PSX_DECL_OP_ARRAY) {
      ps_diag_ctx(parsed->expression.start, "declarator-resolution",
                   "invalid local array bound target");
    }
    node_t *expression = parse_runtime_array_bound_expression(
        &parsed->expression);
    int is_constant = 1;
    long long value = psx_eval_const_int(expression, &is_constant);
    if (is_constant && value < 0) {
      ps_diag_ctx(parsed->expression.start, "decl", "%s",
                   diag_message_for(
                       DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
    }
    if (is_constant && value == 0) {
      ps_ctx_record_unsupported_gnu_extension_warning(
          parsed->expression.start, "zero-length array");
    }
    psx_declarator_op_t *op =
        &application->shape.ops[parsed->declarator_op_index];
    op->array_len = is_constant ? (int)value : 0;
    op->is_incomplete_array = 0;
    op->is_vla_array = is_constant ? 0 : 1;
    application->array_bounds[application->array_bound_count++] =
        (psx_runtime_array_bound_t){
            .declarator_op_index = parsed->declarator_op_index,
            .expression = expression,
            .constant_value = is_constant ? value : 0,
            .is_constant = is_constant,
        };
  }
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    const psx_parsed_function_suffix_t *suffix =
        &declarator->function_suffixes[i];
    if (suffix->declarator_op_index == skipped_function_op_index) continue;
    if (suffix->declarator_op_index < 0 ||
        suffix->declarator_op_index >= application->shape.count ||
        application->shape.ops[suffix->declarator_op_index].kind !=
            PSX_DECL_OP_FUNCTION) {
      ps_diag_ctx(declarator->diagnostic_token, "declarator-resolution",
                   "invalid local function suffix target");
    }
    psx_apply_parsed_function_parameters(
        suffix->parameters,
        &application->shape.ops[suffix->declarator_op_index],
        declarator->diagnostic_token);
  }
}

void psx_apply_parsed_function_parameters(
    psx_parsed_function_parameters_t *parameters,
    psx_declarator_op_t *function_op, token_t *diagnostic_token) {
  psx_resolve_function_parameter_types(
      parameters, function_op, diagnostic_token,
      &parser_decl_resolution_context);
}

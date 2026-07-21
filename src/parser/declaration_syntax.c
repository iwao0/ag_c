#include "declaration_syntax.h"
#include "declarator_shape_builder.h"

#include "aggregate_member_syntax.h"
#include "anon_tag.h"
#include "arena.h"
#include "declarator_syntax.h"
#include "diag.h"
#include "dynarray.h"
#include "enum_const.h"
#include "expr.h"
#include "function_parameter_syntax.h"
#include "runtime_context.h"
#include "../diag/diag.h"
#include "../semantic/scope_graph.h"
#include "../tokenizer/tokenizer.h"

#include <stdlib.h>
#include <string.h>

typedef struct declaration_declarator_parse_context_t
    declaration_declarator_parse_context_t;

static tokenizer_context_t *tokenizer(
    psx_parser_runtime_context_t *runtime_context) {
  return ps_parser_runtime_tokenizer(runtime_context);
}

static token_t *current_token(
    psx_parser_runtime_context_t *runtime_context) {
  return tk_get_current_token_ctx(tokenizer(runtime_context));
}

static ag_diagnostic_context_t *diagnostics(
    psx_parser_runtime_context_t *runtime_context) {
  return ps_parser_runtime_diagnostics(runtime_context);
}

static const psx_decl_specifier_syntax_options_t *
complete_decl_specifier_syntax_options(
    const psx_decl_specifier_syntax_options_t *options,
    psx_decl_specifier_syntax_options_t *storage) {
  if (!storage) return NULL;
  if (!options || !options->runtime_context ||
      !tokenizer(options->runtime_context)) {
    return NULL;
  }
  *storage = *options;
  return storage;
}

static void diagnose_type_name_storage_class(
    psx_parser_runtime_context_t *runtime_context, token_t *start) {
  for (token_t *token = start;
       token && psx_is_decl_prefix_token(token->kind);
       token = token->next) {
    if (token->kind == TK_THREAD_LOCAL || token->kind == TK_EXTERN ||
        token->kind == TK_STATIC || token->kind == TK_AUTO ||
        token->kind == TK_REGISTER || token->kind == TK_ALIGNAS ||
        token->kind == TK_INLINE || token->kind == TK_NORETURN) {
      ps_diag_ctx_in(diagnostics(runtime_context),
          token, "cast", "%s",
          diag_message_for_in(diagnostics(runtime_context), DIAG_ERR_PARSER_CAST_STORAGE_CLASS_FORBIDDEN));
    }
  }
  if (start && start->kind == TK_TYPEDEF) {
    ps_diag_ctx_in(diagnostics(runtime_context),
        start, "cast", "%s",
        diag_message_for_in(diagnostics(runtime_context), DIAG_ERR_PARSER_CAST_STORAGE_CLASS_FORBIDDEN));
  }
}

static token_t *find_declaration_expression_end(
    token_t *token, token_kind_t first_end, token_kind_t second_end) {
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  for (token_t *current = token; current; current = current->next) {
    if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 &&
        (current->kind == first_end || current->kind == second_end))
      return current;
    if (current->kind == TK_LPAREN) paren_depth++;
    else if (current->kind == TK_RPAREN) paren_depth--;
    else if (current->kind == TK_LBRACKET) bracket_depth++;
    else if (current->kind == TK_RBRACKET) bracket_depth--;
    else if (current->kind == TK_LBRACE) brace_depth++;
    else if (current->kind == TK_RBRACE) brace_depth--;
  }
  return NULL;
}

static psx_parsed_const_expr_t make_parsed_const_expr(
    token_t *start, token_t *end) {
  psx_parsed_const_expr_t expression = {
      .start = start,
      .end = end,
  };
  if (start && start->kind == TK_IDENT && start->next == end) {
    token_ident_t *identifier = (token_ident_t *)start;
    expression.identifier_name = identifier->str;
    expression.identifier_name_len = identifier->len;
  }
  return expression;
}

typedef struct {
  psx_parsed_decl_specifier_t *specifier;
  const psx_decl_specifier_syntax_options_t *options;
  psx_parser_runtime_context_t *runtime_context;
} declaration_alignas_parse_context_t;

int psx_token_starts_type_name_syntax(
    token_t *token, const psx_name_classifier_t *name_classifier) {
  if (!token) return 0;
  psx_skip_gnu_attributes_at(&token);
  if (!token) return 0;
  if (token->kind == TK_THREAD_LOCAL || token->kind == TK_EXTERN ||
      token->kind == TK_STATIC || token->kind == TK_AUTO ||
      token->kind == TK_REGISTER || token->kind == TK_TYPEDEF ||
      token->kind == TK_ALIGNAS || token->kind == TK_INLINE ||
      token->kind == TK_NORETURN || token->kind == TK_CONST ||
      token->kind == TK_VOLATILE || token->kind == TK_RESTRICT ||
      token->kind == TK_ATOMIC || token->kind == TK_STRUCT ||
      token->kind == TK_UNION || token->kind == TK_ENUM ||
      psx_is_type_specifier_token(token->kind))
    return 1;
  return ps_name_classifier_is_typedef_name(name_classifier, token);
}

static void consume_declaration_alignas(
    void *context, psx_type_spec_result_t *result) {
  (void)result;
  declaration_alignas_parse_context_t *parse_context = context;
  psx_parsed_decl_specifier_t *specifier =
      parse_context ? parse_context->specifier : NULL;
  psx_parser_runtime_context_t *runtime_context =
      parse_context ? parse_context->runtime_context : NULL;
  const psx_decl_specifier_syntax_options_t *options =
      parse_context ? parse_context->options : NULL;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  if (!specifier || !options || specifier->alignas_specifier_count >= 8) {
    ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "declaration-syntax",
                 "declaration alignas limit exceeded");
  }
  tk_set_current_token_ctx(
      tk_ctx, current_token(runtime_context)->next);
  tk_expect_ctx(tk_ctx, '(');
  token_t *start = current_token(runtime_context);
  psx_parsed_alignas_t *alignas =
      &specifier->alignas_specifiers[
          specifier->alignas_specifier_count++];
  *alignas = (psx_parsed_alignas_t){
      .diagnostic_token = start,
      .scope_seq = PSX_SCOPE_ID_INVALID,
  };
  ps_name_classifier_capture_lookup_point(
      options->name_classifier, &alignas->scope_seq,
      &alignas->declaration_seq);
  if (psx_token_starts_type_name_syntax(
          start, options->name_classifier)) {
    alignas->kind = PSX_PARSED_ALIGNAS_TYPE_NAME;
    alignas->type_name = calloc(1, sizeof(*alignas->type_name));
    if (!alignas->type_name ||
        !psx_parse_type_name_syntax_at(
            start, options, alignas->type_name) ||
        !alignas->type_name->end ||
        alignas->type_name->end->kind != TK_RPAREN) {
      ps_diag_ctx_in(
          diagnostics(runtime_context), start, "declaration-syntax",
          "invalid type name in declaration alignas");
    }
    tk_set_current_token_ctx(tk_ctx, alignas->type_name->end);
  } else {
    alignas->kind = PSX_PARSED_ALIGNAS_EXPRESSION;
    if (!options->parse_assignment_expression) {
      ps_diag_ctx_in(
          diagnostics(runtime_context), start, "declaration-syntax",
          "declaration alignas expression parser is unavailable");
    }
    alignas->expression =
        options->parse_assignment_expression(options->expression_context);
    if (!alignas->expression ||
        current_token(runtime_context)->kind != TK_RPAREN) {
      ps_diag_ctx_in(
          diagnostics(runtime_context), start, "declaration-syntax",
          "invalid expression in declaration alignas");
    }
  }
  tk_expect_ctx(tk_ctx, ')');
}

static void diagnose_declarator_too_complex(void *context, token_t *tok);

static psx_parsed_array_bound_t *append_array_bound(
    psx_parsed_declarator_t *declarator,
    psx_parser_runtime_context_t *runtime_context) {
  if (!declarator || declarator->array_bound_count < 0 ||
      declarator->array_bound_capacity < 0 ||
      declarator->array_bound_count > declarator->array_bound_capacity)
    return NULL;
  if (declarator->array_bound_count == declarator->array_bound_capacity) {
    int capacity = pda_next_cap_in(diagnostics(runtime_context),
        declarator->array_bound_capacity,
        declarator->array_bound_count + 1);
    psx_parsed_array_bound_t *bounds =
        arena_alloc_in(
            ps_parser_runtime_arena(runtime_context),
            (size_t)capacity * sizeof(*bounds));
    if (declarator->array_bounds && declarator->array_bound_count > 0) {
      memcpy(bounds, declarator->array_bounds,
             (size_t)declarator->array_bound_count * sizeof(*bounds));
    }
    declarator->array_bounds = bounds;
    declarator->array_bound_capacity = capacity;
  }
  psx_parsed_array_bound_t *bound =
      &declarator->array_bounds[declarator->array_bound_count++];
  *bound = (psx_parsed_array_bound_t){0};
  return bound;
}

static psx_parsed_function_suffix_t *append_function_suffix(
    psx_parsed_declarator_t *declarator,
    psx_parser_runtime_context_t *runtime_context) {
  if (!declarator || declarator->function_suffix_count < 0 ||
      declarator->function_suffix_capacity < 0 ||
      declarator->function_suffix_count > declarator->function_suffix_capacity)
    return NULL;
  if (declarator->function_suffix_count ==
      declarator->function_suffix_capacity) {
    int capacity = pda_next_cap_in(diagnostics(runtime_context),
        declarator->function_suffix_capacity,
        declarator->function_suffix_count + 1);
    psx_parsed_function_suffix_t *suffixes =
        arena_alloc_in(
            ps_parser_runtime_arena(runtime_context),
            (size_t)capacity * sizeof(*suffixes));
    if (declarator->function_suffixes &&
        declarator->function_suffix_count > 0) {
      memcpy(suffixes, declarator->function_suffixes,
             (size_t)declarator->function_suffix_count * sizeof(*suffixes));
    }
    declarator->function_suffixes = suffixes;
    declarator->function_suffix_capacity = capacity;
  }
  psx_parsed_function_suffix_t *suffix =
      &declarator->function_suffixes[declarator->function_suffix_count++];
  *suffix = (psx_parsed_function_suffix_t){0};
  return suffix;
}

static int parse_type_name_syntax_at(
    token_t *start,
    const psx_decl_specifier_syntax_options_t *options,
    int prepare_constant_bounds, psx_parsed_type_name_t *out) {
  if (!start || !out) return 0;
  psx_decl_specifier_syntax_options_t complete_options;
  options = complete_decl_specifier_syntax_options(
      options, &complete_options);
  if (!options) return 0;
  psx_parser_runtime_context_t *runtime_context =
      options->runtime_context;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  *out = (psx_parsed_type_name_t){0};
  token_t *saved = current_token(runtime_context);
  token_t *type_start = start;
  psx_skip_gnu_attributes_at_with_diagnostics(
      &type_start, diagnostics(runtime_context));
  if (!type_start) return 0;

  out->diagnostic_token = type_start;
  diagnose_type_name_storage_class(runtime_context, type_start);
  if (type_start->kind == TK_ATOMIC && type_start->next &&
      type_start->next->kind == TK_LPAREN) {
    out->atomic_inner = calloc(1, sizeof(*out->atomic_inner));
    if (!out->atomic_inner ||
        !parse_type_name_syntax_at(
            type_start->next->next, options, prepare_constant_bounds,
            out->atomic_inner) ||
        !out->atomic_inner->end ||
      out->atomic_inner->end->kind != TK_RPAREN) {
      psx_dispose_type_name_syntax(out);
      tk_set_current_token_ctx(tk_ctx, saved);
      return 0;
    }
    tk_set_current_token_ctx(
        tk_ctx, out->atomic_inner->end->next);
  } else {
    tk_set_current_token_ctx(tk_ctx, type_start);
    psx_parse_decl_specifier_syntax_ex(&out->specifier, options);
    if (out->specifier.source == PSX_PARSED_DECL_TYPE_NONE) {
      psx_dispose_type_name_syntax(out);
      tk_set_current_token_ctx(tk_ctx, saved);
      return 0;
    }
  }

  psx_decl_specifier_syntax_options_t declarator_options = *options;
  declarator_options.name_classifier = NULL;
  out->declarator = psx_parse_abstract_declarator_syntax_tree_in_contexts(
      &declarator_options);
  (void)prepare_constant_bounds;
  out->end = current_token(runtime_context);
  tk_set_current_token_ctx(tk_ctx, saved);
  return 1;
}

int psx_parse_type_name_syntax_at(
    token_t *start,
    const psx_decl_specifier_syntax_options_t *options,
    psx_parsed_type_name_t *out) {
  return parse_type_name_syntax_at(start, options, 1, out);
}

int psx_parse_runtime_type_name_syntax_at(
    token_t *start,
    const psx_decl_specifier_syntax_options_t *options,
    psx_parsed_type_name_t *out) {
  return parse_type_name_syntax_at(start, options, 0, out);
}

void psx_dispose_type_name_syntax(psx_parsed_type_name_t *type_name) {
  if (!type_name) return;
  psx_dispose_declarator_syntax(&type_name->declarator);
  if (type_name->atomic_inner) {
    psx_dispose_type_name_syntax(type_name->atomic_inner);
    free(type_name->atomic_inner);
  } else {
    ps_dispose_decl_specifier_syntax(&type_name->specifier);
  }
  *type_name = (psx_parsed_type_name_t){0};
}

struct declaration_declarator_parse_context_t {
  psx_parsed_declarator_t *declarator;
  const psx_decl_specifier_syntax_options_t *options;
  psx_parser_runtime_context_t *runtime_context;
  const psx_name_classifier_t *name_classifier;
  int allow_implicit_function_parameters;
  int has_syntax_error;
};

static void diagnose_declarator_too_complex(void *context, token_t *tok) {
  declaration_declarator_parse_context_t *parse_context = context;
  psx_parser_runtime_context_t *runtime_context =
      parse_context ? parse_context->runtime_context : NULL;
  if (!runtime_context) return;
  ps_diag_ctx_in(
      diagnostics(runtime_context), tok, "declaration-syntax",
      "declarator is too complex");
}

static int append_declarator_pointer(
    void *context, int is_const, int is_volatile, int is_restrict,
    int nesting_depth) {
  (void)nesting_depth;
  declaration_declarator_parse_context_t *parse_context = context;
  psx_parsed_declarator_t *declarator =
      parse_context ? parse_context->declarator : NULL;
  return declarator && ps_declarator_shape_append_pointer_in(
                           ps_parser_runtime_arena(
                               parse_context->runtime_context),
                           &declarator->declarator_shape,
                           is_const, is_volatile, is_restrict);
}

static int consume_declarator_suffix(
    void *context, int nesting_depth, int direct_was_parenthesized,
    int direct_pointer_count, int frame_pointer_count) {
  (void)nesting_depth;
  (void)direct_was_parenthesized;
  (void)direct_pointer_count;
  (void)frame_pointer_count;
  declaration_declarator_parse_context_t *parse_context = context;
  psx_parsed_declarator_t *declarator =
      parse_context ? parse_context->declarator : NULL;
  if (!declarator) return 0;
  psx_parser_runtime_context_t *runtime_context =
      parse_context->runtime_context;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  if (current_token(runtime_context)->kind == TK_LBRACKET) {
    tk_expect_ctx(tk_ctx, '[');
    int has_static = 0;
    int is_const = 0;
    int is_volatile = 0;
    int is_restrict = 0;
    for (;;) {
      if (current_token(runtime_context)->kind == TK_STATIC) {
        has_static = 1;
      } else if (current_token(runtime_context)->kind == TK_CONST) {
        is_const = 1;
      } else if (current_token(runtime_context)->kind == TK_VOLATILE) {
        is_volatile = 1;
      } else if (current_token(runtime_context)->kind == TK_RESTRICT) {
        is_restrict = 1;
      } else {
        break;
      }
      tk_set_current_token_ctx(
          tk_ctx, current_token(runtime_context)->next);
    }
    int has_size = current_token(runtime_context)->kind != TK_RBRACKET;
    token_t *expression_start = NULL;
    token_t *expression_end = NULL;
    if (has_size) {
      expression_start = current_token(runtime_context);
      expression_end = find_declaration_expression_end(
          current_token(runtime_context), TK_RBRACKET, TK_EOF);
      if (!expression_end) {
        ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "declaration-syntax",
                     "unterminated array bound");
      }
      tk_set_current_token_ctx(tk_ctx, expression_end);
    }
    tk_expect_ctx(tk_ctx, ']');
    int op_index = declarator->declarator_shape.count;
    if (!ps_declarator_shape_append_array_ex_in(
            ps_parser_runtime_arena(parse_context->runtime_context),
            &declarator->declarator_shape, 0, !has_size)) {
      diagnose_declarator_too_complex(
          context, current_token(runtime_context));
    }
    if (has_size) {
      psx_parsed_array_bound_t *bound = append_array_bound(
          declarator, parse_context->runtime_context);
      if (!bound)
        diagnose_declarator_too_complex(
            context, current_token(runtime_context));
      *bound = (psx_parsed_array_bound_t){
          .declarator_op_index = op_index,
          .expression = make_parsed_const_expr(
              expression_start, expression_end),
          .has_static = has_static,
          .is_const_qualified = is_const,
          .is_volatile_qualified = is_volatile,
          .is_restrict_qualified = is_restrict,
      };
    }
    return 1;
  }
  if (current_token(runtime_context)->kind != TK_LPAREN) return 0;
  int op_index = declarator->declarator_shape.count;
  if (!ps_declarator_shape_append_function_in(
          ps_parser_runtime_arena(parse_context->runtime_context),
          &declarator->declarator_shape)) {
    diagnose_declarator_too_complex(
        context, current_token(runtime_context));
  }
  psx_parsed_function_suffix_t *suffix =
      append_function_suffix(declarator, parse_context->runtime_context);
  if (!suffix)
    diagnose_declarator_too_complex(
        context, current_token(runtime_context));
  psx_parsed_function_parameters_t *parameters =
      calloc(1, sizeof(*parameters));
  if (!parameters) {
    ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "declaration-syntax",
                 "function parameter syntax allocation failed");
  }
  if (!psx_parse_function_parameters_syntax_with_typedef_lookup_in_contexts(
          parameters,
          parse_context->name_classifier &&
                  !parse_context->allow_implicit_function_parameters
              ? PSX_PARAMETER_TYPE_C11_STRICT
              : (parse_context->allow_implicit_function_parameters
                     ? PSX_PARAMETER_TYPE_ALLOW_IMPLICIT_INT
                     : PSX_PARAMETER_TYPE_DEFERRED_TYPEDEF),
          parse_context->options))
    parse_context->has_syntax_error = 1;
  *suffix = (psx_parsed_function_suffix_t){
      .declarator_op_index = op_index,
      .parameters = parameters,
  };
  return 1;
}

static int is_abstract_grouping_parenthesis(
    void *context, int nesting_depth) {
  (void)nesting_depth;
  declaration_declarator_parse_context_t *parse_context = context;
  psx_parser_runtime_context_t *runtime_context =
      parse_context ? parse_context->runtime_context : NULL;
  token_t *next = current_token(runtime_context)->next;
  psx_skip_gnu_attributes_at(&next);
  return next && (next->kind == TK_MUL || next->kind == TK_LPAREN);
}

static int token_starts_parameter_type(
    token_t *token,
    const declaration_declarator_parse_context_t *context) {
  if (!token) return 0;
  switch (token->kind) {
    case TK_VOID:
    case TK_BOOL:
    case TK_CHAR:
    case TK_SHORT:
    case TK_INT:
    case TK_LONG:
    case TK_FLOAT:
    case TK_DOUBLE:
    case TK_SIGNED:
    case TK_UNSIGNED:
    case TK_COMPLEX:
    case TK_IMAGINARY:
    case TK_STRUCT:
    case TK_UNION:
    case TK_ENUM:
    case TK_CONST:
    case TK_VOLATILE:
    case TK_RESTRICT:
    case TK_ATOMIC:
      return 1;
    default:
      return context && ps_name_classifier_is_typedef_name(
                            context->name_classifier, token);
  }
}

static int is_parameter_grouping_parenthesis(
    void *context, int nesting_depth) {
  (void)nesting_depth;
  declaration_declarator_parse_context_t *parse_context = context;
  psx_parser_runtime_context_t *runtime_context =
      parse_context ? parse_context->runtime_context : NULL;
  token_t *next = current_token(runtime_context)->next;
  psx_skip_gnu_attributes_at(&next);
  if (!next || next->kind == TK_RPAREN) return 0;
  return !token_starts_parameter_type(
      next, parse_context);
}

static int parse_declarator_syntax_tree_into(
    psx_parsed_declarator_t *declarator,
    int is_abstract,
    int (*is_grouping_parenthesis)(void *, int),
    const psx_decl_specifier_syntax_options_t *options,
    int allow_implicit_function_parameters) {
  psx_parser_runtime_context_t *runtime_context =
      options ? options->runtime_context : NULL;
  memset(declarator, 0, sizeof(*declarator));
  ps_declarator_shape_init(&declarator->declarator_shape);
  declaration_declarator_parse_context_t parse_context = {
      .declarator = declarator,
      .options = options,
      .runtime_context = runtime_context,
      .name_classifier = options ? options->name_classifier : NULL,
      .allow_implicit_function_parameters =
          allow_implicit_function_parameters,
  };
  declarator->identifier = psx_parse_declarator_syntax(
      &(psx_declarator_syntax_t){
          .context = &parse_context,
          .arena_context = ps_parser_runtime_arena(runtime_context),
          .tokenizer_context = ps_parser_runtime_tokenizer(runtime_context),
          .diagnostic_context = diagnostics(runtime_context),
          .is_grouping_parenthesis =
              is_grouping_parenthesis
                  ? is_grouping_parenthesis
                  : (is_abstract ? is_abstract_grouping_parenthesis : NULL),
          .consume_suffix = consume_declarator_suffix,
          .append_pointer = append_declarator_pointer,
          .diagnose_too_complex = diagnose_declarator_too_complex,
      });
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  if (!is_abstract && tk_consume_ctx(tk_ctx, ':')) {
    declarator->has_bitfield = 1;
    token_t *start = current_token(runtime_context);
    token_t *end = find_declaration_expression_end(
        start, TK_COMMA, TK_SEMI);
    declarator->bit_width_expression =
        make_parsed_const_expr(start, end);
    if (!declarator->bit_width_expression.end) {
      ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "declaration-syntax",
                   "unterminated bit-field width");
    }
    tk_set_current_token_ctx(
        tk_ctx, declarator->bit_width_expression.end);
  }
  declarator->diagnostic_token = declarator->identifier
                                    ? (token_t *)declarator->identifier
                                    : current_token(runtime_context);
  return !parse_context.has_syntax_error;
}

void psx_parse_declarator_syntax_tree_into_with_typedef_lookup_in_contexts(
    psx_parsed_declarator_t *declarator,
    const psx_decl_specifier_syntax_options_t *options) {
  if (!declarator || !options || !options->runtime_context)
    return;
  parse_declarator_syntax_tree_into(
      declarator, 0, NULL, options, 0);
  psx_materialize_declarator_expression_syntax(declarator, options);
}

int psx_try_parse_toplevel_declarator_syntax_tree_with_typedef_lookup_in_contexts(
    psx_parsed_declarator_t *declarator,
    const psx_decl_specifier_syntax_options_t *options) {
  if (!declarator || !options || !options->runtime_context)
    return 0;
  int parsed = parse_declarator_syntax_tree_into(
      declarator, 0, NULL, options, 0);
  if (parsed)
    psx_materialize_declarator_expression_syntax(declarator, options);
  return parsed;
}

psx_parsed_declarator_t psx_parse_abstract_declarator_syntax_tree_in_contexts(
    const psx_decl_specifier_syntax_options_t *options) {
  psx_parsed_declarator_t declarator;
  parse_declarator_syntax_tree_into(
      &declarator, 1, NULL, options, 0);
  psx_materialize_declarator_expression_syntax(&declarator, options);
  return declarator;
}

psx_parsed_declarator_t psx_parse_parameter_declarator_syntax_tree_in_contexts(
    const psx_decl_specifier_syntax_options_t *options) {
  psx_parsed_declarator_t declarator;
  parse_declarator_syntax_tree_into(
      &declarator, 0, is_parameter_grouping_parenthesis,
      options, 0);
  psx_materialize_declarator_expression_syntax(&declarator, options);
  return declarator;
}

const psx_parsed_function_suffix_t *
psx_declarator_outermost_function_suffix(
    const psx_parsed_declarator_t *declarator) {
  if (!declarator || declarator->declarator_shape.count <= 0 ||
      !declarator->declarator_shape.ops ||
      declarator->declarator_shape.ops[0].kind !=
          PSX_DECL_OP_FUNCTION)
    return NULL;
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    const psx_parsed_function_suffix_t *suffix =
        &declarator->function_suffixes[i];
    if (suffix->declarator_op_index == 0) return suffix;
  }
  return NULL;
}

void psx_materialize_declarator_expression_syntax(
    psx_parsed_declarator_t *declarator,
    const psx_decl_specifier_syntax_options_t *options) {
  psx_parser_runtime_context_t *runtime_context =
      options ? options->runtime_context : NULL;
  if (!declarator || !options ||
      !options->parse_assignment_expression ||
      !runtime_context || !tokenizer(runtime_context))
    return;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  token_t *saved = current_token(runtime_context);
  for (int i = 0; i < declarator->array_bound_count; i++) {
    psx_parsed_const_expr_t *expression =
        &declarator->array_bounds[i].expression;
    if (expression->node || !expression->start || !expression->end) continue;
    tk_set_current_token_ctx(tk_ctx, expression->start);
    expression->node =
        options->parse_assignment_expression(
            options->expression_context);
    if (current_token(runtime_context) != expression->end) {
      ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "declaration-syntax",
                   "runtime array bound was not fully consumed");
    }
  }
  if (declarator->has_bitfield &&
      !declarator->bit_width_expression.node &&
      declarator->bit_width_expression.start &&
      declarator->bit_width_expression.end) {
    tk_set_current_token_ctx(
        tk_ctx, declarator->bit_width_expression.start);
    declarator->bit_width_expression.node =
        options->parse_assignment_expression(
            options->expression_context);
    if (current_token(runtime_context) !=
        declarator->bit_width_expression.end) {
      ps_diag_ctx_in(
          diagnostics(runtime_context), current_token(runtime_context),
          "declaration-syntax",
          "bit-field width was not fully consumed");
    }
  }
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    psx_parsed_function_parameters_t *parameters =
        declarator->function_suffixes[i].parameters;
    for (int p = 0; parameters && p < parameters->count; p++) {
      psx_materialize_declarator_expression_syntax(
          &parameters->items[p].declarator, options);
    }
  }
  tk_set_current_token_ctx(tk_ctx, saved);
}

static void parse_tag_specifier(
    psx_parsed_decl_specifier_t *specifier,
    const psx_decl_specifier_syntax_options_t *options) {
  psx_parser_runtime_context_t *runtime_context =
      options->runtime_context;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  psx_parsed_tag_action_t *action = &specifier->tag_action;
  action->diagnostic_token = current_token(runtime_context);
  action->kind = current_token(runtime_context)->kind;
  tk_set_current_token_ctx(
      tk_ctx, current_token(runtime_context)->next);
  psx_skip_gnu_attributes_ctx(tk_ctx);
  token_ident_t *tag = tk_consume_ident_ctx(tk_ctx);
  if (tag) {
    action->name = tag->str;
    action->name_len = tag->len;
  } else if (current_token(runtime_context)->kind == TK_LBRACE) {
    action->is_anonymous = 1;
    psx_make_anonymous_tag_name_in(
        options->runtime_context, &action->name, &action->name_len);
  } else {
    ps_diag_missing_in(diagnostics(runtime_context),
        current_token(runtime_context),
        diag_text_for_in(diagnostics(runtime_context), DIAG_TEXT_TAG_NAME));
  }

  if (tk_consume_ctx(tk_ctx, '{')) {
    action->action = PSX_PARSED_TAG_DEFINITION;
    ps_name_classifier_record_binding_event(options->name_classifier);
    if (action->kind == TK_STRUCT || action->kind == TK_UNION)
      ps_name_classifier_reserve_scope(options->name_classifier);
    specifier->binding_events_recorded =
        options->name_classifier &&
        options->name_classifier->record_binding_event;
    if (action->kind == TK_ENUM) {
      action->enum_body = calloc(1, sizeof(*action->enum_body));
      if (!action->enum_body) {
        ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "declaration-syntax",
                     "enum body allocation failed");
      }
      psx_parse_enum_body_syntax(
          action->enum_body,
          &(psx_enum_body_syntax_context_t){
              .diagnostics = diagnostics(runtime_context),
              .expression_context = options->expression_context,
              .parse_assignment_expression =
                  options->parse_assignment_expression,
              .tokenizer_context = tk_ctx,
              .name_classifier = options->name_classifier,
          });
    } else {
      action->aggregate_body = calloc(1, sizeof(*action->aggregate_body));
      if (!action->aggregate_body) {
        ps_diag_ctx_in(diagnostics(runtime_context), current_token(runtime_context), "declaration-syntax",
                     "aggregate body allocation failed");
      }
      psx_parse_aggregate_body_with_options(
          action->aggregate_body, options);
    }
  } else {
    action->action = PSX_PARSED_TAG_REFERENCE;
    ps_name_classifier_record_binding_event(options->name_classifier);
    specifier->binding_events_recorded =
        options->name_classifier &&
        options->name_classifier->record_binding_event;
  }
}

int psx_try_parse_decl_specifier_syntax_ex(
    psx_parsed_decl_specifier_t *specifier,
    const psx_decl_specifier_syntax_options_t *options) {
  if (!specifier) return 0;
  psx_decl_specifier_syntax_options_t complete_options;
  options = complete_decl_specifier_syntax_options(
      options, &complete_options);
  if (!options) return 0;
  psx_parser_runtime_context_t *runtime_context =
      options->runtime_context;
  tokenizer_context_t *tk_ctx = tokenizer(runtime_context);
  memset(specifier, 0, sizeof(*specifier));
  specifier->diagnostic_token = current_token(runtime_context);
  declaration_alignas_parse_context_t alignas_context = {
      .specifier = specifier,
      .options = options,
      .runtime_context = runtime_context,
  };

  token_kind_t builtin_kind = psx_consume_type_kind_with_syntax_ex(
      &specifier->type_spec,
      &(psx_type_spec_syntax_t){
          .context = options->context,
          .diagnostics = diagnostics(runtime_context),
          .tokenizer_context = tk_ctx,
          .name_classifier = options->name_classifier,
          .consume_alignas_context = &alignas_context,
          .consume_alignas = consume_declaration_alignas,
          .diagnose_complex_requires_float =
              options ? options->diagnose_complex_requires_float : NULL,
      });
  if (builtin_kind != TK_EOF) {
    specifier->source = PSX_PARSED_DECL_TYPE_BUILTIN;
    return 1;
  }
  if (current_token(runtime_context)->kind == TK_STRUCT ||
      current_token(runtime_context)->kind == TK_UNION ||
      current_token(runtime_context)->kind == TK_ENUM) {
    specifier->source = PSX_PARSED_DECL_TYPE_TAG;
    parse_tag_specifier(specifier, options);
    while (current_token(runtime_context)->kind == TK_CONST ||
           current_token(runtime_context)->kind == TK_VOLATILE) {
      if (current_token(runtime_context)->kind == TK_CONST)
        specifier->type_spec.is_const_qualified = 1;
      if (current_token(runtime_context)->kind == TK_VOLATILE)
        specifier->type_spec.is_volatile_qualified = 1;
      tk_set_current_token_ctx(
          tk_ctx, current_token(runtime_context)->next);
    }
    return 1;
  }
  if (current_token(runtime_context)->kind == TK_IDENT &&
      (!options || !options->name_classifier ||
       ps_name_classifier_is_typedef_name(
           options->name_classifier, current_token(runtime_context)))) {
    specifier->source = PSX_PARSED_DECL_TYPEDEF_NAME;
    specifier->typedef_name =
        (token_ident_t *)current_token(runtime_context);
    tk_set_current_token_ctx(
        tk_ctx, current_token(runtime_context)->next);
    while (current_token(runtime_context)->kind == TK_CONST ||
           current_token(runtime_context)->kind == TK_VOLATILE ||
           current_token(runtime_context)->kind == TK_RESTRICT ||
           (current_token(runtime_context)->kind == TK_ATOMIC &&
            !(current_token(runtime_context)->next &&
              current_token(runtime_context)->next->kind == TK_LPAREN))) {
      if (current_token(runtime_context)->kind == TK_CONST)
        specifier->type_spec.is_const_qualified = 1;
      if (current_token(runtime_context)->kind == TK_VOLATILE)
        specifier->type_spec.is_volatile_qualified = 1;
      if (current_token(runtime_context)->kind == TK_ATOMIC)
        specifier->type_spec.is_atomic = 1;
      tk_set_current_token_ctx(
          tk_ctx, current_token(runtime_context)->next);
    }
    return 1;
  }
  if (options && options->allow_implicit_int) {
    specifier->source = PSX_PARSED_DECL_TYPE_IMPLICIT_INT;
    specifier->type_spec.kind = TK_INT;
    return 1;
  }
  return 0;
}

void psx_parse_decl_specifier_syntax_ex(
    psx_parsed_decl_specifier_t *specifier,
    const psx_decl_specifier_syntax_options_t *options) {
  if (psx_try_parse_decl_specifier_syntax_ex(specifier, options)) return;
  psx_parser_runtime_context_t *runtime_context =
      options ? options->runtime_context : NULL;
  if (!runtime_context) return;
  ps_diag_ctx_in(
      diagnostics(runtime_context), current_token(runtime_context),
      "decl", "%s",
      diag_message_for_in(
          diagnostics(runtime_context), DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
}

void ps_dispose_decl_specifier_syntax(
    psx_parsed_decl_specifier_t *specifier) {
  if (!specifier) return;
  for (int i = 0; i < specifier->alignas_specifier_count; i++) {
    psx_parsed_alignas_t *alignas = &specifier->alignas_specifiers[i];
    if (alignas->type_name) {
      psx_dispose_type_name_syntax(alignas->type_name);
      free(alignas->type_name);
    }
  }
  psx_parsed_tag_action_t *tag_action = &specifier->tag_action;
  if (tag_action->aggregate_body) {
    psx_dispose_parsed_aggregate_body(tag_action->aggregate_body);
    free(tag_action->aggregate_body);
  }
  if (tag_action->enum_body) {
    psx_dispose_parsed_enum_body(tag_action->enum_body);
    free(tag_action->enum_body);
  }
}

void psx_dispose_declarator_syntax(psx_parsed_declarator_t *declarator) {
  if (!declarator) return;
  for (int i = 0; i < declarator->function_suffix_count; i++) {
    psx_parsed_function_parameters_t *parameters =
        declarator->function_suffixes[i].parameters;
    if (!parameters) continue;
    psx_dispose_function_parameters_syntax(parameters);
    free(parameters);
  }
}

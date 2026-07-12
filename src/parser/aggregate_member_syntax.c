#include "aggregate_member_syntax.h"

#include "anon_tag.h"
#include "array_suffixes.h"
#include "declarator_syntax.h"
#include "diag.h"
#include "enum_const.h"
#include "semantic_ctx.h"
#include "struct_layout.h"
#include "tag_declaration.h"
#include "../diag/diag.h"
#include "../tokenizer/tokenizer.h"

#include <string.h>

static token_t *current_token(void) { return tk_get_current_token(); }

static void diagnose_member_declarator_too_complex(
    void *context, token_t *tok) {
  (void)context;
  psx_diag_ctx(tok, "member", "member declarator is too complex");
}

static int append_member_declarator_pointer(
    void *context, int is_const, int is_volatile, int nesting_depth) {
  (void)nesting_depth;
  psx_parsed_aggregate_member_declarator_t *declarator = context;
  return declarator && psx_declarator_shape_append_pointer(
                           &declarator->declarator_shape,
                           is_const, is_volatile);
}

static int consume_member_declarator_suffix(
    void *context, int nesting_depth, int direct_was_parenthesized,
    int direct_pointer_count, int frame_pointer_count) {
  (void)nesting_depth;
  (void)direct_was_parenthesized;
  (void)direct_pointer_count;
  (void)frame_pointer_count;
  psx_parsed_aggregate_member_declarator_t *declarator = context;
  if (!declarator) return 0;
  if (current_token()->kind == TK_LBRACKET) {
    tk_expect('[');
    int has_size = 0;
    int dim = psx_parse_array_size_optional_constexpr(&has_size);
    if (!psx_declarator_shape_append_array_ex(
            &declarator->declarator_shape,
            has_size ? dim : 0, !has_size)) {
      diagnose_member_declarator_too_complex(context, current_token());
    }
    return 1;
  }
  if (current_token()->kind != TK_LPAREN) return 0;
  psx_funcptr_signature_t suffix = {0};
  psx_skip_func_param_list(&suffix);
  psx_decl_funcptr_sig_t function = {0};
  function.function.callable.signature = suffix;
  if (!psx_declarator_shape_append_function(
          &declarator->declarator_shape, function)) {
    diagnose_member_declarator_too_complex(context, current_token());
  }
  return 1;
}

psx_parsed_aggregate_member_declarator_t
psx_parse_aggregate_member_declarator(void) {
  psx_parsed_aggregate_member_declarator_t declarator = {0};
  psx_declarator_shape_init(&declarator.declarator_shape);
  declarator.member = psx_parse_declarator_syntax(
      &(psx_declarator_syntax_t){
          .context = &declarator,
          .consume_suffix = consume_member_declarator_suffix,
          .append_pointer = append_member_declarator_pointer,
          .diagnose_too_complex = diagnose_member_declarator_too_complex,
      },
      &declarator.pointer_levels);
  if (tk_consume(':')) {
    declarator.has_bitfield = 1;
    long long width = psx_parse_enum_const_expr();
    declarator.bit_width = width > 0 ? (int)width : 0;
  }
  return declarator;
}

static void parse_member_tag_specifier(
    psx_parsed_aggregate_member_specifier_t *specifier) {
  psx_decl_type_request_t *declaration = &specifier->declaration;
  declaration->tag_kind = current_token()->kind;
  tk_set_current_token(current_token()->next);
  token_ident_t *tag = tk_consume_ident();
  if (tag) {
    declaration->tag_name = tag->str;
    declaration->tag_len = tag->len;
  } else if (current_token()->kind == TK_LBRACE) {
    psx_make_anonymous_tag_name(
        &declaration->tag_name, &declaration->tag_len);
  } else {
    psx_diag_missing(current_token(), diag_text_for(DIAG_TEXT_TAG_NAME));
  }

  if (tk_consume('{')) {
    int member_count = 0;
    int size = 0;
    int alignment = 0;
    member_count = psx_parse_tag_definition_body(
        declaration->tag_kind, declaration->tag_name,
        declaration->tag_len, &size, &alignment);
    psx_apply_parsed_tag_declaration(
        declaration->tag_kind, declaration->tag_name,
        declaration->tag_len, PSX_TAG_DECLARATION_DEFINITION,
        member_count, size, alignment, current_token());
  } else if (!psx_ctx_has_tag_type(
                 declaration->tag_kind, declaration->tag_name,
                 declaration->tag_len)) {
    psx_apply_parsed_tag_declaration(
        declaration->tag_kind, declaration->tag_name,
        declaration->tag_len, PSX_TAG_DECLARATION_REFERENCE,
        0, 0, 0, current_token());
  }
  if (psx_ctx_has_tag_type(
          declaration->tag_kind, declaration->tag_name,
          declaration->tag_len)) {
    declaration->elem_size = psx_ctx_get_tag_size(
        declaration->tag_kind, declaration->tag_name,
        declaration->tag_len);
  }
}

void psx_parse_aggregate_member_specifier(
    psx_parsed_aggregate_member_specifier_t *specifier) {
  if (!specifier) return;
  memset(specifier, 0, sizeof(*specifier));
  specifier->declaration.base_kind = TK_EOF;
  specifier->declaration.tag_kind = TK_EOF;
  specifier->declaration.fp_kind = TK_FLOAT_KIND_NONE;
  specifier->declaration.elem_size = 8;

  psx_type_spec_result_t type_spec = {0};
  token_kind_t builtin_kind = psx_consume_type_kind_ex(&type_spec);
  specifier->requested_alignment = type_spec.alignas_value;
  specifier->declaration.is_unsigned = type_spec.is_unsigned;
  specifier->declaration.is_complex = type_spec.is_complex;
  specifier->declaration.is_const_qualified = type_spec.is_const_qualified;
  specifier->declaration.is_volatile_qualified = type_spec.is_volatile_qualified;
  specifier->declaration.is_atomic = type_spec.is_atomic;
  specifier->declaration.is_long_long = type_spec.is_long_long;
  specifier->declaration.is_plain_char = type_spec.is_plain_char;
  specifier->declaration.is_long_double = type_spec.is_long_double;

  if (builtin_kind != TK_EOF) {
    specifier->declaration.base_kind = builtin_kind;
    specifier->declaration.override_plain_char = builtin_kind == TK_CHAR;
    psx_ctx_get_type_info(
        builtin_kind, NULL, &specifier->declaration.elem_size);
    if (builtin_kind == TK_FLOAT)
      specifier->declaration.fp_kind = TK_FLOAT_KIND_FLOAT;
    else if (builtin_kind == TK_DOUBLE)
      specifier->declaration.fp_kind = TK_FLOAT_KIND_DOUBLE;
    if (type_spec.is_complex) specifier->declaration.elem_size *= 2;
    return;
  }

  if (psx_ctx_is_tag_keyword(current_token()->kind)) {
    parse_member_tag_specifier(specifier);
    return;
  }
  if (psx_ctx_is_typedef_name_token(current_token())) {
    token_ident_t *typedef_name = (token_ident_t *)current_token();
    psx_ctx_find_typedef_decl_type(
        typedef_name->str, typedef_name->len,
        &specifier->declaration.base_decl_type);
    tk_set_current_token(current_token()->next);
    return;
  }
  psx_diag_ctx(current_token(), "decl", "%s",
               diag_message_for(DIAG_ERR_PARSER_MEMBER_TYPE_REQUIRED));
}

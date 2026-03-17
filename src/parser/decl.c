#include "internal/decl.h"
#include "internal/core.h"
#include "internal/diag.h"
#include "internal/expr.h"
#include "internal/node_utils.h"
#include "internal/semantic_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>
#include <string.h>

static lvar_t *locals;
static int locals_offset;

static void skip_func_params(void) {
  if (!tk_consume('(')) return;
  int depth = 1;
  while (depth > 0) {
    if (token->kind == TK_EOF) {
      psx_diag_ctx(token, "decl", "関数宣言子の ')' が不足しています");
    }
    if (token->kind == TK_LPAREN) depth++;
    else if (token->kind == TK_RPAREN) depth--;
    token = token->next;
  }
}

static token_ident_t *consume_decl_name(int *is_pointer) {
  token_ident_t *tok = NULL;
  int open_parens = 0;
  while (tk_consume('(')) open_parens++;
  while (tk_consume('*')) *is_pointer = 1;
  tok = tk_consume_ident();
  if (!tok) psx_diag_ctx(token, "decl", "変数名が期待されます");
  while (open_parens-- > 0) tk_expect(')');
  while (token->kind == TK_LPAREN) {
    skip_func_params();
  }
  return tok;
}

void psx_decl_reset_locals(void) {
  locals = NULL;
  locals_offset = 0;
}

lvar_t *psx_decl_find_lvar(char *name, int len) {
  for (lvar_t *var = locals; var; var = var->next) {
    if (var->len == len && memcmp(var->name, name, len) == 0) {
      return var;
    }
  }
  return NULL;
}

lvar_t *psx_decl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array) {
  lvar_t *var = calloc(1, sizeof(lvar_t));
  var->next = locals;
  var->name = name;
  var->len = len;
  locals_offset += size;
  var->offset = locals_offset;
  var->size = size;
  var->elem_size = elem_size;
  var->is_array = is_array;
  locals = var;
  return var;
}

lvar_t *psx_decl_register_lvar(char *name, int len) {
  return psx_decl_register_lvar_sized(name, len, 8, 8, 0);
}

node_t *psx_decl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind,
                                              token_kind_t tag_kind, char *tag_name, int tag_len,
                                              int base_is_pointer) {
  node_t *init_chain = NULL;

  for (;;) {
    int is_pointer = base_is_pointer;
    while (tk_consume('*')) { is_pointer = 1; }
    if (tag_kind != TK_EOF && !is_pointer && elem_size <= 0) {
      psx_diag_ctx(token, "decl", "不完全型のオブジェクトは宣言できません");
    }
    int var_size = is_pointer ? 8 : elem_size;

    token_ident_t *tok = consume_decl_name(&is_pointer);

    lvar_t *var = psx_decl_find_lvar(tok->str, tok->len);
    if (!var) {
      if (tk_consume('[')) {
        int array_size = tk_expect_number();
        tk_expect(']');
        var = psx_decl_register_lvar_sized(tok->str, tok->len, array_size * elem_size, elem_size, 1);
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = 0;
        if (tk_consume('=')) {
          psx_expr_assign();
        }
      } else {
        var = psx_decl_register_lvar_sized(tok->str, tok->len, var_size, is_pointer ? elem_size : var_size, 0);
        var->tag_kind = tag_kind;
        var->tag_name = tag_name;
        var->tag_len = tag_len;
        var->is_tag_pointer = is_pointer ? 1 : 0;
      }
    }

    if (!is_pointer) {
      var->fp_kind = decl_fp_kind;
    }

    if (tk_consume('=')) {
      node_t *lvar = psx_node_new_lvar_typed(var->offset, is_pointer ? 8 : var->elem_size);
      lvar->fp_kind = var->fp_kind;
      ((node_lvar_t *)lvar)->mem.tag_kind = var->tag_kind;
      ((node_lvar_t *)lvar)->mem.tag_name = var->tag_name;
      ((node_lvar_t *)lvar)->mem.tag_len = var->tag_len;
      ((node_lvar_t *)lvar)->mem.is_tag_pointer = var->is_tag_pointer;
      node_mem_t *assign_node = psx_node_new_assign(lvar, psx_expr_assign());
      assign_node->type_size = is_pointer ? 8 : var->elem_size;
      assign_node->base.fp_kind = var->fp_kind;
      node_t *init_node = (node_t *)assign_node;
      if (!init_chain) init_chain = init_node;
      else init_chain = psx_node_new_binary(ND_COMMA, init_chain, init_node);
    }

    if (!tk_consume(',')) break;
  }

  tk_expect(';');
  return init_chain ? init_chain : psx_node_new_num(0);
}

node_t *psx_decl_parse_declaration(void) {
  token_kind_t type_kind = psx_consume_type_kind();
  int elem_size = 8;
  psx_ctx_get_type_info(type_kind, NULL, &elem_size);
  tk_float_kind_t decl_fp_kind = TK_FLOAT_KIND_NONE;
  if (type_kind == TK_FLOAT) decl_fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (type_kind == TK_DOUBLE) decl_fp_kind = TK_FLOAT_KIND_DOUBLE;
  return psx_decl_parse_declaration_after_type(elem_size, decl_fp_kind, TK_EOF, NULL, 0, 0);
}

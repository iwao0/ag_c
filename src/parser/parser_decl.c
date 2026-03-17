#include "parser_decl.h"
#include "parser_diag.h"
#include "parser_expr.h"
#include "parser_node_utils.h"
#include "parser_semantic_ctx.h"
#include "../tokenizer/tokenizer.h"
#include <stdlib.h>
#include <string.h>

static lvar_t *locals;
static int locals_offset;

extern token_kind_t parser_consume_type_kind(void);

void pdecl_reset_locals(void) {
  locals = NULL;
  locals_offset = 0;
}

lvar_t *pdecl_find_lvar(char *name, int len) {
  for (lvar_t *var = locals; var; var = var->next) {
    if (var->len == len && memcmp(var->name, name, len) == 0) {
      return var;
    }
  }
  return NULL;
}

lvar_t *pdecl_register_lvar_sized(char *name, int len, int size, int elem_size, int is_array) {
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

lvar_t *pdecl_register_lvar(char *name, int len) {
  return pdecl_register_lvar_sized(name, len, 8, 8, 0);
}

node_t *pdecl_parse_declaration_after_type(int elem_size, tk_float_kind_t decl_fp_kind) {
  node_t *init_chain = NULL;

  for (;;) {
    int is_pointer = 0;
    while (tk_consume('*')) { is_pointer = 1; }
    int var_size = is_pointer ? 8 : elem_size;

    token_ident_t *tok = tk_consume_ident();
    if (!tok) {
      pdiag_ctx(token, "decl", "変数名が期待されます");
    }

    lvar_t *var = pdecl_find_lvar(tok->str, tok->len);
    if (!var) {
      if (tk_consume('[')) {
        int array_size = tk_expect_number();
        tk_expect(']');
        var = pdecl_register_lvar_sized(tok->str, tok->len, array_size * elem_size, elem_size, 1);
        if (tk_consume('=')) {
          pexpr_assign();
        }
      } else {
        var = pdecl_register_lvar_sized(tok->str, tok->len, var_size, is_pointer ? elem_size : var_size, 0);
      }
    }

    if (!is_pointer) {
      var->fp_kind = decl_fp_kind;
    }

    if (tk_consume('=')) {
      node_t *lvar = pnode_new_lvar_typed(var->offset, is_pointer ? 8 : var->elem_size);
      lvar->fp_kind = var->fp_kind;
      node_mem_t *assign_node = pnode_new_assign(lvar, pexpr_assign());
      assign_node->type_size = is_pointer ? 8 : var->elem_size;
      assign_node->base.fp_kind = var->fp_kind;
      node_t *init_node = (node_t *)assign_node;
      if (!init_chain) init_chain = init_node;
      else init_chain = pnode_new_binary(ND_COMMA, init_chain, init_node);
    }

    if (!tk_consume(',')) break;
  }

  tk_expect(';');
  return init_chain ? init_chain : pnode_new_num(0);
}

node_t *pdecl_parse_declaration(void) {
  token_kind_t type_kind = parser_consume_type_kind();
  int elem_size = 8;
  pctx_get_type_info(type_kind, NULL, &elem_size);
  tk_float_kind_t decl_fp_kind = TK_FLOAT_KIND_NONE;
  if (type_kind == TK_FLOAT) decl_fp_kind = TK_FLOAT_KIND_FLOAT;
  else if (type_kind == TK_DOUBLE) decl_fp_kind = TK_FLOAT_KIND_DOUBLE;
  return pdecl_parse_declaration_after_type(elem_size, decl_fp_kind);
}

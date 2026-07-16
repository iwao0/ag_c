#include "typed_hir_builder.h"

#include <stdlib.h>
#include <string.h>

#include "../hir/hir_internal.h"
#include "../parser/ast.h"
#include "../parser/gvar_public.h"
#include "../parser/lvar_public.h"
#include "../parser/node_type_public.h"
#include "../parser/semantic_ctx.h"
#include "../parser/vla_runtime.h"
#include "../type_layout.h"

typedef struct {
  psx_hir_module_t *module;
  const psx_semantic_context_t *semantic_context;
  psx_typed_hir_build_failure_t *failure;
} hir_builder_t;

typedef struct {
  psx_hir_node_id_t *items;
  psx_hir_edge_kind_t *edges;
  size_t count;
  size_t capacity;
} hir_children_t;

static void set_failure(
    hir_builder_t *builder, psx_typed_hir_build_status_t status,
    const node_t *source) {
  if (!builder->failure || builder->failure->status != PSX_TYPED_HIR_BUILD_OK)
    return;
  builder->failure->status = status;
  builder->failure->source_node_kind = source ? (int)source->kind : -1;
}

static int append_child(
    hir_builder_t *builder, hir_children_t *children,
    psx_hir_node_id_t child, psx_hir_edge_kind_t edge,
    const node_t *source) {
  if (child == PSX_HIR_NODE_ID_INVALID) return 0;
  if (children->count == children->capacity) {
    size_t capacity = children->capacity ? children->capacity * 2 : 4;
    psx_hir_node_id_t *items = realloc(
        children->items, capacity * sizeof(*items));
    if (!items) {
      set_failure(builder, PSX_TYPED_HIR_BUILD_OUT_OF_MEMORY, source);
      return 0;
    }
    children->items = items;
    psx_hir_edge_kind_t *edges = realloc(
        children->edges, capacity * sizeof(*edges));
    if (!edges) {
      set_failure(builder, PSX_TYPED_HIR_BUILD_OUT_OF_MEMORY, source);
      return 0;
    }
    children->edges = edges;
    children->capacity = capacity;
  }
  children->items[children->count++] = child;
  children->edges[children->count - 1] = edge;
  return 1;
}

static int map_kind(
    node_kind_t source, psx_hir_node_kind_t *kind,
    psx_hir_node_role_t *role) {
#define MAP_EXPR(source_kind, hir_kind) \
  case source_kind: *kind = hir_kind; *role = PSX_HIR_ROLE_EXPRESSION; return 1
#define MAP_STMT(source_kind, hir_kind) \
  case source_kind: *kind = hir_kind; *role = PSX_HIR_ROLE_STATEMENT; return 1
  switch (source) {
    MAP_EXPR(ND_ADD, PSX_HIR_ADD);
    MAP_EXPR(ND_SUB, PSX_HIR_SUB);
    MAP_EXPR(ND_MUL, PSX_HIR_MUL);
    MAP_EXPR(ND_DIV, PSX_HIR_DIV);
    MAP_EXPR(ND_MOD, PSX_HIR_MOD);
    MAP_EXPR(ND_EQ, PSX_HIR_EQ);
    MAP_EXPR(ND_NE, PSX_HIR_NE);
    MAP_EXPR(ND_LT, PSX_HIR_LT);
    MAP_EXPR(ND_LE, PSX_HIR_LE);
    MAP_EXPR(ND_BITAND, PSX_HIR_BITAND);
    MAP_EXPR(ND_BITXOR, PSX_HIR_BITXOR);
    MAP_EXPR(ND_BITOR, PSX_HIR_BITOR);
    MAP_EXPR(ND_SHL, PSX_HIR_SHL);
    MAP_EXPR(ND_SHR, PSX_HIR_SHR);
    MAP_EXPR(ND_LOGAND, PSX_HIR_LOGAND);
    MAP_EXPR(ND_LOGOR, PSX_HIR_LOGOR);
    MAP_EXPR(ND_TERNARY, PSX_HIR_TERNARY);
    MAP_EXPR(ND_COMMA, PSX_HIR_COMMA);
    MAP_EXPR(ND_ASSIGN, PSX_HIR_ASSIGN);
    MAP_EXPR(ND_LVAR, PSX_HIR_LOCAL);
    MAP_STMT(ND_IF, PSX_HIR_IF);
    MAP_STMT(ND_WHILE, PSX_HIR_WHILE);
    MAP_STMT(ND_DO_WHILE, PSX_HIR_DO_WHILE);
    MAP_STMT(ND_FOR, PSX_HIR_FOR);
    MAP_STMT(ND_SWITCH, PSX_HIR_SWITCH);
    MAP_STMT(ND_CASE, PSX_HIR_CASE);
    MAP_STMT(ND_DEFAULT, PSX_HIR_DEFAULT);
    MAP_STMT(ND_BREAK, PSX_HIR_BREAK);
    MAP_STMT(ND_CONTINUE, PSX_HIR_CONTINUE);
    MAP_STMT(ND_GOTO, PSX_HIR_GOTO);
    MAP_STMT(ND_LABEL, PSX_HIR_LABEL);
    MAP_EXPR(ND_PRE_INC, PSX_HIR_PRE_INC);
    MAP_EXPR(ND_PRE_DEC, PSX_HIR_PRE_DEC);
    MAP_EXPR(ND_POST_INC, PSX_HIR_POST_INC);
    MAP_EXPR(ND_POST_DEC, PSX_HIR_POST_DEC);
    MAP_STMT(ND_RETURN, PSX_HIR_RETURN);
    MAP_STMT(ND_BLOCK, PSX_HIR_BLOCK);
    MAP_STMT(ND_FUNCDEF, PSX_HIR_FUNCTION);
    MAP_EXPR(ND_FUNCALL, PSX_HIR_CALL);
    MAP_EXPR(ND_FUNCREF, PSX_HIR_FUNCTION_REF);
    MAP_EXPR(ND_DEREF, PSX_HIR_DEREF);
    MAP_EXPR(ND_ADDR, PSX_HIR_ADDRESS);
    MAP_EXPR(ND_STRING, PSX_HIR_STRING);
    MAP_EXPR(ND_NUM, PSX_HIR_NUMBER);
    MAP_EXPR(ND_GVAR, PSX_HIR_GLOBAL);
    MAP_STMT(ND_VLA_ALLOC, PSX_HIR_VLA_ALLOC);
    MAP_EXPR(ND_FP_TO_INT, PSX_HIR_FP_TO_INT);
    MAP_EXPR(ND_INT_TO_FP, PSX_HIR_INT_TO_FP);
    MAP_EXPR(ND_FNEG, PSX_HIR_FNEG);
    MAP_EXPR(ND_VA_ARG_AREA, PSX_HIR_VA_ARG_AREA);
    MAP_EXPR(ND_CAST, PSX_HIR_CAST);
    MAP_EXPR(ND_CREAL, PSX_HIR_CREAL);
    MAP_EXPR(ND_CIMAG, PSX_HIR_CIMAG);
    MAP_EXPR(ND_STMT_EXPR, PSX_HIR_STMT_EXPR);
    default:
      return 0;
  }
#undef MAP_EXPR
#undef MAP_STMT
}

static psx_hir_node_id_t build_node(
    hir_builder_t *builder, const node_t *source);

static int build_and_append(
    hir_builder_t *builder, hir_children_t *children,
    const node_t *source, psx_hir_edge_kind_t edge) {
  if (!source) return 1;
  return append_child(
      builder, children, build_node(builder, source), edge, source);
}

static int build_special_children(
    hir_builder_t *builder, hir_children_t *children,
    const node_t *source, int *include_common_children) {
  *include_common_children = 1;
  switch (source->kind) {
    case ND_BLOCK: {
      const node_block_t *block = (const node_block_t *)source;
      *include_common_children = 0;
      for (int i = 0; block->body && block->body[i]; i++) {
        if (!build_and_append(
                builder, children, block->body[i],
                PSX_HIR_EDGE_BLOCK_ITEM)) return 0;
      }
      return 1;
    }
    case ND_FUNCDEF: {
      const node_function_definition_t *function =
          (const node_function_definition_t *)source;
      for (int i = 0; i < function->parameter_count; i++) {
        if (!build_and_append(
                builder, children, function->parameters[i],
                PSX_HIR_EDGE_PARAMETER))
          return 0;
      }
      return 1;
    }
    case ND_FUNCALL: {
      const node_function_call_t *call =
          (const node_function_call_t *)source;
      if (!build_and_append(
              builder, children, call->callee,
              PSX_HIR_EDGE_CALLEE)) return 0;
      for (int i = 0; i < call->argument_count; i++) {
        if (!build_and_append(
                builder, children, call->arguments[i],
                PSX_HIR_EDGE_ARGUMENT))
          return 0;
      }
      return 1;
    }
    case ND_IF:
    case ND_FOR:
    case ND_TERNARY: {
      const node_ctrl_t *control = (const node_ctrl_t *)source;
      return build_and_append(
                 builder, children, control->init, PSX_HIR_EDGE_INIT) &&
             build_and_append(
                 builder, children, control->inc,
                 PSX_HIR_EDGE_INCREMENT) &&
             build_and_append(
                 builder, children, control->els, PSX_HIR_EDGE_ELSE);
    }
    default:
      return 1;
  }
}

static int canonical_type_exists(
    const hir_builder_t *builder, psx_qual_type_t type) {
  return type.type_id != PSX_TYPE_ID_INVALID &&
         ps_ctx_type_by_id_in(builder->semantic_context, type.type_id) != NULL;
}

static int attach_global_symbol(
    hir_builder_t *builder, const node_t *source,
    psx_hir_node_spec_t *spec) {
  if (source->kind != ND_GVAR) return 1;
  const global_var_t *global = ((const node_gvar_t *)source)->symbol;
  if (!global) {
    set_failure(
        builder, PSX_TYPED_HIR_BUILD_MISSING_RESOLVED_SYMBOL, source);
    return 0;
  }
  psx_qual_type_t qual_type = ps_gvar_decl_qual_type(global);
  if (!canonical_type_exists(builder, qual_type)) {
    set_failure(
        builder, PSX_TYPED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return 0;
  }
  const psx_semantic_type_table_t *semantic_types =
      ps_ctx_semantic_type_table_in(builder->semantic_context);
  const psx_record_layout_table_t *record_layouts =
      ps_ctx_record_layout_table_in(builder->semantic_context);
  const ag_target_info_t *target =
      ps_ctx_target_info(builder->semantic_context);
  int byte_size = ps_type_sizeof_id_with_records(
      semantic_types, record_layouts, qual_type.type_id, target);
  int alignment = ps_type_alignof_id_with_records(
      semantic_types, record_layouts, qual_type.type_id, target);
  if ((byte_size <= 0 || alignment <= 0) &&
      ps_gvar_is_extern_decl(global)) {
    psx_qual_type_t base = psx_semantic_type_table_base(
        semantic_types, qual_type.type_id);
    if (base.type_id != PSX_TYPE_ID_INVALID) {
      if (byte_size <= 0)
        byte_size = ps_type_sizeof_id_with_records(
            semantic_types, record_layouts, base.type_id, target);
      if (alignment <= 0)
        alignment = ps_type_alignof_id_with_records(
            semantic_types, record_layouts, base.type_id, target);
    }
  }
  psx_hir_symbol_spec_t symbol = {
      .name = ps_gvar_name(global),
      .name_length = ps_gvar_name_len(global) > 0
                         ? (size_t)ps_gvar_name_len(global) : 0,
      .qual_type = qual_type,
      .byte_size = byte_size,
      .alignment = alignment,
      .is_extern = ps_gvar_is_extern_decl(global) ? 1 : 0,
      .is_static = ps_gvar_is_static_storage(global) ? 1 : 0,
      .is_thread_local = ps_gvar_is_thread_local(global) ? 1 : 0,
  };
  spec->symbol_id = psx_hir_module_intern_symbol(
      builder->module, &symbol);
  if (spec->symbol_id == PSX_HIR_SYMBOL_ID_INVALID) {
    set_failure(
        builder, PSX_TYPED_HIR_BUILD_MISSING_RESOLVED_SYMBOL, source);
    return 0;
  }
  return 1;
}

static void copy_payload(
    const node_t *source, psx_hir_node_spec_t *spec) {
  switch (source->kind) {
    case ND_NUM: {
      const node_num_t *number = (const node_num_t *)source;
      spec->integer_value = number->val;
      spec->floating_value = number->fval;
      break;
    }
    case ND_LVAR: {
      const node_lvar_t *local = (const node_lvar_t *)source;
      spec->storage_offset = local->offset;
      if (local->var) {
        spec->object_offset = ps_lvar_offset(local->var);
        spec->object_size = ps_lvar_frame_storage_size(local->var);
        spec->object_align = ps_lvar_align_bytes(local->var);
      } else {
        spec->object_offset = local->offset;
      }
      break;
    }
    case ND_STRING: {
      const node_string_t *string = (const node_string_t *)source;
      spec->name = string->string_label;
      spec->name_length = string->string_label
                              ? strlen(string->string_label) : 0;
      spec->literal_contents = string->literal_contents;
      spec->literal_length = string->literal_length > 0
                                 ? (size_t)string->literal_length : 0;
      int character_width = (int)string->char_width;
      if (character_width <= 0) character_width = 1;
      spec->object_size = (string->byte_len + 1) * character_width;
      spec->object_align = character_width;
      break;
    }
    case ND_FUNCDEF: {
      const node_function_definition_t *function =
          (const node_function_definition_t *)source;
      spec->name = function->name;
      spec->name_length = function->name_len > 0
                              ? (size_t)function->name_len : 0;
      spec->attached_qual_type =
          ps_function_definition_signature_qual_type(function);
      spec->is_static_function = function->is_static ? 1 : 0;
      break;
    }
    case ND_FUNCALL: {
      const node_function_call_t *call =
          (const node_function_call_t *)source;
      spec->name = call->direct_name;
      spec->name_length = call->direct_name_len > 0
                              ? (size_t)call->direct_name_len : 0;
      spec->attached_qual_type = ps_function_call_callee_qual_type(call);
      break;
    }
    case ND_FUNCREF: {
      const node_funcref_t *function = (const node_funcref_t *)source;
      spec->name = function->funcname;
      spec->name_length = function->funcname_len > 0
                              ? (size_t)function->funcname_len : 0;
      break;
    }
    case ND_GVAR: {
      const node_gvar_t *global = (const node_gvar_t *)source;
      spec->name = global->name;
      spec->name_length = global->name_len > 0
                              ? (size_t)global->name_len : 0;
      break;
    }
    case ND_CASE: {
      const node_case_t *case_node = (const node_case_t *)source;
      spec->integer_value = case_node->val;
      spec->label_id = case_node->label_id;
      break;
    }
    case ND_DEFAULT:
      spec->label_id = ((const node_default_t *)source)->label_id;
      break;
    case ND_GOTO:
    case ND_LABEL: {
      const node_jump_t *jump = (const node_jump_t *)source;
      spec->name = jump->name;
      spec->name_length = jump->name_len > 0
                              ? (size_t)jump->name_len : 0;
      spec->label_id = jump->label_id;
      break;
    }
    case ND_VLA_ALLOC: {
      const node_vla_alloc_t *allocation =
          (const node_vla_alloc_t *)source;
      spec->storage_offset =
          allocation->descriptor_frame_off;
      spec->vla_stride_frame_offset =
          allocation->row_stride_frame_off;
      spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
      break;
    }
    default:
      break;
  }
}

static int copy_vla_payload(
    hir_builder_t *builder, const node_t *source,
    psx_hir_node_spec_t *spec) {
  if (source->kind != ND_LVAR) return 1;
  const lvar_t *var = ((const node_lvar_t *)source)->var;
  if (!var || !ps_lvar_is_vla(var)) return 1;
  spec->vla_stride_frame_offset =
      ps_lvar_vla_row_stride_frame_off(var);
  spec->vla_stride_source_offset =
      ps_lvar_vla_row_stride_src_offset(var);
  spec->vla_stride_element_size =
      ps_lvar_vla_row_stride_elem_size(var);
  spec->vla_stride_slot_size = PSX_VLA_RUNTIME_SLOT_SIZE;
  int count = ps_lvar_vla_param_inner_dim_count(var);
  if (count <= 0) return 1;
  int *constants = malloc((size_t)count * sizeof(*constants));
  int *source_offsets = malloc((size_t)count * sizeof(*source_offsets));
  if (!constants || !source_offsets) {
    free(constants);
    free(source_offsets);
    set_failure(builder, PSX_TYPED_HIR_BUILD_OUT_OF_MEMORY, source);
    return 0;
  }
  for (int i = 0; i < count; i++) {
    constants[i] = ps_lvar_vla_param_inner_dim_const(var, i);
    source_offsets[i] =
        ps_lvar_vla_param_inner_dim_src_offset(var, i);
  }
  spec->vla_dimension_constants = constants;
  spec->vla_dimension_source_offsets = source_offsets;
  spec->vla_dimension_count = (size_t)count;
  return 1;
}

static void free_vla_payload(psx_hir_node_spec_t *spec) {
  free((int *)spec->vla_dimension_constants);
  free((int *)spec->vla_dimension_source_offsets);
  spec->vla_dimension_constants = NULL;
  spec->vla_dimension_source_offsets = NULL;
  spec->vla_dimension_count = 0;
}

static psx_hir_node_id_t build_node(
    hir_builder_t *builder, const node_t *source) {
  psx_hir_node_spec_t spec = {0};
  spec.qual_type = (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  spec.attached_qual_type = spec.qual_type;
  if (!map_kind(source->kind, &spec.kind, &spec.role)) {
    set_failure(builder, PSX_TYPED_HIR_BUILD_RAW_SYNTAX_REMAINS, source);
    return PSX_HIR_NODE_ID_INVALID;
  }
  if (spec.role == PSX_HIR_ROLE_EXPRESSION) {
    spec.qual_type = ps_node_qual_type(source);
    if (!canonical_type_exists(builder, spec.qual_type)) {
      set_failure(
          builder, PSX_TYPED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
      return PSX_HIR_NODE_ID_INVALID;
    }
  }
  copy_payload(source, &spec);
  {
    int bit_width = 0;
    int bit_offset = 0;
    int bit_is_signed = 0;
    if (ps_node_bitfield_info(
            (node_t *)source, &bit_width, &bit_offset,
            &bit_is_signed)) {
      spec.bit_width = (unsigned char)bit_width;
      spec.bit_offset = (unsigned char)bit_offset;
      spec.bit_is_signed = bit_is_signed ? 1 : 0;
    }
  }
  if (!attach_global_symbol(builder, source, &spec))
    return PSX_HIR_NODE_ID_INVALID;
  if (source->kind == ND_FUNCDEF &&
      !canonical_type_exists(builder, spec.attached_qual_type)) {
    set_failure(
        builder, PSX_TYPED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return PSX_HIR_NODE_ID_INVALID;
  }
  if (spec.attached_qual_type.type_id != PSX_TYPE_ID_INVALID &&
      !canonical_type_exists(builder, spec.attached_qual_type)) {
    set_failure(
        builder, PSX_TYPED_HIR_BUILD_MISSING_CANONICAL_TYPE, source);
    return PSX_HIR_NODE_ID_INVALID;
  }
  if (!copy_vla_payload(builder, source, &spec))
    return PSX_HIR_NODE_ID_INVALID;

  hir_children_t children = {0};
  int include_common_children = 1;
  if (!build_special_children(
          builder, &children, source, &include_common_children) ||
      (include_common_children &&
       (!build_and_append(
            builder, &children, source->lhs, PSX_HIR_EDGE_LHS) ||
        !build_and_append(
            builder, &children, source->rhs,
            source->kind == ND_FUNCDEF
                ? PSX_HIR_EDGE_FUNCTION_BODY : PSX_HIR_EDGE_RHS)))) {
    free(children.items);
    free(children.edges);
    free_vla_payload(&spec);
    return PSX_HIR_NODE_ID_INVALID;
  }
  spec.children = children.items;
  spec.child_edges = children.edges;
  spec.child_count = children.count;
  psx_hir_node_id_t result = psx_hir_module_add_node(
      builder->module, &spec);
  free(children.items);
  free(children.edges);
  free_vla_payload(&spec);
  if (result == PSX_HIR_NODE_ID_INVALID)
    set_failure(builder, PSX_TYPED_HIR_BUILD_OUT_OF_MEMORY, source);
  return result;
}

int psx_build_typed_hir_root(
    psx_hir_module_t *module,
    const psx_semantic_context_t *semantic_context,
    const node_t *semantic_root,
    psx_typed_hir_build_failure_t *failure) {
  if (failure) memset(failure, 0, sizeof(*failure));
  if (!module || !semantic_context || !semantic_root) {
    if (failure) {
      failure->status = PSX_TYPED_HIR_BUILD_INVALID_INPUT;
      failure->source_node_kind = semantic_root
                                      ? (int)semantic_root->kind : -1;
    }
    return 0;
  }
  size_t node_checkpoint = psx_hir_module_checkpoint(module);
  size_t root_checkpoint = psx_hir_module_root_count(module);
  size_t symbol_checkpoint = psx_hir_module_symbol_checkpoint(module);
  hir_builder_t builder = {
      .module = module,
      .semantic_context = semantic_context,
      .failure = failure,
  };
  psx_hir_node_id_t root = build_node(&builder, semantic_root);
  if (root == PSX_HIR_NODE_ID_INVALID ||
      !psx_hir_module_add_root(module, root)) {
    if (failure && failure->status == PSX_TYPED_HIR_BUILD_OK)
      failure->status = PSX_TYPED_HIR_BUILD_OUT_OF_MEMORY;
    psx_hir_module_rollback(
        module, node_checkpoint, root_checkpoint, symbol_checkpoint);
    return 0;
  }
  return 1;
}

#include "vla_lowering.h"

#include "frame_layout.h"
#include "local_storage.h"
#include "runtime_context.h"
#include "../parser/decl.h"
#include "../parser/diag.h"
#include "../parser/local_registry.h"
#include "../parser/node_utils.h"
#include "../parser/type.h"
#include "../parser/type_builder.h"
#include "../diag/diag.h"
#include "../semantic/vla_runtime_plan.h"
#include "../semantic/resolved_object_ref.h"
#include "../type_layout.h"
#include <stdlib.h>
#include <string.h>

static int type_size(
    const psx_lowering_context_t *lowering_context,
    psx_type_id_t type_id) {
  return ps_type_sizeof_id(
      ps_lowering_semantic_types(lowering_context),
      ps_lowering_record_layouts(lowering_context),
      type_id,
      ag_target_info_data_layout(ps_lowering_target(lowering_context)));
}

static int type_alignment(
    const psx_lowering_context_t *lowering_context,
    psx_type_id_t type_id) {
  return ps_type_alignof_id(
      ps_lowering_semantic_types(lowering_context),
      ps_lowering_record_layouts(lowering_context), type_id,
      ag_target_info_data_layout(ps_lowering_target(lowering_context)));
}

static psx_qual_type_t pointee_value_type(
    const psx_lowering_context_t *lowering_context,
    psx_qual_type_t type) {
  return psx_semantic_type_table_pointee_value(
      ps_lowering_semantic_types(lowering_context), type.type_id);
}

static lvar_t *create_vla_storage(
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    char *name, int name_len, int storage_size, int alignment,
    psx_qual_type_t type, token_t *diagnostic_token) {
  if (!local_registry || !lowering_context ||
      type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  int offset = local_storage_allocate(
      lowering_context, storage_size, alignment);
  return ps_local_registry_create_storage_object_qual_type_in(
      local_registry,
      name, name_len, offset, storage_size, alignment, type,
      diagnostic_token);
}

static lvar_t *create_internal_vla_storage(
    psx_local_registry_t *local_registry,
    psx_lowering_context_t *lowering_context,
    char *name, int name_len, int storage_size,
    psx_qual_type_t type) {
  if (!local_registry || !lowering_context ||
      type.type_id == PSX_TYPE_ID_INVALID)
    return NULL;
  int offset = local_storage_allocate(
      lowering_context, storage_size, 0);
  return ps_local_registry_create_internal_storage_object_qual_type_in(
      local_registry, name, name_len, offset, storage_size, 0, type);
}

psx_vla_lowering_result_t lower_vla_declaration_plan(
    const psx_vla_lowering_request_t *request) {
  psx_vla_lowering_result_t result = {0};
  if (!request || !request->lowering_context) return result;
  ag_diagnostic_context_t *diagnostics =
      ps_lowering_diagnostics(request->lowering_context);
  int count = request->dimension_count;
  psx_qual_type_t element_type = pointee_value_type(
      request->lowering_context, request->type);
  int element_size = type_size(
      request->lowering_context, element_type.type_id);
  if (!request->local_registry ||
      request->type.type_id == PSX_TYPE_ID_INVALID || count <= 0 ||
      element_size <= 0 || !request->dimensions) {
    ps_diag_ctx_in(
        diagnostics, request->diag_tok, "vla-lowering", "%s",
        diag_message_for_in(
            diagnostics, DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }

  int has_constant_dimension = 0;
  for (int i = 0; i < count; i++) {
    const psx_vla_runtime_dimension_t *dimension =
        &request->dimensions[i];
    if (dimension->is_constant) {
      if (dimension->constant_value <= 0) return result;
      has_constant_dimension = 1;
    } else if (!dimension->expression) {
      return result;
    }
  }
  if (has_constant_dimension &&
      request->constant_qual_type.type_id == PSX_TYPE_ID_INVALID)
    return result;

  frame_vla_layout_t layout = frame_layout_vla_storage(
      count, count == 2 && request->dimensions[1].is_constant);
  arena_context_t *arena_context = ps_lowering_arena(
      request->lowering_context);
  int row_stride_offset = 0;
  int remaining_strides = 0;

  result.var = create_vla_storage(
      request->local_registry, request->lowering_context,
      request->name, request->name_len, layout.storage_size,
      request->requested_alignment, request->type,
      request->diag_tok);
  if (!result.var) return result;
  int var_offset = ps_lvar_offset(result.var);
  if (layout.row_stride_relative_offset > 0)
    row_stride_offset = var_offset + layout.row_stride_relative_offset;
  if (count >= 3) remaining_strides = layout.subsequent_stride_count;
  ps_local_registry_set_vla_descriptor(
      result.var, row_stride_offset, remaining_strides, 0, 0);

  psx_vla_runtime_plan_t *plan = arena_alloc_in(
      arena_context, sizeof(*plan));
  if (!plan) return result;
  plan->dimensions = arena_alloc_in(
      arena_context, (size_t)count * sizeof(*plan->dimensions));
  if (!plan->dimensions) return result;
  memcpy(
      plan->dimensions, request->dimensions,
      (size_t)count * sizeof(*plan->dimensions));
  plan->constant_qual_type = request->constant_qual_type;
  plan->dimension_count = count;
  plan->descriptor_frame_offset = var_offset;
  plan->row_stride_frame_offset = row_stride_offset;
  plan->element_size = element_size;
  plan->performs_allocation = 1;

  plan->stride_store_count =
      (row_stride_offset > 0 ? 1 : 0) + remaining_strides;
  if (plan->stride_store_count > 0) {
    plan->stride_store_offsets = arena_alloc_in(
        arena_context,
        (size_t)plan->stride_store_count *
            sizeof(*plan->stride_store_offsets));
    plan->stride_start_dimensions = arena_alloc_in(
        arena_context,
        (size_t)plan->stride_store_count *
            sizeof(*plan->stride_start_dimensions));
    if (!plan->stride_store_offsets ||
        !plan->stride_start_dimensions)
      return result;
  }
  int store_index = 0;
  if (row_stride_offset > 0) {
    plan->stride_store_offsets[store_index] = row_stride_offset;
    plan->stride_start_dimensions[store_index++] = 1;
  }
  for (int level = 1; level < count - 1; level++) {
    plan->stride_store_offsets[store_index] =
        frame_layout_vla_stride_offset(var_offset, level);
    plan->stride_start_dimensions[store_index++] = level + 1;
  }
  result.runtime_plan = plan;
  return result;
}

psx_vla_lowering_result_t lower_vla_declaration(
    const psx_vla_lowering_request_t *request) {
  psx_vla_lowering_result_t result =
      lower_vla_declaration_plan(request);
  if (result.runtime_plan && request && request->lowering_context) {
    result.init = ps_node_new_vla_runtime_in(
        ps_lowering_resolution_store(request->lowering_context),
        ps_lowering_arena(request->lowering_context),
        result.runtime_plan);
  }
  return result;
}

psx_vla_lowering_result_t lower_pointer_to_vla_declaration_plan(
    const psx_pointer_vla_lowering_request_t *request) {
  psx_vla_lowering_result_t result = {0};
  if (!request || !request->lowering_context) return result;
  ag_diagnostic_context_t *diagnostics =
      ps_lowering_diagnostics(request->lowering_context);
  psx_qual_type_t element_type = pointee_value_type(
      request->lowering_context, request->type);
  int element_size = type_size(
      request->lowering_context, element_type.type_id);
  if (!request->local_registry ||
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      !request->name || request->name_len <= 0 ||
      element_size <= 0 || !request->row_dimension) {
    ps_diag_ctx_in(
        diagnostics, request->diag_tok, "vla-lowering", "%s",
        diag_message_for_in(
            diagnostics, DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }

  frame_vla_layout_t layout = frame_layout_pointer_vla_storage();
  result.var = create_vla_storage(
      request->local_registry, request->lowering_context,
      request->name, request->name_len, layout.storage_size,
      request->requested_alignment, request->type,
      request->diag_tok);
  if (!result.var) return result;
  int row_stride_offset =
      ps_lvar_offset(result.var) + layout.row_stride_relative_offset;
  ps_local_registry_set_vla_descriptor(
      result.var, row_stride_offset, 0, 0, element_size);

  arena_context_t *arena_context = ps_lowering_arena(
      request->lowering_context);
  psx_vla_runtime_plan_t *plan = arena_alloc_in(
      arena_context, sizeof(*plan));
  if (!plan) return result;
  plan->dimensions = arena_alloc_in(
      arena_context, sizeof(*plan->dimensions));
  plan->stride_store_offsets = arena_alloc_in(
      arena_context, sizeof(*plan->stride_store_offsets));
  plan->stride_start_dimensions = arena_alloc_in(
      arena_context, sizeof(*plan->stride_start_dimensions));
  if (!plan->dimensions || !plan->stride_store_offsets ||
      !plan->stride_start_dimensions)
    return result;
  plan->dimensions[0] = (psx_vla_runtime_dimension_t){
      .expression = request->row_dimension,
  };
  plan->stride_store_offsets[0] = row_stride_offset;
  plan->stride_start_dimensions[0] = 0;
  plan->dimension_count = 1;
  plan->stride_store_count = 1;
  plan->row_stride_frame_offset = row_stride_offset;
  plan->element_size = element_size;
  result.runtime_plan = plan;
  return result;
}

psx_vla_lowering_result_t lower_pointer_to_vla_declaration(
    const psx_pointer_vla_lowering_request_t *request) {
  psx_vla_lowering_result_t result =
      lower_pointer_to_vla_declaration_plan(request);
  if (result.runtime_plan && request && request->lowering_context) {
    result.init = ps_node_new_vla_runtime_in(
        ps_lowering_resolution_store(request->lowering_context),
        ps_lowering_arena(request->lowering_context),
        result.runtime_plan);
  }
  return result;
}

static char *parameter_stride_storage_name(
    const char *name, int name_len, int *out_len) {
  static const char prefix[] = "__rs_";
  int prefix_len = (int)sizeof(prefix) - 1;
  int length = prefix_len + name_len;
  char *result = malloc((size_t)length + 1);
  if (!result) return NULL;
  memcpy(result, prefix, (size_t)prefix_len);
  memcpy(result + prefix_len, name, (size_t)name_len);
  result[length] = '\0';
  if (out_len) *out_len = length;
  return result;
}

psx_parameter_vla_lowering_result_t lower_parameter_vla_declaration(
    const psx_parameter_vla_lowering_request_t *request) {
  psx_parameter_vla_lowering_result_t result = {0};
  if (!request || !request->lowering_context) return result;
  ag_diagnostic_context_t *diagnostics =
      ps_lowering_diagnostics(request->lowering_context);
  int count = request->inner_dimension_count;
  psx_qual_type_t element_type = pointee_value_type(
      request->lowering_context, request->type);
  int element_size = type_size(
      request->lowering_context, element_type.type_id);
  int parameter_storage_size = type_size(
      request->lowering_context, request->type.type_id);
  int parameter_alignment = type_alignment(
      request->lowering_context, request->type.type_id);
  if (!request->local_registry ||
      request->type.type_id == PSX_TYPE_ID_INVALID ||
      !request->name || request->name_len <= 0 ||
      element_size <= 0 || parameter_storage_size <= 0 ||
      parameter_alignment <= 0 || count < 0 ||
      (count > 0 && !request->inner_dimensions)) {
    ps_diag_ctx_in(
        diagnostics, request->diag_tok, "vla-lowering", "%s",
        diag_message_for_in(
            diagnostics, DIAG_ERR_PARSER_ARRAY_SIZE_POSITIVE_REQUIRED));
  }

  result.var = create_vla_storage(
      request->local_registry, request->lowering_context,
      request->name, request->name_len,
      parameter_storage_size, parameter_alignment, request->type,
      request->diag_tok);
  if (!result.var) return result;

  int has_runtime_dimension = 0;
  for (int i = 0; i < count; i++) {
    if (!request->inner_dimensions[i].is_constant) {
      has_runtime_dimension = 1;
      break;
    }
  }

  if (has_runtime_dimension) {
    psx_qual_type_t stride_slot_type =
        psx_semantic_type_table_array_leaf(
            ps_lowering_semantic_types(request->lowering_context),
            request->stride_storage_type.type_id);
    if (stride_slot_type.type_id == PSX_TYPE_ID_INVALID ||
        type_size(request->lowering_context, stride_slot_type.type_id) !=
            PSX_VLA_RUNTIME_SLOT_SIZE) {
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "vla-lowering",
          "VLA runtime stride slot layout mismatch");
    }
    int stride_name_len = 0;
    char *stride_name = parameter_stride_storage_name(
        request->name, request->name_len, &stride_name_len);
    result.stride_storage = create_internal_vla_storage(
        request->local_registry, request->lowering_context,
        stride_name, stride_name_len,
        PSX_VLA_RUNTIME_SLOT_SIZE * count,
        request->stride_storage_type);
    if (!result.stride_storage) return result;

    int *constants = calloc((size_t)count, sizeof(*constants));
    int *source_offsets = calloc((size_t)count, sizeof(*source_offsets));
    if (!constants || !source_offsets) {
      free(constants);
      free(source_offsets);
      ps_diag_ctx_in(
          diagnostics, request->diag_tok, "param",
          "VLA parameter dimension allocation failed");
    }
    for (int i = 0; i < count; i++) {
      const psx_parameter_vla_dimension_t *dimension =
          &request->inner_dimensions[i];
      constants[i] = dimension->is_constant
                         ? (int)dimension->constant_value : 0;
      if (dimension->is_constant) continue;
      int source_offset =
          psx_typed_hir_tree_root_kind(dimension->expression) ==
                  PSX_HIR_LOCAL
              ? psx_typed_hir_tree_root_storage_offset(
                    dimension->expression)
              : 0;
      lvar_t *source = psx_decl_find_lvar_by_offset_in(
          request->local_registry, source_offset);
      if (!source || !ps_lvar_is_param(source)) {
        const char *source_name = source ? ps_lvar_name(source)
                                         : "<expression>";
        int source_name_len = source ? ps_lvar_name_len(source) : 12;
        ps_diag_ctx_in(
            diagnostics, request->diag_tok, "param",
            diag_message_for_in(
                diagnostics,
                DIAG_ERR_PARSER_VLA_PARAM_DIM_NOT_PRECEDING_PARAM),
            source_name_len, source_name);
      }
      source_offsets[i] = ps_lvar_offset(source);
    }

    ps_local_registry_set_vla_descriptor(
        result.var, ps_lvar_offset(result.stride_storage), count - 1,
        0, element_size);
    ps_local_registry_set_vla_param_inner_dims(
        request->local_registry, result.var,
        constants, source_offsets, count,
        request->diag_tok);
    if (count == 1 && constants[0] == 0) {
      ps_local_registry_set_vla_descriptor(
          result.var, ps_lvar_offset(result.stride_storage), 0,
          source_offsets[0], element_size);
    }
    free(constants);
    free(source_offsets);
  }

  /* Keep the current name ineligible while resolving preceding parameters. */
  ps_local_registry_mark_parameter(result.var, 0);
  return result;
}

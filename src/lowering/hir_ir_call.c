#include "hir_ir_builder_internal.h"
#include "function_type_lowering.h"
#include "../type_layout.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

int hir_ir_setup_parameter_bindings(
    hir_ir_context_t *context, const psx_hir_node_t *root,
    const ir_function_type_t *function_type) {
  if (hir_ir_child_count_for_edge(root, PSX_HIR_EDGE_PARAMETER) !=
      function_type->param_count) {
    context->status = IR_HIR_BUILD_INVALID;
    return 0;
  }
  for (size_t i = 0; i < function_type->param_count; i++) {
    const psx_hir_node_t *parameter = hir_ir_child_for_edge(
        context, root, PSX_HIR_EDGE_PARAMETER, i);
    if (!parameter || psx_hir_node_kind(parameter) != PSX_HIR_LOCAL) {
      context->status = IR_HIR_BUILD_INVALID;
      return 0;
    }
    ir_mir_type_info_t type = hir_ir_classify_node_type(
        context, parameter);
    if ((!hir_ir_is_scalar_value_type(type) &&
         !hir_ir_is_complex_type(type) &&
         type.type_class != IR_MIR_TYPE_AGGREGATE) ||
        function_type->params[i].type_id == PSX_TYPE_ID_INVALID) {
      context->status = IR_HIR_BUILD_UNSUPPORTED;
      return 0;
    }
    int minimum_size =
        (hir_ir_is_complex_type(type) ||
         type.type_class == IR_MIR_TYPE_AGGREGATE)
            ? type.source_size : 0;
    int minimum_align =
        minimum_size >= 8 ? 8 :
        minimum_size >= 4 ? 4 :
        minimum_size >= 2 ? 2 :
        minimum_size > 0 ? 1 : 0;
    int pointer = hir_ir_local_address_with_minimum(
        context, parameter, minimum_size, minimum_align);
    if (pointer < 0) return 0;
    ir_inst_t *binding = ir_inst_new(IR_PARAM_BIND);
    if (!binding) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return 0;
    }
    binding->src1 = ir_val_vreg(pointer, IR_TY_PTR);
    binding->parameter_index = i;
    if (!hir_ir_append_instruction(context, binding)) return 0;
  }
  return 1;
}

static psx_qual_type_t atomic_pointee_type(
    const hir_ir_context_t *context,
    const psx_hir_node_t *pointer_argument) {
  const psx_hir_node_t *current = pointer_argument;
  while (current) {
    psx_qual_type_t pointer_type = psx_hir_node_qual_type(current);
    psx_type_shape_t pointer = {0};
    if (hir_ir_type_shape(context, pointer_type.type_id, &pointer) &&
        (pointer.kind == PSX_TYPE_POINTER ||
         pointer.kind == PSX_TYPE_ARRAY)) {
      psx_qual_type_t pointee = psx_semantic_type_table_base(
          context->options->semantic_types, pointer_type.type_id);
      psx_type_shape_t pointee_type = {0};
      if (hir_ir_type_shape(context, pointee.type_id, &pointee_type) &&
          pointee_type.kind != PSX_TYPE_VOID)
        return pointee;
    }
    if (psx_hir_node_kind(current) != PSX_HIR_CAST) break;
    current = hir_ir_child_for_edge(
        context, current, PSX_HIR_EDGE_LHS, 0);
  }
  return (psx_qual_type_t){
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
}

static int atomic_operation_width(
    const hir_ir_context_t *context,
    const psx_hir_node_t *pointer_argument,
    psx_qual_type_t *object_qual_type,
    psx_type_shape_t *object_shape, int *is_unsigned) {
  psx_qual_type_t pointee = atomic_pointee_type(
      context, pointer_argument);
  psx_type_shape_t pointee_type = {0};
  int has_pointee_type = hir_ir_type_shape(
      context, pointee.type_id, &pointee_type);
  int width = psx_type_layout_sizeof(
      context->options->semantic_types, context->options->record_layouts,
      pointee.type_id, ag_target_info_data_layout(context->options->target));
  if (!has_pointee_type ||
      (pointee_type.kind != PSX_TYPE_BOOL &&
       pointee_type.kind != PSX_TYPE_INTEGER &&
       pointee_type.kind != PSX_TYPE_POINTER) ||
      (width != 1 && width != 2 && width != 4 && width != 8)) {
    return 0;
  }
  if (is_unsigned)
    *is_unsigned = pointee_type.kind == PSX_TYPE_POINTER ||
                   pointee_type.is_unsigned ? 1 : 0;
  if (object_qual_type) *object_qual_type = pointee;
  if (object_shape) *object_shape = pointee_type;
  return width;
}

static int atomic_value_matches_object(
    psx_type_kind_t object_kind, ir_val_t value) {
  return object_kind == PSX_TYPE_POINTER
             ? value.type == IR_TY_PTR
             : hir_ir_is_integer_type(value.type);
}

static ir_val_t scale_atomic_pointer_delta(
    hir_ir_context_t *context, const psx_hir_node_t *value_node,
    ir_val_t value, psx_qual_type_t pointer_object_type, int width) {
  psx_qual_type_t element_type = psx_semantic_type_table_base(
      context->options->semantic_types, pointer_object_type.type_id);
  int stride = psx_type_layout_sizeof(
      context->options->semantic_types, context->options->record_layouts,
      element_type.type_id,
      ag_target_info_data_layout(context->options->target));
  ir_mir_type_info_t value_info = hir_ir_classify_node_type(
      context, value_node);
  if (stride <= 0 ||
      value_info.type_class != IR_MIR_TYPE_INTEGER ||
      !hir_ir_is_integer_type(value.type))
    return hir_ir_unsupported_expr(context);
  ir_type_t delta_type = width == 8 ? IR_TY_I64 : IR_TY_I32;
  value = hir_ir_emit_integer_width_conversion(
      context, value, delta_type, !value_info.is_unsigned);
  if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
  return hir_ir_emit_integer_binary(
      context, IR_MUL, value,
      ir_val_imm(delta_type, stride), delta_type);
}

static ir_val_t hir_ir_build_atomic_call(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    const char *name, size_t name_length) {
  static const size_t prefix_length = 12;
  const char *suffix = name + prefix_length;
  size_t suffix_length = name_length - prefix_length;
  size_t argument_count = hir_ir_child_count_for_edge(
      node, PSX_HIR_EDGE_ARGUMENT);

  if (suffix_length == 5 && memcmp(suffix, "fence", 5) == 0) {
    if (argument_count != 0) return hir_ir_unsupported_expr(context);
    ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
    if (!atomic) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    atomic->atomic_kind = IR_ATOMIC_FENCE;
    if (!hir_ir_append_instruction(context, atomic)) return ir_val_none();
    return ir_val_imm(IR_TY_I32, 0);
  }

  const psx_hir_node_t *pointer_argument = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_ARGUMENT, 0);
  psx_qual_type_t object_qual_type = {
      PSX_TYPE_ID_INVALID, PSX_TYPE_QUALIFIER_NONE};
  psx_type_shape_t object_shape = {0};
  int is_unsigned = 0;
  int width = atomic_operation_width(
      context, pointer_argument, &object_qual_type,
      &object_shape, &is_unsigned);
  if (!pointer_argument || width == 0)
    return hir_ir_unsupported_expr(context);
  ir_val_t pointer = hir_ir_build_expr(context, pointer_argument);
  if (context->status != IR_HIR_BUILD_OK ||
      pointer.type != IR_TY_PTR)
    return hir_ir_unsupported_expr(context);
  ir_type_t value_type = object_shape.kind == PSX_TYPE_POINTER
                             ? IR_TY_PTR
                             : width == 8 ? IR_TY_I64 : IR_TY_I32;

  if (suffix_length == 4 && memcmp(suffix, "load", 4) == 0) {
    if (argument_count != 1) return hir_ir_unsupported_expr(context);
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
    if (!atomic) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    atomic->atomic_kind = IR_ATOMIC_LOAD;
    atomic->atomic_width = (unsigned char)width;
    atomic->is_unsigned = (unsigned char)is_unsigned;
    atomic->src1 = pointer;
    atomic->dst = ir_val_vreg(result, value_type);
    if (!hir_ir_append_instruction(context, atomic)) return ir_val_none();
    return atomic->dst;
  }

  if (suffix_length == 5 && memcmp(suffix, "store", 5) == 0) {
    if (argument_count != 2) return hir_ir_unsupported_expr(context);
    const psx_hir_node_t *value_node = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, 1);
    ir_val_t value = hir_ir_build_expr(context, value_node);
    if (context->status != IR_HIR_BUILD_OK ||
        !atomic_value_matches_object(object_shape.kind, value))
      return hir_ir_unsupported_expr(context);
    ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
    if (!atomic) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    atomic->atomic_kind = IR_ATOMIC_STORE;
    atomic->atomic_width = (unsigned char)width;
    atomic->src1 = pointer;
    atomic->src2 = value;
    if (!hir_ir_append_instruction(context, atomic)) return ir_val_none();
    return ir_val_imm(IR_TY_I32, 0);
  }

  if (suffix_length == 3 && memcmp(suffix, "cas", 3) == 0) {
    if (argument_count != 3) return hir_ir_unsupported_expr(context);
    const psx_hir_node_t *expected_node = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, 1);
    const psx_hir_node_t *desired_node = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, 2);
    ir_val_t expected = hir_ir_build_expr(context, expected_node);
    ir_val_t desired = hir_ir_build_expr(context, desired_node);
    if (context->status != IR_HIR_BUILD_OK ||
        expected.type != IR_TY_PTR ||
        !atomic_value_matches_object(object_shape.kind, desired))
      return hir_ir_unsupported_expr(context);
    int result = hir_ir_new_vreg(context);
    if (result < 0) return ir_val_none();
    ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
    if (!atomic) {
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
    atomic->atomic_kind = IR_ATOMIC_CAS;
    atomic->atomic_width = (unsigned char)width;
    atomic->is_unsigned = (unsigned char)is_unsigned;
    atomic->src1 = pointer;
    atomic->src2 = expected;
    atomic->src3 = desired;
    atomic->dst = ir_val_vreg(result, IR_TY_I32);
    if (!hir_ir_append_instruction(context, atomic)) return ir_val_none();
    return atomic->dst;
  }

  int rmw_operation = -1;
  if (suffix_length == 8 &&
      memcmp(suffix, "exchange", 8) == 0)
    rmw_operation = IR_ARMW_XCHG;
  else if (suffix_length == 9 &&
           memcmp(suffix, "fetch_add", 9) == 0)
    rmw_operation = IR_ARMW_ADD;
  else if (suffix_length == 9 &&
           memcmp(suffix, "fetch_sub", 9) == 0)
    rmw_operation = IR_ARMW_SUB;
  else if (suffix_length == 8 &&
           memcmp(suffix, "fetch_or", 8) == 0)
    rmw_operation = IR_ARMW_OR;
  else if (suffix_length == 9 &&
           memcmp(suffix, "fetch_and", 9) == 0)
    rmw_operation = IR_ARMW_AND;
  else if (suffix_length == 9 &&
           memcmp(suffix, "fetch_xor", 9) == 0)
    rmw_operation = IR_ARMW_XOR;
  if (rmw_operation < 0 || argument_count != 2)
    return hir_ir_unsupported_expr(context);

  const psx_hir_node_t *value_node = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_ARGUMENT, 1);
  ir_val_t value = hir_ir_build_expr(context, value_node);
  if (context->status != IR_HIR_BUILD_OK)
    return hir_ir_unsupported_expr(context);
  if (object_shape.kind == PSX_TYPE_POINTER) {
    if (rmw_operation == IR_ARMW_ADD || rmw_operation == IR_ARMW_SUB) {
      value = scale_atomic_pointer_delta(
          context, value_node, value, object_qual_type, width);
      if (context->status != IR_HIR_BUILD_OK) return ir_val_none();
    } else if (rmw_operation != IR_ARMW_XCHG || value.type != IR_TY_PTR) {
      return hir_ir_unsupported_expr(context);
    }
  } else if (!hir_ir_is_integer_type(value.type)) {
    return hir_ir_unsupported_expr(context);
  }
  int result = hir_ir_new_vreg(context);
  if (result < 0) return ir_val_none();
  ir_inst_t *atomic = ir_inst_new(IR_ATOMIC);
  if (!atomic) {
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  atomic->atomic_kind = IR_ATOMIC_RMW;
  atomic->atomic_rmw_op = (unsigned char)rmw_operation;
  atomic->atomic_width = (unsigned char)width;
  atomic->is_unsigned = (unsigned char)is_unsigned;
  atomic->src1 = pointer;
  atomic->src2 = value;
  atomic->dst = ir_val_vreg(result, value_type);
  if (!hir_ir_append_instruction(context, atomic)) return ir_val_none();
  return atomic->dst;
}

ir_val_t hir_ir_build_call(
    hir_ir_context_t *context, const psx_hir_node_t *node,
    ir_mir_type_info_t result_type) {
  size_t name_length = 0;
  const char *name = psx_hir_node_name(node, &name_length);
  const psx_hir_node_t *callee = hir_ir_child_for_edge(
      context, node, PSX_HIR_EDGE_CALLEE, 0);
  int is_direct = name && name_length > 0;
  if (name_length > INT_MAX || is_direct == (callee != NULL))
    return hir_ir_unsupported_expr(context);
  if (is_direct && name_length > 12 &&
      memcmp(name, "__ag_atomic_", 12) == 0)
    return hir_ir_build_atomic_call(context, node, name, name_length);
  psx_qual_type_t callable_type =
      psx_semantic_type_table_callable_function(
          context->options->semantic_types,
          psx_hir_node_attached_qual_type(node));
  ir_mir_type_context_t type_context = {
      .semantic_types = context->options->semantic_types,
      .record_layouts = context->options->record_layouts,
      .data_layout = ag_target_info_data_layout(context->options->target),
  };
  size_t argument_count = hir_ir_child_count_for_edge(
      node, PSX_HIR_EDGE_ARGUMENT);
  psx_type_shape_t callable_semantic_type = {0};
  int has_callable_semantic_type = hir_ir_type_shape(
      context, callable_type.type_id, &callable_semantic_type);
  size_t parameter_count =
      has_callable_semantic_type &&
              callable_semantic_type.parameter_count >= 0
          ? (size_t)callable_semantic_type.parameter_count : 0;
  int is_variadic = has_callable_semantic_type &&
      callable_semantic_type.is_variadic_function;
  int has_prototype = has_callable_semantic_type &&
      callable_semantic_type.has_function_prototype;
  psx_qual_type_t callable_result = psx_semantic_type_table_base(
      context->options->semantic_types, callable_type.type_id);
  int is_void_result =
      hir_ir_node_type_kind(context, node) == PSX_TYPE_VOID;
  int is_complex_result = hir_ir_is_complex_type(result_type);
  int is_aggregate_result =
      result_type.type_class == IR_MIR_TYPE_AGGREGATE;
  if (callable_type.type_id == PSX_TYPE_ID_INVALID ||
      !has_callable_semantic_type ||
      callable_semantic_type.kind != PSX_TYPE_FUNCTION ||
      argument_count > INT_MAX ||
      (is_variadic
           ? argument_count < parameter_count
           : argument_count != parameter_count && has_prototype) ||
      callable_result.type_id != psx_hir_node_qual_type(node).type_id ||
      (!is_void_result && !is_complex_result &&
       !is_aggregate_result &&
       !hir_ir_is_direct_value_type(result_type)))
    return hir_ir_unsupported_expr(context);
  ir_val_t callee_value = ir_val_none();
  if (callee) {
    ir_mir_type_info_t callee_type = hir_ir_classify_node_type(context, callee);
    callee_value = hir_ir_build_expr(context, callee);
    if (context->status != IR_HIR_BUILD_OK ||
        callee_type.type_class != IR_MIR_TYPE_POINTER ||
        callee_value.type != IR_TY_PTR)
      return hir_ir_unsupported_expr(context);
  }

  ir_call_argument_t *arguments = NULL;
  size_t emitted_count = 0;
  if (argument_count) {
    arguments = calloc(argument_count, sizeof(*arguments));
    if (!arguments) {
      free(arguments);
      context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
      return ir_val_none();
    }
  }
  for (size_t i = 0; i < argument_count; i++) {
    const psx_hir_node_t *argument = hir_ir_child_for_edge(
        context, node, PSX_HIR_EDGE_ARGUMENT, i);
    ir_mir_type_info_t argument_type = hir_ir_classify_node_type(
        context, argument);
    ir_mir_type_info_t parameter_type;
    psx_qual_type_t lowered_argument_type =
        psx_hir_node_qual_type(argument);
    if (i < parameter_count) {
      lowered_argument_type = psx_semantic_type_table_parameter(
          context->options->semantic_types,
          callable_type.type_id, (int)i);
      parameter_type = ir_mir_classify_type_id(
          &type_context, lowered_argument_type.type_id);
    } else {
      parameter_type = argument_type;
      if (argument_type.type_class == IR_MIR_TYPE_INTEGER &&
          argument_type.source_size < 4) {
        parameter_type.type = IR_TY_I32;
        parameter_type.source_size = 4;
        parameter_type.is_unsigned = ir_mir_integer_promotion_is_unsigned(
            argument_type,
            ag_target_info_data_layout(context->options->target));
      }
    }
    if (i >= parameter_count &&
        argument_type.type_class == IR_MIR_TYPE_AGGREGATE) {
      int rounded_size =
          ((argument_type.source_size + 7) / 8) * 8;
      if (argument_type.source_size <= 0 ||
          rounded_size < argument_type.source_size ||
          emitted_count >= argument_count) {
        free(arguments);
        return hir_ir_unsupported_expr(context);
      }
      ir_val_t source = hir_ir_aggregate_value_address(context, argument);
      int temporary = hir_ir_allocate_scalar_temp(context, rounded_size, 8);
      if (context->status != IR_HIR_BUILD_OK ||
          source.type != IR_TY_PTR || temporary < 0) {
        free(arguments);
        return ir_val_none();
      }
      ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
      if (!copy) {
        free(arguments);
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      copy->src1 = ir_val_vreg(temporary, IR_TY_PTR);
      copy->src2 = source;
      copy->alloca_size = argument_type.source_size;
      if (!hir_ir_append_instruction(context, copy)) {
        free(arguments);
        return ir_val_none();
      }
      arguments[emitted_count++] = (ir_call_argument_t){
          .value = ir_val_vreg(temporary, IR_TY_PTR),
          .type = lowered_argument_type,
          .representation = IR_CALL_ARGUMENT_ADDRESS,
      };
      continue;
    }
    if (i < parameter_count &&
        hir_ir_is_complex_type(parameter_type)) {
      if (!hir_ir_is_complex_type(argument_type) ||
          emitted_count >= argument_count) {
        free(arguments);
        return hir_ir_unsupported_expr(context);
      }
      ir_val_t pointer = hir_ir_materialize_complex_operand(
          context, argument, parameter_type);
      if (context->status != IR_HIR_BUILD_OK ||
          pointer.type != IR_TY_PTR) {
        free(arguments);
        return ir_val_none();
      }
      arguments[emitted_count++] = (ir_call_argument_t){
          .value = pointer,
          .type = lowered_argument_type,
          .representation = IR_CALL_ARGUMENT_ADDRESS,
      };
      continue;
    }
    if (i < parameter_count &&
        parameter_type.type_class == IR_MIR_TYPE_AGGREGATE) {
      if (argument_type.type_class != IR_MIR_TYPE_AGGREGATE ||
          argument_type.source_size != parameter_type.source_size ||
          emitted_count >= argument_count) {
        free(arguments);
        return hir_ir_unsupported_expr(context);
      }
      ir_val_t source = hir_ir_aggregate_value_address(context, argument);
      int temporary = hir_ir_allocate_scalar_temp(
          context, parameter_type.source_size, 8);
      if (context->status != IR_HIR_BUILD_OK ||
          source.type != IR_TY_PTR || temporary < 0) {
        free(arguments);
        return ir_val_none();
      }
      ir_inst_t *copy = ir_inst_new(IR_MEMCPY);
      if (!copy) {
        free(arguments);
        context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
        return ir_val_none();
      }
      copy->src1 = ir_val_vreg(temporary, IR_TY_PTR);
      copy->src2 = source;
      copy->alloca_size = parameter_type.source_size;
      if (!hir_ir_append_instruction(context, copy)) {
        free(arguments);
        return ir_val_none();
      }
      arguments[emitted_count++] = (ir_call_argument_t){
          .value = ir_val_vreg(temporary, IR_TY_PTR),
          .type = lowered_argument_type,
          .representation = IR_CALL_ARGUMENT_ADDRESS,
      };
      continue;
    }
    if (i >= parameter_count && hir_ir_is_float_value_type(argument_type) &&
        argument_type.type == IR_TY_F32) {
      parameter_type.type = IR_TY_F64;
      parameter_type.source_size = 8;
    }
    if (!argument || !hir_ir_is_direct_value_type(parameter_type)) {
      free(arguments);
      return hir_ir_unsupported_expr(context);
    }
    ir_val_t value = hir_ir_build_expr(context, argument);
    if (context->status == IR_HIR_BUILD_OK) {
      value = hir_ir_coerce_direct_value_to_qual_type(
          context, value, argument_type,
          parameter_type,
          lowered_argument_type);
    }
    if (context->status != IR_HIR_BUILD_OK) {
      free(arguments);
      return ir_val_none();
    }
    arguments[emitted_count++] = (ir_call_argument_t){
        .value = value,
        .type = lowered_argument_type,
        .representation = IR_CALL_ARGUMENT_VALUE,
    };
  }

  int result_vreg = -1;
  if (!is_void_result && !is_complex_result && !is_aggregate_result) {
    result_vreg = hir_ir_new_vreg(context);
    if (result_vreg < 0) {
      free(arguments);
      return ir_val_none();
    }
  }
  ir_inst_t *call = ir_inst_new(IR_CALL);
  if (!call) {
    free(arguments);
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  if (is_complex_result || is_aggregate_result) {
    int slot = hir_ir_allocate_scalar_temp(
        context, result_type.source_size,
        hir_ir_type_size_for_target(context, result_type.type) >= 8
            ? 8 : 4);
    if (slot < 0) {
      free(arguments);
      free(call);
      return ir_val_none();
    }
    call->result_storage = ir_val_vreg(slot, IR_TY_PTR);
  } else if (!is_void_result) {
    call->dst = ir_val_vreg(result_vreg, result_type.type);
  }
  if (is_direct) {
    call->sym = (char *)name;
    call->sym_len = (int)name_length;
  } else {
    call->callee = callee_value;
  }
  call->args = arguments;
  if (emitted_count > INT_MAX) {
    free(arguments);
    free(call);
    return hir_ir_unsupported_expr(context);
  }
  call->nargs = (int)emitted_count;
  call->is_void_call = is_void_result ? 1 : 0;
  call->is_implicit_call =
      psx_hir_node_is_implicit_call(node) ? 1 : 0;
  if (!ir_function_type_from_type_id(
          context->options->semantic_types,
          callable_type.type_id, &call->function_type)) {
    free(arguments);
    free(call);
    context->status = IR_HIR_BUILD_OUT_OF_MEMORY;
    return ir_val_none();
  }
  call->has_function_type = 1;
  if (!hir_ir_append_instruction(context, call)) return ir_val_none();
  if (is_void_result) return ir_val_none();
  if (is_complex_result || is_aggregate_result)
    return call->result_storage;
  return ir_val_vreg(result_vreg, result_type.type);
}

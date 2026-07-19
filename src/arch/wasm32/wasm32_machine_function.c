#include "wasm32_machine_function.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "wasm32_machine_ir.h"

static int align_to(int value, int alignment) {
  if (alignment <= 1) return value;
  return (value + alignment - 1) & ~(alignment - 1);
}

static int valid_vreg(
    const wasm32_machine_function_t *function, ir_val_t value) {
  return value.id >= 0 && value.id < function->vreg_count;
}

static void note_value(
    wasm32_machine_function_t *function, ir_val_t value) {
  if (!valid_vreg(function, value)) return;
  function->vreg_used[value.id] = 1;
  if (function->vreg_types[value.id] == IR_TY_I32)
    function->vreg_types[value.id] =
        wasm32_machine_value_type(value.type);
}

static int force_i32(
    wasm32_machine_function_t *function,
    unsigned char *forced_i32, ir_val_t value) {
  if (!valid_vreg(function, value)) return 0;
  int changed = function->vreg_types[value.id] != IR_TY_I32 ||
                !forced_i32[value.id];
  function->vreg_types[value.id] = IR_TY_I32;
  forced_i32[value.id] = 1;
  return changed;
}

static wasm32_machine_alloca_t *find_alloca(
    wasm32_machine_function_t *function, int vreg) {
  if (!function) return NULL;
  for (int i = 0; i < function->alloca_count; i++) {
    if (function->allocas[i].vreg == vreg) return &function->allocas[i];
  }
  return NULL;
}

const wasm32_machine_inst_t *wasm32_machine_function_instruction(
    const wasm32_machine_function_t *function,
    const ir_inst_t *source_instruction) {
  if (!function || !source_instruction) return NULL;
  for (int i = 0; i < function->instruction_count; i++) {
    if (function->instructions[i].source == source_instruction)
      return &function->instructions[i];
  }
  return NULL;
}

const wasm32_machine_block_t *wasm32_machine_function_block(
    const wasm32_machine_function_t *function, int block_id) {
  if (!function) return NULL;
  for (int i = 0; i < function->block_count; i++) {
    if (function->blocks[i].id == block_id) return &function->blocks[i];
  }
  return NULL;
}

const wasm32_machine_alloca_t *wasm32_machine_function_alloca(
    const wasm32_machine_function_t *function, int vreg) {
  if (!function) return NULL;
  for (int i = 0; i < function->alloca_count; i++) {
    if (function->allocas[i].vreg == vreg) return &function->allocas[i];
  }
  return NULL;
}

static int collect_allocas(wasm32_machine_function_t *function) {
  int count = 0;
  for (ir_block_t *block = function->source->entry; block;
       block = block->next) {
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next) {
      if (instruction->op == IR_ALLOCA) count++;
    }
  }
  if (count > 0) {
    function->allocas = calloc((size_t)count, sizeof(*function->allocas));
    if (!function->allocas) return 0;
  }
  int frame_size = 0;
  int index = 0;
  for (ir_block_t *block = function->source->entry; block;
       block = block->next) {
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next) {
      if (instruction->op != IR_ALLOCA) continue;
      int alignment = instruction->alloca_align > 0
                          ? instruction->alloca_align
                          : 4;
      frame_size = align_to(frame_size, alignment);
      function->allocas[index++] = (wasm32_machine_alloca_t){
          .vreg = instruction->dst.id,
          .offset = frame_size,
          .size = instruction->alloca_size,
          .value_type = IR_TY_VOID,
      };
      frame_size += instruction->alloca_size;
    }
  }
  function->alloca_count = count;
  function->frame_size = align_to(frame_size, 16);
  return 1;
}

static int is_integer_comparison(ir_op_t op) {
  return op == IR_EQ || op == IR_NE || op == IR_LT || op == IR_LE ||
         op == IR_ULT || op == IR_ULE;
}

static int is_integer_binary(ir_op_t op) {
  return op == IR_ADD || op == IR_SUB || op == IR_MUL || op == IR_DIV ||
         op == IR_UDIV || op == IR_MOD || op == IR_UMOD || op == IR_AND ||
         op == IR_OR || op == IR_XOR || op == IR_SHL || op == IR_SHR ||
         op == IR_LSR || is_integer_comparison(op);
}

static int is_float_binary(ir_op_t op) {
  return op == IR_FADD || op == IR_FSUB || op == IR_FMUL ||
         op == IR_FDIV || op == IR_FEQ || op == IR_FNE ||
         op == IR_FLT || op == IR_FLE;
}

static int is_conversion(ir_op_t op) {
  return op == IR_ZEXT || op == IR_SEXT || op == IR_TRUNC ||
         op == IR_I2F || op == IR_F2I || op == IR_F2F;
}

static ir_type_t memory_access_type(ir_type_t raw, ir_type_t actual) {
  if (raw == IR_TY_I8 || raw == IR_TY_I16) return raw;
  if (raw == IR_TY_PTR)
    return actual == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32;
  if (actual == IR_TY_I32 && raw != IR_TY_I32) return IR_TY_I32;
  return raw;
}

static ir_type_t atomic_memory_type(const ir_inst_t *instruction) {
  switch (instruction->atomic_width ? instruction->atomic_width : 4) {
    case 1: return IR_TY_I8;
    case 2: return IR_TY_I16;
    case 4: return IR_TY_I32;
    case 8: return IR_TY_I64;
    default: return IR_TY_VOID;
  }
}

static ir_type_t binary_operand_type(
    const wasm32_machine_function_t *function,
    const ir_inst_t *instruction) {
  ir_type_t lhs = wasm32_machine_function_vreg_type(
      function, instruction->src1);
  ir_type_t rhs = wasm32_machine_function_vreg_type(
      function, instruction->src2);
  ir_type_t dst = wasm32_machine_function_vreg_type(
      function, instruction->dst);
  if (is_float_binary(instruction->op)) return lhs;
  if (is_integer_comparison(instruction->op))
    return lhs == IR_TY_I64 || rhs == IR_TY_I64
               ? IR_TY_I64
               : IR_TY_I32;
  if (instruction->op == IR_SHL || instruction->op == IR_SHR ||
      instruction->op == IR_LSR)
    return lhs == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32;
  return dst == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32;
}

static int select_call(
    const ir_abi_module_t *abi_module,
    const ir_inst_t *instruction,
    wasm32_machine_call_t *call) {
  const ir_abi_signature_t *abi =
      ir_abi_call_signature(abi_module, instruction);
  if (!abi || abi->fixed_param_count > INT_MAX) return 0;
  size_t argument_count = 0;
  const ir_abi_argument_t *arguments =
      ir_abi_call_arguments(abi_module, instruction, &argument_count);
  if (argument_count > INT_MAX ||
      (argument_count > 0 && !arguments) ||
      !wasm32_machine_call_signature(
          instruction, abi, &call->signature))
    return 0;
  if (argument_count > 0) {
    call->arguments = calloc(
        argument_count, sizeof(*call->arguments));
    if (!call->arguments) {
      wasm32_machine_signature_dispose(&call->signature);
      return 0;
    }
    for (size_t i = 0; i < argument_count; i++) {
      wasm32_machine_argument_t *selected = &call->arguments[i];
      selected->source = arguments[i].source;
      selected->value_type = arguments[i].type;
      selected->byte_offset = arguments[i].byte_offset;
      if (arguments[i].access == IR_ABI_ARGUMENT_DIRECT) {
        selected->access = WASM32_MACHINE_ARGUMENT_DIRECT;
      } else if (arguments[i].access == IR_ABI_ARGUMENT_LOAD &&
                 arguments[i].source.type == IR_TY_PTR &&
                 wasm32_machine_select_load(
                     arguments[i].type, 1, &selected->load)) {
        selected->access = WASM32_MACHINE_ARGUMENT_LOAD;
      } else {
        return 0;
      }
    }
  }
  call->argument_count = (int)argument_count;
  call->fixed_argument_count = (int)abi->fixed_param_count;
  call->result_area = abi->result_area;
  call->direct_result_type =
      abi->result_count == 1 && abi->result_pieces &&
              !call->signature.has_hidden_result
          ? abi->result_pieces[0].type
          : IR_TY_VOID;
  if (call->signature.has_direct_aggregate_result &&
      !wasm32_machine_select_store(
          call->direct_result_type, &call->direct_result_store))
    return 0;
  call->is_indirect =
      instruction->callee.id != IR_VAL_NONE ? 1 : 0;
  call->is_variadic = abi->is_variadic ? 1 : 0;
  if (call->is_variadic &&
      call->argument_count > call->fixed_argument_count) {
    call->variadic_argument_count =
        call->argument_count - call->fixed_argument_count;
    call->variadic_area_size = align_to(
        call->variadic_argument_count * 8, 16);
    call->variadic_arguments = calloc(
        (size_t)call->variadic_argument_count,
        sizeof(*call->variadic_arguments));
    if (!call->variadic_arguments) return 0;
    for (int i = 0; i < call->variadic_argument_count; i++) {
      int argument_index = call->fixed_argument_count + i;
      wasm32_machine_variadic_argument_t *variadic =
          &call->variadic_arguments[i];
      ir_type_t source_type = wasm32_machine_value_type(
          call->arguments[argument_index].value_type);
      ir_type_t storage_type;
      variadic->argument_index = argument_index;
      variadic->byte_offset = i * 8;
      if (source_type == IR_TY_F64) {
        variadic->argument_type = IR_TY_F64;
        storage_type = IR_TY_F64;
      } else if (source_type == IR_TY_F32) {
        variadic->argument_type = IR_TY_F32;
        storage_type = IR_TY_F64;
      } else {
        variadic->argument_type = IR_TY_I64;
        storage_type = IR_TY_I64;
      }
      if (!wasm32_machine_select_conversion(
              variadic->argument_type, storage_type, 1,
              &variadic->conversion) ||
          !wasm32_machine_select_store(
              storage_type, &variadic->store))
        return 0;
    }
  }
  if ((call->signature.has_hidden_result ||
       call->signature.has_direct_aggregate_result) &&
      call->result_area.id == IR_VAL_NONE)
    return 0;
  return 1;
}

static int select_parameter_bind(
    const ir_abi_signature_t *function_abi,
    const ir_inst_t *instruction,
    wasm32_machine_parameter_bind_t *binding) {
  if (!function_abi || !function_abi->param_pieces) return 0;
  size_t source_index = instruction->parameter_index;
  size_t first = function_abi->param_count;
  size_t count = 0;
  for (size_t i = 0; i < function_abi->param_count; i++) {
    if (function_abi->param_pieces[i].source_index != source_index)
      continue;
    if (first == function_abi->param_count) first = i;
    count++;
  }
  if (count == 0 || count > INT_MAX || first > INT_MAX) return 0;
  binding->pieces = malloc(count * sizeof(*binding->pieces));
  binding->stores = calloc(count, sizeof(*binding->stores));
  binding->copy_plans = calloc(count, sizeof(*binding->copy_plans));
  if (!binding->pieces || !binding->stores || !binding->copy_plans)
    return 0;
  int output = 0;
  for (size_t i = 0; i < function_abi->param_count; i++) {
    if (function_abi->param_pieces[i].source_index == source_index)
      binding->pieces[output++] = (wasm32_machine_parameter_piece_t){
          .value_type = function_abi->param_pieces[i].type,
          .source_size = function_abi->param_pieces[i].source_size,
          .byte_offset = function_abi->param_pieces[i].byte_offset,
          .kind = function_abi->param_pieces[i].kind ==
                          IR_ABI_PIECE_INDIRECT
                      ? WASM32_MACHINE_PARAMETER_INDIRECT
                      : WASM32_MACHINE_PARAMETER_DIRECT,
      };
  }
  binding->piece_count = (int)count;
  binding->physical_index =
      (int)first +
      (function_abi->result_count == 1 &&
               function_abi->result_pieces &&
               function_abi->result_pieces[0].kind ==
                   IR_ABI_PIECE_INDIRECT
           ? 1
           : 0);
  for (int i = 0; i < binding->piece_count; i++) {
    if (binding->pieces[i].kind ==
        WASM32_MACHINE_PARAMETER_INDIRECT) {
      if (!wasm32_machine_copy_plan_build(
              binding->pieces[i].source_size,
              &binding->copy_plans[i]))
        return 0;
    } else if (!wasm32_machine_select_store(
                   binding->pieces[i].value_type,
                   &binding->stores[i])) {
      return 0;
    }
  }
  return 1;
}

static int select_instruction(
    const wasm32_machine_function_t *function,
    wasm32_machine_inst_t *selected) {
  ir_inst_t *instruction = selected->source;
  switch (instruction->op) {
    case IR_NOP:
      selected->kind = WASM32_MACHINE_INST_NOP;
      return 1;
    case IR_ALLOCA:
      selected->kind = WASM32_MACHINE_INST_ALLOCA;
      return 1;
    case IR_LOAD_IMM:
      selected->kind = WASM32_MACHINE_INST_INTEGER_CONSTANT;
      return 1;
    case IR_LOAD_FP_IMM:
      selected->kind = WASM32_MACHINE_INST_FLOAT_CONSTANT;
      return 1;
    case IR_LOAD_STR:
      selected->kind = WASM32_MACHINE_INST_STRING_ADDRESS;
      return 1;
    case IR_LOAD_SYM:
      selected->kind = WASM32_MACHINE_INST_SYMBOL_ADDRESS;
      return 1;
    case IR_LOAD_TLV_ADDR:
      selected->kind = WASM32_MACHINE_INST_TLS_ADDRESS;
      return 1;
    case IR_MEMCPY:
      selected->kind = WASM32_MACHINE_INST_MEMORY_COPY;
      return wasm32_machine_copy_plan_build(
          instruction->alloca_size, &selected->copy);
    case IR_ALIGN_PTR:
      selected->kind = WASM32_MACHINE_INST_ALIGN_POINTER;
      return 1;
    case IR_VLA_ALLOC:
      selected->kind = WASM32_MACHINE_INST_DYNAMIC_ALLOCA;
      return 1;
    case IR_VARARG_CURSOR:
      selected->kind = WASM32_MACHINE_INST_VARARG_AREA;
      return 1;
    case IR_LEA:
      selected->kind = WASM32_MACHINE_INST_ADDRESS_ADD;
      return 1;
    default:
      break;
  }
  if (is_integer_binary(instruction->op) ||
      is_float_binary(instruction->op)) {
    selected->kind = WASM32_MACHINE_INST_BINARY;
    return wasm32_machine_select_binary(
        instruction->op,
        binary_operand_type(function, instruction),
        &selected->binary);
  }
  if (instruction->op == IR_NEG || instruction->op == IR_NOT ||
      instruction->op == IR_FNEG) {
    ir_type_t operand_type = instruction->op == IR_FNEG
                                 ? wasm32_machine_function_vreg_type(
                                       function, instruction->src1)
                                 : wasm32_machine_function_vreg_type(
                                       function, instruction->dst);
    selected->kind = WASM32_MACHINE_INST_UNARY;
    return wasm32_machine_select_unary(
        instruction->op, operand_type, &selected->unary);
  }
  if (is_conversion(instruction->op)) {
    int is_unsigned =
        instruction->op == IR_ZEXT ? 1 :
        instruction->op == IR_SEXT ? 0 :
        (instruction->op == IR_I2F || instruction->op == IR_F2I)
            ? instruction->is_unsigned ||
                  wasm32_machine_function_vreg_is_unsigned(
                      function, instruction->src1)
            : wasm32_machine_function_vreg_is_unsigned(
                  function, instruction->src1);
    selected->kind = WASM32_MACHINE_INST_CONVERSION;
    return wasm32_machine_select_conversion(
        wasm32_machine_function_vreg_type(function, instruction->src1),
        wasm32_machine_function_vreg_type(function, instruction->dst),
        is_unsigned, &selected->conversion);
  }
  if (instruction->op == IR_LOAD) {
    ir_type_t memory_type = memory_access_type(
        instruction->dst.type,
        wasm32_machine_function_vreg_type(function, instruction->dst));
    selected->kind = WASM32_MACHINE_INST_LOAD;
    return wasm32_machine_select_load(
        memory_type, instruction->is_unsigned, &selected->load);
  }
  if (instruction->op == IR_STORE) {
    ir_type_t memory_type = memory_access_type(
        instruction->src2.type,
        wasm32_machine_function_vreg_type(function, instruction->src2));
    selected->kind = WASM32_MACHINE_INST_STORE;
    return wasm32_machine_select_store(memory_type, &selected->store);
  }
  if (instruction->op == IR_ATOMIC) {
    selected->kind = WASM32_MACHINE_INST_ATOMIC;
    if (instruction->atomic_kind == IR_ATOMIC_FENCE) {
      selected->atomic.kind = WASM32_MACHINE_ATOMIC_FENCE;
      return 1;
    }
    ir_type_t memory_type = atomic_memory_type(instruction);
    if (memory_type == IR_TY_VOID) return 0;
    if (instruction->atomic_kind == IR_ATOMIC_LOAD ||
        instruction->atomic_kind == IR_ATOMIC_RMW ||
        instruction->atomic_kind == IR_ATOMIC_CAS) {
      if (!wasm32_machine_select_load(
              memory_type, instruction->is_unsigned,
              &selected->atomic.load))
        return 0;
    }
    if (instruction->atomic_kind == IR_ATOMIC_STORE ||
        instruction->atomic_kind == IR_ATOMIC_RMW ||
        instruction->atomic_kind == IR_ATOMIC_CAS) {
      if (!wasm32_machine_select_store(
              memory_type, &selected->atomic.store))
        return 0;
    }
    if (instruction->atomic_kind == IR_ATOMIC_LOAD) {
      selected->atomic.kind = WASM32_MACHINE_ATOMIC_LOAD;
    } else if (instruction->atomic_kind == IR_ATOMIC_STORE) {
      selected->atomic.kind = WASM32_MACHINE_ATOMIC_STORE;
    } else if (instruction->atomic_kind == IR_ATOMIC_RMW &&
               instruction->atomic_rmw_op == IR_ARMW_XCHG) {
      selected->atomic.kind = WASM32_MACHINE_ATOMIC_EXCHANGE;
    } else if (instruction->atomic_kind == IR_ATOMIC_RMW) {
      selected->atomic.kind = WASM32_MACHINE_ATOMIC_RMW;
      if (!wasm32_machine_select_atomic_rmw(
          (ir_atomic_rmw_op_t)instruction->atomic_rmw_op,
          memory_type == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32,
          &selected->atomic.binary))
        return 0;
    } else if (instruction->atomic_kind == IR_ATOMIC_CAS) {
      selected->atomic.kind = WASM32_MACHINE_ATOMIC_COMPARE_EXCHANGE;
      if (!wasm32_machine_select_binary(
              IR_EQ,
              memory_type == IR_TY_I64 ? IR_TY_I64 : IR_TY_I32,
              &selected->atomic.comparison))
        return 0;
    } else {
      return 0;
    }
    return 1;
  }
  if (instruction->op == IR_LABEL || instruction->op == IR_BR ||
      instruction->op == IR_BR_COND || instruction->op == IR_RET ||
      instruction->op == IR_CONTINUATION_SUSPEND) {
    selected->kind = WASM32_MACHINE_INST_CONTROL;
    selected->control.target_block_id = instruction->label_id;
    selected->control.else_block_id = instruction->else_label_id;
    if (instruction->op == IR_LABEL)
      selected->control.kind = WASM32_MACHINE_CONTROL_LABEL;
    else if (instruction->op == IR_BR)
      selected->control.kind = WASM32_MACHINE_CONTROL_BRANCH;
    else if (instruction->op == IR_BR_COND) {
      selected->control.kind =
          WASM32_MACHINE_CONTROL_BRANCH_CONDITIONAL;
      selected->control.value = instruction->src1;
    } else if (instruction->op == IR_RET) {
      selected->control.kind = WASM32_MACHINE_CONTROL_RETURN;
      selected->control.value = instruction->src1;
    } else {
      selected->control.kind = WASM32_MACHINE_CONTROL_SUSPEND;
    }
  }
  return 1;
}

static int initialize_instructions(
    wasm32_machine_function_t *function,
    const ir_abi_module_t *abi_module,
    const ir_abi_signature_t *function_abi) {
  int count = 0;
  int block_count = 0;
  for (ir_block_t *block = function->source->entry; block;
       block = block->next) {
    block_count++;
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next)
      count++;
  }
  if (count > 0) {
    function->instructions =
        calloc((size_t)count, sizeof(*function->instructions));
    if (!function->instructions) return 0;
  }
  if (block_count > 0) {
    function->blocks =
        calloc((size_t)block_count, sizeof(*function->blocks));
    if (!function->blocks) return 0;
  }
  function->instruction_count = count;
  function->block_count = block_count;
  int index = 0;
  int block_index = 0;
  for (ir_block_t *block = function->source->entry; block;
       block = block->next) {
    wasm32_machine_block_t *machine_block =
        &function->blocks[block_index++];
    machine_block->source = block;
    machine_block->id = block->id;
    machine_block->first_instruction = index;
    machine_block->next_block_id = block->next ? block->next->id : -1;
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next) {
      wasm32_machine_inst_t *machine_instruction =
          &function->instructions[index];
      machine_instruction->source = instruction;
      machine_instruction->op = instruction->op;
      machine_instruction->dst = instruction->dst;
      machine_instruction->src1 = instruction->src1;
      machine_instruction->src2 = instruction->src2;
      machine_instruction->src3 = instruction->src3;
      machine_instruction->callee = instruction->callee;
      machine_instruction->result_storage = instruction->result_storage;
      machine_instruction->sym = instruction->sym;
      machine_instruction->sym_len = instruction->sym_len;
      machine_instruction->object_size = instruction->object_size;
      machine_instruction->alloca_size = instruction->alloca_size;
      machine_instruction->alloca_align = instruction->alloca_align;
      machine_instruction->parameter_index = instruction->parameter_index;
      machine_instruction->is_unsigned = instruction->is_unsigned;
      machine_instruction->is_function_symbol =
          instruction->is_function_symbol;
      machine_instruction->is_implicit_call = instruction->is_implicit_call;
      if (instruction->op == IR_LOAD_SYM &&
          instruction->is_function_symbol) {
        const ir_abi_signature_t *reference_abi =
            ir_abi_reference_signature(abi_module, instruction);
        if (reference_abi &&
            !wasm32_machine_signature_from_abi(
                reference_abi, 1,
                &machine_instruction->reference_signature))
          return 0;
        if (reference_abi)
          machine_instruction->has_reference_signature = 1;
      }
      if (instruction->op == IR_CALL) {
        machine_instruction->kind = WASM32_MACHINE_INST_CALL;
        if (!select_call(
                abi_module, instruction,
                &machine_instruction->call))
          return 0;
      } else if (instruction->op == IR_PARAM_BIND) {
        machine_instruction->kind =
            WASM32_MACHINE_INST_PARAMETER_BIND;
        if (!select_parameter_bind(
                function_abi, instruction,
                &machine_instruction->parameter_bind))
          return 0;
      }
      index++;
    }
    machine_block->instruction_count =
        index - machine_block->first_instruction;
    ir_inst_t *tail = block->tail;
    machine_block->has_terminator =
        tail && (tail->op == IR_BR || tail->op == IR_BR_COND ||
                 tail->op == IR_RET ||
                 tail->op == IR_CONTINUATION_SUSPEND)
            ? 1
            : 0;
  }
  return 1;
}

static int select_instructions(wasm32_machine_function_t *function) {
  for (int i = 0; i < function->instruction_count; i++) {
    if (function->instructions[i].kind == WASM32_MACHINE_INST_CALL ||
        function->instructions[i].kind ==
            WASM32_MACHINE_INST_PARAMETER_BIND)
      continue;
    if (!select_instruction(function, &function->instructions[i]))
      return 0;
  }
  return 1;
}

static void collect_instruction_values(
    wasm32_machine_function_t *function,
    ir_inst_t *instruction,
    const wasm32_machine_inst_t *selected) {
  note_value(function, instruction->dst);
  note_value(function, instruction->src1);
  note_value(function, instruction->src2);
  note_value(function, instruction->src3);
  note_value(function, instruction->callee);
  note_value(function, instruction->result_storage);
  if (valid_vreg(function, instruction->dst) && instruction->is_unsigned)
    function->vreg_unsigned[instruction->dst.id] = 1;
  if (instruction->op == IR_LOAD_IMM &&
      instruction->dst.type == IR_TY_I32 &&
      instruction->src1.id == IR_VAL_IMM &&
      instruction->src1.imm > INT32_MAX &&
      valid_vreg(function, instruction->dst)) {
    function->vreg_unsigned[instruction->dst.id] = 1;
  }
  if (instruction->op != IR_CALL) return;
  for (int i = 0; i < selected->call.argument_count; i++)
    note_value(function, selected->call.arguments[i].source);
}

static void collect_function_flags(
    wasm32_machine_function_t *function,
    ir_inst_t *instruction,
    const wasm32_machine_inst_t *selected) {
  if (instruction->op == IR_LABEL || instruction->op == IR_BR ||
      instruction->op == IR_BR_COND)
    function->has_control_flow = 1;
  if (instruction->op == IR_VLA_ALLOC) function->has_vla_alloc = 1;
  if (instruction->op == IR_ATOMIC &&
      instruction->atomic_kind == IR_ATOMIC_CAS) {
    if (instruction->atomic_width == 8)
      function->has_atomic_cas64 = 1;
    else
      function->has_atomic_cas32 = 1;
  }
  if (instruction->op != IR_CALL) return;
  if (selected->call.is_variadic &&
      selected->call.argument_count >
          selected->call.fixed_argument_count)
    function->has_variadic_varargs = 1;
}

static void infer_alloca_value_types(
    wasm32_machine_function_t *function) {
  for (ir_block_t *block = function->source->entry; block;
       block = block->next) {
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next) {
      if (instruction->op == IR_STORE) {
        wasm32_machine_alloca_t *slot =
            find_alloca(function, instruction->src1.id);
        if (slot && slot->value_type == IR_TY_VOID)
          slot->value_type = instruction->src2.type;
      }
      if (instruction->op == IR_LOAD &&
          instruction->dst.type == IR_TY_PTR) {
        const wasm32_machine_alloca_t *slot =
            wasm32_machine_function_alloca(
                function, instruction->src1.id);
        if (slot && slot->value_type == IR_TY_I64 &&
            valid_vreg(function, instruction->dst)) {
          function->vreg_types[instruction->dst.id] = IR_TY_I64;
        }
      }
    }
  }
}

static int propagate_forced_i32(
    wasm32_machine_function_t *function) {
  unsigned char *forced_i32 =
      calloc((size_t)function->vreg_count, 1);
  if (function->vreg_count > 0 && !forced_i32) return 0;
  int changed = 1;
  while (changed) {
    changed = 0;
    for (ir_block_t *block = function->source->entry; block;
         block = block->next) {
      for (ir_inst_t *instruction = block->head; instruction;
           instruction = instruction->next) {
        switch (instruction->op) {
          case IR_ALLOCA:
          case IR_LOAD_STR:
          case IR_LOAD_SYM:
          case IR_LOAD_TLV_ADDR:
            changed |= force_i32(
                function, forced_i32, instruction->dst);
            break;
          case IR_LOAD:
          case IR_STORE:
            changed |= force_i32(
                function, forced_i32, instruction->src1);
            break;
          case IR_ATOMIC:
            changed |= force_i32(
                function, forced_i32, instruction->src1);
            if (instruction->atomic_kind == IR_ATOMIC_CAS)
              changed |= force_i32(
                  function, forced_i32, instruction->src2);
            break;
          case IR_MEMCPY:
            changed |= force_i32(
                function, forced_i32, instruction->src1);
            changed |= force_i32(
                function, forced_i32, instruction->src2);
            break;
          case IR_VLA_ALLOC:
            changed |= force_i32(
                function, forced_i32, instruction->dst);
            changed |= force_i32(
                function, forced_i32, instruction->src1);
            break;
          case IR_VARARG_CURSOR:
            changed |= force_i32(
                function, forced_i32, instruction->dst);
            break;
          case IR_LEA:
            changed |= force_i32(
                function, forced_i32, instruction->dst);
            changed |= force_i32(
                function, forced_i32, instruction->src1);
            break;
          case IR_ALIGN_PTR:
            changed |= force_i32(
                function, forced_i32, instruction->dst);
            changed |= force_i32(
                function, forced_i32, instruction->src1);
            break;
          case IR_CALL: {
            changed |= force_i32(
                function, forced_i32, instruction->callee);
            if (!valid_vreg(function, instruction->dst)) break;
            const wasm32_machine_inst_t *selected =
                wasm32_machine_function_instruction(
                    function, instruction);
            if (!selected ||
                selected->kind != WASM32_MACHINE_INST_CALL) {
              free(forced_i32);
              return 0;
            }
            if (selected->call.signature.result != IR_TY_VOID) {
              if (instruction->dst.type == IR_TY_PTR ||
                  forced_i32[instruction->dst.id]) {
                changed |= force_i32(
                    function, forced_i32, instruction->dst);
              } else if (function->vreg_types[instruction->dst.id] !=
                         selected->call.signature.result) {
                function->vreg_types[instruction->dst.id] =
                    selected->call.signature.result;
                changed = 1;
              }
            }
            break;
          }
          case IR_ADD:
          case IR_SUB:
            if (valid_vreg(function, instruction->dst) &&
                forced_i32[instruction->dst.id]) {
              changed |= force_i32(
                  function, forced_i32, instruction->src1);
              changed |= force_i32(
                  function, forced_i32, instruction->src2);
            }
            if ((valid_vreg(function, instruction->src1) &&
                 forced_i32[instruction->src1.id]) ||
                (valid_vreg(function, instruction->src2) &&
                 forced_i32[instruction->src2.id])) {
              changed |= force_i32(
                  function, forced_i32, instruction->dst);
            }
            break;
          default:
            break;
        }
      }
    }
  }
  free(forced_i32);
  return 1;
}

void wasm32_machine_function_dispose(
    wasm32_machine_function_t *function) {
  if (!function) return;
  wasm32_machine_signature_dispose(&function->signature);
  wasm32_machine_copy_plan_dispose(&function->result_copy);
  for (int i = 0; i < function->instruction_count; i++) {
    wasm32_machine_signature_dispose(
        &function->instructions[i].call.signature);
    wasm32_machine_signature_dispose(
        &function->instructions[i].reference_signature);
    free(function->instructions[i].call.arguments);
    free(function->instructions[i].call.variadic_arguments);
    wasm32_machine_copy_plan_dispose(
        &function->instructions[i].copy);
    for (int piece = 0;
         piece < function->instructions[i].parameter_bind.piece_count;
         piece++) {
      wasm32_machine_copy_plan_dispose(
          &function->instructions[i].parameter_bind.copy_plans[piece]);
    }
    free(function->instructions[i].parameter_bind.pieces);
    free(function->instructions[i].parameter_bind.stores);
    free(function->instructions[i].parameter_bind.copy_plans);
  }
  free(function->vreg_types);
  free(function->vreg_unsigned);
  free(function->vreg_used);
  free(function->instructions);
  free(function->blocks);
  free(function->allocas);
  memset(function, 0, sizeof(*function));
}

int wasm32_machine_function_build(
    const ir_func_t *source,
    const ir_abi_module_t *abi_module,
    wasm32_machine_function_t *function) {
  if (!source || !abi_module || !function || source->next_vreg_id < 0)
    return 0;
  memset(function, 0, sizeof(*function));
  function->source = source;
  function->vreg_count = source->next_vreg_id;
  const ir_abi_signature_t *function_abi =
      ir_abi_function_signature(abi_module, source);
  if (!wasm32_machine_signature_from_abi(
          function_abi, 1, &function->signature))
    return 0;
  function->direct_result_type =
      function_abi->result_count == 1 &&
              function_abi->result_pieces &&
              !function->signature.has_hidden_result
          ? function_abi->result_pieces[0].type
          : IR_TY_VOID;
  function->result_source_size =
      function_abi->result_count == 1 && function_abi->result_pieces
          ? function_abi->result_pieces[0].source_size
          : 0;
  if ((function->signature.has_hidden_result &&
       !wasm32_machine_copy_plan_build(
           function->result_source_size, &function->result_copy)) ||
      (function->signature.has_direct_aggregate_result &&
       !wasm32_machine_select_load(
           function->direct_result_type, 1,
           &function->direct_result_load))) {
    wasm32_machine_function_dispose(function);
    return 0;
  }
  if (function->vreg_count > 0) {
    function->vreg_types =
        malloc((size_t)function->vreg_count * sizeof(*function->vreg_types));
    function->vreg_unsigned = calloc(
        (size_t)function->vreg_count, sizeof(*function->vreg_unsigned));
    function->vreg_used = calloc(
        (size_t)function->vreg_count, sizeof(*function->vreg_used));
    if (!function->vreg_types || !function->vreg_unsigned ||
        !function->vreg_used) {
      wasm32_machine_function_dispose(function);
      return 0;
    }
    for (int i = 0; i < function->vreg_count; i++)
      function->vreg_types[i] = IR_TY_I32;
  }
  if (!collect_allocas(function)) {
    wasm32_machine_function_dispose(function);
    return 0;
  }
  if (!initialize_instructions(
          function, abi_module, function_abi)) {
    wasm32_machine_function_dispose(function);
    return 0;
  }
  int instruction_index = 0;
  for (ir_block_t *block = source->entry; block; block = block->next) {
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next) {
      const wasm32_machine_inst_t *selected =
          &function->instructions[instruction_index++];
      collect_instruction_values(function, instruction, selected);
      collect_function_flags(function, instruction, selected);
    }
  }
  infer_alloca_value_types(function);
  if (!propagate_forced_i32(function)) {
    wasm32_machine_function_dispose(function);
    return 0;
  }
  if (!select_instructions(function)) {
    wasm32_machine_function_dispose(function);
    return 0;
  }
  return 1;
}

ir_type_t wasm32_machine_function_vreg_type(
    const wasm32_machine_function_t *function, ir_val_t value) {
  if (function && valid_vreg(function, value))
    return function->vreg_types[value.id];
  return wasm32_machine_value_type(value.type);
}

int wasm32_machine_function_vreg_is_unsigned(
    const wasm32_machine_function_t *function, ir_val_t value) {
  return function && valid_vreg(function, value) &&
         function->vreg_unsigned[value.id];
}

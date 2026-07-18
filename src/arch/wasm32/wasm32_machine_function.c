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

static void collect_instruction_values(
    wasm32_machine_function_t *function,
    const ir_abi_module_t *abi_module,
    ir_inst_t *instruction) {
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
  size_t argument_count = 0;
  const ir_abi_argument_t *arguments =
      ir_abi_call_arguments(abi_module, instruction, &argument_count);
  for (size_t i = 0; i < argument_count; i++)
    note_value(function, arguments[i].source);
}

static void collect_function_flags(
    wasm32_machine_function_t *function,
    const ir_abi_module_t *abi_module,
    ir_inst_t *instruction) {
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
  const ir_abi_signature_t *abi =
      ir_abi_call_signature(abi_module, instruction);
  size_t argument_count = 0;
  if (abi)
    (void)ir_abi_call_arguments(
        abi_module, instruction, &argument_count);
  if (abi && abi->is_variadic &&
      argument_count > abi->fixed_param_count)
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
    wasm32_machine_function_t *function,
    const ir_abi_module_t *abi_module) {
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
          case IR_VA_ARG_AREA:
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
            const ir_abi_signature_t *abi =
                ir_abi_call_signature(abi_module, instruction);
            wasm32_machine_signature_t signature;
            if (!wasm32_machine_call_signature(
                    instruction, abi, &signature)) {
              free(forced_i32);
              return 0;
            }
            if (signature.result != IR_TY_VOID) {
              if (instruction->dst.type == IR_TY_PTR ||
                  forced_i32[instruction->dst.id]) {
                changed |= force_i32(
                    function, forced_i32, instruction->dst);
              } else if (function->vreg_types[instruction->dst.id] !=
                         signature.result) {
                function->vreg_types[instruction->dst.id] =
                    signature.result;
                changed = 1;
              }
            }
            wasm32_machine_signature_dispose(&signature);
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
  free(function->vreg_types);
  free(function->vreg_unsigned);
  free(function->vreg_used);
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
  for (ir_block_t *block = source->entry; block; block = block->next) {
    for (ir_inst_t *instruction = block->head; instruction;
         instruction = instruction->next) {
      collect_instruction_values(function, abi_module, instruction);
      collect_function_flags(function, abi_module, instruction);
    }
  }
  infer_alloca_value_types(function);
  if (!propagate_forced_i32(function, abi_module)) {
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

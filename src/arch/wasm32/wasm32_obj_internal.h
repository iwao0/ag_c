#ifndef ARCH_WASM32_OBJ_INTERNAL_H
#define ARCH_WASM32_OBJ_INTERNAL_H

#include "wasm32_machine_abi.h"
#include "wasm32_obj.h"
#include "wasm32_obj_buffer.h"

#include <stdio.h>

enum {
  WASM_SEC_TYPE = 1,
  WASM_SEC_IMPORT = 2,
  WASM_SEC_FUNCTION = 3,
  WASM_SEC_CODE = 10,
  WASM_SEC_DATA = 11,
  WASM_SEC_DATACOUNT = 12,

  R_WASM_FUNCTION_INDEX_LEB = 0,
  R_WASM_TABLE_INDEX_SLEB = 1,
  R_WASM_TABLE_INDEX_I32 = 2,
  R_WASM_MEMORY_ADDR_LEB = 3,
  R_WASM_MEMORY_ADDR_I32 = 5,
  R_WASM_TYPE_INDEX_LEB = 6,
  R_WASM_GLOBAL_INDEX_LEB = 7,

  WASM_SYM_FUNCTION = 0,
  WASM_SYM_DATA = 1,
  WASM_SYM_GLOBAL = 2,

  WASM_SYMBOL_BINDING_LOCAL = 0x2,
  WASM_SYMBOL_UNDEFINED = 0x10,
  WASM_SYMBOL_EXPLICIT_NAME = 0x40,

  WASM_SEGMENT_INFO = 5,
  WASM_SYMBOL_TABLE = 8,
};

typedef wasm32_obj_buffer_t wb_t;

#define wb_reserve wasm32_obj_buffer_reserve
#define wb_u8 wasm32_obj_buffer_u8
#define wb_bytes wasm32_obj_buffer_bytes
#define wb_u32le wasm32_obj_buffer_u32le
#define wb_uleb wasm32_obj_buffer_uleb
#define wb_sleb wasm32_obj_buffer_sleb
#define wb_uleb5 wasm32_obj_buffer_uleb5
#define wb_patch_uleb5 wasm32_obj_buffer_patch_uleb5
#define wb_str wasm32_obj_buffer_string
#define emit_section wasm32_obj_buffer_section
#define emit_custom_section wasm32_obj_buffer_custom_section

typedef struct {
  int target_sym;
  int target_is_data;
  int target_is_global;
  int target_is_type;
  uint32_t body_off;
  int type;
  int addend;
} obj_reloc_t;

typedef wasm32_machine_signature_t obj_sig_t;

typedef struct {
  char *name;
  int name_len;
  char *c_signature;
  int c_signature_len;
  obj_sig_t sig;
  int imported;
  int defined;
  int is_static;
  int type_index;
  int func_index;
  int symbol_index;
  wb_t body;
  obj_reloc_t *relocs;
  int reloc_count;
  int reloc_cap;
} obj_func_t;

typedef struct {
  char *name;
  int name_len;
  wb_t bytes;
  size_t alloc_size;
  int align;
  int segment_index;
  int symbol_index;
  int is_static;
  int is_undefined;
  int is_emitted;
  obj_reloc_t *relocs;
  int reloc_count;
  int reloc_cap;
} obj_data_t;

typedef struct {
  char *name;
  int name_len;
  int global_index;
  int symbol_index;
} obj_global_t;

typedef struct {
  FILE *out;
  int capture_output;
  obj_func_t *funcs;
  int func_count;
  int func_cap;
  obj_data_t *data;
  int data_count;
  int data_cap;
  obj_global_t *globals;
  int global_count;
  int global_cap;
  obj_sig_t *types;
  int type_count;
  int type_cap;
  obj_reloc_t *code_relocs;
  int code_reloc_count;
  int code_reloc_cap;
  obj_reloc_t *data_relocs;
  int data_reloc_count;
  int data_reloc_cap;
  int symbol_count;
  int has_indirect_call;
  char *continuation_entry;
  char *continuation_condition;
  char *continuation_step;
  char *continuation_start;
  char *continuation_resume;
  char *continuation_status;
  char *continuation_result;
} obj_ctx_t;

struct wasm32_obj_context_t {
  ag_diagnostic_context_t *diagnostic_context;
  obj_ctx_t obj;
  wb_t capture;
  uint32_t capture_limit;
  int capture_limit_exceeded;
  ir_type_t *emit_local_types;
  unsigned char *emit_local_unsigned;
  int emit_local_count;
  const wasm32_machine_primitive_plan_t *primitives;
};

void wasm32_obj_serialize_sections(
    wasm32_obj_context_t *context, wb_t *output);

#endif

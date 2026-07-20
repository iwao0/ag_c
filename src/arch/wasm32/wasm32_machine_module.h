#ifndef ARCH_WASM32_MACHINE_MODULE_H
#define ARCH_WASM32_MACHINE_MODULE_H

#include "wasm32_machine_function.h"
#include "../../ir/ir_data.h"

typedef struct ir_abi_data_module_t ir_abi_data_module_t;

typedef struct {
  char *name;
  int name_len;
  int offset;
} wasm32_machine_symbol_func_ref_t;

struct wasm32_machine_symbol_t {
  char *name;
  wasm32_machine_symbol_func_ref_t *func_refs;
  int name_len;
  int byte_size;
  int alignment;
  int func_ref_count;
  unsigned char is_extern;
  unsigned char is_static;
  unsigned char is_thread_local;
};

typedef enum {
  WASM32_MACHINE_DATA_OBJECT = 0,
  WASM32_MACHINE_DATA_STRING,
  WASM32_MACHINE_DATA_FLOAT,
} wasm32_machine_data_object_kind_t;

typedef enum {
  WASM32_MACHINE_DATA_RELOC_DATA = 0,
  WASM32_MACHINE_DATA_RELOC_FUNCTION,
} wasm32_machine_data_reloc_kind_t;

typedef struct wasm32_machine_data_object_t
    wasm32_machine_data_object_t;

typedef struct {
  char *target;
  const wasm32_machine_data_object_t *resolved_target;
  wasm32_machine_signature_t function_signature;
  long long addend;
  int target_len;
  int offset;
  int width;
  wasm32_machine_data_reloc_kind_t kind;
  unsigned char has_function_signature;
} wasm32_machine_data_reloc_t;

struct wasm32_machine_data_object_t {
  char *name;
  unsigned char *bytes;
  wasm32_machine_data_reloc_t *relocations;
  int name_len;
  int byte_size;
  int alignment;
  int element_size;
  int relocation_count;
  wasm32_machine_data_object_kind_t kind;
  unsigned char is_extern;
  unsigned char is_static;
  unsigned char is_thread_local;
  unsigned char is_read_only;
  unsigned char has_explicit_initializer;
};

typedef struct {
  wasm32_machine_function_t *functions;
  wasm32_machine_symbol_t *symbols;
  wasm32_machine_data_object_t *data_objects;
  wasm32_machine_primitive_plan_t primitives;
  size_t function_count;
  size_t symbol_count;
  size_t data_object_count;
  unsigned char has_primitive_plan;
} wasm32_machine_module_t;

int wasm32_machine_module_build(
    const ir_module_t *source, const ir_abi_module_t *abi_module,
    wasm32_machine_module_t *module);
int wasm32_machine_module_build_data(
    wasm32_machine_module_t *module,
    const ir_data_module_t *source,
    const ir_abi_data_module_t *data_abi);
void wasm32_machine_module_dispose(wasm32_machine_module_t *module);
const wasm32_machine_function_t *wasm32_machine_module_function(
    const wasm32_machine_module_t *module, size_t index);
const wasm32_machine_symbol_t *wasm32_machine_module_symbol(
    const wasm32_machine_module_t *module,
    const char *name, int name_len);
const wasm32_machine_symbol_func_ref_t *
wasm32_machine_symbol_find_func_ref(
    const wasm32_machine_symbol_t *symbol, int offset);
const wasm32_machine_data_object_t *wasm32_machine_module_data_object(
    const wasm32_machine_module_t *module,
    const char *name, int name_len);
const wasm32_machine_primitive_plan_t *
wasm32_machine_module_primitives(
    const wasm32_machine_module_t *module);

#endif

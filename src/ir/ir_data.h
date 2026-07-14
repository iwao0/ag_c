#ifndef AG_IR_DATA_H
#define AG_IR_DATA_H

#include "ir.h"

typedef enum {
  IR_DATA_OBJECT = 0,
  IR_DATA_STRING,
  IR_DATA_FLOAT,
} ir_data_object_kind_t;

typedef enum {
  IR_DATA_RELOC_DATA = 0,
  IR_DATA_RELOC_FUNCTION,
} ir_data_reloc_kind_t;

typedef struct ir_data_reloc_t {
  struct ir_data_reloc_t *next;
  char *target;
  int target_len;
  int offset;
  int width;
  long long addend;
  ir_data_reloc_kind_t kind;
  ir_callable_sig_t callable_sig;
  unsigned char has_callable_sig;
} ir_data_reloc_t;

typedef struct ir_data_object_t {
  struct ir_data_object_t *next;
  char *name;
  int name_len;
  unsigned char *bytes;
  int byte_size;
  int alignment;
  int element_size;
  ir_data_object_kind_t kind;
  unsigned char is_extern;
  unsigned char is_static;
  unsigned char is_thread_local;
  unsigned char is_read_only;
  unsigned char has_explicit_initializer;
  ir_data_reloc_t *relocs;
  ir_data_reloc_t *relocs_tail;
} ir_data_object_t;

typedef struct {
  ir_data_object_t *objects;
  ir_data_object_t *objects_tail;
} ir_data_module_t;

ir_data_module_t *ir_data_module_new(void);
void ir_data_module_free(ir_data_module_t *module);
ir_data_object_t *ir_data_module_find_object(
    const ir_data_module_t *module, const char *name, int name_len);
ir_data_object_t *ir_data_module_add_object(
    ir_data_module_t *module, const char *name, int name_len,
    ir_data_object_kind_t kind);
int ir_data_object_set_bytes(
    ir_data_object_t *object, const unsigned char *bytes, int byte_size);
ir_data_reloc_t *ir_data_object_add_reloc(
    ir_data_object_t *object, int offset, int width,
    ir_data_reloc_kind_t kind, const char *target, int target_len,
    long long addend, const ir_callable_sig_t *callable_sig);

#endif

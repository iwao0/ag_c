#include "ir_data.h"

#include <stdlib.h>
#include <string.h>

static char *copy_name(const char *name, int name_len) {
  if (!name || name_len <= 0) return NULL;
  char *copy = malloc((size_t)name_len + 1);
  if (!copy) return NULL;
  memcpy(copy, name, (size_t)name_len);
  copy[name_len] = '\0';
  return copy;
}

ir_data_module_t *ir_data_module_new(void) {
  return calloc(1, sizeof(ir_data_module_t));
}

ir_data_object_t *ir_data_module_find_object(
    const ir_data_module_t *module, const char *name, int name_len) {
  if (!module || !name || name_len <= 0) return NULL;
  for (ir_data_object_t *object = module->objects; object;
       object = object->next) {
    if (object->name_len == name_len &&
        memcmp(object->name, name, (size_t)name_len) == 0)
      return object;
  }
  return NULL;
}

ir_data_object_t *ir_data_module_add_object(
    ir_data_module_t *module, const char *name, int name_len,
    ir_data_object_kind_t kind) {
  if (!module || !name || name_len <= 0) return NULL;
  ir_data_object_t *existing =
      ir_data_module_find_object(module, name, name_len);
  if (existing) return existing;
  ir_data_object_t *object = calloc(1, sizeof(*object));
  if (!object) return NULL;
  object->name = copy_name(name, name_len);
  if (!object->name) {
    free(object);
    return NULL;
  }
  object->name_len = name_len;
  object->kind = kind;
  object->alignment = 1;
  object->element_size = 1;
  if (!module->objects) module->objects = object;
  else module->objects_tail->next = object;
  module->objects_tail = object;
  return object;
}

int ir_data_object_set_bytes(
    ir_data_object_t *object, const unsigned char *bytes, int byte_size) {
  if (!object || byte_size < 0 || (byte_size > 0 && !bytes)) return 0;
  unsigned char *copy = NULL;
  if (byte_size > 0) {
    copy = malloc((size_t)byte_size);
    if (!copy) return 0;
    memcpy(copy, bytes, (size_t)byte_size);
  }
  free(object->bytes);
  object->bytes = copy;
  object->byte_size = byte_size;
  return 1;
}

ir_data_reloc_t *ir_data_object_add_reloc(
    ir_data_object_t *object, int offset, int width,
    ir_data_reloc_kind_t kind, const char *target, int target_len,
    long long addend, const ir_callable_sig_t *callable_sig) {
  if (!object || offset < 0 || width <= 0 || !target || target_len <= 0)
    return NULL;
  ir_data_reloc_t *reloc = calloc(1, sizeof(*reloc));
  if (!reloc) return NULL;
  reloc->target = copy_name(target, target_len);
  if (!reloc->target) {
    free(reloc);
    return NULL;
  }
  reloc->target_len = target_len;
  reloc->offset = offset;
  reloc->width = width;
  reloc->kind = kind;
  reloc->addend = addend;
  if (callable_sig) {
    reloc->callable_sig = *callable_sig;
    reloc->has_callable_sig = 1;
  }
  if (!object->relocs) object->relocs = reloc;
  else object->relocs_tail->next = reloc;
  object->relocs_tail = reloc;
  return reloc;
}

void ir_data_module_free(ir_data_module_t *module) {
  if (!module) return;
  for (ir_data_object_t *object = module->objects; object; ) {
    ir_data_object_t *next = object->next;
    for (ir_data_reloc_t *reloc = object->relocs; reloc; ) {
      ir_data_reloc_t *reloc_next = reloc->next;
      free(reloc->target);
      free(reloc);
      reloc = reloc_next;
    }
    free(object->bytes);
    free(object->name);
    free(object);
    object = next;
  }
  free(module);
}

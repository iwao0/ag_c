#include "source_manager.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct ag_source_manager_t {
  const char *current_input;
  const char *current_name;
  char **names;
  size_t name_capacity;
  uint16_t name_count;
};

ag_source_manager_t *ag_source_manager_create(void) {
  return calloc(1, sizeof(ag_source_manager_t));
}

void ag_source_manager_reset_translation_unit(
    ag_source_manager_t *manager) {
  if (!manager) return;
  for (size_t id = 1; id <= manager->name_count; id++) {
    free(manager->names[id]);
  }
  free(manager->names);
  manager->names = NULL;
  manager->name_capacity = 0;
  manager->name_count = 0;
  manager->current_input = NULL;
  manager->current_name = NULL;
}

void ag_source_manager_destroy(ag_source_manager_t *manager) {
  if (!manager) return;
  ag_source_manager_reset_translation_unit(manager);
  free(manager);
}

void ag_source_manager_set_current_input(
    ag_source_manager_t *manager, const char *input) {
  if (manager) manager->current_input = input;
}

const char *ag_source_manager_current_input(
    const ag_source_manager_t *manager) {
  return manager ? manager->current_input : NULL;
}

void ag_source_manager_set_current_name(
    ag_source_manager_t *manager, const char *name) {
  if (manager) manager->current_name = name;
}

const char *ag_source_manager_current_name(
    const ag_source_manager_t *manager) {
  return manager ? manager->current_name : NULL;
}

uint16_t ag_source_manager_intern_name(
    ag_source_manager_t *manager, const char *name) {
  if (!manager || !name || !name[0]) return 0;
  for (size_t id = 1; id <= manager->name_count; id++) {
    if (manager->names[id] == name || !strcmp(manager->names[id], name))
      return id;
  }
  if (manager->name_count == UINT16_MAX) return 0;
  size_t next_id = (size_t)manager->name_count + 1;
  if (next_id >= manager->name_capacity) {
    size_t next_capacity = manager->name_capacity
                               ? manager->name_capacity * 2
                               : 32;
    if (next_capacity <= next_id) next_capacity = next_id + 1;
    if (next_capacity > (size_t)UINT16_MAX + 1)
      next_capacity = (size_t)UINT16_MAX + 1;
    char **next = realloc(
        manager->names, next_capacity * sizeof(*next));
    if (!next) return 0;
    memset(
        next + manager->name_capacity, 0,
        (next_capacity - manager->name_capacity) * sizeof(*next));
    manager->names = next;
    manager->name_capacity = next_capacity;
  }
  size_t len = strlen(name);
  char *copy = malloc(len + 1);
  if (!copy) return 0;
  memcpy(copy, name, len + 1);
  uint16_t id = (uint16_t)next_id;
  manager->name_count = id;
  manager->names[id] = copy;
  return id;
}

const char *ag_source_manager_name(
    const ag_source_manager_t *manager, uint16_t id) {
  if (!manager || id == 0 || id > manager->name_count) return NULL;
  return manager->names[id];
}

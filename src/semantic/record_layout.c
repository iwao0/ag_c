#include "record_layout.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  psx_record_layout_t layout;
  psx_record_member_layout_t *owned_members;
} psx_record_layout_entry_t;

struct psx_record_layout_table_t {
  psx_record_layout_entry_t *entries;
  size_t count;
  size_t capacity;
};

psx_record_layout_table_t *psx_record_layout_table_create(void) {
  return calloc(1, sizeof(psx_record_layout_table_t));
}

void psx_record_layout_table_reset(psx_record_layout_table_t *table) {
  if (!table) return;
  for (size_t i = 0; i < table->count; i++)
    free(table->entries[i].owned_members);
  free(table->entries);
  memset(table, 0, sizeof(*table));
}

void psx_record_layout_table_destroy(psx_record_layout_table_t *table) {
  if (!table) return;
  psx_record_layout_table_reset(table);
  free(table);
}

static psx_record_layout_entry_t *find_entry(
    psx_record_layout_table_t *table, psx_record_id_t record_id,
    const ag_target_info_t *target) {
  const ag_data_layout_t *data_layout = ag_target_info_data_layout(target);
  if (!table || record_id == PSX_RECORD_ID_INVALID || !data_layout)
    return NULL;
  for (size_t i = 0; i < table->count; i++) {
    psx_record_layout_entry_t *entry = &table->entries[i];
    if (entry->layout.record_id == record_id &&
        ag_data_layout_equal(&entry->layout.data_layout, data_layout))
      return entry;
  }
  return NULL;
}

static const psx_record_layout_entry_t *find_entry_const(
    const psx_record_layout_table_t *table, psx_record_id_t record_id,
    const ag_target_info_t *target) {
  const ag_data_layout_t *data_layout = ag_target_info_data_layout(target);
  if (!table || record_id == PSX_RECORD_ID_INVALID || !data_layout)
    return NULL;
  for (size_t i = 0; i < table->count; i++) {
    const psx_record_layout_entry_t *entry = &table->entries[i];
    if (entry->layout.record_id == record_id &&
        ag_data_layout_equal(&entry->layout.data_layout, data_layout))
      return entry;
  }
  return NULL;
}

static psx_record_layout_entry_t *append_entry(
    psx_record_layout_table_t *table) {
  if (table->count == table->capacity) {
    size_t capacity = table->capacity ? table->capacity * 2 : 16;
    if (capacity < table->capacity ||
        capacity > SIZE_MAX / sizeof(*table->entries))
      return NULL;
    psx_record_layout_entry_t *entries = realloc(
        table->entries, capacity * sizeof(*entries));
    if (!entries) return NULL;
    memset(entries + table->capacity, 0,
           (capacity - table->capacity) * sizeof(*entries));
    table->entries = entries;
    table->capacity = capacity;
  }
  return &table->entries[table->count++];
}

int psx_record_layout_table_define(
    psx_record_layout_table_t *table, psx_record_id_t record_id,
    const ag_target_info_t *target, int size, int alignment,
    const psx_record_member_layout_t *members, int member_count) {
  const ag_data_layout_t *data_layout = ag_target_info_data_layout(target);
  if (!table || record_id == PSX_RECORD_ID_INVALID ||
      !ag_data_layout_is_valid(data_layout) || size < 0 ||
      alignment <= 0 || member_count < 0 ||
      (member_count > 0 && !members))
    return 0;
  psx_record_member_layout_t *member_copy = NULL;
  if (member_count > 0) {
    if ((size_t)member_count > SIZE_MAX / sizeof(*member_copy)) return 0;
    member_copy = malloc((size_t)member_count * sizeof(*member_copy));
    if (!member_copy) return 0;
    memcpy(member_copy, members,
           (size_t)member_count * sizeof(*member_copy));
  }
  psx_record_layout_entry_t *entry = find_entry(table, record_id, target);
  if (!entry) entry = append_entry(table);
  if (!entry) {
    free(member_copy);
    return 0;
  }
  free(entry->owned_members);
  entry->owned_members = member_copy;
  entry->layout = (psx_record_layout_t){
      .record_id = record_id,
      .data_layout = *data_layout,
      .size = size,
      .alignment = alignment,
      .member_count = member_count,
      .members = member_copy,
  };
  return 1;
}

const psx_record_layout_t *psx_record_layout_table_lookup(
    const psx_record_layout_table_t *table, psx_record_id_t record_id,
    const ag_target_info_t *target) {
  const psx_record_layout_entry_t *entry = find_entry_const(
      table, record_id, target);
  return entry ? &entry->layout : NULL;
}

const psx_record_member_layout_t *psx_record_layout_member(
    const psx_record_layout_t *layout, int member_index) {
  if (!layout || member_index < 0 || member_index >= layout->member_count)
    return NULL;
  return &layout->members[member_index];
}

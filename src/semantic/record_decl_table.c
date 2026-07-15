#include "record_decl_table.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct psx_record_decl_table_t {
  const psx_record_decl_t **records;
  size_t capacity;
};

psx_record_decl_table_t *psx_record_decl_table_create(void) {
  return calloc(1, sizeof(psx_record_decl_table_t));
}

void psx_record_decl_table_reset(psx_record_decl_table_t *table) {
  if (!table) return;
  free(table->records);
  memset(table, 0, sizeof(*table));
}

void psx_record_decl_table_destroy(psx_record_decl_table_t *table) {
  if (!table) return;
  psx_record_decl_table_reset(table);
  free(table);
}

static int reserve_record_id(
    psx_record_decl_table_t *table, psx_record_id_t record_id) {
  if ((size_t)record_id < table->capacity) return 1;
  size_t capacity = table->capacity ? table->capacity * 2 : 16;
  while (capacity <= (size_t)record_id) {
    if (capacity > SIZE_MAX / 2) return 0;
    capacity *= 2;
  }
  if (capacity > SIZE_MAX / sizeof(*table->records)) return 0;
  const psx_record_decl_t **records = realloc(
      table->records, capacity * sizeof(*records));
  if (!records) return 0;
  memset(records + table->capacity, 0,
         (capacity - table->capacity) * sizeof(*records));
  table->records = records;
  table->capacity = capacity;
  return 1;
}

int psx_record_decl_table_define(
    psx_record_decl_table_t *table, const psx_record_decl_t *record) {
  if (!table || !record || record->record_id == PSX_RECORD_ID_INVALID ||
      (record->record_kind != PSX_TYPE_STRUCT &&
       record->record_kind != PSX_TYPE_UNION))
    return 0;
  if (!reserve_record_id(table, record->record_id)) return 0;
  const psx_record_decl_t *existing = table->records[record->record_id];
  if (existing && existing != record) return 0;
  table->records[record->record_id] = record;
  return 1;
}

const psx_record_decl_t *psx_record_decl_table_lookup(
    const psx_record_decl_table_t *table, psx_record_id_t record_id) {
  if (!table || record_id == PSX_RECORD_ID_INVALID ||
      (size_t)record_id >= table->capacity)
    return NULL;
  return table->records[record_id];
}

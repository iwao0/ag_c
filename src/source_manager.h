#ifndef AG_SOURCE_MANAGER_H
#define AG_SOURCE_MANAGER_H

#include <stdint.h>

typedef struct ag_source_manager_t ag_source_manager_t;

ag_source_manager_t *ag_source_manager_create(void);
void ag_source_manager_destroy(ag_source_manager_t *manager);
void ag_source_manager_reset_translation_unit(
    ag_source_manager_t *manager);

void ag_source_manager_set_current_input(
    ag_source_manager_t *manager, const char *input);
const char *ag_source_manager_current_input(
    const ag_source_manager_t *manager);
void ag_source_manager_set_current_name(
    ag_source_manager_t *manager, const char *name);
const char *ag_source_manager_current_name(
    const ag_source_manager_t *manager);

uint16_t ag_source_manager_intern_name(
    ag_source_manager_t *manager, const char *name);
const char *ag_source_manager_name(
    const ag_source_manager_t *manager, uint16_t id);

#endif

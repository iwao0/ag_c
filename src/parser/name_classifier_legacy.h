#ifndef PARSER_NAME_CLASSIFIER_LEGACY_H
#define PARSER_NAME_CLASSIFIER_LEGACY_H

#include "name_classifier.h"

typedef struct psx_local_registry_t psx_local_registry_t;

typedef struct {
  psx_name_classifier_t source;
  psx_name_classifier_t classifier;
  psx_local_registry_t *local_registry;
} psx_legacy_name_classifier_t;

int psx_legacy_name_classifier_init(
    psx_legacy_name_classifier_t *adapter,
    const psx_name_classifier_t *source,
    psx_local_registry_t *local_registry);
psx_name_classifier_t psx_legacy_name_classifier_view(
    psx_legacy_name_classifier_t *adapter);

#endif

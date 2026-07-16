#ifndef PARSER_NAME_CLASSIFIER_H
#define PARSER_NAME_CLASSIFIER_H

#include "../tokenizer/token.h"

typedef int (*psx_typedef_name_classifier_fn)(
    void *context, const token_t *token);

typedef struct {
  void *context;
  psx_typedef_name_classifier_fn is_typedef_name;
} psx_name_classifier_t;

static inline int ps_name_classifier_is_typedef_name(
    const psx_name_classifier_t *classifier, const token_t *token) {
  return classifier && classifier->is_typedef_name &&
         classifier->is_typedef_name(classifier->context, token);
}

#endif

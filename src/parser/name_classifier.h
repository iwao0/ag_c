#ifndef PARSER_NAME_CLASSIFIER_H
#define PARSER_NAME_CLASSIFIER_H

#include "../tokenizer/token.h"

typedef int (*psx_typedef_name_classifier_fn)(
    void *context, const token_t *token);
typedef void (*psx_name_classifier_declare_fn)(
    void *context, const token_t *token, int is_typedef_name);
typedef void (*psx_name_classifier_scope_fn)(void *context);

typedef struct {
  void *context;
  psx_typedef_name_classifier_fn is_typedef_name;
  psx_name_classifier_declare_fn declare_name;
  psx_name_classifier_scope_fn enter_scope;
  psx_name_classifier_scope_fn leave_scope;
} psx_name_classifier_t;

static inline int ps_name_classifier_is_typedef_name(
    const psx_name_classifier_t *classifier, const token_t *token) {
  return classifier && classifier->is_typedef_name &&
         classifier->is_typedef_name(classifier->context, token);
}

static inline void ps_name_classifier_declare(
    const psx_name_classifier_t *classifier,
    const token_t *token, int is_typedef_name) {
  if (classifier && classifier->declare_name)
    classifier->declare_name(
        classifier->context, token, is_typedef_name);
}

static inline void ps_name_classifier_enter_scope(
    const psx_name_classifier_t *classifier) {
  if (classifier && classifier->enter_scope)
    classifier->enter_scope(classifier->context);
}

static inline void ps_name_classifier_leave_scope(
    const psx_name_classifier_t *classifier) {
  if (classifier && classifier->leave_scope)
    classifier->leave_scope(classifier->context);
}

#endif

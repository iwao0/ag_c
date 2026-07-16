#ifndef PARSER_NAME_CLASSIFIER_H
#define PARSER_NAME_CLASSIFIER_H

#include "../tokenizer/token.h"

typedef int (*psx_typedef_name_classifier_fn)(
    void *context, const token_t *token);
typedef void (*psx_name_classifier_declare_fn)(
    void *context, const token_t *token, int is_typedef_name);
typedef void (*psx_name_classifier_scope_fn)(void *context);
typedef void (*psx_name_classifier_binding_event_fn)(void *context);
typedef void (*psx_name_classifier_reserve_scope_fn)(void *context);
typedef void (*psx_name_classifier_lookup_point_fn)(
    void *context, unsigned *scope_seq,
    unsigned *declaration_seq);

typedef struct {
  void *context;
  psx_typedef_name_classifier_fn is_typedef_name;
  psx_name_classifier_declare_fn declare_name;
  psx_name_classifier_scope_fn enter_scope;
  psx_name_classifier_scope_fn leave_scope;
  psx_name_classifier_binding_event_fn record_binding_event;
  psx_name_classifier_reserve_scope_fn reserve_scope;
  psx_name_classifier_lookup_point_fn capture_lookup_point;
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

static inline int ps_name_classifier_capture_lookup_point(
    const psx_name_classifier_t *classifier,
    unsigned *scope_seq, unsigned *declaration_seq) {
  if (!classifier || !classifier->capture_lookup_point) return 0;
  classifier->capture_lookup_point(
      classifier->context, scope_seq, declaration_seq);
  return 1;
}

static inline void ps_name_classifier_record_binding_event(
    const psx_name_classifier_t *classifier) {
  if (classifier && classifier->record_binding_event)
    classifier->record_binding_event(classifier->context);
}

static inline void ps_name_classifier_reserve_scope(
    const psx_name_classifier_t *classifier) {
  if (classifier && classifier->reserve_scope)
    classifier->reserve_scope(classifier->context);
}

#endif

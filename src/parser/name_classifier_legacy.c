#include "name_classifier_legacy.h"

#include "local_registry.h"
#include "../semantic/scope_graph.h"

static int classify_typedef_name(
    void *context, const token_t *token) {
  psx_legacy_name_classifier_t *adapter = context;
  return ps_name_classifier_is_typedef_name(
      &adapter->source, token);
}

static void declare_name(
    void *context, const token_t *token, int is_typedef_name) {
  psx_legacy_name_classifier_t *adapter = context;
  ps_name_classifier_declare(
      &adapter->source, token, is_typedef_name);
}

static void enter_scope(void *context) {
  psx_legacy_name_classifier_t *adapter = context;
  ps_name_classifier_enter_scope(&adapter->source);
}

static void leave_scope(void *context) {
  psx_legacy_name_classifier_t *adapter = context;
  ps_name_classifier_leave_scope(&adapter->source);
}

static void record_binding_event(void *context) {
  psx_legacy_name_classifier_t *adapter = context;
  ps_name_classifier_record_binding_event(&adapter->source);
}

static void reserve_scope(void *context) {
  psx_legacy_name_classifier_t *adapter = context;
  ps_name_classifier_reserve_scope(&adapter->source);
}

static void capture_lookup_point(
    void *context, unsigned *scope_seq,
    unsigned *declaration_seq) {
  psx_legacy_name_classifier_t *adapter = context;
  if (ps_name_classifier_capture_lookup_point(
          &adapter->source, scope_seq, declaration_seq))
    return;
  if (scope_seq) *scope_seq = PSX_SCOPE_ID_INVALID;
  if (declaration_seq) *declaration_seq = 0;
}

int psx_legacy_name_classifier_init(
    psx_legacy_name_classifier_t *adapter,
    const psx_name_classifier_t *source,
    psx_local_registry_t *local_registry) {
  if (!adapter || !local_registry) return 0;
  *adapter = (psx_legacy_name_classifier_t){
      .source = source ? *source : (psx_name_classifier_t){0},
      .local_registry = local_registry,
  };
  adapter->classifier = (psx_name_classifier_t){
      .context = adapter,
      .is_typedef_name = classify_typedef_name,
      .declare_name = declare_name,
      .enter_scope = enter_scope,
      .leave_scope = leave_scope,
      .record_binding_event = record_binding_event,
      .reserve_scope = reserve_scope,
      .capture_lookup_point = capture_lookup_point,
  };
  return 1;
}

psx_name_classifier_t psx_legacy_name_classifier_view(
    psx_legacy_name_classifier_t *adapter) {
  return adapter ? adapter->classifier : (psx_name_classifier_t){0};
}

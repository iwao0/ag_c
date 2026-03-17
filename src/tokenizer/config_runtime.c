#include "tokenizer.h"

static bool strict_c11_mode = false;
static bool enable_trigraphs = true;
static bool enable_binary_literals = true;
static bool enable_c11_audit_extensions = false;

bool tk_get_strict_c11_mode(void) {
  return strict_c11_mode;
}

void tk_set_strict_c11_mode(bool strict) {
  strict_c11_mode = strict;
}

bool tk_get_enable_trigraphs(void) {
  return enable_trigraphs;
}

void tk_set_enable_trigraphs(bool enable) {
  enable_trigraphs = enable;
}

bool tk_get_enable_binary_literals(void) {
  return enable_binary_literals;
}

void tk_set_enable_binary_literals(bool enable) {
  enable_binary_literals = enable;
}

bool tk_get_enable_c11_audit_extensions(void) {
  return enable_c11_audit_extensions;
}

void tk_set_enable_c11_audit_extensions(bool enable) {
  enable_c11_audit_extensions = enable;
}

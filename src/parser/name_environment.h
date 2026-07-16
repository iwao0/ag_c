#ifndef PARSER_NAME_ENVIRONMENT_H
#define PARSER_NAME_ENVIRONMENT_H

#include "name_classifier.h"

typedef struct psx_parser_name_entry_t psx_parser_name_entry_t;

typedef struct {
  psx_name_classifier_t outer_names;
  psx_parser_name_entry_t *entries;
  unsigned *scope_stack;
  int scope_stack_capacity;
  int scope_depth;
  unsigned current_scope_seq;
  unsigned next_scope_seq;
  unsigned next_declaration_seq;
} psx_parser_name_environment_t;

void ps_parser_name_environment_init(
    psx_parser_name_environment_t *environment,
    psx_name_classifier_t outer_names);
void ps_parser_name_environment_reset(
    psx_parser_name_environment_t *environment,
    psx_name_classifier_t outer_names);
void ps_parser_name_environment_reset_at(
    psx_parser_name_environment_t *environment,
    psx_name_classifier_t outer_names,
    unsigned scope_seq, unsigned next_scope_seq,
    unsigned declaration_seq);
void ps_parser_name_environment_dispose(
    psx_parser_name_environment_t *environment);
psx_name_classifier_t ps_parser_name_environment_classifier(
    psx_parser_name_environment_t *environment);

#endif

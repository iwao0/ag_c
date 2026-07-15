#ifndef PARSER_PARSER_RECOVERY_H
#define PARSER_PARSER_RECOVERY_H

typedef struct psx_parser_runtime_context_t psx_parser_runtime_context_t;

void ps_parser_mark_recoverable_syntax_error_in(
    psx_parser_runtime_context_t *runtime_context);
int ps_parser_has_recoverable_syntax_error_in(
    const psx_parser_runtime_context_t *runtime_context);
void ps_parser_enter_recovery_block_in(
    psx_parser_runtime_context_t *runtime_context);
void ps_parser_leave_recovery_block_in(
    psx_parser_runtime_context_t *runtime_context);

#endif

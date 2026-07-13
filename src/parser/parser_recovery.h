#ifndef PARSER_PARSER_RECOVERY_H
#define PARSER_PARSER_RECOVERY_H

void ps_parser_mark_recoverable_syntax_error(void);
int ps_parser_has_recoverable_syntax_error(void);
void ps_parser_enter_recovery_block(void);
void ps_parser_leave_recovery_block(void);

#endif

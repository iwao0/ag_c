#ifndef TOKENIZER_SCANNER_H
#define TOKENIZER_SCANNER_H

#include <stdbool.h>

char *tk_skip_ignored(char *p, bool *at_bol, bool *has_space, int *line_no);
bool tk_scan_ident_start(const char *p, int *adv);
bool tk_scan_ident_continue(const char *p, int *adv);

#endif

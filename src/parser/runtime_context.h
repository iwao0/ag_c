#ifndef PARSER_RUNTIME_CONTEXT_H
#define PARSER_RUNTIME_CONTEXT_H

#define PSX_PRAGMA_PACK_STACK_MAX 16

typedef struct psx_parser_runtime_context_t {
  int anonymous_tag_seq;
  int pragma_pack_current;
  int pragma_pack_stack[PSX_PRAGMA_PACK_STACK_MAX];
  int pragma_pack_stack_depth;
  int recoverable_syntax_error;
  int function_block_depth;
  int recovery_block_depth;
} psx_parser_runtime_context_t;

psx_parser_runtime_context_t *ps_parser_runtime_context_create(void);
void ps_parser_runtime_context_destroy(psx_parser_runtime_context_t *ctx);
psx_parser_runtime_context_t *ps_parser_runtime_context_activate(
    psx_parser_runtime_context_t *ctx);
psx_parser_runtime_context_t *ps_parser_runtime_context_active(void);
void ps_parser_runtime_context_reset_translation_unit(
    psx_parser_runtime_context_t *ctx);

#endif

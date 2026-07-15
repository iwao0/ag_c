#ifndef PARSER_RUNTIME_CONTEXT_H
#define PARSER_RUNTIME_CONTEXT_H

#define PSX_PRAGMA_PACK_STACK_MAX 16

typedef struct arena_context_t arena_context_t;

typedef struct psx_parser_runtime_context_t {
  arena_context_t *arena_context;
  int anonymous_tag_seq;
  int pragma_pack_current;
  int pragma_pack_stack[PSX_PRAGMA_PACK_STACK_MAX];
  int pragma_pack_stack_depth;
  int recoverable_syntax_error;
  int function_block_depth;
  int recovery_block_depth;
} psx_parser_runtime_context_t;

psx_parser_runtime_context_t *ps_parser_runtime_context_create(
    arena_context_t *arena_context);
void ps_parser_runtime_context_destroy(psx_parser_runtime_context_t *ctx);
arena_context_t *ps_parser_runtime_arena(
    const psx_parser_runtime_context_t *ctx);
void ps_parser_runtime_context_reset_translation_unit(
    psx_parser_runtime_context_t *ctx);

#endif

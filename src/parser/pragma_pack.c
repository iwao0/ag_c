#include "../pragma_pack.h"

#define PACK_STACK_MAX 16

int pragma_pack_current = 0;

static int pack_stack[PACK_STACK_MAX];
static int pack_stack_depth = 0;

void pragma_pack_push(int alignment) {
  if (pack_stack_depth < PACK_STACK_MAX) {
    pack_stack[pack_stack_depth++] = pragma_pack_current;
  }
  pragma_pack_current = alignment;
}

void pragma_pack_pop(void) {
  if (pack_stack_depth > 0) {
    pragma_pack_current = pack_stack[--pack_stack_depth];
  } else {
    pragma_pack_current = 0;
  }
}

void pragma_pack_set(int alignment) {
  pragma_pack_current = alignment;
}

void pragma_pack_reset(void) {
  pragma_pack_current = 0;
}

#include "loop_ctx.h"

static int g_loop_depth = 0;

void psx_loop_reset(void) { g_loop_depth = 0; }

void psx_loop_enter(void) { g_loop_depth++; }

void psx_loop_leave(void) {
  if (g_loop_depth > 0) g_loop_depth--;
}

int psx_loop_depth(void) { return g_loop_depth; }


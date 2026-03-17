#include "loop_ctx.h"

static int g_loop_depth = 0;

void ploop_reset(void) { g_loop_depth = 0; }

void ploop_enter(void) { g_loop_depth++; }

void ploop_leave(void) {
  if (g_loop_depth > 0) g_loop_depth--;
}

int ploop_depth(void) { return g_loop_depth; }


/*
 * A VLA declared by a for-init declaration lives for the whole for
 * statement, is not recreated by continue, and is released on normal exit,
 * break, and goto to an enclosing scope.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define EXTENT_BASE (8 * 1024)
#define OUTER_PASSES 32
#else
#define EXTENT_BASE (2 * 1024 * 1024)
#define OUTER_PASSES 16
#endif

static int evaluate_bound(int *effects, int value) {
  (*effects)++;
  return value;
}

static void check_continue_preserves_for_init_vla(void) {
  for (int outer = 0; outer < OUTER_PASSES; outer++) {
    int effects = 0;
    int iteration = 0;
    int extent = EXTENT_BASE + (outer & 15);
    for (volatile unsigned char bytes[
             evaluate_bound(&effects, extent)];
         iteration < 4; iteration++) {
      assert(effects == 1);
      assert(sizeof(bytes) == (size_t)extent);
      bytes[0] = (unsigned char)(outer + iteration);
      bytes[extent - 1] = (unsigned char)(outer + iteration + 1);
      assert(bytes[0] == (unsigned char)(outer + iteration));
      assert(bytes[extent - 1] ==
             (unsigned char)(outer + iteration + 1));
      if (iteration < 3)
        continue;
    }
    assert(effects == 1);
  }
}

static void check_break_releases_for_init_vla(void) {
  for (int outer = 0; outer < OUTER_PASSES; outer++) {
    int extent = EXTENT_BASE + (outer & 15);
    for (volatile unsigned char bytes[extent];;) {
      bytes[0] = (unsigned char)outer;
      bytes[extent - 1] = (unsigned char)(outer + 1);
      assert(bytes[0] == (unsigned char)outer);
      assert(bytes[extent - 1] == (unsigned char)(outer + 1));
      break;
    }
  }
}

static void check_goto_releases_for_init_vla(void) {
  int outer = 0;
repeat:
  if (outer == OUTER_PASSES)
    return;
  {
    int extent = EXTENT_BASE + (outer & 15);
    for (volatile unsigned char bytes[extent];;) {
      bytes[0] = (unsigned char)(outer + 2);
      bytes[extent - 1] = (unsigned char)(outer + 3);
      assert(bytes[0] == (unsigned char)(outer + 2));
      assert(bytes[extent - 1] == (unsigned char)(outer + 3));
      outer++;
      goto repeat;
    }
  }
}

int main(void) {
  check_continue_preserves_for_init_vla();
  check_break_releases_for_init_vla();
  check_goto_releases_for_init_vla();
  return 0;
}

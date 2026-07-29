/*
 * A VLA local to a switch case must be released on break, continue of an
 * enclosing loop, goto to an enclosing scope, and ordinary switch exit.
 * Restoring the inner dynamic stack must keep an outer VLA alive.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define INNER_EXTENT_BASE (8 * 1024)
#define OUTER_PASSES 64
#else
#define INNER_EXTENT_BASE (2 * 1024 * 1024)
#define OUTER_PASSES 16
#endif

static void touch(
    volatile unsigned char *bytes, int extent, unsigned char seed) {
  bytes[0] = seed;
  bytes[extent - 1] = (unsigned char)(seed + 1);
  assert(bytes[0] == seed);
  assert(bytes[extent - 1] == (unsigned char)(seed + 1));
}

static void check_switch_exit_paths(void) {
  int visits[4] = {0, 0, 0, 0};

  for (int outer = 0; outer < OUTER_PASSES; outer++) {
    int guard_extent = 1024 + (outer & 15);
    volatile unsigned char guard[guard_extent];
    unsigned char guard_seed = (unsigned char)(outer + 17);
    touch(guard, guard_extent, guard_seed);

    int extent = INNER_EXTENT_BASE + (outer & 15);
    switch (outer & 3) {
      case 0: {
        volatile unsigned char bytes[extent];
        touch(bytes, extent, (unsigned char)(outer + 1));
        visits[0]++;
        break;
      }
      case 1: {
        volatile unsigned char bytes[extent];
        touch(bytes, extent, (unsigned char)(outer + 3));
        visits[1]++;
        assert(guard[0] == guard_seed);
        assert(guard[guard_extent - 1] ==
               (unsigned char)(guard_seed + 1));
        continue;
      }
      case 2: {
        volatile unsigned char bytes[extent];
        touch(bytes, extent, (unsigned char)(outer + 5));
        visits[2]++;
        goto after_switch;
      }
      default: {
        volatile unsigned char bytes[extent];
        touch(bytes, extent, (unsigned char)(outer + 7));
        visits[3]++;
      }
    }

after_switch:
    assert(guard[0] == guard_seed);
    assert(guard[guard_extent - 1] ==
           (unsigned char)(guard_seed + 1));
    assert(sizeof(guard) == (size_t)guard_extent);
  }

  for (int index = 0; index < 4; index++)
    assert(visits[index] == OUTER_PASSES / 4);
}

int main(void) {
  check_switch_exit_paths();
  return 0;
}

/*
 * Leaving a scope that owns a VLA releases its storage. Re-entering the same
 * declaration must therefore keep only one instance live instead of consuming
 * stack space cumulatively.
 */
#include <assert.h>
#include <stddef.h>

static void check_loop_scope_release(void) {
  unsigned checksum = 0;

  for (int iteration = 0; iteration < 20000; iteration++) {
    int extent = 4096 + (iteration & 15);
    unsigned char bytes[extent];

    bytes[0] = (unsigned char)iteration;
    bytes[extent - 1] = (unsigned char)(iteration + 1);
    assert(sizeof(bytes) == (size_t)extent);
    checksum += bytes[0] + bytes[extent - 1];
    if ((iteration & 3) == 0)
      continue;
    if (iteration == 19999)
      break;
  }
  assert(checksum != 0);
}

static void check_backward_goto_release(void) {
  int iteration = 0;
  unsigned checksum = 0;

repeat:
  {
    int extent = 4096 + (iteration & 15);
    unsigned char bytes[extent];

    bytes[0] = (unsigned char)(iteration + 3);
    bytes[extent - 1] = (unsigned char)(iteration + 5);
    assert(sizeof(bytes) == (size_t)extent);
    checksum += bytes[0] + bytes[extent - 1];
    iteration++;
    if (iteration < 20000)
      goto repeat;
  }
  assert(checksum != 0);
}

static void check_while_and_do_while_release(void) {
  int iteration = 0;
  unsigned checksum = 0;

  while (iteration < 4000) {
    int extent = 4096 + (iteration & 15);
    unsigned char bytes[extent];

    bytes[0] = (unsigned char)(iteration + 7);
    bytes[extent - 1] = (unsigned char)(iteration + 11);
    checksum += bytes[0] + bytes[extent - 1];
    iteration++;
  }

  iteration = 0;
  do {
    int extent = 4096 + (iteration & 15);
    unsigned char bytes[extent];

    bytes[0] = (unsigned char)(iteration + 13);
    bytes[extent - 1] = (unsigned char)(iteration + 17);
    checksum += bytes[0] + bytes[extent - 1];
    iteration++;
  } while (iteration < 4000);

  assert(checksum != 0);
}

int main(void) {
  check_loop_scope_release();
  check_backward_goto_release();
  check_while_and_do_while_release();
  return 0;
}

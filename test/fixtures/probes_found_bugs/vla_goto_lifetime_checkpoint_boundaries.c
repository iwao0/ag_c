/*
 * A goto target denotes the VLA lifetime state at the label's source
 * position. Checkpoints must neither discard an earlier live VLA nor rely on
 * the label having already executed.
 */
#include <assert.h>
#include <stddef.h>

#ifdef __wasm32__
#define REPEATED_BYTES (8 * 1024)
#else
#define REPEATED_BYTES (64 * 1024)
#endif

static void check_label_between_vlas(void) {
  int first_extent = 4096;
  volatile unsigned char first[first_extent];
  first[0] = 0x35;
  first[first_extent - 1] = 0x79;
  int iteration = 0;

middle:
  ;
  int second_extent = REPEATED_BYTES + (iteration & 15);
  volatile unsigned char second[second_extent];
  second[0] = (unsigned char)iteration;
  second[second_extent - 1] = (unsigned char)(iteration + 1);
  assert(first[0] == 0x35);
  assert(first[first_extent - 1] == 0x79);
  assert(sizeof(second) == (size_t)second_extent);
  if (++iteration < 3)
    goto middle;
}

static void check_skipped_label_checkpoint(void) {
  int extent = 4096;
  volatile unsigned char bytes[extent];
  bytes[0] = 0x27;
  bytes[extent - 1] = 0x63;
  int reached = 0;

  if (!reached)
    goto after;
target:
  assert(reached == 1);
  assert(bytes[0] == 0x27);
  assert(bytes[extent - 1] == 0x63);
  return;
after:
  reached = 1;
  goto target;
}

int main(void) {
  check_label_between_vlas();
  check_skipped_label_checkpoint();
  return 0;
}

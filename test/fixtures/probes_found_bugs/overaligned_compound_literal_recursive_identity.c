// Each active recursive execution needs distinct over-aligned compound literal
// storage, while a goto re-entry in one frame must reuse and reinitialize it.
// Expected: exit=0
#include <assert.h>
#include <stdint.h>

#define FRAME_COUNT 16

struct aligned32_frame {
  _Alignas(32) int depth;
  int round;
  long first;
  long second;
};

union aligned64_frame {
  _Alignas(64) struct {
    int depth;
    int round;
    long marker;
  } state;
  unsigned char bytes[64];
};

_Static_assert(_Alignof(struct aligned32_frame) == 32,
               "recursive struct alignment");
_Static_assert(_Alignof(union aligned64_frame) == 64,
               "recursive union alignment");
_Static_assert(_Alignof(struct aligned32_frame[2]) == 32,
               "recursive array alignment");

static struct aligned32_frame *active_structs[FRAME_COUNT];
static union aligned64_frame *active_unions[FRAME_COUNT];
static struct aligned32_frame *active_arrays[FRAME_COUNT];
static int initializer_effects;

static long initialized_value(int depth, int round, int slot) {
  initializer_effects++;
  return (long)depth * 1000 + round * 100 + slot;
}

static int is_aligned(const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static void check_frame(struct aligned32_frame *record,
                        union aligned64_frame *variant,
                        struct aligned32_frame *array,
                        int depth) {
  assert(record != 0 && variant != 0 && array != 0);
  assert(is_aligned(record, _Alignof(struct aligned32_frame)));
  assert(is_aligned(variant, _Alignof(union aligned64_frame)));
  assert(is_aligned(array, _Alignof(struct aligned32_frame[2])));
  assert(is_aligned(&array[1], _Alignof(struct aligned32_frame)));

  assert(record->depth == depth && record->round == 1);
  assert(record->first == (long)depth * 1000 + 101);
  assert(record->second == (long)depth * 1000 + 102);
  assert(variant->state.depth == depth && variant->state.round == 1);
  assert(variant->state.marker == (long)depth * 1000 + 103);
  assert(array[0].depth == depth && array[0].round == 1);
  assert(array[0].first == (long)depth * 1000 + 104);
  assert(array[0].second == (long)depth * 1000 + 105);
  assert(array[1].depth == depth + 1 && array[1].round == 1);
  assert(array[1].first == (long)depth * 1000 + 106);
  assert(array[1].second == (long)depth * 1000 + 107);
}

static void visit_frame(int depth) {
  int round = 0;
  struct aligned32_frame *first_record = 0;
  union aligned64_frame *first_variant = 0;
  struct aligned32_frame *first_array = 0;
  struct aligned32_frame *record = 0;
  union aligned64_frame *variant = 0;
  struct aligned32_frame *array = 0;

repeat_literals:
  record = &(struct aligned32_frame){
      depth,
      round,
      initialized_value(depth, round, 1),
      initialized_value(depth, round, 2),
  };
  variant = &(union aligned64_frame){
      .state = {
          depth,
          round,
          initialized_value(depth, round, 3),
      },
  };
  array = (struct aligned32_frame[2]){
      {
          depth,
          round,
          initialized_value(depth, round, 4),
          initialized_value(depth, round, 5),
      },
      {
          depth + 1,
          round,
          initialized_value(depth, round, 6),
          initialized_value(depth, round, 7),
      },
  };

  if (round == 0) {
    first_record = record;
    first_variant = variant;
    first_array = array;
    record->depth = -1;
    variant->state.depth = -1;
    array[0].depth = -1;
    round = 1;
    goto repeat_literals;
  }

  assert(record == first_record);
  assert(variant == first_variant);
  assert(array == first_array);
  check_frame(record, variant, array, depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(record != active_structs[ancestor]);
    assert(variant != active_unions[ancestor]);
    assert(array != active_arrays[ancestor]);
    check_frame(active_structs[ancestor], active_unions[ancestor],
                active_arrays[ancestor], ancestor);
  }

  active_structs[depth] = record;
  active_unions[depth] = variant;
  active_arrays[depth] = array;
  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_frame(record, variant, array, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_frame(active_structs[ancestor], active_unions[ancestor],
                active_arrays[ancestor], ancestor);
  active_structs[depth] = 0;
  active_unions[depth] = 0;
  active_arrays[depth] = 0;
}

int main(void) {
  visit_frame(0);
  assert(initializer_effects == FRAME_COUNT * 2 * 7);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_structs[depth] == 0);
    assert(active_unions[depth] == 0);
    assert(active_arrays[depth] == 0);
  }
  return 0;
}

/*
 * Scalar and array compound literals use different value/address lowering
 * paths, but each still denotes one automatic object per active execution of
 * the enclosing block.
 */
#include <assert.h>

#define FRAME_COUNT 24

static int *active_scalars[FRAME_COUNT];
static int *active_arrays[FRAME_COUNT];
static int initializer_effects;

static int initialized_value(int depth, int round, int slot) {
  initializer_effects++;
  return depth * 100 + round * 10 + slot;
}

static void check_values(
    const int *scalar, const int *array, int depth) {
  assert(scalar != 0);
  assert(array != 0);
  assert(scalar != array);
  assert(*scalar == depth * 100 + 10);
  assert(array[0] == depth * 100 + 11);
  assert(array[1] == depth * 100 + 12);
  assert(array[2] == depth * 100 + 13);
}

static void visit_frame(int depth) {
  int round = 0;
  int *first_scalar = 0;
  int *first_array = 0;
  int *scalar = 0;
  int *array = 0;

repeat_literals:
  scalar = &(int){initialized_value(depth, round, 0)};
  array = (int[3]){
      initialized_value(depth, round, 1),
      initialized_value(depth, round, 2),
      initialized_value(depth, round, 3),
  };
  if (round == 0) {
    first_scalar = scalar;
    first_array = array;
    *scalar = -1;
    array[0] = -1;
    round = 1;
    goto repeat_literals;
  }

  assert(scalar == first_scalar);
  assert(array == first_array);
  check_values(scalar, array, depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(scalar != active_scalars[ancestor]);
    assert(array != active_arrays[ancestor]);
    check_values(
        active_scalars[ancestor], active_arrays[ancestor], ancestor);
  }
  active_scalars[depth] = scalar;
  active_arrays[depth] = array;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_values(scalar, array, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_values(
        active_scalars[ancestor], active_arrays[ancestor], ancestor);
  active_scalars[depth] = 0;
  active_arrays[depth] = 0;
}

int main(void) {
  visit_frame(0);
  assert(initializer_effects == FRAME_COUNT * 2 * 4);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_scalars[depth] == 0);
    assert(active_arrays[depth] == 0);
  }
  return 0;
}

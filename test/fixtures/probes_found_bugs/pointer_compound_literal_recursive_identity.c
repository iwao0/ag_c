/*
 * Pointer-valued compound literals are scalar objects with their own storage.
 * Object, function, and pointer-to-array declarators must all reuse that
 * storage within one block execution and keep recursive frames independent.
 */
#include <assert.h>

#define FRAME_COUNT 20

typedef int (*Unary)(int);
typedef int (*RowPointer)[3];

static int object_values[FRAME_COUNT][2];
static int rows[FRAME_COUNT][2][3];
static int **active_objects[FRAME_COUNT];
static Unary *active_functions[FRAME_COUNT];
static RowPointer *active_rows[FRAME_COUNT];
static int object_effects;
static int function_effects;
static int row_effects;

static int add_one(int value) {
  return value + 1;
}

static int add_two(int value) {
  return value + 2;
}

static int *next_object_pointer(int depth, int round) {
  object_effects++;
  return &object_values[depth][round];
}

static Unary next_function_pointer(int round) {
  function_effects++;
  return round == 0 ? add_one : add_two;
}

static RowPointer next_row_pointer(int depth, int round) {
  row_effects++;
  return &rows[depth][round];
}

static void check_values(
    int **object_storage,
    Unary *function_storage,
    RowPointer *row_storage,
    int depth) {
  assert(object_storage != 0);
  assert(function_storage != 0);
  assert(row_storage != 0);
  assert(*object_storage == &object_values[depth][1]);
  assert(**object_storage == depth * 100 + 17);
  assert(*function_storage == add_two);
  assert((*function_storage)(depth * 3) == depth * 3 + 2);
  assert(*row_storage == &rows[depth][1]);
  assert((**row_storage)[0] == depth * 100 + 20);
  assert((**row_storage)[1] == depth * 100 + 21);
  assert((*row_storage)[0][2] == depth * 100 + 22);
}

static void visit_frame(int depth) {
  int round = 0;
  int **first_object = 0;
  Unary *first_function = 0;
  RowPointer *first_row = 0;
  int **object_storage = 0;
  Unary *function_storage = 0;
  RowPointer *row_storage = 0;

repeat_literals:
  object_storage =
      &(int *){next_object_pointer(depth, round)};
  function_storage =
      &(Unary){next_function_pointer(round)};
  row_storage =
      &(RowPointer){next_row_pointer(depth, round)};
  if (round == 0) {
    first_object = object_storage;
    first_function = function_storage;
    first_row = row_storage;
    *object_storage = 0;
    *function_storage = 0;
    *row_storage = 0;
    round = 1;
    goto repeat_literals;
  }

  assert(object_storage == first_object);
  assert(function_storage == first_function);
  assert(row_storage == first_row);
  check_values(object_storage, function_storage, row_storage, depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(object_storage != active_objects[ancestor]);
    assert(function_storage != active_functions[ancestor]);
    assert(row_storage != active_rows[ancestor]);
    check_values(
        active_objects[ancestor],
        active_functions[ancestor],
        active_rows[ancestor],
        ancestor);
  }
  active_objects[depth] = object_storage;
  active_functions[depth] = function_storage;
  active_rows[depth] = row_storage;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_values(object_storage, function_storage, row_storage, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_values(
        active_objects[ancestor],
        active_functions[ancestor],
        active_rows[ancestor],
        ancestor);
  active_objects[depth] = 0;
  active_functions[depth] = 0;
  active_rows[depth] = 0;
}

int main(void) {
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    for (int round = 0; round < 2; round++) {
      object_values[depth][round] =
          depth * 100 + round * 10 + 7;
      for (int element = 0; element < 3; element++)
        rows[depth][round][element] =
            depth * 100 + round * 10 + element + 10;
    }
  }

  visit_frame(0);
  assert(object_effects == FRAME_COUNT * 2);
  assert(function_effects == FRAME_COUNT * 2);
  assert(row_effects == FRAME_COUNT * 2);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_objects[depth] == 0);
    assert(active_functions[depth] == 0);
    assert(active_rows[depth] == 0);
  }
  return 0;
}

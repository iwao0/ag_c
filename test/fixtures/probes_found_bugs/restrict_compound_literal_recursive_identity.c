/*
 * Restrict-qualified object pointer types are valid compound literal types.
 * Direct, typedef, and pointer-to-array declarators must retain one storage
 * object per active block execution without losing their pointee metadata.
 */
#include <assert.h>

#define FRAME_COUNT 20

typedef int *IntPointer;
typedef int (*RowPointer)[3];

static int direct_values[FRAME_COUNT][2];
static int alias_values[FRAME_COUNT][2];
static int rows[FRAME_COUNT][2][3];
static int *restrict *active_direct[FRAME_COUNT];
static restrict IntPointer *active_alias[FRAME_COUNT];
static restrict RowPointer *active_rows[FRAME_COUNT];
static int direct_effects;
static int alias_effects;
static int row_effects;

static int *next_direct(int depth, int round) {
  direct_effects++;
  return &direct_values[depth][round];
}

static IntPointer next_alias(int depth, int round) {
  alias_effects++;
  return &alias_values[depth][round];
}

static RowPointer next_row(int depth, int round) {
  row_effects++;
  return &rows[depth][round];
}

static void check_values(
    int *restrict const *direct_storage,
    restrict IntPointer const *alias_storage,
    restrict RowPointer const *row_storage,
    int depth) {
  assert(direct_storage != 0);
  assert(alias_storage != 0);
  assert(row_storage != 0);
  assert(*direct_storage == &direct_values[depth][1]);
  assert(**direct_storage == depth * 100 + 17);
  assert(*alias_storage == &alias_values[depth][1]);
  assert(**alias_storage == depth * 100 + 27);
  assert(*row_storage == &rows[depth][1]);
  assert((**row_storage)[0] == depth * 100 + 30);
  assert((**row_storage)[1] == depth * 100 + 31);
  assert((**row_storage)[2] == depth * 100 + 32);
}

static void visit_frame(int depth) {
  int round = 0;
  int *restrict *first_direct = 0;
  restrict IntPointer *first_alias = 0;
  restrict RowPointer *first_row = 0;
  int *restrict *direct_storage = 0;
  restrict IntPointer *alias_storage = 0;
  restrict RowPointer *row_storage = 0;

repeat_literals:
  direct_storage =
      &(int *restrict){next_direct(depth, round)};
  alias_storage =
      &(restrict IntPointer){next_alias(depth, round)};
  row_storage =
      &(restrict RowPointer){next_row(depth, round)};
  if (round == 0) {
    first_direct = direct_storage;
    first_alias = alias_storage;
    first_row = row_storage;
    *direct_storage = 0;
    *alias_storage = 0;
    *row_storage = 0;
    round = 1;
    goto repeat_literals;
  }

  assert(direct_storage == first_direct);
  assert(alias_storage == first_alias);
  assert(row_storage == first_row);
  check_values(direct_storage, alias_storage, row_storage, depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(direct_storage != active_direct[ancestor]);
    assert(alias_storage != active_alias[ancestor]);
    assert(row_storage != active_rows[ancestor]);
    check_values(
        active_direct[ancestor],
        active_alias[ancestor],
        active_rows[ancestor],
        ancestor);
  }
  active_direct[depth] = direct_storage;
  active_alias[depth] = alias_storage;
  active_rows[depth] = row_storage;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_values(direct_storage, alias_storage, row_storage, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_values(
        active_direct[ancestor],
        active_alias[ancestor],
        active_rows[ancestor],
        ancestor);
  active_direct[depth] = 0;
  active_alias[depth] = 0;
  active_rows[depth] = 0;
}

int main(void) {
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    for (int round = 0; round < 2; round++) {
      direct_values[depth][round] =
          depth * 100 + round * 10 + 7;
      alias_values[depth][round] =
          depth * 100 + round * 10 + 17;
      for (int element = 0; element < 3; element++)
        rows[depth][round][element] =
            depth * 100 + round * 10 + element + 20;
    }
  }

  visit_frame(0);
  assert(direct_effects == FRAME_COUNT * 2);
  assert(alias_effects == FRAME_COUNT * 2);
  assert(row_effects == FRAME_COUNT * 2);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_direct[depth] == 0);
    assert(active_alias[depth] == 0);
    assert(active_rows[depth] == 0);
  }
  return 0;
}

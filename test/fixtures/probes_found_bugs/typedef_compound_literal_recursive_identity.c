/*
 * Typedef expansion must preserve compound literal storage identity and the
 * complete layout of scalar, record, and multidimensional array aliases in
 * every active recursive frame.
 */
#include <assert.h>

#define FRAME_COUNT 16

typedef unsigned short Count;
typedef Count Counter;

struct Frame {
  Counter depth;
  long value;
};
typedef struct Frame Frame;
typedef Frame FrameAlias;

typedef int Row[3];
typedef Row Matrix[2];

static Counter *active_counters[FRAME_COUNT];
static FrameAlias *active_records[FRAME_COUNT];
static Matrix *active_matrices[FRAME_COUNT];
static int initializer_effects;

static int initialized_value(int depth, int round, int slot) {
  initializer_effects++;
  return depth * 100 + round * 10 + slot;
}

static void check_values(
    const Counter *counter,
    const FrameAlias *record,
    const Matrix *matrix,
    int depth) {
  assert(counter != 0);
  assert(record != 0);
  assert(matrix != 0);
  assert(*counter == (Counter)(depth * 100 + 10));
  assert(record->depth == (Counter)(depth * 100 + 11));
  assert(record->value == (long)(depth * 100 + 12));
  assert((*matrix)[0][0] == depth * 100 + 13);
  assert((*matrix)[0][1] == depth * 100 + 14);
  assert((*matrix)[0][2] == depth * 100 + 15);
  assert((*matrix)[1][0] == depth * 100 + 16);
  assert((*matrix)[1][1] == depth * 100 + 17);
  assert((*matrix)[1][2] == depth * 100 + 18);
  assert(sizeof(*matrix) == 6 * sizeof(int));
}

static void visit_frame(int depth) {
  int round = 0;
  Counter *first_counter = 0;
  FrameAlias *first_record = 0;
  Matrix *first_matrix = 0;
  Counter *counter = 0;
  FrameAlias *record = 0;
  Matrix *matrix = 0;

repeat_literals:
  counter =
      &(Counter){initialized_value(depth, round, 0)};
  record =
      &(FrameAlias){
          initialized_value(depth, round, 1),
          initialized_value(depth, round, 2),
      };
  matrix =
      &(Matrix){
          {
              initialized_value(depth, round, 3),
              initialized_value(depth, round, 4),
              initialized_value(depth, round, 5),
          },
          {
              initialized_value(depth, round, 6),
              initialized_value(depth, round, 7),
              initialized_value(depth, round, 8),
          },
      };
  if (round == 0) {
    first_counter = counter;
    first_record = record;
    first_matrix = matrix;
    *counter = 0;
    record->depth = 0;
    record->value = 0;
    (*matrix)[0][0] = -1;
    (*matrix)[1][2] = -2;
    round = 1;
    goto repeat_literals;
  }

  assert(counter == first_counter);
  assert(record == first_record);
  assert(matrix == first_matrix);
  check_values(counter, record, matrix, depth);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    assert(counter != active_counters[ancestor]);
    assert(record != active_records[ancestor]);
    assert(matrix != active_matrices[ancestor]);
    check_values(
        active_counters[ancestor],
        active_records[ancestor],
        active_matrices[ancestor],
        ancestor);
  }
  active_counters[depth] = counter;
  active_records[depth] = record;
  active_matrices[depth] = matrix;

  if (depth + 1 < FRAME_COUNT)
    visit_frame(depth + 1);

  check_values(counter, record, matrix, depth);
  for (int ancestor = 0; ancestor < depth; ancestor++)
    check_values(
        active_counters[ancestor],
        active_records[ancestor],
        active_matrices[ancestor],
        ancestor);
  active_counters[depth] = 0;
  active_records[depth] = 0;
  active_matrices[depth] = 0;
}

int main(void) {
  visit_frame(0);
  assert(initializer_effects == FRAME_COUNT * 2 * 9);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_counters[depth] == 0);
    assert(active_records[depth] == 0);
    assert(active_matrices[depth] == 0);
  }
  return 0;
}

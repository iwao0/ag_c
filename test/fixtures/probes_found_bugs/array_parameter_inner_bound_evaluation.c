#include <assert.h>

static int bound_evaluations;

static int note_bound(int extent) {
  ++bound_evaluations;
  return extent;
}

static int sum_rows(
    int rows, int columns,
    const int values[rows][note_bound(columns)]) {
  int sum = 0;
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      sum += values[row][column];
    }
  }
  return sum;
}

static int assign_bound_before_body(
    int rows, int columns,
    const int values[rows][(columns = 2)]) {
  return columns + values[1][1];
}

static int comma_bound_once(
    int rows, int columns,
    const int values[rows][(note_bound(columns), columns)]) {
  return values[1][columns - 1];
}

static int pointer_to_runtime_row(
    int columns,
    const int (*values)[note_bound(columns)]) {
  return (*values)[columns - 1];
}

static int multiple_runtime_dimensions(
    int planes, int rows, int columns,
    const int values[planes][note_bound(rows)][note_bound(columns)]) {
  return values[planes - 1][rows - 1][columns - 1];
}

int main(void) {
  int three_columns[2][3] = {
      {1, 2, 3},
      {4, 5, 6},
  };
  int two_columns[2][2] = {
      {1, 2},
      {3, 4},
  };
  int one_row[3] = {7, 8, 9};
  int three_dimensions[2][2][3] = {
      {
          {1, 2, 3},
          {4, 5, 6},
      },
      {
          {7, 8, 9},
          {10, 11, 12},
      },
  };

  assert(bound_evaluations == 0);
  assert(sum_rows(2, 3, three_columns) == 21);
  assert(bound_evaluations == 1);
  assert(sum_rows(2, 3, three_columns) == 21);
  assert(bound_evaluations == 2);
  assert(assign_bound_before_body(2, 7, two_columns) == 6);
  assert(comma_bound_once(2, 3, three_columns) == 6);
  assert(bound_evaluations == 3);
  assert(pointer_to_runtime_row(3, &one_row) == 9);
  assert(bound_evaluations == 4);
  assert(multiple_runtime_dimensions(
             2, 2, 3, three_dimensions) == 12);
  assert(bound_evaluations == 6);
  return 0;
}

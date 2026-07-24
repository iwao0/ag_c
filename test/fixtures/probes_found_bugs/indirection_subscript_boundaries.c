#include <stdatomic.h>
#include <stdbool.h>

struct Incomplete;

enum Index {
  INDEX_ZERO,
  INDEX_ONE,
  INDEX_TWO
};

static int identity(int value) {
  return value;
}

static int read_vla_last(
    int rows, int columns, int (*matrix)[columns]) {
  return matrix[rows - 1][columns - 1];
}

extern int external_values[];

static int read_external_second(void) {
  return external_values[1];
}

int external_values[2] = {13, 17};

int main(void) {
  int values[3] = {4, 7, 9};
  int (*row)[3] = &values;
  int (*function)(int) = identity;
  struct Incomplete *incomplete = 0;
  bool bool_index = true;
  _Atomic int atomic_index = 2;
  int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};

  if (&*incomplete != incomplete)
    return 1;
  if (sizeof(&*incomplete) != sizeof(incomplete))
    return 2;
  if ((*function)(7) != 7)
    return 3;
  if ((*row)[INDEX_ONE] != 7)
    return 4;
  if (INDEX_TWO[values] != 9)
    return 5;
  if (values[bool_index] != 7)
    return 6;
  if (values[atomic_index] != 9)
    return 7;
  if (read_vla_last(2, 3, matrix) != 6)
    return 8;
  if (read_external_second() != 17)
    return 9;
  return 0;
}

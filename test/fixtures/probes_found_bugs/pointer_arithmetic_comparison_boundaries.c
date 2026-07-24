/*
 * Pointer arithmetic scales by the complete pointed-to type, while pointer
 * comparison accepts compatible qualified object pointers, compatible
 * function pointers for equality, and object/void pointer equality.
 */
#include <assert.h>

enum offset {
  OFFSET_TWO = 2
};

struct pair {
  int first;
  int second;
};

struct incomplete;

static int identity(int value) {
  return value;
}

static int successor(int value) {
  return value + 1;
}

static int check_vla(
    int rows, int columns, int matrix[rows][columns]) {
  int (*pointer)[columns] = matrix;
  int index = 0;
  unsigned long row_size = sizeof *(pointer + index++);
  return rows == 2 &&
         index == 1 &&
         row_size == (unsigned long)columns * sizeof(int) &&
         (*(pointer + 1))[columns - 1] == 6;
}

static int check_incomplete_relational_constraint(
    struct incomplete *left, struct incomplete *right) {
  return sizeof(left < right) == sizeof(int);
}

int main(void) {
  int values[5] = {10, 20, 30, 40, 50};
  _Bool one = 1;
  _Atomic int atomic_one = 1;
  int *plain = values;
  const int *qualified = values + 4;
  volatile int *volatile_pointer = values + 3;

  assert(plain[one] == 20);
  assert(*(OFFSET_TWO + plain) == 30);
  assert(*(plain + atomic_one + 2) == 40);
  assert(qualified - plain == 4);
  assert(volatile_pointer - plain == 3);
  assert(plain < qualified && qualified > volatile_pointer);

  struct pair pairs[3] = {{1, 2}, {3, 4}, {5, 6}};
  struct pair *pair_pointer = pairs;
  assert((pair_pointer + 2) - pair_pointer == 2);
  assert((pair_pointer + 1)->second == 4);

  int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
  assert(check_vla(2, 3, matrix));

  _Atomic int atomic_values[3] = {1, 2, 3};
  _Atomic int *atomic_first = atomic_values;
  _Atomic int *atomic_last = atomic_values + 2;
  assert(atomic_last - atomic_first == 2);
  assert(atomic_first < atomic_last);
  assert(*(atomic_first + 1) == 2);

  void *void_pointer = plain;
  const void *const_void_pointer = plain;
  assert(plain == void_pointer);
  assert(const_void_pointer == plain);

  int (*left_function)(int) = identity;
  int (*right_function)(int) = successor;
  assert(left_function != right_function);
  assert(left_function == identity);

  assert(check_incomplete_relational_constraint(0, 0));
  return 0;
}

/* Preserve the const-qualified element type through a function pointer that
 * returns a pointer to an array. */
#include <assert.h>

static const int rows[2][3] = {
    {1, 2, 3},
    {4, 5, 6},
};

static const int (*select_row(int index))[3] {
  return &rows[index];
}

static int sum_row(const int (*row)[3]) {
  return (*row)[0] + (*row)[1] + (*row)[2];
}

int main(void) {
  const int (*(*picker)(int))[3] = select_row;
  assert(sum_row(picker(0)) == 6);
  assert(sum_row(picker(1)) == 15);
  assert((*picker(1))[2] == 6);
  return 0;
}

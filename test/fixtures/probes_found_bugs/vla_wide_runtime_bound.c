#include <assert.h>
#include <stddef.h>

static size_t runtime_row_bytes(
    size_t columns, const int values[1][columns]) {
  (void)values;
  return sizeof values[0];
}

static size_t pointer_runtime_row_bytes(size_t columns) {
  const int (*row)[columns] = (const void *)0;
  return sizeof *row;
}

int main(void) {
  int placeholder = 0;
#ifdef __LP64__
  size_t columns = 2147483648UL;
  assert(runtime_row_bytes(
             columns, (const void *)&placeholder) ==
         8589934592UL);
  assert(pointer_runtime_row_bytes(columns) == 8589934592UL);
#else
  size_t columns = 65537UL;
  assert(runtime_row_bytes(
             columns, (const void *)&placeholder) ==
         262148UL);
  assert(pointer_runtime_row_bytes(columns) == 262148UL);
#endif
  return 0;
}

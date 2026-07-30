// An incomplete array bound and a constant array bound are compatible when
// every corresponding element type is compatible.  The companion TU uses
// complete [3] bounds at the same nested type positions.

int values[3] = {10, 20, 12};

int sum_row(int (*row)[]);
int sum_vla_row(int count, int (*row)[count]);
int (*get_row(void))[];

typedef int incomplete_row_callback_t(int (*)[]);
int apply_row(incomplete_row_callback_t *callback, int (*row)[]);

static int complete_callback(int (*row)[3]) {
  return (*row)[0] + (*row)[1] + (*row)[2];
}

int main(void) {
  if (sum_row(&values) != 42) return 1;
  if (get_row() != &values) return 2;
  if (apply_row(complete_callback, &values) != 42) return 3;
  if (sum_vla_row(3, &values) != 42) return 4;
  return 42;
}

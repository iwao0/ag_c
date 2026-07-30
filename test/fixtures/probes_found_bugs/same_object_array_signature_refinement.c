// A function reference can be emitted while its pointer-to-array parameter or
// return type still has an incomplete bound. A later compatible definition
// with a constant bound must refine the object metadata without conflicting.
// The reverse declaration order and a composite type that completes different
// nested positions must remain compatible as well.

int values[3] = {10, 20, 12};

int sum_row(int (*row)[]);
static int (*saved_sum)(int (*)[]) = sum_row;

int (*get_row(void))[];
static int (*(*saved_get)(void))[] = get_row;

int reverse_sum(int (*row)[3]);
static int (*saved_reverse)(int (*)[3]) = reverse_sum;

int (*exchange_row(int (*row)[]))[3];
static int (*(*saved_exchange)(int (*)[]))[3] = exchange_row;

int sum_row(int (*row)[3]) {
  return (*row)[0] + (*row)[1] + (*row)[2];
}

int (*get_row(void))[3] {
  return &values;
}

int reverse_sum(int (*row)[]) {
  return (*row)[0] + (*row)[1] + (*row)[2];
}

int (*exchange_row(int (*row)[3]))[] {
  return row;
}

int main(void) {
  if (saved_sum(&values) != 42) return 1;
  if (saved_get() != &values) return 2;
  if (saved_reverse(&values) != 42) return 3;
  if (saved_exchange(&values) != &values) return 4;
  return 0;
}

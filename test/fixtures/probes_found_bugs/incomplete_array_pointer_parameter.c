static int values[3] = {1, 2, 3};

static int read_incomplete_array_pointer(int (**pointer)[]);

static int read_incomplete_array_pointer(int (**pointer)[]) {
  return (**pointer)[2];
}

static int read_adjusted_incomplete_array(int (*pointers[])[]) {
  return (*pointers[0])[1];
}

int main(void) {
  int (*row)[] = (int (*)[])values;
  int (*rows[1])[] = {row};
  if (read_incomplete_array_pointer(&row) != 3) return 1;
  if (read_adjusted_incomplete_array(rows) != 2) return 2;
  return 0;
}

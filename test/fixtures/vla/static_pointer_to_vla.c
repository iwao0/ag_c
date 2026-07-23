static int backing[10] = {
    3, 5, 7, 11, 13, 17, 19, 23, 29, 31,
};

static int read_at(int count, int row, int column) {
  static int (*pointer)[count] = 0;
  pointer = (void *)backing;
  return pointer[row][column];
}

int main(void) {
  if (read_at(3, 1, 1) != 13) return 1;
  if (read_at(5, 1, 1) != 19) return 2;
  return 0;
}

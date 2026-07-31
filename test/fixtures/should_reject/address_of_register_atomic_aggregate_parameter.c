/* An atomic aggregate register parameter object is not addressable. */
struct pair {
  int left;
  int right;
};

static int read(register _Atomic(struct pair) value) {
  return &value != 0;
}

int main(void) {
  return read((struct pair){1, 2});
}

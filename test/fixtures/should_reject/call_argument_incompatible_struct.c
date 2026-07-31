/* Function arguments with distinct structure types are incompatible. */
struct first {
  int value;
};

struct second {
  int value;
};

static int read(struct first value) {
  return value.value;
}

int main(void) {
  struct second value = {1};
  return read(value);
}

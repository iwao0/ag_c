/* A structure value is not implicitly convertible to an integer parameter. */
struct value {
  int member;
};

static int read(int value) {
  return value;
}

int main(void) {
  struct value value = {1};
  return read(value);
}

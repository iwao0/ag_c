/* A structure value is not implicitly convertible to an integer return type. */
struct value {
  int member;
};

static int get(void) {
  struct value value = {1};
  return value;
}

int main(void) {
  return get();
}

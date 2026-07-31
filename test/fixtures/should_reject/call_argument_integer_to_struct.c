/* An integer value is not implicitly convertible to a structure parameter. */
struct value {
  int member;
};

static int read(struct value value) {
  return value.member;
}

int main(void) {
  return read(1);
}

/* An integer value is not implicitly convertible to a structure return type. */
struct value {
  int member;
};

static struct value get(void) {
  return 1;
}

int main(void) {
  return get().member;
}

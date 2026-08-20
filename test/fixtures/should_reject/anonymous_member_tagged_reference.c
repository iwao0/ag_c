/* A tagged record reference without a declarator is not anonymous. */
struct Inner {
  int value;
};

struct Outer {
  struct Inner;
};

int main(void) { return 0; }

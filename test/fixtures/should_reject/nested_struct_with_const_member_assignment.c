// The const-member test applies recursively through contained records.
struct Inner {
  const int value;
};
struct Outer {
  struct Inner inner;
};

int main(void) {
  struct Outer left = {{1}};
  struct Outer right = {{2}};
  left = right;
  return left.inner.value;
}

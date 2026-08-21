/* A recursively promoted anonymous member cannot duplicate an outer name. */
struct Outer {
  int value;
  struct {
    struct {
      long value;
    };
  };
};

int main(void) {
  return 0;
}

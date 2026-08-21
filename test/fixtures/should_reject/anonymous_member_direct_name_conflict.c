/* A promoted anonymous member cannot duplicate a direct member name. */
struct Outer {
  int value;
  struct {
    long value;
  };
};

int main(void) {
  return 0;
}

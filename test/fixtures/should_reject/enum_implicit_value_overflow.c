/* An implicitly incremented enumerator must remain representable as int. */
enum Boundary {
  BOUNDARY_MAX = 2147483647,
  BOUNDARY_NEXT
};

int main(void) { return 0; }

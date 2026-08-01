// The canonical C signature must distinguish long double from double even
// when the target lowers both types to the same physical ABI representation.
long double transform_floating_rank(long double value);

int main(void) {
  return transform_floating_rank(7.25L) == 7.5L ? 0 : 1;
}

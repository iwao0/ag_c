/* An unselected generic association cannot contain an incomplete compound literal. */
int main(void) {
  return _Generic(1, int: 0, default: (struct missing){0});
}

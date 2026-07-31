/* An unselected generic association expression must still resolve its identifiers. */
int main(void) {
  return _Generic(1, int: 0, default: undefined_value);
}

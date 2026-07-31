/* An incomplete array type cannot be a generic association type. */
int main(void) {
  return _Generic(1, int[]: 1, default: 0);
}

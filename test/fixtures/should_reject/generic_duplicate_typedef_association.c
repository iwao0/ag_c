/* A typedef name and its underlying type are compatible duplicate associations. */
typedef int integer;

int main(void) {
  return _Generic(1, int: 0, integer: 1);
}

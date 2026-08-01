/* A member row designator preserves the UTF-32 element type constraint. */
struct rows {
  int values[2][3];
};

int main(void) {
  struct rows value = {.values[1] = U"hi"};
  return value.values[1][0];
}
